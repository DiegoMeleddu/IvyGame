#include "IvyLightSubsystem.h"
#include "Components/LightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogIvyLight, Log, All);

static TAutoConsoleVariable<int32> CVarIvyDebugLight(
	TEXT("ivy.DebugLight"),
	0,
	TEXT("1 = draws the rays from each growth step to the gameplay lights."),
	ECVF_Cheat);

namespace IvyLight
{
	const FName Tag(TEXT("IvyLight"));
	constexpr double FacingThreshold = 0.05;  // The surface must face the light
	constexpr float RayLift = 3.f;            // cm: start the ray off the surface
	constexpr float SunRayLength = 50000.f;   // cm: ray length toward a directional light
}

bool UIvyLightSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Only in game and Play In Editor, not in the editor world
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UIvyLightSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	RefreshLights();
}

void UIvyLightSubsystem::RefreshLights()
{
	Lights.Reset();

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		const bool bActorTagged = It->ActorHasTag(IvyLight::Tag);

		TInlineComponentArray<ULightComponent*> LightComponents(*It);
		for (ULightComponent* Light : LightComponents)
		{
			// The tag can be on the actor (simplest) or on a single light component
			if (bActorTagged || Light->ComponentHasTag(IvyLight::Tag))
			{
				Lights.Add(Light);
			}
		}
	}

	UE_LOG(LogIvyLight, Log, TEXT("Gameplay lights found: %d"), Lights.Num());
}

bool UIvyLightSubsystem::IsPointLit(const FVector& Point, const FVector& SurfaceNormal, AActor* IgnoredActor) const
{
	for (const TWeakObjectPtr<ULightComponent>& WeakLight : Lights)
	{
		if (const ULightComponent* Light = WeakLight.Get())
		{
			if (IsLitBy(Light, Point, SurfaceNormal, IgnoredActor))
			{
				return true;
			}
		}
	}
	return false;
}

bool UIvyLightSubsystem::IsLitBy(const ULightComponent* Light, const FVector& Point, const FVector& Normal, const AActor* IgnoredActor) const
{
	// A switched-off or hidden light doesn't count: enables puzzles with switches
	if (!Light->IsVisible() || !Light->bAffectsWorld || Light->Intensity <= 0.f)
	{
		return false;
	}

	const FVector Start = Point + Normal * IvyLight::RayLift;
	FVector End;

	if (const UDirectionalLightComponent* Sun = Cast<UDirectionalLightComponent>(Light))
	{
		// A directional light travels along its forward vector: the light is "behind" it
		const FVector ToLight = -Sun->GetForwardVector();
		if ((Normal | ToLight) < IvyLight::FacingThreshold)
		{
			return false;
		}
		End = Start + ToLight * IvyLight::SunRayLength;
	}
	else if (const ULocalLightComponent* Local = Cast<ULocalLightComponent>(Light))
	{
		const FVector LightPos = Local->GetComponentLocation();
		const FVector Delta = LightPos - Point;
		const double Dist = Delta.Size();
		if (Dist < KINDA_SMALL_NUMBER || Dist > Local->AttenuationRadius)
		{
			return false; // Outside the light radius
		}

		const FVector ToLight = Delta / Dist;
		if ((Normal | ToLight) < IvyLight::FacingThreshold)
		{
			return false; // Surface faces away from the light
		}

		if (const USpotLightComponent* Spot = Cast<USpotLightComponent>(Local))
		{
			const double CosOuter = FMath::Cos(FMath::DegreesToRadians(Spot->OuterConeAngle));
			if ((-ToLight | Spot->GetForwardVector()) < CosOuter)
			{
				return false; // Outside the spot cone
			}
		}
		End = LightPos;
	}
	else
	{
		return false; // Sky lights and other types are not gameplay lights
	}

	// Anything blocking visibility between the point and the light casts a gameplay shadow
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IvyLight), false);
	if (IgnoredActor)
	{
		Params.AddIgnoredActor(IgnoredActor);
	}
	Params.AddIgnoredActor(Light->GetOwner()); // A lamp mesh doesn't shadow its own light

	FHitResult Hit;
	const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);

#if ENABLE_DRAW_DEBUG
	if (CVarIvyDebugLight.GetValueOnGameThread() > 0)
	{
		const FVector DrawEnd = bBlocked ? FVector(Hit.ImpactPoint) : End;
		DrawDebugLine(GetWorld(), Start, DrawEnd, bBlocked ? FColor(90, 90, 90) : FColor::Yellow, false, 2.f, 0, 0.5f);
	}
#endif

	return !bBlocked;
}