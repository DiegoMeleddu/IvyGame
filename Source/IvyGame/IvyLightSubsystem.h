#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "IvyLightSubsystem.generated.h"

class ULightComponent;

/**
 * Knows the gameplay lights of the current level (tag "IvyLight")
 * and answers whether a surface point is directly lit.
 * Never reads rendered lighting: same result on any machine and quality setting.
 */
UCLASS()
class IVYGAME_API UIvyLightSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** Rescans the level for gameplay lights (call it after spawning a new light). */
	UFUNCTION(BlueprintCallable, Category = "Ivy|Light")
	void RefreshLights();

	/** True if at least one active gameplay light reaches the point without obstacles. */
	UFUNCTION(BlueprintPure, Category = "Ivy|Light")
	bool IsPointLit(const FVector& Point, const FVector& SurfaceNormal, AActor* IgnoredActor = nullptr) const;

	UFUNCTION(BlueprintPure, Category = "Ivy|Light")
	int32 GetNumGameplayLights() const { return Lights.Num(); }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	bool IsLitBy(const ULightComponent* Light, const FVector& Point, const FVector& Normal, const AActor* IgnoredActor) const;

	UPROPERTY()
	TArray<TWeakObjectPtr<ULightComponent>> Lights;
};