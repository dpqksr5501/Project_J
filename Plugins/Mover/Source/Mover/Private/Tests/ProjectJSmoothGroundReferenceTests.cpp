// Project-local Editor reference: captured original mode vs live mode, direct math and one owned ThreadPool batch.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Containers/StringConv.h"
#include "Async/Async.h"
#include "MoveLibrary/GroundMovementUtils.h"
#include "DefaultMovementSet/Modes/SmoothWalkingMode.h"
#include "DefaultMovementSet/Modes/SmoothWalkingState.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "MoverComponent.h"
#include "MoverDataModelTypes.h"
#include "MoverSimulationTypes.h"
#include "MoveLibrary/RollbackBlackboard.h"
#include "MoveLibrary/RollbackBlackboardWrappers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#include "Data/ProjectJSmoothGroundReference.inl"

namespace ProjectJSmoothGround119
{
constexpr uint32 Magic = 0x4A534731, Schema = 1;
constexpr int32 CaseCount = 12, MaxPayloadBytes = 65536;
// Declared before baseline capture. NetSerialize bytes additionally require exact equality.
constexpr double VectorTolerance = 1.e-8, QuaternionTolerance = 1.e-6;
constexpr const TCHAR* OriginalModeSHA256 = TEXT("00cf3fcf6ac9a5a1c032aa8905bed867fe153d6378370f310eaa28c443187d1a");
const FName DidGenerateMove(TEXT("DidGenerateMove"));
const TCHAR* const SettingNames[] = {
	TEXT("Acceleration"), TEXT("Deceleration"), TEXT("DirectionalAccelerationFactor"), TEXT("TurningStrength"),
	TEXT("AccelerationSmoothingTime"), TEXT("DecelerationSmoothingTime"), TEXT("AccelerationSmoothingCompensation"),
	TEXT("DecelerationSmoothingCompensation"), TEXT("VelocityDeadzoneThreshold"), TEXT("AccelerationDeadzoneThreshold"),
	TEXT("OutsideInfluenceSmoothingTime"), TEXT("FacingSmoothingTime"), TEXT("FacingDeadzoneThreshold"), TEXT("AngularVelocityDeadzoneThreshold")
};
enum class EHistory : uint8 { None, Retained, Expired };
struct FCase
{
	FString Id;
	float Settings[14] = {1500.f,1500.f,1.f,10.f,.1f,.1f,0.f,0.f,.01f,.001f,.05f,.25f,.1f,.01f};
	bool bDouble = true, bSeed = false;
	EHistory History = EHistory::None;
	float Delta = 1.f / 60.f;
	FVector Desired = FVector(600,150,0), Velocity = FVector(80,-25,0), Angular = FVector(0,0,10);
	FQuat DesiredFacing = FRotator(0,95,0).Quaternion(), CurrentFacing = FRotator(0,10,0).Quaternion();
	FSmoothWalkingState Seed;
	FCase()
	{
		Seed.SpringVelocity = FVector(-200,60,0); Seed.SpringAcceleration = FVector(30,-15,4);
		Seed.IntermediateVelocity = FVector(160,50,-10); Seed.IntermediateFacing = FRotator(5,-40,2).Quaternion();
		Seed.IntermediateAngularVelocity = FVector(.1,-.2,.3);
	}
};
struct FResult
{
	FString Id;
	TArray<uint8> InputBytes;
	FVector Velocity = FVector::ZeroVector, Angular = FVector::ZeroVector;
	bool bHasSpring = false;
	FSmoothWalkingState Spring;
	TArray<uint8> SpringBytes;
};

void SerializeSpringFields(FArchive& Ar, FSmoothWalkingState& Value)
{
	Ar << Value.SpringVelocity << Value.SpringAcceleration << Value.IntermediateVelocity;
	Ar << Value.IntermediateFacing << Value.IntermediateAngularVelocity;
}
void SerializeBoundedBytes(FArchive& Ar, TArray<uint8>& Value, int32 Maximum)
{
	int32 Count = Value.Num(); Ar << Count;
	if (Count < 0 || Count > Maximum) { Ar.SetError(); return; }
	if (Ar.IsLoading()) Value.SetNumUninitialized(Count);
	if (Count) Ar.Serialize(Value.GetData(), Count);
}
void SerializeText(FArchive& Ar, FString& Value, int32 Maximum)
{
	TArray<uint8> Bytes;
	if (Ar.IsSaving())
	{
		const FTCHARToUTF8 Utf8(*Value); Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}
	SerializeBoundedBytes(Ar, Bytes, Maximum);
	if (Ar.IsLoading() && !Ar.IsError())
	{
		if (Bytes.IsEmpty()) Value.Empty();
		else { const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num()); Value = FString(Text.Length(), Text.Get()); }
	}
}
TArray<uint8> InputBytes(FCase Value)
{
	TArray<uint8> Bytes; FMemoryWriter Ar(Bytes, true);
	SerializeText(Ar, Value.Id, 64);
	for (float& Setting : Value.Settings) Ar << Setting;
	Ar << Value.bDouble << Value.bSeed;
	uint8 History = uint8(Value.History); Ar << History << Value.Delta;
	Ar << Value.Desired << Value.DesiredFacing << Value.CurrentFacing << Value.Velocity << Value.Angular;
	SerializeSpringFields(Ar, Value.Seed);
	return Bytes;
}
TArray<FCase> MakeCases()
{
	TArray<FCase> Cases; Cases.Reserve(CaseCount);
	const auto Add = [&](const TCHAR* Id, bool bDouble, bool bSeed, EHistory History, float Delta) -> FCase&
	{
		auto& C = Cases.AddDefaulted_GetRef(); C.Id = Id; C.bDouble = bDouble; C.bSeed = bSeed; C.History = History; C.Delta = Delta; return C;
	};
	Add(TEXT("double_new_accelerating"), true, false, EHistory::None, 1.f/60.f);
	Add(TEXT("single_new_accelerating"), false, false, EHistory::None, 1.f/30.f);
	Add(TEXT("double_retained_turning"), true, true, EHistory::Retained, .02f).Desired = FVector(-300,450,0);
	Add(TEXT("single_retained_turning"), false, true, EHistory::Retained, .02f).Desired = FVector(-300,450,0);
	Add(TEXT("double_expired_state"), true, true, EHistory::Expired, 1.f/60.f);
	Add(TEXT("single_expired_state"), false, true, EHistory::Expired, 1.f/60.f);
	Add(TEXT("zero_delta_new"), true, false, EHistory::None, 0.f);
	Add(TEXT("zero_delta_retained"), false, true, EHistory::Retained, 0.f);
	Add(TEXT("tiny_positive_new"), true, false, EHistory::None, .001f);
	auto& Decel = Add(TEXT("single_retained_decelerating"), false, true, EHistory::Retained, .04f);
	Decel.Desired = FVector::ZeroVector; Decel.Velocity = FVector(500,50,0);
	Decel.Settings[1] = 2300.f; Decel.Settings[5] = .18f; Decel.Settings[7] = .65f;
	auto& Outside = Add(TEXT("double_outside_influence"), true, true, EHistory::Retained, .025f);
	Outside.Desired = FVector(70,100,0); Outside.Velocity = FVector(120,-30,0);
	Outside.Settings[0] = 1800.f; Outside.Settings[2] = .3f; Outside.Settings[3] = 35.f;
	Outside.Settings[4] = .17f; Outside.Settings[6] = .7f; Outside.Settings[10] = .001f; Outside.Settings[11] = .12f;
	auto& Snap = Add(TEXT("double_deadzone_snap"), true, true, EHistory::Retained, 1.f/60.f);
	Snap.Desired = Snap.Velocity = FVector(100,0,0); Snap.DesiredFacing = Snap.CurrentFacing = FQuat::Identity;
	Snap.Seed.SpringVelocity = Snap.Seed.IntermediateVelocity = Snap.Desired;
	Snap.Seed.SpringAcceleration = FVector(.00001,0,0); Snap.Seed.IntermediateFacing = FQuat::Identity;
	Snap.Seed.IntermediateAngularVelocity = FVector(.00001,0,0); Snap.Angular = FVector(.00001,0,0);
	Snap.Settings[8] = .5f; Snap.Settings[9] = .25f; Snap.Settings[12] = .5f; Snap.Settings[13] = .25f;
	return Cases;
}

