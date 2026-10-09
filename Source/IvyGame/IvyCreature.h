#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IvyInteractable.h"
#include "IvyCreature.generated.h"

class UIvyGraphComponent;
class UIvyGrowthComponent;

UENUM(BlueprintType)
enum class EIvyCreatureState : uint8
{
    OffIvy,    // non ancora sull'edera
    Moving,    // segue un percorso
    Waiting,   // sull'edera, ma l'obiettivo non è ancora raggiungibile
    AtGoal     // obiettivo raggiunto
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCreatureStateChanged, EIvyCreatureState, NewState);

UCLASS()
class IVYGAME_API AIvyCreature : public AActor, public IIvyInteractable
{
    GENERATED_BODY()
public:
    AIvyCreature();
    virtual void Tick(float DeltaSeconds) override;

    UFUNCTION(BlueprintPure, Category = "Creature") EIvyCreatureState GetState() const { return State; }

    // Per animazioni, suoni e PuzzleManager
    UPROPERTY(BlueprintAssignable, Category = "Creature") FOnCreatureStateChanged OnStateChanged;

    // IIvyInteractable: la creatura partecipa all'undo come ogni oggetto puzzle
    // IIvyInteractable
    virtual void OnIvyGrowth_Implementation(const FVector& Location, const FVector& GrowthDirection) override {}
    virtual bool OnIvyBlocked_Implementation(const FVector& Location, const FVector& GrowthDirection) override { return false; }
    virtual FIvySavedState IvySaveState_Implementation() override;
    virtual void IvyRestoreState_Implementation(const FIvySavedState& SavedState) override;

protected:
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> RootComp;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Mesh;

    UPROPERTY(EditInstanceOnly, Category = "Creature") TObjectPtr<AActor> GoalActor;
    UPROPERTY(EditAnywhere, Category = "Creature") float Speed = 60.f;          // cm/s
    UPROPERTY(EditAnywhere, Category = "Creature") float HeightOffset = 3.f;    // distanza dal fusto
    UPROPERTY(EditAnywhere, Category = "Creature") float MountRadius = 40.f;    // distanza per salire sull'edera
    UPROPERTY(EditAnywhere, Category = "Creature") float GoalRadius = 40.f;     // distanza per raggiungere l'obiettivo
    UPROPERTY(EditAnywhere, Category = "Creature") float TurnSpeed = 10.f;
    UPROPERTY(EditAnywhere, Category = "Creature") float RepathInterval = 0.2f;

private:
    struct FCreatureSnapshot
    {
        FVector Pos;
        FVector Up;
        FQuat Rot;
        int32 Node;
        EIvyCreatureState State;
    };

    void MarkRepath() { bRepathPending = true; }
    void Repath();
    void TryMount();
    bool BeginSegment();
    void TickMoving(float DeltaSeconds);
    void OnPathFinished();
    void CheckGoal();
    void ApplyTransform(float DeltaSeconds);
    void SetState(EIvyCreatureState NewState);

    UPROPERTY() TObjectPtr<UIvyGraphComponent> Graph;
    UPROPERTY() TObjectPtr<UIvyGrowthComponent> Growth;

    EIvyCreatureState State = EIvyCreatureState::OffIvy;
    TArray<int32> Path;
    int32 PathIndex = 0;            // indice in Path del nodo verso cui sto andando
    int32 CurrentNode = INDEX_NONE; // ultimo nodo raggiunto

    FVector CurrentPos = FVector::ZeroVector;
    FVector CurrentUp = FVector::UpVector;
    FVector SegStartPos, SegEndPos, SegStartUp, SegEndUp;
    float SegLength = 1.f;
    float SegAlpha = 1.f;

    bool bRepathPending = true;
    float RepathCooldown = 0.f;
};