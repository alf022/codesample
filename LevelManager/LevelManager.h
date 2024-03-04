//Copyright 2022 Marchetti S. Alfredo I. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LevelManager/LevelGeneratorTypes.h"
#include "LevelManager/PopulateRoomWorker.h"
#include "LevelManager/RoomLayoutWorker.h"
#include "LevelManager/CorridorLayoutWorker.h"
#include "GameFramework/Character.h"
#include "LevelManager.generated.h"

class ULevelStreamingDynamic;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNoParamsDelegateLevelManagerSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLevelVisibilityChangedSignature, FLevel_ClientData, LevelClientData, bool, IsVisible);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLevelGenerationStateMessageSignature, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTransitionCellsDataGeneratedSignature, const FBiomeManagerInitializationData&, TransitionInitDataOut);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTransitionCellVisibleSignature, bool, IsVisible, const FIntPoint&, CellIDOut);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class LEVELGENERATOR_API ULevelManager : public UActorComponent
{
	GENERATED_BODY()

public:
	ULevelManager();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Called when the full level and all sublevels are loaded.*/
	UPROPERTY(BlueprintAssignable, Category = "Level Generation")
	FNoParamsDelegateLevelManagerSignature OnAllLevelsLoaded;
	
	/** Called when the level is fully generated and ready to be used.*/
	UPROPERTY(BlueprintAssignable, Category = "Level Generation")
	FNoParamsDelegateLevelManagerSignature OnLevelGenerationCompleted;

	/** Called when a level changes visiblity.*/
	UPROPERTY(BlueprintAssignable, Category = "Level Generation")
	FOnLevelVisibilityChangedSignature OnLevelVisibilityChanged;

	/** Called when there are at least one new level that is changed visible.*/
	UPROPERTY(BlueprintAssignable, Category = "Level Generation")
	FNoParamsDelegateLevelManagerSignature OnNewLevelShown;

	/** Called when there is a new state message.*/
	UPROPERTY(BlueprintAssignable, Category = "Level Generation")
	FOnLevelGenerationStateMessageSignature OnLevelGenerationStateMessage;

	/** Called when the transition cells data is generated.*/
	UPROPERTY(BlueprintAssignable, Category = "Level Generation")
	FOnTransitionCellsDataGeneratedSignature OnTransitionCellsDataGenerated;

	/** Called when the transition cell is visible.*/
	UPROPERTY(BlueprintAssignable, Category = "Level Generation")
	FOnTransitionCellVisibleSignature OnTransitionCellVisible;

	bool IsGeneratingLevel();
	FVector GetStartLevelPosition();
	FGameplayTag GetStartLevelBiome();
	FIntPoint GetStartCellID();

	UFUNCTION(BlueprintCallable, Category = "Level Data")
	TArray<FCellData> GetCellsData();

	UFUNCTION(BlueprintCallable, Category = "Level Data")
	FIntPoint GetGridSize();

	UFUNCTION(BlueprintCallable, Category = "Level Data")
	float GetCellSize();

	/** If level streaming automatic handling should be enabled. If disabled, all levels will be visible.*/
	UFUNCTION(BlueprintCallable, Category = "Level Generation")
	void EnableAutoLevelStreaming(bool Enable);

	void SetStartCellVisibilityStatus(bool IsVisible);

	/** TESTING ONLY*/
	UFUNCTION(BlueprintCallable, Category = "Level Data")
	void MoveLevels();

	/**
	*	Clears all generated levels.
	*	Stops level generation if is in process.
	*/
	UFUNCTION(BlueprintCallable, Category = "Level Generation")
	void ClearLevel();

	/**	Generates a level based on the data provided.*/
	UFUNCTION(BlueprintCallable, Category = "Level Generation")
	void GenerateLevel(const TArray <FLevel_ActorSpawnData>& ActorsSpawnDataIn);

private:
	/** The datatable that contains the base levels data.*/
	UPROPERTY(EditAnywhere, Category = "Level")
	UDataTable* Levels;

	/** Conditions for cell population */
	UPROPERTY(EditAnywhere, Category = "Level")
	TArray <FCellPopulateCondition> CellPopulateConditions;

