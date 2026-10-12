#include "IvyPlayerController.h"
#include "IvyGrowthComponent.h"
#include "IvyPlant.h"
#include "IvyTypes.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "Camera/CameraActor.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogIvyController, Log, All);

AIvyPlayerController::AIvyPlayerController()
{
	bAutoManageActiveCameraTarget = false; // Camera is set manually
	bShowMouseCursor = true;
	bEnableClickEvents = false;
}

void AIvyPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalPlayerController())
	{
		return;
	}

	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			if (IvyMappingContext)
			{
				Subsystem->AddMappingContext(IvyMappingContext, 0);
			}
			else
			{
				UE_LOG(LogIvyController, Error, TEXT("IvyMappingContext not assigned on the player controller."));
			}
		}
	}

	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	FindLevelReferences();
}

void AIvyPlayerController::FindLevelReferences()
{
	TArray<AActor*> Cameras;
	UGameplayStatics::GetAllActorsOfClassWithTag(this, ACameraActor::StaticClass(), CameraTag, Cameras);
	if (Cameras.Num() > 0)
	{
		SetViewTarget(Cameras[0]);
	}
	else
	{
		UE_LOG(LogIvyController, Warning, TEXT("No CameraActor with tag '%s' found in the level."), *CameraTag.ToString());
	}

	if (AIvyPlant* Plant = Cast<AIvyPlant>(UGameplayStatics::GetActorOfClass(this, AIvyPlant::StaticClass())))
	{
		Ivy = Plant->GetGrowth();
		OnIvyReady(Ivy.Get());
	}
	else
	{
		UE_LOG(LogIvyController, Warning, TEXT("No AIvyPlant found in the level."));
	}
}

void AIvyPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EIC)
	{
		UE_LOG(LogIvyController, Error, TEXT("InputComponent is not an EnhancedInputComponent. Check Project Settings > Input."));
		return;
	}

	if (GrowAction)
	{
		EIC->BindAction(GrowAction, ETriggerEvent::Started, this, &AIvyPlayerController::OnGrowStarted);
		EIC->BindAction(GrowAction, ETriggerEvent::Completed, this, &AIvyPlayerController::OnGrowStopped);
		EIC->BindAction(GrowAction, ETriggerEvent::Canceled, this, &AIvyPlayerController::OnGrowStopped);
	}
	if (UndoAction)
	{
		EIC->BindAction(UndoAction, ETriggerEvent::Started, this, &AIvyPlayerController::OnUndo);
	}
	if (ResetAction)
	{
		EIC->BindAction(ResetAction, ETriggerEvent::Started, this, &AIvyPlayerController::OnReset);
	}
}

void AIvyPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	bCursorHit = GetHitResultUnderCursor(IvyChannels::Grow, false, CursorHit);

	UIvyGrowthComponent* IvyComp = Ivy.Get();
	const bool bCanGrow = bCursorHit && IvyComp && UIvyGrowthComponent::IsClimbable(CursorHit)
		&& (IvyComp->HasSapForStep() || IvyComp->IsSurfaceLit(CursorHit.ImpactPoint, CursorHit.ImpactNormal));

	if (bCursorHit && IvyComp && IvyComp->IsGrowing())
	{
		IvyComp->SetGrowTarget(CursorHit.ImpactPoint);
	}

	OnCursorUpdated(bCursorHit, CursorHit.ImpactPoint, CursorHit.ImpactNormal, bCanGrow);
}

void AIvyPlayerController::OnGrowStarted()
{
	if (bCursorHit && Ivy.IsValid())
	{
		Ivy->BeginGesture(CursorHit.ImpactPoint);
	}
}

void AIvyPlayerController::OnGrowStopped()
{
	if (Ivy.IsValid())
	{
		Ivy->EndGesture();
	}
}

void AIvyPlayerController::OnUndo()
{
	if (Ivy.IsValid())
	{
		Ivy->Undo();
	}
}

void AIvyPlayerController::OnReset()
{
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
}