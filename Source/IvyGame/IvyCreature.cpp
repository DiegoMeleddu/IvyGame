#include "IvyCreature.h"
#include "IvyPlant.h"
#include "IvyGraphComponent.h"
#include "IvyGrowthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<bool> CVarIvyDebugCreature(TEXT("ivy.DebugCreature"), false, TEXT("Disegna il percorso della creatura"));

AIvyCreature::AIvyCreature()
{
    PrimaryActorTick.bCanEverTick = true;

    RootComp = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(RootComp);

    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    Mesh->SetupAttachment(RootComp);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // non deve bloccare l'edera né le trace
}

void AIvyCreature::BeginPlay()
{
    Super::BeginPlay();
    CurrentPos = GetActorLocation();
    CurrentUp = GetActorUpVector();
    SegStartPos = SegEndPos = CurrentPos;
    SegStartUp = SegEndUp = CurrentUp;

    if (AIvyPlant* Plant = Cast<AIvyPlant>(UGameplayStatics::GetActorOfClass(this, AIvyPlant::StaticClass())))
    {
        Growth = Plant->GetGrowth();
        Graph = Plant->GetGraph();
        if (Graph) Graph->OnGraphChanged.AddUObject(this, &AIvyCreature::MarkRepath);
    }
}

void AIvyCreature::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!Graph || !Growth) return;

    // Ricalcolo a eventi, ma al massimo una volta ogni RepathInterval
    RepathCooldown -= DeltaSeconds;
    if (bRepathPending && RepathCooldown <= 0.f)
    {
        Repath();
        RepathCooldown = RepathInterval;
    }

    if (State == EIvyCreatureState::Moving) TickMoving(DeltaSeconds);

#if ENABLE_DRAW_DEBUG
    if (CVarIvyDebugCreature.GetValueOnGameThread())
    {
        const TArray<FIvyNode>& Nodes = Growth->GetNodes();
        FVector Prev = CurrentPos;
        for (int32 i = PathIndex; i < Path.Num(); ++i)
        {
            if (!Nodes.IsValidIndex(Path[i])) break;
            DrawDebugLine(GetWorld(), Prev, Nodes[Path[i]].Location, FColor::Magenta, false, 0.f, 0, 1.f);
            Prev = Nodes[Path[i]].Location;
        }
        if (GoalActor) DrawDebugSphere(GetWorld(), GoalActor->GetActorLocation(), GoalRadius, 12, FColor::Cyan);
        if (State == EIvyCreatureState::OffIvy) DrawDebugSphere(GetWorld(), CurrentPos, MountRadius, 12, FColor::Orange);
    }
#endif
}

// ---------- Pathfinding ----------

