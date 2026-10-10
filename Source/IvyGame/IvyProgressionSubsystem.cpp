#include "IvyProgressionSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

namespace
{
    const TCHAR* SaveSlot = TEXT("IvyProgress");
}

void UIvyProgressionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    LevelList = GetDefault<UIvyGameSettings>()->LevelList.LoadSynchronous();

    if (UGameplayStatics::DoesSaveGameExist(SaveSlot, 0))
        SaveData = Cast<UIvySaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0));
    if (!SaveData)
        SaveData = Cast<UIvySaveGame>(UGameplayStatics::CreateSaveGameObject(UIvySaveGame::StaticClass()));
}

// ---------- Lettura ----------

int32 UIvyProgressionSubsystem::GetLevelCount() const
{
    return LevelList ? LevelList->Levels.Num() : 0;
}

FText UIvyProgressionSubsystem::GetLevelDisplayName(int32 Index) const
{
    return (LevelList && LevelList->Levels.IsValidIndex(Index)) ? LevelList->Levels[Index].DisplayName : FText::GetEmpty();
}

int32 UIvyProgressionSubsystem::GetCurrentLevelIndex() const
{
    if (!LevelList) return INDEX_NONE;
    // true = rimuove il prefisso del Play in Editor ("UEDPIE_0_")
    const FString Current = UGameplayStatics::GetCurrentLevelName(GetGameInstance(), true);
    for (int32 i = 0; i < LevelList->Levels.Num(); ++i)
    {
        if (LevelList->Levels[i].Level.GetAssetName() == Current) return i;
    }
    return INDEX_NONE;
}

int32 UIvyProgressionSubsystem::GetNextPlayableIndex() const
{
    const int32 Count = GetLevelCount();
    return Count == 0 ? INDEX_NONE : FMath::Min(SaveData->HighestCompletedIndex + 1, Count - 1);
}

bool UIvyProgressionSubsystem::IsLevelUnlocked(int32 Index) const
{
    return Index >= 0 && Index < GetLevelCount() && Index <= SaveData->HighestCompletedIndex + 1;
}

bool UIvyProgressionSubsystem::IsLevelCompleted(int32 Index) const
{
    return Index >= 0 && Index <= SaveData->HighestCompletedIndex;
}

bool UIvyProgressionSubsystem::HasNextLevel() const
{
    const int32 Current = GetCurrentLevelIndex();
    return Current != INDEX_NONE && Current + 1 < GetLevelCount();
}

// ---------- Azioni ----------

void UIvyProgressionSubsystem::CompleteCurrentLevel()
{
    UWorld* World = GetGameInstance()->GetWorld();
    if (!World || CompletedWorld.Get() == World) return;
    CompletedWorld = World;

    const int32 Index = GetCurrentLevelIndex();
    if (Index > SaveData->HighestCompletedIndex)
    {
        SaveData->HighestCompletedIndex = Index;
        SaveProgress();
    }
    OnLevelCompleted.Broadcast(Index);
}

void UIvyProgressionSubsystem::OpenLevelByIndex(int32 Index)
{
    if (!IsLevelUnlocked(Index)) return;
    UGameplayStatics::OpenLevelBySoftObjectPtr(GetGameInstance(), LevelList->Levels[Index].Level);
}

void UIvyProgressionSubsystem::OpenNextLevel()
{
    if (HasNextLevel()) OpenLevelByIndex(GetCurrentLevelIndex() + 1);
    else OpenMainMenu();
}

void UIvyProgressionSubsystem::RestartLevel()
{
    UGameplayStatics::OpenLevel(GetGameInstance(), FName(*UGameplayStatics::GetCurrentLevelName(GetGameInstance(), true)));
}

void UIvyProgressionSubsystem::OpenMainMenu()
{
    const TSoftObjectPtr<UWorld>& Menu = GetDefault<UIvyGameSettings>()->MainMenuLevel;
    if (!Menu.IsNull()) UGameplayStatics::OpenLevelBySoftObjectPtr(GetGameInstance(), Menu);
}

void UIvyProgressionSubsystem::ResetProgress()
{
    SaveData->HighestCompletedIndex = INDEX_NONE;
    SaveProgress();
}

void UIvyProgressionSubsystem::SaveProgress()
{
    UGameplayStatics::SaveGameToSlot(SaveData, SaveSlot, 0);
}