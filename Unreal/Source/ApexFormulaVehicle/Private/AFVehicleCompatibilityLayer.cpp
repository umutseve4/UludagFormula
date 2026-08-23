// Copyright ApexFormula. Original work. Not affiliated with any real motorsport series.

#include "AFVehicleCompatibilityLayer.h"
#include "AFLog.h"
#include "AFUnits.h"

#include "GameFramework/Pawn.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PrimitiveComponent.h"

// The ONLY engine vehicle includes in the project. D-008.
// ASSUMPTION REQUIRING VERIFICATION: these header paths and the module that
// provides them ("ChaosVehicles") are recorded in VERSION_MATRIX.md section
// 5.21 as unverified for Unreal Engine 5.8. Nothing in this environment can
// compile C++, so the include paths below have never been resolved.
#include "ChaosWheeledVehicleMovementComponent.h"
#include "ChaosVehicleWheel.h"

namespace
{
	/**
	 * Converts a metric length to engine centimetres.
	 * Every metre-to-centimetre conversion in the vehicle stack goes through
	 * UAFUnitsHelper so that D-013 has exactly one implementation.
	 */
	double ToCm(const double Metres)
	{
		return UAFUnitsHelper::MetresToCm(Metres);
	}

	/** Converts a metric offset to engine centimetres, component-wise. */
	FVector ToCm(const FVector& Metres)
	{
		return FVector(ToCm(Metres.X), ToCm(Metres.Y), ToCm(Metres.Z));
	}
}

UAFVehicleCompatibilityLayer::UAFVehicleCompatibilityLayer()
{
	// Milestone 2 compiles against a real engine vehicle backend, so the
	// backend is reported as available. "Available" means the code path
	// exists and is bound; it does NOT mean it has been observed to drive a
	// car, because no engine is present in the authoring environment.
	BackendInfo.BackendId = FName(TEXT("chaos"));
	BackendInfo.bBackendAvailable = true;
	BackendInfo.StatusMessage = TEXT(
		"Chaos vehicle backend bound at compile time. Backend behaviour is "
		"unverified: requires local compilation, requires Unreal Editor "
		"verification and requires playtesting before any handling claim.");
}

FAFVehicleBackendInfo UAFVehicleCompatibilityLayer::GetBackendInfo() const
{
	return BackendInfo;
}

bool UAFVehicleCompatibilityLayer::IsBackendAvailable() const
{
	return BackendInfo.bBackendAvailable;
}

UActorComponent* UAFVehicleCompatibilityLayer::CreateBackendMovement(
	APawn* OwnerPawn,
	USkeletalMeshComponent* MeshComponent)
{
	if (BackendMovement != nullptr)
	{
		UE_LOG(LogAFVehicle, Error,
			TEXT("CreateBackendMovement called twice; returning the existing component. ")
			TEXT("The backend movement component is created once per pawn."));
		return BackendMovement;
	}

	if (OwnerPawn == nullptr)
	{
		UE_LOG(LogAFVehicle, Error,
			TEXT("CreateBackendMovement called with a null owner pawn. No backend created."));
		return nullptr;
	}

	if (MeshComponent == nullptr)
	{
		UE_LOG(LogAFVehicle, Error,
			TEXT("CreateBackendMovement called with a null skeletal mesh component. ")
			TEXT("The backend needs a skeletal mesh to resolve wheel bones against."));
		return nullptr;
	}

	// ASSUMPTION REQUIRING VERIFICATION: UChaosWheeledVehicleMovementComponent
	// is the correct concrete movement component in Unreal Engine 5.8, and it
	// can be created outside a constructor with NewObject and registered.
	UChaosWheeledVehicleMovementComponent* Movement =
		NewObject<UChaosWheeledVehicleMovementComponent>(
			OwnerPawn, UChaosWheeledVehicleMovementComponent::StaticClass(),
			TEXT("AFBackendMovement"));

	if (Movement == nullptr)
	{
		UE_LOG(LogAFVehicle, Error,
			TEXT("Failed to create the backend movement component on %s."),
			*OwnerPawn->GetName());
		return nullptr;
	}

	// The movement component drives the primitive it is attached to. Binding
	// it to the skeletal mesh is what lets it resolve wheel bone names.
	Movement->SetUpdatedComponent(MeshComponent);
	Movement->RegisterComponent();
	OwnerPawn->AddInstanceComponent(Movement);

	BackendMovement = Movement;

	UE_LOG(LogAFVehicle, Log,
		TEXT("Backend movement component created on %s and bound to mesh component '%s'."),
		*OwnerPawn->GetName(), *MeshComponent->GetName());

	// Returned as UActorComponent on purpose. D-031: callers must never cast.
	return BackendMovement;
}

