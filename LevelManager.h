//Copyright 2020 Marchetti S. Alfredo I. All Rights Reserved.

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

	/*Called when the full level and all sublevels are loaded.*/
	UPROPERTY(BlueprintAssignable, Category = "Level Generation")
	FNoParamsDelegateLevelManagerSignature OnAllLevelsLoaded;
	
	/*Called when the level is fully generated and ready to be used.*/
	UPROPERTY(BlueprintAssignable, Category = "Level Generation")
	FNoParamsDelegateLevelManagerSignature OnLevelGenerationCompleted;

	/*Called when a level changes visiblity.*/
	UPROPERTY(BlueprintAssignable, Category = "Level Generation")
	FOnLevelVisibilityChangedSignature OnLevelVisibilityChanged;

	/*Called when there are at least one new level that is changed visible.*/
	UPROPERTY(BlueprintAssignable, Category = "Level Generation")
	FNoParamsDelegateLevelManagerSignature OnNewLevelShown;

	///*Called when there are at least one new level that is changed visible.*/
	//UPROPERTY(BlueprintAssignable, Category = "Level generator")
	//FNoParamsDelegateLevelManagerSignature OnNewCompletedLoading;

	/*Called when there is a new state message.*/
	UPROPERTY(BlueprintAssignable, Category = "Level Generation")
	FOnLevelGenerationStateMessageSignature OnLevelGenerationStateMessage;

	/*Called when the transition cells data is generated.*/
	UPROPERTY(BlueprintAssignable, Category = "Level Generation")
	FOnTransitionCellsDataGeneratedSignature OnTransitionCellsDataGenerated;

	/*Called when the transition cell is visible.*/
	UPROPERTY(BlueprintAssignable, Category = "Level Generation")
	FOnTransitionCellVisibleSignature OnTransitionCellVisible;

	/*Determine if there is a generating level process running currently.*/
	bool IsGeneratingLevel();

	/*Get the starting cell ID.*/
	FIntPoint GetStartCellID();

	/*Shows the starting cell so players can be spawned on it.*/
	void SetStartCellVisibilityStatus(bool IsVisible);

	/*Gets the starting level position for players.*/
	FVector GetStartLevelPosition();

	/*Gets the starting level biome.*/
	FGameplayTag GetStartLevelBiome();

	/*Get the cells data*/
	UFUNCTION(BlueprintCallable, Category = "Level Data")
	TArray<FCellData> GetCellsData();

	/*Get the grid size*/
	UFUNCTION(BlueprintCallable, Category = "Level Data")
	FIntPoint GetGridSize();

	/*Get the cell size*/
	UFUNCTION(BlueprintCallable, Category = "Level Data")
	float GetCellSize();

	/*TESTING ONLY*/
	UFUNCTION(BlueprintCallable, Category = "Test")
	void MoveLevels();

	/*If level streaming automatic handling should be enabled. If not all levels will be visible.*/
	UFUNCTION(BlueprintCallable, Category = "Level Generation")
	void EnableAutoLevelStreaming(bool Enable);

	/**
	*	Clears all generated stuff.
	*	Stops level generation if is in process.
	*/
	UFUNCTION(BlueprintCallable, Category = "Level Generation")
	void ClearLevel();

	/**
	*	Generates a level based on the data provided.
	*/
	UFUNCTION(BlueprintCallable, Category = "Level Generation")
	void GenerateLevel(const TArray <FLevel_ActorSpawnData>& ActorsSpawnDataIn);

