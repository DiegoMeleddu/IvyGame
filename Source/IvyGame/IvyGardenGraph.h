#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "IvyTypes.h"
#include "IvyGardenGraph.generated.h"

class UIvyGrowthComponent;

DECLARE_MULTICAST_DELEGATE(FOnIvyGraphChanged);

/**
 * Navigation graph over the nodes of every plant in the level.
 * Tree edges follow each plant; junction edges connect nearby nodes of different branches or plants.
 * Node ids are global and stable while plants grow; a full rebuild (seed or undo) reassigns them.
 */
UCLASS()
class IVYGAME_API UIvyGardenGraph : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UWorld* InWorld);
	virtual UWorld* GetWorld() const override { return World.Get(); }

	void AddPlant(UIvyGrowthComponent* Plant);
	void RemovePlant(UIvyGrowthComponent* Plant);

	/** MaxDistance <= 0 = no limit. */
	int32 FindNearestNode(const FVector& Location, float MaxDistance) const;

	bool FindPath(int32 Start, int32 Goal, TArray<int32>& OutPath) const;

	bool IsValidNode(int32 Id) const { return GetNode(Id) != nullptr; }
	FVector GetNodeLocation(int32 Id) const;
	FVector GetNodeNormal(int32 Id) const;
	int32 GetNumNodes() const { return NodeRefs.Num(); }

	FOnIvyGraphChanged OnGraphChanged;

	/** Max distance to connect nodes of different branches or plants (cm). */
	float JunctionRadius = 15.f;

	int32 MaxJunctionsPerNode = 2;

	/** Spatial hash cell size (cm). */
	float CellSize = 50.f;

private:
	struct FNodeRef
	{
		TWeakObjectPtr<UIvyGrowthComponent> Plant;
		int32 LocalIndex = INDEX_NONE;
	};

	void HandleNodeAdded(int32 LocalIndex, UIvyGrowthComponent* Plant);
	void HandleRebuilt();
	void Rebuild();
	int32 AddNodeToGraph(UIvyGrowthComponent* Plant, int32 LocalIndex);
	void AddEdge(int32 A, int32 B);
	const FIvyNode* GetNode(int32 Id) const;
	void GatherInRadius(const FVector& Point, float Radius, TArray<int32>& Out) const;
	bool HasClearLine(const FIvyNode& A, const AActor* OwnerA, const FIvyNode& B, const AActor* OwnerB) const;
	FIntVector ToCell(const FVector& P) const;

	TWeakObjectPtr<UWorld> World;

	UPROPERTY()
	TArray<TObjectPtr<UIvyGrowthComponent>> Plants;

	TArray<FNodeRef> NodeRefs;                                      // Global id -> (plant, local index)
	TMap<const UIvyGrowthComponent*, TArray<int32>> LocalToGlobal;  // Plant -> local index -> global id
	TArray<TArray<int32>> Adjacency;                                // Global id -> neighbor ids
	TMap<FIntVector, TArray<int32>> Grid;                           // Spatial hash: cell -> global ids
};