bool UAFVehicleCompatibilityLayer::ConfigureBackend(const FAFVehicleBackendSetup& Setup)
{
	if (BackendMovement == nullptr)
	{
		UE_LOG(LogAFVehicle, Error,
			TEXT("ConfigureBackend called before CreateBackendMovement. Nothing to configure."));
		return false;
	}

	const TArray<FString> Problems = Setup.ValidateSelf();
	for (const FString& Problem : Problems)
	{
		UE_LOG(LogAFVehicle, Error, TEXT("Backend setup is invalid: %s"), *Problem);
	}

	if (Problems.Num() > 0)
	{
		return false;
	}

	UChaosWheeledVehicleMovementComponent* Movement =
		Cast<UChaosWheeledVehicleMovementComponent>(BackendMovement);

	if (Movement == nullptr)
	{
		UE_LOG(LogAFVehicle, Error,
			TEXT("The held backend component is not the expected movement type. ")
			TEXT("Refusing to configure it."));
		return false;
	}

	//
	// Mass and centre of mass. Kilograms stay kilograms; metres become
	// centimetres here and nowhere else. D-013.
	//
	// ASSUMPTION REQUIRING VERIFICATION: property names Mass,
	// bEnableCenterOfMassOverride and CenterOfMassOverride on the Unreal
	// Engine 5.8 movement component.
	Movement->Mass = static_cast<float>(Setup.DryMassKg);
	Movement->bEnableCenterOfMassOverride = true;
	Movement->CenterOfMassOverride = ToCm(Setup.CentreOfMassOffsetM);

	//
	// Engine. A flat placeholder curve, not a real powertrain: the powertrain
	// model is Milestone 10 and pretending otherwise here would be a lie in
	// code rather than in prose.
	//
	// ASSUMPTION REQUIRING VERIFICATION: EngineSetup, MaxTorque and MaxRPM.
	Movement->EngineSetup.MaxTorque = static_cast<float>(Setup.PeakDriveTorqueNm);
	Movement->EngineSetup.MaxRPM = static_cast<float>(Setup.MaxRpm);

	//
	// Transmission. Automatic for Milestone 2 so that the acceptance criteria
	// can be assessed without a manual shift model.
	//
	// ASSUMPTION REQUIRING VERIFICATION: TransmissionSetup, bUseAutomaticGears
	// and ForwardGearRatios.
	Movement->TransmissionSetup.bUseAutomaticGears = Setup.bUseAutomaticGears;
	Movement->TransmissionSetup.ForwardGearRatios.Reset();
	for (int32 GearIndex = 0; GearIndex < Setup.ForwardGearCount; ++GearIndex)
	{
		// A monotonically falling ratio set. Placeholder figures.
		const double Ratio = 4.0 - (3.2 * static_cast<double>(GearIndex)
			/ FMath::Max(1.0, static_cast<double>(Setup.ForwardGearCount - 1)));
		Movement->TransmissionSetup.ForwardGearRatios.Add(static_cast<float>(Ratio));
	}

	//
	// Aerodynamics. Placeholder drag only; downforce is Milestone 10.
	//
	// ASSUMPTION REQUIRING VERIFICATION: DragCoefficient and DragArea, and
	// that DragArea is expressed in square centimetres.
	Movement->DragCoefficient = static_cast<float>(Setup.DragCoefficient);
	Movement->DragArea = static_cast<float>(
		Setup.FrontalAreaM2 * UAFUnitsHelper::CmPerMetre * UAFUnitsHelper::CmPerMetre);

	//
	// Wheels. Only the bone binding can be set here; the numeric parameters
	// live on the per-wheel objects, which do not exist until the component
	// registers. See D-036 and TryApplyWheelParameters.
	//
	// ASSUMPTION REQUIRING VERIFICATION: WheelSetups, FChaosWheelSetup,
	// WheelClass, BoneName and AdditionalOffset.
	Movement->WheelSetups.Reset();
	for (const FAFWheelSetup& Wheel : Setup.Wheels)
	{
		FChaosWheelSetup WheelSetup;
		WheelSetup.WheelClass = UChaosVehicleWheel::StaticClass();
		WheelSetup.BoneName = Wheel.BoneName;
		WheelSetup.AdditionalOffset = FVector::ZeroVector;
		Movement->WheelSetups.Add(WheelSetup);
	}

	PendingWheels = Setup.Wheels;
	bWheelParametersApplied = false;
	bBackendConfigured = true;

	UE_LOG(LogAFVehicle, Log,
		TEXT("Backend configured: mass %.1f kg, %d wheel(s), %d forward gear(s), ")
		TEXT("peak drive torque %.1f Nm, max %.0f rpm."),
		Setup.DryMassKg, Setup.Wheels.Num(), Setup.ForwardGearCount,
		Setup.PeakDriveTorqueNm, Setup.MaxRpm);

	// The wheel objects may already exist if the component registered before
	// configuration. Try immediately; ApplyInputFrame retries if not.
	TryApplyWheelParameters();

	return true;
}

