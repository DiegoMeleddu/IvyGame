#include "IvyGrowthComponent.h"
#include "IvyInteractable.h"
#include "IvyLightSubsystem.h"
#include "IvyGardenSubsystem.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogIvyGrowth, Log, All);

static TAutoConsoleVariable<int32> CVarIvyDebug(
	TEXT("ivy.Debug"),
	0,
	TEXT("Growth debug drawing. 0 = off, 1 = traces, nodes and target."),
	ECVF_Cheat);

namespace
{
	const FName NoIvyTag(TEXT("NoIvy"));

	bool ImplementsIvyInteractable(const AActor* Actor)
	{
		return Actor && Actor->GetClass()->ImplementsInterface(UIvyInteractable::StaticClass());
	}

	bool IsIvyDebugOn()
	{
		return CVarIvyDebug.GetValueOnGameThread() > 0;
	}
}

UIvyGrowthComponent::UIvyGrowthComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // Updates are timer-driven
}

void UIvyGrowthComponent::BeginPlay()
{
	Super::BeginPlay();

	LightSubsystem = GetWorld()->GetSubsystem<UIvyLightSubsystem>();
	Garden = GetWorld()->GetSubsystem<UIvyGardenSubsystem>();

	if (!Settings)
	{
		UE_LOG(LogIvyGrowth, Error, TEXT("%s: Settings not assigned! Assign an IvyGrowthSettings asset on the component."), *GetNameSafe(GetOwner()));
	}

	if (Garden)
	{
		// Bind first, so the initial sap broadcast during registration reaches this plant too
		Garden->OnSapChanged.AddDynamic(this, &UIvyGrowthComponent::HandleGardenSapChanged);
		Garden->RegisterPlant(this);
	}
}

void UIvyGrowthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GrowTimer);
	}
	if (Garden)
	{
		Garden->OnSapChanged.RemoveDynamic(this, &UIvyGrowthComponent::HandleGardenSapChanged);
		Garden->UnregisterPlant(this);
	}
	Super::EndPlay(EndPlayReason);
}

void UIvyGrowthComponent::HandleGardenSapChanged(float CurrentSap, float MaxSap)
{
	OnSapChanged.Broadcast(CurrentSap, MaxSap);
}

// ------------------------------------------------------------------ Initialization

void UIvyGrowthComponent::PlantSeed(const FVector& Location, const FVector& Normal, const FVector& Tangent)
{
	GetWorld()->GetTimerManager().ClearTimer(GrowTimer);
	bGestureActive = false;

	Nodes.Reset();
	BranchTips.Reset();

	FIvyNode Root;
	Root.Location = Location;
	Root.Normal = Normal.GetSafeNormal();
	Root.Tangent = FVector::VectorPlaneProject(Tangent, Root.Normal).GetSafeNormal();
	if (Root.Tangent.IsNearlyZero())
	{
		FVector AxisA, AxisB;
		Root.Normal.FindBestAxisVectors(AxisA, AxisB);
		Root.Tangent = AxisA;
	}
	Root.Parent = INDEX_NONE;
	Root.BranchId = 0;
	Root.DistanceAlongBranch = 0.f;
	Root.BirthTime = static_cast<float>(GetWorld()->GetTimeSeconds());

	Nodes.Add(Root);
	BranchTips.Add(0);

	SetGrowingInLight(false);
	OnRebuilt.Broadcast();
	OnIvyChanged.Broadcast();
}

// ------------------------------------------------------------------ Input

bool UIvyGrowthComponent::BeginGesture(const FVector& Target)
{
	if (!Settings || Nodes.Num() == 0)
	{
		return false;
	}
	if (bGestureActive)
	{
		EndGesture();
	}

	ActiveNode = FindStartNode(Target, ActiveBranch);
	if (ActiveNode == INDEX_NONE)
	{
		return false;
	}

	GrowTarget = Target;
	bGestureActive = true;

	GrowTick(); // Run the first step immediately for responsiveness
	GetWorld()->GetTimerManager().SetTimer(GrowTimer, this, &UIvyGrowthComponent::GrowTick, Settings->StepInterval, true);
	return true;
}

void UIvyGrowthComponent::SetGrowTarget(const FVector& Target)
{
	GrowTarget = Target;
}

void UIvyGrowthComponent::EndGesture()
{
	if (!bGestureActive)
	{
		return;
	}

	GetWorld()->GetTimerManager().ClearTimer(GrowTimer);
	bGestureActive = false;
	ActiveNode = INDEX_NONE;
	ActiveBranch = INDEX_NONE;

	SetGrowingInLight(false);
}

