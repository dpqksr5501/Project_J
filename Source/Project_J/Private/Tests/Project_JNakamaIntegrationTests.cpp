#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Backend/Project_JNakamaServicePort.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
struct FNakamaClaimState
{
	TStrongObjectPtr<UGameInstance> Instance;
	TOptional<ProjectJ::MMO::FServiceResponse> Response;
	double Deadline = FPlatformTime::Seconds() + 12.0;

	~FNakamaClaimState()
	{
		if (!Instance.IsValid()) return;
		UWorld* World = Instance->GetWorld();
		Instance->Shutdown();
		if (World)
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	}
};

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FWaitForNakamaClaim, FAutomationTestBase*, Test,
	TSharedPtr<FNakamaClaimState>, State);

bool FWaitForNakamaClaim::Update()
{
	if (!State->Response.IsSet())
	{
		if (FPlatformTime::Seconds() < State->Deadline) return false;
		Test->AddError(TEXT("The local Nakama claim did not complete."));
		return true;
	}
	const auto& Response = State->Response.GetValue();
	if (!Test->TestTrue(TEXT("Nakama accepted the Unreal HTTP request"), Response.Status == ProjectJ::MMO::EServiceStatus::Success)) return true;
	const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Response.Payload.GetData()), Response.Payload.Num());
	TSharedPtr<FJsonObject> Body;
	if (!Test->TestTrue(TEXT("Nakama returned an unwrapped result"),
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text.Length(), Text.Get())), Body) && Body.IsValid())) return true;
	FString Status, ItemId;
	int32 Quantity = 0;
	Test->TestTrue(TEXT("Unreal request committed a real database item"), Body->TryGetStringField(TEXT("status"), Status)
		&& Status == TEXT("COMMITTED") && Body->TryGetStringField(TEXT("item_instance_id"), ItemId)
		&& !ItemId.IsEmpty() && Body->TryGetNumberField(TEXT("quantity"), Quantity) && Quantity == 3);
	return true;
}
}

class FProjectJNakamaRealClaimTest final : public FAutomationTestBase
{
public:
	FProjectJNakamaRealClaimTest() : FAutomationTestBase(TEXT("FProjectJNakamaRealClaimTest"), false) {}
	EAutomationTestFlags GetTestFlags() const override { return EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter; }
	uint32 GetRequiredDeviceNum() const override { return 1; }
	FString GetBeautifiedTestName() const override { return TEXT("ProjectJ.MMO.Integration.NakamaRealMailClaim"); }
	void GetTests(TArray<FString>& Names, TArray<FString>& Commands) const override
	{
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_J_TEST_MAIL_ID")).IsEmpty()) return;
		Names.Add(GetBeautifiedTestName());
		Commands.AddDefaulted();
	}
	bool RunTest(const FString&) override;
};

static FProjectJNakamaRealClaimTest GNakamaRealClaimTest;

bool FProjectJNakamaRealClaimTest::RunTest(const FString&)
{
	FGuid Character, Mail, Key;
	if (!FGuid::Parse(FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_J_TEST_MAIL_CHARACTER")), Character)
		|| !FGuid::Parse(FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_J_TEST_MAIL_ID")), Mail)
		|| !FGuid::Parse(FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_J_TEST_MAIL_KEY")), Key))
	{
		AddError(TEXT("The isolated backend runner must supply its seeded mail command."));
		return false;
	}
	auto State = MakeShared<FNakamaClaimState>();
	State->Instance.Reset(NewObject<UGameInstance>(GEngine));
	State->Instance->InitializeStandalone();
	auto* Port = State->Instance->GetSubsystem<UProject_JNakamaServicePortSubsystem>();
	if (!TestNotNull(TEXT("Initialize the production Nakama service port"), Port)) return false;
	Port->SetRuntimeMode(EProject_JBackendRuntimeMode::LocalDockerNakama);
	ProjectJ::MMO::FServiceRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.IdempotencyKey = Key;
	Request.Service = TEXT("Mail");
	Request.Operation = TEXT("Claim");
	Request.Intent = ProjectJ::MMO::EServiceRequestIntent::Mutation;
	const FString Json = FString::Printf(TEXT("{\"character_id\":\"%s\",\"mail_id\":\"%s\",\"session_epoch\":\"1\"}"),
		*Character.ToString(EGuidFormats::DigitsWithHyphensLower), *Mail.ToString(EGuidFormats::DigitsWithHyphensLower));
	FTCHARToUTF8 Utf8(*Json);
	Request.Payload.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	const TWeakPtr<FNakamaClaimState> Weak = State;
	Port->Submit(MoveTemp(Request), [Weak](ProjectJ::MMO::FServiceResponse Response)
	{
		if (const auto Pinned = Weak.Pin()) Pinned->Response = MoveTemp(Response);
	});
	ADD_LATENT_AUTOMATION_COMMAND(FWaitForNakamaClaim(this, State));
	return true;
}
#endif
