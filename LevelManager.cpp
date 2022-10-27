//Copyright 2020 Marchetti S. Alfredo I. All Rights Reserved.

#include "LevelManager/LevelManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "LevelGenerator.h"//Log
#include "Engine/LevelStreamingDynamic.h"
#include "LevelManager/LevelGeneratorFunctionLibrary.h"
#include "LevelManager/LevelActorInterface.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameplayTagContainer.h"

ULevelManager::ULevelManager() : Super()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.SetTickFunctionEnable(false);
	PrimaryComponentTick.TickInterval = 0.32; //Slow tick to check for levels stream and level gen is enough.
}

void ULevelManager::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{	
	if (bIsGeneratingLevel)
	{
		HandleRoomLayoutThreads();
		HandleCorridorLayoutThreads();
		HandlePopulateRoomThreads();
	}
	else
	{
		HandleLevelStreamming();
	}
}

void ULevelManager::HandleLevelStreamming()
{
	if (!IsLevelStreamingHandlingActive)
	{
		return;
	}
	
	if (bIsGeneratingLevel)
	{
		return;
	}		

	TArray <FVector> CurrentPlayerLocations = GetPlayersLocations();
	if (CurrentPlayerLocations.Num() <= 0)
	{
		return;
	}	

	TArray<int32> ReservedVisibleLevels = GetLevelPlacementDataForVisibleCells();//Forced visib
	
	TArray<int32> AllVisibleLevels= TArray<int32>();
	if (ActorsCanShowLevels)
	{		
		TArray <FVector> CurrentPawnsLocations = GetNonPlayerPawnsLocations();
		AllVisibleLevels = GetLevelPlacementDataBetweenPositions(CurrentPawnsLocations, 0, CellSize, ReservedVisibleLevels);
	}
	else
	{
		AllVisibleLevels = ReservedVisibleLevels;
	}
		
	ProcessAllCellVisibilityStatus();
	
	TArray<int32> MustLoadLevelsLocal = GetLevelPlacementDataBetweenPositions(CurrentPlayerLocations, 0, LoadStreamLevelDistance, AllVisibleLevels);
	TArray<int32> CanRemainLoadLevelsLocal = GetLevelPlacementDataBetweenPositions(CurrentPlayerLocations, LoadStreamLevelDistance, LoadStreamLevelDistance + UnloadStreamLevelTolerance, TArray<int32>());
	TArray<int32> PreviousTickLoadedLevelsLocal = GetLoadedLevelsPlacementData(LoadedLevelsMap);

	HandleLevelsVisibility(MustLoadLevelsLocal, CanRemainLoadLevelsLocal, PreviousTickLoadedLevelsLocal);
	HandleCellsVisibility(PreviousTickLoadedLevelsLocal);
	HandlePooledLevelsTransform();
}

TArray <FVector> ULevelManager::GetPlayersLocations() const
{
	AGameModeBase* GameModeBase = Cast<AGameModeBase>(GetOwner());
	if (!GameModeBase)
	{
		return TArray <FVector>();
	}	

	AGameStateBase* GameStateBase = GameModeBase->GetGameState<AGameStateBase>();
	if (!GameStateBase)
	{
		return TArray <FVector>();
	}

	TArray <APlayerState*> PlayerStates = GameStateBase->PlayerArray;
	TArray <FVector> CurrentPlayerLocations = TArray <FVector>();
	for (int i = 0; i < PlayerStates.Num(); i++)
	{
		if (PlayerStates[i]->GetPawn())
		{
			CurrentPlayerLocations.Add(PlayerStates[i]->GetPawn()->GetActorLocation());
		}
	}

	return CurrentPlayerLocations;
}

TArray <FVector> ULevelManager::GetNonPlayerPawnsLocations() const
{		
	if (!ActorsCanShowLevels)
	{
		return TArray <FVector>();
	}

	TArray <AActor*> LocalActors = TArray <AActor*>();
	UGameplayStatics::GetAllActorsOfClass(this, StreamActorClass, LocalActors);//@TODO This can be something to improve for performance. Since tick is slow, it is fine for now but could find a better way of getting all the actors.
	TArray <FVector> CurrentPawnsLocations = TArray <FVector>();
	for (int i = 0; i < LocalActors.Num(); i++)
	{
		CurrentPawnsLocations.Add(LocalActors[i]->GetActorLocation());
	}	

	return CurrentPawnsLocations;
}

void ULevelManager::HandleLevelsVisibility(const TArray<int32>& MustLoadLevels, const TArray<int32>& CanRemainLoadLevels, const TArray<int32>& PreviousTickLoadedLevels)
{
	//Check if there are levels that need to be hidden
	for (int i = 0; i < PreviousTickLoadedLevels.Num(); i++)
	{
		if (!MustLoadLevels.Contains(PreviousTickLoadedLevels[i]) && !CanRemainLoadLevels.Contains(PreviousTickLoadedLevels[i]))
		{
			ChangeLevelVisibibility(PreviousTickLoadedLevels[i], false);
		}
		else
		{
			//Retrigger the actor spawn untill the actor is actually spawned. If the level is not loaded when the event shown is triggered, it needs to be retriggered.			
			FLevel_ActorSpawnTrack track = FLevel_ActorSpawnTrack();
			if (ActorsSpawnTrack.Contains(PreviousTickLoadedLevels[i]))
			{
				FLevel_ActorSpawnTrack LevelActorsTrack = FLevel_ActorSpawnTrack();
				LevelActorsTrack = *ActorsSpawnTrack.Find(PreviousTickLoadedLevels[i]);

				for (int32 j = 0; j < LevelActorsTrack.ActorsTrack.Num(); j++)
				{
					if (!LevelActorsTrack.ActorsTrack[j].ActorWasSpawned)
					{
						ChangeVisibilitySpawnedActor(PreviousTickLoadedLevels[i], j, true);
					}
				}
			}
		}
	}

	//Check if there are new levels that need to be shown
	bool AtLeastOneIsVisible = false;
	for (int i = 0; i < MustLoadLevels.Num(); i++)
	{
		if (!IsLevelLoaded(MustLoadLevels[i]))
		{
			ChangeLevelVisibibility(MustLoadLevels[i], true);
			AtLeastOneIsVisible = true;
		}
	}

	if (AtLeastOneIsVisible)
	{
		OnNewLevelShown.Broadcast();
	}
}

void ULevelManager::HandleCellsVisibility(const TArray<int32>& PreviousTickLoadedLevels)
{
	TArray <int32> CurrentCells = TArray <int32>();
	TArray <int32> PreviousLoadedCells = GetCellsIndexFromPlacementDataArray(PreviousTickLoadedLevels);
	TArray <int32> CurrentLoadedLevels = GetLoadedLevelsPlacementData(LoadedLevelsMap);
	TArray <int32> NewLoadedCells = GetCellsIndexFromPlacementDataArray(CurrentLoadedLevels);

	for (int i = 0; i < PreviousLoadedCells.Num(); i++)
	{
		if (!NewLoadedCells.Contains(PreviousLoadedCells[i]))
		{
			CurrentCells.AddUnique(PreviousLoadedCells[i]);
		}
	}
	OnCellVisibilityChanged(CurrentCells, false);

	CurrentCells.Empty();
	for (int i = 0; i < NewLoadedCells.Num(); i++)
	{
		if (!PreviousLoadedCells.Contains(NewLoadedCells[i]))
		{
			CurrentCells.AddUnique(NewLoadedCells[i]);
		}
	}
	OnCellVisibilityChanged(CurrentCells, true);
}

void ULevelManager::HandlePooledLevelsTransform()
{
	if (!UseLevelsPool)
	{
		return;
	}

	TArray <int32> LevelsTracked = TArray <int32>();
	LoadedLevelsMap.GetKeys(LevelsTracked);
	FLoadedLevelTrack Leveltrack = FLoadedLevelTrack();
	for (const int32& level : LevelsTracked)
	{
		Leveltrack = *LoadedLevelsMap.Find(level);
		SetLevelTransform(Leveltrack.LevelStreaming, FTransform(GetPlacedLevelRotation(Leveltrack.PlacementDataIndex), GetPlacedLevelPosition(Leveltrack.PlacementDataIndex)));
	}
}

bool ULevelManager::IsGeneratingLevel()
{
	return bIsGeneratingLevel;
}

FVector ULevelManager::GetStartLevelPosition()
{
	FIntPoint StartCell = GetStartCellID();	
	return ULevelGeneratorFunctionLibrary::GetCellWorldPosition(StartCell, CellSize, InitialLocation);
}

FGameplayTag ULevelManager::GetStartLevelBiome()
{
	FIntPoint StartCell = GetStartCellID();
	int32 Index = ULevelGeneratorFunctionLibrary::FindCellIDIndex(StartCell, CellsData);

	if (CellsData.IsValidIndex(Index))
	{
		return CellsData[Index].Cell_Biome;
	}

	return FGameplayTag();
}

void ULevelManager::EnableAutoLevelStreaming(bool Enable)
{
	IsLevelStreamingHandlingActive = Enable;
}

FIntPoint ULevelManager::GetStartCellID()
{
	int StartRoomGridIndex = GetClosestLayoutIndexToAnchor(LayoutData, ELevelGen_LevelType::Normal, StartRoomAnchor);
	FIntPoint StartCell = { -1,-1 }; //invalid cell	
	if (LayoutData.IsValidIndex(StartRoomGridIndex))
	{
		StartCell = GetClosesCellIDToAnchorPoint(LayoutData[StartRoomGridIndex], StartCellAnchor);
	}
	else
	{
		UE_LOG(LevelGeneratorLog, Error, TEXT("ULevelManager::GetStartCellID - Failed to find start room."));
	}

	return StartCell;
}

void ULevelManager::SetStartCellVisibilityStatus(bool IsVisible)
{
	FIntPoint StartCell = GetStartCellID();

	if (StartCell.X != -1 && StartCell.Y != -1)
	{
		if (IsVisible)
		{
			SetCellVisibilityStatus(StartCell, -1); //-1 value is permanent visib
		}
		else
		{
			SetCellVisibilityStatus(StartCell, 500); //0 value is untill next tick. Put many ticks so the cell stays up longer to avoid issues with player rellocation in the same frame it hides.
		}
	}
}

TArray<FCellData> ULevelManager::GetCellsData()
{
	return CellsData;
}

FIntPoint ULevelManager::GetGridSize()
{
	return GridSize;
}

float ULevelManager::GetCellSize()
{
	return CellSize;
}

void ULevelManager::MoveLevels() //This is test only function, might delete later 
{
	TArray <int32> LevelsTracked = TArray <int32>();
	LoadedLevelsMap.GetKeys(LevelsTracked);

	FLoadedLevelTrack track = FLoadedLevelTrack();
	FTransform Transform = FTransform();

	for (const int32& level : LevelsTracked)
	{
		track = *LoadedLevelsMap.Find(level);

		if (track.LevelStreaming)
		{
			Transform = track.LevelStreaming->LevelTransform;
			Transform.SetLocation(Transform.GetLocation() + FVector(500, 0, 0));

			SetLevelTransform(track.LevelStreaming, Transform);
		}
	}
}

TArray<int32> ULevelManager::GetLevelPlacementDataForVisibleCells() const
{
	if (ForcedVisibleCells.Num() <= 0) return TArray<int32>();

	int ElementPosition = 0;
	TArray<int32> CloseLevels = TArray<int32>();

	for (int i = 0; i < ForcedVisibleCells.Num(); i++)
	{
		ElementPosition = ULevelGeneratorFunctionLibrary::FindCellIDIndex(ForcedVisibleCells[i].Cell_ID, CellsData);

		if (CellsData.IsValidIndex(ElementPosition))
		{
			if (!CloseLevels.Contains(CellsData[ElementPosition].LevelPlacementDataIndex))
			{
				CloseLevels.Add(CellsData[ElementPosition].LevelPlacementDataIndex);
			}
		}
	}

	return CloseLevels;
}

TArray<int32> ULevelManager::GetLevelPlacementDataBetweenPositions(const TArray<FVector>& PositionsIn, const float& MinDistanceIn, const float& MaxDistanceIn, const TArray<int32>& VisibleLevelsIn) const
{
	TArray<int32> CloseLevels = VisibleLevelsIn; //If the levels are already reserved they need to be considered

	if (PositionsIn.Num() <= 0 || CellsData.Num() <= 0)
	{
		return CloseLevels;
	}

	FVector CurrentCellLocation = FVector::ZeroVector;
	float CurrentDistance = 0;
	for (int i = 0; i < CellsData.Num(); i++)
	{
		if (!CloseLevels.Contains(CellsData[i].LevelPlacementDataIndex))
		{
			CurrentCellLocation = ULevelGeneratorFunctionLibrary::GetCellWorldPosition(CellsData[i].Cell_ID, CellSize, InitialLocation);

			for (int j = 0; j < PositionsIn.Num(); j++)
			{			
				CurrentDistance = FVector::Dist2D(PositionsIn[j], CurrentCellLocation);

				if (CurrentDistance < MaxDistanceIn && CurrentDistance >= MinDistanceIn)
				{
					CloseLevels.AddUnique(CellsData[i].LevelPlacementDataIndex);
					break;//no need to check other positions for the position array
				}
			}
		}
	}

	return CloseLevels;
}

TArray<int32> ULevelManager::GetCellsIndexFromPlacementDataArray(const TArray<int32>& PlacementDataLevelsIn) const
{
	TArray<int32> CellsOut = TArray<int32>();

	for (int i = 0; i < CellsData.Num(); i++)
	{
		if (PlacementDataLevelsIn.Contains(CellsData[i].LevelPlacementDataIndex))
		{
			CellsOut.Add(i);
		}
	}	
	
	return CellsOut;
}

TArray<int32> ULevelManager::GetLoadedLevelsPlacementData(const TMap<int32, FLoadedLevelTrack>& LoadedLevelsTrack) const
{
	TArray<int32> LevelsDataLocal = TArray<int32>();
	TArray<int32> Keys = TArray<int32>();
	LoadedLevelsTrack.GetKeys(Keys);
	FLoadedLevelTrack track = FLoadedLevelTrack();

	for (const int32& key : Keys)
	{
		track = *LoadedLevelsTrack.Find(key);

		if (track.State == ELevelGen_LevelState::Loaded || track.State == ELevelGen_LevelState::Loading)
		{
			LevelsDataLocal.Add(key);
		}
	}
	
	return LevelsDataLocal;
}

bool ULevelManager::IsCellCloseToPositions(const int32& CellDataIndexIn, const TArray<FVector>& PositionsIn, const float& DistanceIn) const
{
	FVector CurrentCellLocation = ULevelGeneratorFunctionLibrary::GetCellWorldPosition(CellsData[CellDataIndexIn].Cell_ID, CellSize, InitialLocation);
	
	float CurrentDistance = 0;
	bool IsClose = false;
	for (int j = 0; j < PositionsIn.Num(); j++)
	{			
		CurrentDistance = FVector::Dist2D(PositionsIn[j], CurrentCellLocation);

		if (CurrentDistance <= DistanceIn)
		{			
			IsClose = true;
			break;
		}
	}
	
	return IsClose;
}

FGameplayTag ULevelManager::GetCellProminentBiome(const FIntPoint& CellIDIn, const TArray<FCellData>& CellsDataIn, const TArray<FLevel_TransitionCellData>& TransitionCellsDataIn)
{
	int32 CellIndex = ULevelGeneratorFunctionLibrary::FindCellIDIndex(CellIDIn, CellsDataIn);
	
	if (CellsData.IsValidIndex(CellIndex))
	{
		if (CellsData[CellIndex].Cell_Biome == BiomeTransitionTag)
		{
			return ULevelGeneratorFunctionLibrary::GetProminentTransitionBiome(CellIDIn, TransitionCellsDataIn);
		}
		else
		{
			return CellsData[CellIndex].Cell_Biome;
		}
	}

	return FGameplayTag();
}

