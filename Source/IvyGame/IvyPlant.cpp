// IvyPlant.cpp
#include "IvyPlant.h"
#include "IvyGrowthComponent.h"
#include "IvyTypes.h"
#include "ProceduralMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Engine/World.h"

namespace
{
	const FName GrowLengthParam(TEXT("GrowLength"));
	const FName BranchLengthParam(TEXT("BranchLength"));
	const FName IvyTimeParam(TEXT("IvyTime"));
}

AIvyPlant::AIvyPlant()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	Growth = CreateDefaultSubobject<UIvyGrowthComponent>(TEXT("Growth"));

	StemMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("StemMesh"));
	StemMesh->SetupAttachment(SceneRoot);
	StemMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StemMesh->bUseComplexAsSimpleCollision = false;
	StemMesh->SetCastShadow(true);

	Leaves = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Leaves"));
	Leaves->SetupAttachment(SceneRoot);
	Leaves->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Leaves->NumCustomDataFloats = 2; // 0 = BirthTime, 1 = Random
}

void AIvyPlant::BeginPlay()
{
	Super::BeginPlay(); // Also initializes the growth component

	Growth->OnNodeAdded.AddUObject(this, &AIvyPlant::HandleNodeAdded);
	Growth->OnRebuilt.AddUObject(this, &AIvyPlant::HandleRebuilt);

	// Find the starting surface below the actor
	const FVector Up = GetActorUpVector();
	const FVector Start = GetActorLocation() + Up * 10.f;
	const FVector End = GetActorLocation() - Up * SeedTraceDistance;

	FVector SeedLocation = GetActorLocation();
	FVector SeedNormal = Up;

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IvySeed), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, IvyChannels::Grow, Params))
	{
		SeedLocation = Hit.ImpactPoint;
		SeedNormal = Hit.ImpactNormal;
	}

	const float Offset = Growth->Settings ? Growth->Settings->SurfaceOffset : 2.f;
	Growth->PlantSeed(SeedLocation + SeedNormal * Offset, SeedNormal, GetActorForwardVector());
}

void AIvyPlant::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IvyMPC)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(this, IvyMPC, IvyTimeParam, static_cast<float>(GetWorld()->GetTimeSeconds()));
	}

	const float Speed = GetStemGrowSpeed();
	for (int32 b = 0; b < BranchShownLength.Num(); ++b)
	{
		float& Shown = BranchShownLength[b];
		const float Target = BranchTargetLength[b];
		if (FMath::IsNearlyEqual(Shown, Target))
		{
			continue;
		}
		Shown = FMath::FInterpConstantTo(Shown, Target, DeltaSeconds, Speed);
		if (UMaterialInstanceDynamic* MID = BranchMIDs.IsValidIndex(b) ? BranchMIDs[b].Get() : nullptr)
		{
			MID->SetScalarParameterValue(GrowLengthParam, Shown);
		}
	}
}

float AIvyPlant::GetStemGrowSpeed() const
{
	if (StemGrowSpeed > 0.f)
	{
		return StemGrowSpeed;
	}
	const UIvyGrowthSettings* S = Growth->Settings;
	return S ? (S->StepLength / FMath::Max(S->StepInterval, 0.01f)) * 1.5f : 200.f;
}

// ------------------------------------------------------------------ Event handlers

void AIvyPlant::HandleNodeAdded(int32 NodeIndex)
{
	const TArray<FIvyNode>& Nodes = Growth->GetNodes();
	if (!Nodes.IsValidIndex(NodeIndex))
	{
		return;
	}
	const FIvyNode& Node = Nodes[NodeIndex];

	EnsureBranchArrays(Node.BranchId);
	BuildBranch(Node.BranchId);
	BranchTargetLength[Node.BranchId] = Node.DistanceAlongBranch;

	if (UMaterialInstanceDynamic* MID = BranchMIDs[Node.BranchId].Get())
	{
		MID->SetScalarParameterValue(BranchLengthParam, Node.DistanceAlongBranch);
	}

	FTransform LeafTransform;
	float Random = 0.f;
	if (ComputeLeafTransform(NodeIndex, LeafTransform, Random))
	{
		AddLeaf(LeafTransform, Node.BirthTime, Random, true);
	}
}