	/**	The initial location of the map generated.*/
	UPROPERTY(EditAnywhere, Category = "Level")
	FVector InitialLocation = { 0, 0 , 0 };

	/**	The size of the cell. This must be provided based on the base levels.*/
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"), Category = "Level")
	float CellSize = 700;

	/**
	*	When creating a level at start of generation, if should block on load.
	*	This is faster, but might freeze the screen.
	*/
	UPROPERTY(EditAnywhere, Category = "Stream level")
	bool StartLevelsBlockOnLoad = true;

	/**
	*	When creating a level at runtime, if should block on load.
	*	This is faster, but might freeze the screen.
	*/
	UPROPERTY(EditAnywhere, Category = "Stream level")
	bool RuntimeLevelsBlockOnLoad = false;

	/**
	*	If true, will create level instances at runtime instead of generating all levels at start, and then only hidding and loading them.
	*	If true, will take much less time to load on start. Also might cause drops on fps while loading levels at runtime(Affects runtime performance).
	*	If false, this will load all levels on start. It can be slower on start but doest not affect runtime performance.
	*/
	UPROPERTY(EditAnywhere, Category = "Stream level")
	bool CreateLevelsAtRuntime = true;

	/**
	*	Use a level pool instead of creating all levels individually.
	*	If using landscapes, cannot use pool since the transform for landscapes cannot be changed at runtime and does not work properly.
	*/
	UPROPERTY(EditAnywhere, Category = "Stream level")
	bool UseLevelsPool = false;

	/** Distance from the players at wich levels must be loaded at any given time.*/
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"), Category = "Stream level")
	float LoadStreamLevelDistance = 2100.0;

	/**
	*	Added distance to the LoadStreamLevelDistance from the players at wich levels must be unloaded at any given time.
	*	Higher values will keep loaded for more time.
	*/
	UPROPERTY(EditAnywhere, meta =(ClampMin = "0"), Category = "Stream level")
	float UnloadStreamLevelTolerance = 500.0;

	/** If actors other than players can show levels.*/
	UPROPERTY(EditAnywhere, Category = "Stream level")
	bool ActorsCanShowLevels = false;

	/** The actor class that can also show levels besides the player pawns.*/
	UPROPERTY(EditAnywhere, Category = "Stream level")
	TSubclassOf <AActor> StreamActorClass = ACharacter::StaticClass();
					
	/** The percentage of circular corridors compared to rooms. More than 1 will generate double corridors or more per room.*/
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0") , Category = "Corridors")
	float CircularCorridorsAmountPercent = .3f;

	/** The max distance that separates the two rooms of circular corridor in cells.*/
	UPROPERTY(EditAnywhere, Category = "Corridors")
	int MaxCircularCorridorsCellDistance = 10;

	/** How the corridors should be selected.*/
	UPROPERTY(EditAnywhere, Category = "Corridors")
	ELevelGen_CorridorPathSelection CorridorSelectionType = ELevelGen_CorridorPathSelection::Threshold;

	/**
	*	The threshold to use when using that Corridor selection type.
	*	This threshold is added to the minimun possible distance.
	*	If 0, will generate the shortest possible corridors.
	*/
	UPROPERTY(EditAnywhere, Category = "Corridors")
	int32 CorridorSelectionThreshold = 2;

	/**
	*	The maximun amount of threads to create for corridors. 
	*	The fewer threads, the more memory it saves.
	*/
	UPROPERTY(EditAnywhere, Category = "Corridors")
	int32 MaximunCorridorLayoutThreads = 10;

	/** The walls cell size.*/
	UPROPERTY(EditAnywhere, Category = "Walls")
	int32 WallsCellSize = 2;

	/** The amount of rooms to place in each area and thread for layout. This is desired value only, the system will choose the final numbers.*/
	UPROPERTY(EditAnywhere, Category = "Area Layout")
	int32 RoomsAmountPerArea = 8;

	/**
	*	The room axis distribution.
	*	Value of 1 means the rooms will be distributed more horizontally.
	*	Value of 0 means the rooms will be distributed more vertically.
	*/
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", ClampMax = "1"), Category = "Area Layout")
	float AreaDistribution = .5;