bool UAFVehicleCompatibilityLayer::TryApplyWheelParameters()
{
	if (bWheelParametersApplied)
	{
		return true;
	}

	if (PendingWheels.Num() == 0)
	{
		return false;
	}

	UChaosWheeledVehicleMovementComponent* Movement =
		Cast<UChaosWheeledVehicleMovementComponent>(BackendMovement);

	if (Movement == nullptr)
	{
		return false;
	}

	// ASSUMPTION REQUIRING VERIFICATION: the Wheels array of instantiated
	// UChaosVehicleWheel objects, and its population during registration.
	if (Movement->Wheels.Num() != PendingWheels.Num())
	{
		return false;
	}

	for (int32 WheelIndex = 0; WheelIndex < PendingWheels.Num(); ++WheelIndex)
	{
		UChaosVehicleWheel* Wheel = Movement->Wheels[WheelIndex];
		if (Wheel == nullptr)
		{
			return false;
		}

		const FAFWheelSetup& Source = PendingWheels[WheelIndex];

		// ASSUMPTION REQUIRING VERIFICATION: every property name below, and
		// that all of them are expressed in centimetres and degrees.
		Wheel->WheelRadius = static_cast<float>(ToCm(Source.RadiusM));
		Wheel->WheelWidth = static_cast<float>(ToCm(Source.WidthM));
		Wheel->MaxSteerAngle = static_cast<float>(Source.MaxSteerAngleDeg);
		Wheel->MaxBrakeTorque = static_cast<float>(Source.MaxBrakeTorqueNm);
		Wheel->MaxHandBrakeTorque = static_cast<float>(Source.MaxHandbrakeTorqueNm);
		Wheel->bAffectedByHandbrake = Source.bAffectedByHandbrake;
		Wheel->bAffectedBySteering = Source.bAffectedBySteering;
		Wheel->bAffectedByEngine = Source.bDriven;

		Wheel->SuspensionMaxRaise = static_cast<float>(ToCm(Source.SuspensionMaxRaiseM));
		Wheel->SuspensionMaxDrop = static_cast<float>(ToCm(Source.SuspensionMaxDropM));
		Wheel->SuspensionDampingRatio = static_cast<float>(Source.SuspensionDampingRatio);
		Wheel->SpringPreload = static_cast<float>(ToCm(Source.SuspensionRestLengthM));
	}

	bWheelParametersApplied = true;

	UE_LOG(LogAFVehicle, Log,
		TEXT("Wheel parameters applied to %d backend wheel object(s)."),
		PendingWheels.Num());

	return true;
}

