//Copyright 2020 Marchetti S. Alfredo I. All Rights Reserved.

#include "Subsystems/AudioSubsystem.h"
#include "SaveGames/AudioSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Data/GameAudioData.h"
#include "Settings/GameSettings.h"
#include "Tags/GameSettingsTags.h"

bool UAudioSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	UWorld* OuterWorld = CastChecked<UWorld>(Outer);
	return OuterWorld->GetNetMode() != NM_DedicatedServer;
}

void UAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UAudioSubsystem::Deinitialize()
{
	Super::Deinitialize();
	SaveData();
}

void UAudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const UGameSettings* Settings = GetDefault<UGameSettings>();
	UObject* DataAsset = Settings->GameAudioData.LoadSynchronous();
	UGameAudioData* LoadedGameAudioData = Cast<UGameAudioData>(DataAsset);

	if (!LoadedGameAudioData)
	{
		return;
	}

	GameAudioData = LoadedGameAudioData;

	LoadData();

	if (AudioVolumes.Num() <= 0)
	{
		ResetAudioVolumesToDefault();
	}
}

FString UAudioSubsystem::GetSaveName() const
{
	return "AudioData";
}

void UAudioSubsystem::SaveData()
{
	if (UAudioSaveGame* SaveGameInstance = Cast<UAudioSaveGame>(UGameplayStatics::CreateSaveGameObject(UAudioSaveGame::StaticClass())))
	{
		SaveGameInstance->AudioVolumes = AudioVolumes;
		UGameplayStatics::SaveGameToSlot(SaveGameInstance, GetSaveName(), 0);		
	}	
}

bool UAudioSubsystem::LoadData()
{
	if (UGameplayStatics::DoesSaveGameExist(GetSaveName(), 0))
	{		
		if (UAudioSaveGame* SaveGameInstance = Cast<UAudioSaveGame>(UGameplayStatics::LoadGameFromSlot(GetSaveName(), 0)))
		{
			if (SaveGameInstance != nullptr)
			{		
				SetAudioVolumes(SaveGameInstance->AudioVolumes);
				return true;
			}
		}
	}

	return false;
}

float UAudioSubsystem::GetAudioDisplayVolume(const FGameplayTag& Category) const
{
	if (!AudioVolumes.Contains(Category))
	{
		return GameAudioData->DefaultVolume;
	}

	return AudioVolumes[Category];
}

TMap<FGameplayTag, float>& UAudioSubsystem::GetAudioVolumes()
{
	return AudioVolumes;
}

void UAudioSubsystem::SetAudioVolumes(TMap<FGameplayTag, float> NewValues)
{
	AudioVolumes.Empty();

	if (!GameAudioData)
	{
		return;
	}

	for (const TPair<FGameplayTag, float>& pair : NewValues)
	{
		SetAudioVolume_Internal(pair.Key, pair.Value);
	}

	SaveData();
}

FText UAudioSubsystem::GetAudioCategoryDisplayName(const FGameplayTag& Category) const
{
	if (!GameAudioData->AudioCategories.Contains(Category))
	{
		return FText();
	}

	return GameAudioData->AudioCategories[Category].AudioDisplayName;
}

float UAudioSubsystem::GetAudioVolume(const FGameplayTag& Category) const
{
	float Master = GetAudioDisplayVolume(TAG_GAMESETTINGS_AUDIO_MASTER.GetTag());

	if (TAG_GAMESETTINGS_AUDIO_MASTER.GetTag() == Category)
	{
		return Master;
	}

	return GetAudioDisplayVolume(Category) * Master;
}

FAudioCategorySettings UAudioSubsystem::GetAudioSettings(const FGameplayTag& Category) const
{
	if (!GameAudioData)
	{
		return FAudioCategorySettings();
	}

	if (!GameAudioData->AudioCategories.Contains(Category))
	{
		return FAudioCategorySettings();
	}
	
	return GameAudioData->AudioCategories[Category];
}

void UAudioSubsystem::SetAudioVolume(const FGameplayTag& Category, float Volume)
{
	SetAudioVolume_Internal(Category, Volume);
	SaveData();
}

void UAudioSubsystem::SetAudioVolume_Internal(const FGameplayTag& Category, float Volume)
{
	float& CurrentVolume = AudioVolumes.FindOrAdd(Category);
	CurrentVolume = Volume;

	if (TAG_GAMESETTINGS_AUDIO_MASTER.GetTag() == Category)
	{
		for (const TPair<FGameplayTag, float>& pair : AudioVolumes)
		{
			if (TAG_GAMESETTINGS_AUDIO_MASTER.GetTag() == Category) // Master never gets pushed, since is not a real audio and is just a modifier value.
			{
				OnAudioVolumeChanged.Broadcast(Category, Volume); // Call the event anyways for ui updating
				continue;
			}

			PushAudioVolume(Category, GetAudioVolume(Category));
		}
	}
	else
	{
		PushAudioVolume(Category, GetAudioVolume(Category));
	}
}

void UAudioSubsystem::PushAudioVolume(const FGameplayTag& Category, float Volume)
{
	USoundMix* SoundMix = GetAudioSettings(Category).SoundMix;
	UGameplayStatics::SetSoundMixClassOverride(GetWorld(), SoundMix, GetAudioSettings(Category).SoundClass, Volume * GetAudioSettings(Category).VolumeMultiplier);
	UGameplayStatics::PushSoundMixModifier(GetWorld(), SoundMix);

	OnAudioVolumeChanged.Broadcast(Category, Volume);
}

void UAudioSubsystem::ResetAudioVolumesToDefault()
{
	AudioVolumes.Empty();

	if (!GameAudioData)
	{
		return;
	}

	for (const TPair<FGameplayTag, FAudioCategorySettings> pair : GameAudioData->AudioCategories)
	{
		SetAudioVolume_Internal(pair.Key, pair.Value.DefaultVolume);
	}	

	SaveData();
}
