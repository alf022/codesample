//Copyright 2020 Marchetti S. Alfredo I. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UStructs/GameSettingsTypes.h"
#include "KeybindsSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAnyKeybindChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnKeybindChanged, FName, ActionName, int32, KeybindIndex);

class UGameKeybindsData;

UCLASS()
class GAMESETTINGS_API UKeybindsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Keybinds")
	FOnAnyKeybindChanged OnAnyKeybindChanged;

	UPROPERTY(BlueprintAssignable, Category = "Keybinds")
	FOnAnyKeybindChanged OnKeybindDataChanged;

	UPROPERTY(BlueprintAssignable, Category = "Keybinds")
	FOnKeybindChanged OnKeybindChanged;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	
	UFUNCTION(BlueprintCallable, Category = "Keybinds")
	bool SetInputKeybind(FInputActionKeyMapping InputKeyMapping, int32 KeybindIndex);

	UFUNCTION(BlueprintCallable, Category = "Keybinds")
	void ResetKeybindsToDefault();

	UFUNCTION(BlueprintCallable, Category = "Keybinds")
	UGameKeybindsData* GetKeybindsData() const;

	UFUNCTION(BlueprintCallable, Category = "Keybinds")
	bool GetKeyBindInput(const FKeybindDataID& KeybindData, FInputChord& InputChord);

	UFUNCTION(BlueprintCallable, Category = "Keybinds")
	void SetKeybindOverrides(const TMap <FKeybindDataID, FInputChord>& InOverridenInputsMap);

	UFUNCTION(BlueprintCallable, Category = "Keybinds")
	TMap<FKeybindDataID, FInputChord>& GetOverridenInputsMap();

protected:
	UPROPERTY()
	UGameKeybindsData* KeybindsData;

	UPROPERTY()
	TMap <FKeybindDataID, FInputChord> OverridenInputsMap;

	FString GetSaveName() const;
	void SaveData();
	bool LoadData();
	void InitializeKebindsData(UGameKeybindsData* InKeybindsData);
		
	bool IsKeyChordAlredyInUse(const FInputActionKeyMapping& InputKeyMapping, FKeybindDataID& KeybindData) const;
	void ClearKeybinds();

	FInputChord GetInputChord(FInputActionKeyMapping InputKeyMapping) const;

	void ResetKeybindsToDefault_Internal();

	bool SetInputKeybind_Internal(FInputActionKeyMapping InputKeyMapping, int32 KeybindIndex);
	void SetInputOverride(FInputActionKeyMapping InputKeyMapping, int32 KeybindIndex);
	void SetInputOverride(FName ActionName, int32 InputIndex, FInputChord Input);
	void RemoveInputOverride(const FInputActionKeyMapping& InputKeyMapping);

};
