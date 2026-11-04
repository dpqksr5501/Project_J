// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_J.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "NetworkPredictionWorldManager.h"

namespace Project_JNetworkViewTests
{
struct FState
{
	float Value = 0.f;
	void Interpolate(const FState* From, const FState* To, float Alpha)
	{
		Value = FMath::Lerp(From->Value, To->Value, Alpha);
	}
	void ToString(FAnsiStringBuilderBase& Out) const { Out.Appendf("%f", Value); }
};

struct FDriver
{
	const FState* LastSync = nullptr;
	const FState* LastAux = nullptr;
	void FinalizeFrame(const FState* Sync, const FState* Aux) { LastSync = Sync; LastAux = Aux; }
	void SetHiddenForInterpolation(bool) {}
};

struct FModel : FNetworkPredictionModelDef
{
	using StateTypes = TNetworkPredictionStateTypes<void, FState, FState>;
	using Driver = FDriver;
	static const TCHAR* GetName() { return TEXT("ProjectJViewLifetimeFixture"); }
};
}

// This fixture drives the actual services and their public views, without
// creating actors, opening a socket or introducing a separate interpolation.
template<>
struct FNetworkPredictionDriver<Project_JNetworkViewTests::FModel>
	: FNetworkPredictionDriverBase<Project_JNetworkViewTests::FModel>
{
	using FDriver = Project_JNetworkViewTests::FDriver;
	static void DrawDebugOutline(FDriver*, FColor, float = 0.f) {}
	static void DrawDebugText3D(FDriver*, const TCHAR*, FColor, float = 0.f, float = 50.f) {}
	static void DispatchCues(TNetSimCueDispatcher<Project_JNetworkViewTests::FModel>*, FDriver*, int32, int32, int32) {}
};

namespace Project_JNetworkViewTests
{
template<bool bFixed>
void CheckService(FAutomationTestBase& Test)
{
	// The first instance remains registered while enough later ones are added to
	// force repeated sparse-storage growth, then its sparse slot is reused.
	constexpr int32 Count = 257;
	TModelDataStore<FModel> Store;
	TArray<TUniquePtr<FNetworkPredictionStateView>> Views;
	TArray<TUniquePtr<FDriver>> Drivers;
	using FService = std::conditional_t<bFixed, TFixedInterpolateService<FModel>, TIndependentInterpolateService<FModel>>;
	FService Service(&Store);
	FFixedTickState FixedTick;
	FixedTick.Interpolation.ToFrame = 1;
	FixedTick.Interpolation.PCT = .5f;
	FVariableTickState VariableTick;
	VariableTick.Interpolation.fTimeMS = 50.f;

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FNetworkPredictionID ID(Index + 1);
		Views.Add(MakeUnique<FNetworkPredictionStateView>());
		Drivers.Add(MakeUnique<FDriver>());
		auto& Instance = Store.Instances.FindOrAdd(ID);
		Instance.Info.View = Views[Index].Get();
		Instance.Info.Driver = Drivers[Index].Get();
		auto& Receive = Store.ClientRecv.FindOrAdd(ID);
		Receive.InstanceIdx = Store.Instances.GetIndexChecked(ID);
		Receive.FramesIdx = Store.Frames.GetIndex(ID);
		Receive.TraceID = ID.GetTraceID();
		Service.RegisterInstance(ID);
	}

	auto ReceiveFrame = [&](int32 Frame, bool bFirstOnly = false)
	{
		for (int32 Index = 0; Index < (bFirstOnly ? 1 : Count); ++Index)
		{
			const FNetworkPredictionID ID(Index + 1);
			auto& Receive = Store.ClientRecv.FindOrAdd(ID);
			Receive.ServerFrame = Frame;
			Receive.SimTimeMS = Frame * 100;
			Receive.SyncState->Value = float(Index * 10 + Frame * 2);
			Receive.AuxState->Value = float(Index * 10 + Frame * 4);
			NpResizeAndSetBit(Store.ClientRecvBitMask, Store.ClientRecv.GetIndexChecked(ID));
		}
		if constexpr (bFixed) Service.Reconcile(&FixedTick);
		else Service.Reconcile(&VariableTick);
	};
	auto Finalize = [&]
	{
		if constexpr (bFixed) Service.FinalizeFrame(1.f / 60.f, &FixedTick);
		else Service.FinalizeFrame(1.f / 60.f, &VariableTick);
	};