	/** How the separation of areas is measured.*/
	UPROPERTY(EditAnywhere, Category = "Area Layout")
	ELevelGen_DistanceMode AreaSeparationMode = ELevelGen_DistanceMode::BetweenSides;

	/**
	*	The minimun allowed separation between areas, measured in cells.
	*	If negative, allows  superposition.
	*	Randomize between x and y values per room. Each room has a different value assigned.
	*/
	UPROPERTY(EditAnywhere, Category = "Area Layout")
	FVector2D MinAreaSeparation = { 1,1 };

	/**
	*	The max allowed separation between areas, measured in cells.
	*	Must be positive or 0. If 0 there wont be any check for max separation.
	*	Randomize between x and y values per room. Each room has a different value assigned.
	*/
	UPROPERTY(EditAnywhere, Category = "Area Layout")
	FVector2D MaxAreaSeparation = { 1,1 };

	/**
	*	Tag used for transition biomes. 
	*	If null there will be no generation of transition cells.
	*/
	UPROPERTY(EditAnywhere, Category = "Area Layout")
	FGameplayTag BiomeTransitionTag;

	/**	The biomes that are allowed for each area.*/
	UPROPERTY(EditAnywhere, Category = "Area Layout")
	TArray <FGameplayTag> PossibleBiomes;

	/** The min and max amount of rooms that can be generated in a level*/
	UPROPERTY(EditAnywhere, Category = "Room Layout")
	FIntPoint RoomsAmount = { 10, 10 };

	/**	The minimun allowed room size.*/
	UPROPERTY(EditAnywhere, Category = "Room Layout")
	FIntPoint MinRoomSize = { 2,2 };

	/**	The maximun allowed room size.*/
	UPROPERTY(EditAnywhere, Category = "Room Layout")
	FIntPoint MaxRoomSize = { 4,4 };

	/**
	*	The room distribution. 
	*	Value of 1 means the rooms will be distributed more horizontally.
	*	Value of 0 means the rooms will be distributed more vertically.
	*/
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", ClampMax = "1"), Category = "Room Layout")
	float RoomsDistribution = .5;

	/** How the separation of rooms is measured.*/
	UPROPERTY(EditAnywhere, Category = "Room Layout")
	ELevelGen_DistanceMode RoomSeparationMode = ELevelGen_DistanceMode::BetweenSides;

	/**
	*	The minimun allowed separation between rooms, measured in cells.
	*	If negative, allows  superposition.
	*	Randomize between x and y values per room. Each room has a different value assigned.
	*/
	UPROPERTY(EditAnywhere, Category = "Room Layout")
	FVector2D MinRoomSeparation = { 1,1 };

	/**
	*	The max allowed separation between rooms, measured in cells.
	*	Must be positive or 0. If 0 there wont be any check for max separation.
	*	Randomize between x and y values per room. Each room has a different value assigned.
	*/
	UPROPERTY(EditAnywhere, Category = "Room Layout")
	FVector2D MaxRoomSeparation = { 1,1 };

	/**	The start room position in the map using an anchor.*/
	UPROPERTY(EditAnywhere, Category = "Room Layout")
	FVector2D StartRoomAnchor = { 0,0 };

	/**	The start cell position in subgrid using an anchor.*/
	UPROPERTY(EditAnywhere, Category = "Room Layout")
	FVector2D StartCellAnchor = { 0,0 };
		
	UPROPERTY()
	bool bIsGeneratingLevel = false;

	UPROPERTY()
	TArray <FLevelData> LevelsData = TArray <FLevelData>();

	UPROPERTY()
	TArray<FLayoutData> LayoutData = TArray<FLayoutData>();

	UPROPERTY()
	TArray<FLayoutData> CorridorsLayoutData = TArray<FLayoutData>();	

	UPROPERTY()
	FLayoutData WallsLayoutData = FLayoutData();

	UPROPERTY()
	TArray <FLevelPlacementData> LevelPlacementData = TArray <FLevelPlacementData>();

	UPROPERTY()
	TArray<FCellData> CellsData = TArray<FCellData>();

	UPROPERTY()
	TArray<FLevel_TransitionCellData> TransitionCellsData = TArray<FLevel_TransitionCellData>();

