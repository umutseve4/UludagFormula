// Copyright ApexFormula. Original work. Not affiliated with any real motorsport series.

#include "Misc/AutomationTest.h"
#include "AFTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Tests for the plain value types in AFTypes.h.
 *
 * FAFVehicleInputFrame is the only thing a controller hands to a vehicle, so
 * its clamping contract is load bearing: a malformed replay file or a badly
 * configured input mapping must not be able to push an out-of-range axis into
 * the vehicle. These tests are pure value-type checks - no world, no actor,
 * no tick - which is why they belong in the fast tier.
 */
static const EAutomationTestFlags AFCoreTypesTestFlags =
	EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAFInputFrameDefaultsTest,
	"ApexFormula.Core.InputFrame.Defaults",
	AFCoreTypesTestFlags)

bool FAFInputFrameDefaultsTest::RunTest(const FString& Parameters)
{
	const FAFVehicleInputFrame Frame;

	TestEqual(TEXT("Default throttle is zero"), Frame.Throttle, 0.0f);
	TestEqual(TEXT("Default brake is zero"), Frame.Brake, 0.0f);
	TestEqual(TEXT("Default steer is zero"), Frame.Steer, 0.0f);
	TestEqual(TEXT("Default clutch is zero"), Frame.Clutch, 0.0f);
	TestEqual(TEXT("Default session time is zero"), Frame.SessionTime, 0.0, 1.0e-9);

	TestFalse(TEXT("Default shift up is not requested"), Frame.bShiftUp);
	TestFalse(TEXT("Default shift down is not requested"), Frame.bShiftDown);
	TestFalse(TEXT("Default energy deployment is not requested"), Frame.bDeployEnergy);
	TestFalse(TEXT("Default drag reduction is not requested"), Frame.bRequestDragReduction);

	// A default constructed frame must read as neutral, otherwise a vehicle
	// that has never received input would appear to be driving itself.
	TestTrue(TEXT("Default frame is neutral"), Frame.IsNeutral());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAFInputFrameSanitiseTest,
	"ApexFormula.Core.InputFrame.Sanitise",
	AFCoreTypesTestFlags)

bool FAFInputFrameSanitiseTest::RunTest(const FString& Parameters)
{
	// Over the top of every declared range.
	FAFVehicleInputFrame High;
	High.Throttle = 4.0f;
	High.Brake    = 2.5f;
	High.Clutch   = 9.0f;
	High.Steer    = 3.0f;
	High.Sanitise();

	TestEqual(TEXT("Throttle clamps to one"), High.Throttle, 1.0f);
	TestEqual(TEXT("Brake clamps to one"), High.Brake, 1.0f);
	TestEqual(TEXT("Clutch clamps to one"), High.Clutch, 1.0f);
	TestEqual(TEXT("Steer clamps to positive one"), High.Steer, 1.0f);

	// Under the bottom of every declared range. Throttle, brake and clutch are
	// unipolar, so a negative value must land on zero rather than on minus one.
	FAFVehicleInputFrame Low;
	Low.Throttle = -4.0f;
	Low.Brake    = -2.5f;
	Low.Clutch   = -9.0f;
	Low.Steer    = -3.0f;
	Low.Sanitise();

	TestEqual(TEXT("Negative throttle clamps to zero"), Low.Throttle, 0.0f);
	TestEqual(TEXT("Negative brake clamps to zero"), Low.Brake, 0.0f);
	TestEqual(TEXT("Negative clutch clamps to zero"), Low.Clutch, 0.0f);
	TestEqual(TEXT("Negative steer clamps to minus one"), Low.Steer, -1.0f);

	// Values already in range must survive untouched. Sanitise is a clamp, not
	// a quantiser: rounding here would silently change driver intent.
	FAFVehicleInputFrame InRange;
	InRange.Throttle = 0.25f;
	InRange.Brake    = 0.5f;
	InRange.Clutch   = 0.75f;
	InRange.Steer    = -0.125f;
	InRange.Sanitise();

	TestEqual(TEXT("In-range throttle is unchanged"), InRange.Throttle, 0.25f);
	TestEqual(TEXT("In-range brake is unchanged"), InRange.Brake, 0.5f);
	TestEqual(TEXT("In-range clutch is unchanged"), InRange.Clutch, 0.75f);
	TestEqual(TEXT("In-range steer is unchanged"), InRange.Steer, -0.125f);

	// Sanitise must not invent or discard a timestamp; a shifted session time
	// would desynchronise replay playback from the recorded lap.
	FAFVehicleInputFrame Timed;
	Timed.SessionTime = 123.456;
	Timed.Sanitise();
	TestEqual(TEXT("Session time is not touched by Sanitise"), Timed.SessionTime, 123.456, 1.0e-9);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAFInputFrameNeutralTest,
	"ApexFormula.Core.InputFrame.Neutral",
	AFCoreTypesTestFlags)

