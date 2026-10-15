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

	// Any change on any plant becomes a single garden-wide event
	Plant->OnIvyChanged.AddDynamic(this, &UIvyGardenSubsystem::HandlePlantChanged);

	if (Graph)
	{
		Graph->AddPlant(Plant);
	}

	// Until the player chooses, the default selection is the first plant in order
	if (!bPlayerHasSelected)
	{
		SetSelected(Plants[0]);
	}

	// Let listeners show the starting sap of the new plant
	NotifyPlantSapChanged(Plant);
	OnGardenChanged.Broadcast();
}

void UIvyGardenSubsystem::UnregisterPlant(UIvyGrowthComponent* Plant)
{
	if (Plants.Remove(Plant) == 0)
	{
		return;
	}
	Plant->OnIvyChanged.RemoveDynamic(this, &UIvyGardenSubsystem::HandlePlantChanged);

	if (Graph)
	{
		Graph->RemovePlant(Plant);
	}
	if (SelectedPlant == Plant)
	{
		SetSelected(Plants.Num() > 0 ? Plants[0].Get() : nullptr);
	}
	OnGardenChanged.Broadcast();
}

void UIvyGardenSubsystem::HandlePlantChanged()
{
	OnGardenChanged.Broadcast();
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

	// The "selected sap" changed too: simple HUDs bound to OnSapChanged stay correct
	OnSapChanged.Broadcast(GetSap(), GetMaxSap());
}

// ------------------------------------------------------------------ Sap

float UIvyGardenSubsystem::GetSap() const
{
	return SelectedPlant ? SelectedPlant->GetSap() : 0.f;
}

float UIvyGardenSubsystem::GetMaxSap() const
{
	return SelectedPlant ? SelectedPlant->GetMaxSap() : 0.f;
}

float UIvyGardenSubsystem::GetRemainingGrowthDistance() const
{
	return SelectedPlant ? SelectedPlant->GetRemainingGrowthDistance() : 0.f;
}

void UIvyGardenSubsystem::AddSap(float Amount)
{
	if (SelectedPlant)
	{
		SelectedPlant->AddSap(Amount);
	}
}

void UIvyGardenSubsystem::NotifyPlantSapChanged(UIvyGrowthComponent* Plant)
{
	if (!Plant)
	{
		return;
	}
	OnPlantSapChanged.Broadcast(Plant, Plant->GetSap(), Plant->GetMaxSap());

	if (Plant == SelectedPlant)
	{
		OnSapChanged.Broadcast(Plant->GetSap(), Plant->GetMaxSap());
	}
}

// ------------------------------------------------------------------ Light

void UIvyGardenSubsystem::NotifyLightGrowthChanged(bool bGrowingInLight)
{
	OnLightGrowthChanged.Broadcast(bGrowingInLight);
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
	if (bGestureDirty)
	{
		return true;
	}
	for (const FIvyPlantSnapshot& PlantSnapshot : Snapshot.Plants)
	{
		const UIvyGrowthComponent* Plant = PlantSnapshot.Plant.Get();
		if (Plant && (Plant->GetNumNodes() != PlantSnapshot.NumNodes || !FMath::IsNearlyEqual(Plant->GetSap(), PlantSnapshot.Sap)))
		{
			return true;
		}
	}
	return false;
}

FIvyGardenSnapshot UIvyGardenSubsystem::MakeSnapshot() const
{
	FIvyGardenSnapshot Snapshot;

	for (const TObjectPtr<UIvyGrowthComponent>& Plant : Plants)
	{
		if (Plant)
		{
			FIvyPlantSnapshot& PlantSnapshot = Snapshot.Plants.AddDefaulted_GetRef();
			PlantSnapshot.Plant = Plant;
			PlantSnapshot.NumNodes = Plant->GetNumNodes();
			PlantSnapshot.BranchTips = Plant->GetBranchTips();
			PlantSnapshot.Sap = Plant->GetSap();
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
	// Plants first: their rebuild events let the graph and the creature update.
	// Objects are restored last, so their saved state always wins over reactions to the plant rebuild.
	for (const FIvyPlantSnapshot& PlantSnapshot : Snapshot.Plants)
	{
		if (UIvyGrowthComponent* Plant = PlantSnapshot.Plant.Get())
		{
			Plant->RestoreToSnapshot(PlantSnapshot.NumNodes, PlantSnapshot.BranchTips);
			Plant->SetSap(PlantSnapshot.Sap);
		}
	}

	for (const FIvyObjectRecord& Record : Snapshot.Objects)
	{
		if (AActor* Actor = Record.Actor.Get())
		{
			IIvyInteractable::Execute_IvyRestoreState(Actor, Record.State);
		}
	}
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