int32 ULevelManager::GetMinimunDistanceToCells(FIntPoint OrigenIDIn, int MaxDistanceIn) const
{
	int CellIDPosition = ULevelGeneratorFunctionLibrary::FindCellIDIndex(OrigenIDIn, CellsData);
	if (CellsData.IsValidIndex(CellIDPosition))
	{
		return 0;
	}
		
	int CurrentDistance = 1;
	int X_Min = 0;
	int X_Max = 0;
	int Y_Min = 0;
	int Y_Max = 0;

	FIntPoint CellSizeLocal = GridSize;

	bool WhileEnable = true;
	bool FoundDistance = false;
	while (WhileEnable && CurrentDistance <= MaxDistanceIn)
	{
		X_Min = OrigenIDIn.X - CurrentDistance;
		X_Max = OrigenIDIn.X + CurrentDistance + 1;
		Y_Min = OrigenIDIn.Y - CurrentDistance;
		Y_Max = OrigenIDIn.Y + CurrentDistance + 1;

		X_Min = FMath::Clamp(X_Min, 0, CellSizeLocal.X);
		X_Max = FMath::Clamp(X_Max, 0, CellSizeLocal.X);
		Y_Min = FMath::Clamp(Y_Min, 0, CellSizeLocal.Y);
		Y_Max = FMath::Clamp(Y_Max, 0, CellSizeLocal.Y);

		for (int32 i = X_Min; i < X_Max; i++)
		{
			for (int32 j = Y_Min; j < Y_Max; j++)
			{
				CellIDPosition = ULevelGeneratorFunctionLibrary::FindCellIDIndex({ i, j }, CellsData);

				if (CellsData.IsValidIndex(CellIDPosition))
				{
					FoundDistance = true;
					break;
				}
			}

			if (FoundDistance)
			{
				break;
			}
		}

		if (FoundDistance)
		{
			WhileEnable = false;
		}
		else
		{
			CurrentDistance++;
		}
	}

	return CurrentDistance;
}

FIntPoint ULevelManager::GetClosesCellIDToAnchorPoint(const FLayoutData& LayoutDataIn, FVector2D AnchorIn) const
{
	FVector2D AnchorLocal = FVector2D::ZeroVector;
	AnchorLocal.X = FMath::Clamp(AnchorIn.X, 0.f, 1.f);
	AnchorLocal.Y = FMath::Clamp(AnchorIn.Y, 0.f, 1.f);

	FIntPoint Size = FIntPoint();
	FIntPoint Position = FIntPoint();
	Position = ULevelGeneratorFunctionLibrary::GetCellsPositionAndSize(LayoutDataIn.CellsData, Size);
	
	//Determine element. This element is fake and might no be in the array.
	FVector2D CellElementVector = FIntPoint();
	CellElementVector.X = FMath::RoundToFloat((Size.X - 1) * AnchorLocal.X + Position.X);
	CellElementVector.Y = FMath::RoundToFloat((Size.Y - 1) * AnchorLocal.Y + Position.Y);
	FIntPoint CellElement = FIntPoint();
	CellElement.X = CellElementVector.X;
	CellElement.Y = CellElementVector.Y;

	//Calculate cell with lesser distance to fake element
	float DistanceCurrent = 0;
	float minDistance = 999999999999;
	FIntPoint ClosestCell = FIntPoint();
	for (const FCellData& cell: LayoutDataIn.CellsData)
	{
		DistanceCurrent = ULevelGeneratorFunctionLibrary::GetWorldDistanceBetweenCells(cell.Cell_ID, CellElement, CellSize, InitialLocation);

		if (DistanceCurrent < minDistance)
		{
			minDistance = DistanceCurrent;
			ClosestCell = cell.Cell_ID;
		}		
	}

	return ClosestCell;
}

float ULevelManager::GetWorldDistanceBetweenRooms(const FLayoutData& RoomA, const FLayoutData& RoomB) const
{
	FVector RoomACenter = ULevelGeneratorFunctionLibrary::GetRoomWorldPosition(RoomA, CellSize, { .5,.5 });
	FVector RoomBCenter = ULevelGeneratorFunctionLibrary::GetRoomWorldPosition(RoomB, CellSize, { .5,.5 });
	return FVector::Dist2D(RoomACenter, RoomBCenter);
}

TArray<FIntPoint> ULevelManager::GetLevelCellsIDs(int LevelPlacementDataIn) const
{
	TArray<FIntPoint> Cells = TArray<FIntPoint>();
	if (!LevelPlacementData.IsValidIndex(LevelPlacementDataIn)) return Cells;
	
	for (int i = 0; i < CellsData.Num(); i++)
	{
		if (CellsData[i].LevelPlacementDataIndex == LevelPlacementDataIn)
		{
			Cells.Add(CellsData[i].Cell_ID);
		}
	}
	
	return Cells;
}

int ULevelManager::GetClosestLayoutIndexToAnchor(const TArray<FLayoutData>& LayoutDataIn, ELevelGen_LevelType TypeIn, FVector2D AnchorIn) const
{
	FVector AnchorWorldPosition = GetGridWorldLocationInAnchor(AnchorIn);

	FVector CurrentRoomLocation = FVector::ZeroVector;
	float Distance = 0;
	float MinDistance = 1000000;

	int SubgridRoomIndex = -1;

	for (int i = 0; i < LayoutDataIn.Num(); i++)
	{
		if (LayoutDataIn[i].Type == TypeIn)
		{
			CurrentRoomLocation = ULevelGeneratorFunctionLibrary::GetRoomWorldPosition(LayoutDataIn[i], CellSize, { .5,.5 });
			Distance = FVector::Dist2D(AnchorWorldPosition, CurrentRoomLocation);

			if (Distance < MinDistance)
			{
				MinDistance = Distance;
				SubgridRoomIndex = i;
			}
		}
	}

	return SubgridRoomIndex;
}

FVector ULevelManager::GetGridWorldLocationInAnchor(FVector2D AnchorIn) const
{
	FVector2D AnchorLocal = FVector2D::ZeroVector;
	AnchorLocal.X = FMath::Clamp(AnchorIn.X, 0.f, 1.f);
	AnchorLocal.Y = FMath::Clamp(AnchorIn.Y, 0.f, 1.f);

	FVector AnchorWorldPosition = FVector::ZeroVector;
	AnchorWorldPosition.X = AnchorLocal.Y * GridSize.Y * CellSize;
	AnchorWorldPosition.Y = AnchorLocal.X * GridSize.X * CellSize;
	AnchorWorldPosition = AnchorWorldPosition + InitialLocation;

	return AnchorWorldPosition;
}

TArray<FLevel_RoomDistance> ULevelManager::GenerateInitialCorridorsLayoutData(const TArray<FLayoutData>& LayoutDataIn) const
{		
	if (LayoutDataIn.Num() < 2)
	{
		UE_LOG(LevelGeneratorLog, Error, TEXT("ULevelManager::GenerateInitialCorridorsLayoutData - Not enough rooms to connect."));
		return TArray<FLevel_RoomDistance> ();
	}

	//Rooms that are already connected to the three
	TArray<int32> ThreeRooms = TArray<int32>();
	ThreeRooms.Add(GetClosestLayoutIndexToAnchor(LayoutData, ELevelGen_LevelType::Normal, StartRoomAnchor)); // Generate start from start room
	UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::GenerateInitialCorridorsLayoutData - Start room: %i."), ThreeRooms[0]);

	//Build all distances to all rooms
	TArray<FLevel_RoomDistance> AllCorridorsDistance = TArray<FLevel_RoomDistance>();
	FLevel_RoomDistance CurrentCorridorDistance = FLevel_RoomDistance();
	int32 jmin = 0;
	for (int i = 0; i < LayoutDataIn.Num(); i++)
	{
		for (int j = jmin; j < LayoutDataIn.Num(); j++)
		{
			if (LayoutDataIn[i].GridID != LayoutDataIn[j].GridID)
			{
				CurrentCorridorDistance.Distance = GetWorldDistanceBetweenRooms(LayoutDataIn[i], LayoutDataIn[j]);
				CurrentCorridorDistance.RoomA = LayoutDataIn[i].GridID;
				CurrentCorridorDistance.RoomB = LayoutDataIn[j].GridID;

				AllCorridorsDistance.Add(CurrentCorridorDistance);				
			}
		}

		jmin++;
	}
	
	FLevel_RoomDistance CurrentRoomDistance = FLevel_RoomDistance();
	int32 RoomDistanceIndex = 0;
	TArray<FLevel_RoomDistance> ThreeCorridorsLayout = TArray<FLevel_RoomDistance>();
	FIntPoint Layout = FIntPoint();
	int whileCounter = 0;
	bool WhileEnable = true;
	bool AddCorridor = true;
	while (WhileEnable && whileCounter < LayoutDataIn.Num())  //The while counter should be equal to the rooms - 1
	{
		CurrentRoomDistance = GetMinimunRoomDistancePaths(ThreeRooms, AllCorridorsDistance, RoomDistanceIndex);

		//Add the new room
		AddCorridor = true;
		if (!ThreeRooms.Contains(CurrentRoomDistance.RoomA))
		{
			ThreeRooms.Add(CurrentRoomDistance.RoomA);
		}
		else if(!ThreeRooms.Contains(CurrentRoomDistance.RoomB))
		{
			ThreeRooms.Add(CurrentRoomDistance.RoomB);
		}
		else
		{
			//This should not happen
			AddCorridor = false;
			UE_LOG(LevelGeneratorLog, Error, TEXT("ULevelManager::GenerateInitialCorridorsLayoutData - Corridor with rooms %i and %i is not valid. All rooms are in the three. Distance: %i."), CurrentRoomDistance.RoomA, CurrentRoomDistance.RoomB, CurrentRoomDistance.Distance);
		}

		if (AddCorridor)
		{
			ThreeCorridorsLayout.Add(CurrentRoomDistance);
		}
		
		//Remove the corridor from the usable ones
		AllCorridorsDistance.RemoveAt(RoomDistanceIndex);

		//Are all rooms connected?
		if (ThreeRooms.Num() == LayoutDataIn.Num())
		{
			WhileEnable = false;
		}

		if (AllCorridorsDistance.Num() <= 0)
		{
			WhileEnable = false;
			UE_LOG(LevelGeneratorLog, Error, TEXT("ULevelManager::GenerateInitialCorridorsLayoutData - There is no more corridors left. Corridors generation incomplete."));
		}

		whileCounter++;
	}

	UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::GenerateInitialCorridorsLayoutData - Connected %i rooms with %i while counter."), ThreeRooms.Num(), whileCounter);

	//Add some of the unused corridors to create circular paths.
	if (CircularCorridorsAmountPercent > 0)
	{
		AddUnusedCorridors(AllCorridorsDistance, ThreeCorridorsLayout);
	}

	return ThreeCorridorsLayout;	
}

FLevel_RoomDistance ULevelManager::GetMinimunRoomDistancePaths(const TArray<int32>& ThreeRoomsIn, const TArray<FLevel_RoomDistance>& RoomsDistanceIn, int32& RoomDistanceIndexOut) const
{
	FLevel_RoomDistance roomDistanceLocal = FLevel_RoomDistance();
	int32 SmallestDistance = 10000000;
	int32 index = -1;
	for (int i = 0; i < RoomsDistanceIn.Num(); i++)
	{
		if ((ThreeRoomsIn.Contains(RoomsDistanceIn[i].RoomA) && !ThreeRoomsIn.Contains(RoomsDistanceIn[i].RoomB)) ||
			(ThreeRoomsIn.Contains(RoomsDistanceIn[i].RoomB) && !ThreeRoomsIn.Contains(RoomsDistanceIn[i].RoomA)))
		{
			if (RoomsDistanceIn[i].Distance < SmallestDistance)
			{
				roomDistanceLocal = RoomsDistanceIn[i];
				SmallestDistance = RoomsDistanceIn[i].Distance;
				index = i;
			}
		}			
	}

	RoomDistanceIndexOut = index;

	return roomDistanceLocal;
}

void ULevelManager::AddUnusedCorridors(TArray<FLevel_RoomDistance>& UnusedCorridorsIn, TArray<FLevel_RoomDistance>& ThreeCorridorsOut) const
{	
	TArray<FLevel_RoomDistance> FilteredCorridors = TArray<FLevel_RoomDistance>();

	for (const FLevel_RoomDistance& RoomDistance: UnusedCorridorsIn)
	{	
		if ((RoomDistance.Distance/CellSize) < MaxCircularCorridorsCellDistance)
		{
			FilteredCorridors.Add(RoomDistance);
		}				
	}

	int CorridorsAmount = CircularCorridorsAmountPercent * GeneratedRoomsAmount;

	int RandomRoll = 0;
	for (int i = 0; i < CorridorsAmount; i++)
	{
		RandomRoll = UKismetMathLibrary::RandomIntegerInRange(0, FilteredCorridors.Num()); //@TODO: Should randomize with a stream?
		if (FilteredCorridors.IsValidIndex(RandomRoll))
		{
			ThreeCorridorsOut.Add(FilteredCorridors[RandomRoll]);
		}
	}
}

FVector ULevelManager::GetPlacedLevelPosition(int LevelPlacementDataIndex) const
{
	if (!LevelPlacementData.IsValidIndex(LevelPlacementDataIndex))
	{		
		return FVector::ZeroVector;
	}

	FVector CurrentLevelLocation = FVector::ZeroVector;
	CurrentLevelLocation.X = LevelPlacementData[LevelPlacementDataIndex].GridPosition.Y * CellSize + InitialLocation.X; //Because of world coordinates, i need to swap X and Y in the positions
	CurrentLevelLocation.Y = LevelPlacementData[LevelPlacementDataIndex].GridPosition.X * CellSize  + InitialLocation.Y;
	CurrentLevelLocation.Z = InitialLocation.Z;
	
	int CurrenLevelSize = ULevelGeneratorFunctionLibrary::GetMaxCellSideSize(LevelsData[LevelPlacementData[LevelPlacementDataIndex].LevelDataIndex].CellsData);

	switch (LevelPlacementData[LevelPlacementDataIndex].RotationIndex)
	{
	case 1:
	{
		CurrentLevelLocation.X = CurrentLevelLocation.X + CurrenLevelSize * CellSize;
		break;
	}	
	case 2:
	{
		CurrentLevelLocation.X = CurrentLevelLocation.X + CurrenLevelSize * CellSize;
		CurrentLevelLocation.Y = CurrentLevelLocation.Y + CurrenLevelSize * CellSize;
		break;
	}
	case 3: 
	{
		CurrentLevelLocation.Y = CurrentLevelLocation.Y + CurrenLevelSize * CellSize;
		break;
	}
	default:
		break;
	}

	return CurrentLevelLocation;
}

FRotator ULevelManager::GetPlacedLevelRotation(int LevelPlacementDataIndex) const
{
	if (!LevelPlacementData.IsValidIndex(LevelPlacementDataIndex))
	{
		return FRotator::ZeroRotator;
	}		


	FRotator CurrentLevelRotation = FRotator::ZeroRotator;	
	CurrentLevelRotation.Yaw = 90 * LevelPlacementData[LevelPlacementDataIndex].RotationIndex;
		
	return CurrentLevelRotation;
}

void ULevelManager::ClearLevelsPool()
{		
	TArray <int32> KeysLoadedLevels = TArray <int32>();
	LevelsPool.GetKeys(KeysLoadedLevels);

	for (int i = 0; i < KeysLoadedLevels.Num(); i++)
	{
		if (LevelsPool.Contains(KeysLoadedLevels[i]))
		{
			FLevelPoolTrack LevelPoolTrack = FLevelPoolTrack();

			FLevelPoolTrack* LevelPoolTrackPointer = LevelsPool.Find(KeysLoadedLevels[i]);

			if (LevelPoolTrackPointer)
			{
				LevelPoolTrack = *LevelPoolTrackPointer;

				for (int32 j = 0; j < LevelPoolTrack.Levels.Num(); j++)
				{
					if (LevelPoolTrack.Levels[j])
					{			
						UGameplayStatics::UnloadStreamLevelBySoftObjectPtr(this, LevelPoolTrack.Levels[j], FLatentActionInfo(), true);
					}
				}
			}			
		}
	}

	LoadedLevelsMap.Empty();
}

