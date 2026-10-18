#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IvyTypes.h"
#include "IvyGrowthComponent.generated.h"

class UIvyLightSubsystem;
class UIvyGardenSubsystem;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnIvyNodeAdded, int32);
DECLARE_MULTICAST_DELEGATE(FOnIvyRebuilt);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnIvyChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnIvySapChanged, float, CurrentSap, float, MaxSap);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIvyLightGrowthChanged, bool, bGrowingInLight);

/**
 * Owns the data of one ivy plant (nodes, branches, its own sap) and the growth algorithm.
 * Undo is global and lives in UIvyGardenSubsystem.
 * Knows nothing about rendering: other systems listen to its events.
 */
UCLASS(ClassGroup = (Ivy), meta = (BlueprintSpawnableComponent))
class IVYGAME_API UIvyGrowthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIvyGrowthComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ivy")
	TObjectPtr<UIvyGrowthSettings> Settings;

	/** Lower values come first when the player cycles plants. The lowest is selected at start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ivy")
	int32 SelectionOrder = 0;

	/** Sap budget of this plant. <= 0 uses Settings->MaxSap. Lets two plants in the same level have different budgets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ivy|Sap", meta = (ClampMin = "0"))
	float MaxSapOverride = 0.f;

	// --- Obstacle avoidance ---
	/** Number of recent steps used to measure progress toward the target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ivy|Steering", meta = (ClampMin = "2"))
	int32 StuckWindow = 8;

	/** Minimum fraction of the grown distance that must bring the ivy closer to the target (0.3 = 30%). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ivy|Steering", meta = (ClampMin = "0", ClampMax = "1"))
	float MinProgressRatio = 0.3f;

	/** Shortest look-ahead (steps) when simulating detour directions. Used for nearby targets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ivy|Steering", meta = (ClampMin = "1"))
	int32 DetourLookaheadSteps = 20;

	/** Longest look-ahead (steps). The look-ahead grows with the distance to the target up to this value. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ivy|Steering", meta = (ClampMin = "1"))
	int32 MaxLookaheadSteps = 80;

	/** While going around an obstacle, the route is re-planned every this many steps. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ivy|Steering", meta = (ClampMin = "1"))
	int32 DetourReplanSteps = 8;

	/** Detour steps allowed without getting closer before giving up (saves sap). Moving the cursor retries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ivy|Steering", meta = (ClampMin = "1"))
	int32 MaxDetourSteps = 80;

	// --- Plant life cycle ---
	UFUNCTION(BlueprintCallable, Category = "Ivy")
	void PlantSeed(const FVector& Location, const FVector& Normal, const FVector& Tangent);

	UFUNCTION(BlueprintCallable, Category = "Ivy")
	bool BeginGesture(const FVector& Target);

	UFUNCTION(BlueprintCallable, Category = "Ivy")
	void SetGrowTarget(const FVector& Target);

	UFUNCTION(BlueprintCallable, Category = "Ivy")
	void EndGesture();

	/** Undo is global: forwards to the garden. */
	UFUNCTION(BlueprintCallable, Category = "Ivy")
	bool Undo();

	UFUNCTION(BlueprintPure, Category = "Ivy")
	bool CanUndo() const;

	UFUNCTION(BlueprintPure, Category = "Ivy")
	bool IsGrowing() const { return bGestureActive; }

	// --- Sap (owned by this plant) ---
	UFUNCTION(BlueprintCallable, Category = "Ivy|Sap")
	void AddSap(float Amount);

	/** Clamps to [0, MaxSap] and broadcasts the change. Also used by the garden on undo. */
	void SetSap(float NewSap);

	UFUNCTION(BlueprintPure, Category = "Ivy|Sap")
	float GetSap() const { return Sap; }

	UFUNCTION(BlueprintPure, Category = "Ivy|Sap")
	float GetMaxSap() const;

	UFUNCTION(BlueprintPure, Category = "Ivy|Sap")
	bool HasSapForStep() const;

	/** Distance (cm) this plant can still grow with its own sap. -1 = unlimited. */
	UFUNCTION(BlueprintPure, Category = "Ivy|Sap")
	float GetRemainingGrowthDistance() const;

	// --- Light ---
	/** True if the surface point is reached by a gameplay light (growth there is free). */
	UFUNCTION(BlueprintPure, Category = "Ivy|Light")
	bool IsSurfaceLit(const FVector& Location, const FVector& Normal) const;

	/** True while the last grown step was on a lit surface. */
	UFUNCTION(BlueprintPure, Category = "Ivy|Light")
	bool IsGrowingInLight() const { return bGrowingInLight; }

	// --- Queries ---
	/** LightDirection = forward vector of the Directional Light. */
	UFUNCTION(BlueprintPure, Category = "Ivy")
	bool IsPointShadowed(const FVector& Point, const FVector& LightDirection) const;

	UFUNCTION(BlueprintPure, Category = "Ivy")
	int32 GetNumNodes() const { return Nodes.Num(); }

	const TArray<FIvyNode>& GetNodes() const { return Nodes; }
	const TArray<int32>& GetBranchTips() const { return BranchTips; }
	int32 GetNumBranches() const { return BranchTips.Num(); }
	int32 GetBranchTip(int32 BranchId) const;

	static bool IsClimbable(const FHitResult& Hit);

	/** Called by the garden on undo: truncates the plant to a previous state. */
	void RestoreToSnapshot(int32 NumNodes, const TArray<int32>& InBranchTips);

	// --- Events ---
	FOnIvyNodeAdded OnNodeAdded;   // C++: incremental rendering
	FOnIvyRebuilt   OnRebuilt;     // C++: full rebuild (seed / undo)

	UPROPERTY(BlueprintAssignable, Category = "Ivy")
	FOnIvyChanged OnIvyChanged;

	/** Sap of THIS plant changed. */
	UPROPERTY(BlueprintAssignable, Category = "Ivy")
	FOnIvySapChanged OnSapChanged;

	UPROPERTY(BlueprintAssignable, Category = "Ivy")
	FOnIvyLightGrowthChanged OnLightGrowthChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void GrowTick();
	bool GrowStep();

	/** Placement of the next node. bAllowInteraction = false for dry runs (no pushes, no side effects). */
	bool FindPlacement(const FIvyNode& From, const FVector& Dir, FVector& OutLocation, FVector& OutNormal, bool bAllowInteraction) const;

	// --- Steering ---
	/** Greedy direction toward the target on the current surface. bOutValid = false if the target is too far off this surface's plane. */
	FVector ComputeSteeringDir(const FIvyNode& From, const FVector& ToTarget, bool& bOutValid) const;

	/** True if the recent steps brought the ivy too little closer to the target (loops, back-and-forth). */
	bool IsStuck(double Distance) const;

	/** Simulates candidate directions (plus "keep going") and returns the one that gets closest to the target. */
	bool ChooseDetourDir(const FIvyNode& From, FVector& OutDir) const;

	/** Dry run: grows straight along the surface and returns the closest distance to the target reached. */
	double SimulateStraight(const FIvyNode& From, const FVector& StartDir, int32 Steps) const;

	static FVector ComputeTangent(const FVector& Dir, const FVector& FromNormal, const FVector& NewNormal);
	void BeginDetour(double Distance);
	void ResetSteering();

	void AddNode(const FIvyNode& From, const FVector& Location, const FVector& Normal, const FVector& Tangent, const FVector& Dir, bool bLit);
	void SetGrowingInLight(bool bInLight);
	void NotifyInteractables(const FVector& Location, const FVector& Dir);
	int32 FindStartNode(const FVector& Target, int32& OutBranch) const;
	bool TraceIvy(const FVector& Start, const FVector& End, FHitResult& OutHit) const;

	TArray<FIvyNode> Nodes;
	TArray<int32> BranchTips;

	bool bGestureActive = false;
	int32 ActiveNode = INDEX_NONE;
	int32 ActiveBranch = INDEX_NONE;
	FVector GrowTarget = FVector::ZeroVector;

	// --- Sap ---
	float Sap = 0.f;

	// --- Steering state (per gesture, never saved) ---
	FVector TrackedTarget = FVector::ZeroVector;
	TArray<double> RecentDistances;     // Distances to the target over the last StuckWindow steps
	double DetourStartDistance = 0.0;
	double DetourBestDistance = 0.0;
	int32 DetourStepsWithoutGain = 0;
	int32 StepsSinceReplan = 0;
	bool bDetouring = false;
	bool bDetourGaveUp = false;

	/** Set by FindPlacement when this step pushed an object (or the object is still moving from a push). */
	mutable bool bPushedThisStep = false;

	UPROPERTY(Transient)
	TObjectPtr<UIvyLightSubsystem> LightSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UIvyGardenSubsystem> Garden;

	bool bGrowingInLight = false;

	FTimerHandle GrowTimer;
};