bool UIvyGrowthComponent::Undo()
{
	return Garden && Garden->Undo();
}

bool UIvyGrowthComponent::CanUndo() const
{
	return !bGestureActive && Garden && Garden->CanUndo();
}

void UIvyGrowthComponent::RestoreToSnapshot(int32 NumNodes, const TArray<int32>& InBranchTips)
{
	GetWorld()->GetTimerManager().ClearTimer(GrowTimer);
	bGestureActive = false;
	ActiveNode = INDEX_NONE;
	ActiveBranch = INDEX_NONE;

	Nodes.SetNum(FMath::Clamp(NumNodes, 0, Nodes.Num()));
	BranchTips = InBranchTips;

	SetGrowingInLight(false);
	OnRebuilt.Broadcast();
	OnIvyChanged.Broadcast();
}

// ------------------------------------------------------------------ Sap

float UIvyGrowthComponent::GetSap() const
{
	return Garden ? Garden->GetSap() : 0.f;
}

float UIvyGrowthComponent::GetMaxSap() const
{
	return Garden ? Garden->GetMaxSap() : 0.f;
}

bool UIvyGrowthComponent::HasSapForStep() const
{
	return Settings && GetSap() >= Settings->SapCostPerStep;
}

float UIvyGrowthComponent::GetRemainingGrowthDistance() const
{
	if (!Settings)
	{
		return 0.f;
	}
	if (Settings->SapCostPerStep <= KINDA_SMALL_NUMBER)
	{
		return -1.f; // Free growth: no limit
	}
	const float Steps = FMath::FloorToFloat(GetSap() / Settings->SapCostPerStep);
	return Steps * Settings->StepLength;
}

void UIvyGrowthComponent::AddSap(float Amount)
{
	if (Garden)
	{
		Garden->AddSap(Amount);
	}
}

// ------------------------------------------------------------------ Light

bool UIvyGrowthComponent::IsSurfaceLit(const FVector& Location, const FVector& Normal) const
{
	return LightSubsystem && LightSubsystem->IsPointLit(Location, Normal, GetOwner());
}

void UIvyGrowthComponent::SetGrowingInLight(bool bInLight)
{
	if (bGrowingInLight == bInLight)
	{
		return;
	}
	bGrowingInLight = bInLight;
	OnLightGrowthChanged.Broadcast(bGrowingInLight);

	// Forward to the garden, so the HUD only needs to listen to one object
	if (Garden)
	{
		Garden->NotifyLightGrowthChanged(bGrowingInLight);
	}
}

// ------------------------------------------------------------------ Queries

bool UIvyGrowthComponent::IsPointShadowed(const FVector& Point, const FVector& LightDirection) const
{
	if (!Settings || Nodes.Num() == 0)
	{
		return false;
	}

	const FVector ToLight = (-LightDirection).GetSafeNormal();
	if (ToLight.IsNearlyZero())
	{
		return false;
	}

	const double RadiusSq = FMath::Square(static_cast<double>(Settings->ShadowRadius));
	for (const FIvyNode& Node : Nodes)
	{
		const FVector ToNode = Node.Location - Point;
		const double AlongRay = ToNode | ToLight;
		if (AlongRay <= 0.0)
		{
			continue; // Skip nodes that are not between the point and the light
		}
		const FVector Perp = ToNode - ToLight * AlongRay;
		if (Perp.SizeSquared() <= RadiusSq)
		{
			return true;
		}
	}
	return false;
}

int32 UIvyGrowthComponent::GetBranchTip(int32 BranchId) const
{
	return BranchTips.IsValidIndex(BranchId) ? BranchTips[BranchId] : INDEX_NONE;
}

bool UIvyGrowthComponent::IsClimbable(const FHitResult& Hit)
{
	if (!Hit.bBlockingHit)
	{
		return false;
	}
	if (const AActor* Actor = Hit.GetActor())
	{
		if (Actor->ActorHasTag(NoIvyTag) || ImplementsIvyInteractable(Actor))
		{
			return false;
		}
	}
	if (const UPrimitiveComponent* Comp = Hit.GetComponent())
	{
		if (Comp->ComponentHasTag(NoIvyTag))
		{
			return false;
		}
	}
	return true;
}

// ------------------------------------------------------------------ Growth

void UIvyGrowthComponent::GrowTick()
{
	if (!bGestureActive || !Settings)
	{
		return;
	}

#if ENABLE_DRAW_DEBUG
	if (IsIvyDebugOn())
	{
		DrawDebugSphere(GetWorld(), GrowTarget, Settings->ArriveRadius, 12, FColor::Yellow, false, Settings->StepInterval * 1.1f);
	}
#endif

	GrowStep();
}

