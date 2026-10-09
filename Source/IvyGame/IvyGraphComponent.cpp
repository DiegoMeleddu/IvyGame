#include "IvyGraphComponent.h"
#include "IvyGrowthComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "Algo/Reverse.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

static TAutoConsoleVariable<bool> CVarIvyDebugGraph(TEXT("ivy.DebugGraph"), false, TEXT("Disegna le giunzioni del grafo dell'edera"));

UIvyGraphComponent::UIvyGraphComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UIvyGraphComponent::BeginPlay()
{
    Super::BeginPlay();
    Growth = GetOwner()->FindComponentByClass<UIvyGrowthComponent>();
    if (!ensureMsgf(Growth, TEXT("IvyGraphComponent richiede un IvyGrowthComponent sullo stesso attore"))) return;

    Growth->OnNodeAdded.AddUObject(this, &UIvyGraphComponent::HandleNodeAdded);
    Growth->OnRebuilt.AddUObject(this, &UIvyGraphComponent::HandleRebuilt);
    HandleRebuilt(); 
}

void UIvyGraphComponent::HandleNodeAdded(int32 NodeIndex)
{
    AddNodeToGraph(NodeIndex);
    OnGraphChanged.Broadcast();
}

void UIvyGraphComponent::HandleRebuilt()
{
    // After an “undo”: complete reconstruction (a rare occurrence—simplicity wins)
    Adjacency.Reset();
    Grid.Reset();
    const int32 Num = Growth->GetNodes().Num();
    for (int32 i = 0; i < Num; ++i) AddNodeToGraph(i);
    OnGraphChanged.Broadcast();
}

void UIvyGraphComponent::AddNodeToGraph(int32 NodeIndex)
{
    const TArray<FIvyNode>& Nodes = Growth->GetNodes();
    const FIvyNode& N = Nodes[NodeIndex];

    Adjacency.SetNum(NodeIndex + 1);

    // 1) Tree arc: node ↔ parent
    if (N.Parent != INDEX_NONE) AddEdge(NodeIndex, N.Parent);

    // 2) Connecting arches: adjacent nodes of OTHER branches, with no walls in between
    TArray<int32> Candidates;
    GatherInRadius(N.Location, JunctionRadius, Candidates);

    int32 Added = 0;
    for (int32 C : Candidates)
    {
        if (Added >= MaxJunctionsPerNode) break;
        if (C == NodeIndex || C == N.Parent || Nodes[C].BranchId == N.BranchId) continue;
        if (Adjacency[NodeIndex].Contains(C)) continue;
        if (!HasClearLine(N, Nodes[C])) continue;

        AddEdge(NodeIndex, C);
        ++Added;

#if ENABLE_DRAW_DEBUG
        if (CVarIvyDebugGraph.GetValueOnGameThread())
            DrawDebugLine(GetWorld(), N.Location, Nodes[C].Location, FColor::Yellow, false, 10.f, 0, 0.8f);
#endif
    }

    // 3) Placing it in the spatial grid (after the search, so I don't find myself)
    Grid.FindOrAdd(ToCell(N.Location)).Add(NodeIndex);
}

void UIvyGraphComponent::AddEdge(int32 A, int32 B)
{
    Adjacency[A].AddUnique(B);
    Adjacency[B].AddUnique(A);
}

FIntVector UIvyGraphComponent::ToCell(const FVector& P) const
{
    return FIntVector(FMath::FloorToInt(P.X / CellSize),
        FMath::FloorToInt(P.Y / CellSize),
        FMath::FloorToInt(P.Z / CellSize));
}

void UIvyGraphComponent::GatherInRadius(const FVector& Point, float Radius, TArray<int32>& Out) const
{
    const TArray<FIvyNode>& Nodes = Growth->GetNodes();
    const FIntVector Center = ToCell(Point);
    const int32 R = FMath::CeilToInt(Radius / CellSize);
    const float R2 = FMath::Square(Radius);

    for (int32 X = -R; X <= R; ++X)
        for (int32 Y = -R; Y <= R; ++Y)
            for (int32 Z = -R; Z <= R; ++Z)
            {
                if (const TArray<int32>* Cell = Grid.Find(Center + FIntVector(X, Y, Z)))
                {
                    for (int32 Index : *Cell)
                        if (FVector::DistSquared(Nodes[Index].Location, Point) <= R2) Out.Add(Index);
                }
            }
}

