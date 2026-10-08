// IvyPlant.h
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

protected:
	virtual void BeginPlay() override;

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