// Copyright ApexFormula. Original work. Not affiliated with any real motorsport series.

#include "AFSectorTimer.h"
#include "AFLog.h"

bool UAFSectorTimer::Configure(const int32 InSectorCount)
{
	if (InSectorCount < 1)
	{
		UE_LOG(LogAFRace, Warning,
			TEXT("UAFSectorTimer::Configure rejected sector count %d; must be >= 1."),
			InSectorCount);
		return false;
	}

	SectorCount = InSectorCount;
	ResetLap();
	return true;
}

void UAFSectorTimer::BeginLap(const double SessionTime)
{
	if (SectorCount < 1)
	{
		UE_LOG(LogAFRace, Warning,
			TEXT("UAFSectorTimer::BeginLap called before Configure; ignoring."));
		return;
	}

	Splits.Reset();
	Splits.Reserve(SectorCount);
	CurrentSectorEnterTime = SessionTime;
	bLapOpen = true;
}

bool UAFSectorTimer::RecordSectorBoundary(const double SessionTime)
{
	if (SectorCount > 0 && Splits.Num() >= SectorCount)
	{
		UE_LOG(LogAFRace, Warning,
			TEXT("UAFSectorTimer::RecordSectorBoundary called after all %d sectors were closed; ignoring."),
			SectorCount);
		return false;
	}

	if (!bLapOpen)
	{
		UE_LOG(LogAFRace, Warning,
			TEXT("UAFSectorTimer::RecordSectorBoundary called with no lap open; ignoring."));
		return false;
	}

	// Session time must move forward. A non-advancing time would produce a zero
	// or negative sector, which is never a real result and would corrupt any
	// best-split comparison downstream.
	if (!(SessionTime > CurrentSectorEnterTime))
	{
		UE_LOG(LogAFRace, Warning,
			TEXT("UAFSectorTimer::RecordSectorBoundary rejected time %f; must be strictly greater than sector enter time %f."),
			SessionTime, CurrentSectorEnterTime);
		return false;
	}

	FAFSectorSplit Split;
	Split.SectorIndex = Splits.Num();
	Split.EnterTime = CurrentSectorEnterTime;
	Split.ExitTime = SessionTime;
	Split.DurationSeconds = SessionTime - CurrentSectorEnterTime;

	Splits.Add(Split);
	CurrentSectorEnterTime = SessionTime;

	if (Splits.Num() >= SectorCount)
	{
		// The lap is closed. A new lap requires an explicit BeginLap so that a
		// missed timing line can never be silently absorbed into the next lap.
		bLapOpen = false;
	}

	return true;
}

bool UAFSectorTimer::IsLapComplete() const
{
	return SectorCount > 0 && Splits.Num() >= SectorCount;
}

double UAFSectorTimer::GetLapTimeSeconds() const
{
	double Total = 0.0;
	for (const FAFSectorSplit& Split : Splits)
	{
		Total += Split.DurationSeconds;
	}
	return Total;
}

void UAFSectorTimer::ResetLap()
{
	Splits.Reset();
	bLapOpen = false;
	CurrentSectorEnterTime = 0.0;
}