bool UAFVehicleCompatibilityLayer::ApplyInputFrame(const FAFVehicleInputFrame& InputFrame)
{
	LastInputFrame = InputFrame;
	LastInputFrame.Sanitise();
	++AppliedFrameCount;

	UChaosWheeledVehicleMovementComponent* Movement =
		Cast<UChaosWheeledVehicleMovementComponent>(BackendMovement);

	if (Movement == nullptr)
	{
		// Verbose, not Warning: a car-less test map is a legitimate state and
		// a warning per tick would drown the log.
		UE_LOG(LogAFVehicle, Verbose,
			TEXT("ApplyInputFrame received frame %d at t=%f with no backend bound."),
			AppliedFrameCount, LastInputFrame.SessionTime);
		return false;
	}

	// The wheel objects may only now exist. Cheap and idempotent once applied.
	TryApplyWheelParameters();

	// ASSUMPTION REQUIRING VERIFICATION: SetThrottleInput, SetBrakeInput and
	// SetSteeringInput accept normalised floats, with positive steering to
	// the right, which is the FAFVehicleInputFrame convention.
	Movement->SetThrottleInput(LastInputFrame.Throttle);
	Movement->SetBrakeInput(LastInputFrame.Brake);
	Movement->SetSteeringInput(LastInputFrame.Steer);

	// Shift flags are edges: the caller sets them true for exactly one frame,
	// so acting on them directly cannot repeat.
	if (!LastInputFrame.bShiftUp && !LastInputFrame.bShiftDown)
	{
		return true;
	}

	if (LastInputFrame.bShiftUp && LastInputFrame.bShiftDown)
	{
		// Both edges in one frame is a caller bug, not a driver intent.
		UE_LOG(LogAFVehicle, Warning,
			TEXT("Input frame %d requested shift up and shift down simultaneously; ")
			TEXT("both requests ignored."),
			AppliedFrameCount);
		return true;
	}

	// ASSUMPTION REQUIRING VERIFICATION: GetCurrentGear and SetTargetGear,
	// and that gear 0 is neutral with positive values as forward gears.
	const int32 CurrentGear = Movement->GetCurrentGear();
	const int32 RequestedGear = LastInputFrame.bShiftUp ? CurrentGear + 1 : CurrentGear - 1;
	Movement->SetTargetGear(RequestedGear, true);

	UE_LOG(LogAFVehicle, Verbose,
		TEXT("Input frame %d requested gear %d from %d."),
		AppliedFrameCount, RequestedGear, CurrentGear);

	return true;
}

bool UAFVehicleCompatibilityLayer::SetHandbrake(bool bEngaged)
{
	UChaosWheeledVehicleMovementComponent* Movement =
		Cast<UChaosWheeledVehicleMovementComponent>(BackendMovement);

	if (Movement == nullptr)
	{
		UE_LOG(LogAFVehicle, Verbose,
			TEXT("SetHandbrake(%s) ignored: no backend bound."),
			bEngaged ? TEXT("true") : TEXT("false"));
		return false;
	}

	// ASSUMPTION REQUIRING VERIFICATION: SetHandbrakeInput takes a bool.
	Movement->SetHandbrakeInput(bEngaged);
	return true;
}

bool UAFVehicleCompatibilityLayer::ZeroBackendVelocity()
{
	UChaosWheeledVehicleMovementComponent* Movement =
		Cast<UChaosWheeledVehicleMovementComponent>(BackendMovement);

	if (Movement == nullptr)
	{
		UE_LOG(LogAFVehicle, Verbose,
			TEXT("ZeroBackendVelocity ignored: no backend bound."));
		return false;
	}

	// ASSUMPTION REQUIRING VERIFICATION: StopMovementImmediately exists on the
	// movement component and clears both linear and angular velocity.
	Movement->StopMovementImmediately();

	// Belt and braces: the simulated body keeps its own velocity state, and
	// clearing only the movement component has historically left a car
	// creeping after a reset.
	if (UPrimitiveComponent* Primitive =
		Cast<UPrimitiveComponent>(Movement->UpdatedComponent))
	{
		if (Primitive->IsSimulatingPhysics())
		{
			Primitive->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Primitive->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		}
	}

	return true;
}

double UAFVehicleCompatibilityLayer::GetForwardSpeedKph() const
{
	const UChaosWheeledVehicleMovementComponent* Movement =
		Cast<UChaosWheeledVehicleMovementComponent>(BackendMovement);

	if (Movement == nullptr)
	{
		return 0.0;
	}

	// ASSUMPTION REQUIRING VERIFICATION: GetForwardSpeed returns centimetres
	// per second, which is what UnrealSpeedToKph expects.
	return UAFUnitsHelper::UnrealSpeedToKph(
		static_cast<double>(Movement->GetForwardSpeed()));
}

int32 UAFVehicleCompatibilityLayer::GetCurrentGear() const
{
	const UChaosWheeledVehicleMovementComponent* Movement =
		Cast<UChaosWheeledVehicleMovementComponent>(BackendMovement);

	if (Movement == nullptr)
	{
		return 0;
	}

	return Movement->GetCurrentGear();
}

