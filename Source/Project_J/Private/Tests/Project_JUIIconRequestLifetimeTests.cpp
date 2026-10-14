#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/Project_JPlayerHUDWidget.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
struct FIconLifetimeState
{
	TStrongObjectPtr<UProject_JSkillButton> Widget;
	TStrongObjectPtr<UProject_JSkillButton> CallbackOwner;
	TWeakObjectPtr<UProject_JSkillButton> WeakCallbackOwner;
	int32 StaleApplies = 0, CurrentApplies = 0, RetiredApplies = 0;
	int32 CallbackApplies = 0;
	bool bOwnerAliveDuringApply = false;
	bool bWrongTexture = false;
	double Started = FPlatformTime::Seconds();
};

class FFinishIconLifetime : public IAutomationLatentCommand
{
public:
	FFinishIconLifetime(FAutomationTestBase* InTest, TSharedRef<FIconLifetimeState> InState, TFunction<void()> InCleanup)
		: Test(InTest), State(InState), Cleanup(MoveTemp(InCleanup)) {}
	bool Update() override
	{
		const double Age = FPlatformTime::Seconds() - State->Started;
		if (Age < .25 || ((State->CurrentApplies == 0 || State->CallbackApplies == 0) && Age < 5)) { return false; }
		Test->TestEqual(TEXT("Canceled A request cannot apply after A-to-empty-to-A reuse"), State->StaleApplies, 0);
		Test->TestEqual(TEXT("Current request applies its actual loaded texture once"), State->CurrentApplies, 1);
		Test->TestFalse(TEXT("Completion resolves the requested texture"), State->bWrongTexture);
		Test->TestEqual(TEXT("Collected widget never receives completion"), State->RetiredApplies, 0);
		Test->TestEqual(TEXT("Completion that retires its owner runs once"), State->CallbackApplies, 1);
		Test->TestTrue(TEXT("Active completion pins its owner across reentrant GC"), State->bOwnerAliveDuringApply);
		CollectGarbage(RF_NoFlags);
		Test->TestFalse(TEXT("Completed request does not retain a retired owner"), State->WeakCallbackOwner.IsValid());
		Cleanup();
		return true;
	}
private:
	FAutomationTestBase* Test;
	TSharedRef<FIconLifetimeState> State;
	TFunction<void()> Cleanup;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUIIconRequestLifetimeTest, "ProjectJ.Structure.UI.IconRequestLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJUIIconRequestLifetimeTest::RunTest(const FString&)
{
	const FSoftObjectPath Path(TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
	UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Path.ToString());
	if (!TestNotNull(TEXT("Actual engine texture fixture"), Texture)) { return false; }
	auto State = MakeShared<FIconLifetimeState>();
	State->Widget.Reset(NewObject<UProject_JSkillButton>());
	auto* Delay = IConsoleManager::Get().FindConsoleVariable(TEXT("s.StreamableDelegateDelayFrames"));
	const int32 PreviousDelay = Delay ? Delay->GetInt() : 1;
	if (Delay) { Delay->Set(3, ECVF_SetByCode); }
	auto& Request = State->Widget->IconRequest;
	State->CallbackOwner.Reset(NewObject<UProject_JSkillButton>());
	State->WeakCallbackOwner = State->CallbackOwner.Get();
	State->CallbackOwner->IconRequest.Select(Path);
	State->CallbackOwner->IconRequest.Load(State->CallbackOwner.Get(), [State](UTexture2D*, uint64)
	{
		// Simulates an apply/UMG callback retiring its last external reference.
		// Do not dereference the widget after collection on the unfixed baseline.
		State->CallbackOwner.Reset();
		CollectGarbage(RF_NoFlags);
		State->bOwnerAliveDuringApply = State->WeakCallbackOwner.IsValid();
		++State->CallbackApplies;
	});
	Request.Select(Path);
	Request.Load(State->Widget.Get(), [State](UTexture2D*, uint64) { ++State->StaleApplies; });
	Request.Reset();
	Request.Select(Path);
	Request.Load(State->Widget.Get(), [State, Expected = TWeakObjectPtr<UTexture2D>(Texture)](UTexture2D* Loaded, uint64)
	{ ++State->CurrentApplies; State->bWrongTexture = Loaded != Expected.Get(); });
	auto* Retired = NewObject<UProject_JSkillButton>();
	TWeakObjectPtr<UProject_JSkillButton> WeakRetired(Retired);
	Retired->IconRequest.Select(Path);
	Retired->IconRequest.Load(Retired, [State](UTexture2D*, uint64) { ++State->RetiredApplies; });
	CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("A pending icon request does not keep the widget alive"), WeakRetired.IsValid());
	ADD_LATENT_AUTOMATION_COMMAND(FFinishIconLifetime(this, State, [State, Delay, PreviousDelay]()
	{
		State->Widget->IconRequest.Reset();
		State->Widget.Reset();
		if (Delay) { Delay->Set(PreviousDelay, ECVF_SetByCode); }
	}));
	return true;
}
#endif
