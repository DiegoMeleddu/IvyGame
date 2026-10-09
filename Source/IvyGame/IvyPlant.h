#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IvyPlant.generated.h"

class UIvyGrowthComponent;
class UProceduralMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UIvyGraphComponent;

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
	UIvyGraphComponent* GetGraph() const { return Graph; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ivy")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ivy")
	TObjectPtr<UIvyGrowthComponent> Growth;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) 
	TObjectPtr<UIvyGraphComponent> Graph;

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

	/** Probabilità che un nodo non abbia la foglia. */
	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves|Variation", meta = (ClampMin = "0", ClampMax = "1"))
	float LeafSkipChance = 0.15f;

	/** Probabilità che una foglia stia sullo stesso lato della precedente. */
	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves|Variation", meta = (ClampMin = "0", ClampMax = "1"))
	float LeafSideFlipChance = 0.12f;

	/** 0 = foglia esattamente sul nodo, 1 = può scivolare fino al nodo precedente. */
	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves|Variation", meta = (ClampMin = "0", ClampMax = "1"))
	float LeafSpacingJitter = 0.7f;

	/** Variazione della lunghezza del picciolo (0.3 = ±30%). */
	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves|Variation", meta = (ClampMin = "0", ClampMax = "1"))
	float LeafPetioleJitter = 0.35f;

	/** Variazione casuale della direzione della lamina, indipendente dal picciolo. */
	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves|Variation", meta = (ClampMin = "0", Units = "deg"))
	float LeafBladeJitter = 12.f;

	/** Distanza tra i livelli delle foglie: evita lo z-fighting tra foglie sovrapposte. */
	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves|Variation", meta = (ClampMin = "0", Units = "cm"))
	float LeafLayerStep = 0.25f;

	/** Numero di livelli: le foglie vicine (indici consecutivi) finiscono su livelli diversi. */
	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves|Variation", meta = (ClampMin = "1", ClampMax = "8"))
	int32 LeafLayerCount = 4;

	/** Inclinazione casuale della lamina rispetto al muro. */
	UPROPERTY(EditAnywhere, Category = "Ivy|Leaves|Variation", meta = (ClampMin = "0", ClampMax = "30", Units = "deg"))
	float LeafTiltJitter = 8.f;

	// --- Misc ---
	UPROPERTY(EditAnywhere, Category = "Ivy")
	TObjectPtr<UMaterialParameterCollection> IvyMPC;

	UPROPERTY(EditAnywhere, Category = "Ivy", meta = (Units = "cm"))
	float SeedTraceDistance = 300.f;

private:
	void HandleNodeAdded(int32 NodeIndex);
	void HandleRebuilt();
	void BuildBranch(int32 BranchId);
	void EnsureBranchArrays(int32 BranchId);
	UMaterialInstanceDynamic* GetOrCreateBranchMID(int32 BranchId);
	bool ComputeLeafTransform(int32 NodeIndex, FTransform& OutTransform, float& OutRandom) const;
	void AddLeaf(const FTransform& Transform, float BirthTime, float Random, bool bMarkDirty);
	float GetStemGrowSpeed() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> BranchMIDs;

	TArray<float> BranchShownLength;
	TArray<float> BranchTargetLength;
};