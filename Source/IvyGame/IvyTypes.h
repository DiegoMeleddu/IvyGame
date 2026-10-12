#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "IvyTypes.generated.h"

class AActor;

// Project collision channels
namespace IvyChannels
{
	inline constexpr ECollisionChannel Grow = ECC_GameTraceChannel1;     // IvyGrow, default Block
	inline constexpr ECollisionChannel Interact = ECC_GameTraceChannel2; // IvyInteract, default Ignore
}

// A point on the ivy
USTRUCT(BlueprintType)
struct FIvyNode
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ivy")
	FVector Location = FVector::ZeroVector;      // Already raised by SurfaceOffset from the surface

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ivy")
	FVector Normal = FVector::UpVector;          // Normal of the surface the node rests on

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ivy")
	FVector Tangent = FVector::ForwardVector;    // Growth direction on the surface

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ivy")
	int32 Parent = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ivy")
	int32 BranchId = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ivy")
	float DistanceAlongBranch = 0.f;             // cm from the start of the branch

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ivy")
	float BirthTime = 0.f;                       // World time at creation

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ivy")
	bool bLit = false;                           // Grown on a lit surface (free step)
};

// A generic state that a puzzle object saves/restores for undo
USTRUCT(BlueprintType)
struct FIvySavedState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ivy")
	FTransform Transform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ivy")
	TArray<float> Floats;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ivy")
	TArray<bool> Bools;
};

USTRUCT()
struct FIvyObjectRecord
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<AActor> Actor;

	UPROPERTY()
	FIvySavedState State;
};

// State before a gesture. Nodes are only appended, so their count is enough to restore them.
USTRUCT()
struct FIvySnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	int32 NumNodes = 0;

	UPROPERTY()
	TArray<int32> BranchTips;

	UPROPERTY()
	float Sap = 0.f;

	UPROPERTY()
	TArray<FIvyObjectRecord> Objects;
};

/** All gameplay values related to ivy growth. */
UCLASS(BlueprintType)
class IVYGAME_API UIvyGrowthSettings : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Growth", meta = (ClampMin = "1", Units = "cm"))
	float StepLength = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Growth", meta = (ClampMin = "0.01", Units = "s"))
	float StepInterval = 0.05f;

	/** Maximum rotation per step toward the cursor */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Growth", meta = (ClampMin = "1", ClampMax = "180", Units = "deg"))
	float TurnRate = 25.f;

	/** Random (deterministic) deviation per step */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Growth", meta = (ClampMin = "0", ClampMax = "45", Units = "deg"))
	float Wander = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Growth", meta = (ClampMin = "0", Units = "cm"))
	float SurfaceOffset = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Growth", meta = (ClampMin = "1", Units = "cm"))
	float ProbeDepth = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Growth", meta = (ClampMin = "0", Units = "cm"))
	float ArriveRadius = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Growth")
	int32 RandomSeed = 1337;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sap", meta = (ClampMin = "0"))
	float MaxSap = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sap", meta = (ClampMin = "0"))
	float SapCostPerStep = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = "0", Units = "cm"))
	float InteractRadius = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = "0", Units = "cm"))
	float ShadowRadius = 30.f;
};