bool RunRealMode(FAutomationTestBase& Test, AActor* Owner, const FCase& C, FResult& Out)
{
	const TStrongObjectPtr<UMoverComponent> Mover(NewObject<UMoverComponent>(Owner)); Owner->AddInstanceComponent(Mover.Get());
	const TStrongObjectPtr<USmoothWalkingMode> Mode(NewObject<USmoothWalkingMode>(Mover.Get()));
	const TStrongObjectPtr<URollbackBlackboard> Blackboard(NewObject<URollbackBlackboard>(Mover.Get()));
	if (!Test.TestTrue(TEXT("Native mode is added to its owned Mover"), Mover->AddMovementModeFromObject(TEXT("SmoothGroundReference119"), Mode.Get()))) return false;
	auto* ModesField = FindFProperty<FMapProperty>(UMoverComponent::StaticClass(), TEXT("MovementModes"));
	if (!Test.TestNotNull(TEXT("Native editable movement-mode property"), ModesField)) return false;
	FPropertyChangedEvent ModesChanged(ModesField);
	// The normal Editor property-change entry builds this transient mode's shared settings.
	static_cast<UObject*>(Mover.Get())->PostEditChangeProperty(ModesChanged);
	if (!Test.TestNotNull(TEXT("Native authoring path created required common settings"), Mover->FindSharedSettings<UCommonLegacyMovementSettings>())) return false;
	if (!Test.TestFalse(TEXT("Reference setup does not start a simulation backend"), Mover->IsRegistered())) return false;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(SettingNames); ++Index)
	{
		auto* Field = FindFProperty<FFloatProperty>(Mode->GetClass(), SettingNames[Index]);
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s reflected setting %s"), *C.Id, SettingNames[Index]), Field)) return false;
		Field->SetPropertyValue_InContainer(Mode.Get(), C.Settings[Index]);
	}
	auto* DoubleSpring = FindFProperty<FBoolProperty>(Mode->GetClass(), TEXT("bSmoothFacingWithDoubleSpring"));
	if (!Test.TestNotNull(TEXT("Native double-spring setting"), DoubleSpring)) return false;
	DoubleSpring->SetPropertyValue_InContainer(Mode.Get(), C.bDouble);
	FMoverSimContext Context; Context.SimulationOwner = Mover.Get(); Context.Blackboard = FRollbackBlackboardSimWrapper(*Blackboard);
	Mode->OnRegistered(TEXT("SmoothGroundReference119"), Context);
	if (!Test.TestTrue(TEXT("Mode owns its real Mover outer"), Mode->GetMoverComponent() == Mover.Get()) ||
		!Test.TestTrue(TEXT("Native OnRegistered created the movement receipt entry"), Context.Blackboard.HasEntry(DidGenerateMove))) return false;
	const auto BeginFrame = [&](int32 Frame)
	{
		FMoverTimeStep Step; Step.ServerFrame = Frame; Step.BaseSimTimeMs = Frame * 16.0; Step.StepMs = 16.f;
		Blackboard->BeginSimulationFrame(Step);
	};
	int32 Frame = 0;
	if (C.History != EHistory::None)
	{
		BeginFrame(Frame++);
		const bool Written = Context.Blackboard.TrySet(DidGenerateMove, true);
		Blackboard->EndSimulationFrame();
		if (!Test.TestTrue(TEXT("Previous simulation frame recorded the normal move receipt"), Written)) return false;
		if (C.History == EHistory::Expired) { BeginFrame(Frame++); Blackboard->EndSimulationFrame(); }
	}
	BeginFrame(Frame);
	bool bDidGenerate = false; Context.Blackboard.TryGet(DidGenerateMove, bDidGenerate);
	const bool bHistoryValid = Test.TestEqual(*FString::Printf(TEXT("%s native receipt lifetime"), *C.Id), bDidGenerate, C.History == EHistory::Retained);
	FMoverTickStartData Start; Start.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
	if (C.bSeed) Start.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FSmoothWalkingState>() = C.Seed;
	Out.Id = C.Id; Out.InputBytes = InputBytes(C); Out.Velocity = C.Velocity; Out.Angular = C.Angular;
	// Exercise the real mode/context independently of the direct helper's owned packets.
	Mode->GenerateWalkMove_Implementation(Start, C.Delta, Context, C.Desired, C.DesiredFacing, C.CurrentFacing, Out.Angular, Out.Velocity);
	const auto* State = Start.SyncState.SyncStateCollection.FindDataByType<FSmoothWalkingState>();
	Out.bHasSpring = State != nullptr;
	bool bSerialized = true;
	if (State)
	{
		Out.Spring = *State;
		FMemoryWriter Ar(Out.SpringBytes, true); bool bNetSuccess = false;
		bSerialized = Out.Spring.NetSerialize(Ar, nullptr, bNetSuccess) && bNetSuccess && !Ar.IsError();
	}
	Blackboard->EndSimulationFrame(); static_cast<UBaseMovementMode*>(Mode.Get())->OnUnregistered(Context);
	const bool bFinite = !Out.Velocity.ContainsNaN() && !Out.Angular.ContainsNaN() && (!State ||
		(!Out.Spring.SpringVelocity.ContainsNaN() && !Out.Spring.SpringAcceleration.ContainsNaN() &&
		 !Out.Spring.IntermediateVelocity.ContainsNaN() && !Out.Spring.IntermediateFacing.ContainsNaN() && !Out.Spring.IntermediateAngularVelocity.ContainsNaN()));
	return bHistoryValid && Test.TestTrue(TEXT("Real private spring state serializes successfully"), bSerialized) &&
		Test.TestTrue(TEXT("Baseline outputs and private state are finite"), bFinite);
}