int32 ULevelManager::GetLevelsPoolNumber()
{
	TArray <int32> LevelsChecked = TArray <int32>();

	int32 LevelsPooled = 0;
	
	for (int32 i = 0; i < LevelPlacementData.Num(); i++)
	{
		if (!LevelsChecked.Contains(LevelPlacementData[i].LevelDataIndex))
		{
			LevelsPooled += GetLevelDataPoolMinimunLevels(LevelPlacementData[i].LevelDataIndex);
			LevelsChecked.AddUnique(LevelPlacementData[i].LevelDataIndex);
		}
	}

	UE_LOG(LevelGeneratorLog, Error, TEXT("ULevelManager::GetLevelsPoolNumber - Levels Pool Size: %i."), LevelsPooled);

	return LevelsPooled;
}

void ULevelManager::GenerateLevelsPool()
{
	TArray <int32> LevelsChecked = TArray <int32>();
	int32 RequiredPoolLevels = 0;

	int32 LevelsPooled = 0;
	FVector PoolLevelLocation = FVector::ZeroVector;

	for (int32 i = 0; i < LevelPlacementData.Num(); i++)
	{
		if (!LevelsChecked.Contains(LevelPlacementData[i].LevelDataIndex))
		{
			RequiredPoolLevels = GetLevelDataPoolMinimunLevels(LevelPlacementData[i].LevelDataIndex);

			for (int j = 0; j < RequiredPoolLevels; j++)
			{
				//Create level instance	
				LevelsPooled++; //Current index	
				FString LevelNameLocal = "Level_Pool_" + FString::FromInt(LevelsPooled);
				bool Succes;

				ULevelStreamingDynamic* LevelDynamic = ULevelStreamingDynamic::LoadLevelInstance(this, LevelsData[LevelPlacementData[i].LevelDataIndex].LevelName,
					PoolLevelLocation, FRotator(), Succes, LevelNameLocal); //Do not rotate pooled levels initially.
				
				if (LevelDynamic)
				{
					LevelDynamic->SetShouldBeVisible(false);
					LevelDynamic->bShouldBlockOnLoad = true;
					LevelDynamic->bShouldBlockOnUnload = false;

					LevelDynamic->LevelColor = FColor::MakeRandomColor();
					LevelDynamic->SetShouldBeLoaded(true);
					LevelDynamic->bInitiallyLoaded = true;
					LevelDynamic->bInitiallyVisible = false;

					AddLevelToPool(LevelPlacementData[i].LevelDataIndex, LevelDynamic);

					LevelDynamic->OnLevelLoaded.AddDynamic(this, &ULevelManager::OnPoolLevelInstanceLoaded);
				}
				else
				{
					UE_LOG(LevelGeneratorLog, Error, TEXT("ULevelManager::GenerateLevelsPool - Failed to create a level for level: %s."), *LevelsData[LevelPlacementData[i].LevelDataIndex].LevelName);
				}
			}	

			LevelsChecked.AddUnique(LevelPlacementData[i].LevelDataIndex);
		}
	}

	return;
}

int32 ULevelManager::GetLevelDataPoolMinimunLevels(int32 LevelDataIndexIn) const
{
	int32 LevelsInRadius = 0;
	int32 MinLevelsCounter = 0;

	FVector LevelPosition = FVector::ZeroVector;
	FVector CurrentLevelPosition = FVector::ZeroVector;

	for (int32 i = 0; i < LevelPlacementData.Num(); i++)
	{
		if (LevelPlacementData[i].LevelDataIndex == LevelDataIndexIn)
		{
			//Determine all levels of same index in radius
			LevelsInRadius = 1;//reset counters. Start in 1 since the self map is counted.

			LevelPosition = GetPlacedLevelPosition(i);

			//Search other levels of same placement data at unload distance of this one.
			for (int32 j = 0; j < LevelPlacementData.Num(); j++)
			{
				if (i != j)
				{
					if (LevelPlacementData[j].LevelDataIndex == LevelDataIndexIn)
					{
						CurrentLevelPosition = GetPlacedLevelPosition(j);

						if (FVector::Dist2D(LevelPosition, CurrentLevelPosition) <= LoadStreamLevelDistance + UnloadStreamLevelTolerance)
						{
							LevelsInRadius++;
						}
					}
				}
			}

			if (LevelsInRadius > MinLevelsCounter)
			{
				MinLevelsCounter = LevelsInRadius;
			}
		}
	}

	return MinLevelsCounter;
}

void ULevelManager::AddLevelToPool(int32 LevelDataIndexIn, ULevelStreamingDynamic* StreamingLevel)
{
	if (!UseLevelsPool)
	{
		return;
	}	

	FLevelPoolTrack LevelPoolTrack =  FLevelPoolTrack();

	if (LevelsPool.Contains(LevelDataIndexIn))
	{
		FLevelPoolTrack* LevelPoolTrackPointer = LevelsPool.Find(LevelDataIndexIn);
		
		if (LevelPoolTrackPointer)
		{
			LevelPoolTrack = *LevelPoolTrackPointer;			
			LevelPoolTrack.Levels.AddUnique(StreamingLevel);					
			LevelsPool[LevelDataIndexIn] = LevelPoolTrack;			
		}
	}
	else
	{
		LevelPoolTrack.Levels.Add(StreamingLevel);
		LevelsPool.Add(LevelDataIndexIn, LevelPoolTrack);
	}	
}

void ULevelManager::RemoveLevelFromPool(int32 LevelDataIndexIn, ULevelStreamingDynamic* StreamingLevel)
{
	if (!UseLevelsPool)
	{
		return;
	}

	if (!LevelsPool.Contains(LevelDataIndexIn))
	{
		return;
	}

	FLevelPoolTrack* LevelPoolTrackPointer = LevelsPool.Find(LevelDataIndexIn);
	int32 index = -1;
	if (LevelPoolTrackPointer)
	{
		FLevelPoolTrack LevelPoolTrack = *LevelPoolTrackPointer;
		for (int32 i = 0; i < LevelPoolTrack.Levels.Num(); i++)
		{
			if (LevelPoolTrack.Levels[i] == StreamingLevel)
			{
				index = i;
				break;
			}
		}		

		if (LevelPoolTrack.Levels.IsValidIndex(index))
		{
			LevelPoolTrack.Levels.RemoveAt(index);
			LevelsPool[LevelDataIndexIn] = LevelPoolTrack;
		}
	}
}

bool ULevelManager::IsLevelAvailableInPool(int32 LevelDataIndexIn)
{
	if (!UseLevelsPool)
	{
		return false;
	}

	if (IsGeneratingLevel())
	{
		return false;
	}

	if (!LevelsPool.Contains(LevelDataIndexIn))
	{
		return false;
	}

	FLevelPoolTrack* LevelPoolTrackPointer = LevelsPool.Find(LevelDataIndexIn);
	
	if (LevelPoolTrackPointer)
	{
		FLevelPoolTrack LevelPoolTrack = *LevelPoolTrackPointer;

		if (LevelPoolTrack.Levels.Num() > 0)
		{
			if (LevelPoolTrack.Levels[0])
			{
				return true; //level 0 is valid. Always use the level 0. Assumming all levels in the pool are valid. If not should check all other levels and remove invalids
			}
		}
	}
		
	return false;
}

bool ULevelManager::SetLevelTransform(ULevelStreaming* StreamingLevel, const FTransform& Transform)
{
	if (StreamingLevel == nullptr)
	{		
		return false;
	}

	if (!StreamingLevel->IsLevelVisible())
	{	
		return false;
	}

	if (StreamingLevel->LevelTransform.Equals(Transform))
	{
		return false;
	}
	
	StreamingLevel->Modify();
	RemoveLevelTransform(StreamingLevel);
	StreamingLevel->LevelTransform = Transform;

	ULevel* LoadedLevel = StreamingLevel->GetLoadedLevel();
	if (LoadedLevel != NULL)
	{
		ApplyLevelTransform(LoadedLevel, StreamingLevel->LevelTransform);		
	}
	
	return true;
}

void ULevelManager::RemoveLevelTransform(const ULevelStreaming* StreamingLevel)
{
	check(StreamingLevel);
	ULevel* LoadedLevel = StreamingLevel->GetLoadedLevel();
	if (LoadedLevel != NULL)
	{
		ApplyLevelTransform(LoadedLevel, StreamingLevel->LevelTransform.Inverse());
	}
}

void ULevelManager::ApplyLevelTransform(ULevel* Level, const FTransform& LevelTransform)
{
	bool bTransformActors = !LevelTransform.Equals(FTransform::Identity);
	if (bTransformActors)
	{
		if (!LevelTransform.GetRotation().IsIdentity())
		{
			// If there is a rotation applied, then the relative precomputed bounds become invalid.
			Level->bTextureStreamingRotationChanged = true;
		}
		
		// Iterate over all actors in the level and transform them
		for (int32 ActorIndex = 0; ActorIndex < Level->Actors.Num(); ActorIndex++)
		{
			AActor* Actor = Level->Actors[ActorIndex];
			
			// Don't want to transform children they should stay relative to there parents.
			if (Actor && Actor->GetAttachParentActor() == NULL)
			{
				// Has to modify root component directly as GetActorPosition is incorrect this early
				USceneComponent* RootComponent = Actor->GetRootComponent();
				if (RootComponent)
				{
					TEnumAsByte<EComponentMobility::Type> ComponentMobilityTemp = RootComponent->Mobility;
					RootComponent->Mobility = EComponentMobility::Movable;					
					RootComponent->SetRelativeLocationAndRotation(LevelTransform.TransformPosition(RootComponent->GetRelativeLocation()), LevelTransform.TransformRotation(RootComponent->GetRelativeRotation().Quaternion()));
					RootComponent->Mobility = ComponentMobilityTemp;
				}
				else
				{
					FString Name = *Actor->GetFName().ToString();
					UE_LOG(LevelGeneratorLog, Error, TEXT("ULevelManager::ApplyLevelTransform - Actor %s has no root. Transform failed."), *Name);
				}
			}
		}
		
		Level->MarkLevelBoundsDirty();
		Level->OnApplyLevelTransform.Broadcast(LevelTransform);
	}
}

FString ULevelManager::GetLevelInstanceNameOverride(int LevelPlacementDataIndex) const
{
	FString levelNameOverride = "Level" + FString::FromInt(LevelPlacementDataIndex + 10); //offset with a arbitrary index to avoid 0
	return levelNameOverride;
}

void ULevelManager::CreateLevelInstance(int LevelPlacementDataIndex, bool ForceAsyncOnLoad)
{
	if (!LevelPlacementData.IsValidIndex(LevelPlacementDataIndex))
	{
		UE_LOG(LevelGeneratorLog, Error, TEXT("ULevelManager::CreateLevelInstance - Invalid placement data provided."));
		return;
	}

	ULevelStreamingDynamic* LevelDynamic = nullptr;
	bool Succes;
	
	if(IsLevelAvailableInPool(LevelPlacementData[LevelPlacementDataIndex].LevelDataIndex))
	{
		LevelDynamic = LevelsPool[LevelPlacementData[LevelPlacementDataIndex].LevelDataIndex].Levels[0]; //For now is always using index 0 of the pool

	//	SetLevelTransform(LevelDynamic, FTransform(GetPlacedLevelRotation(LevelPlacementDataIndex), GetPlacedLevelPosition(LevelPlacementDataIndex)));

		RemoveLevelFromPool(LevelPlacementData[LevelPlacementDataIndex].LevelDataIndex, LevelDynamic);
		
		if (LevelDynamic->OnLevelShown.IsBound())
		{
			LevelDynamic->OnLevelShown.RemoveDynamic(this, &ULevelManager::OnLevelInstanceLoaded);
		}

		if (LevelDynamic->OnLevelLoaded.IsBound())
		{
			LevelDynamic->OnLevelLoaded.RemoveDynamic(this, &ULevelManager::OnPoolLevelInstanceLoaded);
		}		
	
		Succes = true;
	}
	else
	{
		LevelDynamic = ULevelStreamingDynamic::LoadLevelInstance(this, LevelsData[LevelPlacementData[LevelPlacementDataIndex].LevelDataIndex].LevelName,
			GetPlacedLevelPosition(LevelPlacementDataIndex), GetPlacedLevelRotation(LevelPlacementDataIndex), Succes, GetLevelInstanceNameOverride(LevelPlacementDataIndex), nullptr, false);

		UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::CreateLevelInstance - Level instance created: %s of map name: %s."),
			*GetLevelInstanceNameOverride(LevelPlacementDataIndex), *LevelsData[LevelPlacementData[LevelPlacementDataIndex].LevelDataIndex].LevelName);
	}
	
	if (Succes && LevelDynamic)
	{
		LevelDynamic->SetShouldBeVisible(true);//When creating the level always set to visible
		LevelDynamic->bShouldBlockOnLoad = ForceAsyncOnLoad;
		LevelDynamic->bShouldBlockOnUnload = false;

		LevelDynamic->LevelColor = FColor::MakeRandomColor();
		LevelDynamic->SetShouldBeLoaded(true);
		LevelDynamic->bInitiallyLoaded = true;
		LevelDynamic->bInitiallyVisible = true;

		LevelDynamic->OnLevelShown.AddDynamic(this, &ULevelManager::OnLevelInstanceLoaded);

		UpdateLevelTrack(LevelPlacementDataIndex, LevelDynamic, ELevelGen_LevelState::Loading, true);
	}
	else
	{
		UE_LOG(LevelGeneratorLog, Error, TEXT("ULevelManager::CreateLevelInstance - Cannot create level level: %s, for map name: %s."),
			*("Level_" + FString::FromInt(LevelPlacementDataIndex)), *LevelsData[LevelPlacementData[LevelPlacementDataIndex].LevelDataIndex].LevelName);
	}
}

void ULevelManager::UpdateLevelTrack(int LevelPlacementDataIndexIn, ULevelStreamingDynamic* LevelStreamingIn, ELevelGen_LevelState StateIn, bool IsFirstTimeLoadedIn)
{	
	if (LoadedLevelsMap.Contains(LevelPlacementDataIndexIn))
	{
		LoadedLevelsMap.Remove(LevelPlacementDataIndexIn);
	}	

	FLoadedLevelTrack LocalLevelTrack = FLoadedLevelTrack();
	LocalLevelTrack.PlacementDataIndex = LevelPlacementDataIndexIn;
	LocalLevelTrack.State = StateIn;
	LocalLevelTrack.IsLoadedForFirstTime = IsFirstTimeLoadedIn;
	LocalLevelTrack.LevelStreaming = LevelStreamingIn;

	if (UseLevelsPool)
	{
		SetLevelTransform(LocalLevelTrack.LevelStreaming, FTransform(GetPlacedLevelRotation(LevelPlacementDataIndexIn), GetPlacedLevelPosition(LevelPlacementDataIndexIn)));
	}	

	LoadedLevelsMap.Add(LevelPlacementDataIndexIn, LocalLevelTrack);				
}

FLoadedLevelTrack ULevelManager::GetLevelTrack(int LevelPlacementDataIndexIn) const
{	
	if (LoadedLevelsMap.Contains(LevelPlacementDataIndexIn))
	{
		return LoadedLevelsMap[LevelPlacementDataIndexIn];
	}

	return FLoadedLevelTrack();
}

