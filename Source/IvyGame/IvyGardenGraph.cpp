#include "IvyGardenGraph.h"
#include "IvyGrowthComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "Algo/Reverse.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

static TAutoConsoleVariable<bool> CVarIvyDebugGraph(
	TEXT("ivy.DebugGraph"),
	false,
	TEXT("Draws the junction edges of the ivy graph (yellow = same plant, cyan = between plants)."));

void UIvyGardenGraph::Initialize(UWorld* InWorld)
{
	World = InWorld;
}

// ------------------------------------------------------------------ Plants

void UIvyGardenGraph::AddPlant(UIvyGrowthComponent* Plant)
{
	if (!Plant || Plants.Contains(Plant))
	{
		return;
	}
	Plants.Add(Plant);

	// The plant pointer travels as payload, so the handler knows which plant grew
	Plant->OnNodeAdded.AddUObject(this, &UIvyGardenGraph::HandleNodeAdded, Plant);
	Plant->OnRebuilt.AddUObject(this, &UIvyGardenGraph::HandleRebuilt);

	Rebuild();
}

void UIvyGardenGraph::RemovePlant(UIvyGrowthComponent* Plant)
{
	if (!Plant)
	{
		return;
	}
	Plant->OnNodeAdded.RemoveAll(this);
	Plant->OnRebuilt.RemoveAll(this);

	if (Plants.Remove(Plant) > 0)
	{
		Rebuild();
	}
}

void UIvyGardenGraph::HandleNodeAdded(int32 LocalIndex, UIvyGrowthComponent* Plant)
{
	if (AddNodeToGraph(Plant, LocalIndex) != INDEX_NONE)
	{
		OnGraphChanged.Broadcast();
	}
}

void UIvyGardenGraph::HandleRebuilt()
{
	Rebuild();
}

void UIvyGardenGraph::Rebuild()
{
	// Seed or undo: full rebuild (rare, simplicity wins). Global ids are reassigned.
	NodeRefs.Reset();
	LocalToGlobal.Reset();
	Adjacency.Reset();
	Grid.Reset();

	for (const TObjectPtr<UIvyGrowthComponent>& Plant : Plants)
	{
		if (!Plant)
		{
			continue;
		}
		const int32 NumNodes = Plant->GetNumNodes();
		for (int32 i = 0; i < NumNodes; ++i)
		{
			AddNodeToGraph(Plant, i);
		}
	}
	OnGraphChanged.Broadcast();
}

// ------------------------------------------------------------------ Building

int32 UIvyGardenGraph::AddNodeToGraph(UIvyGrowthComponent* Plant, int32 LocalIndex)
{
	if (!Plant || !Plant->GetNodes().IsValidIndex(LocalIndex))
	{
		return INDEX_NONE;
	}
	const FIvyNode& Node = Plant->GetNodes()[LocalIndex];
	const AActor* Owner = Plant->GetOwner();

	TArray<int32>& Map = LocalToGlobal.FindOrAdd(Plant);
	while (Map.Num() <= LocalIndex)
	{
		Map.Add(INDEX_NONE);
	}

	FNodeRef Ref;
	Ref.Plant = Plant;
	Ref.LocalIndex = LocalIndex;
	const int32 Id = NodeRefs.Add(Ref);
	Map[LocalIndex] = Id;
	Adjacency.AddDefaulted();

	// 1) Tree edge: node <-> parent (same plant)
	int32 ParentId = INDEX_NONE;
	if (Node.Parent != INDEX_NONE && Map.IsValidIndex(Node.Parent))
	{
		ParentId = Map[Node.Parent];
		if (ParentId != INDEX_NONE)
		{
			AddEdge(Id, ParentId);
		}
	}

	// 2) Junction edges: nearby nodes of other branches or other plants, with nothing in between
	TArray<int32> Candidates;
	GatherInRadius(Node.Location, JunctionRadius, Candidates);

	int32 Added = 0;
	for (const int32 Candidate : Candidates)
	{
		if (Added >= MaxJunctionsPerNode)
		{
			break;
		}
		if (Candidate == Id || Candidate == ParentId || Adjacency[Id].Contains(Candidate))
		{
			continue;
		}

		const FIvyNode* Other = GetNode(Candidate);
		const UIvyGrowthComponent* OtherPlant = NodeRefs[Candidate].Plant.Get();
		if (!Other || !OtherPlant)
		{
			continue;
		}

		const bool bSamePlant = OtherPlant == Plant;
		if (bSamePlant && Other->BranchId == Node.BranchId)
		{
			continue; // Same branch: already connected through the tree
		}
		if (!HasClearLine(Node, Owner, *Other, OtherPlant->GetOwner()))
		{
			continue;
		}

		AddEdge(Id, Candidate);
		++Added;

#if ENABLE_DRAW_DEBUG
		if (CVarIvyDebugGraph.GetValueOnGameThread())
		{
			DrawDebugLine(GetWorld(), Node.Location, Other->Location, bSamePlant ? FColor::Yellow : FColor::Cyan, false, 10.f, 0, 0.8f);
		}
#endif
	}

	// 3) Add to the spatial hash after the search, so the node doesn't find itself
	Grid.FindOrAdd(ToCell(Node.Location)).Add(Id);
	return Id;
}

