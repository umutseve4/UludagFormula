// Copyright ApexFormula. Original work. Not affiliated with any real motorsport series.

#include "AFVehiclePawn.h"
#include "AFVehicleCompatibilityLayer.h"
#include "AFVehicleComponentBase.h"
#include "AFVehicleDefinition.h"
#include "AFLog.h"
#include "AFUnits.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"

namespace
{
	/**
	 * Rate limit for the last-good-transform sample, seconds.
	 * Sampling every frame is wasted work and makes the reset point drift to
	 * "half a metre ago", which is not a useful place to be put back.
	 */
	constexpr double GoodTransformIntervalSeconds = 0.25;

	/** Minimum forward speed before a transform is considered worth recording, kph. */
	constexpr double GoodTransformMinSpeedKph = 5.0;

	/**
	 * Dot product of the actor up vector against world up, above which the
	 * vehicle counts as upright. 0.7 is roughly 45 degrees of lean.
	 */
	constexpr double UprightDotThreshold = 0.7;
}

AAFVehiclePawn::AAFVehiclePawn()
{
	// Milestone 2 needs a tick: the last-good-transform sampler runs here.
	PrimaryActorTick.bCanEverTick = true;

	VehicleMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("VehicleMesh"));
	SetRootComponent(VehicleMeshComponent);

	// The mesh is the simulated body. The backend movement component drives
	// it, so it must simulate physics and collide with the world.
	VehicleMeshComponent->SetSimulatePhysics(true);
	VehicleMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	VehicleMeshComponent->SetCollisionObjectType(ECollisionChannel::ECC_Pawn);
	VehicleMeshComponent->SetGenerateOverlapEvents(true);

	//
	// Chase camera rig. D-034.
	//
	// The boom does NOT inherit pitch or roll. A boom that follows body roll
	// makes a car on a kerb look like the world is tilting, which reads as a
	// physics bug during playtesting even when the physics is correct. Yaw is
	// inherited so the camera stays behind the car through a corner.
	//
	// The distances below are ApexFormula design values in centimetres,
	// because component transforms are engine units. They are the one place
	// outside the compatibility layer where centimetres legitimately appear,
	// since no metric source value is being converted here. See D-013.
	//
	ChaseCameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("ChaseCameraBoom"));
	ChaseCameraBoom->SetupAttachment(VehicleMeshComponent);
	ChaseCameraBoom->TargetArmLength = 750.0f;
	ChaseCameraBoom->SocketOffset = FVector(0.0f, 0.0f, 200.0f);
	ChaseCameraBoom->bUsePawnControlRotation = false;
	ChaseCameraBoom->bInheritPitch = false;
	ChaseCameraBoom->bInheritRoll = false;
	ChaseCameraBoom->bInheritYaw = true;
	ChaseCameraBoom->bEnableCameraLag = true;
	ChaseCameraBoom->bEnableCameraRotationLag = true;
	ChaseCameraBoom->CameraLagSpeed = 12.0f;
	ChaseCameraBoom->CameraRotationLagSpeed = 8.0f;
	ChaseCameraBoom->bDoCollisionTest = true;

	ChaseCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
	ChaseCamera->SetupAttachment(ChaseCameraBoom, USpringArmComponent::SocketName);
	ChaseCamera->bUsePawnControlRotation = false;
	ChaseCamera->SetRelativeRotation(FRotator(-8.0f, 0.0f, 0.0f));
	ChaseCamera->FieldOfView = 90.0f;

	CompatibilityLayer = CreateDefaultSubobject<UAFVehicleCompatibilityLayer>(TEXT("CompatibilityLayer"));
}