void AIvyPlant::HandleRebuilt()
{
	StemMesh->ClearAllMeshSections();
	Leaves->ClearInstances();

	const TArray<FIvyNode>& Nodes = Growth->GetNodes();
	const int32 NumBranches = Growth->GetNumBranches();

	BranchShownLength.SetNumZeroed(NumBranches);
	BranchTargetLength.SetNumZeroed(NumBranches);

	for (int32 b = 0; b < NumBranches; ++b)
	{
		EnsureBranchArrays(b);
		BuildBranch(b);

		const int32 Tip = Growth->GetBranchTip(b);
		const float Length = Nodes.IsValidIndex(Tip) ? Nodes[Tip].DistanceAlongBranch : 0.f;
		BranchTargetLength[b] = Length;
		BranchShownLength[b] = Length; // Show full length immediately, no animation

		if (UMaterialInstanceDynamic* MID = BranchMIDs[b].Get())
		{
			MID->SetScalarParameterValue(GrowLengthParam, Length);
			MID->SetScalarParameterValue(BranchLengthParam, Length);
		}
	}

	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		FTransform LeafTransform;
		float Random = 0.f;
		if (ComputeLeafTransform(i, LeafTransform, Random))
		{
			AddLeaf(LeafTransform, Nodes[i].BirthTime, Random, false);
		}
	}
	Leaves->MarkRenderStateDirty();
}

// ------------------------------------------------------------------ Stem

void AIvyPlant::EnsureBranchArrays(int32 BranchId)
{
	if (BranchShownLength.Num() <= BranchId)
	{
		BranchShownLength.SetNumZeroed(BranchId + 1);
		BranchTargetLength.SetNumZeroed(BranchId + 1);
	}
	if (BranchMIDs.Num() <= BranchId)
	{
		BranchMIDs.SetNum(BranchId + 1);
	}
}

UMaterialInstanceDynamic* AIvyPlant::GetOrCreateBranchMID(int32 BranchId)
{
	if (!StemMaterial)
	{
		return nullptr;
	}
	EnsureBranchArrays(BranchId);
	if (!BranchMIDs[BranchId])
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(StemMaterial, this);
		MID->SetScalarParameterValue(GrowLengthParam, 0.f);
		BranchMIDs[BranchId] = MID;
	}
	return BranchMIDs[BranchId];
}