	TArray <FPopulateRoomWorker*> PopulateRoomActiveThreads;

	TArray <FRoomLayoutWorker*> RoomLayoutActiveThreads;

	TArray <FCorridorLayoutWorker*> CorridorLayoutActiveThreads;
	
	UPROPERTY()
	FIntPoint GridSize = { 0,0 };
	
	UPROPERTY()
	TArray <FLevel_CellVisibilityStatus> ForcedVisibleCells = TArray <FLevel_CellVisibilityStatus>();

	UPROPERTY()
	TMap <int32, FLoadedLevelTrack> LoadedLevelsMap;

	UPROPERTY()
	TMap <int32, FLevelPoolTrack> LevelsPool;
	
	UPROPERTY()
	bool IsGeneratingLevelsPool = false;

	UPROPERTY()
	bool IsLevelStreamingHandlingActive = false;

	UPROPERTY()
	int32 LoadedLevelsAmount = 0;

	UPROPERTY()
	int32 StartLevelsAmount = 0;

	UPROPERTY()
	TArray <FLevel_ActorSpawnData> ActorsSpawnData = TArray <FLevel_ActorSpawnData>();

	UPROPERTY()
	TMap<int32, FLevel_ActorSpawnTrack> ActorsSpawnTrack;

	UPROPERTY()
	int32 GeneratedRoomsAmount = 0;
		
	void HandleLevelStreamming();
	TArray <FVector> GetPlayersPawnsLocations() const;
	TArray <FVector> GetNonPlayersPawnsLocations() const;	
	TArray <int32> GetLevelPlacementDataForVisibleCells() const;
	TArray <int32> GetLevelPlacementDataBetweenPositions(const TArray<FVector>& PositionsIn, const float& MinDistanceIn, const float& MaxDistanceIn, const TArray<int32>& VisibleLevelsIn) const;
	TArray <int32> GetLoadedLevelsPlacementData(const TMap <int32, FLoadedLevelTrack>& LoadedLevelsTrack) const;
	void ProcessAllCellVisibilityStatus();
	void HandleLevelsVisibility(const TArray<int32>& MustLoadLevels, const TArray<int32>& CanRemainLoadLevels, const TArray<int32>& PreviousTickLoadedLevels);	
	void HandleCellsVisibility(const TArray<int32>& PreviousTickLoadedLevels);	
	TArray <int32> GetCellsIndexFromPlacementDataArray(const TArray<int32>& PlacementDataLevelsIn) const;
	void HandlePooledLevelsTransform();
			
	void SetCellVisibilityStatus(FIntPoint CellID, int32 VisibilityTickDuration = 1);
	void OnCellVisibilityChanged(const TArray<int>& CellDataIndex, bool IsVisible);
	bool FindTransitionCellData(FIntPoint CellIDIn, FLevel_TransitionCellData& TransitionDataOut) const;

	FGameplayTag GetCellProminentBiome(const FIntPoint& CellIDIn, const TArray<FCellData>& CellsDataIn, const TArray< FLevel_TransitionCellData>& TransitionCellsDataIn);

	int32 GetMinimunDistanceToCells(FIntPoint OrigenIDIn, int MaxDistanceIn) const;
	FIntPoint GetClosesCellIDToAnchorPoint(const FLayoutData& LayoutDataIn, FVector2D AnchorIn) const;
	float GetWorldDistanceBetweenRooms(const FLayoutData& RoomA, const FLayoutData& RoomB) const;
	TArray <FIntPoint> GetLevelCellsIDs(int LevelPlacementDataIn) const;
	int	GetClosestLayoutIndexToAnchor(const TArray<FLayoutData>& LayoutDataIn, ELevelGen_LevelType TypeIn, FVector2D AnchorIn) const;
	FVector GetGridWorldLocationInAnchor(FVector2D AnchorIn) const;

	TArray<FLevel_RoomDistance> GenerateInitialCorridorsLayoutData(const TArray<FLayoutData>& LayoutDataIn) const;
	FLevel_RoomDistance GetMinimunRoomDistancePaths(const TArray<int32>& ThreeRoomsIn, const TArray<FLevel_RoomDistance>& RoomsDistanceIn, int32& RoomDistanceIndexOut) const;
	void AddUnusedCorridors(TArray<FLevel_RoomDistance>& UnusedCorridorsIn, TArray<FLevel_RoomDistance>& ThreeCorridorsOut) const;
			
