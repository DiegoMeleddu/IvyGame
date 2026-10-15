#include "IvyGrowthComponent.h"
#include "IvyInteractable.h"
#include "IvyLightSubsystem.h"
#include "IvyGardenSubsystem.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogIvyGrowth, Log, All);

static TAutoConsoleVariable<int32> CVarIvyDebug(
	TEXT("ivy.Debug"),
	0,
	TEXT("Growth debug drawing. 0 = off, 1 = traces, nodes, target and detour candidates."),
	ECVF_Cheat);

namespace
{
	const FName NoIvyTag(TEXT("NoIvy"));

	/** Greedy steering is trusted only if the target is within ~45 degrees of the surface plane (in-plane / total). */
	constexpr double MinInPlaneRatio = 0.7;

	/** Directions tried around the surface normal when choosing a detour. */
	constexpr int32 NumDetourCandidates = 8;

	/** Moving the target farther than this many steps counts as a new target. */
	constexpr float TargetResetSteps = 6.f;

	/** A detour ends once the ivy is this many steps closer than when it started (and greedy steering is reliable). */
	constexpr float DetourExitSteps = 3.f;

	/** Score bonus (in steps) for continuing in the current direction: avoids zig-zag between re-plans. */
	constexpr float KeepDirectionBonusSteps = 1.f;

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
	ResetSteering();

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
	TrackedTarget = Target;
	ResetSteering();
	bGestureActive = true;

	GrowTick(); // Run the first step immediately for responsiveness
	GetWorld()->GetTimerManager().SetTimer(GrowTimer, this, &UIvyGrowthComponent::GrowTick, Settings->StepInterval, true);
	return true;
}

void UIvyGrowthComponent::SetGrowTarget(const FVector& Target)
{
	GrowTarget = Target;

	if (!Settings || FVector::DistSquared(Target, TrackedTarget) <= FMath::Square(static_cast<double>(Settings->StepLength * TargetResetSteps)))
	{
		return;
	}

	// A clearly new target: restart progress tracking, keep an active detour but re-plan it right away
	TrackedTarget = Target;
	RecentDistances.Reset();
	bDetourGaveUp = false;
	DetourStepsWithoutGain = 0;
	if (bDetouring && Nodes.IsValidIndex(ActiveNode))
	{
		const double Distance = FVector::Dist(Nodes[ActiveNode].Location, Target);
		DetourStartDistance = Distance;
		DetourBestDistance = Distance;
		StepsSinceReplan = DetourReplanSteps;
	}
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
	ResetSteering();

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
	ResetSteering();

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
		DrawDebugSphere(GetWorld(), GrowTarget, Settings->ArriveRadius, 12, bDetouring ? FColor::Orange : FColor::Yellow, false, Settings->StepInterval * 1.1f);
	}
#endif

	GrowStep();
}