FLevel_ClientData ULevelManager::GetLevelClientData(int LevelPlacementDataIndexIn) const
{
	FLevel_ClientData Data = FLevel_ClientData();
	Data.LevelStreaming = GetLevelTrack(LevelPlacementDataIndexIn).LevelStreaming;
	Data.LevelPlacementDataIndex = LevelPlacementDataIndexIn;
	Data.IsTransition = false;	
	Data.UpdatedFoliage = false;
	Data.UpdatedLandscape = false;	
	Data.AllowFoliageReposition = LevelsData[LevelPlacementData[LevelPlacementDataIndexIn].LevelDataIndex].AllowFoliageReposition;

	for (int i = 0; i < TransitionCellsData.Num(); i++)
	{
		if (TransitionCellsData[i].CellData.LevelPlacementDataIndex == LevelPlacementDataIndexIn)
		{
			Data.IsTransition = true;
			Data.Biome_Up = TransitionCellsData[i].Biome_Up;
			Data.Biome_Down = TransitionCellsData[i].Biome_Down;
			Data.Biome_Left = TransitionCellsData[i].Biome_Left;
			Data.Biome_Right = TransitionCellsData[i].Biome_Right;
			break;//Only because my transition levels are only 1 cell, i can do this.
		}
	}

	if (!Data.IsTransition)
	{
		for (const FCellData& cell : CellsData)
		{
			if (cell.LevelPlacementDataIndex == LevelPlacementDataIndexIn)
			{
				Data.Biome_Up = cell.Cell_Biome; //All the level has the same biome. Just use UP
				break;
			}
		}	
	}
	
	return Data;
}

TArray<FLevel_ClientData> ULevelManager::GetLoadedLevelsClientData() const
{
	TArray<int32> PreviousTickLoadedLevels = GetLoadedLevelsPlacementData(LoadedLevelsMap);
	TArray<FLevel_ClientData> ClientDataLevels = TArray<FLevel_ClientData>();

	for (const int32& LevelPlacementDataIndex : PreviousTickLoadedLevels)
	{
		ClientDataLevels.Add(GetLevelClientData(LevelPlacementDataIndex));
	}

	return ClientDataLevels;
}

void ULevelManager::OnPoolLevelInstanceLoaded()
{
	if (IsGeneratingLevelsPool)
	{
		LoadedLevelsAmount++;
		int32 LevelLoadingPercent = 100 * LoadedLevelsAmount / StartLevelsAmount;
		LevelLoadingPercent = FMath::Max(0, 100);
		FString Message = FString::Printf(TEXT("Generating level pool: %i"), LevelLoadingPercent);
		OnLevelGenerationStateMessage.Broadcast(Message + "%");

		if (LoadedLevelsAmount == StartLevelsAmount)
		{
			IsGeneratingLevelsPool = false;			
			CreateAllStartAreaLevelInstances();
		}
	}
}

void ULevelManager::OnLevelInstanceLoaded()
{
	if (IsGeneratingLevelsPool)
	{
		return;
	}
	
	TArray <int32> LevelsTracked = TArray <int32>();
	LoadedLevelsMap.GetKeys(LevelsTracked);
	FLoadedLevelTrack track = FLoadedLevelTrack();
	for (const int32& level : LevelsTracked)
	{
		track = *LoadedLevelsMap.Find(level);

		if (track.State == ELevelGen_LevelState::Loading)
		{
			OnLevelCompletedLoading(level, track);
			break;
		}
	}

	if (IsGeneratingLevel())
	{
		LoadedLevelsAmount++;
		int32 LevelLoadingPercent = 100 * LoadedLevelsAmount / StartLevelsAmount;
		LevelLoadingPercent = FMath::Max(0, 100);
		FString Message = FString::Printf(TEXT("Loading level: %i"), LevelLoadingPercent);
		OnLevelGenerationStateMessage.Broadcast(Message + "%");

		UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::OnLevelInstanceLoaded - Instance Loaded: %i / %i. "), LoadedLevelsAmount, StartLevelsAmount);

		if (LoadedLevelsAmount == StartLevelsAmount)
		{
			OnAllLevelsLoaded.Broadcast();
			FinishStreamLoadStartLevels();
		}
	}				
}

void ULevelManager::OnLevelCompletedLoading(int32 LevelPlacementDataIndexIn, const FLoadedLevelTrack& LevelTrackIn)
{
	if (ActorsSpawnTrack.Contains(LevelPlacementDataIndexIn))
	{
		FLevel_ActorSpawnTrack LevelActorsTrack = FLevel_ActorSpawnTrack();
		LevelActorsTrack = *ActorsSpawnTrack.Find(LevelPlacementDataIndexIn);

		for (int32 i = 0; i < LevelActorsTrack.ActorsTrack.Num(); i++)
		{
			ChangeVisibilitySpawnedActor(LevelPlacementDataIndexIn, i, true);
		}					
	}
	
	UpdateLevelTrack(LevelPlacementDataIndexIn, LevelTrackIn.LevelStreaming, ELevelGen_LevelState::Loaded, false);		
	OnLevelVisibilityChanged.Broadcast(GetLevelClientData(LevelPlacementDataIndexIn), true);//This is not called from pool, so the level should be visible.
}

void ULevelManager::OnLevelStartUnloading(int32 LevelPlacementDataIndexIn)
{
	if (ActorsSpawnTrack.Contains(LevelPlacementDataIndexIn))
	{		
		FLevel_ActorSpawnTrack LevelActorsTrack = FLevel_ActorSpawnTrack();
		LevelActorsTrack = *ActorsSpawnTrack.Find(LevelPlacementDataIndexIn);

		for (int32 i = 0; i < LevelActorsTrack.ActorsTrack.Num(); i++)
		{
			ChangeVisibilitySpawnedActor(LevelPlacementDataIndexIn, i, false);
		}
	}
}

bool ULevelManager::IsLevelLoaded(int LevelPlacementDataIndex) const
{
	return LoadedLevelsMap.Contains(LevelPlacementDataIndex) && (LoadedLevelsMap[LevelPlacementDataIndex].State == ELevelGen_LevelState::Loaded || LoadedLevelsMap[LevelPlacementDataIndex].State == ELevelGen_LevelState::Loading);
}

bool ULevelManager::CanHideLevel(int LevelPlacementDataIndex) const
{
	TArray <FIntPoint> LevelCells = GetLevelCellsIDs(LevelPlacementDataIndex);
	
	bool CanHide = true;
	for (int i = 0; i < LevelCells.Num(); i++)
	{
		if (!CanHideCell(LevelCells[i]))
		{
			CanHide = false;
			break;
		}
	}
	
	return CanHide;
}

bool ULevelManager::CanHideCell(FIntPoint CellIDIn) const
{	
	FVector CellLocation = ULevelGeneratorFunctionLibrary::GetCellWorldPosition(CellIDIn, CellSize, InitialLocation);
	FVector BoxExtension = { CellSize / 2 ,CellSize / 2 ,5000 };
	
	TArray <AActor*> ActorsHit = TArray <AActor*>();
	TArray <AActor*> ActorsToIgnore = TArray <AActor*>();

	TArray<TEnumAsByte<EObjectTypeQuery>>  ObjectsType;
	ObjectsType.Add(EObjectTypeQuery::ObjectTypeQuery3);
	UKismetSystemLibrary::BoxOverlapActors(GetWorld(), CellLocation, BoxExtension, ObjectsType, APawn::StaticClass(), ActorsToIgnore, ActorsHit);
		
	return (ActorsHit.Num() <= 0); //If hits nothing then can hide cell
}

void ULevelManager::ChangeLevelVisibibility(int LevelPlacementDataIndex, bool IsVisible)
{
	FLoadedLevelTrack* LevelPointer = nullptr;
	FLoadedLevelTrack LevelTrack = FLoadedLevelTrack();

	if (LoadedLevelsMap.Contains(LevelPlacementDataIndex))
	{
		LevelPointer = LoadedLevelsMap.Find(LevelPlacementDataIndex);

		if (LevelPointer)
		{
			LevelTrack = *LevelPointer;		
		}		
	}

	if (LevelTrack.LevelStreaming)
	{		
		if (IsVisible)
		{
			LevelTrack.LevelStreaming->SetShouldBeVisible(true);
			UpdateLevelTrack(LevelPlacementDataIndex, LevelTrack.LevelStreaming, ELevelGen_LevelState::Loaded, false);
			OnLevelVisibilityChanged.Broadcast(GetLevelClientData(LevelPlacementDataIndex), IsVisible);

			RemoveLevelFromPool(LevelPlacementData[LevelPlacementDataIndex].LevelDataIndex, LevelTrack.LevelStreaming);

			UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::ChangeLevelVisibibility - Level is visible: %i."), LevelPlacementDataIndex);
		}
		else
		{
			LevelTrack.LevelStreaming->SetShouldBeVisible(false);
			OnLevelStartUnloading(LevelPlacementDataIndex);

			if (IsGeneratingLevelsPool)
			{
				UpdateLevelTrack(LevelPlacementDataIndex, nullptr, ELevelGen_LevelState::Unloaded, false); //Use a null pointer for the level here. This level has no valid level now since is now in the pool @TODO instead of null, could leave it and check if can reuse this if available in the pool when showing again? Maybe is an optimization.
			}
			else
			{
				UpdateLevelTrack(LevelPlacementDataIndex, LevelTrack.LevelStreaming, ELevelGen_LevelState::Unloaded, false);
			}

			OnLevelVisibilityChanged.Broadcast(GetLevelClientData(LevelPlacementDataIndex), IsVisible);
			AddLevelToPool(LevelPlacementData[LevelPlacementDataIndex].LevelDataIndex, LevelTrack.LevelStreaming);
			UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::ChangeLevelVisibibility - Level is hidden: %i."), LevelPlacementDataIndex);						
		}				
	}
	else
	{
		if (IsVisible)
		{
			UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::ChangeLevelVisibibility - Level %i not found. Creating new level instance."), LevelPlacementDataIndex);
			CreateLevelInstance(LevelPlacementDataIndex, RuntimeLevelsBlockOnLoad);	
			OnLevelVisibilityChanged.Broadcast(GetLevelClientData(LevelPlacementDataIndex), IsVisible);
		}
		else
		{
			UE_LOG(LevelGeneratorLog, Error, TEXT("ULevelManager::ChangeLevelVisibibility - Level %i not found. Cannot hide level."), LevelPlacementDataIndex);
		}	
	}
}

void ULevelManager::ClearLevel()
{
	PrimaryComponentTick.SetTickFunctionEnable(false);

	for (int i = 0; i < PopulateRoomActiveThreads.Num(); i++)
	{
		if (PopulateRoomActiveThreads[i])
		{
			delete PopulateRoomActiveThreads[i];
		}
	}

	for (int i = 0; i < CorridorLayoutActiveThreads.Num(); i++)
	{
		if (CorridorLayoutActiveThreads[i])
		{
			delete CorridorLayoutActiveThreads[i];
		}
	}

	for (int i = 0; i < RoomLayoutActiveThreads.Num(); i++)
	{
		if (RoomLayoutActiveThreads[i])
		{
			delete RoomLayoutActiveThreads[i];
		}
	}

	TArray <int32> KeysActorsTracks = TArray <int32>();
	ActorsSpawnTrack.GetKeys(KeysActorsTracks);

	for (int i = 0; i < KeysActorsTracks.Num(); i++)
	{		
		FLevel_ActorSpawnTrack LevelTrack = FLevel_ActorSpawnTrack();

		if (ActorsSpawnTrack.Contains(KeysActorsTracks[i]))
		{
			LevelTrack = *ActorsSpawnTrack.Find(KeysActorsTracks[i]);

			for (const FLevel_SingleActorSpawnTrack& track : LevelTrack.ActorsTrack)
			{
				if (track.ActorReference.IsValid())
				{
					track.ActorReference->Destroy();
				}
			}
		}		
	}
	ActorsSpawnTrack.Empty();
	
	TArray <int32> KeysLoadedLevels = TArray <int32>();
	LoadedLevelsMap.GetKeys(KeysLoadedLevels);

	for (int i = 0; i < KeysLoadedLevels.Num(); i++)
	{		
		if (LoadedLevelsMap.Contains(KeysLoadedLevels[i]))
		{
			FLoadedLevelTrack* LoadedLevelTrackPointer = nullptr;
			LoadedLevelTrackPointer = LoadedLevelsMap.Find(KeysLoadedLevels[i]);

			if (LoadedLevelTrackPointer)
			{
				FLoadedLevelTrack LoadedLevelTrack = *LoadedLevelTrackPointer;
				UGameplayStatics::UnloadStreamLevelBySoftObjectPtr(this, LoadedLevelTrack.LevelStreaming, FLatentActionInfo(), false);
			}	
		}
	}

	LoadedLevelsMap.Empty();
	LoadedLevelsAmount = 0;

	ClearLevelsPool(); //@TODO I could keep pools once generated or do it in a smart way that avoids creating so many instances again, etc, But since i dont call this function repetedly, is not big deal for now.

	bIsGeneratingLevel = false;
	IsLevelStreamingHandlingActive = false;	

	LevelPlacementData.Empty();
	CellsData.Empty();
	LayoutData.Empty();

	PopulateRoomActiveThreads.Empty();
	CorridorLayoutActiveThreads.Empty();
	RoomLayoutActiveThreads.Empty();

	ForcedVisibleCells.Empty();
	LayoutData.Empty();
	CorridorsLayoutData.Empty();
}

void ULevelManager::GenerateLevel(const TArray <FLevel_ActorSpawnData>& ActorsSpawnDataIn)
{
	ClearLevel();

	if (!UpdateLevelsData())
	{
		return;
	}

	bIsGeneratingLevel = true;
	OnLevelGenerationStateMessage.Broadcast("Generating level");		
	ActorsSpawnData = ActorsSpawnDataIn;
	UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::GenerateLevel - Started Rooms generation."));
	StartRoomsLayoutDataGeneration();	
}

bool ULevelManager::UpdateLevelsData()
{
	LevelsData.Empty();
	TArray<FLevelData, FDefaultAllocator> Data;
	
	if (!Levels)
	{
		UE_LOG(LevelGeneratorLog, Error, TEXT("ULevelManager::UpdateLevelsData - Levels datatable is invalid."));
		return false;
	}
		
	TArray <FName> RowNames = Levels->GetRowNames();
	FLevelData* Pointer = nullptr;
	FLevelData Struct = FLevelData();

	for (const FName& rowname : RowNames)
	{
		Pointer = Levels->FindRow<FLevelData>(rowname, FString());

		if (Pointer)
		{
			Struct = *Pointer;
			if (Struct.IsEnabled)
			{
				LevelsData.Add(Struct);
			}			
		}
	}

	return LevelsData.Num() > 0;
}

int32 ULevelManager::GenerateThreadID()
{
	int32 ID = UKismetMathLibrary::RandomIntegerInRange(0, 999999999);
	UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::GenerateThreadID - Generated ID: %i."), ID);
	return ID;
}

void ULevelManager::StartRoomsLayoutDataGeneration()
{
	TArray<int32> GeneratedRoomsAmountPerThread = TArray<int32>();

	int32 RoomsNumber = UKismetMathLibrary::RandomIntegerInRange(RoomsAmount.X, RoomsAmount.Y);
	RoomsNumber = FMath::Clamp(RoomsNumber, 1, RoomsAmount.Y); //at least 1 room

	while (RoomsNumber > 0)
	{
		if (RoomsNumber >= RoomsAmountPerArea)
		{
			GeneratedRoomsAmountPerThread.Add(RoomsAmountPerArea);
			RoomsNumber = RoomsNumber - RoomsAmountPerArea;
		}
		else
		{
			if (RoomsNumber > 0)
			{
				for (int32& RoomNum : GeneratedRoomsAmountPerThread)
				{
					RoomNum++;
					RoomsNumber--;

					if (RoomsNumber == 0)
					{
						break;
					}
				}
			}			
		}
	}
	
	for (int i = 0; i < GeneratedRoomsAmountPerThread.Num(); i++)
	{
		FRoomLayoutWorker* LayoutWorker = new FRoomLayoutWorker(GenerateThreadID(), GeneratedRoomsAmountPerThread[i], RoomsDistribution, MinRoomSize, RoomSeparationMode, MaxRoomSize, MinRoomSeparation, MaxRoomSeparation);
		RoomLayoutActiveThreads.Add(LayoutWorker);
	}

	PrimaryComponentTick.SetTickFunctionEnable(true);
}