	FVector GetPlacedLevelPosition(int LevelPlacementDataIndex) const;
	FRotator GetPlacedLevelRotation(int LevelPlacementDataIndex) const;
	void ClearLevelsPool();
	int32 GetLevelsPoolNumber();
	void GenerateLevelsPool();
	int32 GetLevelDataPoolMinimunLevels(int32 LevelDataIndexIn) const;
	void AddLevelToPool(int32 LevelDataIndexIn, ULevelStreamingDynamic* StreamingLevel);
	void RemoveLevelFromPool(int32 LevelDataIndexIn, ULevelStreamingDynamic* StreamingLevel);
	bool IsLevelAvailableInPool(int32 LevelDataIndexIn);

	bool SetLevelTransform(ULevelStreaming* StreamingLevel, const FTransform& Transform);
	void RemoveLevelTransform(const ULevelStreaming* StreamingLevel);
	void ApplyLevelTransform(ULevel* Level, const FTransform& LevelTransform);

	FString GetLevelInstanceNameOverride(int LevelPlacementDataIndex) const;
	void CreateLevelInstance(int LevelPlacementDataIndex, bool ForceAsyncOnLoad = false);
	void UpdateLevelTrack(int LevelPlacementDataIndexIn, ULevelStreamingDynamic* LevelStreamingIn, ELevelGen_LevelState StateIn, bool IsFirstTimeLoadedIn);
	FLoadedLevelTrack GetLevelTrack(int LevelPlacementDataIndexIn) const;
	FLevel_ClientData GetLevelClientData(int LevelPlacementDataIndexIn) const;		
	TArray <FLevel_ClientData> GetLoadedLevelsClientData() const;
	
	UFUNCTION()
	void OnPoolLevelInstanceLoaded();

	UFUNCTION()
	void OnLevelInstanceLoaded();

	void OnLevelCompletedLoading(int32 LevelPlacementDataIndexIn, const FLoadedLevelTrack& LevelTrackIn);
	void OnLevelStartUnloading(int32 LevelPlacementDataIndexIn);

	bool IsLevelLoaded(int LevelPlacementDataIndex) const;
	bool CanHideLevel(int LevelPlacementDataIndex) const;
	bool CanHideCell(FIntPoint CellIDIn) const;
	void ChangeLevelVisibibility(int LevelPlacementDataIndex, bool IsVisible);
	bool UpdateLevelsData();

	int32 GenerateThreadID();

	void StartRoomsLayoutDataGeneration();
	bool AreAllRoomLayoutThreadsCompleted() const;
	void HandleRoomLayoutThreads();
	TArray <FGameplayTag> GenerateBiomesForAreas(int32 AreasAmountIn);
	void StartCorridorsLayoutDataGeneration();
	bool AreAllCorridorLayoutThreadsCompleted() const;
	void HandleCorridorLayoutThreads();
	void OnCorridorsLayoutDataGenerated();

	TArray <FCellData> GetWallsCellsData() const;
	void UpdateBiomesForWallCellsData(TArray <FCellData>& CellsDataOut);
	void UpdateBiomesForWallCell(const FGameplayTagContainer& BiomesIn, const TArray<ELevelGen_LevelType>& TypesIn, bool UseCellsDataGlobal, TArray <FCellData>& WallsCellsDataOut);
	bool HasCellsWithBiomesAndType(const FGameplayTagContainer& BiomesIn, const TArray<ELevelGen_LevelType>& TypesIn, const TArray<FCellData>& CellsDataIn, FCellData& MatchingCellOut) const;	
	bool HasAnyCellWithNoBiome(const TArray<FCellData>& CellsDataIn);
	TArray<FCellData> MutateTransitionCells(TArray<FCellData>& CellsDataOut) const;
	bool CanMutateCell(const FCellData& CellIn, const TArray<FCellData>& CellsDataIn) const;
	void UpdateMutatedCellsInLayouts(const TArray<FCellData>& MutatedCellsIn, TArray<FLayoutData>& LayoutsOut) const;
	void StartRoomsPopulation();
	void StartCorridorsPopulation();
	void StartWallsPopulation();
	bool AreAllPopulateRoomThreadsCompleted() const;
	void HandlePopulateRoomThreads();
	void ProcessPopulateRoomThreadsOutput(FPopulateRoomWorker* PopulateRoomThreadIn);
	void OnLevelGenerated();