double UAFVehicleCompatibilityLayer::GetEngineRpm() const
{
	const UChaosWheeledVehicleMovementComponent* Movement =
		Cast<UChaosWheeledVehicleMovementComponent>(BackendMovement);

	if (Movement == nullptr)
	{
		return 0.0;
	}

	// ASSUMPTION REQUIRING VERIFICATION: GetEngineRotationSpeed returns
	// revolutions per minute rather than radians per second.
	return static_cast<double>(Movement->GetEngineRotationSpeed());
}

bool UAFVehicleCompatibilityLayer::AreAllWheelsGrounded() const
{
	const UChaosWheeledVehicleMovementComponent* Movement =
		Cast<UChaosWheeledVehicleMovementComponent>(BackendMovement);

	if (Movement == nullptr)
	{
		return false;
	}

	if (Movement->Wheels.Num() == 0)
	{
		// No wheels is not "all wheels grounded". Returning true here would
		// let a reset heuristic treat a car with no backend as planted.
		return false;
	}

	for (const UChaosVehicleWheel* Wheel : Movement->Wheels)
	{
		// ASSUMPTION REQUIRING VERIFICATION: IsInAir on the wheel object.
		if (Wheel == nullptr || Wheel->IsInAir())
		{
			return false;
		}
	}

	return true;
}

TArray<FString> FAFVehicleBackendSetup::ValidateSelf() const
{
	TArray<FString> Problems;

	if (DryMassKg <= 0.0)
	{
		Problems.Add(FString::Printf(
			TEXT("DryMassKg is %.3f. Mass must be greater than zero."), DryMassKg));
	}

	if (Wheels.Num() == 0)
	{
		Problems.Add(TEXT("Wheels is empty. The backend needs at least one wheel."));
	}

	int32 DrivenCount = 0;
	int32 SteeredCount = 0;

	for (int32 WheelIndex = 0; WheelIndex < Wheels.Num(); ++WheelIndex)
	{
		const FAFWheelSetup& Wheel = Wheels[WheelIndex];

		for (const FString& WheelProblem : Wheel.ValidateSelf())
		{
			Problems.Add(FString::Printf(
				TEXT("Wheel %d: %s"), WheelIndex, *WheelProblem));
		}

		if (Wheel.bDriven)
		{
			++DrivenCount;
		}

		if (Wheel.bAffectedBySteering)
		{
			++SteeredCount;
		}
	}

	if (Wheels.Num() > 0 && DrivenCount == 0)
	{
		Problems.Add(TEXT(
			"No wheel is marked bDriven. The vehicle cannot accelerate, which "
			"fails the Milestone 2 acceptance criterion 'vehicle accelerates'."));
	}

	if (Wheels.Num() > 0 && SteeredCount == 0)
	{
		Problems.Add(TEXT(
			"No wheel is marked bAffectedBySteering. The vehicle cannot steer, "
			"which fails the Milestone 2 acceptance criterion 'vehicle steers'."));
	}

	if (PeakDriveTorqueNm <= 0.0)
	{
		Problems.Add(FString::Printf(
			TEXT("PeakDriveTorqueNm is %.3f. Drive torque must be greater than zero."),
			PeakDriveTorqueNm));
	}

	if (MaxRpm <= 0.0)
	{
		Problems.Add(FString::Printf(
			TEXT("MaxRpm is %.3f. Maximum engine speed must be greater than zero."),
			MaxRpm));
	}

	if (PeakTorqueRpm > MaxRpm)
	{
		Problems.Add(FString::Printf(
			TEXT("PeakTorqueRpm %.1f exceeds MaxRpm %.1f. Peak torque would never be reached."),
			PeakTorqueRpm, MaxRpm));
	}

	if (ForwardGearCount <= 0)
	{
		Problems.Add(FString::Printf(
			TEXT("ForwardGearCount is %d. At least one forward gear is required."),
			ForwardGearCount));
	}

	if (DragCoefficient < 0.0)
	{
		Problems.Add(FString::Printf(
			TEXT("DragCoefficient is %.3f. Negative drag would accelerate the vehicle."),
			DragCoefficient));
	}

	if (FrontalAreaM2 <= 0.0)
	{
		Problems.Add(FString::Printf(
			TEXT("FrontalAreaM2 is %.3f. Frontal area must be greater than zero."),
			FrontalAreaM2));
	}

	return Problems;
}