bool UIvyGrowthComponent::GrowStep()
{
	// Sap is checked later: a step on a lit surface is free, and we only know
	// whether it is lit after computing where the new node goes
	if (!Nodes.IsValidIndex(ActiveNode))
	{
		return false;
	}

	const FIvyNode From = Nodes[ActiveNode]; // Copy by value: adding to the array may reallocate it

	const FVector ToTarget = GrowTarget - From.Location;
	if (ToTarget.SizeSquared() <= FMath::Square(static_cast<double>(Settings->ArriveRadius)))
	{
		return false; // Target reached
	}

	// 1) Compute the desired direction on the current surface plane
	const FVector Desired = FVector::VectorPlaneProject(ToTarget, From.Normal).GetSafeNormal();
	if (Desired.IsNearlyZero())
	{
		return false;
	}

	FVector Current = FVector::VectorPlaneProject(From.Tangent, From.Normal).GetSafeNormal();
	if (Current.IsNearlyZero())
	{
		Current = Desired;
	}

	// 2) Clamp the turning angle per step
	FVector Dir = Desired;
	const double AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Current | Desired, -1.0, 1.0)));
	if (AngleDeg > Settings->TurnRate)
	{
		const FQuat FullTurn = FQuat::FindBetweenNormals(Current, Desired);
		const float Alpha = static_cast<float>(Settings->TurnRate / AngleDeg);
		Dir = FQuat::Slerp(FQuat::Identity, FullTurn, Alpha).RotateVector(Current);
	}

	// 3) Apply deterministic random deviation
	FRandomStream Stream(Settings->RandomSeed + Nodes.Num() * 7919);
	const float WanderRad = FMath::DegreesToRadians(Stream.FRandRange(-Settings->Wander, Settings->Wander));
	Dir = FQuat(From.Normal, WanderRad).RotateVector(Dir);
	Dir = FVector::VectorPlaneProject(Dir, From.Normal).GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		return false;
	}

	// 4) Find a valid placement for the new node
	FVector NewLocation, NewNormal;
	if (!FindPlacement(From, Dir, NewLocation, NewNormal))
	{
		return false; // No valid placement this step
	}

	// 5) Compute the new tangent, with a fallback for degenerate cases
	FVector NewTangent = FVector::VectorPlaneProject(Dir, NewNormal);
	if (NewTangent.SizeSquared() < 0.09)
	{
		const FVector Fallback = (Dir | NewNormal) < 0.0 ? From.Normal : -From.Normal;
		NewTangent = FVector::VectorPlaneProject(Fallback, NewNormal);
	}
	NewTangent.Normalize();

	// 6) Light: a step on a lit surface is free, otherwise it needs sap
	const bool bLit = IsSurfaceLit(NewLocation, NewNormal);
	if (!bLit && !HasSapForStep())
	{
		SetGrowingInLight(false);
		return false; // Out of sap and in the dark
	}

	AddNode(From, NewLocation, NewNormal, NewTangent, Dir, bLit);
	return true;
}

bool UIvyGrowthComponent::FindPlacement(const FIvyNode& From, const FVector& Dir, FVector& OutLocation, FVector& OutNormal)
{
	const float Step = Settings->StepLength;
	const float Offset = Settings->SurfaceOffset;
	const float Probe = Settings->ProbeDepth;
	const FVector P = From.Location;
	const FVector N = From.Normal;

	FHitResult Hit;

	// A) Check for an obstacle ahead
	if (TraceIvy(P, P + Dir * Step, Hit))
	{
		AActor* HitActor = Hit.GetActor();
		if (ImplementsIvyInteractable(HitActor))
		{
			// Pushing objects always requires sap, even in the light
			if (HasSapForStep() && IIvyInteractable::Execute_OnIvyBlocked(HitActor, Hit.ImpactPoint, Dir))
			{
				if (Garden)
				{
					Garden->MarkGestureDirty(); // The level changed without growing
				}
			}
			return false;
		}
		if (!IsClimbable(Hit))
		{
			return false;
		}
		OutNormal = Hit.ImpactNormal;
		OutLocation = Hit.ImpactPoint + OutNormal * Offset;
		return true;
	}

	// B) Check for a surface below the next position
	const FVector Ahead = P + Dir * Step;
	if (TraceIvy(Ahead, Ahead - N * (Offset + Probe), Hit))
	{
		if (!IsClimbable(Hit))
		{
			return false;
		}
		OutNormal = Hit.ImpactNormal;
		OutLocation = Hit.ImpactPoint + OutNormal * Offset;
		return true;
	}

	// C) Check for a surface around an edge
	const FVector Below = Ahead - N * (Offset + Probe);
	if (TraceIvy(Below, Below - Dir * (Step + Offset + Probe), Hit))
	{
		if (!IsClimbable(Hit))
		{
			return false;
		}
		OutNormal = Hit.ImpactNormal;
		OutLocation = Hit.ImpactPoint + OutNormal * Offset;
		return true;
	}

	return false; // No surface found
}