void AAFVehiclePawn::BeginPlay()
{
	Super::BeginPlay();

	// Seed the reset target before anything can move the car.
	LastGoodTransform = GetActorTransform();

	if (CompatibilityLayer != nullptr)
	{
		// The layer creates and owns the engine movement component. This pawn
		// only holds the returned pointer for lifetime; it must never cast it.
		// See D-031.
		BackendMovement = CompatibilityLayer->CreateBackendMovement(this, VehicleMeshComponent);

		if (BackendMovement == nullptr)
		{
			UE_LOG(LogAFVehicle, Error,
				TEXT("%s: no backend movement component was created. ")
				TEXT("The vehicle will not respond to driver input."),
				*GetName());
		}
	}
	else
	{
		UE_LOG(LogAFVehicle, Error,
			TEXT("%s: CompatibilityLayer is null. This should be impossible; ")
			TEXT("it is created as a default subobject."),
			*GetName());
	}

	if (VehicleDefinition != nullptr)
	{
		ApplyVehicleDefinition();
	}
	else
	{
		UE_LOG(LogAFVehicle, Warning,
			TEXT("%s spawned with no VehicleDefinition assigned."), *GetName());
	}
}

void AAFVehiclePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	SecondsSinceGoodTransform += static_cast<double>(DeltaSeconds);

	if (SecondsSinceGoodTransform >= GoodTransformIntervalSeconds)
	{
		SecondsSinceGoodTransform = 0.0;
		UpdateLastGoodTransform();
	}
}

void AAFVehiclePawn::UpdateLastGoodTransform()
{
	if (CompatibilityLayer == nullptr)
	{
		return;
	}

	// A transform is only worth returning to if the car was upright, on the
	// ground and actually going somewhere when it was recorded. Recording a
	// stationary or airborne pose would let the reset key put the driver back
	// into the situation they were trying to escape.
	const bool bUpright =
		FVector::DotProduct(GetActorUpVector(), FVector::UpVector) > UprightDotThreshold;

	if (!bUpright)
	{
		return;
	}

	if (!CompatibilityLayer->AreAllWheelsGrounded())
	{
		return;
	}

	if (CompatibilityLayer->GetForwardSpeedKph() < GoodTransformMinSpeedKph)
	{
		return;
	}

	LastGoodTransform = GetActorTransform();
}

bool AAFVehiclePawn::ApplyVehicleDefinition()
{
	if (VehicleDefinition == nullptr)
	{
		UE_LOG(LogAFVehicle, Warning,
			TEXT("%s: ApplyVehicleDefinition called with no definition assigned."),
			*GetName());
		return false;
	}

	const TArray<FString> Problems = VehicleDefinition->ValidateSelf();
	for (const FString& Problem : Problems)
	{
		UE_LOG(LogAFVehicle, Error,
			TEXT("%s: vehicle definition '%s' is invalid: %s"),
			*GetName(), *VehicleDefinition->VehicleId.ToString(), *Problem);
	}

	if (Problems.Num() > 0)
	{
		return false;
	}

	//
	// Build the backend setup from the definition. This is plain data: no
	// engine vehicle type is named here, which is what lets this code live
	// outside the compatibility layer. See D-032.
	//
	if (CompatibilityLayer != nullptr && BackendMovement != nullptr)
	{
		FAFVehicleBackendSetup Setup;
		Setup.DryMassKg = VehicleDefinition->DryMassKg;
		Setup.Wheels = VehicleDefinition->Wheels;

		// Centre of mass, metres, relative to the mesh origin. The definition
		// stores a longitudinal bias between the axles plus a height; the
		// offset is derived rather than authored so the two cannot disagree.
		const double MidWheelbaseToBias =
			(VehicleDefinition->CentreOfMassBiasRear - 0.5) * VehicleDefinition->WheelbaseM;

		// Negative X is rearward in the ApexFormula mesh convention: the mesh
		// faces +X, so moving mass rearward is a negative X offset.
		Setup.CentreOfMassOffsetM = FVector(
			-MidWheelbaseToBias,
			0.0,
			VehicleDefinition->CentreOfMassHeightM);

		if (!CompatibilityLayer->ConfigureBackend(Setup))
		{
			UE_LOG(LogAFVehicle, Error,
				TEXT("%s: backend configuration failed for vehicle definition '%s'."),
				*GetName(), *VehicleDefinition->VehicleId.ToString());
			return false;
		}
	}

	// Deterministic ordering. GetComponents does not guarantee a stable order,
	// so sort by SubsystemId before configuring. Configuration order must not
	// depend on how the components happened to be registered.
	TArray<UAFVehicleComponentBase*> Subsystems;
	GetComponents<UAFVehicleComponentBase>(Subsystems);

	Subsystems.Sort(
		[](const UAFVehicleComponentBase& A, const UAFVehicleComponentBase& B)
		{
			return A.SubsystemId.LexicalLess(B.SubsystemId);
		});

	for (UAFVehicleComponentBase* Subsystem : Subsystems)
	{
		if (Subsystem != nullptr)
		{
			Subsystem->ApplyConfiguration();
		}
	}

	UE_LOG(LogAFVehicle, Log,
		TEXT("%s configured from vehicle definition '%s' with %d subsystem component(s)."),
		*GetName(), *VehicleDefinition->VehicleId.ToString(), Subsystems.Num());

	return true;
}

