// Copyright ApexFormula. Original work. Not affiliated with any real motorsport series.

#include "AFLapValidator.h"
#include "AFTypes.h"
#include "Misc/AutomationTest.h"

/**
 * Lap legality tests.
 *
 * Reference: TECHNICAL_ARCHITECTURE.md section 9.
 *
 * UAFLapValidator is pure: it holds no world, never ticks, and takes every
 * input explicitly. These tests therefore drive a complete lap, a cut lap and
 * an abandoned lap without a car, a track or a frame.
 *
 * Checkpoint names below are ApexFormula test fixtures. They are not the names
 * of any real circuit's marshalling posts or corners.
 *
 * Status: requires local compilation. These have never been executed.
 */

#if WITH_DEV_AUTOMATION_TESTS

static const EAutomationTestFlags AFLapTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::CommandletContext |
	EAutomationTestFlags::ClientContext |
	EAutomationTestFlags::ProductFilter;

/** Four-checkpoint fixture circuit. Index 0 is always the timing line. */
static TArray<FName> AFMakeTestCheckpointOrder()
{
	TArray<FName> Order;
	Order.Add(TEXT("CP_TimingLine"));
	Order.Add(TEXT("CP_Alpha"));
	Order.Add(TEXT("CP_Bravo"));
	Order.Add(TEXT("CP_Charlie"));
	return Order;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFLapValidatorConfigureTest,
	"ApexFormula.Race.LapValidator.Configure", AFLapTestFlags)

bool FAFLapValidatorConfigureTest::RunTest(const FString& Parameters)
{
	UAFLapValidator* Validator = NewObject<UAFLapValidator>();
	TestNotNull(TEXT("Validator allocated"), Validator);
	if (!Validator)
	{
		return false;
	}

	// A circuit needs a timing line plus at least one other checkpoint,
	// otherwise "passed every checkpoint in order" is vacuously true.
	AddExpectedError(TEXT("rejected an order with"),
		EAutomationExpectedErrorFlags::Contains, 0);
	TArray<FName> TooShort;
	TooShort.Add(TEXT("CP_TimingLine"));
	TestFalse(TEXT("One checkpoint rejected"), Validator->Configure(TooShort));
	TestFalse(TEXT("Empty order rejected"), Validator->Configure(TArray<FName>()));

	// An unset name can never be matched against a real crossing report.
	AddExpectedError(TEXT("is unset"), EAutomationExpectedErrorFlags::Contains, 0);
	TArray<FName> WithNone;
	WithNone.Add(TEXT("CP_TimingLine"));
	WithNone.Add(NAME_None);
	WithNone.Add(TEXT("CP_Bravo"));
	TestFalse(TEXT("Unset entry rejected"), Validator->Configure(WithNone));

	// A duplicate makes the expected-order cursor ambiguous.
	AddExpectedError(TEXT("appears more than once"),
		EAutomationExpectedErrorFlags::Contains, 0);
	TArray<FName> WithDuplicate;
	WithDuplicate.Add(TEXT("CP_TimingLine"));
	WithDuplicate.Add(TEXT("CP_Alpha"));
	WithDuplicate.Add(TEXT("CP_Alpha"));
	TestFalse(TEXT("Duplicate rejected"), Validator->Configure(WithDuplicate));

	TestEqual(TEXT("No order stored after rejections"),
		Validator->GetExpectedCheckpointOrder().Num(), 0);

	// BeginLap before a valid configuration must not open a lap.
	AddExpectedError(TEXT("BeginLap called before Configure"),
		EAutomationExpectedErrorFlags::Contains, 0);
	Validator->BeginLap(0, 0.0);
	TestFalse(TEXT("Lap did not open"), Validator->IsLapOpen());

	TestTrue(TEXT("Valid order accepted"), Validator->Configure(AFMakeTestCheckpointOrder()));
	TestEqual(TEXT("Order stored"), Validator->GetExpectedCheckpointOrder().Num(), 4);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFLapValidatorCleanLapTest,
	"ApexFormula.Race.LapValidator.CleanLap", AFLapTestFlags)