void UIvyGrowthComponent::AddNode(const FIvyNode& From, const FVector& Location, const FVector& Normal, const FVector& Tangent, const FVector& Dir, bool bLit)
{
	FIvyNode Node;
	Node.Location = Location;
	Node.Normal = Normal;
	Node.Tangent = Tangent;
	Node.Parent = ActiveNode;
	Node.bLit = bLit;

	const float StepDistance = static_cast<float>(FVector::Dist(From.Location, Location));
	if (ActiveBranch == INDEX_NONE)
	{
		// Start a new branch
		ActiveBranch = BranchTips.Add(INDEX_NONE);
		Node.DistanceAlongBranch = StepDistance;
	}
	else
	{
		Node.DistanceAlongBranch = From.DistanceAlongBranch + StepDistance;
	}
	Node.BranchId = ActiveBranch;
	Node.BirthTime = static_cast<float>(GetWorld()->GetTimeSeconds());

	const int32 NewIndex = Nodes.Add(Node);
	BranchTips[ActiveBranch] = NewIndex;
	ActiveNode = NewIndex;

	// Only steps in the dark consume the shared sap (the garden broadcasts the change)
	if (!bLit && Garden)
	{
		Garden->ConsumeSap(Settings->SapCostPerStep);
	}
	SetGrowingInLight(bLit);

#if ENABLE_DRAW_DEBUG
	if (IsIvyDebugOn())
	{
		DrawDebugPoint(GetWorld(), Location, 8.f, FColor::Cyan, false, 3.f);
		DrawDebugLine(GetWorld(), Location, Location + Normal * 10.f, FColor::Blue, false, 3.f, 0, 0.5f);
	}
#endif

	OnNodeAdded.Broadcast(NewIndex);
	OnIvyChanged.Broadcast();

	NotifyInteractables(Location, Dir);
}

void UIvyGrowthComponent::NotifyInteractables(const FVector& Location, const FVector& Dir)
{
	if (Settings->InteractRadius <= 0.f)
	{
		return;
	}

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IvyInteract), false, GetOwner());
	GetWorld()->OverlapMultiByChannel(Overlaps, Location, FQuat::Identity, IvyChannels::Interact,
		FCollisionShape::MakeSphere(Settings->InteractRadius), Params);

	TSet<AActor*> Notified;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		if (ImplementsIvyInteractable(Actor) && !Notified.Contains(Actor))
		{
			Notified.Add(Actor);
			IIvyInteractable::Execute_OnIvyGrowth(Actor, Location, Dir);
		}
	}
}

int32 UIvyGrowthComponent::FindStartNode(const FVector& Target, int32& OutBranch) const
{
	int32 Best = INDEX_NONE;
	double BestDistSq = TNumericLimits<double>::Max();
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		const double DistSq = FVector::DistSquared(Nodes[i].Location, Target);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = i;
		}
	}
	if (Best == INDEX_NONE)
	{
		OutBranch = INDEX_NONE;
		return INDEX_NONE;
	}

	// Continue the existing branch if the node is close to its tip
	const FIvyNode& Node = Nodes[Best];
	const int32 Tip = BranchTips[Node.BranchId];
	if (Best == Tip || Nodes[Tip].DistanceAlongBranch - Node.DistanceAlongBranch <= Settings->StepLength * 2.f)
	{
		OutBranch = Node.BranchId;
		return Tip;
	}

	OutBranch = INDEX_NONE; // Otherwise, start a new branch
	return Best;
}

// ------------------------------------------------------------------ Tracing

bool UIvyGrowthComponent::TraceIvy(const FVector& Start, const FVector& End, FHitResult& OutHit) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IvyGrow), false, GetOwner());
	const bool bHit = GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, IvyChannels::Grow, Params);

#if ENABLE_DRAW_DEBUG
	if (IsIvyDebugOn())
	{
		DrawDebugLine(GetWorld(), Start, bHit ? FVector(OutHit.ImpactPoint) : End,
			bHit ? FColor::Green : FColor::Red, false, 1.f, 0, 0.3f);
	}
#endif
	return bHit;
}