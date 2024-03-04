#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SBaseTerrainEditor;
class STextBlock;
class UTerrainGeneratorSubsystem;
class UTerrainData;
class STextCheckbox;

class STerrainEditorActionsTab : public SCompoundWidget
{		
public:
	SLATE_BEGIN_ARGS(STerrainEditorActionsTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<SBaseTerrainEditor>& InTerrainEditor);

private:	
	TSharedPtr<STextBlock> SeedText;
	TSharedPtr<SBaseTerrainEditor> TerrainEditor;
		
	TSharedPtr <STextCheckbox> TextboxDrawCentralCell;
	TSharedPtr <STextCheckbox> TextboxDrawInitialCell;
	TSharedPtr <STextCheckbox> TextboxDrawInitialRoom;
	TSharedPtr <STextCheckbox> TextboxDrawRoomBorders;
	TSharedPtr <STextCheckbox> TextboxDrawRoomCollision;
	TSharedPtr <STextCheckbox> TextboxDrawCorridors;
	TSharedPtr <STextCheckbox> TextboxDrawWalls;	
	TSharedPtr <STextCheckbox> TextboxDrawDoors;	
	TSharedPtr <STextCheckbox> TextboxDrawCells;

	TSharedPtr<SComboBox<TSharedPtr<FText>>> StateComboBox;
	TSharedPtr<FText> SelectedState;
	TArray<TSharedPtr<FText>> StateOptions;

	UTerrainGeneratorSubsystem* TerrainGeneratorSubsystem= nullptr;

protected:

	UTerrainData* TerrainData = nullptr;

	FReply GenerateTerrainButtonClicked();

	static FString GetETerrainGen_StateName(uint32 Value)
	{
		static const UEnum* ETerrainGen_StateType = FindObject<UEnum>(nullptr, L"/Script/CoreUObject.Class'/Script/TerrainGenerator.ETerrainGen_State'");
	
	//	UE_LOG(LogTemp, Error, TEXT("GetETerrainGen_StateName- %s."), *GetNameSafe(ETerrainGen_StateType->GetOuter()));

		return ETerrainGen_StateType->GetNameStringByIndex(static_cast<uint32>(Value));
	}

	void UpdateSeedText();
	UTerrainGeneratorSubsystem* GetTerrainGeneratorSubsystem();
	
	void LoadTerrainData();

	void OnCheckBoxStateChangedGrid(ECheckBoxState NewState) const;
	void OnCheckBoxStateChangedGridMesh(ECheckBoxState NewState) const;
	void OnCheckBoxStateChangedDraw(ECheckBoxState NewState) const;
};