// Plain owning values only. No UObject, state collection, FString or borrowed reference crosses the worker boundary.
struct FOwnedSpring
{
	FVector SpringVelocity = FVector::ZeroVector, SpringAcceleration = FVector::ZeroVector, IntermediateVelocity = FVector::ZeroVector;
	FQuat IntermediateFacing = FQuat::Identity;
	FVector IntermediateAngularVelocity = FVector::ZeroVector;
};
struct FOwnedStep
{
	FSmoothWalkingMoveParams Params;
	FOwnedSpring Spring;
	FVector Velocity = FVector::ZeroVector, Angular = FVector::ZeroVector;
	bool bHasSpring = false;
};
struct FWorkerBatch
{
	TArray<FOwnedStep> Steps;
	bool bRanOnGameThread = true;
};
FOwnedStep CaptureOwnedStep(const FCase& C)
{
	check(IsInGameThread());
	FOwnedStep Step;
	auto& P = Step.Params;
	P.DeltaSeconds = C.Delta; P.DesiredVelocity = C.Desired; P.DesiredFacing = C.DesiredFacing; P.CurrentFacing = C.CurrentFacing;
	P.Acceleration = C.Settings[0]; P.Deceleration = C.Settings[1]; P.DirectionalAccelerationFactor = C.Settings[2]; P.TurningStrength = C.Settings[3];
	P.AccelerationSmoothingTime = C.Settings[4]; P.DecelerationSmoothingTime = C.Settings[5];
	P.AccelerationSmoothingCompensation = C.Settings[6]; P.DecelerationSmoothingCompensation = C.Settings[7];
	P.VelocityDeadzoneThreshold = C.Settings[8]; P.AccelerationDeadzoneThreshold = C.Settings[9];
	P.OutsideInfluenceSmoothingTime = C.Settings[10]; P.FacingSmoothingTime = C.Settings[11];
	P.FacingDeadzoneThreshold = C.Settings[12]; P.AngularVelocityDeadzoneThreshold = C.Settings[13];
	P.bSmoothFacingWithDoubleSpring = C.bDouble;
	// The real mode fixture independently verifies its retained/expired native blackboard history on a non-SP owner.
	P.bInitializeSpring = !C.bSeed || C.History != EHistory::Retained;
	Step.bHasSpring = C.bSeed || C.Delta > FLT_EPSILON;
	Step.Velocity = C.Velocity; Step.Angular = C.Angular;
	if (C.bSeed)
	{
		Step.Spring.SpringVelocity = C.Seed.SpringVelocity; Step.Spring.SpringAcceleration = C.Seed.SpringAcceleration;
		Step.Spring.IntermediateVelocity = C.Seed.IntermediateVelocity; Step.Spring.IntermediateFacing = C.Seed.IntermediateFacing;
		Step.Spring.IntermediateAngularVelocity = C.Seed.IntermediateAngularVelocity;
	}
	return Step;
}
bool MaterializeResultGT(FAutomationTestBase& Test, const FCase& C, const FOwnedStep& Step, FResult& Out)
{
	check(IsInGameThread());
	Out.Id = C.Id; Out.InputBytes = InputBytes(C); Out.Velocity = Step.Velocity; Out.Angular = Step.Angular; Out.bHasSpring = Step.bHasSpring;
	if (Step.bHasSpring)
	{
		Out.Spring.SpringVelocity = Step.Spring.SpringVelocity; Out.Spring.SpringAcceleration = Step.Spring.SpringAcceleration;
		Out.Spring.IntermediateVelocity = Step.Spring.IntermediateVelocity; Out.Spring.IntermediateFacing = Step.Spring.IntermediateFacing;
		Out.Spring.IntermediateAngularVelocity = Step.Spring.IntermediateAngularVelocity;
		FMemoryWriter Ar(Out.SpringBytes, true); bool bNetSuccess = false;
		const bool bOK = Out.Spring.NetSerialize(Ar, nullptr, bNetSuccess) && bNetSuccess && !Ar.IsError();
		if (!Test.TestTrue(TEXT("Helper output serializes only after returning to GT"), bOK)) return false;
	}
	return true;
}

