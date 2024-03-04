#include "Layout/TerrainLayoutData.h"

#define LOCTEXT_NAMESPACE "TerrainLayoutData"

#if WITH_EDITOR
EDataValidationResult UTerrainLayoutData::IsDataValid(TArray<FText>& ValidationErrors)
{
	EDataValidationResult Result = CombineDataValidationResults(Super::IsDataValid(ValidationErrors), EDataValidationResult::Valid);

	if (!InitialRoomsLayout)
	{
		Result = EDataValidationResult::Invalid;
		ValidationErrors.Add(FText(LOCTEXT("InitialRoomsLayoutInvalid", "InitialRoomsLayout is invalid")));
	}

	int32 EntryIndex = 0;
	for (const UTerrainLayoutRoomData* roomlayot : RoomLayouts)
	{
		if (!roomlayot)
		{
			Result = EDataValidationResult::Invalid;
			ValidationErrors.Add(FText::Format(LOCTEXT("RoomLayoutInvalid", "Null entry at index {0} in RoomLayouts"), FText::AsNumber(EntryIndex)));
		}

		++EntryIndex;
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