void UIvyGardenGraph::AddEdge(int32 A, int32 B)
{
	Adjacency[A].AddUnique(B);
	Adjacency[B].AddUnique(A);
}

// ------------------------------------------------------------------ Node access

const FIvyNode* UIvyGardenGraph::GetNode(int32 Id) const
{
	if (!NodeRefs.IsValidIndex(Id))
	{
		return nullptr;
	}
	const FNodeRef& Ref = NodeRefs[Id];
	const UIvyGrowthComponent* Plant = Ref.Plant.Get();
	if (!Plant || !Plant->GetNodes().IsValidIndex(Ref.LocalIndex))
	{
		return nullptr;
	}
	return &Plant->GetNodes()[Ref.LocalIndex];
}

FVector UIvyGardenGraph::GetNodeLocation(int32 Id) const
{
	const FIvyNode* Node = GetNode(Id);
	return Node ? Node->Location : FVector::ZeroVector;
}

FVector UIvyGardenGraph::GetNodeNormal(int32 Id) const
{
	const FIvyNode* Node = GetNode(Id);
	return Node ? Node->Normal : FVector::UpVector;
}

// ------------------------------------------------------------------ Spatial queries

FIntVector UIvyGardenGraph::ToCell(const FVector& P) const
{
	return FIntVector(FMath::FloorToInt(P.X / CellSize),
		FMath::FloorToInt(P.Y / CellSize),
		FMath::FloorToInt(P.Z / CellSize));
}

void UIvyGardenGraph::GatherInRadius(const FVector& Point, float Radius, TArray<int32>& Out) const
{
	const FIntVector Center = ToCell(Point);
	const int32 R = FMath::CeilToInt(Radius / CellSize);
	const double RadiusSq = FMath::Square(static_cast<double>(Radius));

	for (int32 X = -R; X <= R; ++X)
	{
		for (int32 Y = -R; Y <= R; ++Y)
		{
			for (int32 Z = -R; Z <= R; ++Z)
			{
				const TArray<int32>* Cell = Grid.Find(Center + FIntVector(X, Y, Z));
				if (!Cell)
				{
					continue;
				}
				for (const int32 Id : *Cell)
				{
					const FIvyNode* Node = GetNode(Id);
					if (Node && FVector::DistSquared(Node->Location, Point) <= RadiusSq)
					{
						Out.Add(Id);
					}
				}
			}
		}
	}
}

