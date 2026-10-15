#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "UI/Project_JCharacterUIBindingComponent.h"
#include "UI/Project_JCharacterViewModel.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "NativeGameplayTags.h"
#include "GameFramework/Actor.h"
#include "IFieldNotificationClassDescriptor.h"
#include "UObject/GarbageCollection.h"
#include "Tests/Project_JPlayerMaturityFixtures.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/UnrealType.h"
#include "Mount/Project_JMountComponent.h"
#include "Tests/Project_JMountLifecycleFixture.h"
#include "Project_JGameplayTags.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Mass/Project_JMassRepresentationSubsystem.h"
#include "Mass/Project_JMassMovementProcessor.h"
#include "MassEntityManager.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/Project_JNPCActivationComponent.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "Components/Project_JPlayerInputBindingComponent.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "Components/Project_JSkillInputRouterComponent.h"
#include "Components/Project_JCombatIntroComponent.h"
#include "AIController.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Animation/AnimMontage.h"
#include "System/Project_JNPCPathSubsystem.h"
#include "System/Project_JTargetScoringSubsystem.h"
#include "System/Project_JVisualAssetSubsystem.h"
#include <limits>

UE_DEFINE_GAMEPLAY_TAG_STATIC(ReauditAttackTag,"ProjectJ.Tests.Reaudit.Attack");
UE_DEFINE_GAMEPLAY_TAG_STATIC(ReauditModifierTag,"InputTag.Tests.Reaudit.Modifier");

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCharacterUIBindingReentryTest,
    "ProjectJ.Reaudit.CharacterUIBindingReentry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCharacterUIBindingReentryTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT {World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    auto* Owner=World->SpawnActor<AActor>();
    auto MakeASC=[Owner](float Health){
        auto* ASC=NewObject<UProject_JAbilitySystemComponent>(Owner);ASC->RegisterComponent();ASC->InitAbilityActorInfo(Owner,Owner);
        auto* Attributes=NewObject<UProject_JAttributeSet>(Owner);ASC->AddAttributeSetSubobject(Attributes);
        ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(),100);
        ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(),Health);
        ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxManaAttribute(),100);
        return ASC;
    };
    auto* A=MakeASC(10);auto* B=MakeASC(70);auto* C=MakeASC(30);
    auto* Binding=NewObject<UProject_JCharacterUIBindingComponent>(Owner);Binding->RegisterComponent();
    Binding->InitializeFromAttributes(A,const_cast<UProject_JAttributeSet*>(A->GetSet<UProject_JAttributeSet>()),1);
    auto* Model=Binding->GetCharacterViewModel();
    if(!TestNotNull(TEXT("A live remote demand creates the actual MVVM model"),Model)){return false;}
    bool bReentered=false;
    const auto HealthField=Model->GetFieldNotificationDescriptor().GetField(Model->GetClass(),TEXT("Health"));
    const auto Handle=Model->AddFieldValueChangedDelegate(HealthField,
        INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda([&](UObject*,UE::FieldNotification::FFieldId){
            if(!bReentered){bReentered=true;Binding->InitializeFromAttributes(B,const_cast<UProject_JAttributeSet*>(B->GetSet<UProject_JAttributeSet>()),9);}
        }));
    Binding->InitializeFromAttributes(C,const_cast<UProject_JAttributeSet*>(C->GetSet<UProject_JAttributeSet>()),3);
    Model->RemoveFieldValueChangedDelegate(HealthField,Handle);
    TestTrue(TEXT("Native FieldNotify synchronously reentered the binding"),bReentered);
    TestEqual(TEXT("The latest source is presented"),Model->GetHealth(),70.f);
    Binding->ReleaseLegacyViewModelRequest();
    Binding->InitializeFromAttributes(A,const_cast<UProject_JAttributeSet*>(A->GetSet<UProject_JAttributeSet>()),1);
    Model=Binding->GetCharacterViewModel();
    B->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(),75);
    TestEqual(TEXT("Retired B delegates cannot overwrite the newly bound A model"),Model->GetHealth(),10.f);
    A->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(),15);
    TestEqual(TEXT("The current source still publishes actual attribute updates"),Model->GetHealth(),15.f);
    const TWeakObjectPtr<UProject_JCharacterViewModel> WeakModel(Model);
    bool bCallbackHeldModel=false;
    Model->AddFieldValueChangedDelegate(HealthField,
        INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda([&](UObject*,UE::FieldNotification::FFieldId){
            Binding->ReleaseLegacyViewModelRequest();
            CollectGarbage(RF_NoFlags,true);
            bCallbackHeldModel=WeakModel.IsValid();
        }));
    A->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(),20);
    TestTrue(TEXT("FieldNotify keeps its model alive through release and GC"),bCallbackHeldModel);
    CollectGarbage(RF_NoFlags,true);
    TestFalse(TEXT("Callback pins do not retain the retired model afterward"),WeakModel.IsValid());
    Binding->DestroyComponent();
    TestNull(TEXT("Destroyed binding cannot reacquire a model"),Binding->GetCharacterViewModel());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCombatPresentationRetirementTest,
    "ProjectJ.Reaudit.CombatPresentationRetirement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCombatPresentationRetirementTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT {World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    auto* Owner=World->SpawnActor<AActor>();
    auto* Presentation=NewObject<UProject_JCombatPresentationComponent>(Owner);Presentation->RegisterComponent();
    const FGameplayTag Attack=ReauditAttackTag;
    Presentation->BeginAttackPresentation(Attack);
    if(!TestEqual(TEXT("Live presentation accepts a valid native attack identity"),Presentation->GetActiveAttackTag(),Attack)){return false;}
    Presentation->DestroyComponent();
    TestFalse(TEXT("Pre-BeginPlay destruction retires the active presentation identity"),Presentation->GetActiveAttackTag().IsValid());
    Presentation->BeginAttackPresentation(Attack);
    TestFalse(TEXT("A retired presentation rejects a late attack begin"),Presentation->GetActiveAttackTag().IsValid());
    Presentation->EndAttackPresentation();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMovementPublicationTest,
    "ProjectJ.Reaudit.MovementPublication", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMovementPublicationTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT {World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    auto* Character=World->SpawnActor<AProject_JCombatFacingFixture>();
    auto* Profile=NewObject<UProject_JLocomotionProfile>(Character);
    FindFProperty<FObjectPropertyBase>(Character->GetClass(),TEXT("CharacterAnimProfile"))->SetObjectPropertyValue_InContainer(Character,nullptr);
    FindFProperty<FObjectPropertyBase>(Character->GetClass(),TEXT("LocomotionProfile"))->SetObjectPropertyValue_InContainer(Character,Profile);
    if(!TestTrue(TEXT("Fixture selects the real profile resolver"),Character->GetLocomotionProfile()==Profile)){return false;}
    auto* Movement=Character->GetCharacterMovement();
    Movement->MaxWalkSpeed=432;Movement->MaxAcceleration=111;Movement->BrakingDecelerationWalking=222;
    Movement->GroundFriction=3;Movement->RotationRate.Yaw=99;
    Profile->WalkSpeed=std::numeric_limits<float>::quiet_NaN();
    Profile->MovementPolicy.RunMaxAcceleration=999;
    Character->UpdateTestMovementPolicy();
    TestEqual(TEXT("Nonfinite speed cannot poison the CMC"),Movement->MaxWalkSpeed,432.f);
    TestEqual(TEXT("Invalid policy cannot partially publish acceleration"),Movement->MaxAcceleration,111.f);
    TestEqual(TEXT("Invalid policy preserves braking"),Movement->BrakingDecelerationWalking,222.f);
    TestEqual(TEXT("Invalid policy preserves friction"),Movement->GroundFriction,3.f);
    TestEqual(TEXT("Invalid policy preserves rotation"),Movement->RotationRate.Yaw,99.0);
    Profile->WalkSpeed=120;
    Character->UpdateTestMovementPolicy();
    TestEqual(TEXT("Corrected live profile resumes normal publication"),Movement->MaxWalkSpeed,120.f);
    TestEqual(TEXT("The complete valid policy is published"),Movement->MaxAcceleration,999.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMountTagReentryTest,
    "ProjectJ.Reaudit.MountTagReentry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMountTagReentryTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT {World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    auto* Rider=World->SpawnActor<AProject_JPlayerMaturityASCFixture>();
    auto* Controller=World->SpawnActor<APlayerController>();Controller->Possess(Rider);
    auto* ASC=NewObject<UProject_JAbilitySystemComponent>(Rider);ASC->RegisterComponent();ASC->InitAbilityActorInfo(Rider,Rider);Rider->CurrentASC=ASC;
    auto* Component=NewObject<UProject_JMountComponent>(Rider);Component->RegisterComponent();
    auto* First=World->SpawnActor<AProject_JMountLifecycleFixture>();
    auto* Second=World->SpawnActor<AProject_JMountLifecycleFixture>();
    const auto Tag=FProject_JGameplayTags::Get().State_Mounted;
    bool bInterrupted=false,bRemountAttempted=false,bRemounted=false;
    const auto Handle=ASC->RegisterGameplayTagEvent(Tag,EGameplayTagEventType::AnyCountChange).AddLambda(
        [&](const FGameplayTag,int32 Count){
            if(Count==1&&!bInterrupted){bInterrupted=true;First->DismountRider(true);}
            else if(Count==0&&bInterrupted&&!bRemountAttempted){bRemountAttempted=true;bRemounted=Component->RequestMount(Second);}
        });
    TestFalse(TEXT("Interrupted first mount does not regain possession"),Component->RequestMount(First));
    ASC->RegisterGameplayTagEvent(Tag,EGameplayTagEventType::AnyCountChange).Remove(Handle);
    TestTrue(TEXT("Tag callback used the public request to mount the second actor"),bRemounted);
    TestEqual(TEXT("Latest mount owns the rider relationship"),Component->GetMountedMount(),static_cast<AProject_JMountCharacter*>(Second));
    TestTrue(TEXT("Latest mount owns possession"),Controller->GetPawn()==Second);
    TestEqual(TEXT("Nested transition has exactly one mounted contribution"),ASC->GetTagCount(Tag),1);
    TestTrue(TEXT("Retired first mount cannot restore movement over the new session"),Rider->GetCharacterMovement()->MovementMode==MOVE_None);
    Second->DismountRider(true);
    TestEqual(TEXT("Final dismount releases the only contribution"),ASC->GetTagCount(Tag),0);
    Component->DestroyComponent();
    TestFalse(TEXT("Destroyed rider component cannot reopen a mount request"),Component->RequestMount(First));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJFollowerIdentityReplacementTest,
    "ProjectJ.Reaudit.FollowerIdentityReplacement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJFollowerIdentityReplacementTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT {World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    auto* Character=World->SpawnActor<ACharacter>();auto* Source=Character->GetMesh();
    auto MakeFollower=[&](){
        auto* Mesh=NewObject<USkeletalMeshComponent>(Character);Mesh->SetupAttachment(Source);Mesh->RegisterComponent();
        Mesh->AnimScriptInstance=NewObject<UProject_JRetargetAnimInstance>(Mesh);return Mesh;
    };
    auto* Old=MakeFollower();
    auto* Anim=NewObject<UProject_JCharacterAnimInstance>(Source);Source->AnimScriptInstance=Anim;Anim->InitializeAnimation();
    TestEqual(TEXT("Initial retarget follower is found"),Anim->GetRuntimeRetargetFollowerMesh(),Old);
    Old->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    auto* Current=MakeFollower();
    TestEqual(TEXT("Detached but live old component cannot retain the pose-source role"),Anim->GetRuntimeRetargetFollowerMesh(),Current);
    Current->DestroyComponent();
    TestNull(TEXT("No attached follower releases the role"),Anim->GetRuntimeRetargetFollowerMesh());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJComponentPlayReuseTest,
    "ProjectJ.Reaudit.ComponentPlayReuse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJComponentPlayReuseTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT {World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    World->InitializeActorsForPlay(FURL());World->GetWorldSettings()->NotifyBeginPlay();World->GetWorldSettings()->NotifyMatchStarted();
    auto* Rider=World->SpawnActor<AProject_JPlayerMaturityASCFixture>();
    auto* Controller=World->SpawnActor<APlayerController>();Controller->Possess(Rider);
    auto* ASC=NewObject<UProject_JAbilitySystemComponent>(Rider);ASC->RegisterComponent();ASC->InitAbilityActorInfo(Rider,Rider);Rider->CurrentASC=ASC;
    auto* Mounting=NewObject<UProject_JMountComponent>(Rider);Mounting->RegisterComponent();
    auto* Presentation=NewObject<UProject_JCombatPresentationComponent>(Rider);Presentation->RegisterComponent();
    auto* Mount=World->SpawnActor<AProject_JMountLifecycleFixture>();
    if(!TestTrue(TEXT("Registered mount component actually began play"),Mounting->HasBegunPlay()) ||
       !TestTrue(TEXT("Registered presentation component actually began play"),Presentation->HasBegunPlay())){return false;}
    Presentation->BeginAttackPresentation(ReauditAttackTag);
    TestEqual(TEXT("First play accepts attack presentation"),Presentation->GetActiveAttackTag(),ReauditAttackTag.GetTag());
    TestTrue(TEXT("First play accepts mounting"),Mounting->RequestMount(Mount));
    Rider->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
    TestFalse(TEXT("EndPlay releases the mount relationship"),Mounting->IsMounted());
    TestFalse(TEXT("EndPlay releases attack presentation"),Presentation->GetActiveAttackTag().IsValid());
    Rider->DispatchBeginPlay(true);
    Controller->Possess(Rider);ASC->InitAbilityActorInfo(Rider,Rider);
    Presentation->BeginAttackPresentation(ReauditAttackTag);
    TestEqual(TEXT("New play lifecycle reopens presentation admission"),Presentation->GetActiveAttackTag(),ReauditAttackTag.GetTag());
    TestTrue(TEXT("New play lifecycle reopens mount admission"),Mounting->RequestMount(Mount));
    Mount->DismountRider(true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAuthoredFollowerRoleTest,
    "ProjectJ.Reaudit.AuthoredFollowerRole", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAuthoredFollowerRoleTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT {World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    auto* Class=LoadClass<ACharacter>(nullptr,TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_Greatsword_C"));
    if(!TestNotNull(TEXT("Actual character Blueprint class loads"),Class)){return false;}
    FActorSpawnParameters Parameters;Parameters.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Character=World->SpawnActor<ACharacter>(Class,Parameters);
    if(!TestNotNull(TEXT("Actual Blueprint construction creates a character"),Character)){return false;}
    auto* Source=Character->GetMesh();auto* Anim=Cast<UProject_JCharacterAnimInstance>(Source->GetAnimInstance());
    if(!TestNotNull(TEXT("Actual source uses native Master anim instance"),Anim)){return false;}
    auto* Follower=Anim->GetRuntimeRetargetFollowerMesh();
    if(!TestNotNull(TEXT("Common resolver finds the actual constructed follower"),Follower)){return false;}
    TestTrue(TEXT("Actual follower remains attached to the source"),Follower->GetAttachParent()==Source);
    TestTrue(TEXT("Actual follower keeps the native retarget ABP contract"),Follower->GetAnimInstance()&&Follower->GetAnimInstance()->IsA<UProject_JRetargetAnimInstance>());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMassReentrantRetirementTest,
    "ProjectJ.Reaudit.MassReentrantRetirement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMassReentrantRetirementTest::RunTest(const FString&)
{
    for (const bool bCollectDuringCallback : {false, true})
    for (const bool bDuringUnregister : {false, true})
    {
        auto* World=UWorld::CreateWorld(EWorldType::Game,false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        bool bWorldDestroyed=false;
        ON_SCOPE_EXIT {if(!bWorldDestroyed){World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);}GEngine->DestroyWorldContext(World);};
        FActorSpawnParameters Parameters;Parameters.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* NPC=World->SpawnActor<AProject_JMassRetirementFixture>(AProject_JMassRetirementFixture::StaticClass(),FVector(0,0,100),FRotator::ZeroRotator,Parameters);
        World->InitializeActorsForPlay(FURL());World->GetWorldSettings()->NotifyBeginPlay();World->GetWorldSettings()->NotifyMatchStarted();
        auto* Bridge=World->GetSubsystem<UProject_JMassRepresentationSubsystem>();
        if(!TestNotNull(TEXT("Actual game world supplies the opt-in Mass bridge"),Bridge)){return false;}
        TStrongObjectPtr<UProject_JMassRepresentationSubsystem> PinnedBridge(Bridge);
        TStrongObjectPtr<AProject_JMassRetirementFixture> PinnedNPC(NPC);
        const TWeakObjectPtr<UProject_JMassRepresentationSubsystem> WeakBridge(Bridge);
        const TWeakObjectPtr<AProject_JMassRetirementFixture> WeakNPC(NPC);
        NPC->GetAttributeSet()->InitMaxHealth(100);NPC->GetAttributeSet()->InitHealth(73);
        NPC->GetAbilitySystemComponent()->InitAbilityActorInfo(NPC,NPC);
        Bridge->SetObserversForTest({FVector(20000,0,0)});
        const auto Token=Bridge->RegisterNPC(NPC);
        if(!TestTrue(TEXT("Authority NPC registers a real Mass entity"),Token.IsValid())){return false;}
        auto Path=MakeShared<FNavigationPath,ESPMode::ThreadSafe>();
        Path->GetPathPoints().Add(FNavPathPoint(NPC->GetNavAgentLocation()));
        Path->GetPathPoints().Add(FNavPathPoint(NPC->GetNavAgentLocation()+FVector(10000,0,0)));Path->MarkReady();
        if(!TestTrue(TEXT("Real route is accepted before the ownership handoff"),Bridge->SetRoute(Token,Path))){return false;}
        const bool bMovementTick=NPC->GetCharacterMovement()->IsComponentTickEnabled();
        const bool bMeshTick=CastChecked<UProject_JBudgetedSkeletalMeshComponent>(NPC->GetMesh())->GetRequestedTickEnabled();
        const bool bHidden=NPC->IsHidden(),bCollision=NPC->GetActorEnableCollision(),bDamage=NPC->CanBeDamaged();
        const auto SavedEntities=Bridge->Entities;
        TStrongObjectPtr<UProject_JMassMovementProcessor> SavedProcessor(Bridge->Processor.Get());
        const TWeakObjectPtr<UProject_JMassMovementProcessor> WeakProcessor(SavedProcessor.Get());
        bool bCallback=false,bFixtureRescuedStorage=false;
        if(bDuringUnregister)
        {
            Bridge->Tick(0.05f);
            if(!TestTrue(TEXT("Unregister fixture first hands movement to Mass"),Bridge->IsMassOwned(Token.Id))){return false;}
        }
        NPC->OnHiddenChanged=[&](bool bNewHidden)
        {
            bCallback=true;
            TestEqual(TEXT("Actual hidden callback occurs at the selected ownership transition"),bNewHidden,!bDuringUnregister);
            TestTrue(TEXT("External callback runs inside a guarded Mass step"),Bridge->bStepping);
            World->EndPlay(EEndPlayReason::Quit);
            bWorldDestroyed=true;
            World->DestroyWorld(false); // The engine collection invokes Deinitialize exactly once.
            TestFalse(TEXT("Engine-owned world cleanup deinitialized the tickable subsystem"),Bridge->IsInitialized());
            TestTrue(TEXT("Reentrant deinitialize retains entity storage until the active step completes"),Bridge->Entities==SavedEntities);
            TestTrue(TEXT("Reentrant deinitialize retains the active processor until the step completes"),Bridge->Processor.Get()==SavedProcessor.Get());
            // Baseline-only test protection: preserve the native failure without a following null access.
            if(!Bridge->Entities || !Bridge->Processor)
            {
                bFixtureRescuedStorage=true;
                Bridge->Entities=SavedEntities;Bridge->Processor=SavedProcessor.Get();
                AddInfo(TEXT("TEST-ONLY RESCUE restored prematurely released storage; this is a contract failure reproduction, not a crash reproduction."));
            }
            if(bCollectDuringCallback)
            {
                // The fixture lends no owner/NPC root while the product callback is active.
                SavedProcessor.Reset();PinnedBridge.Reset();PinnedNPC.Reset();
                CollectGarbage(RF_NoFlags,true);
                TestTrue(TEXT("Active product step keeps the deinitialized bridge alive across callback GC"),WeakBridge.IsValid());
                TestTrue(TEXT("Product ownership callback keeps the retired NPC alive across callback GC"),WeakNPC.IsValid());
                TestTrue(TEXT("Active bridge keeps its processor alive without a fixture processor root"),WeakProcessor.IsValid());
                // Retain evidence subjects only after the collection, for assertions after return.
                SavedProcessor.Reset(WeakProcessor.Get());
                PinnedBridge.Reset(WeakBridge.Get());PinnedNPC.Reset(WeakNPC.Get());
            }
        };
        if(bDuringUnregister)
        {
            TestTrue(TEXT("In-flight unregister finishes its admitted entity retirement"),Bridge->UnregisterNPC(Bridge->GetToken(Token.Id)));
        }
        else {Bridge->Tick(0.05f);}
        TestTrue(TEXT("Native actor virtual callback actually invoked engine-owned world teardown"),bCallback);
        TestEqual(TEXT("Retirement returns the captured movement tick"),NPC->GetCharacterMovement()->IsComponentTickEnabled(),bMovementTick);
        TestEqual(TEXT("Retirement returns the captured requested pose tick"),CastChecked<UProject_JBudgetedSkeletalMeshComponent>(NPC->GetMesh())->GetRequestedTickEnabled(),bMeshTick);
        TestEqual(TEXT("Retirement returns the captured visibility"),NPC->IsHidden(),bHidden);
        TestEqual(TEXT("Retirement returns the captured collision admission"),NPC->GetActorEnableCollision(),bCollision);
        TestEqual(TEXT("Retirement returns the captured damage admission"),NPC->CanBeDamaged(),bDamage);
        TestEqual(TEXT("Completed stop releases all registrations"),Bridge->GetStats().Registered,0);
        TestFalse(TEXT("Completed stop releases Mass movement ownership"),Bridge->IsMassOwned(Token.Id));
        TestFalse(TEXT("Late registration cannot reopen retired storage"),Bridge->RegisterNPC(NPC).IsValid());
        TestFalse(TEXT("Late route publication is rejected"),Bridge->SetRoute(Token,Path));
        if(!bFixtureRescuedStorage)
        {
            TestFalse(TEXT("Completed stop releases its entity manager reference"),Bridge->Entities.IsValid());
            TestNull(TEXT("Completed stop releases its processor reference"),Bridge->Processor.Get());
        }
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSubsystemCallbackGCTest,
    "ProjectJ.Reaudit.SubsystemCallbackGC", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJSubsystemCallbackGCTest::RunTest(const FString&)
{
    for(int32 Case=0;Case<3;++Case)
    {
        auto* World=UWorld::CreateWorld(EWorldType::Game,false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        bool bDestroyed=false;
        ON_SCOPE_EXIT {if(!bDestroyed){World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);}GEngine->DestroyWorldContext(World);};
        AActor* Owner=World->SpawnActor<AActor>();
        AProject_JNPCCharacter* Pawn=nullptr;
        if(Case==2)
        {
            Pawn=World->SpawnActor<AProject_JNPCCharacter>();
            World->SpawnActor<AAIController>()->Possess(Pawn);Owner=Pawn;
        }
        World->InitializeActorsForPlay(FURL());World->GetWorldSettings()->NotifyBeginPlay();World->GetWorldSettings()->NotifyMatchStarted();
        UObject* Service=Case==0 ? static_cast<UObject*>(World->GetSubsystem<UProject_JTargetScoringSubsystem>())
            : Case==1 ? static_cast<UObject*>(World->GetSubsystem<UProject_JVisualAssetSubsystem>())
            : static_cast<UObject*>(World->GetSubsystem<UProject_JNPCPathSubsystem>());
        if(!TestNotNull(TEXT("Real world supplies the callback delivery subsystem"),Service)){return false;}
        TStrongObjectPtr<UObject> PinnedService(Service),PinnedOwner(Owner);
        const TWeakObjectPtr<UObject> WeakService(Service),WeakOwner(Owner);
        int32 Delivered=0,Late=0;
        const auto TeardownAndCollect=[&]()
        {
            ++Delivered;
            World->EndPlay(EEndPlayReason::Quit);bDestroyed=true;World->DestroyWorld(false);
            PinnedService.Reset();PinnedOwner.Reset();
            CollectGarbage(RF_NoFlags,true);
            TestTrue(TEXT("Active GT delivery keeps its deinitialized subsystem alive across GC"),WeakService.IsValid());
            TestTrue(TEXT("External completion keeps its owner alive across GC"),WeakOwner.IsValid());
            PinnedService.Reset(WeakService.Get());PinnedOwner.Reset(WeakOwner.Get());
        };
        if(Case==0)
        {
            auto* Scoring=CastChecked<UProject_JTargetScoringSubsystem>(Service);
            ProjectJ::TargetScoring::FSnapshot Snapshot;Snapshot.Candidates.Add({1,FVector(10,0,0)});
            TestTrue(TEXT("Serial query is admitted"),Scoring->Submit(Owner,Snapshot,EProject_JTargetScoringExecution::Serial,1,
                [&](const auto&){TeardownAndCollect();}).IsValid());
            TestTrue(TEXT("Second query is pending before retirement"),Scoring->Submit(Owner,Snapshot,EProject_JTargetScoringExecution::Serial,2,
                [&](const auto&){++Late;}).IsValid());
            Scoring->Tick(0);
            TestEqual(TEXT("Teardown drains pending scoring jobs"),Scoring->GetPendingCount(),0);
        }
        else if(Case==1)
        {
            auto* Visual=CastChecked<UProject_JVisualAssetSubsystem>(Service);
            const FSoftObjectPath Path(TEXT("/Engine/BasicShapes/Cube.Cube"));
            const auto Token=Visual->Request(Owner,Path,[&](UObject*){TeardownAndCollect();});
            TestTrue(TEXT("Visual lease is admitted"),Token!=0);
            TestTrue(TEXT("Another visual lease is pending"),Visual->Request(Owner,Path,[&](UObject*){++Late;})!=0);
            Visual->ExpireForTest(Token); // Existing expiry seam; no synthetic worker or load completion.
            Visual->Tick(0);
            TestEqual(TEXT("Teardown drains visual leases"),Visual->GetLeaseCount(),0);
        }
        else
        {
            auto* Paths=CastChecked<UProject_JNPCPathSubsystem>(Service);
            TestTrue(TEXT("Native pawn path is admitted"),Paths->Submit(Owner,Pawn,FVector(1000,0,0),1,
                [&](const auto&){TeardownAndCollect();})!=0);
            TestTrue(TEXT("Another owner path is pending"),Paths->Submit(World->SpawnActor<AActor>(),Pawn,FVector(2000,0,0),1,
                [&](const auto&){++Late;})!=0);
            Paths->Tick(0);Paths->Tick(0); // This real world has no nav data: normal failed-path delivery.
            TestEqual(TEXT("Teardown drains path requests"),Paths->GetRequestCount(),0);
        }
        TestEqual(TEXT("First completion invoked actual world cleanup and GC"),Delivered,1);
        TestEqual(TEXT("Retirement suppresses queued later completions"),Late,0);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCPlayReuseTest,
    "ProjectJ.Reaudit.NPCPlayReuse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCPlayReuseTest::RunTest(const FString&)
{
    for(int32 Case=0;Case<3;++Case)
    {
        auto* World=UWorld::CreateWorld(EWorldType::Game,false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        ON_SCOPE_EXIT {World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
        auto* NPC=World->SpawnActor<AProject_JNPCCharacter>();
        auto* AI=World->SpawnActor<AAIController>();AI->Possess(NPC);
        auto MakeScoring=[NPC](){auto* C=NewObject<UProject_JTargetScoringComponent>(NPC);NPC->AddInstanceComponent(C);C->RegisterComponent();return C;};
        auto MakeActions=[NPC](){auto* C=NewObject<UProject_JNPCActionComponent>(NPC);NPC->AddInstanceComponent(C);C->RegisterComponent();return C;};
        auto* Scoring=MakeScoring();auto* Actions=MakeActions();
        auto* Activation=NewObject<UProject_JNPCActivationComponent>(NPC);NPC->AddInstanceComponent(Activation);Activation->RegisterComponent();
        auto* Definition=NewObject<UProject_JNPCActivationDefinition>(Activation); // Explicit movement-only capability.
        World->InitializeActorsForPlay(FURL());World->GetWorldSettings()->NotifyBeginPlay();World->GetWorldSettings()->NotifyMatchStarted();
        NPC->GetAttributeSet()->InitMaxHealth(100);NPC->GetAttributeSet()->InitHealth(100);
        NPC->GetAbilitySystemComponent()->InitAbilityActorInfo(NPC,NPC);
        if(!TestTrue(TEXT("First play opts into real authority scoring/actions"),Activation->ActivateNPC(Definition,Scoring,Actions,1))){return false;}
        NPC->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
        TestFalse(TEXT("EndPlay retires the old activation"),Activation->HasOwnedActivation());
        TestFalse(TEXT("EndPlay retires the old scoring registration"),Scoring->IsBatchedNPCDecisionRegistered());
        TestEqual(TEXT("EndPlay stops the old action consumer"),Actions->GetActionState(),EProjectJNPCActionState::Disabled);
        // Keep the selected component, replace collaborators to isolate each public admission boundary.
        if(Case>=1){Scoring->DestroyComponent();Scoring=MakeScoring();}
        if(Case==2){Actions->DestroyComponent();Actions=MakeActions();}
        NPC->DispatchBeginPlay(true);
        AI=World->SpawnActor<AAIController>();AI->Possess(NPC);
        if(!TestTrue(TEXT("Engine-ended AI controller is replaced by a valid new controller"),IsValid(AI)&&!AI->IsActorBeingDestroyed())){return false;}
        NPC->GetAbilitySystemComponent()->InitAbilityActorInfo(NPC,NPC);
        if(Case==0)
        {
            TestTrue(TEXT("A reused scoring component accepts a new explicit registration"),Scoring->StartBatchedNPCDecisions(1));
        }
        else if(Case==1)
        {
            if(!TestTrue(TEXT("Fresh scoring independently authorizes the action reuse fixture"),Scoring->StartBatchedNPCDecisions(1))){return false;}
            TestTrue(TEXT("A reused action component accepts a new ownership lease"),Actions->StartActions(Scoring,{}));
        }
        else {TestTrue(TEXT("A reused activation accepts fresh valid collaborators"),Activation->ActivateNPC(Definition,Scoring,Actions,1));}
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerInputPlayReuseTest,
    "ProjectJ.Reaudit.PlayerInputPlayReuse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPlayerInputPlayReuseTest::RunTest(const FString&)
{
    AddExpectedError(TEXT("has no CharacterAnimProfile, LocomotionProfile, or MotionMatchingAssetSet assigned."),EAutomationExpectedErrorFlags::Contains,2);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT {World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    auto* Player=World->SpawnActor<AProject_JGreatswordCharacter>();
    World->InitializeActorsForPlay(FURL());World->GetWorldSettings()->NotifyBeginPlay();World->GetWorldSettings()->NotifyMatchStarted();
    auto* ASC=NewObject<UProject_JAbilitySystemComponent>(Player);ASC->RegisterComponent();
    auto* ASCProperty=FindFProperty<FObjectPropertyBase>(AProject_JBaseCharacter::StaticClass(),TEXT("AbilitySystemComponent"));
    ASCProperty->SetObjectPropertyValue_InContainer(Player,ASC);ASC->InitAbilityActorInfo(Player,Player);
    const auto InputTag=FProject_JGameplayTags::Get().InputTag_Weapon_RMB;
    FGameplayAbilitySpec Spec(UProject_JGameplayAbility_Melee::StaticClass(),1);Spec.GetDynamicSpecSourceTags().AddTag(InputTag);
    const auto Handle=ASC->GiveAbility(Spec);
    auto* Binding=Player->FindComponentByClass<UProject_JPlayerInputBindingComponent>();
    auto* Executor=Player->FindComponentByClass<UProject_JSkillInputExecutionComponent>();
    auto* Router=Player->FindComponentByClass<UProject_JSkillInputRouterComponent>();
    auto* Device=NewObject<UEnhancedInputComponent>(Player);Device->RegisterComponent();
    FProject_JPlayerInputActionSet ActionSet;ActionSet.AttackAction=NewObject<UInputAction>();ActionSet.HeavyAttackAction=NewObject<UInputAction>();
    if(!TestTrue(TEXT("First play acquires real EnhancedInput bindings"),Binding->BindInput(Device,Player,ActionSet))){return false;}
    const int32 FirstBindingCount=Device->GetActionEventBindings().Num();
    Executor->Initialize(Player);Executor->HandleInputTagPressed(InputTag);
    if(!TestTrue(TEXT("First play actually publishes held GAS input"),ASC->FindAbilitySpecFromHandle(Handle)->InputPressed)){return false;}
    Router->Initialize(Player);Router->HandleModifierPressed(ReauditModifierTag);
    FGameplayTagContainer Required;Required.AddTag(ReauditModifierTag);
    if(!TestTrue(TEXT("First play admits the public modifier input"),Router->AreModifierTagsMatched(Required,{}))){return false;}
    Player->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
    TestEqual(TEXT("Old play removes its real device bindings"),Device->GetActionEventBindings().Num(),0);
    TestFalse(TEXT("Old play releases its held GAS input"),ASC->FindAbilitySpecFromHandle(Handle)->InputPressed);
    TestFalse(TEXT("Old play releases its held modifier"),Router->AreModifierTagsMatched(Required,{}));
    Player->DispatchBeginPlay(true);ASCProperty->SetObjectPropertyValue_InContainer(Player,ASC);ASC->InitAbilityActorInfo(Player,Player);
    TestTrue(TEXT("New play reacquires its EnhancedInput bindings"),Binding->BindInput(Device,Player,ActionSet));
    TestEqual(TEXT("New play acquires one set of device bindings"),Device->GetActionEventBindings().Num(),FirstBindingCount);
    Executor->Initialize(Player);Executor->HandleInputTagPressed(InputTag);
    TestTrue(TEXT("New play publishes held GAS input again"),ASC->FindAbilitySpecFromHandle(Handle)->InputPressed);
    Router->Initialize(Player);Router->HandleModifierPressed(ReauditModifierTag);
    TestTrue(TEXT("New play admits modifier input again"),Router->AreModifierTagsMatched(Required,{}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCombatIntroPlayReuseTest,
    "ProjectJ.Reaudit.CombatIntroPlayReuse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCombatIntroPlayReuseTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT {World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    auto* Owner=World->SpawnActor<ACharacter>();
    auto* Intro=NewObject<UProject_JCombatIntroComponent>(Owner);Owner->AddInstanceComponent(Intro);Intro->RegisterComponent();
    auto* Mesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
    auto* Montage=LoadObject<UAnimMontage>(nullptr,TEXT("/Game/Anim_Assets/Great_Sword/Animations/Sword/Montage/AM_Greatsword_LMB1.AM_Greatsword_LMB1"));
    if(!TestNotNull(TEXT("Existing mesh loads"),Mesh)||!TestNotNull(TEXT("Existing montage loads"),Montage)){return false;}
    Owner->GetMesh()->SetSkeletalMesh(Mesh);Owner->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
    World->InitializeActorsForPlay(FURL());World->GetWorldSettings()->NotifyBeginPlay();World->GetWorldSettings()->NotifyMatchStarted();
    if(!TestTrue(TEXT("First play starts actual draw montage"),Intro->PlayIntro(*Owner,Montage,1))){return false;}
    Owner->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
    TestFalse(TEXT("EndPlay retires old draw state"),Intro->IsPlayingIntro()||Intro->IsPendingCombatMode());
    Owner->DispatchBeginPlay(true);
    TestTrue(TEXT("New play starts actual draw montage again"),Intro->PlayIntro(*Owner,Montage,1));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMountActorPlayReuseTest,
    "ProjectJ.Reaudit.MountActorPlayReuse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMountActorPlayReuseTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT {World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    World->InitializeActorsForPlay(FURL());World->GetWorldSettings()->NotifyBeginPlay();World->GetWorldSettings()->NotifyMatchStarted();
    auto* Rider=World->SpawnActor<AProject_JPlayerMaturityASCFixture>();
    auto* Controller=World->SpawnActor<APlayerController>();
    auto* ControllerState=World->SpawnActor<APlayerState>();
    Controller->SetPlayerState(ControllerState);ControllerState->SetOwner(Controller);Controller->Possess(Rider);
    auto* ASC=NewObject<UProject_JAbilitySystemComponent>(Rider);ASC->RegisterComponent();ASC->InitAbilityActorInfo(Rider,Rider);Rider->CurrentASC=ASC;
    auto* Mounting=NewObject<UProject_JMountComponent>(Rider);Rider->AddInstanceComponent(Mounting);Mounting->RegisterComponent();
    auto* Mount=World->SpawnActor<AProject_JMountLifecycleFixture>();
    if(!TestTrue(TEXT("Initial mount actor accepts an actual rider"),Mounting->RequestMount(Mount))){return false;}
    Mount->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
    TestFalse(TEXT("Mount removal retires the rider relationship"),Mounting->IsMounted());
    TestTrue(TEXT("A player controller with persistent state survives stream removal"),IsValid(Controller));
    Mount->DispatchBeginPlay(true);Controller->Possess(Rider);ASC->InitAbilityActorInfo(Rider,Rider);
    TestTrue(TEXT("The same returned mount actor accepts a new rider lease"),Mounting->RequestMount(Mount));
    return true;
}
#endif
