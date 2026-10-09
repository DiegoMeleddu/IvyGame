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

class IVYGAME_API IIvyInteractable
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Ivy")
    void OnIvyGrowth(const FVector& Location, const FVector& GrowthDirection);

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Ivy")
    bool OnIvyBlocked(const FVector& Location, const FVector& GrowthDirection);

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Ivy")
    FIvySavedState IvySaveState();

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Ivy")
    void IvyRestoreState(const FIvySavedState& SavedState);
};