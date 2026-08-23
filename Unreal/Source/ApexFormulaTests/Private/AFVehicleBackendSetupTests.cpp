// Copyright ApexFormula. Original work. Not affiliated with any real motorsport series.

#include "AFVehicleCompatibilityLayer.h"
#include "AFWheelSetup.h"
#include "AFTypes.h"

#include "Misc/AutomationTest.h"

/**
 * Automation tests for the Milestone 2 vehicle backend configuration contract.
 *
 * Milestone reference: MILESTONE_2_IMPLEMENTATION.md section 7.
 *
 * Scope. These tests exercise FAFVehicleBackendSetup::ValidateSelf and the
 * behaviour of UAFVehicleCompatibilityLayer before any backend exists. They
 * deliberately do NOT create a world, spawn a pawn or step physics: handling,
 * grounding and oscillation are Milestone 2 acceptance criteria that can only
 * be established by playtesting, and a passing unit test must never be
 * presented as evidence that the car drives.
 *
 * Status: requires local compilation. These have never been executed.
 */

#if WITH_DEV_AUTOMATION_TESTS

static const EAutomationTestFlags AFVehicleBackendSetupTestFlags =
	EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ProductFilter;

/** One internally consistent front wheel. */
static FAFWheelSetup MakeSteeredWheel(const TCHAR* InBoneName)
{
	FAFWheelSetup Wheel;
	Wheel.BoneName = FName(InBoneName);
	Wheel.bAffectedBySteering = true;
	Wheel.MaxSteerAngleDeg = 22.0;
	Wheel.bDriven = false;
	Wheel.bAffectedByHandbrake = false;
	Wheel.MaxHandbrakeTorqueNm = 0.0;
	return Wheel;
}

/** One internally consistent rear wheel. */
static FAFWheelSetup MakeDrivenWheel(const TCHAR* InBoneName)
{
	FAFWheelSetup Wheel;
	Wheel.BoneName = FName(InBoneName);
	Wheel.bAffectedBySteering = false;
	Wheel.MaxSteerAngleDeg = 0.0;
	Wheel.bDriven = true;
	Wheel.bAffectedByHandbrake = true;
	Wheel.MaxHandbrakeTorqueNm = 3000.0;
	return Wheel;
}

/** A four-corner setup that every ValidateSelf check should accept. */
static FAFVehicleBackendSetup MakeValidSetup()
{
	FAFVehicleBackendSetup Setup;
	Setup.Wheels.Add(MakeSteeredWheel(TEXT("AF_Wheel_FL")));
	Setup.Wheels.Add(MakeSteeredWheel(TEXT("AF_Wheel_FR")));
	Setup.Wheels.Add(MakeDrivenWheel(TEXT("AF_Wheel_RL")));
	Setup.Wheels.Add(MakeDrivenWheel(TEXT("AF_Wheel_RR")));
	return Setup;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAFBackendSetupValidBaselineTest,
	"ApexFormula.Vehicle.BackendSetup.ValidBaselineHasNoProblems",
	AFVehicleBackendSetupTestFlags)

