#include "IvyGardenSubsystem.h"
#include "IvyGardenGraph.h"
#include "IvyGrowthComponent.h"
#include "IvyInteractable.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Algo/Sort.h"

DEFINE_LOG_CATEGORY_STATIC(LogIvyGarden, Log, All);

namespace
{
	constexpr int32 MaxUndoSteps = 50;
}

bool UIvyGardenSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Only in game and Play In Editor, not in the editor world
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UIvyGardenSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Graph = NewObject<UIvyGardenGraph>(this);
	Graph->Initialize(GetWorld());
}

// ------------------------------------------------------------------ Plants

void UIvyGardenSubsystem::RegisterPlant(UIvyGrowthComponent* Plant)
{
	if (!Plant || Plants.Contains(Plant))
	{
		return;
	}
	Plants.Add(Plant);

	// Designer-controlled order (SelectionOrder), then actor name for stability
	Algo::Sort(Plants, [](const TObjectPtr<UIvyGrowthComponent>& A, const TObjectPtr<UIvyGrowthComponent>& B)
		{
			if (A->SelectionOrder != B->SelectionOrder)
			{
				return A->SelectionOrder < B->SelectionOrder;
			}
			return GetNameSafe(A->GetOwner()) < GetNameSafe(B->GetOwner());
		});

	if (Graph)
	{
		Graph->AddPlant(Plant);
	}

	// The first plant with settings defines the shared sap budget of the level
	if (const UIvyGrowthSettings* PlantSettings = Plant->Settings)
	{
		if (!bSapInitialized)
		{
			MaxSap = PlantSettings->MaxSap;
			bSapInitialized = true;
			SetSap(MaxSap);
		}
		else if (!FMath::IsNearlyEqual(MaxSap, PlantSettings->MaxSap))
		{
			UE_LOG(LogIvyGarden, Warning, TEXT("%s has a different MaxSap (%.0f). Sap is shared: the level keeps %.0f. Use the same Settings asset on every plant."),
				*GetNameSafe(Plant->GetOwner()), PlantSettings->MaxSap, MaxSap);
		}
	}

	// Until the player chooses, the default selection is the first plant in order
	if (!bPlayerHasSelected)
	{
		SetSelected(Plants[0]);
	}
}

void UIvyGardenSubsystem::UnregisterPlant(UIvyGrowthComponent* Plant)
{
	if (Plants.Remove(Plant) == 0)
	{
		return;
	}
	if (Graph)
	{
		Graph->RemovePlant(Plant);
	}
	if (SelectedPlant == Plant)
	{
		SetSelected(Plants.Num() > 0 ? Plants[0].Get() : nullptr);
	}
}

TArray<UIvyGrowthComponent*> UIvyGardenSubsystem::GetPlants() const
{
	TArray<UIvyGrowthComponent*> Result;
	Result.Reserve(Plants.Num());
	for (const TObjectPtr<UIvyGrowthComponent>& Plant : Plants)
	{
		Result.Add(Plant);
	}
	return Result;
}

UIvyGrowthComponent* UIvyGardenSubsystem::FindPlantAtRoot(const FVector& Location, float Radius) const
{
	UIvyGrowthComponent* Best = nullptr;
	double BestDistSq = FMath::Square(static_cast<double>(Radius));

	for (const TObjectPtr<UIvyGrowthComponent>& Plant : Plants)
	{
		if (!Plant || Plant->GetNumNodes() == 0)
		{
			continue;
		}
		const double DistSq = FVector::DistSquared(Plant->GetNodes()[0].Location, Location);
		if (DistSq <= BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Plant;
		}
	}
	return Best;
}

// ------------------------------------------------------------------ Selection

void UIvyGardenSubsystem::SelectPlant(UIvyGrowthComponent* Plant)
{
	if (!Plant || !Plants.Contains(Plant))
	{
		return;
	}
	if (SelectedPlant && SelectedPlant->IsGrowing())
	{
		return; // Never switch plant in the middle of a gesture
	}
	bPlayerHasSelected = true;
	SetSelected(Plant);
}

void UIvyGardenSubsystem::SelectNextPlant(int32 Direction)
{
	if (Plants.Num() < 2)
	{
		return;
	}
	const int32 Current = FMath::Max(0, Plants.IndexOfByKey(SelectedPlant));
	const int32 Next = (Current + (Direction >= 0 ? 1 : -1) + Plants.Num()) % Plants.Num();
	SelectPlant(Plants[Next]);
}