bool ULevelManager::AreAllRoomLayoutThreadsCompleted() const
{
	bool AllAreCompleted = true;

	for (int i = 0; i < RoomLayoutActiveThreads.Num(); i++)
	{
		if (RoomLayoutActiveThreads[i])
		{
			if (!RoomLayoutActiveThreads[i]->bIsThreadCompleted)
			{
				AllAreCompleted = false;
				break;
			}
		}
	}
	return AllAreCompleted;
}

void ULevelManager::HandleRoomLayoutThreads()
{
	if (RoomLayoutActiveThreads.Num() <= 0)
	{
		return;
	}

	if (AreAllRoomLayoutThreadsCompleted())
	{
		//Generate room layouts to represent each thread/area
		TArray<FLayoutData> ThreadLayouts = TArray<FLayoutData>();
		FLayoutData CurrentThreadRoomLayout = FLayoutData();
		CurrentThreadRoomLayout.Type = ELevelGen_LevelType::Normal;

		for (int i = 0; i < RoomLayoutActiveThreads.Num(); i++)
		{
			CurrentThreadRoomLayout.Size = ULevelGeneratorFunctionLibrary::GetGridSize(RoomLayoutActiveThreads[i]->RoomsLayoutData);
			ThreadLayouts.Add(CurrentThreadRoomLayout);
		}

		FRandomStream Stream = FRandomStream(GenerateThreadID());
		Stream.GenerateNewSeed();
		ULevelGeneratorFunctionLibrary::DistributeRoomLayoutAroundGridCenter(Stream, AreaDistribution, MinAreaSeparation.Y, ThreadLayouts);
		ULevelGeneratorFunctionLibrary::ProcessRoomsLayout(Stream, MinAreaSeparation, MaxAreaSeparation, AreaSeparationMode, ThreadLayouts);

	
		TArray<FLayoutData> FinalRoomLayouts = TArray<FLayoutData>();	
		TArray<FGameplayTag> GeneratedBiomes = GenerateBiomesForAreas(RoomLayoutActiveThreads.Num());

		for (int32 i = 0; i < RoomLayoutActiveThreads.Num(); i++)
		{
			for (int32 j = 0; j < RoomLayoutActiveThreads[i]->RoomsLayoutData.Num(); j++)
			{
				CurrentThreadRoomLayout = RoomLayoutActiveThreads[i]->RoomsLayoutData[j];
				CurrentThreadRoomLayout.Position = CurrentThreadRoomLayout.Position + ThreadLayouts[i].Position + WallsCellSize; //Add walls displacement to ensure they can be placed
				CurrentThreadRoomLayout.GridID = FinalRoomLayouts.Num();
				CurrentThreadRoomLayout.Biome = GeneratedBiomes[i];
				FinalRoomLayouts.Add(CurrentThreadRoomLayout);
			}
		}
		LayoutData = FinalRoomLayouts;
		
		for (int i = 0; i < LayoutData.Num(); i++)
		{
			ULevelGeneratorFunctionLibrary::UpdateLayoutCellData_Global(i, LayoutData[i]);
		}
				
		ULevelGeneratorFunctionLibrary::RemoveRepeatedCellsFromLayouts(LayoutData, TArray<FLayoutData>()); //because these are the rooms, the celldata array is empty.

		//Update cells data partially
		for (const FLayoutData Layout : LayoutData)
		{
			CellsData.Append(Layout.CellsData);
		}

		//Update grid size of total and add offset
		GridSize = ULevelGeneratorFunctionLibrary::GetGridSize(LayoutData);
		GridSize.X = GridSize.X + 2 + WallsCellSize + 5; //Add walls displacement to ensure they can be placed. Also add 5 value, because sometimes is not working properly, maybe some aproximation?
		GridSize.Y = GridSize.Y + 2 + WallsCellSize + 5;
		
		GeneratedRoomsAmount = LayoutData.Num(); // at this point there is only rooms on the layout data

		for (int i = 0; i < RoomLayoutActiveThreads.Num(); i++)
		{
			if (RoomLayoutActiveThreads[i])
			{
				delete RoomLayoutActiveThreads[i];
			}
		}

		RoomLayoutActiveThreads.Empty();

		UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::HandleRoomLayoutThreads - Completed rooms layout thread operation."));
		StartCorridorsLayoutDataGeneration();
	}
}

TArray<FGameplayTag> ULevelManager::GenerateBiomesForAreas(int32 AreasAmountIn)
{
	TArray<FGameplayTag> UsedBiomes = TArray<FGameplayTag>();
	TArray<FGameplayTag> UnusedBiomes = PossibleBiomes;
	int32 RandomSelection = 0;

	if (PossibleBiomes.Num() <= 0)
	{
		UsedBiomes.SetNum(AreasAmountIn, true);
		return UsedBiomes;
	}

	for (int i = 0; i < AreasAmountIn; i++)
	{
		if (UnusedBiomes.Num() <= 0) UnusedBiomes = PossibleBiomes;

		RandomSelection = FMath::RandRange(0, UnusedBiomes.Num() - 1);
		UsedBiomes.Add(UnusedBiomes[RandomSelection]);
		UnusedBiomes.RemoveAt(RandomSelection);
	}

	return UsedBiomes;
}

void ULevelManager::StartCorridorsLayoutDataGeneration()
{
	UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::StartCorridorsLayoutDataGeneration - Started Corridors Layout generation."));

	TArray<FLevel_RoomDistance> InitialCorridorsLayoutData = GenerateInitialCorridorsLayoutData(LayoutData);
	UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::StartCorridorsLayoutDataGeneration - Initial Corridors Layouts: %i"), InitialCorridorsLayoutData.Num());

	TArray<FLevel_RoomDistance> CurrentLayouts = TArray<FLevel_RoomDistance>();

	//This is the amount of corridors per thread. The Idea is to not mass too many threads. The threads handle a few corridors ideally
	int Threads = FMath::Clamp(MaximunCorridorLayoutThreads, 1, MaximunCorridorLayoutThreads);
	int32 TotalPerThread = InitialCorridorsLayoutData.Num() / (Threads - 1);
	
	for (int i = 0; i < InitialCorridorsLayoutData.Num(); i++)
	{				
		CurrentLayouts.Add(InitialCorridorsLayoutData[i]);		
		
		if (CurrentLayouts.Num() >= TotalPerThread || i == InitialCorridorsLayoutData.Num() - 1)   
		{
			FCorridorLayoutWorker* CorridorWorker = new FCorridorLayoutWorker(GenerateThreadID(), CurrentLayouts, CellSize, InitialLocation, GridSize, CorridorSelectionType, CorridorSelectionThreshold, LayoutData, CellsData);
			CorridorLayoutActiveThreads.Add(CorridorWorker);
			CurrentLayouts.Empty();
		}		
	}
}

bool ULevelManager::AreAllCorridorLayoutThreadsCompleted() const
{
	bool AllAreCompleted = true;
	for (int i = 0; i < CorridorLayoutActiveThreads.Num(); i++)
	{
		if (CorridorLayoutActiveThreads[i])
		{
			if (!CorridorLayoutActiveThreads[i]->bIsThreadCompleted)
			{
				AllAreCompleted = false;
				break;
			}
		}
	}

	return AllAreCompleted;
}

void ULevelManager::HandleCorridorLayoutThreads()
{
	if (CorridorLayoutActiveThreads.Num() <= 0)
	{
		return;
	}

	if (!AreAllCorridorLayoutThreadsCompleted())
	{
		return;
	}

	CorridorsLayoutData.Empty();

	for (int i = 0; i < CorridorLayoutActiveThreads.Num(); i++)
	{
		if (CorridorLayoutActiveThreads[i]->bLayoutGenerated)
		{
			CorridorsLayoutData.Append(CorridorLayoutActiveThreads[i]->GeneratedCorridorLayout);
		}
	}

	ULevelGeneratorFunctionLibrary::RemoveRepeatedCellsFromLayouts(CorridorsLayoutData, LayoutData);

	for (int i = 0; i < CorridorLayoutActiveThreads.Num(); i++)
	{
		if (CorridorLayoutActiveThreads[i])
		{
			delete CorridorLayoutActiveThreads[i];
		}
	}

	CorridorLayoutActiveThreads.Empty();

	UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::HandleCorridorLayoutThreads - Completed corridors layout thread operation."));
	OnCorridorsLayoutDataGenerated();
}

void ULevelManager::OnCorridorsLayoutDataGenerated()
{
	UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::OnCorridorsLayoutDataGenerated - Started Corridors population."));
	
	//Partial update of cells data corridors
	for (const FLayoutData& Layout : CorridorsLayoutData)
	{
		CellsData.Append(Layout.CellsData);
	}

	//Generate walls layout before populating
	WallsLayoutData = FLayoutData();

	WallsLayoutData.GridID = 0;
	WallsLayoutData.Position = 0;
	WallsLayoutData.Size = GridSize;
	WallsLayoutData.Type = ELevelGen_LevelType::Blocking;
	WallsLayoutData.CellsData = GetWallsCellsData();
	UpdateBiomesForWallCellsData(WallsLayoutData.CellsData);

	//Partial update of walls cells data	
	CellsData.Append(WallsLayoutData.CellsData); //Append walls cell

	//Mutate transition cells
	TArray<FCellData> MutatedCells = MutateTransitionCells(CellsData);

	//Need to update layouts so the cells data has the mutated cells in the layouts.	
	UpdateMutatedCellsInLayouts(MutatedCells, LayoutData);	//Update layout room data

	UpdateMutatedCellsInLayouts(MutatedCells, CorridorsLayoutData);	//Update layout corridors data 
	LayoutData.Append(CorridorsLayoutData);

	TArray <FLayoutData> WallLayout = TArray <FLayoutData>(); //Update layout walls data 
	WallLayout.Add(WallsLayoutData);
	UpdateMutatedCellsInLayouts(MutatedCells, WallLayout);
	WallsLayoutData = WallLayout[0];

	//Update layout data with the walls layout
	LayoutData.Add(WallLayout[0]);

	StartRoomsPopulation();
}

TArray <FCellData> ULevelManager::GetWallsCellsData() const
{
	TArray <FCellData> LocalCells = TArray <FCellData>();
	FCellData CurrentCellData = FCellData();
	CurrentCellData.IsEmpty = true;

	FIntPoint CellSizeLocal = GridSize;
	int32 CurrentMinDistance = 0;

	for (int i = 0; i < CellSizeLocal.X; i++)
	{
		for (int j = 0; j < CellSizeLocal.Y; j++)
		{
			CurrentMinDistance = GetMinimunDistanceToCells({ i,j }, WallsCellSize + 1); //Check one more than the max distance, past that is not needed

			//If the cell is empty or is smaller than the distance allowed needs to be used.
			if (CurrentMinDistance > 0 && CurrentMinDistance <= WallsCellSize)
			{
				//Only need ID here. The other data is populated in the thread
				CurrentCellData.Cell_ID = { i,j };
				CurrentCellData.Type = ELevelGen_LevelType::Blocking;

				LocalCells.Add(CurrentCellData);
			}
		}
	}

	return LocalCells;
}

void ULevelManager::UpdateBiomesForWallCellsData(TArray<FCellData>& CellsDataOut)
{
	FGameplayTagContainer AllValidBiomes = FGameplayTagContainer();
	AllValidBiomes.Reset();
	for (const FGameplayTag& Tag : PossibleBiomes)
	{
		AllValidBiomes.AddTag(Tag);
	}

	TArray<ELevelGen_LevelType> TypesToCheck = TArray<ELevelGen_LevelType>();
	TypesToCheck.Empty();
	TypesToCheck.Add(ELevelGen_LevelType::Normal);
	TypesToCheck.Add(ELevelGen_LevelType::Corridor);
	UpdateBiomesForWallCell(AllValidBiomes, TypesToCheck, true, CellsDataOut);

	TypesToCheck.Add(ELevelGen_LevelType::Blocking);
	int32 whilecounter = 0;	
	while (HasAnyCellWithNoBiome(CellsDataOut) && whilecounter < 200)  //there are cells to cover?
	{
		UpdateBiomesForWallCell(AllValidBiomes, TypesToCheck, false, CellsDataOut);
		whilecounter++;
	}

	if (whilecounter >= 200)
	{
		UE_LOG(LevelGeneratorLog, Error, TEXT("ULevelManager::UpdateBiomesForWallCellsData - Update took too long. Cancelled operation."));
	}
}

void ULevelManager::UpdateBiomesForWallCell(const FGameplayTagContainer& BiomesIn, const TArray<ELevelGen_LevelType>& TypesIn, bool UseCellsDataGlobal, TArray<FCellData>& WallsCellsDataOut)
{
	TArray<FCellData> AdyacentCellsData = TArray<FCellData>();
	FCellData MatchingCell = FCellData();
	bool HasCellsOfBiome = false;

	for (FCellData& Cell : WallsCellsDataOut)
	{
		if (!Cell.Cell_Biome.IsValid())
		{
			if (UseCellsDataGlobal)
			{
				AdyacentCellsData = ULevelGeneratorFunctionLibrary::GetAdyacentCellsDataOfCell(Cell.Cell_ID, CellsData);						
			}
			else //Instead of global look only walls. Is faster because there are lesser wall cells and also does not need to check into corridors and rooms anymore
			{
				AdyacentCellsData = ULevelGeneratorFunctionLibrary::GetAdyacentCellsDataOfCell(Cell.Cell_ID, WallsCellsDataOut);							
			}

			if (HasCellsWithBiomesAndType(BiomesIn, TypesIn, AdyacentCellsData, MatchingCell))
			{
				Cell.Cell_Biome = MatchingCell.Cell_Biome;
			}
		}
	}
}

bool ULevelManager::HasCellsWithBiomesAndType(const FGameplayTagContainer& BiomesIn, const TArray<ELevelGen_LevelType>& TypesIn, const TArray<FCellData>& CellsDataIn, FCellData& MatchingCellOut) const
{
	bool HasAtLeastOne = false;
	for (const FCellData& Cell: CellsDataIn)
	{
		if (Cell.Cell_Biome.IsValid())
		{
			if (TypesIn.Contains(Cell.Type) && BiomesIn.HasTag(Cell.Cell_Biome))
			{
				HasAtLeastOne = true;
				MatchingCellOut = Cell;
				break;
			}
		}
	}
		
	return HasAtLeastOne;
}

bool ULevelManager::HasAnyCellWithNoBiome(const TArray<FCellData>& CellsDataIn)
{
	bool HasCell = false;
	for (const FCellData& Cell: CellsDataIn)
	{
		if (!Cell.Cell_Biome.IsValid())
		{
			HasCell = true;
			break;
		}		
	}	
	
	return HasCell;
}