void AIvyCreature::Repath()
{
    bRepathPending = false;
    if (State == EIvyCreatureState::AtGoal || !GoalActor) return;

    // Non sono (più) sull'edera: provo a salirci
    if (!Graph->IsValidNode(CurrentNode))
    {
        CurrentNode = INDEX_NONE;
        TryMount();
        return;
    }

    // Se sono a metà segmento, lo completo: il nuovo percorso parte dal nodo verso cui sto già andando
    const bool bMidSegment = State == EIvyCreatureState::Moving &&
        Path.IsValidIndex(PathIndex) && Graph->IsValidNode(Path[PathIndex]);
    const int32 StartNode = bMidSegment ? Path[PathIndex] : CurrentNode;

    // Obiettivo "best effort": il nodo più vicino all'obiettivo, anche se non ancora abbastanza vicino
    const int32 GoalNode = Graph->FindNearestNode(GoalActor->GetActorLocation(), -1.f);

    TArray<int32> NewPath;
    if (GoalNode == INDEX_NONE || !Graph->FindPath(StartNode, GoalNode, NewPath))
    {
        if (!bMidSegment) SetState(EIvyCreatureState::Waiting);
        return;
    }

    Path = MoveTemp(NewPath);
    PathIndex = bMidSegment ? 0 : 1;   // Path[0] è StartNode: se ci sono già sopra, salto al successivo

    if (!Path.IsValidIndex(PathIndex))
    {
        // Sono già sul nodo migliore possibile
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
    if (BeginSegment()) SetState(EIvyCreatureState::Moving);
}

// ---------- Movimento ----------

bool AIvyCreature::BeginSegment()
{
    const TArray<FIvyNode>& Nodes = Growth->GetNodes();
    if (!Path.IsValidIndex(PathIndex) || !Nodes.IsValidIndex(Path[PathIndex]))
    {
        Path.Reset();
        PathIndex = 0;
        return false;
    }

    const FIvyNode& Target = Nodes[Path[PathIndex]];
    SegStartPos = CurrentPos;
    SegStartUp = CurrentUp;
    SegEndPos = Target.Location + Target.Normal * HeightOffset;
    SegEndUp = Target.Normal;
    SegLength = FMath::Max(FVector::Dist(SegStartPos, SegEndPos), KINDA_SMALL_NUMBER);
    SegAlpha = 0.f;
    return true;
}

void AIvyCreature::TickMoving(float DeltaSeconds)
{
    float Remaining = Speed * DeltaSeconds;

    // Il while gestisce il caso in cui in un frame si percorrono più segmenti
    while (Remaining > 0.f && Path.IsValidIndex(PathIndex))
    {
        const float Left = (1.f - SegAlpha) * SegLength;
        if (Remaining < Left)
        {
            SegAlpha += Remaining / SegLength;
            break;
        }

        // Nodo raggiunto
        Remaining -= Left;
        SegAlpha = 1.f;
        CurrentPos = SegEndPos;
        CurrentUp = SegEndUp;
        CurrentNode = Path[PathIndex];
        ++PathIndex;
        if (Path.IsValidIndex(PathIndex)) BeginSegment();
    }

    CurrentPos = FMath::Lerp(SegStartPos, SegEndPos, SegAlpha);
    const FVector BlendedUp = FMath::Lerp(SegStartUp, SegEndUp, SegAlpha).GetSafeNormal();
    CurrentUp = BlendedUp.IsNearlyZero() ? SegEndUp : BlendedUp;   // normali opposte: evito il vettore nullo

    ApplyTransform(DeltaSeconds);

    if (!Path.IsValidIndex(PathIndex)) OnPathFinished();
}

void AIvyCreature::OnPathFinished()
{
    Path.Reset();
    PathIndex = 0;
    CheckGoal();
    // Dopo essere salita sull'edera, calcola subito il percorso verso l'obiettivo
    if (State == EIvyCreatureState::Waiting) bRepathPending = true;
}

void AIvyCreature::CheckGoal()
{
    const bool bAtGoal = GoalActor && FVector::Dist(CurrentPos, GoalActor->GetActorLocation()) <= GoalRadius;
    SetState(bAtGoal ? EIvyCreatureState::AtGoal : EIvyCreatureState::Waiting);
}

void AIvyCreature::ApplyTransform(float DeltaSeconds)
{
    // Avanti = direzione del segmento proiettata sul piano della superficie
    FVector Forward = FVector::VectorPlaneProject(SegEndPos - SegStartPos, CurrentUp).GetSafeNormal();
    if (Forward.IsNearlyZero())
        Forward = FVector::VectorPlaneProject(GetActorForwardVector(), CurrentUp).GetSafeNormal();
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
    if (State == NewState) return;
    State = NewState;
    OnStateChanged.Broadcast(State);
}

// ---------- Undo ----------

void AIvyCreature::IvySaveState_Implementation()
{
    FCreatureSnapshot S;
    S.Pos = CurrentPos;
    S.Up = CurrentUp;
    S.Rot = GetActorQuat();
    S.Node = CurrentNode;
    S.State = State;
    History.Add(S);
}

void AIvyCreature::IvyRestoreState_Implementation()
{
    if (History.IsEmpty()) return;
    const FCreatureSnapshot S = History.Pop();

    CurrentPos = S.Pos;
    CurrentUp = S.Up;
    CurrentNode = S.Node;
    Path.Reset();
    PathIndex = 0;
    SegStartPos = SegEndPos = CurrentPos;
    SegStartUp = SegEndUp = CurrentUp;
    SegAlpha = 1.f;

    SetActorLocationAndRotation(CurrentPos, S.Rot);
    // Un percorso "in corso" non è più valido: lo ricalcolo dopo la ricostruzione del grafo
    SetState(S.State == EIvyCreatureState::Moving ? EIvyCreatureState::Waiting : S.State);
    bRepathPending = true;
}