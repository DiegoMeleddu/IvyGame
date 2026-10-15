#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "IvyTypes.h"
#include "IvyGardenSubsystem.generated.h"

class UIvyGrowthComponent;
class UIvyGardenGraph;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGardenSapChanged, float, CurrentSap, float, MaxSap);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPlantSapChanged, UIvyGrowthComponent*, Plant, float, CurrentSap, float, MaxSap);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectedPlantChanged, UIvyGrowthComponent*, SelectedPlant);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGardenLightGrowthChanged, bool, bGrowingInLight);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGardenChanged);

/** Saved state of one plant for undo. Nodes are only appended, so their count is enough. */
USTRUCT()
struct FIvyPlantSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<UIvyGrowthComponent> Plant;

	UPROPERTY()
	int32 NumNodes = 0;

	UPROPERTY()
	TArray<int32> BranchTips;

	UPROPERTY()
	float Sap = 0.f;
};

/** Saved state of the whole level before a gesture: all plants (with their sap) and puzzle objects. */
USTRUCT()
struct FIvyGardenSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FIvyPlantSnapshot> Plants;

	UPROPERTY()
	TArray<FIvyObjectRecord> Objects;
};

/**
 * Coordinates every ivy plant of the level:
 * plant registry and selection, a single global undo stack,
 * the navigation graph shared by all plants and the events the HUD and puzzles listen to.
 * Each plant owns its sap; the garden only relays sap events.
 */
UCLASS()
class IVYGAME_API UIvyGardenSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// --- Plants ---
	void RegisterPlant(UIvyGrowthComponent* Plant);
	void UnregisterPlant(UIvyGrowthComponent* Plant);

	UFUNCTION(BlueprintPure, Category = "Ivy|Garden")
	TArray<UIvyGrowthComponent*> GetPlants() const;

	UFUNCTION(BlueprintPure, Category = "Ivy|Garden")
	int32 GetNumPlants() const { return Plants.Num(); }

	/** The plant whose root is closest to Location within Radius, or null. */
	UFUNCTION(BlueprintPure, Category = "Ivy|Garden")
	UIvyGrowthComponent* FindPlantAtRoot(const FVector& Location, float Radius) const;

	/** Navigation graph over all plants (used by the creature). */
	UIvyGardenGraph* GetGraph() const { return Graph; }

	/** Fired when any plant grows, is seeded or is restored by undo. */
	UPROPERTY(BlueprintAssignable, Category = "Ivy|Garden")
	FOnGardenChanged OnGardenChanged;

	// --- Selection ---
	UFUNCTION(BlueprintPure, Category = "Ivy|Garden")
	UIvyGrowthComponent* GetSelectedPlant() const { return SelectedPlant; }

	/** 0-based position of the selected plant in the selection order, or -1. */
	UFUNCTION(BlueprintPure, Category = "Ivy|Garden")
	int32 GetSelectedPlantIndex() const { return Plants.IndexOfByKey(SelectedPlant); }

	/** Ignored while the selected plant is growing. */
	UFUNCTION(BlueprintCallable, Category = "Ivy|Garden")
	void SelectPlant(UIvyGrowthComponent* Plant);

	/** Direction +1 = next plant, -1 = previous plant. Ignored while the selected plant is growing. */
	UFUNCTION(BlueprintCallable, Category = "Ivy|Garden")
	void SelectNextPlant(int32 Direction = 1);

	UPROPERTY(BlueprintAssignable, Category = "Ivy|Garden")
	FOnSelectedPlantChanged OnSelectedPlantChanged;

	// --- Sap (per plant) ---
	/** Sap of the selected plant. */
	UFUNCTION(BlueprintPure, Category = "Ivy|Garden")
	float GetSap() const;

	/** Max sap of the selected plant. */
	UFUNCTION(BlueprintPure, Category = "Ivy|Garden")
	float GetMaxSap() const;

	/** Distance (cm) the selected plant can still grow with its sap. -1 = unlimited. */
	UFUNCTION(BlueprintPure, Category = "Ivy|Garden")
	float GetRemainingGrowthDistance() const;

	/** Adds sap to the selected plant: it is always the one growing, so pickups reward the plant that touched them. */
	UFUNCTION(BlueprintCallable, Category = "Ivy|Garden")
	void AddSap(float Amount);

	/** Called by plants when their sap changes. */
	void NotifyPlantSapChanged(UIvyGrowthComponent* Plant);

	/** Sap of the SELECTED plant changed (also fired when the selection changes). Kept for simple HUDs. */
	UPROPERTY(BlueprintAssignable, Category = "Ivy|Garden")
	FOnGardenSapChanged OnSapChanged;

	/** Sap of ANY plant changed. Use this for one HUD row per plant. */
	UPROPERTY(BlueprintAssignable, Category = "Ivy|Garden")
	FOnPlantSapChanged OnPlantSapChanged;

	// --- Light ---
	/** Called by plants. Only the selected plant can grow, so this always describes the active plant. */
	void NotifyLightGrowthChanged(bool bGrowingInLight);

	UPROPERTY(BlueprintAssignable, Category = "Ivy|Garden")
	FOnGardenLightGrowthChanged OnLightGrowthChanged;

	// --- Global undo ---
	/** Call when the player starts a growth gesture: takes a snapshot of the whole level. */
	void BeginGesture();

	/** Call when the gesture ends: the snapshot is kept only if something changed. */
	void EndGesture();

	/** Call when a gesture changes the level without growing (e.g. an object was pushed). */
	void MarkGestureDirty() { bGestureDirty = true; }

	UFUNCTION(BlueprintCallable, Category = "Ivy|Garden")
	bool Undo();

	UFUNCTION(BlueprintPure, Category = "Ivy|Garden")
	bool CanUndo() const { return !bGestureOpen && UndoStack.Num() > 0; }

	// --- Queries ---
	/** True if any plant shadows the point. LightDirection = forward vector of the Directional Light. */
	UFUNCTION(BlueprintPure, Category = "Ivy|Garden")
	bool IsPointShadowed(const FVector& Point, const FVector& LightDirection) const;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	UFUNCTION()
	void HandlePlantChanged();

	void SetSelected(UIvyGrowthComponent* Plant);
	FIvyGardenSnapshot MakeSnapshot() const;
	void ApplySnapshot(const FIvyGardenSnapshot& Snapshot);
	bool HasChangedSince(const FIvyGardenSnapshot& Snapshot) const;

	UPROPERTY()
	TArray<TObjectPtr<UIvyGrowthComponent>> Plants;

	UPROPERTY()
	TObjectPtr<UIvyGrowthComponent> SelectedPlant;

	UPROPERTY()
	TObjectPtr<UIvyGardenGraph> Graph;

	/** Once the player picks a plant, registration order no longer changes the selection. */
	bool bPlayerHasSelected = false;

	TArray<FIvyGardenSnapshot> UndoStack;
	FIvyGardenSnapshot PendingSnapshot;
	bool bGestureOpen = false;
	bool bGestureDirty = false;
};