void UIvyGardenSubsystem::SetSelected(UIvyGrowthComponent* Plant)
{
	if (SelectedPlant == Plant)
	{
		return;
	}
	SelectedPlant = Plant;
	OnSelectedPlantChanged.Broadcast(SelectedPlant);
}

// ------------------------------------------------------------------ Sap

void UIvyGardenSubsystem::AddSap(float Amount)
{
	SetSap(Sap + Amount);
}

void UIvyGardenSubsystem::ConsumeSap(float Amount)
{
	SetSap(Sap - Amount);
}

void UIvyGardenSubsystem::SetSap(float NewSap)
{
	Sap = FMath::Clamp(NewSap, 0.f, MaxSap);
	OnSapChanged.Broadcast(Sap, MaxSap);
}

// ------------------------------------------------------------------ Undo

void UIvyGardenSubsystem::BeginGesture()
{
	if (bGestureOpen)
	{
		EndGesture();
	}
	PendingSnapshot = MakeSnapshot();
	bGestureOpen = true;
	bGestureDirty = false;
	bPlayerHasSelected = true; // Playing locks the current selection
}

void UIvyGardenSubsystem::EndGesture()
{
	if (!bGestureOpen)
	{
		return;
	}
	bGestureOpen = false;

	if (HasChangedSince(PendingSnapshot))
	{
		UndoStack.Add(MoveTemp(PendingSnapshot));
		if (UndoStack.Num() > MaxUndoSteps)
		{
			UndoStack.RemoveAt(0);
		}
	}
	PendingSnapshot = FIvyGardenSnapshot();
}

bool UIvyGardenSubsystem::Undo()
{
	if (!CanUndo())
	{
		return false;
	}
	const FIvyGardenSnapshot Snapshot = UndoStack.Pop();
	ApplySnapshot(Snapshot);
	return true;
}

bool UIvyGardenSubsystem::HasChangedSince(const FIvyGardenSnapshot& Snapshot) const
{
	if (bGestureDirty || !FMath::IsNearlyEqual(Sap, Snapshot.Sap))
	{
		return true;
	}
	for (const FIvyPlantSnapshot& PlantSnapshot : Snapshot.Plants)
	{
		const UIvyGrowthComponent* Plant = PlantSnapshot.Plant.Get();
		if (Plant && Plant->GetNumNodes() != PlantSnapshot.NumNodes)
		{
			return true;
		}
	}
	return false;
}

FIvyGardenSnapshot UIvyGardenSubsystem::MakeSnapshot() const
{
	FIvyGardenSnapshot Snapshot;
	Snapshot.Sap = Sap;

	for (const TObjectPtr<UIvyGrowthComponent>& Plant : Plants)
	{
		if (Plant)
		{
			FIvyPlantSnapshot& PlantSnapshot = Snapshot.Plants.AddDefaulted_GetRef();
			PlantSnapshot.Plant = Plant;
			PlantSnapshot.NumNodes = Plant->GetNumNodes();
			PlantSnapshot.BranchTips = Plant->GetBranchTips();
		}
	}

	TArray<AActor*> Interactables;
	UGameplayStatics::GetAllActorsWithInterface(GetWorld(), UIvyInteractable::StaticClass(), Interactables);
	for (AActor* Actor : Interactables)
	{
		FIvyObjectRecord& Record = Snapshot.Objects.AddDefaulted_GetRef();
		Record.Actor = Actor;
		Record.State = IIvyInteractable::Execute_IvySaveState(Actor);
	}
	return Snapshot;
}

void UIvyGardenSubsystem::ApplySnapshot(const FIvyGardenSnapshot& Snapshot)
{
	// Plants first: their rebuild events let the graph and the creature update
	for (const FIvyPlantSnapshot& PlantSnapshot : Snapshot.Plants)
	{
		if (UIvyGrowthComponent* Plant = PlantSnapshot.Plant.Get())
		{
			Plant->RestoreToSnapshot(PlantSnapshot.NumNodes, PlantSnapshot.BranchTips);
		}
	}

	for (const FIvyObjectRecord& Record : Snapshot.Objects)
	{
		if (AActor* Actor = Record.Actor.Get())
		{
			IIvyInteractable::Execute_IvyRestoreState(Actor, Record.State);
		}
	}

	SetSap(Snapshot.Sap);
}

// ------------------------------------------------------------------ Queries

bool UIvyGardenSubsystem::IsPointShadowed(const FVector& Point, const FVector& LightDirection) const
{
	for (const TObjectPtr<UIvyGrowthComponent>& Plant : Plants)
	{
		if (Plant && Plant->IsPointShadowed(Point, LightDirection))
		{
			return true;
		}
	}
	return false;
}