bool UIvyGrowthComponent::GrowStep()
{
	// Sap is checked later: a step on a lit surface is free, and we only know
	// whether it is lit after computing where the new node goes
	if (!Nodes.IsValidIndex(ActiveNode) || bDetourGaveUp)
	{
		return false;
	}

	const FIvyNode From = Nodes[ActiveNode]; // Copy by value: adding to the array may reallocate it

	const FVector ToTarget = GrowTarget - From.Location;
	const double Distance = ToTarget.Size();
	if (Distance <= Settings->ArriveRadius)
	{
		return false; // Target reached
	}

	// 1) Greedy direction toward the target on this surface (fast path)
	bool bSteerValid = false;
	FVector Dir = ComputeSteeringDir(From, ToTarget, bSteerValid);

	// 2) Leave the detour once clearly closer and greedy steering is reliable again
	if (bDetouring && bSteerValid && Distance < DetourStartDistance - Settings->StepLength * DetourExitSteps)
	{
		bDetouring = false;
		RecentDistances.Reset();
	}

	if (!bDetouring)
	{
		RecentDistances.Add(Distance);
		if (RecentDistances.Num() > StuckWindow)
		{
			RecentDistances.RemoveAt(0);
		}

		if (!bSteerValid || IsStuck(Distance))
		{
			// Local minimum (loop, back-and-forth) or target off this surface: plan a way around
			if (!ChooseDetourDir(From, Dir))
			{
				bDetourGaveUp = true;
				return false;
			}
			BeginDetour(Distance);
		}
		else
		{
			// Deterministic random deviation, only while steering freely (detours stay predictable)
			FRandomStream Stream(Settings->RandomSeed + Nodes.Num() * 7919);
			const float WanderRad = FMath::DegreesToRadians(Stream.FRandRange(-Settings->Wander, Settings->Wander));
			Dir = FQuat(From.Normal, WanderRad).RotateVector(Dir);
			Dir = FVector::VectorPlaneProject(Dir, From.Normal).GetSafeNormal();
		}
	}
	else
	{
		// Give up only after many steps without getting closer (a long but useful detour is fine)
		if (Distance < DetourBestDistance - Settings->StepLength)
		{
			DetourBestDistance = Distance;
			DetourStepsWithoutGain = 0;
		}
		else if (++DetourStepsWithoutGain > MaxDetourSteps)
		{
			bDetourGaveUp = true; // Save sap: wait for a new target
			return false;
		}

		// Receding horizon: re-plan regularly, otherwise follow the surface straight
		if (++StepsSinceReplan >= DetourReplanSteps)
		{
			StepsSinceReplan = 0;
			if (!ChooseDetourDir(From, Dir))
			{
				bDetourGaveUp = true;
				return false;
			}
		}
		else
		{
			Dir = FVector::VectorPlaneProject(From.Tangent, From.Normal).GetSafeNormal();
		}
	}

	if (Dir.IsNearlyZero())
	{
		return false;
	}

	// 3) Find a valid placement for the new node
	FVector NewLocation, NewNormal;
	if (!FindPlacement(From, Dir, NewLocation, NewNormal, /*bAllowInteraction*/ true))
	{
		if (!bDetouring)
		{
			return false; // Counts as no progress: a detour starts once the window shows it
		}

		// The detour hit a dead end: re-plan from here
		StepsSinceReplan = 0;
		if (!ChooseDetourDir(From, Dir) || !FindPlacement(From, Dir, NewLocation, NewNormal, true))
		{
			bDetourGaveUp = true;
			return false;
		}
	}

	// 4) Compute the new tangent, with a fallback for degenerate cases
	const FVector NewTangent = ComputeTangent(Dir, From.Normal, NewNormal);

	// 5) Light: a step on a lit surface is free, otherwise it needs sap
	const bool bLit = IsSurfaceLit(NewLocation, NewNormal);
	if (!bLit && !HasSapForStep())
	{
		SetGrowingInLight(false);
		return false; // Out of sap and in the dark
	}

	AddNode(From, NewLocation, NewNormal, NewTangent, Dir, bLit);
	return true;
}

// ------------------------------------------------------------------ Steering

FVector UIvyGrowthComponent::ComputeSteeringDir(const FIvyNode& From, const FVector& ToTarget, bool& bOutValid) const
{
	bOutValid = false;

	const double TotalLength = ToTarget.Size();
	const FVector InPlane = FVector::VectorPlaneProject(ToTarget, From.Normal);
	if (TotalLength <= KINDA_SMALL_NUMBER || InPlane.Size() < TotalLength * MinInPlaneRatio)
	{
		return FVector::ZeroVector; // Target too far off this surface: the projection would mislead
	}

	const FVector Desired = InPlane.GetSafeNormal();
	FVector Current = FVector::VectorPlaneProject(From.Tangent, From.Normal).GetSafeNormal();
	if (Current.IsNearlyZero())
	{
		Current = Desired;
	}

	// Clamp the turning angle per step
	FVector Dir = Desired;
	const double AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Current | Desired, -1.0, 1.0)));
	if (AngleDeg > Settings->TurnRate)
	{
		const FQuat FullTurn = FQuat::FindBetweenNormals(Current, Desired);
		const float Alpha = static_cast<float>(Settings->TurnRate / AngleDeg);
		Dir = FQuat::Slerp(FQuat::Identity, FullTurn, Alpha).RotateVector(Current);
	}

	bOutValid = true;
	return Dir.GetSafeNormal();
}

bool UIvyGrowthComponent::IsStuck(double Distance) const
{
	if (RecentDistances.Num() < StuckWindow)
	{
		return false; // Not enough history yet
	}
	// Efficiency check: over the window, at least MinProgressRatio of the grown length must be real progress
	const double Gained = RecentDistances[0] - Distance;
	const double Required = static_cast<double>(StuckWindow - 1) * Settings->StepLength * MinProgressRatio;
	return Gained < Required;
}

void UIvyGrowthComponent::BeginDetour(double Distance)
{
	bDetouring = true;
	DetourStartDistance = Distance;
	DetourBestDistance = Distance;
	DetourStepsWithoutGain = 0;
	StepsSinceReplan = 0;
	RecentDistances.Reset();
}

