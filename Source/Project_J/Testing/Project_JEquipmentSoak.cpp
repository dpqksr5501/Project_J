// Editor-only, opt-in load fixture. No map, Blueprint or shared asset is modified.
#if WITH_EDITOR
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JPlayerState.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJEquipmentSoak, Log, All);
TRACE_DECLARE_INT_COUNTER(SoakActors, TEXT("ProjectJ/EquipmentSoak/Actors"));
TRACE_DECLARE_INT_COUNTER(SoakEquipped, TEXT("ProjectJ/EquipmentSoak/Equipped"));
TRACE_DECLARE_INT_COUNTER(SoakAttacking, TEXT("ProjectJ/EquipmentSoak/Attacking"));
TRACE_DECLARE_INT_COUNTER(SoakCycles, TEXT("ProjectJ/EquipmentSoak/CompletedActorCycles"));
TRACE_DECLARE_INT_COUNTER(SoakFailures, TEXT("ProjectJ/EquipmentSoak/Failures"));
TRACE_DECLARE_INT_COUNTER(SoakPhase, TEXT("ProjectJ/EquipmentSoak/Phase"));
TRACE_DECLARE_FLOAT_COUNTER(SoakFrameMs, TEXT("ProjectJ/EquipmentSoak/WorldTickIntervalMs"));
TRACE_DECLARE_FLOAT_COUNTER(SoakDriverMs, TEXT("ProjectJ/EquipmentSoak/DriverMs"));
TRACE_DECLARE_FLOAT_COUNTER(SoakPhaseMs, TEXT("ProjectJ/EquipmentSoak/PhaseElapsedMs"));
TRACE_DECLARE_FLOAT_COUNTER(SoakMemoryMiB, TEXT("ProjectJ/EquipmentSoak/ProcessPhysicalMiB"));

namespace ProjectJEquipmentSoak
{
constexpr int32 Population = 100;
enum class EPhase : uint8 { Spawn, Warmup, Equip, Attack, Unequip, Cooldown, Finished };
const TCHAR* PhaseName(EPhase Phase)
{
 static const TCHAR* Names[] = { TEXT("Spawn"), TEXT("Warmup"), TEXT("Equip"), TEXT("Attack"), TEXT("Unequip"), TEXT("Cooldown"), TEXT("Finished") };
 return Names[uint8(Phase)];
}

struct FBot
{
 TWeakObjectPtr<AProject_JGreatswordCharacter> Pawn;
 TWeakObjectPtr<AProject_JPlayerState> State;
 TWeakObjectPtr<AAIController> Controller;
 FVector Origin;
 int32 BaselineAbilityCount = 0;
};

// All fixture state and UObject access stays on the game thread. Engine animation,
// rendering and streaming retain their normal task ownership. This is not 100 clients.
struct FRun
{
 TWeakObjectPtr<UWorld> World;
 TStrongObjectPtr<UClass> VisualTemplate;
 TStrongObjectPtr<UProject_JEquipmentItemDefinition> Item;
 TArray<FBot> Bots;
 EPhase Phase = EPhase::Spawn;
 int32 Repetitions = 10, Cycle = 0, Completed = 0, Failures = 0, PhaseTicks = 0;
 bool bFinished = false, bPassed = false, bOwnTrace = false, bInTick = false;
 double Started = 0, PhaseStarted = 0, LastTick = 0, LastMemorySample = 0;
 FVector Origin;
 FString Directory, TraceDestination, Rows = TEXT("cycle,phase,elapsed_ms,completed_actor_cycles,failures\n");
 FDelegateHandle TickHandle, CleanupHandle;

