#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/AnimClassInterface.h"
#include "Chooser.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "PoseSearch/PoseSearchContext.h"
#include "PoseSearch/PoseSearchResult.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchFeatureChannel.h"
#include "UObject/UnrealType.h"

// Diagnostic reconstruction after evaluation, not a capture of the original
// search query. Includes raw channel costs, not reselect-history filters/bias.
FString UProject_JCharacterAnimInstance::DescribeReconstructedSearchCostsForTesting()
{
	using namespace UE::PoseSearch;
	auto* Anim = this;
	const auto& Proxy = Anim->GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
	const auto* Node = Proxy.GetCapturedMotionMatchingNode();
	const auto* Database = Proxy.GetCurrentActiveDatabase();
	const auto* Interface = IAnimClassInterface::GetFromClass(Anim->GetClass());
	if (!Node || !Database || !Database->Schema || !Interface) return TEXT("Unavailable");
	const IPoseHistory* History = nullptr;
	for (const FStructProperty* Property : Interface->GetAnimNodeProperties())
		if (Property && Property->Struct->IsChildOf(FAnimNode_PoseSearchHistoryCollector_Base::StaticStruct()))
		{
			const auto* Collector = Property->ContainerPtrToValuePtr<FAnimNode_PoseSearchHistoryCollector_Base>(Anim);
			if (const auto* H = Collector->GetPoseHistoryPtr(); H && !H->IsEmpty()) { History = H; break; }
		}
	if (!History) return TEXT("MissingHistory");
	FMemMark Mark(FMemStack::Get());
	FChooserEvaluationContext ChooserContext; ChooserContext.AddObjectParam(Anim);
	FSearchContext Context(0.f, FFloatInterval(0.f, 0.f), FPoseSearchEvent());
	Context.AddRole(Database->Schema->GetDefaultRole(), &ChooserContext, History);
	FSearchResult Continuing; Continuing.InitFrom(Node->GetMotionMatchingState().SearchResult);
	Context.UpdateContinuingPoseSearchResult(Continuing, Continuing);
	const auto Query = Context.GetOrBuildQuery(Database->Schema);
	const auto& Index = Database->GetSearchIndex();
	const auto Channels = Database->Schema->GetChannels();
	// Clip-name groups are diagnostic labels only; they never filter the search.
	constexpr int32 Groups = 4;
	const TCHAR* Labels[Groups] = {TEXT("Loop"), TEXT("Arc"), TEXT("BoxDiamond"), TEXT("Other")};
	float Best[Groups] = {MAX_flt, MAX_flt, MAX_flt, MAX_flt};
	int32 BestPose[Groups] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
	TArray<float> Buffer;
	for (const auto& Asset : Index.Assets)
	{
		const auto* Entry = Database->GetDatabaseAnimationAsset(Asset.GetSourceAssetIdx());
		if (!Entry) continue;
		const FString Name = GetNameSafe(Entry->GetAnimationAsset());
		const int32 Group = Asset.IsLooping() ? 0 : Name.Contains(TEXT("_Arc_")) ? 1 :
			(Name.Contains(TEXT("_Box_")) || Name.Contains(TEXT("_Diamond_"))) ? 2 : 3;
		for (int32 P = Asset.GetFirstPoseIdx(); P < Asset.GetFirstPoseIdx() + Asset.GetNumPoses(); ++P)
		{
			const auto Values = Index.GetPoseValuesSafe(P, Buffer);
			float Cost = Index.PoseMetadata[P].GetCostAddend();
			for (int32 D = 0; D < Query.Num(); ++D) Cost += FMath::Square((Values[D] - Query[D]) * Index.WeightsSqrt[D]);
			if (Cost < Best[Group]) { Best[Group] = Cost; BestPose[Group] = P; }
		}
	}
	const auto& Data = Proxy.GetThreadSafeData();
	FString Result = FString::Printf(TEXT(" Eligible=%d Settled=%d Decel=%d"), Data.MotionMatching.SelectionContext.bUseForwardCycle, Data.MotionMatching.SelectionContext.bUseSettledCycle, Data.Movement.bIsDecelerating);
	for (int32 Group = 0; Group < Groups; ++Group)
	{
		const int32 P = BestPose[Group];
		if (P == INDEX_NONE) continue;
		const auto& Asset = Index.Assets[Index.PoseMetadata[P].GetAssetIndex()];
		const auto Values = Index.GetPoseValuesSafe(P, Buffer);
		Result += FString::Printf(TEXT(" %s=%s@%.3f Total=%.5f"), Labels[Group],
			*GetNameSafe(Database->GetDatabaseAnimationAsset(Asset.GetSourceAssetIdx())->GetAnimationAsset()), Database->GetRealAssetTime(P), Best[Group]);
		for (const auto& Channel : Channels)
		{
			float Cost = 0;
			for (int32 D = Channel->GetChannelDataOffset(); D < Channel->GetChannelDataOffset() + Channel->GetChannelCardinality(); ++D)
				Cost += FMath::Square((Values[D] - Query[D]) * Index.WeightsSqrt[D]);
			Result += FString::Printf(TEXT(" %s=%.5f"), *Channel->GetClass()->GetName(), Cost);
		}
	}
	return Result;
}
#endif
