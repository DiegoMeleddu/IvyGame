#include "IvyCreature.h"
#include "IvyGardenSubsystem.h"
#include "IvyGardenGraph.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<bool> CVarIvyDebugCreature(TEXT("ivy.DebugCreature"), false, TEXT("Draws the creature path, goal and mount radius"));

AIvyCreature::AIvyCreature()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComp = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(RootComp);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComp);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // Must not block the ivy or any trace
}

void AIvyCreature::BeginPlay()
{
	Super::BeginPlay();

	CurrentPos = GetActorLocation();
	CurrentUp = GetActorUpVector();
	SegStartPos = SegEndPos = CurrentPos;
	SegStartUp = SegEndUp = CurrentUp;

	// The graph covers every plant of the level
	if (UIvyGardenSubsystem* Garden = GetWorld()->GetSubsystem<UIvyGardenSubsystem>())
	{
		Graph = Garden->GetGraph();
		if (Graph)
		{
			Graph->OnGraphChanged.AddUObject(this, &AIvyCreature::MarkRepath);
		}
	}
}

void AIvyCreature::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Graph)
	{
		return;
	}

	// Event-driven repath, at most once every RepathInterval
	RepathCooldown -= DeltaSeconds;
	if (bRepathPending && RepathCooldown <= 0.f)
	{
		Repath();
		RepathCooldown = RepathInterval;
	}

	if (State == EIvyCreatureState::Moving)
	{
		TickMoving(DeltaSeconds);
	}

#if ENABLE_DRAW_DEBUG
	if (CVarIvyDebugCreature.GetValueOnGameThread())
	{
		FVector Prev = CurrentPos;
		for (int32 i = PathIndex; i < Path.Num(); ++i)
		{
			if (!Graph->IsValidNode(Path[i]))
			{
				break;
			}
			const FVector NodePos = Graph->GetNodeLocation(Path[i]);
			DrawDebugLine(GetWorld(), Prev, NodePos, FColor::Magenta, false, 0.f, 0, 1.f);
			Prev = NodePos;
		}
		if (GoalActor)
		{
			DrawDebugSphere(GetWorld(), GoalActor->GetActorLocation(), GoalRadius, 12, FColor::Cyan);
		}
		if (State == EIvyCreatureState::OffIvy)
		{
			DrawDebugSphere(GetWorld(), CurrentPos, MountRadius, 12, FColor::Orange);
		}
	}
#endif
}

// ------------------------------------------------------------------ Pathfinding

void AIvyCreature::Repath()
{
	bRepathPending = false;
	if (State == EIvyCreatureState::AtGoal || !GoalActor)
	{
		return;
	}

	// Not on the ivy (anymore): try to climb on
	if (!Graph->IsValidNode(CurrentNode))
	{
		CurrentNode = INDEX_NONE;
		TryMount();
		return;
	}

	// Mid-segment: finish it, the new path starts from the node we are already heading to
	const bool bMidSegment = State == EIvyCreatureState::Moving
		&& Path.IsValidIndex(PathIndex) && Graph->IsValidNode(Path[PathIndex]);
	const int32 StartNode = bMidSegment ? Path[PathIndex] : CurrentNode;

	// Best-effort goal: the node closest to the goal, even if not close enough yet
	const int32 GoalNode = Graph->FindNearestNode(GoalActor->GetActorLocation(), -1.f);

	TArray<int32> NewPath;
	if (GoalNode == INDEX_NONE || !Graph->FindPath(StartNode, GoalNode, NewPath))
	{
		if (!bMidSegment)
		{
			SetState(EIvyCreatureState::Waiting);
		}
		return;
	}

	Path = MoveTemp(NewPath);
	PathIndex = bMidSegment ? 0 : 1; // Path[0] is StartNode: if we are already on it, skip to the next

	if (!Path.IsValidIndex(PathIndex))
	{
		// Already on the best possible node
		Path.Reset();
		PathIndex = 0;
		CheckGoal();
		return;
	}

	if (!bMidSegment && !BeginSegment())
	{
		SetState(EIvyCreatureState::Waiting);
		return;
	}
	SetState(EIvyCreatureState::Moving);
}

void AIvyCreature::TryMount()
{
	const int32 Node = Graph->FindNearestNode(CurrentPos, MountRadius);
	if (Node == INDEX_NONE)
	{
		SetState(EIvyCreatureState::OffIvy);
		return;
	}
	Path = { Node };
	PathIndex = 0;
	if (BeginSegment())
	{
		SetState(EIvyCreatureState::Moving);
	}
}

// ------------------------------------------------------------------ Movement

bool AIvyCreature::BeginSegment()
{
	if (!Path.IsValidIndex(PathIndex) || !Graph->IsValidNode(Path[PathIndex]))
	{
		Path.Reset();
		PathIndex = 0;
		return false;
	}

	const int32 Target = Path[PathIndex];
	const FVector TargetNormal = Graph->GetNodeNormal(Target);

	SegStartPos = CurrentPos;
	SegStartUp = CurrentUp;
	SegEndPos = Graph->GetNodeLocation(Target) + TargetNormal * HeightOffset;
	SegEndUp = TargetNormal;
	SegLength = FMath::Max(static_cast<float>(FVector::Dist(SegStartPos, SegEndPos)), KINDA_SMALL_NUMBER);
	SegAlpha = 0.f;
	return true;
}

