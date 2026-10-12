#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "IvyPlayerController.generated.h"

class UInputMappingContext;
class UInputAction;
class UIvyGrowthComponent;

UCLASS()
class IVYGAME_API AIvyPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AIvyPlayerController();

	virtual void PlayerTick(float DeltaTime) override;

	UFUNCTION(BlueprintPure, Category = "Ivy")
	UIvyGrowthComponent* GetIvy() const { return Ivy.Get(); }

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	/** Called every frame with the current cursor state. Implement in Blueprint for cursor feedback. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Ivy")
	void OnCursorUpdated(bool bHit, FVector Location, FVector Normal, bool bCanGrow);

	/** Called once the level plant has been found. Implement in Blueprint to create the HUD. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Ivy")
	void OnIvyReady(UIvyGrowthComponent* IvyGrowth);

	UPROPERTY(EditDefaultsOnly, Category = "Ivy|Input")
	TObjectPtr<UInputMappingContext> IvyMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Ivy|Input")
	TObjectPtr<UInputAction> GrowAction;

	UPROPERTY(EditDefaultsOnly, Category = "Ivy|Input")
	TObjectPtr<UInputAction> UndoAction;

	UPROPERTY(EditDefaultsOnly, Category = "Ivy|Input")
	TObjectPtr<UInputAction> ResetAction;

	UPROPERTY(EditDefaultsOnly, Category = "Ivy|Camera")
	FName CameraTag = TEXT("IvyCamera");

private:
	void OnGrowStarted();
	void OnGrowStopped();
	void OnUndo();
	void OnReset();
	void FindLevelReferences();

	TWeakObjectPtr<UIvyGrowthComponent> Ivy;
	FHitResult CursorHit;
	bool bCursorHit = false;
};