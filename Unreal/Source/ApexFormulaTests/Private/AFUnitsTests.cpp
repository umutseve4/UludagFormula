// Copyright ApexFormula. Original work. Not affiliated with any real motorsport series.

#include "AFUnits.h"
#include "Misc/AutomationTest.h"

/**
 * Unit conversion and lap-time formatting tests.
 *
 * Reference: DECISION_LOG.md D-013, the unit and axis contract.
 * Status: requires local compilation. These have never been executed.
 */

#if WITH_DEV_AUTOMATION_TESTS

static const EAutomationTestFlags AFTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::CommandletContext |
	EAutomationTestFlags::ClientContext |
	EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFUnitsLengthTest,
	"ApexFormula.Core.Units.Length", AFTestFlags)

bool FAFUnitsLengthTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("1 m is 100 cm"), UAFUnitsHelper::MetresToCm(1.0), 100.0);
	TestEqual(TEXT("250 cm is 2.5 m"), UAFUnitsHelper::CmToMetres(250.0), 2.5);

	// Round trip must be lossless for a representative wheelbase.
	const double Wheelbase = 3.60;
	TestEqual(TEXT("Metre round trip"),
		UAFUnitsHelper::CmToMetres(UAFUnitsHelper::MetresToCm(Wheelbase)), Wheelbase);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFUnitsAxisTest,
	"ApexFormula.Core.Units.Axis", AFTestFlags)

bool FAFUnitsAxisTest::RunTest(const FString& Parameters)
{
	// D-013: Blender is right handed Z up, Unreal is left handed Z up.
	// The contract is x -> x, y -> negated y, z -> z, all scaled by 100.
	const FVector Blender(1.0, 2.0, 3.0);
	const FVector Unreal = UAFUnitsHelper::BlenderPointToUnrealCm(Blender);

	TestEqual(TEXT("X scales"), Unreal.X, 100.0);
	TestEqual(TEXT("Y negates and scales"), Unreal.Y, -200.0);
	TestEqual(TEXT("Z scales"), Unreal.Z, 300.0);

	// The inverse must return the original point exactly.
	const FVector Back = UAFUnitsHelper::UnrealCmToBlenderPoint(Unreal);
	TestEqual(TEXT("Axis round trip X"), Back.X, Blender.X);
	TestEqual(TEXT("Axis round trip Y"), Back.Y, Blender.Y);
	TestEqual(TEXT("Axis round trip Z"), Back.Z, Blender.Z);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFUnitsSpeedTest,
	"ApexFormula.Core.Units.Speed", AFTestFlags)

bool FAFUnitsSpeedTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("10 m/s is 36 km/h"), UAFUnitsHelper::MpsToKph(10.0), 36.0);
	TestEqual(TEXT("36 km/h is 10 m/s"), UAFUnitsHelper::KphToMps(36.0), 10.0);

	// Unreal reports speed in cm/s. 1000 cm/s is 10 m/s is 36 km/h.
	TestEqual(TEXT("1000 cm/s is 36 km/h"), UAFUnitsHelper::UnrealSpeedToKph(1000.0), 36.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFUnitsLapTimeFormatTest,
	"ApexFormula.Core.Units.LapTimeFormat", AFTestFlags)

bool FAFUnitsLapTimeFormatTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Zero"), UAFUnitsHelper::FormatLapTime(0.0), FString(TEXT("0:00.000")));
	TestEqual(TEXT("Sub minute"), UAFUnitsHelper::FormatLapTime(9.5), FString(TEXT("0:09.500")));
	TestEqual(TEXT("Over a minute"), UAFUnitsHelper::FormatLapTime(83.456), FString(TEXT("1:23.456")));

	// The carry case. Naive truncation prints 0:12.1000 or 0:12.100 here.
	TestEqual(TEXT("Millisecond carry"), UAFUnitsHelper::FormatLapTime(12.9996), FString(TEXT("0:13.000")));

	// A negative time is a bug upstream, but it must not print garbage.
	const FString Negative = UAFUnitsHelper::FormatLapTime(-1.25);
	TestTrue(TEXT("Negative is signed"), Negative.StartsWith(TEXT("-")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