 bool Start(UWorld* InWorld, int32 InRepetitions, FVector InOrigin)
 {
  check(IsInGameThread());
  if (!InWorld || !InWorld->IsGameWorld() || InWorld->GetNetMode() != NM_Standalone || InWorld->bIsTearingDown)
  {
   UE_LOG(LogProjectJEquipmentSoak, Error, TEXT("Use a Standalone PIE/game world. This fixture does not emulate client connections."));
   return false;
  }
  World = InWorld; Repetitions = FMath::Clamp(InRepetitions, 1, 100); Origin = InOrigin;
  Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Profiling/EquipmentSoak100") /
   (FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")) + TEXT("_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
  IFileManager::Get().MakeDirectory(*Directory, true);
  constexpr const TCHAR* Channels = TEXT("cpu,frame,bookmark,counters,task,log,gpu,loadtime,file");
  if (FTraceAuxiliary::IsConnected())
  {
   if (FTraceAuxiliary::IsPaused())
   { UE_LOG(LogProjectJEquipmentSoak, Error, TEXT("Existing trace is paused. Resume it or stop it before starting the fixture.")); return false; }
   // Never stop or redirect somebody else's trace session.
   FTraceAuxiliary::EnableChannels(Channels);
   TraceDestination = FTraceAuxiliary::GetTraceDestinationString();
  }
  else
  {
   TraceDestination = Directory / TEXT("EquipmentSoak100.utrace");
   FTraceAuxiliary::FOptions Options; Options.bExcludeTail = true;
   bOwnTrace = FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::File, *TraceDestination, Channels, &Options);
   if (!bOwnTrace) { UE_LOG(LogProjectJEquipmentSoak, Error, TEXT("Cannot start trace: %s"), *TraceDestination); return false; }
  }
  // SET suppresses unchanged values: ALWAYS is necessary for a zero-failure run
  // to contain a Failures counter at all, including repeated traces in one editor.
  TRACE_COUNTER_SET_ALWAYS(SoakActors, 0); TRACE_COUNTER_SET_ALWAYS(SoakEquipped, 0); TRACE_COUNTER_SET_ALWAYS(SoakAttacking, 0);
  TRACE_COUNTER_SET_ALWAYS(SoakCycles, 0); TRACE_COUNTER_SET_ALWAYS(SoakFailures, 0); TRACE_COUNTER_SET_ALWAYS(SoakPhase, 0);
  TRACE_BOOKMARK(TEXT("EquipmentSoak100 Setup cycles=%d directory=%s"), Repetitions, *Directory);
  {
   TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_EquipmentSoak_LoadFixtureAssets);
   // Deliberate setup preload. Subsequent repetitions are shared-asset/warm-cache tests.
   VisualTemplate.Reset(LoadClass<AProject_JPlayerCharacter>(nullptr, TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_GreatSword_C")));
   Item.Reset(LoadObject<UProject_JEquipmentItemDefinition>(nullptr, TEXT("/Game/DataAssetSets/Animation_Profiles/Equip/DA_Greatsword_Equip.DA_Greatsword_Equip")));
  }
  Started = PhaseStarted = LastTick = FPlatformTime::Seconds();
  if (!VisualTemplate.IsValid() || !Item.IsValid() || !Item->CombatStyleDefinition || !Item->WeaponPresentationProfile)
  { Fail(TEXT("Missing fixture Blueprint/equipment/combat style/weapon presentation")); return false; }
  if (!Item->EquipmentMesh.IsNull())
  { Fail(TEXT("This weapon-actor fixture expects EquipmentMesh empty; use a separate soft-mesh loading test")); return false; }
  TickHandle = FWorldDelegates::OnWorldPostActorTick.AddRaw(this, &FRun::Tick);
  CleanupHandle = FWorldDelegates::OnWorldCleanup.AddRaw(this, &FRun::OnWorldCleanup);
  UE_LOG(LogProjectJEquipmentSoak, Display, TEXT("Started: actors=100 cycles=%d mode=Standalone warm/shared asset; report=%s trace=%s"), Repetitions, *Directory, *TraceDestination);
  return true;
 }

 void RecordPhase()
 {
  Rows += FString::Printf(TEXT("%d,%s,%.3f,%d,%d\n"), Cycle, PhaseName(Phase),
   (FPlatformTime::Seconds() - PhaseStarted) * 1000, Completed, Failures);
 }
 void Enter(EPhase Next)
 {
  RecordPhase(); Phase = Next; PhaseStarted = FPlatformTime::Seconds(); PhaseTicks = 0;
  TRACE_COUNTER_SET(SoakPhase, int64(Phase));
  TRACE_BOOKMARK(TEXT("EquipmentSoak100 cycle=%d phase=%s"), Cycle, PhaseName(Phase));
 }
 void Fail(const TCHAR* Reason, int32 Bot = INDEX_NONE)
 {
  ++Failures; TRACE_COUNTER_SET(SoakFailures, Failures);
  TRACE_BOOKMARK(TEXT("EquipmentSoak100 FAILURE cycle=%d phase=%s bot=%d reason=%s"), Cycle, PhaseName(Phase), Bot, Reason);
  UE_LOG(LogProjectJEquipmentSoak, Error, TEXT("cycle=%d phase=%s bot=%d: %s"), Cycle, PhaseName(Phase), Bot, Reason);
  Finish(false, Reason);
 }
 void OnWorldCleanup(UWorld* InWorld, bool, bool)
 {
  if (InWorld == World.Get()) Finish(false, TEXT("WorldCleanup"));
 }

 bool SpawnOne()
 {
  TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_EquipmentSoak_Spawn);
  UWorld* W = World.Get();
  const auto* Template = CastChecked<AProject_JPlayerCharacter>(VisualTemplate->GetDefaultObject());
  const int32 Index = Bots.Num();
  FBot& Bot = Bots.AddDefaulted_GetRef();
  Bot.Origin = Origin + FVector((Index % 10) * 400, (Index / 10) * 400, 0);
  FActorSpawnParameters Params;
  Params.ObjectFlags |= RF_Transient;
  Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  Bot.Controller = W->SpawnActor<AAIController>(Params);
  Bot.State = W->SpawnActor<AProject_JPlayerState>(Params);
  Params.bDeferConstruction = true;
  Bot.Pawn = W->SpawnActor<AProject_JGreatswordCharacter>(AProject_JGreatswordCharacter::StaticClass(), Bot.Origin, FRotator::ZeroRotator, Params);
  if (!Bot.Controller.IsValid() || !Bot.State.IsValid() || !Bot.Pawn.IsValid()) return false;
  auto* Pawn = Bot.Pawn.Get();
  // Copy only immutable visual/class defaults. Never execute the user's BP BeginPlay
  // or its temporary equip/input graph on 100 uncontrolled clones.
  for (const FName Name : { FName(TEXT("CharacterAnimProfile")), FName(TEXT("CharacterClassDefinition")) })
  {
   auto* Property = FindFProperty<FObjectPropertyBase>(AProject_JPlayerCharacter::StaticClass(), Name);
   if (!Property) return false;
   Property->SetObjectPropertyValue_InContainer(Pawn, Property->GetObjectPropertyValue_InContainer(Template));
  }
  Pawn->GetMesh()->SetSkeletalMesh(Template->GetMesh()->GetSkeletalMeshAsset());
  Pawn->GetMesh()->SetRelativeTransform(Template->GetMesh()->GetRelativeTransform());
  Pawn->GetMesh()->SetAnimInstanceClass(Template->GetMesh()->GetAnimClass());
  // Equal visibility-independent animation work in both visible PIE and NullRHI smoke runs.
  Pawn->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
  Pawn->GetMesh()->bEnableUpdateRateOptimizations = false;
  Pawn->SetActorEnableCollision(false); // stationary grid; inter-bot collision is excluded
  Bot.State->SetOwner(Bot.Controller.Get());
  Bot.Controller->SetPlayerState(Bot.State.Get());
  Pawn->SetPlayerState(Bot.State.Get());
  Pawn->FinishSpawning(FTransform(FRotator::ZeroRotator, Bot.Origin));
  Bot.Controller->Possess(Pawn);
  Pawn->GetCharacterMovement()->DisableMovement();
  auto* ASC = Pawn->GetAbilitySystemComponent();
  if (!ASC || ASC != Bot.State->GetAbilitySystemComponent() || !Pawn->GetMesh()->GetAnimInstance()) return false;
  Bot.State->GetEquipmentManagerComponent()->UnequipSlot(EProject_JEquipmentSlot::Weapon);
  Bot.BaselineAbilityCount = ASC->GetActivatableAbilities().Num();
  TRACE_COUNTER_SET(SoakActors, Bots.Num());
  return true;
 }