void AAFVehiclePawn::SubmitInputFrame(const FAFVehicleInputFrame& InputFrame)
{
	if (CompatibilityLayer != nullptr)
	{
		CompatibilityLayer->ApplyInputFrame(InputFrame);
	}
}

void AAFVehiclePawn::SetHandbrake(bool bEngaged)
{
	if (CompatibilityLayer != nullptr)
	{
		CompatibilityLayer->SetHandbrake(bEngaged);
	}
}

void AAFVehiclePawn::ResetVehicle()
{
	// Order matters. Zero the velocities first, then move: teleporting a body
	// that still carries velocity leaves it moving at the destination, which
	// is exactly the "car flies away after reset" failure.
	if (CompatibilityLayer != nullptr)
	{
		CompatibilityLayer->ZeroBackendVelocity();
	}

	// Lift the reset target slightly so the car does not spawn intersecting
	// the surface it was recorded on. 20 cm is a design value.
	FTransform ResetTransform = LastGoodTransform;
	ResetTransform.AddToTranslation(FVector(0.0, 0.0, 20.0));

	// Discard pitch and roll: the recorded transform was upright, but a small
	// residual lean would be re-applied on every reset and compound.
	FRotator ResetRotation = ResetTransform.Rotator();
	ResetRotation.Pitch = 0.0;
	ResetRotation.Roll = 0.0;
	ResetTransform.SetRotation(ResetRotation.Quaternion());

	SetActorTransform(ResetTransform, false, nullptr, ETeleportType::TeleportPhysics);

	// Zero again after the teleport. The physics body can pick up velocity
	// from the move itself.
	if (CompatibilityLayer != nullptr)
	{
		CompatibilityLayer->ZeroBackendVelocity();
		CompatibilityLayer->SetHandbrake(false);
	}

	++ResetCount;
	SecondsSinceGoodTransform = 0.0;

	// A reset is a rule-relevant decision, so it is logged with the rule
	// macro. Silent rule decisions are prohibited.
	AF_LOG_RULE(LogAFVehicle, ParticipantId, (GetWorld() != nullptr) ? GetWorld()->GetTimeSeconds() : 0.0,
		TEXT("%s reset to the last good transform. Reset count %d. ")
		TEXT("Lap invalidation reason for Milestone 3: VehicleReset."),
		*GetName(), ResetCount);
}

int32 AAFVehiclePawn::GetParticipantId() const
{
	return ParticipantId;
}

FString AAFVehiclePawn::GetParticipantDisplayName() const
{
	// D-035. The interface declares FString. DriverDisplayName stays FText
	// because it is localisable authored content; the conversion happens here,
	// at the boundary, and nowhere else.
	return DriverDisplayName.ToString();
}

FVector AAFVehiclePawn::GetParticipantLocation() const
{
	return GetActorLocation();
}

FVector AAFVehiclePawn::GetParticipantForward() const
{
	return GetActorForwardVector();
}

double AAFVehiclePawn::GetParticipantSpeedKph() const
{
	return (CompatibilityLayer != nullptr)
		? CompatibilityLayer->GetForwardSpeedKph()
		: 0.0;
}

bool AAFVehiclePawn::IsParticipantActive() const
{
	return ParticipantId != INDEX_NONE && !IsActorBeingDestroyed();
}