bool UIvyGardenGraph::HasClearLine(const FIvyNode& A, const AActor* OwnerA, const FIvyNode& B, const AActor* OwnerB) const
{
	UWorld* WorldPtr = GetWorld();
	if (!WorldPtr)
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(IvyJunction), false);
	if (OwnerA)
	{
		Params.AddIgnoredActor(OwnerA);
	}
	if (OwnerB)
	{
		Params.AddIgnoredActor(OwnerB);
	}

	// Lift both points along their normals so the trace doesn't hit the walls they rest on
	FHitResult Hit;
	return !WorldPtr->LineTraceSingleByChannel(Hit, A.Location + A.Normal * 4.f, B.Location + B.Normal * 4.f, IvyChannels::Grow, Params);
}

int32 UIvyGardenGraph::FindNearestNode(const FVector& Location, float MaxDistance) const
{
	int32 Best = INDEX_NONE;
	double BestDistSq = MaxDistance > 0.f ? FMath::Square(static_cast<double>(MaxDistance)) : TNumericLimits<double>::Max();

	// Small radius -> spatial hash; large or unlimited radius -> linear search
	if (MaxDistance > 0.f && MaxDistance <= CellSize * 3.f)
	{
		TArray<int32> Candidates;
		GatherInRadius(Location, MaxDistance, Candidates);
		for (const int32 Id : Candidates)
		{
			const double DistSq = FVector::DistSquared(GetNodeLocation(Id), Location);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Best = Id;
			}
		}
	}
	else
	{
		for (int32 Id = 0; Id < NodeRefs.Num(); ++Id)
		{
			const FIvyNode* Node = GetNode(Id);
			if (!Node)
			{
				continue;
			}
			const double DistSq = FVector::DistSquared(Node->Location, Location);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Best = Id;
			}
		}
	}
	return Best;
}

// ------------------------------------------------------------------ Pathfinding

bool UIvyGardenGraph::FindPath(int32 Start, int32 Goal, TArray<int32>& OutPath) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Ivy_FindPath);
	OutPath.Reset();
	if (!IsValidNode(Start) || !IsValidNode(Goal))
	{
		return false;
	}

	const int32 Num = NodeRefs.Num();
	const FVector GoalPos = GetNodeLocation(Goal);

	TArray<float> G;    G.Init(TNumericLimits<float>::Max(), Num); // Cost from the start node
	TArray<int32> From; From.Init(INDEX_NONE, Num);                 // To rebuild the path
	TBitArray<> Closed(false, Num);

	struct FOpen
	{
		int32 Node;
		float F;
	};
	auto Less = [](const FOpen& A, const FOpen& B) { return A.F < B.F; };
	TArray<FOpen> Open;

	G[Start] = 0.f;
	Open.HeapPush({ Start, static_cast<float>(FVector::Dist(GetNodeLocation(Start), GoalPos)) }, Less);

	while (Open.Num() > 0)
	{
		FOpen Current;
		Open.HeapPop(Current, Less);
		if (Closed[Current.Node])
		{
			continue; // Stale heap entry
		}

		if (Current.Node == Goal)
		{
			for (int32 N = Goal; N != INDEX_NONE; N = From[N])
			{
				OutPath.Add(N);
			}
			Algo::Reverse(OutPath);
			return true;
		}
		Closed[Current.Node] = true;

		const FVector CurrentPos = GetNodeLocation(Current.Node);
		for (const int32 Next : Adjacency[Current.Node])
		{
			if (Closed[Next] || !IsValidNode(Next))
			{
				continue;
			}
			const FVector NextPos = GetNodeLocation(Next);
			const float NewG = G[Current.Node] + static_cast<float>(FVector::Dist(CurrentPos, NextPos));
			if (NewG < G[Next])
			{
				G[Next] = NewG;
				From[Next] = Current.Node;
				// Euclidean heuristic: admissible, never overestimates the real distance
				Open.HeapPush({ Next, NewG + static_cast<float>(FVector::Dist(NextPos, GoalPos)) }, Less);
			}
		}
	}
	return false;
}