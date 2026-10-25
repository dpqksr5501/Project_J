#include "Animation/Project_JAnimNode_FootClearance.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Movement/Project_JMovementQueries.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TwoBoneIK.h"

void FProject_JAnimNode_FootClearance::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_SkeletalControlBase::Initialize_AnyThread(Context);
	Legs.Reset();
}

void FProject_JAnimNode_FootClearance::PrepareContact(const UAnimInstance* Instance)
{
	check(IsInGameThread());
	// Keep point/influence storage between evaluations; copying an asset's
	// nested arrays afresh would allocate once per support vertex, per avatar.
	for(auto& Leg:Legs)
	{
		Leg.bActive=Leg.bValid=false;Leg.PointCount=0;
#if WITH_EDITOR
		Leg.DebugRequiredLift=Leg.DebugAppliedLift=-1.f;
#endif
	}
	const auto* Body=Cast<UProject_JRetargetAnimInstance>(Instance);
	auto* Mesh=Instance?Instance->GetSkelMeshComponent():nullptr;
	auto* Pawn=Mesh?Cast<APawn>(Mesh->GetOwner()):nullptr;
	auto* World=Mesh?Mesh->GetWorld():nullptr;
	if(!Body || !Mesh || !Pawn || !World || World->GetNetMode()==NM_DedicatedServer ||
		!Body->bTierAllowsFootIK || Body->CurrentQualityTier==EProject_JAnimBudgetTier::Hidden)return;
	// Physical movement owns contact admission. The main animation snapshot can
	// still describe the airborne frame when this post-process prepares landing.
	const auto Physical=Project_J::Movement::ReadPhysicalFrame(Pawn);
	const auto* Capsule=Project_J::Movement::FindCapsule(Pawn);
	// Descending poses may reach the surface before the capsule lands. This is
	// depenetration only: points already above the plane always produce zero lift.
	const bool bDescending=Physical.Mode==MOVE_Falling && Physical.Velocity.Z<0;
	if(!Physical.bValid || (!Physical.bGrounded && !bDescending) || !Capsule)return;
	const FTransform ToWorld=Mesh->GetComponentTransform();
	const FVector MeshScale=ToWorld.GetScale3D();
	if(MeshScale.X<=0 || !MeshScale.AllComponentsEqual(.001))return;
	TInlineComponentArray<USkeletalMeshComponent*> Parts(Pawn);
	for(const auto* Part:Parts)
	{
		if(!Part->IsRegistered() || !Part->IsVisible() || Part->bHiddenInGame ||
			(Part!=Mesh && Part->LeaderPoseComponent.Get()!=Mesh))continue;
		auto* Asset=Part->GetSkeletalMeshAsset();
		const auto* Data=Asset?Cast<UProject_JFootContactData>(Asset->GetAssetUserDataOfClass(UProject_JFootContactData::StaticClass())):nullptr;
		if(!Data || !Part->GetComponentTransform().Equals(Mesh->GetComponentTransform(),.001))continue;
		for(const auto& Sole:Data->Soles)
		{
			if(Sole.Points.IsEmpty() || Sole.Points.Num()>UProject_JFootContactData::MaxPointsPerLeg ||
				!FMath::IsFinite(Sole.Clearance) || Sole.Clearance<0 || !FMath::IsFinite(Sole.MaxLift) || Sole.MaxLift<=0)continue;
			auto* Leg=Legs.FindByPredicate([&](const auto& L){return L.bActive && L.Data.Foot==Sole.Foot;});
			if(!Leg)
			{
				Leg=Legs.FindByPredicate([](const auto& L){return !L.bActive;});
				if(!Leg)Leg=&Legs.AddDefaulted_GetRef();
				Leg->bActive=true;Leg->Data.Foot=Sole.Foot;Leg->Data.Calf=Sole.Calf;Leg->Data.Thigh=Sole.Thigh;
				Leg->Data.Clearance=Sole.Clearance;Leg->Data.MaxLift=Sole.MaxLift;
			}
			else if(Leg->Data.Thigh==Sole.Thigh && Leg->Data.Calf==Sole.Calf)
			{
				Leg->Data.Clearance=FMath::Max(Leg->Data.Clearance,Sole.Clearance);
				Leg->Data.MaxLift=FMath::Min(Leg->Data.MaxLift,Sole.MaxLift);
			}
			else {Leg->Data.MaxLift=0;continue;}
			if(Leg->PointCount+Sole.Points.Num()>UProject_JFootContactData::MaxPointsPerLeg){Leg->Data.MaxLift=0;continue;}
			for(const auto& Point:Sole.Points)
			{
				if(Leg->Data.Points.Num()<=Leg->PointCount)Leg->Data.Points.AddDefaulted();
				Leg->Data.Points[Leg->PointCount++].Influences=Point.Influences;
			}
		}
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ProjectJFootClearance),false,Pawn);
	TArray<AActor*> Attached;Pawn->GetAttachedActors(Attached);Query.AddIgnoredActors(Attached);
	const double Scale=MeshScale.X;
	const double CapsuleZ=Capsule->GetComponentLocation().Z;
	const double BottomZ=CapsuleZ-Capsule->GetScaledCapsuleHalfHeight();
	for(auto& Leg:Legs)
	{
		if(!Leg.bActive || Leg.PointCount==0 || Leg.Data.MaxLift<=0 || !Mesh->DoesSocketExist(Leg.Data.Foot))continue;
		FVector Start=Mesh->GetSocketLocation(Leg.Data.Foot),End=Start;
		// Keep foot XY, but trace around the physical capsule's support height.
		// A raised foot from the previous airborne pose must not miss the floor
		// just as the new landing pose reaches it.
		Start.Z=CapsuleZ+40*Scale;End.Z=BottomZ-80*Scale;
		FHitResult Hit;
		if(!World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility,Query) ||
			Hit.bStartPenetrating || Hit.ImpactNormal.Z<.5)continue;
		const FVector Point=ToWorld.InverseTransformPosition(Hit.ImpactPoint);
		const FVector Normal=ToWorld.InverseTransformVectorNoScale(Hit.ImpactNormal).GetSafeNormal();
		Leg.Plane=FPlane(Point,Normal);Leg.bValid=true;
	}
}