 void Tick(UWorld* InWorld, ELevelTick TickType, float)
 {
  if (bInTick) return;
  {
   TGuardValue<bool> InTick(bInTick, true);
   Advance(InWorld, TickType);
  }
  // Close the driver CPU scope before flushing the final trace, including aborts.
  if (bFinished && bOwnTrace) { FTraceAuxiliary::Stop(); bOwnTrace = false; }
 }

 void Advance(UWorld* InWorld, ELevelTick TickType)
 {
  if (bFinished || InWorld != World.Get() || TickType == LEVELTICK_ViewportsOnly) return;
  check(IsInGameThread());
  TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_EquipmentSoak_Driver);
  const double Now = FPlatformTime::Seconds();
  TRACE_COUNTER_SET_ALWAYS(SoakFrameMs, (Now - LastTick) * 1000); LastTick = Now;
  TRACE_COUNTER_SET_ALWAYS(SoakPhaseMs, (Now - PhaseStarted) * 1000);
  if (Now - LastMemorySample >= 1)
  { TRACE_COUNTER_SET(SoakMemoryMiB, FPlatformMemory::GetStats().UsedPhysical / 1048576.0); LastMemorySample = Now; }
  ++PhaseTicks;
  if (InWorld->bIsTearingDown) { Finish(false, TEXT("WorldTearingDown")); return; }
  if (Now - Started > 30 + Repetitions * 30) { Fail(TEXT("Overall wall-clock timeout")); return; }
  if (Phase == EPhase::Spawn)
  {
   for (int32 N = 0; N < 5 && Bots.Num() < Population; ++N)
    if (!SpawnOne()) { Fail(TEXT("Spawn/PlayerState/ASC/AnimInstance setup failed"), Bots.Num()-1); return; }
   if (Bots.Num() == Population) Enter(EPhase::Warmup);
  }
  else if (Phase == EPhase::Warmup || Phase == EPhase::Cooldown)
  {
   if (Now - PhaseStarted >= (Phase == EPhase::Warmup ? 2.0 : 0.5))
   {
    ++Cycle; Enter(EPhase::Equip);
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_EquipmentSoak_Equip100);
    for (FBot& Bot : Bots)
    {
     if (!Bot.Pawn.IsValid() || !Bot.State.IsValid()) { Fail(TEXT("Actor destroyed before equip")); return; }
     Bot.Pawn->SetActorLocation(Bot.Origin, false, nullptr, ETeleportType::TeleportPhysics);
     Bot.State->GetEquipmentManagerComponent()->EquipItem(Item.Get());
     Bot.Pawn->GetAbilitySystemComponent()->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
     Bot.Pawn->FindComponentByClass<UProject_JWeaponPresentationComponent>()->AttachWeaponToDrawnSocket();
    }
   }
  }
  else
  {
   int32 Ready = 0, Equipped = 0, Attacking = 0;
   for (int32 Index = 0; Index < Bots.Num(); ++Index)
   {
    const FBot& Bot = Bots[Index];
    if (!Bot.Pawn.IsValid() || !Bot.State.IsValid() || !Bot.Controller.IsValid()) { Fail(TEXT("Actor destroyed during cycle"), Index); return; }
    auto* Pawn = Bot.Pawn.Get(); auto* ASC = Pawn->GetAbilitySystemComponent();
    auto* Manager = Bot.State->GetEquipmentManagerComponent();
    auto* Weapon = Pawn->FindComponentByClass<UProject_JWeaponPresentationComponent>();
    auto* Presentation = Pawn->FindComponentByClass<UProject_JCombatPresentationComponent>();
    auto* Hit = Pawn->FindComponentByClass<UProject_JCombatHitValidationComponent>();
    const bool bEquipped = Manager->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon) != nullptr;
    const bool bAttacking = ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Attacking) || Pawn->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying();
    Equipped += bEquipped; Attacking += bAttacking;
    if (Phase == EPhase::Equip) Ready += bEquipped && IsValid(Weapon->GetSpawnedWeapon());
    if (Phase == EPhase::Attack) Ready += !bAttacking && !Presentation->GetActiveAttackTag().IsValid() && !Hit->GetActiveAttackDefinition();
    if (Phase == EPhase::Unequip)
     Ready += !bEquipped && !IsValid(Weapon->GetSpawnedWeapon()) && !bAttacking && !Presentation->GetActiveAttackTag().IsValid()
      && ASC->GetActivatableAbilities().Num() == Bot.BaselineAbilityCount;
   }
   TRACE_COUNTER_SET(SoakEquipped, Equipped); TRACE_COUNTER_SET(SoakAttacking, Attacking);
   if (Ready != Population && Now - PhaseStarted > 10) { Fail(TEXT("Phase timeout: equipment/attack/cleanup did not reach expected state")); return; }
   if (Ready == Population && Phase == EPhase::Equip)
   {
    Enter(EPhase::Attack);
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_EquipmentSoak_Attack100);
    for (int32 Index = 0; Index < Bots.Num(); ++Index)
    {
     auto* Pawn = Bots[Index].Pawn.Get();
     auto* Input = Pawn->FindComponentByClass<UProject_JSkillInputExecutionComponent>();
     Input->ClearCommandInputHistory();
     Input->HandleInputTagPressed(FProject_JGameplayTags::Get().InputTag_Weapon_LMB);
     CastChecked<UProject_JAbilitySystemComponent>(Pawn->GetAbilitySystemComponent())->AbilityInputTagReleased(FProject_JGameplayTags::Get().InputTag_Weapon_LMB);
     // A request is not counted as an attack unless the real GAS+montage path starts.
     if (!Pawn->GetAbilitySystemComponent()->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Attacking)
      || !Pawn->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying()
      || !Pawn->FindComponentByClass<UProject_JCombatPresentationComponent>()->GetActiveAttackTag().IsValid())
     { Fail(TEXT("LMB did not start GAS/montage/presentation"), Index); return; }
    }
    TRACE_COUNTER_SET(SoakAttacking, Population);
   }
   else if (Ready == Population && Phase == EPhase::Attack)
   {
    Enter(EPhase::Unequip);
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_EquipmentSoak_Unequip100);
    for (FBot& Bot : Bots)
    {
     Bot.State->GetEquipmentManagerComponent()->UnequipSlot(EProject_JEquipmentSlot::Weapon);
     Bot.Pawn->GetAbilitySystemComponent()->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
    }
   }
   else if (Ready == Population && Phase == EPhase::Unequip && PhaseTicks >= 2)
   {
    Completed += Population; TRACE_COUNTER_SET(SoakCycles, Completed);
    if (Cycle == Repetitions) Finish(true, TEXT("Completed")); else Enter(EPhase::Cooldown);
   }
  }
  TRACE_COUNTER_SET_ALWAYS(SoakDriverMs, (FPlatformTime::Seconds() - Now) * 1000);
 }

 void Finish(bool bSuccess, const TCHAR* Reason)
 {
  if (bFinished) return;
  check(IsInGameThread());
  bFinished = true; bPassed = bSuccess;
  FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle);
  FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
  RecordPhase(); Phase = EPhase::Finished; TRACE_COUNTER_SET(SoakPhase, int64(Phase));
  TRACE_BOOKMARK(TEXT("EquipmentSoak100 End success=%d completed=%d failures=%d reason=%s"), bSuccess, Completed, Failures, Reason);
  {
   TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_EquipmentSoak_Cleanup100);
   for (FBot& Bot : Bots)
   {
    if (Bot.Pawn.IsValid())
    {
     if (auto* ASC = Bot.Pawn->GetAbilitySystemComponent()) ASC->CancelAllAbilities();
     if (Bot.State.IsValid()) Bot.State->GetEquipmentManagerComponent()->UnequipSlot(EProject_JEquipmentSlot::Weapon);
     Bot.Pawn->Destroy();
    }
    if (Bot.Controller.IsValid()) { Bot.Controller->SetPlayerState(nullptr); Bot.Controller->Destroy(); }
    if (Bot.State.IsValid()) Bot.State->Destroy();
   }
  }
  Bots.Reset(); Item.Reset(); VisualTemplate.Reset();
  TRACE_COUNTER_SET(SoakActors, 0); TRACE_COUNTER_SET(SoakEquipped, 0); TRACE_COUNTER_SET(SoakAttacking, 0);
  FString Summary = FString::Printf(TEXT("success=%d\nreason=%s\npopulation=100\nrepetitions=%d\ncompleted_actor_cycles=%d\nfailures=%d\nelapsed_seconds=%.3f\ntrace=%s\ntrace_owned=%d\nmode=Standalone authoritative native player characters with independent PlayerStates, AI possession\nassets=existing BP visual/class defaults; no Blueprint event graph; shared preloaded greatsword\nanimation=AlwaysTickPoseAndRefreshBones; URO off; movement/collision off\nlimits=no client RPC validation, no damage validation, no cold-load claim, no pixel/VFX visibility assertion\n"),
   bSuccess, Reason, Repetitions, Completed, Failures, FPlatformTime::Seconds()-Started, *TraceDestination, bOwnTrace);
  Summary += FString::Printf(TEXT("nullrhi=%d\n"), FParse::Param(FCommandLine::Get(), TEXT("NullRHI")));
  const bool bSaved = FFileHelper::SaveStringToFile(Rows, *(Directory / TEXT("Phases.csv")))
   && FFileHelper::SaveStringToFile(Summary, *(Directory / TEXT("Summary.txt")));
  if (!bSaved) { bPassed = false; UE_LOG(LogProjectJEquipmentSoak, Error, TEXT("Could not save report: %s"), *Directory); }
  if (bOwnTrace && !bInTick) { FTraceAuxiliary::Stop(); bOwnTrace = false; }
  UE_LOG(LogProjectJEquipmentSoak, Display, TEXT("Finished success=%d completed=%d failures=%d reason=%s report=%s"), bPassed, Completed, Failures, Reason, *Directory);
 }
};