	void StartStreamLoadLevels();
	void InitializeLevelsPool();
	void CreateAllStartAreaLevelInstances();
	TArray <int32> GetStartAreaLevelsPlacementData(TArray<FIntPoint>& CellsIDsOut);
	void FinishStreamLoadStartLevels();

	TArray <FLevel_TransitionCellData> GetTransitionCellsData() const;
	bool PropagateBiomesFromAdyacentTransitionCells(TArray <FLevel_TransitionCellData>& TransitionDataOut) const;
	bool PropagateBiomesFromAdyacentQuadsInSameCell(ELevelGen_QuadPropagationMode ModeIn, TArray <FLevel_TransitionCellData>& TransitionDataOut) const;
	void UpdateTransitionCellSurroundingBiomes(const FCellData& CellIn, FLevel_TransitionCellData& TransitionDataOut) const;
	FGameplayTag GetAdyacentCellBiomeInDirection(ELevelGen_CorridorDirection DirectionIn, const FCellData& CellIn) const;
	TArray <FGameplayTag> GetAdyacentBiomesOfQuadInDirection_InOwnCell(ELevelGen_CorridorDirection DirectionIn, const FLevel_TransitionCellData& CellTransDataIn, const TArray <FLevel_TransitionCellData>& TransitionDataIn) const;
	FGameplayTag GetAdyacentBiomeOfQuadInDirection_InAdyacentCell(ELevelGen_CorridorDirection DirectionIn, const FLevel_TransitionCellData& CellTransDataIn, const TArray <FLevel_TransitionCellData>& TransitionDataIn) const;
	FGameplayTag GetQuadBiomeInDirection(ELevelGen_CorridorDirection DirectionIn, const FLevel_TransitionCellData& CellTransDataIn) const;
	void SetQuadBiomeInDirection(ELevelGen_CorridorDirection DirectionIn, const FGameplayTag& NewValueIn, FLevel_TransitionCellData& CellTransDataOut) const;
	ELevelGen_CorridorDirection AdjustDirectionWithCellRotation(ELevelGen_CorridorDirection DirectionIn, const FLevel_TransitionCellData& CellTransDataIn)const;
	bool AreAllTransitionCellsValid(const TArray<FLevel_TransitionCellData>& TransitionDataIn) const;
	FGameplayTag GetRandomValidBiomeInArray(const TArray <FGameplayTag>& BiomesIn) const;
	int32 GetValidBiomeAmount(const TArray <FGameplayTag>& BiomesIn) const;
	int32 GetBiomeAmount(const FGameplayTag& BiomeIn, const TArray <FGameplayTag>& BiomesIn) const;
	bool IsBiomeValid(const FGameplayTag BiomeIn) const;

	void GenerateSpawnActorsTrack();
	void GenerateInitialSpawnActorsTrack(int32 SpawnActorDataIndex);
	void GenerateTrackForSingleSpawnActor(int32 SpawnActorDataIndex, FIntPoint CellIDIn, const FLevel_ActorSpawnData& SpawnDataIn);
	void AddSpawnActorTrackToMap(const FLevel_SingleActorSpawnTrack& TrackIn);
	FLevel_SingleActorSpawnTrack GetSpawnActorTrack(int32 LevelPlacementDataIndexIn, int ActorTrackIndexIn, bool& SuccesOut) const;
	void SpawnActorForTrack(int32 LevelPlacementDataIndexIn, int ActorTrackIndexIn);
	FTransform GetSpawnActorTransform(const FLevel_SingleActorSpawnTrack& TrackIn, bool& SuccesOut) const;
	void ChangeVisibilitySpawnedActor(int32 LevelPlacementDataIndexIn, int ActorTrackIndexIn, bool IsVisible);

	void OnLevelGenerationEnd();		
};
