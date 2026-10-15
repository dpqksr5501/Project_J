#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JReturnedChooserTraversal.h"
#include "Animation/AnimSequence.h"
#include "ObjectChooser_Asset.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJReturnedChooserTraversalTest,
	"ProjectJ.Continuation.ReturnedChooserTraversal", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJReturnedChooserTraversalTest::RunTest(const FString&)
{
	TStrongObjectPtr<UAnimSequence> Animation(NewObject<UAnimSequence>());
	TStrongObjectPtr<UChooserTable> A(NewObject<UChooserTable>()), B(NewObject<UChooserTable>());
	const auto Link = [](UChooserTable* Table, UObject* Next)
	{
		Table->FallbackResult.InitializeAs<FAssetChooser>();
		Table->FallbackResult.GetMutable<FAssetChooser>().Asset = Next;
	};
	FChooserEvaluationContext Context;
	FString Path;
	Link(A.Get(), A.Get());
	const FInstancedStruct Expression = UChooserFunctionLibrary::MakeEvaluateChooser(A.Get());
	TestTrue(TEXT("Engine one-hop returns table itself; no project loop invoked"),
		UChooserFunctionLibrary::EvaluateObjectChooserBase(Context, Expression, UObject::StaticClass()) == A.Get());
	TestNull(TEXT("Self-return terminates without animation"), Project_J::Animation::EvaluateReturnedChooserTables(A.Get(), Context, Path));
	Link(A.Get(), B.Get()); Link(B.Get(), A.Get());
	TestNull(TEXT("Two-table cycle terminates"), Project_J::Animation::EvaluateReturnedChooserTables(A.Get(), Context, Path));
	Link(B.Get(), Animation.Get());
	TestTrue(TEXT("Acyclic chain preserves exact terminal animation"),
		Project_J::Animation::EvaluateReturnedChooserTables(A.Get(), Context, Path) == Animation.Get());
	Link(B.Get(), nullptr);
	TestNull(TEXT("Null terminal remains a miss"), Project_J::Animation::EvaluateReturnedChooserTables(A.Get(), Context, Path));
	TestNull(TEXT("Null root remains a miss"), Project_J::Animation::EvaluateReturnedChooserTables(nullptr, Context, Path));
	TArray<TStrongObjectPtr<UChooserTable>> Chain;
	for (int32 Index = 0; Index < Project_J::Animation::MaxReturnedChooserTables + 1; ++Index)
	{
		Chain.Emplace(NewObject<UChooserTable>());
	}
	for (int32 Index = 0; Index + 1 < Chain.Num(); ++Index) { Link(Chain[Index].Get(), Chain[Index + 1].Get()); }
	Link(Chain.Last().Get(), Animation.Get());
	TestNull(TEXT("Overlong acyclic content terminates"), Project_J::Animation::EvaluateReturnedChooserTables(Chain[0].Get(), Context, Path));
	TestTrue(TEXT("Maximum supported chain preserves its terminal"),
		Project_J::Animation::EvaluateReturnedChooserTables(Chain[1].Get(), Context, Path) == Animation.Get());
	return true;
}
#endif