bool FAFBackendSetupValidBaselineTest::RunTest(const FString& Parameters)
{
	const FAFVehicleBackendSetup Setup = MakeValidSetup();
	const TArray<FString> Problems = Setup.ValidateSelf();

	// If this fails, every negative test below is meaningless, because a
	// problem would be reported whether or not the mutation was applied.
	TestEqual(TEXT("A four-corner baseline setup reports no problems"), Problems.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAFBackendSetupNoWheelsTest,
	"ApexFormula.Vehicle.BackendSetup.EmptyWheelArrayIsRejected",
	AFVehicleBackendSetupTestFlags)

bool FAFBackendSetupNoWheelsTest::RunTest(const FString& Parameters)
{
	FAFVehicleBackendSetup Setup = MakeValidSetup();
	Setup.Wheels.Empty();

	TestTrue(TEXT("A setup with no wheels reports at least one problem"),
		Setup.ValidateSelf().Num() > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAFBackendSetupNoDrivenWheelTest,
	"ApexFormula.Vehicle.BackendSetup.NoDrivenWheelIsRejected",
	AFVehicleBackendSetupTestFlags)

bool FAFBackendSetupNoDrivenWheelTest::RunTest(const FString& Parameters)
{
	FAFVehicleBackendSetup Setup = MakeValidSetup();
	for (FAFWheelSetup& Wheel : Setup.Wheels)
	{
		Wheel.bDriven = false;
	}

	// A car with no driven wheel cannot accelerate, which is Milestone 2
	// acceptance criterion A1. Catching it in configuration is much cheaper
	// than discovering it as "the throttle does nothing" in a playtest.
	TestTrue(TEXT("A setup with no driven wheel reports at least one problem"),
		Setup.ValidateSelf().Num() > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAFBackendSetupNoSteeredWheelTest,
	"ApexFormula.Vehicle.BackendSetup.NoSteeredWheelIsRejected",
	AFVehicleBackendSetupTestFlags)

bool FAFBackendSetupNoSteeredWheelTest::RunTest(const FString& Parameters)
{
	FAFVehicleBackendSetup Setup = MakeValidSetup();
	for (FAFWheelSetup& Wheel : Setup.Wheels)
	{
		Wheel.bAffectedBySteering = false;
		Wheel.MaxSteerAngleDeg = 0.0;
	}

	TestTrue(TEXT("A setup with no steered wheel reports at least one problem"),
		Setup.ValidateSelf().Num() > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAFBackendSetupMassTest,
	"ApexFormula.Vehicle.BackendSetup.NonPositiveMassIsRejected",
	AFVehicleBackendSetupTestFlags)

bool FAFBackendSetupMassTest::RunTest(const FString& Parameters)
{
	FAFVehicleBackendSetup Setup = MakeValidSetup();
	Setup.DryMassKg = 0.0;

	TestTrue(TEXT("Zero dry mass reports at least one problem"),
		Setup.ValidateSelf().Num() > 0);

	Setup.DryMassKg = -1.0;
	TestTrue(TEXT("Negative dry mass reports at least one problem"),
		Setup.ValidateSelf().Num() > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAFBackendSetupRpmOrderingTest,
	"ApexFormula.Vehicle.BackendSetup.PeakTorqueRpmAboveMaxRpmIsRejected",
	AFVehicleBackendSetupTestFlags)

bool FAFBackendSetupRpmOrderingTest::RunTest(const FString& Parameters)
{
	FAFVehicleBackendSetup Setup = MakeValidSetup();

	// Peak torque cannot occur above the rev limit. A setup that claims it
	// does produces a torque curve the engine will clamp silently.
	Setup.MaxRpm = 12000.0;
	Setup.PeakTorqueRpm = 12001.0;

	TestTrue(TEXT("Peak torque rpm above max rpm reports at least one problem"),
		Setup.ValidateSelf().Num() > 0);

	Setup.PeakTorqueRpm = 12000.0;
	TestEqual(TEXT("Peak torque rpm exactly at max rpm is accepted"),
		Setup.ValidateSelf().Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAFBackendSetupGearCountTest,
	"ApexFormula.Vehicle.BackendSetup.ZeroForwardGearsIsRejected",
	AFVehicleBackendSetupTestFlags)

bool FAFBackendSetupGearCountTest::RunTest(const FString& Parameters)
{
	FAFVehicleBackendSetup Setup = MakeValidSetup();
	Setup.ForwardGearCount = 0;

	TestTrue(TEXT("Zero forward gears reports at least one problem"),
		Setup.ValidateSelf().Num() > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAFBackendSetupWheelProblemPrefixTest,
	"ApexFormula.Vehicle.BackendSetup.WheelProblemsAreIndexPrefixed",
	AFVehicleBackendSetupTestFlags)

bool FAFBackendSetupWheelProblemPrefixTest::RunTest(const FString& Parameters)
{
	FAFVehicleBackendSetup Setup = MakeValidSetup();

	// Break exactly one corner. Without an index in the message, an author
	// reading the log cannot tell which of four identical-looking wheels is
	// wrong, which is the whole point of the prefix.
	Setup.Wheels[2].RadiusM = 0.0;

	const TArray<FString> Problems = Setup.ValidateSelf();
	TestTrue(TEXT("Breaking one wheel reports at least one problem"), Problems.Num() > 0);

	bool bFoundIndexedProblem = false;
	for (const FString& Problem : Problems)
	{
		if (Problem.Contains(TEXT("Wheel 2")))
		{
			bFoundIndexedProblem = true;
			break;
		}
	}

	TestTrue(TEXT("The problem naming wheel index 2 is present"), bFoundIndexedProblem);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAFBackendSetupOscillationGuardTest,
	"ApexFormula.Vehicle.BackendSetup.UnderdampedSuspensionIsRejected",
	AFVehicleBackendSetupTestFlags)

bool FAFBackendSetupOscillationGuardTest::RunTest(const FString& Parameters)
{
	FAFVehicleBackendSetup Setup = MakeValidSetup();

	// Milestone 2 acceptance criterion A2 forbids a car that oscillates at
	// rest. The wheel-level damping guard is the configuration-time defence
	// against it, so the aggregate must surface it rather than swallow it.
	Setup.Wheels[0].SuspensionDampingRatio = 0.2;

	TestTrue(TEXT("An underdamped corner reports at least one problem"),
		Setup.ValidateSelf().Num() > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAFCompatibilityLayerUnboundTest,
	"ApexFormula.Vehicle.CompatibilityLayer.UnboundBackendIsInert",
	AFVehicleBackendSetupTestFlags)

bool FAFCompatibilityLayerUnboundTest::RunTest(const FString& Parameters)
{
	UAFVehicleCompatibilityLayer* Layer =
		NewObject<UAFVehicleCompatibilityLayer>(GetTransientPackage());

	TestNotNull(TEXT("A compatibility layer can be constructed"), Layer);
	if (Layer == nullptr)
	{
		return false;
	}

	// Before CreateBackendMovement runs there is nothing to talk to. Every
	// read must return a safe neutral value rather than crash, because the
	// pawn queries the layer from Tick regardless of backend state.
	TestFalse(TEXT("Wheel parameters are not applied before configuration"),
		Layer->AreWheelParametersApplied());

	TestEqual(TEXT("No input frames have been applied yet"),
		Layer->GetAppliedFrameCount(), 0);

	FAFVehicleInputFrame Frame;
	Frame.Throttle = 1.0f;
	Frame.SessionTime = 0.0;

	TestFalse(TEXT("Applying a frame with no backend reports failure"),
		Layer->ApplyInputFrame(Frame));

	TestEqual(TEXT("Forward speed with no backend is zero"),
		Layer->GetForwardSpeedKph(), 0.0, 1.0e-9);

	// An empty wheel array must not read as "fully grounded". Reporting true
	// here would let the pawn record a last-good-transform for a car that has
	// no wheels at all.
	TestFalse(TEXT("A layer with no wheels is not reported as grounded"),
		Layer->AreAllWheelsGrounded());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
