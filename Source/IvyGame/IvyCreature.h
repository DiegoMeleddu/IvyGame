#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IvyInteractable.h"
#include "IvyCreature.generated.h"

class UIvyGardenGraph;

UENUM(BlueprintType)
enum class EIvyCreatureState : uint8
{
	OffIvy,    // Not on the ivy yet
	Moving,    // Following a path
	Waiting,   // On the ivy, but the goal is not reachable yet
	AtGoal     // Goal reached
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCreatureStateChanged, EIvyCreatureState, NewState);

/**
 * A creature that walks on the ivy of every plant, using A* on the garden graph.
 * Logic lives in C++; animations and effects are driven in Blueprint through OnStateChanged.
 */
UCLASS()
class IVYGAME_API AIvyCreature : public AActor, public IIvyInteractable
{
	GENERATED_BODY()

public:
	AIvyCreature();
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "Creature")
	EIvyCreatureState GetState() const { return State; }

	AActor* GetGoalActor() const { return GoalActor; }

	/** For animations, sounds and puzzle logic. */
	UPROPERTY(BlueprintAssignable, Category = "Creature")
	FOnCreatureStateChanged OnStateChanged;

	// IIvyInteractable: the creature takes part in undo like any puzzle object
	virtual void OnIvyGrowth_Implementation(const FVector& Location, const FVector& GrowthDirection) override {}
	virtual bool OnIvyBlocked_Implementation(const FVector& Location, const FVector& GrowthDirection) override { return false; }
	virtual FIvySavedState IvySaveState_Implementation() override;
	virtual void IvyRestoreState_Implementation(const FIvySavedState& SavedState) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> RootComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Creature")
	TObjectPtr<AActor> GoalActor;

	UPROPERTY(EditAnywhere, Category = "Creature")
	float Speed = 60.f;          // cm/s

	UPROPERTY(EditAnywhere, Category = "Creature")
	float HeightOffset = 3.f;    // Distance from the stem

	UPROPERTY(EditAnywhere, Category = "Creature")
	float MountRadius = 40.f;    // Distance to climb onto the ivy

	UPROPERTY(EditAnywhere, Category = "Creature")
	float GoalRadius = 40.f;     // Distance to reach the goal

	UPROPERTY(EditAnywhere, Category = "Creature")
	float TurnSpeed = 10.f;

	UPROPERTY(EditAnywhere, Category = "Creature")
	float RepathInterval = 0.2f;

private:
	void MarkRepath() { bRepathPending = true; }
	void Repath();
	void TryMount();
	bool BeginSegment();
	void TickMoving(float DeltaSeconds);
	void OnPathFinished();
	void CheckGoal();
	void ApplyTransform(float DeltaSeconds);
	void SetState(EIvyCreatureState NewState);

	UPROPERTY()
	TObjectPtr<UIvyGardenGraph> Graph;

	EIvyCreatureState State = EIvyCreatureState::OffIvy;
	TArray<int32> Path;             // Global node ids
	int32 PathIndex = 0;            // Index in Path of the node we are heading to
	int32 CurrentNode = INDEX_NONE; // Last node reached (global id)

	FVector CurrentPos = FVector::ZeroVector;
	FVector CurrentUp = FVector::UpVector;
	FVector SegStartPos, SegEndPos, SegStartUp, SegEndUp;
	float SegLength = 1.f;
	float SegAlpha = 1.f;

	bool bRepathPending = true;
	float RepathCooldown = 0.f;
};