bool FAFInputFrameNeutralTest::RunTest(const FString& Parameters)
{
	// Each axis on its own must break neutrality.
	{
		FAFVehicleInputFrame Frame;
		Frame.Throttle = 0.2f;
		TestFalse(TEXT("Throttle applied is not neutral"), Frame.IsNeutral());
	}
	{
		FAFVehicleInputFrame Frame;
		Frame.Brake = 0.2f;
		TestFalse(TEXT("Brake applied is not neutral"), Frame.IsNeutral());
	}
	{
		FAFVehicleInputFrame Frame;
		Frame.Steer = -0.2f;
		TestFalse(TEXT("Negative steer is not neutral"), Frame.IsNeutral());
	}
	{
		FAFVehicleInputFrame Frame;
		Frame.Clutch = 0.2f;
		TestFalse(TEXT("Clutch applied is not neutral"), Frame.IsNeutral());
	}

	// Each button on its own must break neutrality too. A gear request with no
	// axis movement is still driver intent and must reach the vehicle.
	{
		FAFVehicleInputFrame Frame;
		Frame.bShiftUp = true;
		TestFalse(TEXT("Shift up alone is not neutral"), Frame.IsNeutral());
	}
	{
		FAFVehicleInputFrame Frame;
		Frame.bShiftDown = true;
		TestFalse(TEXT("Shift down alone is not neutral"), Frame.IsNeutral());
	}
	{
		FAFVehicleInputFrame Frame;
		Frame.bDeployEnergy = true;
		TestFalse(TEXT("Energy deployment alone is not neutral"), Frame.IsNeutral());
	}
	{
		FAFVehicleInputFrame Frame;
		Frame.bRequestDragReduction = true;
		TestFalse(TEXT("Drag reduction alone is not neutral"), Frame.IsNeutral());
	}

	// Session time is not driver intent, so advancing the clock on an otherwise
	// idle frame must leave it neutral.
	{
		FAFVehicleInputFrame Frame;
		Frame.SessionTime = 42.0;
		TestTrue(TEXT("Session time alone is still neutral"), Frame.IsNeutral());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAFSessionEnumTest,
	"ApexFormula.Core.Enums.Ordering",
	AFCoreTypesTestFlags)

bool FAFSessionEnumTest::RunTest(const FString& Parameters)
{
	// These enums are serialised into save games and telemetry, so their
	// underlying values are part of the on-disk contract. Reordering them
	// later would silently reinterpret existing saved data - this test makes
	// that a build failure instead.
	TestEqual(TEXT("Practice is zero"), (int32)EAFSessionType::Practice, 0);
	TestEqual(TEXT("Qualifying is one"), (int32)EAFSessionType::Qualifying, 1);
	TestEqual(TEXT("Race is two"), (int32)EAFSessionType::Race, 2);
	TestEqual(TEXT("TimeTrial is three"), (int32)EAFSessionType::TimeTrial, 3);

	TestEqual(TEXT("NotStarted is zero"), (int32)EAFSessionPhase::NotStarted, 0);
	TestEqual(TEXT("Forming is one"), (int32)EAFSessionPhase::Forming, 1);
	TestEqual(TEXT("Running is two"), (int32)EAFSessionPhase::Running, 2);
	TestEqual(TEXT("Suspended is three"), (int32)EAFSessionPhase::Suspended, 3);
	TestEqual(TEXT("Finishing is four"), (int32)EAFSessionPhase::Finishing, 4);
	TestEqual(TEXT("Complete is five"), (int32)EAFSessionPhase::Complete, 5);

	// NotInvalidated must stay at zero: a zero-initialised lap record has to
	// mean "the lap counts", not "the lap was thrown out for track limits".
	TestEqual(TEXT("NotInvalidated is zero"), (int32)EAFLapInvalidationReason::NotInvalidated, 0);
	TestEqual(TEXT("TrackLimits is one"), (int32)EAFLapInvalidationReason::TrackLimits, 1);
	TestEqual(TEXT("MissedCheckpoint is two"), (int32)EAFLapInvalidationReason::MissedCheckpoint, 2);
	TestEqual(TEXT("WrongDirection is three"), (int32)EAFLapInvalidationReason::WrongDirection, 3);
	TestEqual(TEXT("Collision is four"), (int32)EAFLapInvalidationReason::Collision, 4);
	TestEqual(TEXT("VehicleReset is five"), (int32)EAFLapInvalidationReason::VehicleReset, 5);
	TestEqual(TEXT("PitLane is six"), (int32)EAFLapInvalidationReason::PitLane, 6);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
