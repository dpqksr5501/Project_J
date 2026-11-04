#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Combat/Project_JCombatPresentationSet.h"
#include "Project_JNPCCharacter.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "System/Project_JVisualAssetSubsystem.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JSpellbladeCharacter.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "Equipment/Project_JWeaponPresentationActor.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "System/Project_JPresentationBudgetSubsystem.h"
#include "GameplayTagsManager.h"
#include "HAL/PlatformTime.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "NiagaraSystem.h"
#if WITH_EDITOR
#include "NiagaraEffectType.h"
#include "Misc/DataValidation.h"
#include "UObject/Package.h"
#endif
#include "Camera/CameraActor.h"
#include "Components/Project_JGameplayWarningMarkerComponent.h"
#include "Animation/Project_JAnimNotifyState_CombatPresentationCue.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Controller.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TestAttackTag, "ProjectJ.Tests.Presentation.Attack");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TestCueTag, "ProjectJ.Tests.Presentation.Cue");
namespace
{
// Test-local value/lifetime fixture, not a production loader or reflected type.
struct FSoftCueWorld
{
	UWorld* World;
	UProject_JVisualAssetSubsystem* Assets;
	AProject_JNPCCharacter* NPC;
	UProject_JCombatPresentationComponent* Cue;
	UProject_JCombatPresentationSet* Set;
	UProject_JAttackPresentationProfile* Profile;
	explicit FSoftCueWorld(bool bDedicated = false)
	{
		const EWorldType::Type Type = bDedicated ? EWorldType::PIE : EWorldType::Game;
		World = UWorld::CreateWorld(Type, false);
		GEngine->CreateNewWorldContext(Type).SetCurrentWorld(World);
#if WITH_EDITOR
		if (bDedicated) { World->SetPlayInEditorInitialNetMode(NM_DedicatedServer); }
#endif
		Assets = World->GetSubsystem<UProject_JVisualAssetSubsystem>();
		FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		NPC = World->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(), FVector(0, 0, 100), FRotator::ZeroRotator, Spawn);
		checkf(NPC, TEXT("Concrete soft-cue NPC fixture must spawn before component access."));
		Cue = NewObject<UProject_JCombatPresentationComponent>(NPC);
		NPC->AddInstanceComponent(Cue); Cue->RegisterComponent();
		Set = NewObject<UProject_JCombatPresentationSet>(Cue);
		Profile = NewObject<UProject_JAttackPresentationProfile>(Set);
		auto& Entry = Set->AttackPresentations.AddDefaulted_GetRef();
		Entry.AttackTag = TestAttackTag; Entry.Profile = Profile;
		Cue->BasePresentationSet = Set;
	}
	void SetCue(const FSoftObjectPath& Path, bool bLooping)
	{
		Profile->Cues.Reset();
		auto& Value = Profile->Cues.AddDefaulted_GetRef();
		Value.CueTag = TestCueTag; Value.NiagaraSystem = TSoftObjectPtr<UNiagaraSystem>(Path);
		Value.bLooping = bLooping; Value.AttachmentTarget = EProject_JCombatVFXAttachmentTarget::World;
	}
	~FSoftCueWorld()
	{
		if (World->GetBegunPlay()) { World->EndPlay(EEndPlayReason::LevelTransition); }
		World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	}
};
class FSoftCueBoundaryCommand : public IAutomationLatentCommand
{
public:
	explicit FSoftCueBoundaryCommand(TFunction<bool()> InBody) : Body(MoveTemp(InBody)) {}
	bool Update() override { return Body(); }
private:
	TFunction<bool()> Body;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPresentationRecoveryTest, "ProjectJ.Integrated.PresentationRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)
bool FProjectJPresentationRecoveryTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	auto* NPC = World->SpawnActor<AProject_JNPCCharacter>();
	auto* C = NewObject<UProject_JCombatPresentationComponent>(NPC); C->RegisterComponent();
	C->BeginAttackPresentation(TestAttackTag);
	auto* Set = NewObject<UProject_JCombatPresentationSet>(C);
	auto* Profile = NewObject<UProject_JAttackPresentationProfile>(C);
	FProject_JCombatVFXCueDefinition Cue; Cue.CueTag = TestCueTag; Cue.AttachmentTarget = EProject_JCombatVFXAttachmentTarget::CharacterMesh;
	Profile->Cues.Add(Cue);
	FProject_JCombatAttackPresentationEntry Entry; Entry.AttackTag = TestAttackTag; Entry.Profile = Profile;
	Set->AttackPresentations.Add(Entry); C->BasePresentationSet = Set;
	TestTrue(TEXT("NPC resolves the existing set/profile/cue types"), C->ResolveCue(TestCueTag) == &Profile->Cues[0]);
	const uint32 First = C->ActiveAttackInstance;
	C->BeginAttackPresentation(TestAttackTag);
	TestTrue(TEXT("Repeated attack tag has a new instance"), C->ActiveAttackInstance != First);
	C->StartedCueTags.AddTag(TestCueTag);
	C->ReplicatedPresentationState.EventOrder = 30;
	C->ReplicatedPresentationState.Revision = 30;
	C->LastAppliedPresentationEventOrder = 40; // Later one-shot does not invalidate persistent loop recovery.
	C->LastAppliedRecoveryEventOrder = 20;
	C->ApplyReplicatedState();
	TestTrue(TEXT("Same-attack recovery preserves one-shot deduplication"), C->StartedCueTags.HasTagExact(TestCueTag));
	TestEqual(TEXT("Older useful recovery accepted after a newer one-shot"), C->LastAppliedPresentationRevision, 30);
	TestEqual(TEXT("Event ordering never moves backwards"), C->LastAppliedPresentationEventOrder, 40);
	C->LastAppliedRecoveryEventOrder = 50;
	C->ReplicatedPresentationState.Revision = 31; C->ReplicatedPresentationState.EventOrder = 45;
	C->ReplicatedPresentationState.ActiveAttackTag = {};
	C->ApplyReplicatedState();
	TestTrue(TEXT("Stale recovery cannot erase a newer attack"), C->ActiveAttackTag.IsValid());
	C->EndAttackPresentation();
	const int32 Order = C->NextPresentationEventOrder;
	C->EndAttackPresentation();
	TestEqual(TEXT("Repeated end does not emit network updates"), C->NextPresentationEventOrder, Order);
	C->DestroyComponent(); World->DestroyWorld(false);
	const FSoftObjectPath Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
	{
		FSoftCueWorld S(true); S.SetCue(Cube, true);
		TestEqual(TEXT("Authority fixture is dedicated"), S.NPC->GetNetMode(), NM_DedicatedServer);
		S.Cue->RefreshPresentation(); S.Cue->BeginAttackPresentation(TestAttackTag);
		const uint64 Lease = S.Cue->BeginCueLease(TestCueTag);
		TestTrue(TEXT("Unloaded soft payload still grants server notify ownership"), Lease != 0);
		TestTrue(TEXT("Server metadata still publishes persistent loop recovery"), S.Cue->ReplicatedPresentationState.ActiveLoopingCueTags.HasTagExact(TestCueTag));
		TestEqual(TEXT("Dedicated metadata paths issue zero asset requests"), S.Assets->GetStats().Accepted, uint64(0));
		S.Cue->EndCueLease(Lease);
		TestFalse(TEXT("Server end removes persistent loop identity"), S.Cue->ReplicatedPresentationState.ActiveLoopingCueTags.HasTagExact(TestCueTag));
	}
#endif
	{
		FSoftCueWorld S; S.SetCue(Cube, true);
		S.Cue->BeginAttackPresentation(TestAttackTag);
		S.Cue->ReplicatedPresentationState.ActiveAttackTag = TestAttackTag;
		S.Cue->ReplicatedPresentationState.AttackInstance = S.Cue->ActiveAttackInstance;
		S.Cue->ReplicatedPresentationState.ActiveLoopingCueTags.AddTag(TestCueTag);
		S.Cue->ReplicatedPresentationState.Revision = S.Cue->ReplicatedPresentationState.EventOrder = 10;
		S.Cue->ApplyReplicatedState();
		TestEqual(TEXT("Early recovery retains an unresolved desired loop"), S.Cue->PendingLoopingCues.Num(), 1);
		S.World->InitializeActorsForPlay(FURL()); S.World->GetWorldSettings()->NotifyBeginPlay();
		TestTrue(TEXT("BeginPlay does not retire already accepted recovery"), S.Cue->PendingLoopingCues.Contains(TestCueTag));
		TestEqual(TEXT("BeginPlay preserves the accepted recovery revision"), S.Cue->LastAppliedPresentationRevision, 10);
		S.Cue->ReplicatedPresentationState.ActiveLoopingCueTags.Reset();
		S.Cue->ReplicatedPresentationState.Revision = S.Cue->ReplicatedPresentationState.EventOrder = 11;
		S.Cue->ApplyReplicatedState();
		TestEqual(TEXT("OnRep removes unresolved loops omitted by current recovery"), S.Cue->PendingLoopingCues.Num(), 0);
		const uint32 OldRevision = S.Cue->VisualAssetRevision;
		S.Cue->RefreshPresentation();
		S.Cue->OnVisualAssetReady(Cube, OldRevision, nullptr);
		TestEqual(TEXT("Old same-path completion does not release the replacement lease"), S.Cue->VisualAssetLeases.Num(), 1);

		S.Cue->BasePresentationSet = nullptr; S.Cue->RefreshPresentation();
		TArray<FSoftObjectPath> Paths;
		for (int32 I = 0; I < S.Cue->MaxVisualAssetPaths; ++I)
		{
			const FSoftObjectPath Path(FString::Printf(TEXT("/Game/Tests/Prewarm_%d.Prewarm_%d"), I, I));
			Paths.Add(Path); TestTrue(TEXT("Bounded prewarm slot is admitted"), S.Cue->EnsureVisualAsset(Path));
		}
		S.Cue->PendingLoopingCues.Add(TestCueTag, {Paths[0], S.Cue->ActiveAttackInstance});
		const FSoftObjectPath Actual(TEXT("/Game/Tests/ActualCue.ActualCue"));
		TestTrue(TEXT("Actual 65th cue displaces idle prewarming"), S.Cue->EnsureVisualAsset(Actual, true));
		TestEqual(TEXT("Cue demand keeps component leases bounded"), S.Cue->VisualAssetLeases.Num(), S.Cue->MaxVisualAssetPaths);
		TestTrue(TEXT("Already pending loop keeps its residency priority"), S.Cue->VisualAssetLeases.Contains(Paths[0]));
		FGameplayTagContainer AllTags; UGameplayTagsManager::Get().RequestAllGameplayTags(AllTags, true);
		TArray<FGameplayTag> Tags; AllTags.GetGameplayTagArray(Tags);
		if (TestTrue(TEXT("Native tag catalog supplies distinct cue identities"), Tags.Num() > S.Cue->MaxVisualAssetPaths))
		{
			TArray<FSoftObjectPath> ResidentPaths; S.Cue->VisualAssetLeases.GetKeys(ResidentPaths);
			S.Cue->PendingLoopingCues.Reset();
			for (int32 I = 0; I < ResidentPaths.Num(); ++I)
			{
				S.Cue->PendingLoopingCues.Add(Tags[I], {ResidentPaths[I], S.Cue->ActiveAttackInstance});
			}
			TestFalse(TEXT("New cue cannot evict residency of already pending loops"), S.Cue->EnsureVisualAsset(Cube, true));
			S.Cue->BasePresentationSet = S.Set; S.Profile->Cues.Reset();
			auto& Extra = S.Profile->Cues.AddDefaulted_GetRef();
			Extra.CueTag = Tags[S.Cue->MaxVisualAssetPaths]; Extra.NiagaraSystem = TSoftObjectPtr<UNiagaraSystem>(Cube); Extra.bLooping = true;
			S.Cue->PlayCue(Extra.CueTag);
			TestEqual(TEXT("Desired-loop map stays bounded when many cues share paths"), S.Cue->PendingLoopingCues.Num(), S.Cue->MaxVisualAssetPaths);
		}
		S.Cue->BasePresentationSet = nullptr;
		S.Cue->RefreshPresentation();
		TestEqual(TEXT("Refresh retires all previous world leases"), S.Assets->GetLeaseCount(), 0);
	}
	{
		// Isolate this owner's synchronous pair update from every other world/loader producer.
		// The payload and weapon class are already loaded; no loader Tick or fake completion is needed.
		FSoftCueWorld S;
		const TStrongObjectPtr<UNiagaraSystem> WarmSystem(LoadObject<UNiagaraSystem>(nullptr,
			TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_SlashTrail_Basic.NS_SlashTrail_Basic")));
		const TStrongObjectPtr<USkeletalMesh> WarmMesh(LoadObject<USkeletalMesh>(nullptr,
			TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")));
		if (!TestNotNull(TEXT("Pair refresh has a typed warm cue payload"), WarmSystem.Get()) ||
			!TestNotNull(TEXT("Pair refresh has the authored root socket mesh"), WarmMesh.Get())) { return false; }
		const FSoftObjectPath CuePath(WarmSystem.Get()); S.SetCue(CuePath, true);
		FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const TStrongObjectPtr<AProject_JSpellbladeCharacter> Player(S.World->SpawnActor<AProject_JSpellbladeCharacter>(
			AProject_JSpellbladeCharacter::StaticClass(), FVector(300, 0, 100), FRotator::ZeroRotator, Spawn));
		if (!TestNotNull(TEXT("Local pair refresh has a concrete owner"), Player.Get())) { return false; }
		Player->GetMesh()->SetSkeletalMesh(WarmMesh.Get());
		auto* Presentation = Player->FindComponentByClass<UProject_JCombatPresentationComponent>();
		auto* Weapon = Player->FindComponentByClass<UProject_JWeaponPresentationComponent>();
		if (!TestNotNull(TEXT("Local pair has the production cue owner"), Presentation) ||
			!TestNotNull(TEXT("Local pair has the production weapon owner"), Weapon)) { return false; }
		auto* StyleA = NewObject<UProject_JCombatStyleDefinition>(Player.Get());
		auto* StyleB = NewObject<UProject_JCombatStyleDefinition>(Player.Get());
		StyleA->CombatPresentationSet = S.Set; StyleB->CombatPresentationSet = S.Set;
		auto* Skin = NewObject<UProject_JWeaponPresentationProfile>(Player.Get());
		Skin->WeaponActorClass = ACameraActor::StaticClass();
		Skin->DrawnSocketName = Skin->SheathedSocketName = TEXT("root"); Skin->bPreferVisualFollowerSockets = false;
		if (!TestEqual(TEXT("Independent loader starts without another owner's leases"), S.Assets->GetLeaseCount(), 0) ||
			!TestEqual(TEXT("Independent loader starts without another owner's requests"), S.Assets->GetStats().Accepted, uint64(0))) { return false; }
		Player->SetCurrentEquipmentConfiguration(StyleA, Skin);
		const TWeakObjectPtr<AActor> WarmWeapon(Weapon->GetSpawnedWeapon());
		if (!TestTrue(TEXT("Initial pair creates its warm weapon synchronously"), WarmWeapon.IsValid()) ||
			!TestEqual(TEXT("One selected cue path owns one initial lease"), Presentation->VisualAssetLeases.Num(), 1)) { return false; }
		const uint64 OldToken = Presentation->VisualAssetLeases.FindRef(CuePath);
		const uint64 BeforeAccepted = S.Assets->GetStats().Accepted;
		const uint32 BeforeRevision = Presentation->VisualAssetRevision;
		const int32 BeforeLeases = S.Assets->GetLeaseCount();
		if (!TestTrue(TEXT("Initial cue path has a real loader token"), OldToken != 0) ||
			!TestEqual(TEXT("Only this owner supplied one initial request"), BeforeAccepted, uint64(1)) ||
			!TestEqual(TEXT("Only this owner retains one initial lease"), BeforeLeases, 1)) { return false; }

		Player->SetCurrentEquipmentConfiguration(StyleB, Skin);
		const uint64 NewToken = Presentation->VisualAssetLeases.FindRef(CuePath);
		TestEqual(TEXT("Style-only local pair retires its visual generation once"), Presentation->VisualAssetRevision, BeforeRevision + 1);
		TestEqual(TEXT("Style-only local pair acquires the selected path once"), S.Assets->GetStats().Accepted, BeforeAccepted + 1);
		TestTrue(TEXT("The old loader token was replaced by a distinct current token"), NewToken != 0 && NewToken != OldToken);
		TestEqual(TEXT("Token replacement retains no extra old lease"), S.Assets->GetLeaseCount(), BeforeLeases);
		TestEqual(TEXT("The cue owner still owns exactly its selected path"), Presentation->VisualAssetLeases.Num(), 1);
		TestTrue(TEXT("Style-only pair preserves the same UObject weapon lifetime"),
			WarmWeapon.HasSameIndexAndSerialNumber(TWeakObjectPtr<AActor>(Weapon->GetSpawnedWeapon())));
		TestTrue(TEXT("The new style and unchanged skin remain selected"),
			Player->GetCurrentCombatStyle() == StyleB && Player->GetCurrentWeaponPresentationProfile() == Skin);
		const uint32 StableRevision = Presentation->VisualAssetRevision;
		const uint64 StableAccepted = S.Assets->GetStats().Accepted;
		Player->SetCurrentEquipmentConfiguration(StyleB, Skin);
		TestEqual(TEXT("The same pair does not retire its generation"), Presentation->VisualAssetRevision, StableRevision);
		TestEqual(TEXT("The same pair makes no new loader request"), S.Assets->GetStats().Accepted, StableAccepted);
		TestEqual(TEXT("The same pair preserves its exact current lease"), Presentation->VisualAssetLeases.FindRef(CuePath), NewToken);
		TestTrue(TEXT("The same pair preserves the physical weapon"),
			WarmWeapon.HasSameIndexAndSerialNumber(TWeakObjectPtr<AActor>(Weapon->GetSpawnedWeapon())));
		Player->SetCurrentEquipmentConfiguration(nullptr, nullptr);
		TestEqual(TEXT("Normal pair retirement releases this owner's selected path"), S.Assets->GetLeaseCount(), 0);
		TestEqual(TEXT("Normal pair retirement clears component lease ownership"), Presentation->VisualAssetLeases.Num(), 0);
		TestNull(TEXT("Normal pair retirement removes the physical weapon"), Weapon->GetSpawnedWeapon());
	}
	{
		FSoftCueWorld S; S.SetCue(Cube, true);
		FActorSpawnParameters PlayerSpawn; PlayerSpawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* Player = S.World->SpawnActor<AProject_JSpellbladeCharacter>(AProject_JSpellbladeCharacter::StaticClass(), FVector(300, 0, 100), FRotator::ZeroRotator, PlayerSpawn);
		if (!TestNotNull(TEXT("Concrete style replication fixture"), Player)) { return false; }
		Player->SwapRoles();
		auto* Presentation = Player->FindComponentByClass<UProject_JCombatPresentationComponent>();
		auto* StyleA = NewObject<UProject_JCombatStyleDefinition>(Player);
		auto* StyleB = NewObject<UProject_JCombatStyleDefinition>(Player);
		StyleA->CombatPresentationSet = S.Set;
		auto* NextSet = NewObject<UProject_JCombatPresentationSet>(StyleB);
		auto* NextProfile = NewObject<UProject_JAttackPresentationProfile>(NextSet);
		NextProfile->Cues = S.Profile->Cues;
		const FSoftObjectPath Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		NextProfile->Cues[0].NiagaraSystem = TSoftObjectPtr<UNiagaraSystem>(Sphere);
		auto& NextEntry = NextSet->AttackPresentations.AddDefaulted_GetRef(); NextEntry.AttackTag = TestAttackTag; NextEntry.Profile = NextProfile;
		StyleB->CombatPresentationSet = NextSet;
		Player->CurrentCombatStyle = StyleA; Player->OnRep_CurrentCombatStyle();
		Presentation->BeginAttackPresentation(TestAttackTag);
		Presentation->PendingLoopingCues.Add(TestCueTag, {Cube, Presentation->ActiveAttackInstance});
		const uint32 OldRevision = Presentation->VisualAssetRevision;
		const auto* UnchangedWeapon = Player->GetCurrentWeaponPresentationProfile();
		Player->CurrentCombatStyle = StyleB; Player->OnRep_CurrentCombatStyle();
		TestTrue(TEXT("Style-only remote OnRep changes visual generation"), Presentation->VisualAssetRevision != OldRevision);
		TestEqual(TEXT("Style-only remote OnRep retires pending old loops"), Presentation->PendingLoopingCues.Num(), 0);
		TestTrue(TEXT("Style-only fixture did not rely on a weapon profile update"), Player->GetCurrentWeaponPresentationProfile() == UnchangedWeapon);
		TestTrue(TEXT("Remote style-only update prewarms the new style payload"), Presentation->VisualAssetLeases.Contains(Sphere));
		TestFalse(TEXT("Remote style-only update releases the old payload"), Presentation->VisualAssetLeases.Contains(Cube));
		auto* Skin = NewObject<UProject_JWeaponPresentationProfile>(Player); Skin->CosmeticPresentationOverrideSet = S.Set;
		// Isolate effective-set precedence here; this test does not author/spawn
		// a weapon actor. Actual style-only replication was exercised above.
		Player->CurrentWeaponPresentationProfile = Skin; Presentation->RefreshPresentation();
		TestTrue(TEXT("Winning skin payload is admitted"), Presentation->VisualAssetLeases.Contains(Cube));
		TestFalse(TEXT("Masked style payload does not consume a preload slot"), Presentation->VisualAssetLeases.Contains(Sphere));
		TestEqual(TEXT("One effective attack/cue winner consumes one slot"), Presentation->VisualAssetLeases.Num(), 1);
	}

	// Actual remote profile-only replacement with the same combat style. The
	// native world destroy notification observes ordering without a test UCLASS.
	TStrongObjectPtr<UClass> FixtureWeaponClass(LoadClass<AProject_JWeaponPresentationActor>(nullptr,
		TEXT("/Game/Character_BPs/GreatSword/BP_Greatsword_Basic.BP_Greatsword_Basic_C")));
	if (!TestNotNull(TEXT("Authored concrete weapon class"), FixtureWeaponClass.Get()) ||
		!TestFalse(TEXT("Authored weapon can be instantiated"), FixtureWeaponClass->HasAnyClassFlags(CLASS_Abstract))) { return false; }
	for (const int32 CallbackMode : {0, 1, 2, 3, 4})
	{
		FSoftCueWorld S; S.SetCue(Cube, true);
		FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* Player = S.World->SpawnActor<AProject_JSpellbladeCharacter>(AProject_JSpellbladeCharacter::StaticClass(), FVector(600, 0, 100), FRotator::ZeroRotator, Spawn);
		if (!TestNotNull(TEXT("Concrete remote weapon-only fixture"), Player)) { return false; }
		Player->SwapRoles();
		auto* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
		if (!TestNotNull(TEXT("Authored root-bone attachment mesh"), Mesh)) { return false; }
		Player->GetMesh()->SetSkeletalMesh(Mesh);
		auto* Presentation = Player->FindComponentByClass<UProject_JCombatPresentationComponent>();
		auto* Weapon = Player->FindComponentByClass<UProject_JWeaponPresentationComponent>();
		if (!TestNotNull(TEXT("Existing combat presentation"), Presentation) || !TestNotNull(TEXT("Existing weapon presentation"), Weapon)) { return false; }
		auto* Style = NewObject<UProject_JCombatStyleDefinition>(Player); Style->CombatPresentationSet = S.Set;
		Player->CurrentCombatStyle = Style;
		auto* FirstWeaponProfile = NewObject<UProject_JWeaponPresentationProfile>(Player);
		auto* Next = NewObject<UProject_JWeaponPresentationProfile>(Player);
		auto* Latest = NewObject<UProject_JWeaponPresentationProfile>(Player);
		for (auto* WeaponProfile : {FirstWeaponProfile, Next, Latest})
		{
			WeaponProfile->WeaponActorClass = FixtureWeaponClass.Get();
			WeaponProfile->DrawnSocketName = WeaponProfile->SheathedSocketName = TEXT("root");
			WeaponProfile->bPreferVisualFollowerSockets = false;
		}
		Player->CurrentWeaponPresentationProfile = FirstWeaponProfile; Player->OnRep_CurrentWeaponPresentationProfile();
		AActor* PreviousWeapon = Weapon->GetSpawnedWeapon();
		if (!TestNotNull(TEXT("First actual weapon actor"), PreviousWeapon)) { return false; }
		// This test deliberately collects garbage inside callbacks. Preserve UObject
		// index/serial identity without pinning the retired actor or assuming unique addresses.
		const TWeakObjectPtr<AActor> PreviousWeaponIdentity(PreviousWeapon);
		const UPTRINT PreviousWeaponAddress = reinterpret_cast<UPTRINT>(PreviousWeapon);
		Presentation->BeginAttackPresentation(TestAttackTag);
		Presentation->PendingLoopingCues.Add(TestCueTag, {Cube, Presentation->ActiveAttackInstance});
		const uint64 OldNotify = ++Presentation->NextCueLeaseToken; Presentation->CueLeases.Add(OldNotify, TestCueTag);
		const uint32 PreviousVisualRevision = Presentation->VisualAssetRevision;
		TWeakObjectPtr<AProject_JSpellbladeCharacter> WeakPlayer(Player);
		bool bObservedDestroy = false;
		const FDelegateHandle Handle = S.World->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateLambda(
			[this, Player, Presentation, PreviousWeaponIdentity, PreviousVisualRevision, OldNotify, CallbackMode, Latest, &bObservedDestroy](AActor* Destroyed)
			{
				if (!PreviousWeaponIdentity.HasSameIndexAndSerialNumber(TWeakObjectPtr<AActor>(Destroyed))) { return; }
				bObservedDestroy = true;
				TestTrue(TEXT("Remote old cue generation retired before weapon destruction"), Presentation->VisualAssetRevision != PreviousVisualRevision);
				TestEqual(TEXT("Weapon destruction observes no old pending loop"), Presentation->PendingLoopingCues.Num(), 0);
				TestFalse(TEXT("Weapon destruction observes no old notify lease"), Presentation->CueLeases.Contains(OldNotify));
				if (CallbackMode == 1)
				{
					// Simulated proxies require the same forced local removal used by replication.
					TestTrue(TEXT("Remote proxy removal is accepted"), Player->Destroy(true));
					CollectGarbage(RF_NoFlags, true);
				}
				if (CallbackMode == 2 || CallbackMode == 3)
				{
					// Reenter with C while the outer weapon refresh still holds B.
					Player->CurrentWeaponPresentationProfile = Latest;
					Player->OnRep_CurrentWeaponPresentationProfile();
					CollectGarbage(RF_NoFlags, true);
				}
			}));
		bool bSpawnReentered = false; int32 NewWeaponSpawns = 0;
		const FDelegateHandle SpawnHandle = S.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda(
			[Player, Latest, CallbackMode, &bSpawnReentered, &NewWeaponSpawns](AActor* Created)
			{
				if (Created->GetOwner() != Player || !Created->IsA<AProject_JWeaponPresentationActor>()) { return; }
				++NewWeaponSpawns;
				if (CallbackMode != 4 || bSpawnReentered) { return; }
				bSpawnReentered = true;
				// UWorld broadcasts before the nondeferred SpawnActor returns.
				Player->CurrentWeaponPresentationProfile = Latest; Player->OnRep_CurrentWeaponPresentationProfile();
				CollectGarbage(RF_NoFlags, true);
			}));
		Player->CurrentWeaponPresentationProfile = CallbackMode == 3 ? nullptr : Next;
		Player->OnRep_CurrentWeaponPresentationProfile();
		S.World->RemoveOnActorDestroyedHandler(Handle);
		if (CallbackMode == 4)
		{
			auto* Budget = S.World->GetSubsystem<UProject_JPresentationBudgetSubsystem>();
			TestTrue(TEXT("Actual spawn-return selection callback ran"), bSpawnReentered);
			TestTrue(TEXT("Superseded creation queues current selection"), Budget->GetStats().Pending > 0);
			for (int32 Tick = 0; Tick < 8 && Budget->GetStats().Pending > 0; ++Tick)
			{
				Budget->Tick(0);
				TestTrue(TEXT("Existing apply count remains bounded"), Budget->GetStats().LastApplications <= Budget->MaxApplications);
			}
			TestEqual(TEXT("Queued latest selection is applied"), Budget->GetStats().Pending, 0);
		}
		S.World->RemoveOnActorSpawnedHandler(SpawnHandle);
		TestTrue(TEXT("Actual weapon destruction callback executed"), bObservedDestroy);
		if (CallbackMode == 1)
		{
			TestTrue(TEXT("Owner destruction/GC callback completes safely"), !WeakPlayer.IsValid() || WeakPlayer->IsActorBeingDestroyed());
			TestEqual(TEXT("No replacement spawn after owner destruction"), NewWeaponSpawns, 0);
		}
		else
		{
			TestTrue(TEXT("Weapon-only update keeps the same combat style"), Player->GetCurrentCombatStyle() == Style);
			TestEqual(TEXT("No recursive creation beyond one obsolete spawn and queued current spawn"), NewWeaponSpawns, CallbackMode == 4 ? 2 : 1);
			const auto CurrentIdentity = TWeakObjectPtr<AActor>(Weapon->GetSpawnedWeapon());
			TestTrue(TEXT("Current replacement weapon exists"), CurrentIdentity.IsValid());
			TestFalse(TEXT("Replacement owns a distinct UObject lifetime even when allocator addresses are reused"),
				CurrentIdentity.HasSameIndexAndSerialNumber(PreviousWeaponIdentity));
			if (CurrentIdentity.IsValid() && reinterpret_cast<UPTRINT>(Weapon->GetSpawnedWeapon()) == PreviousWeaponAddress)
				AddInfo(FString::Printf(TEXT("Callback mode%d reused the retired raw address; UObject lifetime identity was checked independently"), CallbackMode));
			const auto* Expected = CallbackMode >= 2 ? Latest : Next;
			TestTrue(TEXT("Latest profile selection is retained"), Player->GetCurrentWeaponPresentationProfile() == Expected);
			const TWeakObjectPtr<AActor> ReplacementIdentity(Weapon->GetSpawnedWeapon());
			const uint32 BeforeStableQuery = Presentation->VisualAssetRevision;
			Weapon->RefreshPresentation();
			TestTrue(TEXT("Latest profile was applied, not just left as selected metadata"), ReplacementIdentity.IsValid() &&
				ReplacementIdentity.HasSameIndexAndSerialNumber(TWeakObjectPtr<AActor>(Weapon->GetSpawnedWeapon())));
			TestEqual(TEXT("Stable weapon query does not retire combat generation"), Presentation->VisualAssetRevision, BeforeStableQuery);
		}
	}

	// Real shared loader callback, using a valid wrong-type package so this
	// resource-admission boundary never tries to create a Niagara component.
	auto Async = MakeShared<FSoftCueWorld>(); Async->SetCue(Cube, true);
	Async->Cue->BeginAttackPresentation(TestAttackTag); Async->Cue->PlayCue(TestCueTag);
	TestEqual(TEXT("Actual shared loader is used"), Async->Assets->GetLeaseCount(), 1);
	TestEqual(TEXT("No inline application on Request"), Async->Assets->GetStats().Delivered, uint64(0));
	const double Began = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FSoftCueBoundaryCommand([this, Async, Began]()
	{
		if (FPlatformTime::Seconds() - Began > 15) { AddError(TEXT("Soft cue loader fixture timed out")); return true; }
		Async->Assets->Tick(0);
		if (Async->Assets->GetStats().Delivered == 0) { return false; }
		TestEqual(TEXT("Wrong-type completion clears failed component residency"), Async->Cue->VisualAssetLeases.Num(), 0);
		TestEqual(TEXT("Wrong-type completion does not retain a desired loop"), Async->Cue->PendingLoopingCues.Num(), 0);
		TestEqual(TEXT("No Niagara component spawned from wrong-type asset"), Async->Cue->ActiveLoopingCues.Num(), 0);
		Async->SetCue(FSoftObjectPath(TEXT("/Engine/BasicShapes/Sphere.Sphere")), false);
		Async->Cue->RefreshPresentation(); Async->Cue->PlayCue(TestCueTag);
		TWeakObjectPtr<UProject_JCombatPresentationComponent> Weak(Async->Cue);
		Async->Cue->DestroyComponent(); Async->NPC->Destroy();
		Async->Cue = nullptr; Async->NPC = nullptr; Async->Set = nullptr; Async->Profile = nullptr;
		CollectGarbage(RF_NoFlags, true);
		TestFalse(TEXT("Cancelled component does not survive through a loader capture"), Weak.IsValid());
		TestEqual(TEXT("Destroy/GC releases pending and resident leases"), Async->Assets->GetLeaseCount(), 0);
		Async->Assets->Tick(0);
		TestEqual(TEXT("No extra apply after weak consumer cancellation"), Async->Assets->GetStats().Delivered, uint64(1));
		return true;
	}));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGameplayWarningTest, "ProjectJ.Integrated.GameplayWarningLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)
bool FProjectJGameplayWarningTest::RunTest(const FString&)
{
    FSoftCueWorld S;
    S.NPC->AutoPossessAI = EAutoPossessAI::Disabled;
    if (AController* Controller = S.NPC->GetController()) { Controller->UnPossess(); }
    const TStrongObjectPtr<UProject_JCombatPresentationSet> LateSet(S.Set);
    const TStrongObjectPtr<UProject_JAttackPresentationProfile> LateProfile(S.Profile);
    S.Cue->BasePresentationSet = nullptr; // A genuinely absent profile, not merely an unloaded Niagara payload.
    S.World->InitializeActorsForPlay(FURL());
    S.World->GetWorldSettings()->NotifyBeginPlay();
    if (!TestTrue(TEXT("Real world dispatch begins the presentation component"), S.Cue->HasBegunPlay())) return false;
    S.Cue->BeginAttackPresentation(TestAttackTag);
    TestFalse(TEXT("Nonlocal NPC is not protected by the cosmetic local-owner exception"), S.NPC->IsLocallyControlled());
    auto* Budget = S.World->GetSubsystem<UProject_JPresentationBudgetSubsystem>();
    for (int32 Index = 0; Index < Budget->MaxApplications; ++Index)
    {
        TestTrue(TEXT("Normal public cosmetic admission fills this real frame's credits"), Budget->TryAcquireOneShot(S.Cue));
    }
    TestFalse(TEXT("The next ordinary cosmetic is rejected"), Budget->TryAcquireOneShot(S.Cue));
    const int32 Applications = Budget->GetStats().FrameApplications;
    TStrongObjectPtr<UProject_JAnimNotifyState_CombatPresentationCue> Notify(NewObject<UProject_JAnimNotifyState_CombatPresentationCue>());
    Notify->CueTag = TestCueTag; Notify->Purpose = EProject_JCombatCuePurpose::GameplayWarning;
    FAnimNotifyEventReference Event; Event.SetNotifyInstanceID(153);
    Notify->NotifyBegin(S.NPC->GetMesh(), nullptr, 1.0f, Event);
    auto* Marker = S.Cue->WarningMarker.Get();
    if (!TestNotNull(TEXT("Actual warning producer creates a native baseline without any profile"), Marker)) return false;
    TestTrue(TEXT("Native line component is registered with a real RHI render state"), Marker->IsRegistered() && Marker->IsRenderStateCreated());
    TestEqual(TEXT("Merged baseline contains exactly twenty native lines"), Marker->BatchedLines.Num(), 20);
    TestFalse(TEXT("No marker/component update tick is introduced"), Marker->IsComponentTickEnabled());
    TestTrue(TEXT("Shipping marker uses the concrete local-space runtime proxy"), Marker->IsA<UProject_JGameplayWarningMarkerComponent>());
    const FVector PreviousBoundsOrigin = Marker->Bounds.Origin;
    const FVector Move(25, 10, 0);
    TestTrue(TEXT("Normal owner root movement succeeds"), S.NPC->SetActorLocation(S.NPC->GetActorLocation() + Move));
    TestTrue(TEXT("Same tickless marker follows the actual owner root"), S.Cue->WarningMarker.Get() == Marker
        && Marker->GetAttachParent() == S.NPC->GetRootComponent()
        && (Marker->Bounds.Origin - PreviousBoundsOrigin).Equals(Move, KINDA_SMALL_NUMBER));
    TestTrue(TEXT("Warning reaches persistent authority recovery despite the missing profile"),
        S.Cue->ReplicatedPresentationState.ActiveWarningCueTags.HasTagExact(TestCueTag));
    TestEqual(TEXT("Core warning consumes no exhausted Niagara spawn credits"), Budget->GetStats().FrameApplications, Applications);
    TestEqual(TEXT("Cold profile baseline issues no asset load"), S.Assets->GetStats().Accepted, uint64(0));

    // Native cosmetic payload remains separately optional/budgeted, including its authored EffectType.
    S.SetCue(FSoftObjectPath(TEXT("/Game/Effects/ProjectJ/Combat/NS_CombatTrail.NS_CombatTrail")), true);
    S.Cue->BasePresentationSet = S.Set;
    const auto* LateCue = S.Cue->ResolveCue(TestCueTag);
    if (!TestNotNull(TEXT("Late profile resolves the real cosmetic cue"), LateCue)) return false;
    TestTrue(TEXT("Actual optional decoration uses the remote spawn-budget branch"), S.Cue->ShouldBudgetRemoteSpawn(*LateCue));
    S.Cue->RefreshPresentation();
    TestEqual(TEXT("Late profile/style refresh preserves the exact producer-owned core"), S.Cue->WarningMarker.Get(), Marker);
    const uint64 Cosmetic = S.Cue->BeginCueLease(TestCueTag);
    const uint64 Warning = S.Cue->BeginCueLease(TestCueTag, true);
    const uint64 OtherWarning = S.Cue->BeginCueLease(TestAttackTag, true);
    TestTrue(TEXT("Distinct simultaneous warning and cosmetic leases are admitted"), Cosmetic && Warning && OtherWarning);
    TestEqual(TEXT("Multiple warnings do not create more marker components/lines"), S.Cue->WarningMarker.Get(), Marker);
    TestEqual(TEXT("Merged native geometry remains bounded at twenty lines"), Marker->BatchedLines.Num(), 20);
    S.Cue->EndCueLease(Cosmetic);
    TestEqual(TEXT("A cosmetic end cannot remove same-tag warning protection"), S.Cue->WarningMarker.Get(), Marker);
    const uint64 RemainingCosmetic = S.Cue->BeginCueLease(TestCueTag);
    Notify->NotifyEnd(S.NPC->GetMesh(), nullptr, Event);
    TestEqual(TEXT("One warning notify end preserves the other warning lease"), S.Cue->WarningMarker.Get(), Marker);
    S.Cue->EndCueLease(Warning);
    TestFalse(TEXT("Last same-tag warning end removes its meaning while a cosmetic lease continues"), S.Cue->ActiveWarningCues.HasTagExact(TestCueTag));
    TestTrue(TEXT("The continuing cosmetic lease remains current"), S.Cue->IsCueLeaseCurrent(RemainingCosmetic));
    TestEqual(TEXT("Another tag's warning keeps the one shared marker"), S.Cue->WarningMarker.Get(), Marker);
    const TWeakObjectPtr<ULineBatchComponent> OldMarker(Marker);
    S.Cue->EndCueLease(OtherWarning);
    TestNull(TEXT("Final warning end detaches the baseline"), S.Cue->WarningMarker.Get());
    TestTrue(TEXT("The retired native component is destroyed or destroying"), !OldMarker.IsValid() || OldMarker->IsBeingDestroyed());
    const uint64 Replacement = S.Cue->BeginCueLease(TestCueTag, true);
    auto* ReplacementMarker = S.Cue->WarningMarker.Get();
    S.Cue->EndCueLease(Warning);
    TestTrue(TEXT("Old warning end cannot retire its replacement"), Replacement && S.Cue->WarningMarker.Get() == ReplacementMarker);
    S.Cue->BeginAttackPresentation(TestAttackTag);
    TestNull(TEXT("Same-tag new attack retires old warning baseline"), S.Cue->WarningMarker.Get());
    TestFalse(TEXT("Old attack warning lease is no longer current"), S.Cue->IsCueLeaseCurrent(Replacement));
    S.Cue->BeginCueLease(TestCueTag, true);
    const TWeakObjectPtr<ULineBatchComponent> EndPlayMarker(S.Cue->WarningMarker.Get());
    const TStrongObjectPtr<AProject_JNPCCharacter> EndingOwner(S.NPC);
    const TStrongObjectPtr<UProject_JCombatPresentationComponent> EndingCue(S.Cue);
    TestTrue(TEXT("Normal actor destruction routes actual begun component EndPlay"), EndingOwner->Destroy());
    TestNull(TEXT("Existing component EndPlay retires core ownership"), EndingCue->WarningMarker.Get());
    TestTrue(TEXT("EndPlay retires the actual native signal component"), !EndPlayMarker.IsValid() || EndPlayMarker->IsBeingDestroyed());

#if WITH_EDITOR
    FSoftCueWorld Server(true);
    Server.Cue->BasePresentationSet = nullptr;
    Server.World->InitializeActorsForPlay(FURL());
    Server.World->GetWorldSettings()->NotifyBeginPlay();
    if (!TestTrue(TEXT("Dedicated fixture uses actual component BeginPlay"), Server.Cue->HasBegunPlay())) return false;
    Server.Cue->BeginAttackPresentation(TestAttackTag);
    const uint64 ServerLease = Server.Cue->BeginCueLease(TestCueTag, true);
    TestTrue(TEXT("Dedicated authority publishes warning without a profile"), ServerLease &&
        Server.Cue->ReplicatedPresentationState.ActiveWarningCueTags.HasTagExact(TestCueTag));
    TestNull(TEXT("Dedicated authority creates no visual marker"), Server.Cue->WarningMarker.Get());
    TestEqual(TEXT("Dedicated warning requests no assets"), Server.Assets->GetStats().Accepted, uint64(0));
    FSoftCueWorld Receiver;
    Receiver.NPC->AutoPossessAI = EAutoPossessAI::Disabled;
    if (AController* Controller = Receiver.NPC->GetController()) { Controller->UnPossess(); }
    Receiver.Cue->BasePresentationSet = nullptr;
    Receiver.World->InitializeActorsForPlay(FURL());
    Receiver.World->GetWorldSettings()->NotifyBeginPlay();
    if (!TestTrue(TEXT("Recovery fixture uses actual component BeginPlay"), Receiver.Cue->HasBegunPlay())) return false;
    TestFalse(TEXT("Recovery owner also has no local AI-controller exemption"), Receiver.NPC->IsLocallyControlled());
    const auto WarningStart = Server.Cue->ReplicatedPresentationState;
    Receiver.Cue->ReplicatedPresentationState = WarningStart;
    Receiver.Cue->ApplyReplicatedState();
    TestTrue(TEXT("Existing recovery consumer creates a real baseline for a nonlocal owner without a profile"),
        IsValid(Receiver.Cue->WarningMarker) && Receiver.Cue->WarningMarker->IsRegistered());
    auto* RecoveredMarker = Receiver.Cue->WarningMarker.Get();
    Receiver.Cue->ApplyReplicatedState();
    TestEqual(TEXT("A duplicate recovery cannot allocate another native marker"), Receiver.Cue->WarningMarker.Get(), RecoveredMarker);
    Server.Cue->EndCueLease(ServerLease);
    Receiver.Cue->ReplicatedPresentationState = Server.Cue->ReplicatedPresentationState;
    Receiver.Cue->ApplyReplicatedState();
    TestNull(TEXT("Exact authority warning end recovers its removal too"), Receiver.Cue->WarningMarker.Get());
    Receiver.Cue->ReplicatedPresentationState = WarningStart;
    Receiver.Cue->ApplyReplicatedState();
    TestNull(TEXT("Old start order cannot resurrect a recovered end"), Receiver.Cue->WarningMarker.Get());
    AddInfo(TEXT("Recovery is exercised through the existing consumer in native worlds; this is not a socket replication/pixel-visibility or offscreen-UI proof."));
#endif
    return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPresentationCueLifetimeValidationTest,
	"ProjectJ.Integrated.PresentationCueLifetimeValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPresentationCueLifetimeValidationTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UProject_JAttackPresentationProfile> Profile(NewObject<UProject_JAttackPresentationProfile>());
	TStrongObjectPtr<UNiagaraSystem> System(NewObject<UNiagaraSystem>());
	TStrongObjectPtr<UNiagaraEffectType> Type(NewObject<UNiagaraEffectType>());
	auto& Cue = Profile->Cues.AddDefaulted_GetRef();
	Cue.CueTag = TestCueTag; Cue.NiagaraSystem = System.Get();
	// Only test-owned transient authoring objects use this WITH_EDITOR API.
	// No gameplay component or renderer is needed to validate a lifetime contract.
	System->SetEffectType(Type.Get());
	const auto& Effective = System->GetScalabilitySettings();
	TestFalse(TEXT("Fixture effective distance culling is disabled"), bool(Effective.bCullByDistance));
	TestFalse(TEXT("Fixture effective type-count culling is disabled"), bool(Effective.bCullMaxInstanceCount));
	TestFalse(TEXT("Fixture effective system-count culling is disabled"), bool(Effective.bCullPerSystemMaxInstanceCount));
	TestFalse(TEXT("Fixture effective visibility culling is disabled"),
		bool(Effective.VisibilityCulling.bCullWhenNotRendered || Effective.VisibilityCulling.bCullByViewFrustum));
	TestFalse(TEXT("Fixture effective global-budget culling is disabled"), bool(Effective.BudgetScaling.bCullByGlobalBudget));
	struct FReactionCase { ENiagaraCullReaction Reaction; const TCHAR* Name; bool bResumes; };
	const FReactionCase Cases[] = {
		{ENiagaraCullReaction::DeactivateResume, TEXT("DeactivateResume"), true},
		{ENiagaraCullReaction::DeactivateImmediateResume, TEXT("DeactivateImmediateResume"), true},
		{ENiagaraCullReaction::PauseResume, TEXT("PauseResume"), true},
		{ENiagaraCullReaction::Deactivate, TEXT("Deactivate"), false},
		{ENiagaraCullReaction::DeactivateImmediate, TEXT("DeactivateImmediate"), false}
	};
	for (const auto& Case : Cases)
	{
		Type->CullReaction = Case.Reaction;
		Cue.bLooping = false;
		FDataValidationContext OneShotContext;
		const auto OneShotResult = Profile->IsDataValid(OneShotContext);
		TestTrue(FString::Printf(TEXT("One-shot %s has the expected validation result"), Case.Name),
			OneShotResult == (Case.bResumes ? EDataValidationResult::Invalid : EDataValidationResult::Valid));
		TestEqual(FString::Printf(TEXT("One-shot %s reports exactly its lifetime violation"), Case.Name),
			OneShotContext.GetNumErrors(), Case.bResumes ? uint32(1) : uint32(0));
		Cue.bLooping = true;
		FDataValidationContext OwnedContext;
		TestTrue(FString::Printf(TEXT("Notify-owned loop %s preserves its policy"), Case.Name),
			Profile->IsDataValid(OwnedContext) == EDataValidationResult::Valid);
		TestEqual(FString::Printf(TEXT("Notify-owned loop %s has no lifetime error"), Case.Name),
			OwnedContext.GetNumErrors(), uint32(0));
	}
	Cue.bLooping = false; System->SetEffectType(nullptr);
	FDataValidationContext LegacyContext;
	TestTrue(TEXT("An existing system without EffectType keeps its prior validation contract"),
		Profile->IsDataValid(LegacyContext) == EDataValidationResult::Valid);
	TestEqual(TEXT("No EffectType is silently assigned by validation"), System->GetEffectType(), static_cast<UNiagaraEffectType*>(nullptr));

	// The real authored profile is read as a whole. A newly unsafe saved cue is
	// reported, never rewritten or relabelled to make this test succeed.
	TStrongObjectPtr<UProject_JAttackPresentationProfile> SavedProfile(LoadObject<UProject_JAttackPresentationProfile>(nullptr,
		TEXT("/Game/DataAssetSets/Presentation/Spellblade/DA_AP_Spellblade/DA_AP_Spellblade.DA_AP_Spellblade")));
	if (!TestNotNull(TEXT("Actual saved Spellblade presentation profile loads"), SavedProfile.Get())) return false;
	TestTrue(TEXT("Actual saved profile contains authored cues"), !SavedProfile->Cues.IsEmpty());
	TMap<UPackage*, bool> PackageDirtyStates;
	TArray<TStrongObjectPtr<UObject>> AssetPins;
	const auto TrackAsset = [&](UObject* Asset)
	{
		if (!Asset) return;
		AssetPins.Emplace(Asset);
		UPackage* Package = Asset->GetOutermost();
		if (!PackageDirtyStates.Contains(Package))
		{
			PackageDirtyStates.Add(Package, Package->IsDirty());
			TestFalse(FString::Printf(TEXT("Saved package starts clean: %s"), *Package->GetName()), Package->IsDirty());
		}
	};
	TrackAsset(SavedProfile.Get());
	for (const auto& SavedCue : SavedProfile->Cues)
	{
		UNiagaraSystem* SavedSystem = SavedCue.NiagaraSystem.LoadSynchronous();
		if (!TestNotNull(FString::Printf(TEXT("Saved cue system loads: %s"), *SavedCue.CueTag.ToString()), SavedSystem)) continue;
		TrackAsset(SavedSystem);
		UNiagaraEffectType* SavedType = SavedSystem->GetEffectType();
		TrackAsset(SavedType);
		AddInfo(FString::Printf(TEXT("Saved cue %s: notifyOwned=%d, system=%s, EffectType=%s, reaction=%d"),
			*SavedCue.CueTag.ToString(), SavedCue.bLooping, *SavedSystem->GetPathName(),
			*GetPathNameSafe(SavedType), SavedType ? int32(SavedType->CullReaction) : -1));
	}
	FDataValidationContext SavedContext;
	TestTrue(TEXT("Every actual saved Spellblade cue obeys the authoring lifetime contract"),
		SavedProfile->IsDataValid(SavedContext) == EDataValidationResult::Valid);
	TestEqual(TEXT("Actual saved Spellblade profile reports no validation errors"), SavedContext.GetNumErrors(), uint32(0));
	for (const auto& Issue : SavedContext.GetIssues()) AddInfo(Issue.Message.ToString());
	for (const auto& Pair : PackageDirtyStates)
		TestEqual(FString::Printf(TEXT("Validation preserves package dirty state: %s"), *Pair.Key->GetName()),
			Pair.Key->IsDirty(), Pair.Value);
	return true;
}
#endif
#endif