void AIvyCreature::TickMoving(float DeltaSeconds)
{
	float Remaining = Speed * DeltaSeconds;

	// The loop handles covering several segments in one frame
	while (Remaining > 0.f && Path.IsValidIndex(PathIndex))
	{
		const float Left = (1.f - SegAlpha) * SegLength;
		if (Remaining < Left)
		{
			SegAlpha += Remaining / SegLength;
			break;
		}

		// Node reached
		Remaining -= Left;
		SegAlpha = 1.f;
		CurrentPos = SegEndPos;
		CurrentUp = SegEndUp;
		CurrentNode = Path[PathIndex];
		++PathIndex;
		if (Path.IsValidIndex(PathIndex))
		{
			BeginSegment();
		}
	}

	CurrentPos = FMath::Lerp(SegStartPos, SegEndPos, SegAlpha);
	const FVector BlendedUp = FMath::Lerp(SegStartUp, SegEndUp, SegAlpha).GetSafeNormal();
	CurrentUp = BlendedUp.IsNearlyZero() ? SegEndUp : BlendedUp; // Opposite normals: avoid the zero vector

	ApplyTransform(DeltaSeconds);

	if (!Path.IsValidIndex(PathIndex))
	{
		OnPathFinished();
	}
}

void AIvyCreature::OnPathFinished()
{
	Path.Reset();
	PathIndex = 0;
	CheckGoal();
	// After climbing on the ivy, compute the path to the goal right away
	if (State == EIvyCreatureState::Waiting)
	{
		bRepathPending = true;
	}
}

void AIvyCreature::CheckGoal()
{
	const bool bAtGoal = GoalActor && FVector::Dist(CurrentPos, GoalActor->GetActorLocation()) <= GoalRadius;
	SetState(bAtGoal ? EIvyCreatureState::AtGoal : EIvyCreatureState::Waiting);
}

void AIvyCreature::ApplyTransform(float DeltaSeconds)
{
	// Forward = segment direction projected on the surface plane
	FVector Forward = FVector::VectorPlaneProject(SegEndPos - SegStartPos, CurrentUp).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::VectorPlaneProject(GetActorForwardVector(), CurrentUp).GetSafeNormal();
	}
	if (Forward.IsNearlyZero())
	{
		FVector Unused;
		CurrentUp.FindBestAxisVectors(Forward, Unused);
	}

	const FQuat TargetRot = FRotationMatrix::MakeFromZX(CurrentUp, Forward).ToQuat();
	const float Alpha = FMath::Clamp(DeltaSeconds * TurnSpeed, 0.f, 1.f);
	SetActorLocationAndRotation(CurrentPos, FQuat::Slerp(GetActorQuat(), TargetRot, Alpha));
}

void AIvyCreature::SetState(EIvyCreatureState NewState)
{
	if (State == NewState)
	{
		return;
	}
	State = NewState;
	OnStateChanged.Broadcast(State);
}

// ------------------------------------------------------------------ Undo

// Layout of FIvySavedState.Floats for the creature
namespace IvyCreatureSave
{
	constexpr int32 UpX = 0, UpY = 1, UpZ = 2, Node = 3, State = 4, Count = 5;
}

FIvySavedState AIvyCreature::IvySaveState_Implementation()
{
	FIvySavedState S;
	S.Transform = GetActorTransform();
	S.Floats.SetNum(IvyCreatureSave::Count);
	S.Floats[IvyCreatureSave::UpX] = static_cast<float>(CurrentUp.X);
	S.Floats[IvyCreatureSave::UpY] = static_cast<float>(CurrentUp.Y);
	S.Floats[IvyCreatureSave::UpZ] = static_cast<float>(CurrentUp.Z);
	S.Floats[IvyCreatureSave::Node] = static_cast<float>(CurrentNode); // Informative only, see restore
	S.Floats[IvyCreatureSave::State] = static_cast<float>(static_cast<uint8>(State));
	return S;
}

void AIvyCreature::IvyRestoreState_Implementation(const FIvySavedState& SavedState)
{
	const TArray<float>& F = SavedState.Floats;
	if (F.Num() < IvyCreatureSave::Count)
	{
		return;
	}

	CurrentPos = SavedState.Transform.GetLocation();
	CurrentUp = FVector(F[IvyCreatureSave::UpX], F[IvyCreatureSave::UpY], F[IvyCreatureSave::UpZ]).GetSafeNormal();

	// Undo rebuilds the graph and reassigns global ids: the saved id may point to another node.
	// Re-attach from the saved position instead (Repath -> TryMount finds the nearest node).
	CurrentNode = INDEX_NONE;

	const EIvyCreatureState SavedCreatureState = static_cast<EIvyCreatureState>(FMath::RoundToInt(F[IvyCreatureSave::State]));

	Path.Reset();
	PathIndex = 0;
	SegStartPos = SegEndPos = CurrentPos;
	SegStartUp = SegEndUp = CurrentUp;
	SegAlpha = 1.f;

	SetActorTransform(SavedState.Transform);
	// A path in progress is no longer valid: it will be recomputed after the graph rebuild
	SetState(SavedCreatureState == EIvyCreatureState::Moving ? EIvyCreatureState::Waiting : SavedCreatureState);
	bRepathPending = true;
}