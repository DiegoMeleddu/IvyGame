#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IvyTypes.h"
#include "IvyGraphComponent.generated.h"

class UIvyGrowthComponent;

DECLARE_MULTICAST_DELEGATE(FOnIvyGraphChanged);

UCLASS(ClassGroup = (Ivy), meta = (BlueprintSpawnableComponent))
class IVYGAME_API UIvyGraphComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UIvyGraphComponent();

    // MaxDistance <= 0 = nessun limite
    UFUNCTION(BlueprintCallable, Category = "Ivy|Graph")
    int32 FindNearestNode(const FVector& Location, float MaxDistance) const;

    UFUNCTION(BlueprintCallable, Category = "Ivy|Graph")
    bool FindPath(int32 Start, int32 Goal, TArray<int32>& OutPath) const;

    bool IsValidNode(int32 Index) const { return Adjacency.IsValidIndex(Index); }
    const TArray<int32>& GetNeighbors(int32 Index) const { return Adjacency[Index]; }

    FOnIvyGraphChanged OnGraphChanged;

protected:
    virtual void BeginPlay() override;

    // MaxDistance
    UPROPERTY(EditAnywhere, Category = "Ivy|Graph") float JunctionRadius = 15.f;
    UPROPERTY(EditAnywhere, Category = "Ivy|Graph") int32 MaxJunctionsPerNode = 2;
    // Dimension grid
    UPROPERTY(EditAnywhere, Category = "Ivy|Graph") float CellSize = 50.f;

private:
    void HandleNodeAdded(int32 NodeIndex);
    void HandleRebuilt();
    void AddNodeToGraph(int32 NodeIndex);
    void AddEdge(int32 A, int32 B);
    void GatherInRadius(const FVector& Point, float Radius, TArray<int32>& Out) const;
    bool HasClearLine(const FIvyNode& A, const FIvyNode& B) const;
    FIntVector ToCell(const FVector& P) const;

    UPROPERTY() TObjectPtr<UIvyGrowthComponent> Growth;

    TArray<TArray<int32>> Adjacency;           // List adiacenza
    TMap<FIntVector, TArray<int32>> Grid;
};