TArray<FCellData> ULevelManager::MutateTransitionCells(TArray<FCellData>& CellsDataOut) const
{
	if (BiomeTransitionTag == FGameplayTag())
	{
		return TArray<FCellData>();
	}

	bool MutatedDataInLoop = true;
	int32 WhileCounter = 0;

	TArray<FCellData> MutatedCells = TArray<FCellData>();
	
	while (MutatedDataInLoop)
	{		
		MutatedDataInLoop = false;

		//Do it layered per biome to simplify the transitions created
		for (const FGameplayTag Biome : PossibleBiomes)
		{
			for (FCellData& MutableCell : CellsDataOut)
			{		
				//First loop exclude rooms, I want to avoid rooms if possible. Next loops can modify them as it might be needed.
				if (WhileCounter == 0 && MutableCell.Type == ELevelGen_LevelType::Normal) continue;

				//Is not transition and matches the current analized biome
				if (MutableCell.Cell_Biome == Biome) //having a biome makes non transition, no need to check.
				{
					if (CanMutateCell(MutableCell, CellsDataOut))
					{
						//Mutate cell biome
						MutableCell.Cell_Biome = BiomeTransitionTag;
						MutatedCells.Add(MutableCell);
						MutatedDataInLoop = true;					
					}
				}							
			}
		}

		WhileCounter++;
	}	

	UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::MutateTransitionCells - Mutation took %i loops. Mutated cells: %i"), WhileCounter, MutatedCells.Num());

	return MutatedCells;
}

bool ULevelManager::CanMutateCell(const FCellData& CellIn, const TArray<FCellData>& CellsDataIn) const
{
	if (CellIn.Cell_Biome == BiomeTransitionTag)
	{
		return false; //Already mutated 
	}

	TArray<FCellData> AdyacentCellsData = ULevelGeneratorFunctionLibrary::GetAdyacentCellsDataOfCell(CellIn.Cell_ID, CellsDataIn);	
	bool IsTrans = false;
	for (const FCellData& Adyacell : AdyacentCellsData)
	{
		//The ady cell is not transition
		if (!Adyacell.Cell_Biome.MatchesTagExact(BiomeTransitionTag))
		{
			//Different biomes to the adyacent?
			if (!CellIn.Cell_Biome.MatchesTagExact(Adyacell.Cell_Biome))
			{
				IsTrans = true;
				break;
			}
		}
	}

	return IsTrans;
}

void ULevelManager::UpdateMutatedCellsInLayouts(const TArray<FCellData>& MutatedCellsIn, TArray<FLayoutData>& LayoutsOut) const
{
	int32 CellIndex = 0;
	for (const FCellData& MutatedCell : MutatedCellsIn)
	{	
		for (FLayoutData& Layout : LayoutsOut)
		{
			CellIndex = ULevelGeneratorFunctionLibrary::FindCellIDIndex(MutatedCell.Cell_ID, Layout.CellsData);
			if (Layout.CellsData.IsValidIndex(CellIndex))
			{
				Layout.CellsData[CellIndex] = MutatedCell;
			}				
		}
	}
}

void ULevelManager::StartRoomsPopulation()
{
	for (int i = 0; i < LayoutData.Num(); i++)
	{
		FPopulateRoomWorker* PopulateWorker = new FPopulateRoomWorker(GenerateThreadID(), ELevelGen_LevelType::Normal, CellSize, InitialLocation, BiomeTransitionTag, CellPopulateConditions, LayoutData[i], LevelsData);
		PopulateRoomActiveThreads.Add(PopulateWorker);
	}
}

void ULevelManager::StartCorridorsPopulation()
{
	for (int i = 0; i < CorridorsLayoutData.Num(); i++)
	{
		FPopulateRoomWorker* PopulateWorker = new FPopulateRoomWorker(GenerateThreadID(), ELevelGen_LevelType::Corridor, CellSize, InitialLocation, BiomeTransitionTag, CellPopulateConditions, CorridorsLayoutData[i], LevelsData);
		PopulateRoomActiveThreads.Add(PopulateWorker);
	}
}

void ULevelManager::StartWallsPopulation()
{
	UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::StartWallsPopulation - Started Walls generation."));
	FPopulateRoomWorker* PopulateWorker = new FPopulateRoomWorker(GenerateThreadID(), ELevelGen_LevelType::Blocking, CellSize, InitialLocation, BiomeTransitionTag, CellPopulateConditions, WallsLayoutData, LevelsData);
	PopulateRoomActiveThreads.Add(PopulateWorker);
}

bool ULevelManager::AreAllPopulateRoomThreadsCompleted() const
{
	bool AllAreCompleted = true;
	for (int i = 0; i < PopulateRoomActiveThreads.Num(); i++)
	{
		if (PopulateRoomActiveThreads[i])
		{
			if (!PopulateRoomActiveThreads[i]->bIsThreadCompleted)
			{
				AllAreCompleted = false;
				break;
			}
		}
	}
	return AllAreCompleted;
}

void ULevelManager::HandlePopulateRoomThreads()
{
	if (PopulateRoomActiveThreads.Num() <= 0)
	{
		return;
	}

	if (AreAllPopulateRoomThreadsCompleted())
	{
		for (int i = 0; i < PopulateRoomActiveThreads.Num(); i++)
		{
			if (PopulateRoomActiveThreads[i])
			{
				ProcessPopulateRoomThreadsOutput(PopulateRoomActiveThreads[i]);
			}
		}

		ELevelGen_LevelType OperationType = ELevelGen_LevelType::Normal;

		for (int i = 0; i < PopulateRoomActiveThreads.Num(); i++)
		{
			if (PopulateRoomActiveThreads[i])
			{
				//Find the first thread operation type. They all should be the same.
				OperationType = PopulateRoomActiveThreads[i]->MapType;
				break;
			}
		}

		for (int i = 0; i < PopulateRoomActiveThreads.Num(); i++)
		{
			if (PopulateRoomActiveThreads[i])
			{
				delete PopulateRoomActiveThreads[i];
			}
		}
		PopulateRoomActiveThreads.Empty();

		switch (OperationType)
		{
		case ELevelGen_LevelType::Normal:
		{			
			UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::HandlePopulateRoomThreads - Completed populating rooms thread operation."));
			if (LayoutData.Num() > 1)//At this point, the layout data is only rooms. 
			{
				StartCorridorsPopulation();
			}
			else
			{
				StartWallsPopulation();
			}
			break;
		}
		case ELevelGen_LevelType::Corridor:
		{
			UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::HandlePopulateRoomThreads - Completed populating corridors thread operation."));
			StartWallsPopulation();
			break;
		}
		case ELevelGen_LevelType::Blocking:
		{
			UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::HandlePopulateRoomThreads - Completed populating walls thread operation."));
			OnLevelGenerated();
			break;
		}
		default:
			break;
		}
	}
}

void ULevelManager::ProcessPopulateRoomThreadsOutput(FPopulateRoomWorker* PopulateRoomThreadIn)
{
	int LevelPlacementDataPreviousNum = LevelPlacementData.Num();
	LevelPlacementData.Append(PopulateRoomThreadIn->LevelPlacementDataGenerated);
	TArray <FCellData> CellsDataGenerated = PopulateRoomThreadIn->CellsDataGenerated;

	for (int i = 0; i < CellsDataGenerated.Num(); i++)
	{
		CellsDataGenerated[i].LevelPlacementDataIndex = CellsDataGenerated[i].LevelPlacementDataIndex + LevelPlacementDataPreviousNum; //offset with the placement data that already exist
	}

	//Update cells data array.
	int32 CellIndex = 0;
	for (const FCellData Cell : CellsDataGenerated)
	{
		CellIndex = ULevelGeneratorFunctionLibrary::FindCellIDIndex(Cell.Cell_ID, CellsData);

		if (CellsData.IsValidIndex(CellIndex))
		{
			CellsData[CellIndex] = Cell;
		}
		else
		{
			CellsData.Add(Cell);
		}
	}

	int GridIDIndex = LayoutData.Find(PopulateRoomThreadIn->RoomLayoutData);
	if (LayoutData.IsValidIndex(GridIDIndex))
	{
		LayoutData[GridIDIndex].CellsData = CellsDataGenerated;
	}
	else
	{
		UE_LOG(LevelGeneratorLog, Error, TEXT("ULevelManager::ProcessPopulateRoomThreadsOutput - Thread cannot update layout data. Cannot find LayoutData."));
	}
}

void ULevelManager::OnLevelGenerated()
{
	StartStreamLoadLevels();	
}

void ULevelManager::StartStreamLoadLevels()
{	
	UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::StartStreamLoadLevels - Start stream load levels."));
	
	if (UseLevelsPool)
	{
		InitializeLevelsPool();
	}
	else
	{
		IsGeneratingLevelsPool = false;
		CreateAllStartAreaLevelInstances();
	}
}

void ULevelManager::InitializeLevelsPool()
{
	IsGeneratingLevelsPool = true;
	LoadedLevelsAmount = 0;
	StartLevelsAmount = GetLevelsPoolNumber();
	GenerateLevelsPool();
}

void ULevelManager::CreateAllStartAreaLevelInstances()
{	
	UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::CreateAllStartAreaLevelInstances - Create start area levels."));

	LoadedLevelsAmount = 0;

	TArray <int32> LevelsPlacementDataIndex = TArray <int32>();
	if (CreateLevelsAtRuntime)
	{
		TArray<FIntPoint> Cells = TArray<FIntPoint>();
		LevelsPlacementDataIndex = GetStartAreaLevelsPlacementData(Cells);
		StartLevelsAmount = LevelsPlacementDataIndex.Num() - 1;

		//Load start area
		for (const int32& index : LevelsPlacementDataIndex)
		{
			CreateLevelInstance(index, StartLevelsBlockOnLoad);
		}
	}
	else
	{
		StartLevelsAmount = LevelPlacementData.Num();

		//Load all levels
		for (int32 i = 0; i < LevelPlacementData.Num(); i++)
		{		
			CreateLevelInstance(i, StartLevelsBlockOnLoad);
		}		
	}	
}

TArray<int32> ULevelManager::GetStartAreaLevelsPlacementData(TArray<FIntPoint>& CellsIDsOut)
{
	CellsIDsOut.Empty();

	FIntPoint StartCell = GetStartCellID();

	int32 min_x = StartCell.X - 5;
	int32 max_x = StartCell.X + 5 + 1;
	int32 min_y = StartCell.Y - 5; 
	int32 max_y = StartCell.Y + 5 + 1;

	TArray <int32> LevelsPlacementDataIndex = TArray <int32>();
	int32 CellIndex = 0;

	for (int32 i = min_x; i < max_x; i++)
	{
		for (int32 j = min_y; j < max_y; j++)
		{
			CellIndex = ULevelGeneratorFunctionLibrary::FindCellIDIndex({ i,j }, CellsData);

			if (CellsData.IsValidIndex(CellIndex))
			{
				CellsIDsOut.Add({ i,j });
				LevelsPlacementDataIndex.AddUnique(CellsData[CellIndex].LevelPlacementDataIndex);
			}
		}
	}

	return LevelsPlacementDataIndex;
}

void ULevelManager::FinishStreamLoadStartLevels()
{
	UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::FinishStreamLoadStartLevels - Finish stream loading start levels."));

	FBiomeManagerInitializationData InitData = FBiomeManagerInitializationData();
	InitData.CellSize = CellSize;
	InitData.InitialLocation = InitialLocation;
	InitData.StartCellID = GetStartCellID();
	InitData.CellsData = CellsData;
	InitData.BiomeTransitionTag = BiomeTransitionTag;
	TransitionCellsData = GetTransitionCellsData();
	InitData.LoadedLevelsClientData = GetLoadedLevelsClientData(); // Get loaded data AFTER transition cells data update, if not wont work
	InitData.TransitionCellsData = TransitionCellsData;
	OnTransitionCellsDataGenerated.Broadcast(InitData);	

	GenerateSpawnActorsTrack();
	OnLevelGenerationEnd();
}

TArray<FLevel_TransitionCellData> ULevelManager::GetTransitionCellsData() const
{
	TArray <FCellData> TransitionCells = TArray <FCellData>();

	for (const FCellData Cell : CellsData)
	{
		if (Cell.Cell_Biome == BiomeTransitionTag)
		{
			TransitionCells.Add(Cell);
		}
	}

	TArray <FLevel_TransitionCellData> TransitionData = TArray <FLevel_TransitionCellData>();
	FLevel_TransitionCellData LocalData = FLevel_TransitionCellData();

	//Propagate biomes from normal biome cells to transitions that are adyacent
	for (const FCellData TransCell : TransitionCells)
	{
		LocalData.CellData = TransCell;

		LocalData.LevelWorldLocation = GetPlacedLevelPosition(TransCell.LevelPlacementDataIndex);
		LocalData.LevelWorldRotation = GetPlacedLevelRotation(TransCell.LevelPlacementDataIndex);

		UpdateTransitionCellSurroundingBiomes(TransCell, LocalData);
		TransitionData.Add(LocalData);
	}

	//At this point some tags will still be transitions if two transition cells are next to each other.
	//Propagate biomes into internal transition cells
	int32 whileCounter = 0;
	bool whileEnable = true;

	bool PropagatedToAdyacentCell = false;

	while (whileEnable && whileCounter < 50000)
	{
		PropagatedToAdyacentCell = PropagateBiomesFromAdyacentTransitionCells(TransitionData);

		if (!PropagatedToAdyacentCell)
		{
			if (!PropagateBiomesFromAdyacentQuadsInSameCell(ELevelGen_QuadPropagationMode::Normal, TransitionData))
			{
				if (!PropagateBiomesFromAdyacentQuadsInSameCell(ELevelGen_QuadPropagationMode::Random, TransitionData))
				{
					if (!PropagateBiomesFromAdyacentQuadsInSameCell(ELevelGen_QuadPropagationMode::RandomDobleTransition, TransitionData))
					{
						PropagateBiomesFromAdyacentQuadsInSameCell(ELevelGen_QuadPropagationMode::Corner, TransitionData);
					}
				}
			}
		}

		//If there are no more invalid stop operation
		if (AreAllTransitionCellsValid(TransitionData))
		{
			whileEnable = false;
		}

		whileCounter++;
	}

#if WITH_EDITOR

	if (whileCounter >= 5000)
	{
		UE_LOG(LevelGeneratorLog, Error, TEXT("ULevelManager::GetTransitionCellsData - Transition cells propagation took:%i loops."), whileCounter);
	}

#endif

	UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::GetTransitionCellsData - Transition cells propagation took:%i loops."), whileCounter);

	return TransitionData;
}

bool ULevelManager::PropagateBiomesFromAdyacentTransitionCells(TArray <FLevel_TransitionCellData>& TransitionDataOut) const
{
	bool PropagatedQuad = false;

	TArray <ELevelGen_CorridorDirection> AllDirections = TArray <ELevelGen_CorridorDirection>();
	AllDirections.Add(ELevelGen_CorridorDirection::Up);
	AllDirections.Add(ELevelGen_CorridorDirection::Right);
	AllDirections.Add(ELevelGen_CorridorDirection::Down);
	AllDirections.Add(ELevelGen_CorridorDirection::Left);

	FGameplayTag AdyacentCellAdyacentQuad = FGameplayTag();
	bool IsValidBiome = false;

	for (FLevel_TransitionCellData& TransData : TransitionDataOut)
	{
		for (const ELevelGen_CorridorDirection& Direction : AllDirections)
		{		
			if (!IsBiomeValid(GetQuadBiomeInDirection(Direction, TransData)))
			{
				AdyacentCellAdyacentQuad = GetAdyacentBiomeOfQuadInDirection_InAdyacentCell(Direction, TransData, TransitionDataOut);
				IsValidBiome = IsBiomeValid(AdyacentCellAdyacentQuad);
				if (IsValidBiome)
				{
					SetQuadBiomeInDirection(Direction, AdyacentCellAdyacentQuad, TransData);
					PropagatedQuad = true;
				}
			}
		}
	}

	return PropagatedQuad;
}