bool FProject_JAnimNode_FootClearance::IsValidToEvaluate(const USkeleton*,const FBoneContainer& Bones)
{
	auto Resolve=[&](FName Name){FBoneReference R;R.BoneName=Name;R.Initialize(Bones);return R.IsValidToEvaluate(Bones)?R.GetCompactPoseIndex(Bones):FCompactPoseBoneIndex(INDEX_NONE);};
	bool Any=false;
	for(auto& Leg:Legs)
	{
		if(!Leg.bValid)continue;
		Leg.Thigh=Resolve(Leg.Data.Thigh);Leg.Calf=Resolve(Leg.Data.Calf);Leg.Foot=Resolve(Leg.Data.Foot);
		Leg.bValid=Leg.Thigh!=INDEX_NONE && Leg.Calf!=INDEX_NONE && Leg.Foot!=INDEX_NONE;
		if(!Leg.bValid || Bones.GetParentBoneIndex(Leg.Foot)!=Leg.Calf || Bones.GetParentBoneIndex(Leg.Calf)!=Leg.Thigh){Leg.bValid=false;continue;}
		if(Leg.Influences.Num()<Leg.PointCount)Leg.Influences.SetNum(Leg.PointCount);
		for(int32 P=0;P<Leg.PointCount;++P)
		{
			const auto& Point=Leg.Data.Points[P];auto& Indices=Leg.Influences[P];Indices.Reset();float Sum=0;
			for(const auto& Influence:Point.Influences)
			{
				const auto Index=Resolve(Influence.Bone);Indices.Add(Index);
				bool Descendant=false;for(auto Parent=Index;Parent!=INDEX_NONE;Parent=Bones.GetParentBoneIndex(Parent))if(Parent==Leg.Foot){Descendant=true;break;}
				Leg.bValid&=Descendant && !Influence.Position.ContainsNaN() && FMath::IsFinite(Influence.Weight) && Influence.Weight>0;
				Sum+=Influence.Weight;
			}
			Leg.bValid&=FMath::IsNearlyEqual(Sum,1.f,.001f);
		}
		Any|=Leg.bValid;
	}
	return Any;
}

float FProject_JAnimNode_FootClearance::RequiredLift(TConstArrayView<FVector> Positions,const FPlane& Plane,float Clearance,float MaxLift)
{
	float Lift=0;
	for(const FVector& Point:Positions)Lift=FMath::Max(Lift,Clearance-float(Plane.PlaneDot(Point)));
	return FMath::Clamp(Lift,0.f,MaxLift);
}

void FProject_JAnimNode_FootClearance::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& Transforms)
{
	for(auto& Leg:Legs)
	{
		if(!Leg.bValid)continue;
		TArray<FVector,TInlineAllocator<128>> Positions;
		for(int32 P=0;P<Leg.PointCount;++P)
		{
			FVector Position=FVector::ZeroVector;const auto& Point=Leg.Data.Points[P];
			for(int32 I=0;I<Point.Influences.Num();++I)
				Position+=Output.Pose.GetComponentSpaceTransform(Leg.Influences[P][I]).TransformPosition(Point.Influences[I].Position)*Point.Influences[I].Weight;
			Positions.Add(Position);
		}
		const float Lift=RequiredLift(Positions,Leg.Plane,Leg.Data.Clearance,Leg.Data.MaxLift);
#if WITH_EDITOR
		Leg.DebugRequiredLift=RequiredLift(Positions,Leg.Plane,Leg.Data.Clearance,TNumericLimits<float>::Max());
		Leg.DebugAppliedLift=Lift;
#endif
		if(Lift<=UE_SMALL_NUMBER)continue;
		auto Upper=Output.Pose.GetComponentSpaceTransform(Leg.Thigh),Lower=Output.Pose.GetComponentSpaceTransform(Leg.Calf),Foot=Output.Pose.GetComponentSpaceTransform(Leg.Foot);
		const FQuat Rotation=Foot.GetRotation();
		const FVector A=Upper.GetLocation(),B=Lower.GetLocation(),C=Foot.GetLocation();
		const FVector Axis=(C-A).GetSafeNormal();FVector Pole=B-(A+Axis*FVector::DotProduct(B-A,Axis));
		if(!Pole.Normalize())Pole=Upper.GetRotation().GetAxisY();
		AnimationCore::SolveTwoBoneIK(Upper,Lower,Foot,B+Pole*100.,C+Leg.Plane.GetNormal()*Lift,false,1.,1.);
		Foot.SetRotation(Rotation);
		Transforms.Emplace(Leg.Thigh,Upper);Transforms.Emplace(Leg.Calf,Lower);Transforms.Emplace(Leg.Foot,Foot);
	}
	Transforms.Sort([](const auto& A,const auto& B){return A.BoneIndex<B.BoneIndex;});
}

#if WITH_EDITOR
TArray<FProject_JAnimNode_FootClearance::FContactDebug> FProject_JAnimNode_FootClearance::GetContactDebugData() const
{
	TArray<FContactDebug> Result;
	for(const auto& Leg:Legs)if(Leg.bActive)Result.Add({Leg.Data.Foot,Leg.bValid,Leg.DebugRequiredLift,Leg.DebugAppliedLift});
	return Result;
}
#endif
