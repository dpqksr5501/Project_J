// Real Character/ASC/component lifecycle, with a single-node idle animation for
// repeatable allocator load. Actual ABP+GAS attacks are covered by CombatContinuity.
namespace ProjectJAnimationBudgetProbe
{
class FCharacterBudgetComparison : public IAutomationLatentCommand
{
	FAutomationTestBase* Test;
	UWorld* World = nullptr;
	TArray<TWeakObjectPtr<AProject_JNPCCharacter>> Actors;
	TArray<FDelegateHandle> Handles;
	TStrongObjectPtr<USkeletalMesh> MeshAsset;
	TStrongObjectPtr<UAnimMontage> Montage;
	int32 Mode = 0, Frames = 0, Finalizations[2] = {0, 0};
	double Deadline = 0;
	void Cleanup()
	{
		if (!World) { return; }
		for (int32 Index = 0; Index < Actors.Num(); ++Index)
		{
			if (Actors[Index].IsValid())
			{
				Actors[Index]->GetMesh()->UnregisterOnBoneTransformsFinalizedDelegate(Handles[Index]);
				Actors[Index]->Destroy();
			}
		}
		auto* Service = World->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>();
		Test->TestEqual(TEXT("Destroy releases all project registrations"), Service->GetManagedCount(), 0);
		Service->SetEnabledOverride(false);
		Test->TestFalse(TEXT("Feature-owned allocator enable restored after disable"), IAnimationBudgetAllocator::Get(World)->GetEnabled());
		World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World = nullptr;
		Actors.Reset(); Handles.Reset(); MeshAsset.Reset(); Montage.Reset();
	}
public:
	explicit FCharacterBudgetComparison(FAutomationTestBase* InTest) : Test(InTest) {}
	~FCharacterBudgetComparison() { Cleanup(); }
	bool Update() override
	{
		if (!World)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
			World->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>()->SetEnabledOverride(Mode == 1);
			MeshAsset.Reset(LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")));
			Montage.Reset(LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Anim_Assets/Great_Sword/Animations/Sword/Montage/AM_Greatsword_LMB1.AM_Greatsword_LMB1")));
			if (!MeshAsset || !Montage || Montage->SlotAnimTracks.IsEmpty() || Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.IsEmpty())
			{ Test->AddError(TEXT("Missing Character workload assets")); Cleanup(); return true; }
			Frames = 0; Deadline = FPlatformTime::Seconds() + 90;
		}
		if (FPlatformTime::Seconds() > Deadline) { Test->AddError(TEXT("Character budget comparison timed out")); Cleanup(); return true; }
		if (Actors.Num() < 100)
		{
			for (int32 I = 0; I < 5 && Actors.Num() < 100; ++I)
			{
				FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				auto* Actor = World->SpawnActor<AProject_JNPCCharacter>(Params);
				Actor->SetActorEnableCollision(false); Actor->GetCharacterMovement()->DisableMovement();
				auto* Mesh = Cast<UProject_JBudgetedSkeletalMeshComponent>(Actor->GetMesh());
				if (!Mesh) { Actor->Destroy(); Test->AddError(TEXT("Character default mesh was not upgraded")); Cleanup(); return true; }
				Mesh->SetSkeletalMeshAsset(MeshAsset.Get());
				Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				Mesh->bEnableUpdateRateOptimizations = false;
				Mesh->bBudgetTickWhenNotRendered = true;
				Mesh->bSuppressNotifyEventDispatch = true; // idle workload must not dispatch borrowed attack notifies
				Mesh->PlayAnimation(Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference(), true);
				Mesh->GetSingleNodeInstance()->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
				Actors.Add(Actor);
				Handles.Add(Mesh->RegisterOnBoneTransformsFinalizedDelegate(FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateLambda([this]
				{ if (Frames > 120 && Frames <= 240) { ++Finalizations[Mode]; } })));
			}
			World->Tick(LEVELTICK_All, 1.0f / 60.0f); return false;
		}
		++Frames;
		World->Tick(LEVELTICK_All, 1.0f / 60.0f);
		if (Frames == 120) { TRACE_BOOKMARK(TEXT("CharacterABA MeasureBegin Mode=%d Count=100"), Mode); }
		if (Frames < 240) { return false; }
		TRACE_BOOKMARK(TEXT("CharacterABA MeasureEnd Mode=%d Count=100 Finalizations=%d"), Mode, Finalizations[Mode]);
		auto* Service = World->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>();
		Test->TestEqual(TEXT("All 100 actual Characters follow world opt-in"), Service->GetManagedCount(), Mode == 1 ? 100 : 0);
		if (Mode == 1)
		{
			auto* Mesh = CastChecked<UProject_JBudgetedSkeletalMeshComponent>(Actors[0]->GetMesh());
			Mesh->SetCombatCritical(true);
			Test->TestFalse(TEXT("Critical transition unregisters synchronously"), Mesh->IsManagedByBudget());
			Service->Refresh(); Test->TestEqual(TEXT("Only critical actor leaves the budget"), Service->GetManagedCount(), 99);
			Mesh->SetCombatCritical(false); Service->Refresh(); Test->TestEqual(TEXT("Actor rejoins after protection release"), Service->GetManagedCount(), 100);
			Mesh->SetComponentTickEnabled(false); Service->Refresh();
			Test->TestFalse(TEXT("Budget exit preserves externally disabled tick"), Mesh->IsComponentTickEnabled());
			Mesh->SetComponentTickEnabled(true); Service->Refresh();
			Service->SetEnabledOverride(false);
			Test->TestEqual(TEXT("Live feature disable unregisters all meshes"), Service->GetManagedCount(), 0);
			Test->TestFalse(TEXT("Allocator exit restores original URO-off setting"), Mesh->bEnableUpdateRateOptimizations);
			Service->SetEnabledOverride(true);
			Test->TestEqual(TEXT("Live re-enable registers without duplicates"), Service->GetManagedCount(), 100);
			auto* Controller = World->SpawnActor<APlayerController>();
			// Match GameMode's local-controller initialization in this transient world.
			Controller->SetAsLocalPlayerController(); Controller->Possess(Actors[0].Get());
			Service->Refresh(); Test->TestFalse(TEXT("Locally controlled player is excluded"), Mesh->IsManagedByBudget());
			Controller->UnPossess(); Controller->Destroy();
			Service->Refresh(); Test->TestTrue(TEXT("Possession loss reevaluates eligibility"), Mesh->IsManagedByBudget());
		}
		Cleanup();
		if (Mode++ == 0) { return false; }
		Test->TestTrue(TEXT("Baseline produced actual Character poses"), Finalizations[0] > 0);
		Test->TestTrue(TEXT("Low budget reduced optional Character bone finalizations"), Finalizations[1] > 0 && Finalizations[1] < Finalizations[0]);
		Test->AddInfo(FString::Printf(TEXT("CharacterABA Count=100 FramesPerMode=120 Mode0Finalizations=%d Mode1Finalizations=%d; includes interpolation, not evaluation-task count"), Finalizations[0], Finalizations[1]));
		return true;
	}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCharacterBudgetTest, "ProjectJ.GroupB.CharacterBudgetLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCharacterBudgetTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(ProjectJAnimationBudgetProbe::FCharacterBudgetComparison(this)); return true; }
