#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IvyTypes.h"
#include "IvyGrowthComponent.generated.h"

class UIvyLightSubsystem;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnIvyNodeAdded, int32);
DECLARE_MULTICAST_DELEGATE(FOnIvyRebuilt);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnIvyChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnIvySapChanged, float, CurrentSap, float, MaxSap);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIvyLightGrowthChanged, bool, bGrowingInLight);

/**
 * Owns the ivy data (nodes, branches, sap, undo) and the growth algorithm.
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ivy", meta = (ClampMin = "1"))
	int32 MaxUndoSteps = 50;

	// --- Plant life cycle ---
	UFUNCTION(BlueprintCallable, Category = "Ivy")
	void PlantSeed(const FVector& Location, const FVector& Normal, const FVector& Tangent);

	UFUNCTION(BlueprintCallable, Category = "Ivy")
	bool BeginGesture(const FVector& Target);

	UFUNCTION(BlueprintCallable, Category = "Ivy")
	void SetGrowTarget(const FVector& Target);

	UFUNCTION(BlueprintCallable, Category = "Ivy")
	void EndGesture();

	UFUNCTION(BlueprintCallable, Category = "Ivy")
	bool Undo();

	UFUNCTION(BlueprintPure, Category = "Ivy")
	bool CanUndo() const { return !bGestureActive && UndoStack.Num() > 0; }

	UFUNCTION(BlueprintPure, Category = "Ivy")
	bool IsGrowing() const { return bGestureActive; }

	// --- Sap ---
	UFUNCTION(BlueprintCallable, Category = "Ivy|Sap")
	void AddSap(float Amount);

	UFUNCTION(BlueprintPure, Category = "Ivy|Sap")
	float GetSap() const { return Sap; }

	UFUNCTION(BlueprintPure, Category = "Ivy|Sap")
	float GetMaxSap() const;

	UFUNCTION(BlueprintPure, Category = "Ivy|Sap")
	bool HasSapForStep() const;

	/** Distance (cm) the plant can still grow with the current sap. -1 = unlimited. */
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
	int32 GetNumBranches() const { return BranchTips.Num(); }
	int32 GetBranchTip(int32 BranchId) const;

	static bool IsClimbable(const FHitResult& Hit);

	// --- Events ---
	FOnIvyNodeAdded OnNodeAdded;   // C++: incremental rendering
	FOnIvyRebuilt   OnRebuilt;     // C++: full rebuild (seed / undo)

	UPROPERTY(BlueprintAssignable, Category = "Ivy")
	FOnIvyChanged OnIvyChanged;

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
	bool FindPlacement(const FIvyNode& From, const FVector& Dir, FVector& OutLocation, FVector& OutNormal);
	void AddNode(const FIvyNode& From, const FVector& Location, const FVector& Normal, const FVector& Tangent, const FVector& Dir, bool bLit);
	void SetGrowingInLight(bool bInLight);
	void NotifyInteractables(const FVector& Location, const FVector& Dir);
	int32 FindStartNode(const FVector& Target, int32& OutBranch) const;
	FIvySnapshot MakeSnapshot() const;
	void ApplySnapshot(const FIvySnapshot& Snapshot);
	bool TraceIvy(const FVector& Start, const FVector& End, FHitResult& OutHit) const;

	TArray<FIvyNode> Nodes;
	TArray<int32> BranchTips;
	float Sap = 0.f;

	TArray<FIvySnapshot> UndoStack;
	FIvySnapshot PendingSnapshot;
	bool bGestureActive = false;
	bool bGestureDirty = false;

	int32 ActiveNode = INDEX_NONE;
	int32 ActiveBranch = INDEX_NONE;
	FVector GrowTarget = FVector::ZeroVector;

	UPROPERTY(Transient)
	TObjectPtr<UIvyLightSubsystem> LightSubsystem;

	bool bGrowingInLight = false;

	FTimerHandle GrowTimer;
};