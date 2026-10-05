#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Combat/Project_JComboDefinition.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "System/Project_JCharacterAnimationBudgetSubsystem.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "UObject/UObjectIterator.h"

namespace ProjectJAuthoredAnimationPerformance
{
class FMeasurement : public IAutomationLatentCommand
{
	FAutomationTestBase* Test;
	UWorld* World = nullptr;
	TArray<TWeakObjectPtr<AProject_JPlayerCharacter>> Players;
	TArray<FDelegateHandle> BoneHandles;
	TArray<double> Samples;
	TArray<TSharedPtr<FJsonValue>> Rows;
	int32 Count = 1, Case = 0, Tick = 0, BoneFrames = 0, MMFrames = 0, Followers = 0;
	int32 Warmup = 120, Measured = 120, Repeats = 2;
	int32 ProtectedFrames = 0, ManagedFrames = 0;
	int32 BurstAbilityTasks = 0;
	double Deadline = 0;
	FString Output;
	bool bFailed = false;
	uint64 Region = 0;
	int32 TierFrames[5] = {};
	int32 Scenario() const { return (Case / 2) % 4; }
	bool Budgeted() const { return Case % 2 != 0; }
	static const TCHAR* Label(int32 I) { return I == 0 ? TEXT("Near") : I == 1 ? TEXT("Far") : I == 2 ? TEXT("Hidden") : TEXT("AttackBurst"); }

