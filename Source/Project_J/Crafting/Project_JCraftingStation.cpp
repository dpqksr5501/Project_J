#include "Crafting/Project_JCraftingStation.h"
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/Project_JCraftingJobsComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Crafting/Project_JCraftingRecipeDefinition.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/Project_JPlayerState.h"
#include "GameFramework/Pawn.h"
#include "Interaction/Project_JInteractionTargetComponent.h"
#include "Net/UnrealNetwork.h"

AProject_JCraftingStation::AProject_JCraftingStation()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetNetUpdateFrequency(2.f);
	SetMinNetUpdateFrequency(1.f);
	InteractionBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBounds"));
	SetRootComponent(InteractionBounds);
	InteractionBounds->InitBoxExtent(FVector(80.f, 45.f, 45.f));
	InteractionBounds->SetCollisionObjectType(ECC_WorldDynamic);
	InteractionBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBounds->SetCollisionResponseToAllChannels(ECR_Overlap);
	InteractionBounds->SetGenerateOverlapEvents(false);
	BenchVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BenchVisual"));
	BenchVisual->SetupAttachment(InteractionBounds);
	BenchVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InteractionTarget = CreateDefaultSubobject<UProject_JInteractionTargetComponent>(TEXT("InteractionTarget"));
	InteractionTarget->PromptText = NSLOCTEXT("Crafting", "BenchPrompt", "Craft");
	InteractionTarget->InteractionRange = 250.f;
}

void AProject_JCraftingStation::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AProject_JCraftingStation, RecipeId);
	DOREPLIFETIME(AProject_JCraftingStation, RecipeVersion);
	DOREPLIFETIME(AProject_JCraftingStation, Batches);
}

bool AProject_JCraftingStation::IsSpatiallyAvailable(const APawn* Interactor) const
{
	if (!IsValid(Interactor) || Interactor->IsActorBeingDestroyed() || IsActorBeingDestroyed() ||
		!GetWorld() || Interactor->GetWorld() != GetWorld() || !InteractionTarget ||
		!InteractionTarget->IsAvailable() || RecipeId.IsNone() || RecipeVersion < 1 ||
		Batches < 1 || Batches > 100) return false;
	const float Range = InteractionTarget->InteractionRange;
	const auto Origin = Interactor->GetActorLocation(), Target = GetActorLocation();
	// The normal character interaction searches300cm. The bench enforces its own
	// authored, finite range again for direct authoritative Interact callers.
	if (!FMath::IsFinite(Range) || Range <= 0.f || Range > 300.f || Origin.ContainsNaN() || Target.ContainsNaN() ||
		FVector::DistSquared(Origin, Target) > FMath::Square(Range)) return false;
	if (InteractionTarget->bRequireLineOfSight)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ProjectJCraftingVisibility), false, Interactor);
		Params.AddIgnoredActor(this);
		if (GetWorld()->LineTraceTestByChannel(Origin, Target, ECC_Visibility, Params)) return false;
	}
	return true;
}

bool AProject_JCraftingStation::CanInteract_Implementation(APawn* Interactor) const
{
	// A client prompt is advisory. The local profile provider is server-only;
	// consulting its readiness here would hide a valid interaction on clients.
	return IsSpatiallyAvailable(Interactor);
}

void AProject_JCraftingStation::Interact_Implementation(APawn* Interactor)
{
	TryCraft(Interactor);
}

bool AProject_JCraftingStation::TryCraft(APawn* Interactor)
{
	check(IsInGameThread());
	if (GetNetMode() == NM_Client || !HasAuthority() || !Interactor || !Interactor->HasAuthority() || !IsSpatiallyAvailable(Interactor)) return false;
	auto* Player = Interactor->GetPlayerState<AProject_JPlayerState>();
	auto* Instance = GetGameInstance();
	if (!IsValid(Player) || Player->IsActorBeingDestroyed() || Player->GetPawn() != Interactor ||
		Player->GetGameInstance() != Instance || !Instance) return false;
	auto* Profiles = Instance->GetSubsystem<UProject_JLocalProfileSubsystem>();
	auto* Data = Instance->GetSubsystem<UProject_JCharacterDataSubsystem>();
	if (!Profiles || !Profiles->IsReady(Player) || Profiles->IsCheckpointPending(Player) || !Data) return false;
	// A saved job owns its frozen terms even if its recipe/station content changed.
	// Completion and refund are character-owned; starting still requires this bench.
	auto* Jobs = Player->GetCraftingJobsComponent();
	if (!Jobs) return false;
	const FGuid ExistingJob = Jobs->FindRecipeJob(RecipeId, RecipeVersion);
	if (ExistingJob.IsValid()) return Jobs->IsJobDue(ExistingJob) && Jobs->TryCollectJob(ExistingJob);
	auto* Recipe = Data->GetCraftingRecipe(RecipeId, RecipeVersion);
	ProjectJ::Crafting::FRecipeValues Values; FString Rules;
	if (!Recipe || !Recipe->Capture(*Data, Values, Rules) || Batches > Values.MaxBatches) return false;
	FProject_JPersistentEdit Edit;
	Edit.Kind = Values.DurationSecondsPerBatch > 0 ? EProject_JPersistentEdit::StartCraftJob : EProject_JPersistentEdit::Craft;
	Edit.CraftingRecipe = RecipeId; Edit.CraftingRecipeVersion = RecipeVersion;
	Edit.CraftingBatches = Batches; Edit.CraftingRulesDigest = MoveTemp(Rules);
	Edit.Request = Profiles->NewRequestId(Player);
	return Edit.Request.IsValid() && Profiles->SubmitEdit(Player, Edit);
}