bool ULevelManager::PropagateBiomesFromAdyacentQuadsInSameCell(ELevelGen_QuadPropagationMode ModeIn, TArray<FLevel_TransitionCellData>& TransitionDataOut) const
{	
	bool PropagatedQuad = false;

	TArray <ELevelGen_CorridorDirection> AllDirections = TArray <ELevelGen_CorridorDirection>();
	AllDirections.Add(ELevelGen_CorridorDirection::Up);
	AllDirections.Add(ELevelGen_CorridorDirection::Right);
	AllDirections.Add(ELevelGen_CorridorDirection::Down);
	AllDirections.Add(ELevelGen_CorridorDirection::Left);

	TArray<FGameplayTag> QuadAdyacentTags = TArray<FGameplayTag>();
	FGameplayTag AdyacentCellAdyacentQuad = FGameplayTag();

	bool IsValidBiome = false;
	int32 ValidBiomeCount = 0;

	for (FLevel_TransitionCellData& TransData : TransitionDataOut)
	{
		for (const ELevelGen_CorridorDirection& Direction : AllDirections)
		{			
			if (!IsBiomeValid(GetQuadBiomeInDirection(Direction, TransData)))
			{
				QuadAdyacentTags = GetAdyacentBiomesOfQuadInDirection_InOwnCell(Direction, TransData, TransitionDataOut);
				ValidBiomeCount = GetValidBiomeAmount(QuadAdyacentTags);
				
				switch (ModeIn)
				{
				case ELevelGen_QuadPropagationMode::Normal:
				{
					if (ValidBiomeCount == 2)
					{
						//If there is 2 equals, pick that biome. The 3rd one could be null or transition and that is fine.
						if (QuadAdyacentTags[0] == QuadAdyacentTags[1])
						{
							SetQuadBiomeInDirection(Direction, QuadAdyacentTags[0], TransData); //Succesful										
							PropagatedQuad = true;
							break;
						}
						else
						{
							AdyacentCellAdyacentQuad = GetAdyacentBiomeOfQuadInDirection_InAdyacentCell(Direction, TransData, TransitionDataOut);

							//Different and there is a null? Can randomize
							if (AdyacentCellAdyacentQuad == FGameplayTag())
							{
								SetQuadBiomeInDirection(Direction, GetRandomValidBiomeInArray(QuadAdyacentTags), TransData); //Succesful. I dont care to break loop in this position. This cell does not affect others.							
							}
						}
					}
					break;
				}					
				case ELevelGen_QuadPropagationMode::Random:
				{
					if (ValidBiomeCount == 2)
					{
						if (QuadAdyacentTags[0] != QuadAdyacentTags[1])
						{
							SetQuadBiomeInDirection(Direction, GetRandomValidBiomeInArray(QuadAdyacentTags), TransData); //Succesful										
							PropagatedQuad = true;
							break;
						}
					}
					break;
				}				
				case ELevelGen_QuadPropagationMode::RandomDobleTransition:
				{
					if (ValidBiomeCount == 1)
					{
						//1 valid, 1 transition and the adyacent cell is transition. This is when many transition cells are adyacent
						AdyacentCellAdyacentQuad = GetAdyacentBiomeOfQuadInDirection_InAdyacentCell(Direction, TransData, TransitionDataOut);

						if (AdyacentCellAdyacentQuad == BiomeTransitionTag && QuadAdyacentTags.Contains(BiomeTransitionTag))
						{
							SetQuadBiomeInDirection(Direction, GetRandomValidBiomeInArray(QuadAdyacentTags), TransData); //Succesful										
							PropagatedQuad = true;
							break;
						}
					}
					break;
				}		
				case ELevelGen_QuadPropagationMode::Corner:
				{
					if (ValidBiomeCount == 1)
					{
						//1 valid, 1 transition and the adyacent cell is null. This is a corner and copies the valid tag
						AdyacentCellAdyacentQuad = GetAdyacentBiomeOfQuadInDirection_InAdyacentCell(Direction, TransData, TransitionDataOut);

						if (AdyacentCellAdyacentQuad == FGameplayTag() && QuadAdyacentTags.Contains(BiomeTransitionTag))
						{
							SetQuadBiomeInDirection(Direction, GetRandomValidBiomeInArray(QuadAdyacentTags), TransData); //Succesful. I dont care to break loop in this position. This cell does not affect others.	
						}
					}
					break;
				}
				default:
					break;
				}				
			}
		}
		
		if(PropagatedQuad) break;
	}

	return PropagatedQuad;
}

void ULevelManager::UpdateTransitionCellSurroundingBiomes(const FCellData& CellIn, FLevel_TransitionCellData& TransitionDataOut) const
{
	SetQuadBiomeInDirection(ELevelGen_CorridorDirection::Up, GetAdyacentCellBiomeInDirection(ELevelGen_CorridorDirection::Up, CellIn), TransitionDataOut);
	SetQuadBiomeInDirection(ELevelGen_CorridorDirection::Right, GetAdyacentCellBiomeInDirection(ELevelGen_CorridorDirection::Right, CellIn), TransitionDataOut);
	SetQuadBiomeInDirection(ELevelGen_CorridorDirection::Down, GetAdyacentCellBiomeInDirection(ELevelGen_CorridorDirection::Down, CellIn), TransitionDataOut);
	SetQuadBiomeInDirection(ELevelGen_CorridorDirection::Left, GetAdyacentCellBiomeInDirection(ELevelGen_CorridorDirection::Left, CellIn), TransitionDataOut);		
}
 
FGameplayTag ULevelManager::GetAdyacentCellBiomeInDirection(ELevelGen_CorridorDirection DirectionIn, const FCellData& CellIn) const
{
	FIntPoint AdyCellID = ULevelGeneratorFunctionLibrary::GetAdyacentCellInDirectionOfCell(DirectionIn, CellIn.Cell_ID);
	
	int32 CellIndex = 0;
	CellIndex = ULevelGeneratorFunctionLibrary::FindCellIDIndex(AdyCellID, CellsData);
	
	FGameplayTag AdyBiome = FGameplayTag(); //If the cell does not exist, put a null biome.

	if (CellsData.IsValidIndex(CellIndex))
	{		
		AdyBiome = CellsData[CellIndex].Cell_Biome;
	}

	return AdyBiome;
}

TArray<FGameplayTag> ULevelManager::GetAdyacentBiomesOfQuadInDirection_InOwnCell(ELevelGen_CorridorDirection DirectionIn, const FLevel_TransitionCellData& CellTransDataIn, const TArray<FLevel_TransitionCellData>& TransitionDataIn) const
{
	TArray<FGameplayTag> QuadTags = TArray<FGameplayTag>();

	//Add neighbour quads biomes tags
	switch (DirectionIn)
	{
	case ELevelGen_CorridorDirection::Up:
	{
		QuadTags.Add(GetQuadBiomeInDirection(ELevelGen_CorridorDirection::Left, CellTransDataIn));
		QuadTags.Add(GetQuadBiomeInDirection(ELevelGen_CorridorDirection::Right, CellTransDataIn));
		break;
	}
	case ELevelGen_CorridorDirection::Down:
	{
		QuadTags.Add(GetQuadBiomeInDirection(ELevelGen_CorridorDirection::Left, CellTransDataIn));
		QuadTags.Add(GetQuadBiomeInDirection(ELevelGen_CorridorDirection::Right, CellTransDataIn));
		break;
	}
	case ELevelGen_CorridorDirection::Left:
	{
		QuadTags.Add(GetQuadBiomeInDirection(ELevelGen_CorridorDirection::Up, CellTransDataIn));
		QuadTags.Add(GetQuadBiomeInDirection(ELevelGen_CorridorDirection::Down, CellTransDataIn));
		break;
	}
	case ELevelGen_CorridorDirection::Right:
	{
		QuadTags.Add(GetQuadBiomeInDirection(ELevelGen_CorridorDirection::Up, CellTransDataIn));
		QuadTags.Add(GetQuadBiomeInDirection(ELevelGen_CorridorDirection::Down, CellTransDataIn));
		break;
	}
	default:
		break;
	}

	return QuadTags;
}

FGameplayTag ULevelManager::GetAdyacentBiomeOfQuadInDirection_InAdyacentCell(ELevelGen_CorridorDirection DirectionIn, const FLevel_TransitionCellData& CellTransDataIn, const TArray<FLevel_TransitionCellData>& TransitionDataIn) const
{
	FGameplayTag QuadTags = FGameplayTag();
	ELevelGen_CorridorDirection CellOppositeQuadDirection = ELevelGen_CorridorDirection();

	//Get tag of opposite direction for cell check  
	switch (DirectionIn)
	{
	case ELevelGen_CorridorDirection::Up:
	{
		CellOppositeQuadDirection = ELevelGen_CorridorDirection::Down;
		break;
	}
	case ELevelGen_CorridorDirection::Down:
	{
		CellOppositeQuadDirection = ELevelGen_CorridorDirection::Up;
		break;
	}
	case ELevelGen_CorridorDirection::Left:
	{
		CellOppositeQuadDirection = ELevelGen_CorridorDirection::Right;
		break;
	}
	case ELevelGen_CorridorDirection::Right:
	{
		CellOppositeQuadDirection = ELevelGen_CorridorDirection::Left;
		break;
	}
	default:
		break;
	}

	//Find adyacent cell and the quad on the opposite
	FIntPoint AdyCellID = ULevelGeneratorFunctionLibrary::GetAdyacentCellInDirectionOfCell(DirectionIn, CellTransDataIn.CellData.Cell_ID);
	bool FoundTag = false;
	for (const FLevel_TransitionCellData& TransData : TransitionDataIn) //The cell must be a transition cell, since if it would be a normal cell, it would have already propagated.
	{
		if (TransData.CellData.Cell_ID == AdyCellID)
		{
			//Add the tag of the quad in the neightbour cell
			QuadTags = GetQuadBiomeInDirection(CellOppositeQuadDirection, TransData);
			FoundTag = true;
			break;
		}
	}

	//If the adyacent is not found, it means that the cell does not exist. Add null. 
	//The cell cannot exist in cells data, since it would already be propagated. This must be a transition cell.
	if (!FoundTag)
	{
		QuadTags = FGameplayTag();
	}

	return QuadTags;
}

FGameplayTag ULevelManager::GetQuadBiomeInDirection(ELevelGen_CorridorDirection DirectionIn, const FLevel_TransitionCellData& CellTransDataIn) const
{
	ELevelGen_CorridorDirection NewDirection = AdjustDirectionWithCellRotation(DirectionIn, CellTransDataIn);

	FGameplayTag QuadBiome = FGameplayTag();

	switch (NewDirection)
	{
	case ELevelGen_CorridorDirection::Up: 
	{
		QuadBiome = CellTransDataIn.Biome_Up;
		break;
	}
	case ELevelGen_CorridorDirection::Down:
	{
		QuadBiome = CellTransDataIn.Biome_Down;
		break;
	}
	case ELevelGen_CorridorDirection::Left: 
	{
		QuadBiome = CellTransDataIn.Biome_Left;
		break;
	}
	case ELevelGen_CorridorDirection::Right:
	{
		QuadBiome = CellTransDataIn.Biome_Right;
		break;
	}
	default:
		break;
	}

	//If is null, set to transition. Null is reserved for when the cell itself does not exist.
	if (QuadBiome == FGameplayTag())
	{
		QuadBiome = BiomeTransitionTag;
	}

	return QuadBiome;
}

void ULevelManager::SetQuadBiomeInDirection(ELevelGen_CorridorDirection DirectionIn, const FGameplayTag& NewValueIn, FLevel_TransitionCellData& CellTransDataOut) const
{
	ELevelGen_CorridorDirection NewDirection = AdjustDirectionWithCellRotation(DirectionIn, CellTransDataOut);

	switch (NewDirection)
	{
	case ELevelGen_CorridorDirection::Up: 
	{
		CellTransDataOut.Biome_Up = NewValueIn;
		break;
	}
	case ELevelGen_CorridorDirection::Down: 
	{
		CellTransDataOut.Biome_Down = NewValueIn;
		break;
	}	
	case ELevelGen_CorridorDirection::Left:
	{
		CellTransDataOut.Biome_Left = NewValueIn;
		break;
	}
	case ELevelGen_CorridorDirection::Right: 
	{
		CellTransDataOut.Biome_Right = NewValueIn;
		break;
	}
	default:
		break;
	}
}

ELevelGen_CorridorDirection ULevelManager::AdjustDirectionWithCellRotation(ELevelGen_CorridorDirection DirectionIn, const FLevel_TransitionCellData& CellTransDataIn) const
{
	int DirectionIndex = 0;
	if(DirectionIn == ELevelGen_CorridorDirection::Up) DirectionIndex = 0;
	else if (DirectionIn == ELevelGen_CorridorDirection::Right) DirectionIndex = 1;
	else if (DirectionIn == ELevelGen_CorridorDirection::Down) DirectionIndex = 2;
	else if (DirectionIn == ELevelGen_CorridorDirection::Left) DirectionIndex = 3;

	DirectionIndex = 4 + DirectionIndex - LevelPlacementData[CellTransDataIn.CellData.LevelPlacementDataIndex].RotationIndex; //Add 4 to avoid negative values when subtracting. Then is mapped anyways
	DirectionIndex = DirectionIndex % 4; //map to 0 -3

	TArray <ELevelGen_CorridorDirection> AllDirections = TArray <ELevelGen_CorridorDirection>();
	AllDirections.Add(ELevelGen_CorridorDirection::Up); //0
	AllDirections.Add(ELevelGen_CorridorDirection::Right);//1
	AllDirections.Add(ELevelGen_CorridorDirection::Down);//2
	AllDirections.Add(ELevelGen_CorridorDirection::Left);//3

	ELevelGen_CorridorDirection NewDirection = AllDirections[DirectionIndex];

	return NewDirection;
}

bool ULevelManager::AreAllTransitionCellsValid(const TArray<FLevel_TransitionCellData>& TransitionDataIn) const
{
	bool result = true;
	for (const FLevel_TransitionCellData& TransData : TransitionDataIn)
	{
		//any transition or null tag is invalid
		if (TransData.Biome_Down == BiomeTransitionTag || TransData.Biome_Up == BiomeTransitionTag || TransData.Biome_Left == BiomeTransitionTag || TransData.Biome_Right == BiomeTransitionTag ||
			TransData.Biome_Down == FGameplayTag() || TransData.Biome_Up == FGameplayTag() || TransData.Biome_Left == FGameplayTag() || TransData.Biome_Right == FGameplayTag())
		{
			result = false;
			break;
		}	
	}

	return result;
}

FGameplayTag ULevelManager::GetRandomValidBiomeInArray(const TArray<FGameplayTag>& BiomesIn) const
{
	TArray<FGameplayTag> ValidTags = TArray<FGameplayTag>();
	for (const FGameplayTag Biome : BiomesIn)
	{
		if (IsBiomeValid(Biome))
		{
			ValidTags.Add(Biome);
		}
	}
	
	int32 RandomIndex = FMath::RandRange(0, ValidTags.Num() - 1);
	if (ValidTags.IsValidIndex(RandomIndex))
	{
		return ValidTags[RandomIndex];
	}
	
	return FGameplayTag();
}

int32 ULevelManager::GetValidBiomeAmount(const TArray<FGameplayTag>& BiomesIn) const
{
	int32 Count = 0;
	for (const FGameplayTag Biome : BiomesIn)
	{
		if (IsBiomeValid(Biome))
		{
			Count++;
		}
	}
	
	return Count;
}

int32 ULevelManager::GetBiomeAmount(const FGameplayTag& BiomeIn, const TArray<FGameplayTag>& BiomesIn) const
{
	int32 Count = 0;
	for (const FGameplayTag Biome : BiomesIn)
	{
		if (BiomeIn == Biome)
		{
			Count++;
		}
	}

	return Count;
}

bool ULevelManager::IsBiomeValid(const FGameplayTag BiomeIn) const
{	
	return 	PossibleBiomes.Contains(BiomeIn) && BiomeIn != FGameplayTag() && BiomeIn != BiomeTransitionTag;
}