	void Cleanup()
	{
		if (Region) { TRACE_END_REGION_WITH_ID(Region); Region = 0; }
		if (World)
		{
			for (int32 I = 0; I < Players.Num(); ++I)
			{ if (Players[I].IsValid() && BoneHandles.IsValidIndex(I)) { Players[I]->GetMesh()->UnregisterOnBoneTransformsFinalizedDelegate(BoneHandles[I]); } }
			World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World = nullptr;
		}
		Players.Reset(); BoneHandles.Reset(); Samples.Reset();
	}
	bool Setup()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
		World->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>()->SetEnabledOverride(Budgeted());
		FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient;
		for (int32 I = 0; I < Count; ++I)
		{
			auto* Controller = World->SpawnActor<AAIController>(Params);
			auto* State = World->SpawnActor<AProject_JPlayerState>(Params);
			if (!Controller || !State) { return false; }
			State->SetOwner(Controller); Controller->SetPlayerState(State);
			auto* Player = ProjectJAuthoredAnimationFixture::Spawn(World, Controller, State, FVector(I * 200, 0, 1000));
			if (!Player) { return false; }
			// Unpossessed authority actors exercise non-local policy without faking a net role.
			Controller->UnPossess();
			Player->SetPlayerState(State); // APawn::UnPossessed clears it; keep fixture runtime ownership.
			Players.Add(Player);
			auto* Mesh = Cast<UProject_JBudgetedSkeletalMeshComponent>(Player->GetMesh());
			if (!Mesh || !Cast<UProject_JCharacterAnimInstance>(Mesh->GetAnimInstance())) { return false; }
			Mesh->bEnableUpdateRateOptimizations = false; // Baseline = no ABA/URO.
			BoneHandles.Add(Mesh->RegisterOnBoneTransformsFinalizedDelegate(FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateLambda([this]
			{ if (Tick >= Warmup) { ++BoneFrames; } })));
			TInlineComponentArray<USkeletalMeshComponent*> Visuals(Player);
			Followers += Visuals.Num() - 1;
			if (auto* Tier = FindFProperty<FFloatProperty>(AProject_JBaseCharacter::StaticClass(), TEXT("CurrentSignificance")))
			{ Tier->SetPropertyValue_InContainer(Player, Scenario() == 1 ? 2.0f : 0.0f); }
		}
		Tick = BoneFrames = MMFrames = ProtectedFrames = ManagedFrames = BurstAbilityTasks = 0;
		for (int32& Value : TierFrames) { Value = 0; }
		Deadline = FPlatformTime::Seconds() + 180;
		return true;
	}
	bool Attack()
	{
		for (const auto& Entry : Players)
		{
			auto* Player = Entry.Get(); auto* ASC = Player->GetAbilitySystemComponent();
			if (!ASC) { return false; }
			ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
			Player->FindComponentByClass<UProject_JSkillInputExecutionComponent>()->HandleInputTagPressed(FProject_JGameplayTags::Get().InputTag_Weapon_LMB);
			if (!Player->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying()) { return false; }
			Test->TestFalse(TEXT("Burst montage immediately leaves ABA"), CastChecked<UProject_JBudgetedSkeletalMeshComponent>(Player->GetMesh())->IsManagedByBudget());
		}
		for (TObjectIterator<UAbilityTask> It; It; ++It)
		{ if (It->GetWorld() == World && It->GetState() == EGameplayTaskState::Active) { ++BurstAbilityTasks; } }
		return true;
	}
	void FinishCase()
	{
		Samples.Sort(); double Sum = 0; for (double Sample : Samples) { Sum += Sample; }
		auto Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("count"), Count); Row->SetBoolField(TEXT("aba"), Budgeted());
		Row->SetStringField(TEXT("scenario"), Label(Scenario())); Row->SetNumberField(TEXT("repeat"), Case / 8);
		Row->SetNumberField(TEXT("mean_world_tick_ms"), Sum / Samples.Num());
		Row->SetNumberField(TEXT("p50_world_tick_ms"), Samples[Samples.Num() / 2]);
		Row->SetNumberField(TEXT("p95_world_tick_ms"), Samples[FMath::CeilToInt(Samples.Num() * .95) - 1]);
		Row->SetNumberField(TEXT("max_world_tick_ms"), Samples.Last());
		Row->SetNumberField(TEXT("measured_ticks"), Samples.Num()); Row->SetNumberField(TEXT("warmup_ticks"), Warmup);
		Row->SetNumberField(TEXT("bone_finalizations"), BoneFrames); Row->SetNumberField(TEXT("mm_result_observations"), MMFrames);
		Row->SetNumberField(TEXT("managed_mesh_ticks"), ManagedFrames); Row->SetNumberField(TEXT("combat_protected_ticks"), ProtectedFrames);
		Row->SetNumberField(TEXT("active_ability_tasks_at_burst"), BurstAbilityTasks);
		Row->SetNumberField(TEXT("authored_follower_components"), Followers);
		TArray<TSharedPtr<FJsonValue>> Tiers; for (int32 Value : TierFrames) { Tiers.Add(MakeShared<FJsonValueNumber>(Value)); }
		Row->SetArrayField(TEXT("policy_tier_mesh_ticks_local_near_mid_far_hidden"), Tiers);
		Rows.Add(MakeShared<FJsonValueObject>(Row));
		Test->AddInfo(FString::Printf(TEXT("AuthoredCPU Count=%d Scenario=%s ABA=%d Repeat=%d Mean=%.3fms P95=%.3fms Bones=%d MM=%d Followers=%d"),
			Count, Label(Scenario()), Budgeted(), Case / 8, Sum / Samples.Num(), Samples[FMath::CeilToInt(Samples.Num() * .95) - 1], BoneFrames, MMFrames, Followers));
		Test->TestTrue(TEXT("Authored workload actually finalized bones"), BoneFrames > 0);
		if (Scenario() == 0) { Test->TestTrue(TEXT("Authored near graph produced a real MM result"), MMFrames > 0); }
		if (Scenario() < 3) { Test->TestTrue(TEXT("Fixture exercised its named policy tier"), TierFrames[Scenario() == 0 ? 1 : Scenario() == 1 ? 3 : 4] > 0); }
		if (Scenario() == 3) { Test->TestTrue(TEXT("Attack burst retained gameplay pose demand"), ProtectedFrames > 0); }
		Cleanup(); ++Case; Followers = 0;
	}
	bool Save()
	{
		auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("completed"), !bFailed);
		Result->SetStringField(TEXT("scope"), TEXT("NullRHI CPU: real authored Blueprint, equipment, ABP and constructed followers; controlled movement and simulated render timestamps/significance. World tick wall time includes task completion, excludes outer engine frame/rendering. Bone finalizations include interpolation, MM observations are not search counts."));
		Result->SetArrayField(TEXT("rows"), Rows);
		FString Json; FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Json));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
		return FFileHelper::SaveStringToFile(Json, *Output);
	}
