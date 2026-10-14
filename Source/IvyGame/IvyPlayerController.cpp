#include "IvyPlayerController.h"
#include "IvyGrowthComponent.h"
#include "IvyGardenSubsystem.h"
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

	// Plants register in the garden during their own BeginPlay, which may run before or after ours
	Garden = GetWorld()->GetSubsystem<UIvyGardenSubsystem>();
	if (UIvyGardenSubsystem* GardenPtr = Garden.Get())
	{
		GardenPtr->OnSelectedPlantChanged.AddDynamic(this, &AIvyPlayerController::HandleSelectedPlantChanged);
		if (UIvyGrowthComponent* Selected = GardenPtr->GetSelectedPlant())
		{
			HandleSelectedPlantChanged(Selected);
		}
	}
}

void AIvyPlayerController::HandleSelectedPlantChanged(UIvyGrowthComponent* SelectedPlant)
{
	if (SelectedPlant && !bIvyReadyFired)
	{
		bIvyReadyFired = true;
		OnIvyReady(SelectedPlant);
	}
}

UIvyGrowthComponent* AIvyPlayerController::GetIvy() const
{
	return Garden.IsValid() ? Garden->GetSelectedPlant() : nullptr;
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
	if (NextPlantAction)
	{
		EIC->BindAction(NextPlantAction, ETriggerEvent::Started, this, &AIvyPlayerController::OnNextPlant);
	}
	if (PreviousPlantAction)
	{
		EIC->BindAction(PreviousPlantAction, ETriggerEvent::Started, this, &AIvyPlayerController::OnPreviousPlant);
	}
}

void AIvyPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	bCursorHit = GetHitResultUnderCursor(IvyChannels::Grow, false, CursorHit);

	UIvyGrowthComponent* Plant = GetIvy();

	// Growth is possible with sap, or for free on a lit surface
	const bool bCanGrow = bCursorHit && Plant && UIvyGrowthComponent::IsClimbable(CursorHit)
		&& (Plant->HasSapForStep() || Plant->IsSurfaceLit(CursorHit.ImpactPoint, CursorHit.ImpactNormal));

	if (bCursorHit && Plant && Plant->IsGrowing())
	{
		Plant->SetGrowTarget(CursorHit.ImpactPoint);
	}

	OnCursorUpdated(bCursorHit, CursorHit.ImpactPoint, CursorHit.ImpactNormal, bCanGrow);
}

void AIvyPlayerController::OnGrowStarted()
{
	UIvyGardenSubsystem* GardenPtr = Garden.Get();
	UIvyGrowthComponent* Plant = GetIvy();
	if (!bCursorHit || !GardenPtr || !Plant)
	{
		return;
	}

	// Clicking the root of another plant selects it instead of growing
	if (UIvyGrowthComponent* Clicked = GardenPtr->FindPlantAtRoot(CursorHit.ImpactPoint, RootSelectRadius))
	{
		if (Clicked != Plant)
		{
			GardenPtr->SelectPlant(Clicked);
			return;
		}
	}

	// The garden snapshots the whole level, then the selected plant starts growing
	GardenPtr->BeginGesture();
	Plant->BeginGesture(CursorHit.ImpactPoint);
}

void AIvyPlayerController::OnGrowStopped()
{
	if (UIvyGrowthComponent* Plant = GetIvy())
	{
		Plant->EndGesture();
	}
	if (Garden.IsValid())
	{
		Garden->EndGesture();
	}
}

void AIvyPlayerController::OnUndo()
{
	UIvyGrowthComponent* Plant = GetIvy();
	if (Garden.IsValid() && !(Plant && Plant->IsGrowing()))
	{
		Garden->Undo();
	}
}

void AIvyPlayerController::OnReset()
{
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
}

void AIvyPlayerController::OnNextPlant()
{
	if (Garden.IsValid())
	{
		Garden->SelectNextPlant(1);
	}
}

void AIvyPlayerController::OnPreviousPlant()
{
	if (Garden.IsValid())
	{
		Garden->SelectNextPlant(-1);
	}
}