void SerializeResults(FArchive& Ar, TArray<FResult>& Values)
{
	uint32 FileMagic = Magic, FileSchema = Schema; Ar << FileMagic << FileSchema;
	FString Producer = OriginalModeSHA256; SerializeText(Ar, Producer, 64);
	int32 Count = Values.Num(); Ar << Count;
	if (FileMagic != Magic || FileSchema != Schema || Producer != OriginalModeSHA256 || Count != CaseCount) { Ar.SetError(); return; }
	if (Ar.IsLoading()) Values.SetNum(Count);
	for (auto& Value : Values)
	{
		SerializeText(Ar, Value.Id, 64); SerializeBoundedBytes(Ar, Value.InputBytes, 2048);
		Ar << Value.Velocity << Value.Angular << Value.bHasSpring;
		if (Value.bHasSpring) SerializeSpringFields(Ar, Value.Spring);
		SerializeBoundedBytes(Ar, Value.SpringBytes, 512);
		if (Ar.IsError()) return;
	}
}
bool CompareResults(FAutomationTestBase& Test, const TArray<FResult>& Actual, const TArray<FResult>& Expected)
{
	if (!Test.TestEqual(TEXT("Reference bounded case count"), Expected.Num(), Actual.Num())) return false;
	bool bOK = true;
	for (int32 Index = 0; Index < Actual.Num(); ++Index)
	{
		const auto& A = Actual[Index]; const auto& E = Expected[Index];
		const auto Check = [&](const TCHAR* Label, bool bValue) { bOK &= Test.TestTrue(*FString::Printf(TEXT("%s %s"), *A.Id, Label), bValue); };
		Check(TEXT("case identity/input bytes"), A.Id == E.Id && A.InputBytes == E.InputBytes);
		Check(TEXT("output velocity"), A.Velocity.Equals(E.Velocity, VectorTolerance));
		Check(TEXT("output angular velocity"), A.Angular.Equals(E.Angular, VectorTolerance));
		Check(TEXT("spring presence"), A.bHasSpring == E.bHasSpring);
		if (A.bHasSpring && E.bHasSpring)
		{
			Check(TEXT("spring velocity"), A.Spring.SpringVelocity.Equals(E.Spring.SpringVelocity, VectorTolerance));
			Check(TEXT("spring acceleration"), A.Spring.SpringAcceleration.Equals(E.Spring.SpringAcceleration, VectorTolerance));
			Check(TEXT("intermediate velocity"), A.Spring.IntermediateVelocity.Equals(E.Spring.IntermediateVelocity, VectorTolerance));
			Check(TEXT("intermediate facing"), A.Spring.IntermediateFacing.Equals(E.Spring.IntermediateFacing, QuaternionTolerance));
			Check(TEXT("intermediate angular velocity"), A.Spring.IntermediateAngularVelocity.Equals(E.Spring.IntermediateAngularVelocity, VectorTolerance));
			Check(TEXT("actual NetSerialize bytes"), A.SpringBytes == E.SpringBytes);
		}
	}
	return bOK;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSmoothGroundReference119Test, "ProjectJ.Mover.SmoothGroundReference119",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJSmoothGroundReference119Test::RunTest(const FString&)
{
	using namespace ProjectJSmoothGround119;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Reference owns a native world"), World)) return false;
	const TStrongObjectPtr<AActor> Owner(World->SpawnActor<AActor>());
	if (!TestNotNull(TEXT("Reference owns an actual authority actor"), Owner.Get()) || !TestTrue(TEXT("No role mutation"), Owner->HasAuthority()) ||
		!TestTrue(TEXT("Fixture is locally simulating rather than an interpolated simulated proxy"), Owner->GetLocalRole() != ROLE_SimulatedProxy)) { World->DestroyWorld(false); return false; }
	const TArray<FCase> Cases = MakeCases();
	TArray<FResult> Actual; bool bOK = true;
	for (const auto& Case : Cases)
	{
		FResult Result; if (!RunRealMode(*this, Owner.Get(), Case, Result)) { bOK = false; break; }
		Actual.Add(MoveTemp(Result));
	}
	World->DestroyWorld(false);
	if (!bOK || Actual.Num() != CaseCount) return false;
	TArray<uint8> Bytes; Bytes.Append(ProjectJSmoothGroundReference119::Payload, UE_ARRAY_COUNT(ProjectJSmoothGroundReference119::Payload));
	if (Bytes.Num() > MaxPayloadBytes) { AddError(TEXT("Permanent reference exceeds bounded schema.")); return false; }
	FMemoryReader Ar(Bytes, true); TArray<FResult> Expected; SerializeResults(Ar, Expected);
	if (Ar.IsError() || Ar.Tell() != Bytes.Num()) { AddError(TEXT("Invalid/trailing permanent reference payload.")); return false; }
	AddInfo(TEXT("Compare live real mode against the immutable pre-extraction reference."));
	const bool bModeMatches = CompareResults(*this, Actual, Expected);
	TArray<FResult> Direct; TArray<FOwnedStep> WorkerInput;
	Direct.Reserve(CaseCount); WorkerInput.Reserve(CaseCount);
	for (const auto& Case : Cases)
	{
		FOwnedStep Step = CaptureOwnedStep(Case);
		WorkerInput.Add(CaptureOwnedStep(Case)); // Separate owned storage, never the direct step's borrowed view.
		if (!TestTrue(TEXT("Direct and worker inputs own distinct spring storage before mutation"), &Step.Spring != &WorkerInput.Last().Spring)) return false;
		FSmoothWalkingStateView View{Step.Spring.SpringVelocity, Step.Spring.SpringAcceleration, Step.Spring.IntermediateVelocity,
			Step.Spring.IntermediateFacing, Step.Spring.IntermediateAngularVelocity};
		UGroundMovementUtils::ComputeSmoothWalkingMove(Step.Params, View, Step.Angular, Step.Velocity);
		FResult Result; if (!MaterializeResultGT(*this, Case, Step, Result)) return false;
		Direct.Add(MoveTemp(Result));
	}
	AddInfo(TEXT("Compare direct GT helper against the same original reference."));
	const bool bDirectMatches = CompareResults(*this, Direct, Expected);
	// Exactly one batch on the existing engine ThreadPool. It captures only owned plain values.
	TFuture<FWorkerBatch> Future = Async(EAsyncExecution::ThreadPool, [Steps = MoveTemp(WorkerInput)]() mutable
	{
		FWorkerBatch Batch; Batch.bRanOnGameThread = IsInGameThread();
		for (FOwnedStep& Step : Steps)
		{
			// The view is created inside this worker from its owned storage and never escapes the iteration.
			FSmoothWalkingStateView View{Step.Spring.SpringVelocity, Step.Spring.SpringAcceleration, Step.Spring.IntermediateVelocity,
				Step.Spring.IntermediateFacing, Step.Spring.IntermediateAngularVelocity};
			UGroundMovementUtils::ComputeSmoothWalkingMove(Step.Params, View, Step.Angular, Step.Velocity);
		}
		Batch.Steps = MoveTemp(Steps); return Batch;
	});
	FWorkerBatch Batch = Future.Get();
	const bool bActualWorker = TestFalse(TEXT("All twelve pure math cases actually ran off the GameThread"), Batch.bRanOnGameThread);
	if (!TestEqual(TEXT("The one existing ThreadPool batch returned every owned case"), Batch.Steps.Num(), CaseCount)) return false;
	TArray<FResult> Worker; Worker.Reserve(CaseCount);
	for (int32 Index = 0; Index < Cases.Num(); ++Index)
	{
		FResult Result; if (!MaterializeResultGT(*this, Cases[Index], Batch.Steps[Index], Result)) return false;
		Worker.Add(MoveTemp(Result));
	}
	AddInfo(TEXT("Compare joined real ThreadPool helper outputs against the same original reference."));
	const bool bWorkerMatches = CompareResults(*this, Worker, Expected);
	return bModeMatches && bDirectMatches && bActualWorker && bWorkerMatches;
}
#endif
