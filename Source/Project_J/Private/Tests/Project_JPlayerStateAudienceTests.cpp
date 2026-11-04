#if WITH_DEV_AUTOMATION_TESTS
#include "Network/Project_JPlayerStateAudienceFilter.h"
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerStateAudienceBoundsTest, "ProjectJ.MMO.Network.PlayerStateAudienceBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJPlayerStateAudienceBoundsTest::RunTest(const FString&)
{
	FVector Center; float Radius = 0;
	using Filter = UProject_JPlayerStateAudienceFilter;
	TestNull(TEXT("Missing world never creates a replication filter"), Filter::Find(nullptr));
	TestFalse(TEXT("Missing state cannot bind a filter"), Filter::TryBind(nullptr));
	const FVector Pawn(234567, -345678, 1234), Avatar = Pawn + FVector(30000, 40000, 12000);
	TestTrue(TEXT("A separated controlled Pawn and retained ASC Avatar admit an enclosing bound"),
		Filter::BuildConservativeBound(Pawn, 15000, Avatar, 5000, 150000, Center, Radius));
	TestTrue(TEXT("The existing controlled-Pawn center remains exact"), Center == Pawn);
	// Test actual sphere boundary points against Iris's stored float squared radius.
	// These independently cover all signed axes plus distributed 3D directions.
	for (int32 Sphere = 0; Sphere < 2; ++Sphere)
	{
		const FVector Source = Sphere ? Avatar : Pawn;
		const float SourceRadius = Sphere ? 5000.f : 15000.f;
		const double ExactRadius = FMath::Max(double(SourceRadius), FMath::Sqrt(double(SourceRadius * SourceRadius)));
		for (int32 Index = 0; Index < 32; ++Index)
		{
			const double Angle = Index * (2.0 * UE_DOUBLE_PI / 32.0);
			const FVector Direction = FVector(FMath::Cos(Angle), FMath::Sin(Angle), Index % 2 ? 0.75 : -0.75).GetSafeNormal();
			const FVector Point = Source + Direction * ExactRadius;
			if (!TestTrue(TEXT("Every tested source exact-distance boundary lies within stored PS square"),
				FVector::DistSquared(Point, Center) <= double(Radius * Radius))) return false;
		}
	}
	TestTrue(TEXT("Identical source spheres preserve their radius under float-square rounding"),
		Filter::BuildConservativeBound(Pawn, 15000.13f, Pawn, 15000.13f, 150000, Center, Radius) &&
		Radius >= 15000.13f && double(Radius * Radius) >= double(15000.13f * 15000.13f));
	TestFalse(TEXT("Union above the grid maximum uses fallback, never an inward clamp"),
		Filter::BuildConservativeBound(Pawn, 15000, Pawn + FVector(145001, 0, 0), 5000, 150000, Center, Radius));
	TestTrue(TEXT("Failure returns finite placeholder geometry"), Center.IsZero() && Radius == 0);
	TestFalse(TEXT("No outward-safe representation at exact max uses fallback"),
		Filter::BuildConservativeBound(Pawn, 150000, Pawn, 150000, 150000, Center, Radius));
	TestFalse(TEXT("Negative source radius is rejected"), Filter::BuildConservativeBound(Pawn, -1, Pawn, 10, 150000, Center, Radius));
	TestFalse(TEXT("NaN radius is rejected"), Filter::BuildConservativeBound(Pawn, std::numeric_limits<float>::quiet_NaN(), Pawn, 10, 150000, Center, Radius));
	TestFalse(TEXT("Infinite source position is rejected"), Filter::BuildConservativeBound(FVector(std::numeric_limits<double>::infinity(), 0, 0), 1, Pawn, 1, 150000, Center, Radius));
	TestFalse(TEXT("Squared-radius overflow is rejected"), Filter::BuildConservativeBound(Pawn, FLT_MAX, Pawn, 1, FLT_MAX, Center, Radius));
	TestFalse(TEXT("Unbounded grid maximum is not used as an allow-all sphere"), Filter::BuildConservativeBound(Pawn, 1, Pawn, 1, 0, Center, Radius));
	return true;
}
#endif