protected:
	/*The datatable that contains the level data.*/
	UPROPERTY(EditAnywhere, Category = "Level")
	UDataTable* Levels;

	/** Conditions for cell population */
	UPROPERTY(EditAnywhere, Category = "Level")
	TArray <FCellPopulateCondition> CellPopulateConditions;

	/**
	*	The initial location of the map generated.
	*/
	UPROPERTY(EditAnywhere, Category = "Level")
	FVector InitialLocation = { 0, 0 , 0 };

	/**
	*	The size of the cell. This must be provided based on the levels.
	*	Make sure the landscape is at 0,0,0 location in the level. It can work weird with negative values and rotations.
	*/
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
	*	If true, will take much less to load on start. Also might cause drops on fps while loading levels at runtime(Affects runtime performance).
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

	/*Distance from the players at wich levels must be loaded at any given time.*/
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"), Category = "Stream level")
	float LoadStreamLevelDistance = 2100.0;

	/**
	*	Added distance to the LoadStreamLevelDistance from the players at wich levels must be unloaded at any given time.
	*	Higher values will keep loaded for more time.
	*/
	UPROPERTY(EditAnywhere, meta =(ClampMin = "0"), Category = "Stream level")
	float UnloadStreamLevelTolerance = 500.0;

	/*If actors other than players can show levels.*/
	UPROPERTY(EditAnywhere, Category = "Stream level")
	bool ActorsCanShowLevels = false;

	/*The actor class that can also show levels besides the player pawns. Anything that can walk should be here, to avoid falling from the level.*/
	UPROPERTY(EditAnywhere, Category = "Stream level")
	TSubclassOf <AActor> StreamActorClass = ACharacter::StaticClass();
					
	/*The percentage of circular corridors compared to rooms. More than 1 will generate double corridors or more per room.*/
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0") , Category = "Corridors")
	float CircularCorridorsAmountPercent = .3f;

	/*The max distance that separates the two rooms of circular corridor in cells.*/
	UPROPERTY(EditAnywhere, Category = "Corridors")
	int MaxCircularCorridorsCellDistance = 10;

	/*How the corridors should be selected.*/
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

	/*The walls cell size.*/
	UPROPERTY(EditAnywhere, Category = "Walls")
	int32 WallsCellSize = 2;

	/*The amount of rooms to place in each area and thread for layout. This is desired value only, the system will choose the final numbers.*/
	UPROPERTY(EditAnywhere, Category = "Area Layout")
	int32 RoomsAmountPerArea = 8;

	/**
	*	The room distribution.
	*	Value of 1 means the rooms will be distributed more horizontally.
	*	Value of 0 means the rooms will be distributed more vertically.
	*/
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", ClampMax = "1"), Category = "Area Layout")
	float AreaDistribution = .5;

	/*How the separation of areas is done.*/
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

	/**
	*	The biomes that are allowed for each area.	
	*/
	UPROPERTY(EditAnywhere, Category = "Area Layout")
	TArray <FGameplayTag> PossibleBiomes;

	/*The min and max amount of rooms that can be generated in a level*/
	UPROPERTY(EditAnywhere, Category = "Room Layout")
	FIntPoint RoomsAmount = { 10, 10 };

	/**
	*	The minimun allowed room size.
	*/
	UPROPERTY(EditAnywhere, Category = "Room Layout")
	FIntPoint MinRoomSize = { 2,2 };

	/**
	*	The maximun allowed room size.
	*/
	UPROPERTY(EditAnywhere, Category = "Room Layout")
	FIntPoint MaxRoomSize = { 4,4 };

	/**
	*	The room distribution. 
	*	Value of 1 means the rooms will be distributed more horizontally.
	*	Value of 0 means the rooms will be distributed more vertically.
	*/
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", ClampMax = "1"), Category = "Room Layout")
	float RoomsDistribution = .5;

	/*How the separation of rooms is done.*/
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

	/**
	*	The start room position in the map using an anchor.
	*/
	UPROPERTY(EditAnywhere, Category = "Room Layout")
	FVector2D StartRoomAnchor = { 0,0 };

	/**
	*	The start cell position in subgrid using an anchor.
	*/
	UPROPERTY(EditAnywhere, Category = "Room Layout")
	FVector2D StartCellAnchor = { 0,0 };
	
	/*Track if there is a level beign generated or not.*/
	UPROPERTY()
	bool bIsGeneratingLevel = false;

	/**
	*	The levels data for the current level generation.
	*/
	UPROPERTY()
	TArray <FLevelData> LevelsData = TArray <FLevelData>();

	/*All layout data.*/
	UPROPERTY()
	TArray<FLayoutData> LayoutData = TArray<FLayoutData>();

	/*All corridors layout data.*/
	UPROPERTY()
	TArray<FLayoutData> CorridorsLayoutData = TArray<FLayoutData>();	

	/*Walls layout data.*/
	UPROPERTY()
	FLayoutData WallsLayoutData = FLayoutData();

	/**
	*	The levels placement data generated for the current level.
	*/
	UPROPERTY()
	TArray <FLevelPlacementData> LevelPlacementData = TArray <FLevelPlacementData>();

	/**
	*	The level current generated grid.
	*/
	UPROPERTY()
	TArray<FCellData> CellsData = TArray<FCellData>();

	UPROPERTY()
	TArray<FLevel_TransitionCellData> TransitionCellsData = TArray<FLevel_TransitionCellData>();

	/*The active threads for populating rooms.*/
	TArray <FPopulateRoomWorker*> PopulateRoomActiveThreads;

	/*The active threads for room layout generation.*/
	TArray <FRoomLayoutWorker*> RoomLayoutActiveThreads;

	/*The active threads for populating corridors.*/
	TArray <FCorridorLayoutWorker*> CorridorLayoutActiveThreads;
	
	/*The grid size generated.*/
	UPROPERTY()
	FIntPoint GridSize = { 0,0 };
	
	/*All the cells that are forced to be visible.*/
	UPROPERTY()
	TArray <FLevel_CellVisibilityStatus> ForcedVisibleCells = TArray <FLevel_CellVisibilityStatus>();

	/*Track loaded levels*/
	UPROPERTY()
	TMap <int32, FLoadedLevelTrack> LoadedLevelsMap;

	/*Levels pool*/
	UPROPERTY()
	TMap <int32, FLevelPoolTrack> LevelsPool;
	
	/*Track if there is a levels pool generation.*/
	UPROPERTY()
	bool IsGeneratingLevelsPool = false;

	/*Track the actual streamming operation. This must be used to keep the order of operations in the manager.*/
	UPROPERTY()
	bool IsLevelStreamingHandlingActive = false;

	/*Track the levels that finished loading.*/
	UPROPERTY()
	int32 LoadedLevelsAmount = 0;

	/*The levels to be loaded on start.*/
	UPROPERTY()
	int32 StartLevelsAmount = 0;

	/*Data for spawning additional actors in the level.*/
	UPROPERTY()
	TArray <FLevel_ActorSpawnData> ActorsSpawnData = TArray <FLevel_ActorSpawnData>();

	/*Spawn actors track for each level placement data index.*/
	UPROPERTY()
	TMap<int32, FLevel_ActorSpawnTrack> ActorsSpawnTrack;

	/*Track rooms number.*/
	UPROPERTY()
	int32 GeneratedRoomsAmount = 0;

	/*Determines if the levels have to be loaded or unloaded based on player pawn position.*/
	void HandleLevelStreamming();

	/*Gets all the levels placement data indexes that are forced for the visible cells*/
	TArray <int32> GetLevelPlacementDataForVisibleCells() const;

	/*Gets all the levels placement data indexes that are close to a world position*/
	TArray <int32> GetLevelPlacementDataBetweenPositions(const TArray<FVector>& PositionsIn, const float& MinDistanceIn, const float& MaxDistanceIn, const TArray<int32>& VisibleLevelsIn) const;

	/*Given an array of levels placement data, get the cells IDs of those levels*/
	TArray <int32> GetCellsIndexFromPlacementDataArray(const TArray<int32>& PlacementDataLevelsIn) const;

	/*Get the data for loaded levels.*/
	TArray <int32> GetLoadedLevelsPlacementData(const TMap <int32, FLoadedLevelTrack>& LoadedLevelsTrack) const;

	/*Determine if a cell is close to any of the given positions.*/
	bool IsCellCloseToPositions(const int32& CellDataIndexIn, const TArray<FVector>& PositionsIn, const float& DistanceIn) const;

	/*Get the most common biome of the cell.*/
	FGameplayTag GetCellProminentBiome(const FIntPoint& CellIDIn, const TArray<FCellData>& CellsDataIn, const TArray< FLevel_TransitionCellData>& TransitionCellsDataIn);

	/*Get the minimun distance to other cells from a cell ID point.*/
	int32 GetMinimunDistanceToCells(FIntPoint OrigenIDIn, int MaxDistanceIn) const;

	/*Gets the ID in the subgrid layout data that better adjust to the provided anchor.*/
	FIntPoint GetClosesCellIDToAnchorPoint(const FLayoutData& LayoutDataIn, FVector2D AnchorIn) const;

	/*Gets the distance to the center points between two rooms.*/
	float GetWorldDistanceBetweenRooms(const FLayoutData& RoomA, const FLayoutData& RoomB) const;

	/*Gets the level cells IDs*/
	TArray <FIntPoint> GetLevelCellsIDs(int LevelPlacementDataIn) const;

	/*Gets the ID in the subgrid layout data for the rooms that better adjust to the provided anchor.*/
	int	GetClosestLayoutIndexToAnchor(const TArray<FLayoutData>& LayoutDataIn, ELevelGen_LevelType TypeIn, FVector2D AnchorIn) const;

	/*Gets the world location in the anchor for the whole grid.*/
	FVector GetGridWorldLocationInAnchor(FVector2D AnchorIn) const;

	/*Generates the initial ID corridors data. Decides the connections between and removes the innecesary ones.*/
	TArray<FLevel_RoomDistance> GenerateInitialCorridorsLayoutData(const TArray<FLayoutData>& LayoutDataIn) const;

	/*Select minimun room distance corridor layout.*/
	FLevel_RoomDistance GetMinimunRoomDistancePaths(const TArray<int32>& ThreeRoomsIn, const TArray<FLevel_RoomDistance>& RoomsDistanceIn, int32& RoomDistanceIndexOut) const;

	/**
	*	Adds some of the unused corridors to the three corridors array.
	*/
	void AddUnusedCorridors(TArray<FLevel_RoomDistance>& UnusedCorridorsIn, TArray<FLevel_RoomDistance>& ThreeCorridorsOut) const;

	/**
	* Get the level location based on placement data.
	*/
	FVector GetPlacedLevelPosition(int LevelPlacementDataIndex) const;

	/**
	* Get the level  rotation based on placement data.
	*/
	FRotator GetPlacedLevelRotation(int LevelPlacementDataIndex) const;

	/*Clears the level pool.*/
	void ClearLevelsPool();

	/*Gets the level pool number*/
	int32 GetLevelsPoolNumber();

	/**
	*	Generates a level pool.
	*/
	void GenerateLevelsPool();
	
	/*Get the min amount of levels for this leveldata index.*/
	int32 GetLevelDataPoolMinimunLevels(int32 LevelDataIndexIn) const;

	/*Add a level to the pool*/
	void AddLevelToPool(int32 LevelDataIndexIn, ULevelStreamingDynamic* StreamingLevel);

	/*Removes a level from the pool*/
	void RemoveLevelFromPool(int32 LevelDataIndexIn, ULevelStreamingDynamic* StreamingLevel);

	/*Check if there is a level for this data index in the pool*/
	bool IsLevelAvailableInPool(int32 LevelDataIndexIn);

	/*Sets the level transform by displacing all actors.*/
	bool SetLevelTransform(ULevelStreaming* StreamingLevel, const FTransform& Transform);

	void RemoveLevelTransform(const ULevelStreaming* StreamingLevel);

	void ApplyLevelTransform(ULevel* Level, const FTransform& LevelTransform);

	/*Creates the level instance for this level placement data.*/
	void CreateLevelInstance(int LevelPlacementDataIndex, bool ForceAsyncOnLoad = false);

	/*Update the level track*/
	void UpdateLevelTrack(int LevelPlacementDataIndexIn, ULevelStreamingDynamic* LevelStreamingIn, ELevelGen_LevelState StateIn, bool IsFirstTimeLoadedIn);

	/*Gets the level track*/
	FLoadedLevelTrack GetLevelTrack(int LevelPlacementDataIndexIn) const;

	/*Gets the level client data*/
	FLevel_ClientData GetLevelClientData(int LevelPlacementDataIndexIn) const;
		
	/*Gets all loaded levels client data*/
	TArray <FLevel_ClientData> GetLoadedLevelsClientData() const;

	/*Check that the level is loaded*/
	void OnPoolLevelInstanceLoaded();

	/*Check that the level is loaded*/
	void OnLevelInstanceLoaded();

	/*Called when the level is loaded.*/
	void OnLevelCompletedLoading(int32 LevelPlacementDataIndexIn, const FLoadedLevelTrack& LevelTrackIn);

	/*Called when the level is unloaded or hidden.*/
	void OnLevelStartUnloading(int32 LevelPlacementDataIndexIn);

	/*Determine if a level is loaded.*/
	bool IsLevelLoaded(int LevelPlacementDataIndex) const;

	/*Determine if a level can be hidden.*/
	bool CanHideLevel(int LevelPlacementDataIndex) const;
	
	/*Determine if a cell can be hidden.*/
	bool CanHideCell(FIntPoint CellIDIn) const;

	/*Changes a level visibility.*/
	void ChangeLevelVisibibility(int LevelPlacementDataIndex, bool IsVisible);

	/*Updates the level data using the level datatable.*/
	bool UpdateLevelsData();

	/*Generates a ID for seeding the random gen in the thread.*/
	int32 GenerateThreadID();

	/*Start the layout of the rooms in the level.*/
	void StartRoomsLayoutDataGeneration();
	
	/**
	*	Determine if all threads are completed.
	*/
	bool AreAllRoomLayoutThreadsCompleted() const;

	/*Reads data from threads and executes next logic for level generation.*/
	void HandleRoomLayoutThreads();

	/*Generates an array of biomes for the areas.*/
	TArray <FGameplayTag> GenerateBiomesForAreas(int32 AreasAmountIn);

	/*Start the layout of the corridors in the level.*/
	void StartCorridorsLayoutDataGeneration();

	/**
	*	Determine if all threads are completed.
	*/
	bool AreAllCorridorLayoutThreadsCompleted() const;

	/*Reads data from threads and executes next logic for level generation.*/
	void HandleCorridorLayoutThreads();

	/*Called after all corridor layout data is generated.*/
	void OnCorridorsLayoutDataGenerated();

	/*Get the cells that should be used for walls.*/
	TArray <FCellData> GetWallsCellsData() const;

	/*Updates biomes for walls.*/
	void UpdateBiomesForWallCellsData(TArray <FCellData>& CellsDataOut);

	/*Update the biome in the cell.*/
	void UpdateBiomesForWallCell(const FGameplayTagContainer& BiomesIn, const TArray<ELevelGen_LevelType>& TypesIn, bool UseCellsDataGlobal, TArray <FCellData>& WallsCellsDataOut);
	
	/*Determine if has a cell with at least one of the biomes.*/
	bool HasCellsWithBiomesAndType(const FGameplayTagContainer& BiomesIn, const TArray<ELevelGen_LevelType>& TypesIn, const TArray<FCellData>& CellsDataIn, FCellData& MatchingCellOut) const;
	
	/*Determine if are cells without biome*/
	bool HasAnyCellWithNoBiome(const TArray<FCellData>& CellsDataIn);

	/*Find and mutate all transition cells.*/
	TArray<FCellData> MutateTransitionCells(TArray<FCellData>& CellsDataOut) const;

	/*Determine if a cells is a transition cell and can be mutated.*/
	bool CanMutateCell(const FCellData& CellIn, const TArray<FCellData>& CellsDataIn) const;

	/*Update the layouts with mutated cells data.*/
	void UpdateMutatedCellsInLayouts(const TArray<FCellData>& MutatedCellsIn, TArray<FLayoutData>& LayoutsOut) const;
	
	/*Populate rooms*/
	void StartRoomsPopulation();

	/*Populate corridors*/
	void StartCorridorsPopulation();

	/*Populate walls.*/
	void StartWallsPopulation();

	/*Determine if all threads are completed.*/
	bool AreAllPopulateRoomThreadsCompleted() const;

	/*Reads data from threads and executes next logic for level generation.*/
	void HandlePopulateRoomThreads();

	/*Processing of data generated by the thread.*/
	void ProcessPopulateRoomThreadsOutput(FPopulateRoomWorker* PopulateRoomThreadIn);

	/*Called after all population of blocking cells is completed.*/
	void OnLevelGenerated();

	/*Start stream load levels that are needed.*/
	void StartStreamLoadLevels();

	/*Initialize the levels pool.*/
	void InitializeLevelsPool();

	/*Creates all level instances for the starting area.*/
	void CreateAllStartAreaLevelInstances();

	/*Gets the start levels area and also retuns the cells IDs*/
	TArray <int32> GetStartAreaLevelsPlacementData(TArray<FIntPoint>& CellsIDsOut);

	/*Finish stream load levels that are needed in start.*/
	void FinishStreamLoadStartLevels();

	/*Get the transition cells data and notify.*/
	TArray <FLevel_TransitionCellData> GetTransitionCellsData() const;

	/**
	*	Propagates biomes of adyacent transition cells.
	*	Return wheter it could update at least one quad or not.
	*/
	bool PropagateBiomesFromAdyacentTransitionCells(TArray <FLevel_TransitionCellData>& TransitionDataOut) const;

	/**
	*	Propagates biomes of adyacent quads in the same cell.
	*	Return wheter it could update at least one quad or not.
	*/
	bool PropagateBiomesFromAdyacentQuadsInSameCell(ELevelGen_QuadPropagationMode ModeIn, TArray <FLevel_TransitionCellData>& TransitionDataOut) const;

	/*Get the transition cells data and notify.*/
	void UpdateTransitionCellSurroundingBiomes(const FCellData& CellIn, FLevel_TransitionCellData& TransitionDataOut) const;

	/*Gets the biome of the adyacent cell of a cell in a determined direction.*/
	FGameplayTag GetAdyacentCellBiomeInDirection(ELevelGen_CorridorDirection DirectionIn, const FCellData& CellIn) const;

	/*Gets all tags of quads in the same cell than this quad is.*/
	TArray <FGameplayTag> GetAdyacentBiomesOfQuadInDirection_InOwnCell(ELevelGen_CorridorDirection DirectionIn, const FLevel_TransitionCellData& CellTransDataIn, const TArray <FLevel_TransitionCellData>& TransitionDataIn) const;

	/*Gets the tag of the quad adyacent to the cell in the adyacent transition cell.*/
	FGameplayTag GetAdyacentBiomeOfQuadInDirection_InAdyacentCell(ELevelGen_CorridorDirection DirectionIn, const FLevel_TransitionCellData& CellTransDataIn, const TArray <FLevel_TransitionCellData>& TransitionDataIn) const;

	/**
	*	Gets the biome tag of a quad in a direction.	
	*/
	FGameplayTag GetQuadBiomeInDirection(ELevelGen_CorridorDirection DirectionIn, const FLevel_TransitionCellData& CellTransDataIn) const;

	/**
	*	Sets the biome tag of a quad in a direction.
	*/
	void SetQuadBiomeInDirection(ELevelGen_CorridorDirection DirectionIn, const FGameplayTag& NewValueIn, FLevel_TransitionCellData& CellTransDataOut) const;

	/*Adjust rotation*/
	ELevelGen_CorridorDirection AdjustDirectionWithCellRotation(ELevelGen_CorridorDirection DirectionIn, const FLevel_TransitionCellData& CellTransDataIn)const;

	/*Find if all transition cells are correctly set.*/
	bool AreAllTransitionCellsValid(const TArray<FLevel_TransitionCellData>& TransitionDataIn) const;
	
	/*Randomize a valid biome in the array.*/
	FGameplayTag GetRandomValidBiomeInArray(const TArray <FGameplayTag>& BiomesIn) const;

	/*Counts the valid biomes.*/
	int32 GetValidBiomeAmount(const TArray <FGameplayTag>& BiomesIn) const;

	/*Counts the biomes of specific type.*/
	int32 GetBiomeAmount(const FGameplayTag& BiomeIn, const TArray <FGameplayTag>& BiomesIn) const;

	/*Determine if the biome is valid. This means is not null or Transition.*/
	bool IsBiomeValid(const FGameplayTag BiomeIn) const;

	/*Find if there is any invalid transition cell.*/
	bool FindTransitionCellData(FIntPoint CellIDIn, FLevel_TransitionCellData& TransitionDataOut) const;

	/*Called when the level is generated.*/
	void OnLevelGenerationEnd();
	
	/*Set the visibility of a cell for tick times.*/
	void SetCellVisibilityStatus(FIntPoint CellID, int32 VisibilityTickDuration = 1);

	/*Handles the cell visibility status track on tick..*/
	void ProcessAllCellVisibilityStatus();

	/*Called when cells changes visibility.*/
	void OnCellVisibilityChanged(const TArray<int>& CellDataIndex, bool IsVisible);

	/*Generates spawn track for actors.*/
	void GenerateSpawnActorsTrack();

	/*Generates a spawn track for actor data and adds it to the track.*/
	void GenerateInitialSpawnActorsTrack(int32 SpawnActorDataIndex);

	/*Generates and adds to track for single actor spawned.*/
	void GenerateTrackForSingleSpawnActor(int32 SpawnActorDataIndex, FIntPoint CellIDIn, const FLevel_ActorSpawnData& SpawnDataIn);

	/*Generates and adds to track for single actor spawned.*/
	void AddSpawnActorTrackToMap(const FLevel_SingleActorSpawnTrack& TrackIn);

	/*Get the track for a single actor.*/
	FLevel_SingleActorSpawnTrack GetSpawnActorTrack(int32 LevelPlacementDataIndexIn, int ActorTrackIndexIn, bool& SuccesOut) const;

	/*Spawns the actor of a track index.*/
	void SpawnActorForTrack(int32 LevelPlacementDataIndexIn, int ActorTrackIndexIn);
	
	/*Get the transform from the track. The level must be loaded for this to work.*/
	FTransform GetSpawnActorTransform(const FLevel_SingleActorSpawnTrack& TrackIn, bool& SuccesOut) const;

	/*Shows or hides the tracked spawned actor. If needed, it alsos spawns it for first time.*/
	void ChangeVisibilitySpawnedActor(int32 LevelPlacementDataIndexIn, int ActorTrackIndexIn, bool IsVisible);		
};
