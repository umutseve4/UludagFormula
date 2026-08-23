// Copyright ApexFormula. Original work. Not affiliated with any real motorsport series.

#include "AFTelemetryBus.h"
#include "AFTelemetryTypes.h"
#include "Misc/AutomationTest.h"

/**
 * Telemetry bus delivery tests.
 *
 * Reference: TECHNICAL_ARCHITECTURE.md section 10.
 *
 * The bus is the single hub. If per-channel and all-channel delivery ever
 * diverge, or if unsubscribe silently fails, the HUD becomes a second source of
 * truth for race state. These tests exist to make that failure loud.
 *
 * Status: requires local compilation. These have never been executed.
 */

#if WITH_DEV_AUTOMATION_TESTS

static const EAutomationTestFlags AFBusTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::CommandletContext |
	EAutomationTestFlags::ClientContext |
	EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFTelemetryChannelDeliveryTest,
	"ApexFormula.Core.Telemetry.ChannelDelivery", AFBusTestFlags)

bool FAFTelemetryChannelDeliveryTest::RunTest(const FString& Parameters)
{
	UAFTelemetryBus* Bus = NewObject<UAFTelemetryBus>();
	TestNotNull(TEXT("Bus allocated"), Bus);
	if (!Bus)
	{
		return false;
	}

	int32 SpeedHits = 0;
	double LastSpeed = 0.0;

	const FDelegateHandle SpeedHandle = Bus->SubscribeToChannel(
		AFTelemetryChannels::VehicleSpeedKph,
		FAFOnTelemetrySample::FDelegate::CreateLambda(
			[&SpeedHits, &LastSpeed](const FAFTelemetrySample& Sample)
			{
				++SpeedHits;
				LastSpeed = Sample.Value;
			}));

	TestEqual(TEXT("One channel subscribed"), Bus->GetSubscribedChannelCount(), 1);

	Bus->PublishValue(AFTelemetryChannels::VehicleSpeedKph, 1.0, 0, 275.5);
	TestEqual(TEXT("Speed subscriber fired once"), SpeedHits, 1);
	TestEqual(TEXT("Speed value delivered"), LastSpeed, 275.5);

	// A different channel must not reach this subscriber.
	Bus->PublishValue(AFTelemetryChannels::VehicleThrottle, 2.0, 0, 1.0);
	TestEqual(TEXT("Other channel did not fire it"), SpeedHits, 1);

	TestEqual(TEXT("Two samples published"), Bus->GetPublishedSampleCount(), (int64)2);

	// After unsubscribing, the same publish must be silent.
	Bus->UnsubscribeFromChannel(AFTelemetryChannels::VehicleSpeedKph, SpeedHandle);
	Bus->PublishValue(AFTelemetryChannels::VehicleSpeedKph, 3.0, 0, 300.0);
	TestEqual(TEXT("Unsubscribed subscriber is silent"), SpeedHits, 1);
	TestEqual(TEXT("Empty channel entry is removed"), Bus->GetSubscribedChannelCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFTelemetryAllChannelTest,
	"ApexFormula.Core.Telemetry.AllChannelDelivery", AFBusTestFlags)

bool FAFTelemetryAllChannelTest::RunTest(const FString& Parameters)
{
	UAFTelemetryBus* Bus = NewObject<UAFTelemetryBus>();
	TestNotNull(TEXT("Bus allocated"), Bus);
	if (!Bus)
	{
		return false;
	}

	int32 AllHits = 0;
	TArray<FName> SeenChannels;

	const FDelegateHandle AllHandle = Bus->SubscribeToAll(
		FAFOnTelemetrySample::FDelegate::CreateLambda(
			[&AllHits, &SeenChannels](const FAFTelemetrySample& Sample)
			{
				++AllHits;
				SeenChannels.Add(Sample.Channel);
			}));

	// An all-channel subscriber is not a channel subscriber.
	TestEqual(TEXT("No per-channel entries"), Bus->GetSubscribedChannelCount(), 0);

	Bus->PublishValue(AFTelemetryChannels::VehicleSpeedKph, 1.0, 0, 100.0);
	Bus->PublishValue(AFTelemetryChannels::RacePosition, 1.0, 0, 3.0);
	Bus->PublishValue(AFTelemetryChannels::FuelMassKg, 1.0, 0, 42.0);

	TestEqual(TEXT("All three delivered"), AllHits, 3);
	TestTrue(TEXT("Saw speed"), SeenChannels.Contains(AFTelemetryChannels::VehicleSpeedKph));
	TestTrue(TEXT("Saw position"), SeenChannels.Contains(AFTelemetryChannels::RacePosition));
	TestTrue(TEXT("Saw fuel"), SeenChannels.Contains(AFTelemetryChannels::FuelMassKg));

	Bus->UnsubscribeFromAll(AllHandle);
	Bus->PublishValue(AFTelemetryChannels::VehicleSpeedKph, 2.0, 0, 110.0);
	TestEqual(TEXT("Unsubscribed all-channel is silent"), AllHits, 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFTelemetryRejectionTest,
	"ApexFormula.Core.Telemetry.Rejection", AFBusTestFlags)

bool FAFTelemetryRejectionTest::RunTest(const FString& Parameters)
{
	UAFTelemetryBus* Bus = NewObject<UAFTelemetryBus>();
	TestNotNull(TEXT("Bus allocated"), Bus);
	if (!Bus)
	{
		return false;
	}

	int32 Hits = 0;
	Bus->SubscribeToAll(
		FAFOnTelemetrySample::FDelegate::CreateLambda(
			[&Hits](const FAFTelemetrySample&) { ++Hits; }));

	// A sample with no channel name is a producer bug. It must be dropped, not
	// broadcast into an unnamed bucket that silently accumulates.
	AddExpectedError(TEXT("channel"), EAutomationExpectedErrorFlags::Contains, 0);
	Bus->PublishValue(NAME_None, 1.0, 0, 1.0);

	TestEqual(TEXT("Unnamed channel is dropped"), Hits, 0);
	TestEqual(TEXT("Dropped sample is not counted"), Bus->GetPublishedSampleCount(), (int64)0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFTelemetryResetTest,
	"ApexFormula.Core.Telemetry.Reset", AFBusTestFlags)

bool FAFTelemetryResetTest::RunTest(const FString& Parameters)
{
	UAFTelemetryBus* Bus = NewObject<UAFTelemetryBus>();
	TestNotNull(TEXT("Bus allocated"), Bus);
	if (!Bus)
	{
		return false;
	}

	int32 Hits = 0;
	Bus->SubscribeToChannel(
		AFTelemetryChannels::VehicleGear,
		FAFOnTelemetrySample::FDelegate::CreateLambda(
			[&Hits](const FAFTelemetrySample&) { ++Hits; }));

	Bus->PublishValue(AFTelemetryChannels::VehicleGear, 1.0, 0, 4.0);
	TestEqual(TEXT("Delivered before reset"), Hits, 1);

	// Session teardown must leave no stale subscriber holding a destroyed object.
	Bus->Reset();
	TestEqual(TEXT("No channels after reset"), Bus->GetSubscribedChannelCount(), 0);
	TestEqual(TEXT("Counter cleared"), Bus->GetPublishedSampleCount(), (int64)0);

	Bus->PublishValue(AFTelemetryChannels::VehicleGear, 2.0, 0, 5.0);
	TestEqual(TEXT("Old subscriber is gone"), Hits, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFTelemetryChannelRegistryTest,
	"ApexFormula.Core.Telemetry.ChannelRegistry", AFBusTestFlags)

bool FAFTelemetryChannelRegistryTest::RunTest(const FString& Parameters)
{
	const TArray<FName>& All = AFTelemetryChannels::GetAllChannels();

	TestEqual(TEXT("Ten declared channels"), All.Num(), 10);

	// No channel may be unnamed, and none may repeat. A duplicate here would
	// mean two producers quietly overwriting each other on the HUD.
	TSet<FName> Seen;
	for (const FName& Channel : All)
	{
		TestFalse(TEXT("Channel is named"), Channel.IsNone());

		bool bAlready = false;
		Seen.Add(Channel, &bAlready);
		TestFalse(
			FString::Printf(TEXT("Channel %s is unique"), *Channel.ToString()),
			bAlready);
	}

	// Every channel string is namespaced. This is what keeps the registry
	// readable when it grows past ten entries.
	for (const FName& Channel : All)
	{
		TestTrue(
			FString::Printf(TEXT("Channel %s is namespaced"), *Channel.ToString()),
			Channel.ToString().Contains(TEXT(".")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