bool FAFLapValidatorCleanLapTest::RunTest(const FString& Parameters)
{
	UAFLapValidator* Validator = NewObject<UAFLapValidator>();
	TestNotNull(TEXT("Validator allocated"), Validator);
	if (!Validator)
	{
		return false;
	}

	TestTrue(TEXT("Configured"), Validator->Configure(AFMakeTestCheckpointOrder()));

	Validator->BeginLap(3, 500.0);
	TestTrue(TEXT("Lap open"), Validator->IsLapOpen());

	// BeginLap consumes the timing line, so nothing has been passed yet.
	TestEqual(TEXT("No checkpoints passed at the line"),
		Validator->GetPassedCheckpointCount(), 0);

	TestTrue(TEXT("Alpha in order"), Validator->NotifyCheckpointPassed(TEXT("CP_Alpha"), 520.0));
	TestEqual(TEXT("One passed"), Validator->GetPassedCheckpointCount(), 1);

	TestTrue(TEXT("Bravo in order"), Validator->NotifyCheckpointPassed(TEXT("CP_Bravo"), 545.0));
	TestTrue(TEXT("Charlie in order"), Validator->NotifyCheckpointPassed(TEXT("CP_Charlie"), 570.0));
	TestEqual(TEXT("Three passed"), Validator->GetPassedCheckpointCount(), 3);

	TestEqual(TEXT("Still not invalidated"),
		Validator->GetCurrentInvalidationReason(),
		EAFLapInvalidationReason::NotInvalidated);

	bool bHasResult = false;
	const FAFLapResult Result = Validator->CompleteLap(584.750, bHasResult);

	TestTrue(TEXT("Result produced"), bHasResult);
	TestTrue(TEXT("Lap is valid"), Result.bValid);
	TestEqual(TEXT("Reason is NotInvalidated"),
		Result.InvalidationReason, EAFLapInvalidationReason::NotInvalidated);
	TestEqual(TEXT("Lap index carried through"), Result.LapIndex, 3);
	TestEqual(TEXT("Start time"), Result.StartTime, 500.0, 1.0e-9);
	TestEqual(TEXT("End time"), Result.EndTime, 584.750, 1.0e-9);
	TestEqual(TEXT("Lap time"), Result.LapTimeSeconds, 84.750, 1.0e-9);

	TestFalse(TEXT("Lap closed after completion"), Validator->IsLapOpen());

	// A second CompleteLap has no lap to close and must report that rather
	// than emitting a duplicate result.
	bool bSecondResult = true;
	Validator->CompleteLap(600.0, bSecondResult);
	TestFalse(TEXT("No second result"), bSecondResult);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFLapValidatorOutOfOrderTest,
	"ApexFormula.Race.LapValidator.OutOfOrder", AFLapTestFlags)

bool FAFLapValidatorOutOfOrderTest::RunTest(const FString& Parameters)
{
	UAFLapValidator* Validator = NewObject<UAFLapValidator>();
	TestNotNull(TEXT("Validator allocated"), Validator);
	if (!Validator)
	{
		return false;
	}

	TestTrue(TEXT("Configured"), Validator->Configure(AFMakeTestCheckpointOrder()));
	Validator->BeginLap(0, 0.0);

	TestTrue(TEXT("Alpha in order"), Validator->NotifyCheckpointPassed(TEXT("CP_Alpha"), 20.0));

	// Skipping Bravo is a cut. The lap is marked invalid, but the driver still
	// finishes it, so the lap must stay open and keep timing.
	TestFalse(TEXT("Charlie out of order refused"),
		Validator->NotifyCheckpointPassed(TEXT("CP_Charlie"), 45.0));
	TestTrue(TEXT("Lap is still open after a cut"), Validator->IsLapOpen());
	TestEqual(TEXT("Cursor did not advance"), Validator->GetPassedCheckpointCount(), 1);
	TestEqual(TEXT("Marked MissedCheckpoint"),
		Validator->GetCurrentInvalidationReason(),
		EAFLapInvalidationReason::MissedCheckpoint);

	// The driver rejoins and completes the remainder correctly.
	TestTrue(TEXT("Bravo accepted after the cut"),
		Validator->NotifyCheckpointPassed(TEXT("CP_Bravo"), 50.0));
	TestTrue(TEXT("Charlie accepted"),
		Validator->NotifyCheckpointPassed(TEXT("CP_Charlie"), 70.0));

	bool bHasResult = false;
	const FAFLapResult Result = Validator->CompleteLap(90.0, bHasResult);

	TestTrue(TEXT("Result produced"), bHasResult);
	// Every checkpoint was eventually consumed and the time is positive, yet the
	// lap must still be invalid because of the earlier cut.
	TestFalse(TEXT("Cut lap is invalid"), Result.bValid);
	TestEqual(TEXT("Reason retained"),
		Result.InvalidationReason, EAFLapInvalidationReason::MissedCheckpoint);
	TestEqual(TEXT("Invalid lap still has a recorded time"),
		Result.LapTimeSeconds, 90.0, 1.0e-9);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFLapValidatorUnknownCheckpointTest,
	"ApexFormula.Race.LapValidator.UnknownCheckpoint", AFLapTestFlags)