bool UIvyGraphComponent::HasClearLine(const FIvyNode& A, const FIvyNode& B) const
{
    // I raise the points along the normal so they don't hit the wall they're on
    FCollisionQueryParams Q(SCENE_QUERY_STAT(IvyJunction), false, GetOwner());
    FHitResult Hit;
    return !GetWorld()->LineTraceSingleByChannel(Hit, A.Location + A.Normal * 4.f,
        B.Location + B.Normal * 4.f, ECC_IvyGrow, Q);
}

int32 UIvyGraphComponent::FindNearestNode(const FVector& Location, float MaxDistance) const
{
    if (!Growth) return INDEX_NONE;
    const TArray<FIvyNode>& Nodes = Growth->GetNodes();

    int32 Best = INDEX_NONE;
    float BestD2 = MaxDistance > 0.f ? FMath::Square(MaxDistance) : TNumericLimits<float>::Max();

    // Small radius → grid; large or unlimited radius → linear search
    if (MaxDistance > 0.f && MaxDistance <= CellSize * 3.f)
    {
        TArray<int32> Candidates;
        GatherInRadius(Location, MaxDistance, Candidates);
        for (int32 i : Candidates)
        {
            const float D2 = FVector::DistSquared(Nodes[i].Location, Location);
            if (D2 < BestD2) { BestD2 = D2; Best = i; }
        }
    }
    else
    {
        for (int32 i = 0; i < Adjacency.Num(); ++i)
        {
            const float D2 = FVector::DistSquared(Nodes[i].Location, Location);
            if (D2 < BestD2) { BestD2 = D2; Best = i; }
        }
    }
    return Best;
}

bool UIvyGraphComponent::FindPath(int32 Start, int32 Goal, TArray<int32>& OutPath) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(Ivy_FindPath);
    OutPath.Reset();
    if (!IsValidNode(Start) || !IsValidNode(Goal)) return false;

    const TArray<FIvyNode>& Nodes = Growth->GetNodes();
    const int32 Num = Adjacency.Num();
    const FVector GoalPos = Nodes[Goal].Location;

    TArray<float> G;    G.Init(TNumericLimits<float>::Max(), Num);  // costo dal nodo di partenza
    TArray<int32> From; From.Init(INDEX_NONE, Num);                  // per ricostruire il percorso
    TBitArray<> Closed(false, Num);

    struct FOpen { int32 Node; float F; };
    auto Less = [](const FOpen& A, const FOpen& B) { return A.F < B.F; };
    TArray<FOpen> Open;

    G[Start] = 0.f;
    Open.HeapPush({ Start, FVector::Dist(Nodes[Start].Location, GoalPos) }, Less);

    while (Open.Num() > 0)
    {
        FOpen Current;
        Open.HeapPop(Current, Less);
        if (Closed[Current.Node]) continue;   // voce obsoleta nella heap

        if (Current.Node == Goal)
        {
            for (int32 N = Goal; N != INDEX_NONE; N = From[N]) OutPath.Add(N);
            Algo::Reverse(OutPath);
            return true;
        }
        Closed[Current.Node] = true;

        for (int32 Next : Adjacency[Current.Node])
        {
            if (Closed[Next]) continue;
            const float NewG = G[Current.Node] + FVector::Dist(Nodes[Current.Node].Location, Nodes[Next].Location);
            if (NewG < G[Next])
            {
                G[Next] = NewG;
                From[Next] = Current.Node;
                // Euristica euclidea
                Open.HeapPush({ Next, NewG + FVector::Dist(Nodes[Next].Location, GoalPos) }, Less);
            }
        }
    }
    return false;
}