bool UIvyGrowthComponent::ChooseDetourDir(const FIvyNode& From, FVector& OutDir) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Ivy_ChooseDetour);

	const double Distance = FVector::Dist(From.Location, GrowTarget);

	// Look-ahead grows with the distance: far targets need to "see" past the obstacle
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(static_cast<float>(Distance) / Settings->StepLength),
		DetourLookaheadSteps, FMath::Max(DetourLookaheadSteps, MaxLookaheadSteps));

	// Reference axis on the surface: toward the target if possible, else world up, else any axis
	FVector Axis = FVector::VectorPlaneProject(GrowTarget - From.Location, From.Normal).GetSafeNormal();
	if (Axis.IsNearlyZero())
	{
		Axis = FVector::VectorPlaneProject(FVector::UpVector, From.Normal).GetSafeNormal();
	}
	if (Axis.IsNearlyZero())
	{
		FVector Unused;
		From.Normal.FindBestAxisVectors(Axis, Unused);
	}

	double BestScore = TNumericLimits<double>::Max();
	bool bFound = false;

	// Candidate 0: keep the current direction (small bonus to avoid zig-zag between re-plans)
	const FVector KeepDir = FVector::VectorPlaneProject(From.Tangent, From.Normal).GetSafeNormal();
	if (!KeepDir.IsNearlyZero())
	{
		const double Score = SimulateStraight(From, KeepDir, Steps);
		if (Score < TNumericLimits<double>::Max())
		{
			BestScore = Score - Settings->StepLength * KeepDirectionBonusSteps;
			OutDir = KeepDir;
			bFound = true;
		}
	}

	// Candidates 1..N: evenly spaced directions around the surface normal
	for (int32 i = 0; i < NumDetourCandidates; ++i)
	{
		const float AngleRad = 2.f * PI * static_cast<float>(i) / static_cast<float>(NumDetourCandidates);
		const FVector Candidate = FQuat(From.Normal, AngleRad).RotateVector(Axis);

		const double Score = SimulateStraight(From, Candidate, Steps);
		if (Score < BestScore)
		{
			BestScore = Score;
			OutDir = Candidate;
			bFound = true;
		}
	}

#if ENABLE_DRAW_DEBUG
	if (bFound && IsIvyDebugOn())
	{
		DrawDebugDirectionalArrow(GetWorld(), From.Location, From.Location + OutDir * 30.f, 8.f, FColor::Magenta, false, 3.f, 0, 1.f);
	}
#endif
	return bFound;
}

double UIvyGrowthComponent::SimulateStraight(const FIvyNode& From, const FVector& StartDir, int32 Steps) const
{
	FIvyNode Node = From;
	double Closest = TNumericLimits<double>::Max();

	for (int32 i = 0; i < Steps; ++i)
	{
		const FVector Dir = (i == 0)
			? StartDir
			: FVector::VectorPlaneProject(Node.Tangent, Node.Normal).GetSafeNormal();
		if (Dir.IsNearlyZero())
		{
			break;
		}

		FVector Location, Normal;
		if (!FindPlacement(Node, Dir, Location, Normal, /*bAllowInteraction*/ false))
		{
			break; // Dead end: the candidate is scored on what it reached so far
		}

#if ENABLE_DRAW_DEBUG
		if (IsIvyDebugOn())
		{
			DrawDebugLine(GetWorld(), Node.Location, Location, FColor(255, 140, 0, 120), false, 1.f, 0, 0.3f);
		}
#endif

		Node.Tangent = ComputeTangent(Dir, Node.Normal, Normal);
		Node.Location = Location;
		Node.Normal = Normal;
		Closest = FMath::Min(Closest, FVector::Dist(Location, GrowTarget));

		if (Closest <= Settings->ArriveRadius)
		{
			break; // This route reaches the target: no need to look further
		}
	}
	return Closest; // Max if even the first step failed
}

FVector UIvyGrowthComponent::ComputeTangent(const FVector& Dir, const FVector& FromNormal, const FVector& NewNormal)
{
	FVector Tangent = FVector::VectorPlaneProject(Dir, NewNormal);
	if (Tangent.SizeSquared() < 0.09)
	{
		// Wrapping an edge: continue "over" it instead of stopping
		const FVector Fallback = (Dir | NewNormal) < 0.0 ? FromNormal : -FromNormal;
		Tangent = FVector::VectorPlaneProject(Fallback, NewNormal);
	}
	return Tangent.GetSafeNormal();
}

void UIvyGrowthComponent::ResetSteering()
{
	RecentDistances.Reset();
	DetourStartDistance = 0.0;
	DetourBestDistance = 0.0;
	DetourStepsWithoutGain = 0;
	StepsSinceReplan = 0;
	bDetouring = false;
	bDetourGaveUp = false;
}

// ------------------------------------------------------------------ Placement

bool UIvyGrowthComponent::FindPlacement(const FIvyNode& From, const FVector& Dir, FVector& OutLocation, FVector& OutNormal, bool bAllowInteraction) const
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
			// Pushing objects always requires sap, even in the light. Dry runs never push.
			if (bAllowInteraction && HasSapForStep() && IIvyInteractable::Execute_OnIvyBlocked(HitActor, Hit.ImpactPoint, Dir))
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
		DrawDebugPoint(GetWorld(), Location, 8.f, bDetouring ? FColor::Orange : FColor::Cyan, false, 3.f);
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