bool FAFLapValidatorUnknownCheckpointTest::RunTest(const FString& Parameters)
{
	UAFLapValidator* Validator = NewObject<UAFLapValidator>();
	TestNotNull(TEXT("Validator allocated"), Validator);
	if (!Validator)
	{
		return false;
	}

	TestTrue(TEXT("Configured"), Validator->Configure(AFMakeTestCheckpointOrder()));

	// A crossing report with no lap open is ignored quietly; it must not
	// invalidate a lap that does not exist.
	TestFalse(TEXT("Ignored with no lap open"),
		Validator->NotifyCheckpointPassed(TEXT("CP_Alpha"), 1.0));
	TestEqual(TEXT("Nothing invalidated"),
		Validator->GetCurrentInvalidationReason(),
		EAFLapInvalidationReason::NotInvalidated);

	Validator->BeginLap(0, 0.0);

	// A checkpoint that is not on this circuit means the level and the track
	// definition disagree. That is a content error and must be loud.
	AddExpectedError(TEXT("is not part of the configured circuit"),
		EAutomationExpectedErrorFlags::Contains, 0);
	TestFalse(TEXT("Unknown checkpoint refused"),
		Validator->NotifyCheckpointPassed(TEXT("CP_NotOnThisCircuit"), 10.0));
	TestEqual(TEXT("Marked MissedCheckpoint"),
		Validator->GetCurrentInvalidationReason(),
		EAFLapInvalidationReason::MissedCheckpoint);
	TestEqual(TEXT("Cursor untouched"), Validator->GetPassedCheckpointCount(), 0);

	// The timing line itself is a known name but is never the expected next
	// checkpoint; it arrives through CompleteLap instead.
	TestFalse(TEXT("Timing line refused mid-lap"),
		Validator->NotifyCheckpointPassed(TEXT("CP_TimingLine"), 12.0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFLapValidatorFirstCauseTest,
	"ApexFormula.Race.LapValidator.FirstCauseWins", AFLapTestFlags)

bool FAFLapValidatorFirstCauseTest::RunTest(const FString& Parameters)
{
	UAFLapValidator* Validator = NewObject<UAFLapValidator>();
	TestNotNull(TEXT("Validator allocated"), Validator);
	if (!Validator)
	{
		return false;
	}

	TestTrue(TEXT("Configured"), Validator->Configure(AFMakeTestCheckpointOrder()));
	Validator->BeginLap(1, 0.0);

	// A track-limit excursion that causes a spin and then a collision must be
	// reported as the excursion, which is the cause, not the collision, which
	// is the consequence.
	Validator->InvalidateLap(EAFLapInvalidationReason::TrackLimits);
	TestEqual(TEXT("First cause recorded"),
		Validator->GetCurrentInvalidationReason(),
		EAFLapInvalidationReason::TrackLimits);

	Validator->InvalidateLap(EAFLapInvalidationReason::Collision);
	Validator->InvalidateLap(EAFLapInvalidationReason::VehicleReset);
	TestEqual(TEXT("Later causes did not overwrite"),
		Validator->GetCurrentInvalidationReason(),
		EAFLapInvalidationReason::TrackLimits);

	// NotInvalidated must never be usable to launder an invalid lap clean.
	Validator->InvalidateLap(EAFLapInvalidationReason::NotInvalidated);
	TestEqual(TEXT("Invalidation cannot be cleared through InvalidateLap"),
		Validator->GetCurrentInvalidationReason(),
		EAFLapInvalidationReason::TrackLimits);

	TestTrue(TEXT("Alpha"), Validator->NotifyCheckpointPassed(TEXT("CP_Alpha"), 20.0));
	TestTrue(TEXT("Bravo"), Validator->NotifyCheckpointPassed(TEXT("CP_Bravo"), 40.0));
	TestTrue(TEXT("Charlie"), Validator->NotifyCheckpointPassed(TEXT("CP_Charlie"), 60.0));

	bool bHasResult = false;
	const FAFLapResult Result = Validator->CompleteLap(85.0, bHasResult);
	TestTrue(TEXT("Result produced"), bHasResult);
	TestFalse(TEXT("Lap invalid"), Result.bValid);
	TestEqual(TEXT("TrackLimits survived to the result"),
		Result.InvalidationReason, EAFLapInvalidationReason::TrackLimits);

	// ResetLap must clear the reason so the next lap starts clean.
	Validator->ResetLap();
	TestEqual(TEXT("Reason cleared by reset"),
		Validator->GetCurrentInvalidationReason(),
		EAFLapInvalidationReason::NotInvalidated);
	TestEqual(TEXT("Cursor reset"), Validator->GetPassedCheckpointCount(), 0);
	TestEqual(TEXT("Order preserved by reset"),
		Validator->GetExpectedCheckpointOrder().Num(), 4);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFLapValidatorIncompleteLapTest,
	"ApexFormula.Race.LapValidator.IncompleteLap", AFLapTestFlags)

bool FAFLapValidatorIncompleteLapTest::RunTest(const FString& Parameters)
{
	UAFLapValidator* Validator = NewObject<UAFLapValidator>();
	TestNotNull(TEXT("Validator allocated"), Validator);
	if (!Validator)
	{
		return false;
	}

	TestTrue(TEXT("Configured"), Validator->Configure(AFMakeTestCheckpointOrder()));

	// Reaching the line having skipped the second half of the circuit is the
	// classic short-cut. It must never produce a valid lap.
	Validator->BeginLap(0, 0.0);
	TestTrue(TEXT("Alpha"), Validator->NotifyCheckpointPassed(TEXT("CP_Alpha"), 15.0));

	bool bHasResult = false;
	const FAFLapResult ShortCut = Validator->CompleteLap(30.0, bHasResult);
	TestTrue(TEXT("Result produced"), bHasResult);
	TestFalse(TEXT("Short-cut lap is invalid"), ShortCut.bValid);
	TestEqual(TEXT("Reason is MissedCheckpoint"),
		ShortCut.InvalidationReason, EAFLapInvalidationReason::MissedCheckpoint);

	// A lap that ends at or before it started is a timing fault, never a
	// zero-second record.
	Validator->BeginLap(1, 100.0);
	TestTrue(TEXT("Alpha"), Validator->NotifyCheckpointPassed(TEXT("CP_Alpha"), 110.0));
	TestTrue(TEXT("Bravo"), Validator->NotifyCheckpointPassed(TEXT("CP_Bravo"), 120.0));
	TestTrue(TEXT("Charlie"), Validator->NotifyCheckpointPassed(TEXT("CP_Charlie"), 130.0));

	AddExpectedError(TEXT("which is not after the lap start"),
		EAutomationExpectedErrorFlags::Contains, 0);
	bool bBackwardsResult = false;
	const FAFLapResult Backwards = Validator->CompleteLap(100.0, bBackwardsResult);
	TestTrue(TEXT("Result produced"), bBackwardsResult);
	TestFalse(TEXT("Non-advancing lap is invalid"), Backwards.bValid);
	TestEqual(TEXT("Lap time clamped to zero"),
		Backwards.LapTimeSeconds, 0.0, 1.0e-9);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