bool ULevelManager::FindTransitionCellData(FIntPoint CellIDIn, FLevel_TransitionCellData& TransitionDataOut) const
{
	bool founddata = false;
	for (const FLevel_TransitionCellData& TransData : TransitionCellsData)
	{
		if (TransData.CellData.Cell_ID == CellIDIn)
		{
			TransitionDataOut = TransData;
			founddata = true;
			break;
		}
	}

	return founddata;
}

void ULevelManager::OnLevelGenerationEnd()
{
	SetStartCellVisibilityStatus(true);
	bIsGeneratingLevel = false;
	OnLevelGenerationCompleted.Broadcast();
	UE_LOG(LevelGeneratorLog, Log, TEXT("ULevelManager::OnLevelGenerationEnd - Level generated succesfully."));
}

void ULevelManager::SetCellVisibilityStatus(FIntPoint CellID, int32 VisibilityTickDuration)
{
	FLevel_CellVisibilityStatus Status = FLevel_CellVisibilityStatus();
	Status.Cell_ID = CellID;
	Status.VisibilityTickDuration = VisibilityTickDuration;
	
	int StatusPosition = ForcedVisibleCells.Find(Status);
	
	if (ForcedVisibleCells.IsValidIndex(StatusPosition))
	{
		ForcedVisibleCells[StatusPosition] = Status;
	}
	else
	{
		ForcedVisibleCells.Add(Status);
	}
}

void ULevelManager::ProcessAllCellVisibilityStatus()
{
	TArray <FLevel_CellVisibilityStatus> LocalVisib = TArray <FLevel_CellVisibilityStatus>();
	FLevel_CellVisibilityStatus CurrentCell = FLevel_CellVisibilityStatus();

	for (int i = 0; i < ForcedVisibleCells.Num(); i++)
	{
		if (ForcedVisibleCells[i].VisibilityTickDuration < 0)
		{
			//Negatives are kept forever untill manual override
			LocalVisib.Add(ForcedVisibleCells[i]);
		}
		else if (ForcedVisibleCells[i].VisibilityTickDuration > 0)
		{
			//Decrement those that are higher than 0
			CurrentCell = ForcedVisibleCells[i];
			CurrentCell.VisibilityTickDuration = CurrentCell.VisibilityTickDuration - 1;			
			LocalVisib.Add(CurrentCell);
		}
		//Those that are 0 will not be added to the array.
	}

	ForcedVisibleCells = LocalVisib;
}

void ULevelManager::OnCellVisibilityChanged(const TArray<int>& CellDataIndex, bool IsVisible)
{	
	FLevel_TransitionCellData TransCellData = FLevel_TransitionCellData();
		
	TArray <FIntPoint> CellsToBuild = TArray <FIntPoint>();

	for (const int32 Index : CellDataIndex)
	{
		if (CellsData.IsValidIndex(Index))
		{
			if (FindTransitionCellData(CellsData[Index].Cell_ID, TransCellData))
			{
				OnTransitionCellVisible.Broadcast(IsVisible, TransCellData.CellData.Cell_ID);
			}
		}
	}	
}

void ULevelManager::GenerateSpawnActorsTrack()
{
	ActorsSpawnTrack.Empty();

	for (int i = 0; i < ActorsSpawnData.Num(); i++)
	{
		GenerateInitialSpawnActorsTrack(i);
	}
}

void ULevelManager::GenerateInitialSpawnActorsTrack(int32 SpawnActorDataIndex)
{
	if (!ActorsSpawnData.IsValidIndex(SpawnActorDataIndex)) return;

	FLevel_ActorSpawnData DataLocal = ActorsSpawnData[SpawnActorDataIndex];
	int32 SpawnsAmount = UKismetMathLibrary::RandomIntegerInRange(DataLocal.Amount_Min, DataLocal.Amount_Max);
	
	FIntPoint CellID = FIntPoint();
	FVector2D Anchor = FVector2D::ZeroVector;
	int RandomCell = 0;
	int Chancerolled = 0;
	int32 StartRoomLocal = 0;	
	for (int i = 0; i < SpawnsAmount; i++)
	{
		Chancerolled = UKismetMathLibrary::RandomIntegerInRange(0, 100);		
		if (Chancerolled < DataLocal.SpawnChance)
		{		
			//Diferent spawn modes determine the cell diferently
			switch (DataLocal.SpawnType)
			{
			case ELevelGen_ActorSpawnType::Anywhere:
			{
				//Randomize a non blocking cell from the cell data array  @TODO make the cell non block, for now i dont care
				RandomCell = UKismetMathLibrary::RandomIntegerInRange(0, CellsData.Num() - 1);
				if (!CellsData.IsValidIndex(RandomCell)) return;
				CellID = CellsData[RandomCell].Cell_ID;
				GenerateTrackForSingleSpawnActor(SpawnActorDataIndex, CellID, DataLocal);
				break;
			}
			case ELevelGen_ActorSpawnType::PlayerStart:
			{
				CellID = GetStartCellID();
				if (CellID.X == -1 && CellID.Y == -1) return; //Invalid cell?
				GenerateTrackForSingleSpawnActor(SpawnActorDataIndex, CellID, DataLocal);
				break;
			}		
			case ELevelGen_ActorSpawnType::PerRoom:		
			{
				//Per room, need to get a cell based on anchors. 
				for (int j = 0; j < LayoutData.Num(); j++)
				{
					if (LayoutData[j].Type == ELevelGen_LevelType::Normal)
					{
						Anchor.X = UKismetMathLibrary::RandomFloatInRange(DataLocal.LocationAnchor_Min.X, DataLocal.LocationAnchor_Max.X);
						Anchor.Y = UKismetMathLibrary::RandomFloatInRange(DataLocal.LocationAnchor_Min.Y, DataLocal.LocationAnchor_Max.Y);

						CellID = GetClosesCellIDToAnchorPoint(LayoutData[j], Anchor);

						GenerateTrackForSingleSpawnActor(SpawnActorDataIndex, CellID, DataLocal);
					}
				}
				break;
			}				
			case ELevelGen_ActorSpawnType::PerRoomButStart:
			{
				StartRoomLocal = GetClosestLayoutIndexToAnchor(LayoutData, ELevelGen_LevelType::Normal, StartRoomAnchor);

				//Per room, need to get a cell based on anchors. 
				for (int j = 0; j < LayoutData.Num(); j++)
				{
					if (j != StartRoomLocal)
					{
						if (LayoutData[j].Type == ELevelGen_LevelType::Normal)
						{
							Anchor.X = UKismetMathLibrary::RandomFloatInRange(DataLocal.LocationAnchor_Min.X, DataLocal.LocationAnchor_Max.X);
							Anchor.Y = UKismetMathLibrary::RandomFloatInRange(DataLocal.LocationAnchor_Min.Y, DataLocal.LocationAnchor_Max.Y);

							CellID = GetClosesCellIDToAnchorPoint(LayoutData[j], Anchor);

							GenerateTrackForSingleSpawnActor(SpawnActorDataIndex, CellID, DataLocal);
						}
					}
				}
				break;
			}
			case ELevelGen_ActorSpawnType::PerCorridor:
			{
				//Per corridor, need to get a cell based on anchors. 
				for (int j = 0; j < LayoutData.Num(); j++)
				{
					if (LayoutData[j].Type == ELevelGen_LevelType::Corridor)
					{
						Anchor.X = UKismetMathLibrary::RandomFloatInRange(DataLocal.LocationAnchor_Min.X, DataLocal.LocationAnchor_Max.X);
						Anchor.Y = UKismetMathLibrary::RandomFloatInRange(DataLocal.LocationAnchor_Min.Y, DataLocal.LocationAnchor_Max.Y);

						CellID = GetClosesCellIDToAnchorPoint(LayoutData[j], Anchor);

						GenerateTrackForSingleSpawnActor(SpawnActorDataIndex, CellID, DataLocal);
					}
				}
				break;
			}			
			default:
				return;//If default return
				break;
			}	
		}
	}
}

void ULevelManager::GenerateTrackForSingleSpawnActor(int32 SpawnActorDataIndex, FIntPoint CellIDIn, const FLevel_ActorSpawnData& SpawnDataIn)
{
	FLevel_SingleActorSpawnTrack TrackLocal = FLevel_SingleActorSpawnTrack();
	TrackLocal.ActorSpawnDataIndex = SpawnActorDataIndex;

	TrackLocal.CellDataIndex = ULevelGeneratorFunctionLibrary::FindCellIDIndex(CellIDIn, CellsData);
		
	TrackLocal.Anchor.X = UKismetMathLibrary::RandomFloatInRange(SpawnDataIn.LocationAnchor_Min.X, SpawnDataIn.LocationAnchor_Max.X); //Use same anchors but now for cell? could have different anchors for cell, but is not really important.
	TrackLocal.Anchor.Y = UKismetMathLibrary::RandomFloatInRange(SpawnDataIn.LocationAnchor_Min.Y, SpawnDataIn.LocationAnchor_Max.Y);

	TrackLocal.Anchor.X = FMath::Clamp(TrackLocal.Anchor.X, 0.f, 1.f);
	TrackLocal.Anchor.Y = FMath::Clamp(TrackLocal.Anchor.Y, 0.f, 1.f);

	AddSpawnActorTrackToMap(TrackLocal);
}

void ULevelManager::AddSpawnActorTrackToMap(const FLevel_SingleActorSpawnTrack& TrackIn)
{
	int32 LevelPlacementIndex = CellsData[TrackIn.CellDataIndex].LevelPlacementDataIndex;
	FLevel_ActorSpawnTrack LevelTrack = FLevel_ActorSpawnTrack();

	if (ActorsSpawnTrack.Contains(LevelPlacementIndex))
	{
		LevelTrack = *ActorsSpawnTrack.Find(LevelPlacementIndex);
		LevelTrack.ActorsTrack.Add(TrackIn);
		ActorsSpawnTrack[LevelPlacementIndex] = LevelTrack;
	}
	else
	{
		LevelTrack.ActorsTrack.Add(TrackIn);
		ActorsSpawnTrack.Add(LevelPlacementIndex, LevelTrack);
	}
}

FLevel_SingleActorSpawnTrack ULevelManager::GetSpawnActorTrack(int32 LevelPlacementDataIndexIn, int ActorTrackIndexIn, bool& SuccesOut) const
{
	FLevel_ActorSpawnTrack LevelActorsTrack = FLevel_ActorSpawnTrack();

	if (!ActorsSpawnTrack.Contains(LevelPlacementDataIndexIn))
	{
		UE_LOG(LevelGeneratorLog, Error, TEXT("ULevelManager::GetSpawnActorTrack - Invalid level index."));
		SuccesOut = false;
		return FLevel_SingleActorSpawnTrack();
	}

	LevelActorsTrack = *ActorsSpawnTrack.Find(LevelPlacementDataIndexIn);

	if (!LevelActorsTrack.ActorsTrack.IsValidIndex(ActorTrackIndexIn))
	{
		UE_LOG(LevelGeneratorLog, Error, TEXT("ULevelManager::GetSpawnActorTrack - Invalid actor index."));
		SuccesOut = false;
		return FLevel_SingleActorSpawnTrack();
	}

	SuccesOut = true;
	return LevelActorsTrack.ActorsTrack[ActorTrackIndexIn];
}

void ULevelManager::SpawnActorForTrack(int32 LevelPlacementDataIndexIn, int ActorTrackIndexIn)
{	
	bool Succes = false;
	FLevel_SingleActorSpawnTrack Track = GetSpawnActorTrack(LevelPlacementDataIndexIn, ActorTrackIndexIn, Succes);
		
	if (!Succes)
	{
		return;
	}

	FTransform Transform = GetSpawnActorTransform(Track, Succes);
	
	if (Succes)
	{
		if (!ActorsSpawnData[Track.ActorSpawnDataIndex].ActorClass)
		{
			UE_LOG(LevelGeneratorLog, Error, TEXT("ULevelManager::SpawnActorForTrack - Invalid class."));
			return;
		}

		AActor* ActorReference = GetWorld()->SpawnActorDeferred<AActor>(ActorsSpawnData[Track.ActorSpawnDataIndex].ActorClass, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		UGameplayStatics::FinishSpawningActor(ActorReference, Transform);

		Track.ActorReference = ActorReference;
		Track.Transform = Transform;
		Track.ActorWasSpawned = true;
		FLevel_ActorSpawnTrack LevelActorsTrack = *ActorsSpawnTrack.Find(LevelPlacementDataIndexIn);
		LevelActorsTrack.ActorsTrack[ActorTrackIndexIn] = Track;
		ActorsSpawnTrack[LevelPlacementDataIndexIn] = LevelActorsTrack;
	}
		
	return;
}

FTransform ULevelManager::GetSpawnActorTransform(const FLevel_SingleActorSpawnTrack& TrackIn, bool& SuccesOut) const
{
	FVector CellLocation = ULevelGeneratorFunctionLibrary::GetCellWorldPosition(CellsData[TrackIn.CellDataIndex].Cell_ID, CellSize, InitialLocation, TrackIn.Anchor);
	
	FVector traceStart_ = FVector::ZeroVector;
	traceStart_ = CellLocation;
	traceStart_.Z = 5000;

	FVector traceEnd_ = FVector::ZeroVector;
	traceEnd_ = CellLocation;
	traceEnd_.Z = -5000;

	FHitResult TraceHitResults;
	FTransform TransformGenerated = FTransform();
	GetOwner()->GetWorld()->LineTraceSingleByChannel(TraceHitResults, traceStart_, traceEnd_, ECollisionChannel::ECC_GameTraceChannel1);//@TODO This will not work if u are not using landscape, maybe the trace channel needs to be a parameter etc.

	if (!TraceHitResults.bBlockingHit)
	{
		//If the level is not loaded yet, this can fail. That is why i need to retrigger actor spawns on tick.
		SuccesOut = false;
		return FTransform();
	}
	
	TransformGenerated = FTransform();
	TransformGenerated.SetLocation(TraceHitResults.Location);

	SuccesOut = true;
	return TransformGenerated;
}

void ULevelManager::ChangeVisibilitySpawnedActor(int32 LevelPlacementDataIndexIn, int ActorTrackIndexIn, bool IsVisible)
{	
	bool Succes = false;
	FLevel_SingleActorSpawnTrack Track = GetSpawnActorTrack(LevelPlacementDataIndexIn, ActorTrackIndexIn, Succes);

	if (!Succes) return;

	if (!Track.ActorReference.IsValid() && !Track.ActorWasSpawned && IsVisible)
	{
		SpawnActorForTrack(LevelPlacementDataIndexIn, ActorTrackIndexIn);
		Track = GetSpawnActorTrack(LevelPlacementDataIndexIn, ActorTrackIndexIn, Succes); //Must update track in this case.
	}

	if (!Track.ActorReference.IsValid()) //if the actor is not valid, it means it was destroyed by other means and is no longer needed.
	{
		return;
	}

	FCellData CellDataActualizedBiome = CellsData[Track.CellDataIndex];
	CellDataActualizedBiome.Cell_Biome = GetCellProminentBiome(CellDataActualizedBiome.Cell_ID, CellsData, TransitionCellsData);

	if (IsVisible)
	{
		Track.ActorReference->SetActorHiddenInGame(false);
		Track.ActorReference->SetActorTransform(Track.Transform);

		if (Track.ActorReference->GetClass()->ImplementsInterface(ULevelActorInterface::StaticClass()))
		{
			ILevelActorInterface::Execute_LevelActorVisibilityChanged(Track.ActorReference.Get(), true, CellDataActualizedBiome);
		}	
	}
	else
	{
		Track.ActorReference->SetActorHiddenInGame(true);

		if (Track.ActorReference->GetClass()->ImplementsInterface(ULevelActorInterface::StaticClass()))
		{
			ILevelActorInterface::Execute_LevelActorVisibilityChanged(Track.ActorReference.Get(), false, CellDataActualizedBiome);
		}
	}
}
