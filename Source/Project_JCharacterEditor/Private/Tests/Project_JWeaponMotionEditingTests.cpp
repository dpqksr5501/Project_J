#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "WeaponMotionEditing.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWeaponMotionEditingTest, "ProjectJ.Maturity.Editor.WeaponSocketFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJWeaponMotionEditingTest::RunTest(const FString&)
{
	FTransform Socket(FRotator(0, 90, 0), FVector(10, 20, 30), FVector(2));
	FTransform Relative = FTransform::Identity;
	TestTrue(TEXT("Uniformly scaled socket accepts world drag"), Project_J::WeaponMotionEditor::ApplyWorldDelta(Relative, Socket, FVector(0, 20, 0), FRotator::ZeroRotator));
	TestTrue(TEXT("90 degree socket stores the drag in the correct local axis and scale"), Relative.GetTranslation().Equals(FVector(10, 0, 0)));
	Relative.SetRotation(FRotator(15, 30, -40).Quaternion());
	const FQuat WorldBefore = (Relative * Socket).GetRotation();
	const FRotator Delta(20, 0, 0);
	Project_J::WeaponMotionEditor::ApplyWorldDelta(Relative, Socket, FVector::ZeroVector, Delta);
	TestTrue(TEXT("Local rotation composes to the requested world rotation"), (Relative * Socket).GetRotation().Equals(Delta.Quaternion() * WorldBefore));
	const FTransform Before = Relative; Socket.SetScale3D(FVector(1, 2, 1));
	TestFalse(TEXT("Unsupported nonuniform attachment scale cannot silently corrupt a key"), Project_J::WeaponMotionEditor::ApplyWorldDelta(Relative, Socket, FVector(1), Delta));
	TestTrue(TEXT("Rejected edit preserves the key"), Before.Equals(Relative));
	return true;
}
#endif
