// Copyright ApexFormula. Original work. Not affiliated with any real motorsport series.

#include "AFSectorTimer.h"
#include "Misc/AutomationTest.h"

/**
 * Sector timing tests.
 *
 * Reference: TECHNICAL_ARCHITECTURE.md section 9.
 *
 * UAFSectorTimer is pure by design: no tick, no world, no actor. That is
 * precisely what lets these tests drive a whole lap without a car, a track or
 * a frame. Every session time below is supplied explicitly, so the same call
 * sequence must always produce the same splits.
 *
 * Status: requires local compilation. These have never been executed.
 */

#if WITH_DEV_AUTOMATION_TESTS

static const EAutomationTestFlags AFSectorTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::CommandletContext |
	EAutomationTestFlags::ClientContext |
	EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFSectorTimerConfigureTest,
	"ApexFormula.Race.SectorTimer.Configure", AFSectorTestFlags)

bool FAFSectorTimerConfigureTest::RunTest(const FString& Parameters)
{
	UAFSectorTimer* Timer = NewObject<UAFSectorTimer>();
	TestNotNull(TEXT("Timer allocated"), Timer);
	if (!Timer)
	{
		return false;
	}

	TestEqual(TEXT("Unconfigured sector count is zero"), Timer->GetSectorCount(), 0);
	TestFalse(TEXT("Unconfigured lap is not open"), Timer->IsLapOpen());
	TestFalse(TEXT("Unconfigured lap is not complete"), Timer->IsLapComplete());

	// A circuit cannot have zero or negative sectors. Accepting one would make
	// IsLapComplete() true forever on an empty split list.
	AddExpectedError(TEXT("Configure rejected sector count"),
		EAutomationExpectedErrorFlags::Contains, 0);
	TestFalse(TEXT("Zero sectors rejected"), Timer->Configure(0));
	TestFalse(TEXT("Negative sectors rejected"), Timer->Configure(-3));
	TestEqual(TEXT("Rejected configure left the count alone"), Timer->GetSectorCount(), 0);

	// BeginLap before Configure must be refused rather than opening a lap that
	// can never close.
	AddExpectedError(TEXT("BeginLap called before Configure"),
		EAutomationExpectedErrorFlags::Contains, 0);
	Timer->BeginLap(0.0);
	TestFalse(TEXT("Lap did not open without configuration"), Timer->IsLapOpen());

	TestTrue(TEXT("Three sectors accepted"), Timer->Configure(3));
	TestEqual(TEXT("Sector count stored"), Timer->GetSectorCount(), 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFSectorTimerFullLapTest,
	"ApexFormula.Race.SectorTimer.FullLap", AFSectorTestFlags)

bool FAFSectorTimerFullLapTest::RunTest(const FString& Parameters)
{
	UAFSectorTimer* Timer = NewObject<UAFSectorTimer>();
	TestNotNull(TEXT("Timer allocated"), Timer);
	if (!Timer)
	{
		return false;
	}

	TestTrue(TEXT("Configured for three sectors"), Timer->Configure(3));

	// A representative ApexFormula lap: 28.400 + 31.250 + 25.100 = 84.750 s.
	// These are ApexFormula design values, not measurements of any real circuit.
	Timer->BeginLap(100.0);
	TestTrue(TEXT("Lap open after BeginLap"), Timer->IsLapOpen());
	TestFalse(TEXT("Lap not complete at the line"), Timer->IsLapComplete());
	TestEqual(TEXT("No splits yet"), Timer->GetSplits().Num(), 0);
	TestEqual(TEXT("Lap time is zero at the line"), Timer->GetLapTimeSeconds(), 0.0);

	TestTrue(TEXT("Sector 1 closed"), Timer->RecordSectorBoundary(128.400));
	TestEqual(TEXT("One split"), Timer->GetSplits().Num(), 1);
	TestTrue(TEXT("Lap still open after sector 1"), Timer->IsLapOpen());

	TestTrue(TEXT("Sector 2 closed"), Timer->RecordSectorBoundary(159.650));
	TestTrue(TEXT("Sector 3 closed"), Timer->RecordSectorBoundary(184.750));

	TestEqual(TEXT("Three splits"), Timer->GetSplits().Num(), 3);

	// The lap closes itself on the final sector. A new lap must require an
	// explicit BeginLap so a missed timing line can never be absorbed silently.
	TestFalse(TEXT("Lap auto-closed on the final sector"), Timer->IsLapOpen());
	TestTrue(TEXT("Lap reported complete"), Timer->IsLapComplete());

	const TArray<FAFSectorSplit>& Splits = Timer->GetSplits();
	if (Splits.Num() == 3)
	{
		TestEqual(TEXT("Split 0 index"), Splits[0].SectorIndex, 0);
		TestEqual(TEXT("Split 1 index"), Splits[1].SectorIndex, 1);
		TestEqual(TEXT("Split 2 index"), Splits[2].SectorIndex, 2);

		TestEqual(TEXT("Sector 1 duration"), Splits[0].DurationSeconds, 28.400, 1.0e-9);
		TestEqual(TEXT("Sector 2 duration"), Splits[1].DurationSeconds, 31.250, 1.0e-9);
		TestEqual(TEXT("Sector 3 duration"), Splits[2].DurationSeconds, 25.100, 1.0e-9);

		// Each sector must open exactly where the previous one closed. A gap
		// here would lose time that belongs to the lap.
		TestEqual(TEXT("Sector 2 opens where 1 closed"),
			Splits[1].EnterTime, Splits[0].ExitTime, 1.0e-9);
		TestEqual(TEXT("Sector 3 opens where 2 closed"),
			Splits[2].EnterTime, Splits[1].ExitTime, 1.0e-9);
	}

	TestEqual(TEXT("Lap time is the sum of the splits"),
		Timer->GetLapTimeSeconds(), 84.750, 1.0e-9);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFSectorTimerRejectionTest,
	"ApexFormula.Race.SectorTimer.Rejection", AFSectorTestFlags)

bool FAFSectorTimerRejectionTest::RunTest(const FString& Parameters)
{
	UAFSectorTimer* Timer = NewObject<UAFSectorTimer>();
	TestNotNull(TEXT("Timer allocated"), Timer);
	if (!Timer)
	{
		return false;
	}

	TestTrue(TEXT("Configured for two sectors"), Timer->Configure(2));

	// A boundary with no lap open is a caller bug, not a zero-length sector.
	AddExpectedError(TEXT("with no lap open"), EAutomationExpectedErrorFlags::Contains, 0);
	TestFalse(TEXT("Boundary refused with no lap open"), Timer->RecordSectorBoundary(10.0));
	TestEqual(TEXT("Nothing recorded"), Timer->GetSplits().Num(), 0);

	Timer->BeginLap(10.0);

	// Session time must move strictly forward. Equal or backwards time would
	// produce a zero or negative sector and corrupt every best-split compare.
	AddExpectedError(TEXT("rejected time"), EAutomationExpectedErrorFlags::Contains, 0);
	TestFalse(TEXT("Equal time rejected"), Timer->RecordSectorBoundary(10.0));
	TestFalse(TEXT("Backwards time rejected"), Timer->RecordSectorBoundary(9.5));
	TestEqual(TEXT("Still nothing recorded"), Timer->GetSplits().Num(), 0);
	TestTrue(TEXT("Lap survived the rejections"), Timer->IsLapOpen());

	TestTrue(TEXT("Forward time accepted"), Timer->RecordSectorBoundary(45.0));
	TestTrue(TEXT("Final sector accepted"), Timer->RecordSectorBoundary(90.0));
	TestFalse(TEXT("Lap closed"), Timer->IsLapOpen());

	// One boundary too many must not append a fourth split to a three-sector lap.
	AddExpectedError(TEXT("after all"), EAutomationExpectedErrorFlags::Contains, 0);
	TestFalse(TEXT("Extra boundary refused"), Timer->RecordSectorBoundary(120.0));
	TestEqual(TEXT("Split count unchanged"), Timer->GetSplits().Num(), 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFSectorTimerResetTest,
	"ApexFormula.Race.SectorTimer.Reset", AFSectorTestFlags)

bool FAFSectorTimerResetTest::RunTest(const FString& Parameters)
{
	UAFSectorTimer* Timer = NewObject<UAFSectorTimer>();
	TestNotNull(TEXT("Timer allocated"), Timer);
	if (!Timer)
	{
		return false;
	}

	TestTrue(TEXT("Configured for two sectors"), Timer->Configure(2));
	Timer->BeginLap(0.0);
	TestTrue(TEXT("Sector 1 closed"), Timer->RecordSectorBoundary(40.0));

	// ResetLap abandons the lap but must keep the circuit configuration, so a
	// session restart does not have to re-read the track definition.
	Timer->ResetLap();
	TestEqual(TEXT("Splits cleared"), Timer->GetSplits().Num(), 0);
	TestFalse(TEXT("Lap closed by reset"), Timer->IsLapOpen());
	TestFalse(TEXT("Lap not complete after reset"), Timer->IsLapComplete());
	TestEqual(TEXT("Sector count preserved"), Timer->GetSectorCount(), 2);
	TestEqual(TEXT("Lap time cleared"), Timer->GetLapTimeSeconds(), 0.0);

	// A second lap on the same timer must be clean, not appended to the first.
	Timer->BeginLap(200.0);
	TestTrue(TEXT("Second lap sector 1"), Timer->RecordSectorBoundary(230.0));
	TestTrue(TEXT("Second lap sector 2"), Timer->RecordSectorBoundary(265.0));
	TestEqual(TEXT("Second lap has exactly two splits"), Timer->GetSplits().Num(), 2);
	TestEqual(TEXT("Second lap time"), Timer->GetLapTimeSeconds(), 65.0, 1.0e-9);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