TSharedPtr<FRun> Active;
bool Start(UWorld* World, int32 Repetitions, FVector Origin)
{
 if (Active && !Active->bFinished) { UE_LOG(LogProjectJEquipmentSoak, Warning, TEXT("A run is already active. Use ProjectJ.EquipmentSoak.Stop first.")); return false; }
 Active = MakeShared<FRun>();
 const bool bStarted = Active->Start(World, Repetitions, Origin);
 if (!bStarted && !Active->bFinished) Active->bFinished = true;
 return bStarted;
}

FAutoConsoleCommandWithWorldAndArgs StartCommand(TEXT("ProjectJ.EquipmentSoak.Start"),
 TEXT("Standalone only. Spawn 100 native player characters, equip/LMB/unequip in synchronized rounds; record trace. Optional rounds (1..100, default 10)."),
 FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
 {
  FVector Origin(0, 0, 100);
  if (World && World->GetFirstPlayerController() && World->GetFirstPlayerController()->GetPawn())
   Origin = World->GetFirstPlayerController()->GetPawn()->GetActorLocation() + FVector(1000, -1800, 0);
  Start(World, Args.IsEmpty() ? 10 : FCString::Atoi(*Args[0]), Origin);
 }));
FAutoConsoleCommand StopCommand(TEXT("ProjectJ.EquipmentSoak.Stop"), TEXT("Stop only the active equipment fixture and its owned trace; destroy fixture actors."),
 FConsoleCommandDelegate::CreateLambda([] { if (Active) Active->Finish(false, TEXT("UserStopped")); }));
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
namespace ProjectJEquipmentSoak
{
class FSmokeCommand : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 UWorld* World = nullptr;
 bool bStarted = false;
 int32 CancelMode = 0;
public:
 explicit FSmokeCommand(FAutomationTestBase* InTest, int32 InCancelMode = 0) : Test(InTest), CancelMode(InCancelMode) {}
 virtual bool Update() override
 {
  if (!bStarted)
  {
   bStarted = true;
   World = UWorld::CreateWorld(EWorldType::Game, false);
   GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
   World->InitializeActorsForPlay(FURL());
   World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
   if (!Start(World, 3, FVector(0, 0, 100)))
   { Test->AddError(TEXT("Equipment soak start failed")); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return true; }
  }
  World->Tick(LEVELTICK_All, 1.0f / 60.0f);
  if (CancelMode != 0 && !Active->bFinished && Active->Phase == EPhase::Attack)
  {
   // Hold weak identities across cancellation to detect uncleaned fixture actors.
   const TArray<FBot> Before = Active->Bots;
   if (CancelMode == 1) Active->Finish(false, TEXT("AutomationUserStop"));
   else
   {
    World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit);
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World = nullptr;
   }
   Test->TestTrue(TEXT("Cancellation closes the run"), Active->bFinished);
   Test->TestFalse(TEXT("Cancellation is not a successful completion"), Active->bPassed);
   Test->TestFalse(TEXT("Owned trace is closed"), Active->bOwnTrace);
   for (const FBot& Bot : Before)
   {
    Test->TestFalse(TEXT("Pawn released"), Bot.Pawn.IsValid());
    Test->TestFalse(TEXT("PlayerState released"), Bot.State.IsValid());
    Test->TestFalse(TEXT("Controller released"), Bot.Controller.IsValid());
   }
   if (World) { World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
   return true;
  }
  if (!Active->bFinished) return false;
  Test->TestTrue(TEXT("100 characters complete 3 equip/LMB/unequip rounds"), Active->bPassed);
  Test->TestEqual(TEXT("Completed actor cycles"), Active->Completed, 300);
  Test->TestEqual(TEXT("No failures"), Active->Failures, 0);
  Test->AddInfo(FString::Printf(TEXT("Trace/report: %s; NullRHI is CPU/lifecycle validation, not a GPU benchmark"), *Active->Directory));
  World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
  return true;
 }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEquipmentSoak100Test, "ProjectJ.EquipmentSoak.HundredCharacters",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJEquipmentSoak100Test::RunTest(const FString&)
{
 ADD_LATENT_AUTOMATION_COMMAND(ProjectJEquipmentSoak::FSmokeCommand(this));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEquipmentSoakStopTest, "ProjectJ.EquipmentSoak.StopDuringAttack",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJEquipmentSoakStopTest::RunTest(const FString&)
{
 ADD_LATENT_AUTOMATION_COMMAND(ProjectJEquipmentSoak::FSmokeCommand(this, 1));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEquipmentSoakWorldCleanupTest, "ProjectJ.EquipmentSoak.WorldCleanupDuringAttack",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJEquipmentSoakWorldCleanupTest::RunTest(const FString&)
{
 ADD_LATENT_AUTOMATION_COMMAND(ProjectJEquipmentSoak::FSmokeCommand(this, 2));
 return true;
}
#endif
#endif