	auto CheckViews = [&](float SyncOffset, float AuxOffset, bool bFirstOnly = false)
	{
		for (int32 Index = 0; Index < (bFirstOnly ? 1 : Count); ++Index)
		{
			const FDriver& Driver = *Drivers[Index];
			const FNetworkPredictionStateView& View = *Views[Index];
			// Compare against the live value actually passed by the service before
			// dereferencing any exposed pointer: the old implementation fails
			// safely here instead of making the regression itself access freed RAM.
			const bool bSyncLive = Test.TestTrue(FString::Printf(TEXT("%s Sync view %d refers to the current service output"), bFixed ? TEXT("Fixed") : TEXT("Independent"), Index), View.PresentationSyncState == Driver.LastSync && Driver.LastSync);
			const bool bAuxLive = Test.TestTrue(FString::Printf(TEXT("%s Aux view %d refers to the current service output"), bFixed ? TEXT("Fixed") : TEXT("Independent"), Index), View.PresentationAuxState == Driver.LastAux && Driver.LastAux);
			if (bSyncLive) Test.TestEqual(TEXT("The public Sync view exposes the current output"), static_cast<const FState*>(View.PresentationSyncState)->Value, float(Index * 10) + SyncOffset);
			if (bAuxLive) Test.TestEqual(TEXT("The public Aux view exposes the current output"), static_cast<const FState*>(View.PresentationAuxState)->Value, float(Index * 10) + AuxOffset);
		}
	};

	ReceiveFrame(0);
	if constexpr (!bFixed)
	{
		// A cold stream with only one receive still has a current presentation.
		VariableTick.Interpolation.fTimeMS = 0.f;
		Finalize();
		CheckViews(0.f, 0.f);
	}
	ReceiveFrame(1);
	VariableTick.Interpolation.fTimeMS = 50.f;
	Finalize();
	CheckViews(1.f, 2.f);
	if constexpr (!bFixed)
	{
		// Starvation holds the last received endpoint; it must not leave the
		// public view at the previous half-interpolated value.
		VariableTick.Interpolation.fTimeMS = 125.f;
		Finalize();
		CheckViews(2.f, 4.f);
		FNetworkPredictionStateView& View = *Views[0];
		if (View.PresentationSyncState == Drivers[0]->LastSync && View.PresentationAuxState == Drivers[0]->LastAux)
		{
			// Mutate the same exposed storage that WritePresentationSyncState /
			// WritePresentationAuxState target, as based-movement correction does.
			// Neither the pending endpoint nor retained receive history may alias it.
			static_cast<FState*>(View.PresentationSyncState)->Value = -700.f;
			static_cast<FState*>(View.PresentationAuxState)->Value = -900.f;
			Test.TestEqual(TEXT("Presentation writes preserve the pending receive endpoint"), static_cast<const FState*>(View.PendingSyncState)->Value, 2.f);
			Test.TestEqual(TEXT("Presentation Aux writes preserve the pending receive endpoint"), static_cast<const FState*>(View.PendingAuxState)->Value, 4.f);
			Finalize();
			CheckViews(2.f, 4.f);
		}
		// New history resumes interpolation normally after the endpoint hold.
		ReceiveFrame(2);
		VariableTick.Interpolation.fTimeMS = 150.f;
		Finalize();
		CheckViews(3.f, 6.f);
	}
	const FNetworkPredictionID FirstID(1);
	Service.UnregisterInstance(FirstID);
	Test.TestTrue(TEXT("Unregister clears both exposed presentation views before storage is released"), Views[0]->PresentationSyncState == nullptr && Views[0]->PresentationAuxState == nullptr);
	Service.RegisterInstance(FirstID);
	ReceiveFrame(0, true);
	ReceiveFrame(1, true);
	VariableTick.Interpolation.fTimeMS = 50.f;
	Finalize();
	CheckViews(1.f, 2.f, true);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Service.UnregisterInstance(FNetworkPredictionID(Index + 1));
		Test.TestTrue(TEXT("Final retirement clears each public presentation view"), Views[Index]->PresentationSyncState == nullptr && Views[Index]->PresentationAuxState == nullptr);
	}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNetworkPredictionViewLifetimeTest, "ProjectJ.Foundation.Network.InterpolationViewLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJNetworkPredictionViewLifetimeTest::RunTest(const FString&)
{
	Project_JNetworkViewTests::CheckService<true>(*this);
	Project_JNetworkViewTests::CheckService<false>(*this);
	return true;
}
#endif
