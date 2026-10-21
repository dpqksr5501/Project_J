#pragma once

#include "Movement/Project_JMoverPawn.h"
#include "Movement/Project_JMovementQueries.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "DefaultMovementSet/CharacterMoverSimulationTypes.h"
#include "Backends/MoverNetworkPredictionLiaison.h"
#endif

namespace ProjectJ::Tests
{
/** Seeds an authority observer/ability fixture through the actual Mover publication boundary.
 * This does not simulate a floor, jump, collision or montage and is not physics proof.
 */
inline bool PublishMoverFrame(AProject_JMoverPawn* Pawn, EMovementMode Mode, FVector Velocity=FVector::ZeroVector)
{
#if WITH_DEV_AUTOMATION_TESTS
 if (!Pawn || !Pawn->HasAuthority() || !Pawn->GetMover()) return false;
 if(Mode!=MOVE_Walking && Mode!=MOVE_Flying && Mode!=MOVE_Falling && Mode!=MOVE_None) return false;
 auto* Liaison=Pawn->FindComponentByClass<UMoverNetworkPredictionLiaisonComponent>();
 if (!Liaison) return false;
 // Assignment deep-copies Mover data blocks; copy construction would mutate
 // the currently published frame before the pending/finalize boundary.
 FMoverSyncState State; State=Pawn->GetMover()->GetSyncState();
 State.MovementMode=Mode==MOVE_Walking?DefaultModeNames::Walking:Mode==MOVE_Flying?DefaultModeNames::Flying:Mode==MOVE_Falling?DefaultModeNames::Falling:FName(TEXT("Attached"));
 auto& Physical=State.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
 Physical.SetTransforms_WorldSpace(Pawn->GetActorLocation(),Pawn->GetActorRotation(),Velocity,FVector::ZeroVector);
 if (!Liaison->WritePendingSyncState(State)) return false;
 FMoverAuxStateContext Aux;
 // The liaison's interpolation wrapper requires NP's tick-scoped ActiveInstance.
 // Authority fixtures can publish directly through Mover after writing pending state.
 Pawn->GetMover()->FinalizeFrame(&State,&Aux);
 return true;
#else
 return false; // Synthetic state injection is unavailable in Shipping.
#endif
}
}