public:
	explicit FMeasurement(FAutomationTestBase* InTest) : Test(InTest)
	{
		FParse::Value(FCommandLine::Get(), TEXT("ProjectJAnimationCount="), Count); Count = FMath::Clamp(Count, 1, 200);
		FParse::Value(FCommandLine::Get(), TEXT("ProjectJAnimationRepeats="), Repeats); Repeats = FMath::Clamp(Repeats, 1, 5);
		FParse::Value(FCommandLine::Get(), TEXT("ProjectJAnimationOutput="), Output);
		if (Output.IsEmpty()) { Output = FPaths::ProjectSavedDir() / TEXT("Profiling/AuthoredAnimation") / FGuid::NewGuid().ToString() / TEXT("cpu.json"); }
	}
	virtual bool Update() override
	{
		auto* EngineBudget = IConsoleManager::Get().FindConsoleVariable(TEXT("a.Budget.Enabled"));
		if (!EngineBudget || EngineBudget->GetInt() != 1 || (!World && !Setup()))
		{ Test->AddError(TEXT("Authored CPU fixture needs a.Budget.Enabled=1 and valid authored character/equipment/ABP.")); bFailed = true; Cleanup(); Save(); return true; }
		if (FPlatformTime::Seconds() > Deadline)
		{ Test->AddError(TEXT("Authored CPU case exceeded bounded lifetime")); bFailed = true; Cleanup(); Save(); return true; }
		if (Scenario() == 3 && Tick == Warmup && !Attack())
		{ Test->AddError(TEXT("Production LMB failed to start simultaneous authored attacks")); bFailed = true; Cleanup(); Save(); return true; }
		if (Tick == Warmup)
		{
			const FString Name = FString::Printf(TEXT("ProjectJ.AuthoredCPU_%d_%s_ABA%d_R%d"), Count, Label(Scenario()), Budgeted(), Case / 8);
			Region = TRACE_BEGIN_REGION_WITH_ID(*Name);
		}
		for (const auto& Entry : Players)
		{
			auto* Player = Entry.Get();
			Player->GetCharacterMovement()->Velocity = Scenario() == 3 ? FVector::ZeroVector : FVector(200, 0, 0);
			Player->AddActorWorldOffset(Player->GetCharacterMovement()->Velocity / 60.0f, false);
			TInlineComponentArray<USkeletalMeshComponent*> Visuals(Player);
			for (auto* Mesh : Visuals) { Mesh->SetLastRenderTime(Scenario() == 2 ? -1000.0f : World->GetTimeSeconds()); }
		}
		const double Begin = FPlatformTime::Seconds();
		{ TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_AuthoredAnimation_WorldTick); World->Tick(LEVELTICK_All, 1.0f / 60.0f); }
		const double Milliseconds = (FPlatformTime::Seconds() - Begin) * 1000;
		if (Tick >= Warmup)
		{
			Samples.Add(Milliseconds);
			for (const auto& Entry : Players)
			{
				auto* Mesh = CastChecked<UProject_JBudgetedSkeletalMeshComponent>(Entry->GetMesh());
				ManagedFrames += Mesh->IsManagedByBudget(); ProtectedFrames += Mesh->IsCombatCritical();
				auto* Anim = CastChecked<UProject_JCharacterAnimInstance>(Mesh->GetAnimInstance());
				MMFrames += !Anim->GetThreadSafeMotionMatchingSelectedAnimation().IsNone();
				++TierFrames[FMath::Clamp(int32(Anim->GetCurrentOptimizationPolicy().Tier), 0, 4)];
			}
		}
		if (++Tick < Warmup + Measured) { return false; }
		FinishCase();
		if (Case < 8 * Repeats) { return false; }
		Test->TestTrue(TEXT("Saved reproducible CPU measurement"), Save()); return true;
	}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAuthoredAnimationCPU, "ProjectJ.Animation.AuthoredCPU", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FProjectJAuthoredAnimationCPU::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(ProjectJAuthoredAnimationPerformance::FMeasurement(this)); return true; }
#endif
