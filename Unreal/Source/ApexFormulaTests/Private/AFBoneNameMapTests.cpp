// Copyright ApexFormula. Original work. Not affiliated with any real motorsport series.

#include "AFBoneNameMap.h"
#include "Misc/AutomationTest.h"

/**
 * Bone convention tests.
 *
 * Reference: DECISION_LOG.md D-012, the eleven-bone convention.
 * Reference: BlenderPipeline/scripts/af_pipeline_config.py lines 155 to 206.
 *
 * The single most error-prone fact in this project is that the skeleton order
 * and the deform-bone order are DIFFERENT. BONE_ORDER is hierarchy-interleaved
 * (each suspension immediately precedes the wheel it parents). DEFORM_BONES is
 * grouped (chassis, then all suspensions, then all wheels) and excludes BOTH
 * AF_Root and AF_Steering. Each is asserted independently below.
 *
 * Status: requires local compilation. These have never been executed.
 * The same two orderings are additionally checked against the Python config
 * itself by Tools/af_static_validate.py, which HAS been executed.
 */

#if WITH_DEV_AUTOMATION_TESTS

static const EAutomationTestFlags AFBoneTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::CommandletContext |
	EAutomationTestFlags::ClientContext |
	EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFBoneOrderTest,
	"ApexFormula.Core.Bones.SkeletonOrder", AFBoneTestFlags)

bool FAFBoneOrderTest::RunTest(const FString& Parameters)
{
	UAFBoneNameMap* Map = NewObject<UAFBoneNameMap>();
	TestNotNull(TEXT("Map allocated"), Map);
	if (!Map)
	{
		return false;
	}

	const TArray<FName> Expected = {
		TEXT("AF_Root"),
		TEXT("AF_Chassis"),
		TEXT("AF_Steering"),
		TEXT("AF_Suspension_FL"),
		TEXT("AF_Wheel_FL"),
		TEXT("AF_Suspension_FR"),
		TEXT("AF_Wheel_FR"),
		TEXT("AF_Suspension_RL"),
		TEXT("AF_Wheel_RL"),
		TEXT("AF_Suspension_RR"),
		TEXT("AF_Wheel_RR")
	};

	const TArray<FName> Actual = Map->GetAllBoneNamesInOrder();

	TestEqual(TEXT("Eleven bones"), Actual.Num(), 11);
	TestEqual(TEXT("Expected list is eleven"), Expected.Num(), 11);

	const int32 Count = FMath::Min(Expected.Num(), Actual.Num());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		TestEqual(
			FString::Printf(TEXT("Bone %d"), Index),
			Actual[Index].ToString(),
			Expected[Index].ToString());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFDeformBoneTest,
	"ApexFormula.Core.Bones.DeformOrder", AFBoneTestFlags)

bool FAFDeformBoneTest::RunTest(const FString& Parameters)
{
	UAFBoneNameMap* Map = NewObject<UAFBoneNameMap>();
	TestNotNull(TEXT("Map allocated"), Map);
	if (!Map)
	{
		return false;
	}

	const TArray<FName> Expected = {
		TEXT("AF_Chassis"),
		TEXT("AF_Suspension_FL"),
		TEXT("AF_Suspension_FR"),
		TEXT("AF_Suspension_RL"),
		TEXT("AF_Suspension_RR"),
		TEXT("AF_Wheel_FL"),
		TEXT("AF_Wheel_FR"),
		TEXT("AF_Wheel_RL"),
		TEXT("AF_Wheel_RR")
	};

	const TArray<FName> Actual = Map->GetDeformBoneNames();

	TestEqual(TEXT("Nine deform bones"), Actual.Num(), 9);

	const int32 Count = FMath::Min(Expected.Num(), Actual.Num());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		TestEqual(
			FString::Printf(TEXT("Deform bone %d"), Index),
			Actual[Index].ToString(),
			Expected[Index].ToString());
	}

	// Explicit exclusions. Both are control bones and carry no weights.
	TestFalse(TEXT("Root is not a deform bone"), Actual.Contains(FName(TEXT("AF_Root"))));
	TestFalse(TEXT("Steering is not a deform bone"), Actual.Contains(FName(TEXT("AF_Steering"))));

	// The two orderings must genuinely differ, not merely be subsets.
	const TArray<FName> All = Map->GetAllBoneNamesInOrder();
	TestNotEqual(TEXT("Orderings differ in length"), Actual.Num(), All.Num());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFBoneParentTest,
	"ApexFormula.Core.Bones.Parents", AFBoneTestFlags)

bool FAFBoneParentTest::RunTest(const FString& Parameters)
{
	UAFBoneNameMap* Map = NewObject<UAFBoneNameMap>();
	TestNotNull(TEXT("Map allocated"), Map);
	if (!Map)
	{
		return false;
	}

	TestEqual(TEXT("Root has no parent"),
		Map->GetParentBone(TEXT("AF_Root")), FName(NAME_None));
	TestEqual(TEXT("Chassis parents to root"),
		Map->GetParentBone(TEXT("AF_Chassis")), FName(TEXT("AF_Root")));
	TestEqual(TEXT("Steering parents to chassis"),
		Map->GetParentBone(TEXT("AF_Steering")), FName(TEXT("AF_Chassis")));

	// Every suspension hangs off the chassis; every wheel hangs off its own
	// suspension, never off the chassis directly.
	const TArray<FString> Corners = { TEXT("FL"), TEXT("FR"), TEXT("RL"), TEXT("RR") };
	for (const FString& Corner : Corners)
	{
		const FName Suspension = *FString::Printf(TEXT("AF_Suspension_%s"), *Corner);
		const FName Wheel = *FString::Printf(TEXT("AF_Wheel_%s"), *Corner);

		TestEqual(FString::Printf(TEXT("Suspension %s parents to chassis"), *Corner),
			Map->GetParentBone(Suspension), FName(TEXT("AF_Chassis")));
		TestEqual(FString::Printf(TEXT("Wheel %s parents to its suspension"), *Corner),
			Map->GetParentBone(Wheel), Suspension);
	}

	// An unknown name must not silently resolve to the root.
	TestEqual(TEXT("Unknown bone has no parent"),
		Map->GetParentBone(TEXT("AF_NotABone")), FName(NAME_None));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAFBoneValidateSelfTest,
	"ApexFormula.Core.Bones.ValidateSelf", AFBoneTestFlags)

bool FAFBoneValidateSelfTest::RunTest(const FString& Parameters)
{
	UAFBoneNameMap* Map = NewObject<UAFBoneNameMap>();
	TestNotNull(TEXT("Map allocated"), Map);
	if (!Map)
	{
		return false;
	}

	// A freshly constructed map is the canonical convention and must be clean.
	const TArray<FString> Problems = Map->ValidateSelf();
	TestEqual(TEXT("Default map validates clean"), Problems.Num(), 0);

	// Breaking a name must be caught, not tolerated.
	Map->WheelBones[EAFCorner::FL] = NAME_None;
	TestTrue(TEXT("Unset wheel bone is reported"), Map->ValidateSelf().Num() > 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
