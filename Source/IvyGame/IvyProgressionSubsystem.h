#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "GameFramework/SaveGame.h"
#include "IvyProgressionSubsystem.generated.h"

// ---------- Elenco ordinato dei livelli (Data Asset) ----------
USTRUCT(BlueprintType)
struct FIvyLevelEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level")
    TSoftObjectPtr<UWorld> Level;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level")
    FText DisplayName;
};

UCLASS(BlueprintType)
class IVYGAME_API UIvyLevelList : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Levels")
    TArray<FIvyLevelEntry> Levels;
};

// ---------- Impostazioni di progetto: Project Settings → Game → Ivy ----------
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Ivy"))
class IVYGAME_API UIvyGameSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(Config, EditAnywhere, Category = "Levels")
    TSoftObjectPtr<UIvyLevelList> LevelList;

    UPROPERTY(Config, EditAnywhere, Category = "Levels")
    TSoftObjectPtr<UWorld> MainMenuLevel;
};

// ---------- Progressi salvati su disco ----------
UCLASS()
class IVYGAME_API UIvySaveGame : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY()
    int32 HighestCompletedIndex = INDEX_NONE;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIvyLevelCompleted, int32, LevelIndex);

// ---------- Progressione: sopravvive ai cambi di livello ----------
UCLASS()
class IVYGAME_API UIvyProgressionSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    // Lettura
    UFUNCTION(BlueprintPure, Category = "Ivy|Levels") int32 GetLevelCount() const;
    UFUNCTION(BlueprintPure, Category = "Ivy|Levels") FText GetLevelDisplayName(int32 Index) const;
    UFUNCTION(BlueprintPure, Category = "Ivy|Levels") int32 GetCurrentLevelIndex() const;
    UFUNCTION(BlueprintPure, Category = "Ivy|Levels") int32 GetNextPlayableIndex() const;
    UFUNCTION(BlueprintPure, Category = "Ivy|Levels") bool IsLevelUnlocked(int32 Index) const;
    UFUNCTION(BlueprintPure, Category = "Ivy|Levels") bool IsLevelCompleted(int32 Index) const;
    UFUNCTION(BlueprintPure, Category = "Ivy|Levels") bool HasNextLevel() const;

    // Azioni
    UFUNCTION(BlueprintCallable, Category = "Ivy|Levels") void CompleteCurrentLevel();
    UFUNCTION(BlueprintCallable, Category = "Ivy|Levels") void OpenLevelByIndex(int32 Index);
    UFUNCTION(BlueprintCallable, Category = "Ivy|Levels") void OpenNextLevel();
    UFUNCTION(BlueprintCallable, Category = "Ivy|Levels") void RestartLevel();
    UFUNCTION(BlueprintCallable, Category = "Ivy|Levels") void OpenMainMenu();
    UFUNCTION(BlueprintCallable, Category = "Ivy|Levels") void ResetProgress();

    UPROPERTY(BlueprintAssignable, Category = "Ivy|Levels")
    FOnIvyLevelCompleted OnLevelCompleted;

private:
    void SaveProgress();

    UPROPERTY() TObjectPtr<UIvyLevelList> LevelList;
    UPROPERTY() TObjectPtr<UIvySaveGame> SaveData;

    // Evita una doppia vittoria nello stesso livello
    TWeakObjectPtr<UWorld> CompletedWorld;
};