#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IvyTypes.h"
#include "IvyInteractable.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UIvyInteractable : public UInterface
{
	GENERATED_BODY()
}; 

/**
 * Implemented in Blueprint by puzzle objects.
 * For collision: IvyInteract = Overlap to receive OnIvyGrowth,
 * IvyGrow = Block to receive OnIvyBlocked.
 */

class IVYGAME_API IIvyInteractable
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintImplementableEvent, Category = "Ivy")
	void OnIvyGrowth(FVector NodeLocation, FVector GrowDirection);

	UFUNCTION(BlueprintImplementableEvent, Category = "Ivy")
	bool OnIvyBlocked(FVector HitLocation, FVector PushDirection);

	UFUNCTION(BlueprintImplementableEvent, Category = "Ivy")
	FIvySavedState IvySaveState();

	UFUNCTION(BlueprintImplementableEvent, Category = "Ivy")
	void IvyRestoreState(const FIvySavedState& State);
};