#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/NetworkVersion.h"
#include "Misc/ScopeExit.h"
#include "Project_J.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJRealtimeProtocolAdmissionTest,
    "ProjectJ.Foundation.Network.RealtimeProtocolAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJRealtimeProtocolAdmissionTest::RunTest(const FString&)
{
    const auto& Versions = FNetworkVersion::GetNetworkCustomVersions();
    const auto* GameVersion = Versions.GetVersion(FGameNetworkCustomVersion::Guid);
    if (!TestNotNull(TEXT("Primary module initializes the engine game protocol slot"), GameVersion)) return false;
    TestTrue(TEXT("Breaking realtime schema has an explicit non-default protocol"), ProjectJ::Network::RealtimeProtocolVersion > 0u);
    TestEqual(TEXT("Actual network protocol matches the project's wire contract"),
        FNetworkVersion::GetNetworkProtocolVersion(FGameNetworkCustomVersion::Guid), ProjectJ::Network::RealtimeProtocolVersion);
    TestEqual(TEXT("Replay does not advertise untested compatibility with an old wire format"),
        FNetworkVersion::GetCompatibleNetworkProtocolVersion(FGameNetworkCustomVersion::Guid), ProjectJ::Network::RealtimeProtocolVersion);

    const uint32 Before = FNetworkVersion::GetLocalNetworkVersion(false);
    const auto ReplayBefore = FNetworkVersion::GetReplayVersion();
    const FString ProjectVersionBefore = FNetworkVersion::GetProjectVersion();
    const int32 EntriesBefore = Versions.GetAllVersions().Num();
    // Both official setters invalidate their relevant cached checksums. Reusing
    // the same contract on module initialization must remain idempotent.
    FNetworkVersion::SetGameNetworkProtocolVersion(ProjectJ::Network::RealtimeProtocolVersion);
    FNetworkVersion::SetGameCompatibleNetworkProtocolVersion(ProjectJ::Network::RealtimeProtocolVersion);
    TestEqual(TEXT("Repeated initialization has the same real network checksum"), FNetworkVersion::GetLocalNetworkVersion(false), Before);
    TestEqual(TEXT("Repeated initialization has the same replay checksum"), FNetworkVersion::GetReplayVersion().NetworkVersion, ReplayBefore.NetworkVersion);
    TestEqual(TEXT("Registration reuses the engine slot without adding custom entries"), Versions.GetAllVersions().Num(), EntriesBefore);
    TestEqual(TEXT("Realtime registration does not rewrite the project/product version"), FNetworkVersion::GetProjectVersion(), ProjectVersionBefore);
    // Retain the old class-export boundary and reject the immediately preceding
    // quest-history and preceding realtime schemas through the engine's real compatibility path.
    TestFalse(TEXT("No override bypasses protocol admission"), FNetworkVersion::IsNetworkCompatibleOverride.IsBound());
    TestFalse(TEXT("No override replaces the protocol checksum"), FNetworkVersion::GetLocalNetworkVersionOverride.IsBound());
    const uint32 PreviousProtocols[] = {8u, 11u, 12u};
    for (uint32 PreviousProtocol : PreviousProtocols)
    {
        uint32 PreviousChecksum = 0;
        uint32 PreviousReplayChecksum = 0;
        {
            ON_SCOPE_EXIT
            {
                FNetworkVersion::SetGameNetworkProtocolVersion(ProjectJ::Network::RealtimeProtocolVersion);
                FNetworkVersion::SetGameCompatibleNetworkProtocolVersion(ProjectJ::Network::RealtimeProtocolVersion);
            };
            FNetworkVersion::SetGameNetworkProtocolVersion(PreviousProtocol);
            FNetworkVersion::SetGameCompatibleNetworkProtocolVersion(PreviousProtocol);
            PreviousChecksum = FNetworkVersion::GetLocalNetworkVersion(false);
            PreviousReplayChecksum = FNetworkVersion::GetReplayVersion().NetworkVersion;
        }
        TestTrue(TEXT("Previous class export schema has a different engine checksum"), PreviousChecksum != Before);
        TestFalse(TEXT("Current server rejects old peer"), FNetworkVersion::IsNetworkCompatible(Before, PreviousChecksum));
        TestFalse(TEXT("Old server rejects current peer"), FNetworkVersion::IsNetworkCompatible(PreviousChecksum, Before));
        TestTrue(TEXT("Previous replay is not advertised as compatible"), PreviousReplayChecksum != ReplayBefore.NetworkVersion);
        TestEqual(TEXT("Admission test restores current checksum"), FNetworkVersion::GetLocalNetworkVersion(false), Before);
        TestEqual(TEXT("Admission test restores current replay checksum"), FNetworkVersion::GetReplayVersion().NetworkVersion, ReplayBefore.NetworkVersion);
    }
    return !HasAnyErrors();
}
#endif
