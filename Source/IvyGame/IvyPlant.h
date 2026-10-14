#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IvyPlant.generated.h"

class UIvyGrowthComponent;
class UIvyGardenSubsystem;
class UProceduralMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;

/**
 * Ivy rendering actor.
 * Place it with Z pointing away from the starting surface; X defines the initial growth direction.
 */
UCLASS()
class IVYGAME_API AIvyPlant : public AActor
{
	GENERATED_BODY()

public:
	AIvyPlant();

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "Ivy")
	UIvyGrowthComponent* GetGrowth() const { return Growth; }

	UFUNCTION(BlueprintPure, Category = "Ivy")
	bool IsSelected() const { return bIsSelected; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Called when this plant becomes (or stops being) the selected one. Implement in Blueprint for extra feedback. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Ivy")
	void OnSelectionChanged(bool bSelected);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ivy")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ivy")
	TObjectPtr<UIvyGrowthComponent> Growth;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ivy")
	TObjectPtr<UProceduralMeshComponent> StemMesh;

	/** Leaf instances. Mesh and material are set on this component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ivy")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Leaves;

	// --- Stem ---
	UPROPERTY(EditAnywhere, Category = "Ivy|Stem")
	TObjectPtr<UMaterialInterface> StemMaterial;

	UPROPERTY(EditAnywhere, Category = "Ivy|Stem", meta = (ClampMin = "0.1", Units = "cm"))
	float BaseRadius = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Ivy|Stem", meta = (ClampMin = "0.05", Units = "cm"))
	float TipRadius = 0.4f;

	/** Length over which the stem tapers toward the tip. */
	UPROPERTY(EditAnywhere, Category = "Ivy|Stem", meta = (ClampMin = "1", Units = "cm"))
	float TaperLength = 80.f;

	UPROPERTY(EditAnywhere, Category = "Ivy|Stem", meta = (ClampMin = "0.1", ClampMax = "1"))
	float SideBranchScale = 0.75f;

	UPROPERTY(EditAnywhere, Category = "Ivy|Stem", meta = (ClampMin = "3", ClampMax = "16"))
	int32 RadialSegments = 6;

	/** Stem reveal speed in cm/s. 0 = automatic. */
	UPROPERTY(EditAnywhere, Category = "Ivy|Stem", meta = (ClampMin = "0"))
	float StemGrowSpeed = 0.f;

	// --- Leaves ---
	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves", meta = (ClampMin = "1"))
	int32 LeafEveryNNodes = 2;

	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves")
	FVector2D LeafScaleRange = FVector2D(0.6, 1.1);

	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves", meta = (Units = "deg"))
	float LeafSideAngle = 55.f;

	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves", meta = (Units = "deg"))
	float LeafAngleJitter = 20.f;

	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves", meta = (Units = "cm"))
	float LeafLift = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves", meta = (ClampMin = "0", Units = "cm"))
	float LeafPetioleLength = 3.f;

	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves", meta = (Units = "deg"))
	float LeafBladeTurn = 20.f;

	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves", meta = (ClampMin = "0", ClampMax = "1"))
	float LeafDroop = 0.35f;

	/** Chance that a node has no leaf. */
	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves|Variation", meta = (ClampMin = "0", ClampMax = "1"))
	float LeafSkipChance = 0.15f;

	/** Chance that a leaf stays on the same side as the previous one. */
	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves|Variation", meta = (ClampMin = "0", ClampMax = "1"))
	float LeafSideFlipChance = 0.12f;

	/** 0 = leaf exactly on its node, 1 = it can slide back to the previous node. */
	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves|Variation", meta = (ClampMin = "0", ClampMax = "1"))
	float LeafSpacingJitter = 0.7f;

	/** Petiole length variation (0.3 = +/-30%). */
	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves|Variation", meta = (ClampMin = "0", ClampMax = "1"))
	float LeafPetioleJitter = 0.35f;

	/** Random blade direction variation, independent from the petiole. */
	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves|Variation", meta = (ClampMin = "0", Units = "deg"))
	float LeafBladeJitter = 12.f;

	/** Distance between leaf layers: avoids z-fighting between overlapping leaves. */
	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves|Variation", meta = (ClampMin = "0", Units = "cm"))
	float LeafLayerStep = 0.25f;

	/** Number of layers: nearby leaves (consecutive indices) end up on different layers. */
	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves|Variation", meta = (ClampMin = "1", ClampMax = "8"))
	int32 LeafLayerCount = 4;

	/** Random tilt of the blade relative to the wall. */
	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves|Variation", meta = (ClampMin = "0", ClampMax = "30", Units = "deg"))
	float LeafTiltJitter = 8.f;

	// --- Misc ---
	UPROPERTY(EditAnywhere, Category = "Ivy")
	TObjectPtr<UMaterialParameterCollection> IvyMPC;

	UPROPERTY(EditAnywhere, Category = "Ivy", meta = (Units = "cm"))
	float SeedTraceDistance = 300.f;

private:
	UFUNCTION()
	void HandleSelectedPlantChanged(UIvyGrowthComponent* SelectedPlant);

	void ApplySelection(bool bSelected);
	void HandleNodeAdded(int32 NodeIndex);
	void HandleRebuilt();
	void BuildBranch(int32 BranchId);
	void EnsureBranchArrays(int32 BranchId);
	UMaterialInstanceDynamic* GetOrCreateBranchMID(int32 BranchId);
	bool ComputeLeafTransform(int32 NodeIndex, FTransform& OutTransform, float& OutRandom) const;
	void AddLeaf(const FTransform& Transform, float BirthTime, float Random, float Lit, bool bMarkDirty);
	float GetStemGrowSpeed() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> BranchMIDs;

	UPROPERTY(Transient)
	TObjectPtr<UIvyGardenSubsystem> Garden;

	TArray<float> BranchShownLength;
	TArray<float> BranchTargetLength;

	bool bIsSelected = false;
};