void AIvyPlant::BuildBranch(int32 BranchId)
{
	const TArray<FIvyNode>& Nodes = Growth->GetNodes();

	// Collect the branch nodes in order
	TArray<int32> Points;
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		if (Nodes[i].BranchId == BranchId)
		{
			Points.Add(i);
		}
	}

	// Include the parent node as the starting point, if any
	bool bHasJoint = false;
	if (Points.Num() > 0 && Nodes[Points[0]].Parent != INDEX_NONE)
	{
		Points.Insert(Nodes[Points[0]].Parent, 0);
		bHasJoint = true;
	}

	if (Points.Num() < 2)
	{
		StemMesh->ClearMeshSection(BranchId);
		return;
	}

	const int32 Segs = FMath::Max(3, RadialSegments);
	const int32 RingVerts = Segs + 1; // Extra vertex for the UV seam
	const float BranchLength = Nodes[Points.Last()].DistanceAlongBranch;
	const float Base = (BranchId == 0) ? BaseRadius : BaseRadius * SideBranchScale;
	const FTransform& ActorTransform = GetActorTransform();

	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FProcMeshTangent> Tangents;
	TArray<int32> Triangles;

	Vertices.Reserve(Points.Num() * RingVerts);
	Normals.Reserve(Points.Num() * RingVerts);
	UVs.Reserve(Points.Num() * RingVerts);
	Tangents.Reserve(Points.Num() * RingVerts);
	Triangles.Reserve((Points.Num() - 1) * Segs * 6);

	for (int32 k = 0; k < Points.Num(); ++k)
	{
		const FIvyNode& Node = Nodes[Points[k]];
		const float Distance = (k == 0 && bHasJoint) ? 0.f : Node.DistanceAlongBranch;

		// Compute the stem direction from neighboring nodes
		const FVector Prev = Nodes[Points[FMath::Max(k - 1, 0)]].Location;
		const FVector Next = Nodes[Points[FMath::Min(k + 1, Points.Num() - 1)]].Location;
		FVector Forward = (Next - Prev).GetSafeNormal();
		if (Forward.IsNearlyZero())
		{
			Forward = Node.Tangent;
		}

		FVector Up = FVector::VectorPlaneProject(Node.Normal, Forward).GetSafeNormal();
		if (Up.IsNearlyZero())
		{
			FVector AxisB;
			Forward.FindBestAxisVectors(Up, AxisB);
		}
		const FVector Right = FVector::CrossProduct(Forward, Up);

		const float TaperAlpha = FMath::Clamp((BranchLength - Distance) / TaperLength, 0.f, 1.f);
		const float Radius = FMath::Lerp(TipRadius, Base, TaperAlpha);

		const FVector LocalCenter = ActorTransform.InverseTransformPosition(Node.Location);
		const FVector LocalForward = ActorTransform.InverseTransformVectorNoScale(Forward);

		for (int32 s = 0; s <= Segs; ++s)
		{
			const float Angle = 2.f * PI * static_cast<float>(s) / static_cast<float>(Segs);
			const FVector WorldDir = Up * FMath::Cos(Angle) + Right * FMath::Sin(Angle);
			const FVector LocalDir = ActorTransform.InverseTransformVectorNoScale(WorldDir);

			Vertices.Add(LocalCenter + LocalDir * Radius);
			Normals.Add(LocalDir);
			UVs.Add(FVector2D(Distance, static_cast<float>(s) / static_cast<float>(Segs))); // U = distance along the branch
			Tangents.Add(FProcMeshTangent(LocalForward, false));
		}
	}

	for (int32 k = 0; k < Points.Num() - 1; ++k)
	{
		for (int32 s = 0; s < Segs; ++s)
		{
			const int32 A = k * RingVerts + s;
			const int32 B = (k + 1) * RingVerts + s;
			const int32 C = k * RingVerts + s + 1;
			const int32 D = (k + 1) * RingVerts + s + 1;

			Triangles.Append({ A, B, C });
			Triangles.Append({ B, D, C });
		}
	}

	StemMesh->CreateMeshSection_LinearColor(BranchId, Vertices, Triangles, Normals, UVs,
		TArray<FLinearColor>(), Tangents, /*bCreateCollision*/ false);

	if (UMaterialInstanceDynamic* MID = GetOrCreateBranchMID(BranchId))
	{
		StemMesh->SetMaterial(BranchId, MID);
	}
}

// ------------------------------------------------------------------ Leaves

bool AIvyPlant::ComputeLeafTransform(int32 NodeIndex, FTransform& OutTransform, float& OutRandom) const
{
	const TArray<FIvyNode>& Nodes = Growth->GetNodes();
	const FIvyNode& Node = Nodes[NodeIndex];

	if (Node.Parent == INDEX_NONE || NodeIndex % LeafEveryNNodes != 0)
	{
		return false;
	}

	const int32 Seed = (Growth->Settings ? Growth->Settings->RandomSeed : 0) + NodeIndex * 196613;
	FRandomStream Rand(Seed);

	// Alternate leaf side along the stem
	const float Side = ((NodeIndex / LeafEveryNNodes) % 2 == 0) ? 1.f : -1.f;
	const float YawDeg = Side * LeafSideAngle + Rand.FRandRange(-LeafAngleJitter, LeafAngleJitter);

	const FVector LeafDir = FQuat(Node.Normal, FMath::DegreesToRadians(YawDeg)).RotateVector(Node.Tangent);
	const FVector LeafX = FVector::VectorPlaneProject(LeafDir, Node.Normal).GetSafeNormal();
	if (LeafX.IsNearlyZero())
	{
		return false;
	}

	// Leaf mesh convention: pivot at the base, facing +Z, pointing +X
	const FRotator Rotation = FRotationMatrix::MakeFromXZ(LeafX, Node.Normal).Rotator();
	const float Scale = Rand.FRandRange(LeafScaleRange.X, LeafScaleRange.Y);

	OutTransform = FTransform(Rotation, Node.Location + Node.Normal * LeafLift, FVector(Scale));
	OutRandom = Rand.FRand();
	return true;
}

void AIvyPlant::AddLeaf(const FTransform& Transform, float BirthTime, float Random, bool bMarkDirty)
{
	const int32 Index = Leaves->AddInstance(Transform, /*bWorldSpace*/ true);
	const float CustomData[2] = { BirthTime, Random };
	Leaves->SetCustomData(Index, MakeArrayView(CustomData), bMarkDirty);
}