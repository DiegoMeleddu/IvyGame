#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IvyTypes.h"
#include "IvyInteractable.generated.h"

UINTERFACE(BlueprintType)
class IVYGAME_API UIvyInteractable : public UInterface
{
    GENERATED_BODY()
};

/** Implemented by puzzle objects that react to the ivy (pushables, pickups, switches, flowers). */
class IVYGAME_API IIvyInteractable
{
    GENERATED_BODY()
public:
    /** Called when the ivy grows near the object (IvyInteract channel overlap). */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Ivy")
    void OnIvyGrowth(const FVector& Location, const FVector& GrowthDirection);

    /** Called when the ivy grows into the object. Return true if the object reacted (e.g. it was pushed). */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Ivy")
    bool OnIvyBlocked(const FVector& Location, const FVector& GrowthDirection);

    /**
     * True if the ivy may grow on this object's surface.
     * Default false: interactable objects are not climbable.
     * A pushable returns true once a push fails (e.g. blocked by a step).
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Ivy")
    bool CanIvyClimb();
    virtual bool CanIvyClimb_Implementation() { return false; }

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Ivy")
    FIvySavedState IvySaveState();

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Ivy")
    void IvyRestoreState(const FIvySavedState& SavedState);
};