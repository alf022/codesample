// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Core/TerrainBaseSubsystem.h"
#include "LayoutTypes.h"
#include "Layout/CorridorTypes.h"
#include "Terrain/TerrainGeneratorTypes.h"
#include "TerrainLayoutSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE(FNoParamsDelegateLayoutSubsystemSignature);

class UTerrainData;
class UTerrainLayoutData;
class UTerrainGeneratorSubsystem;

UCLASS()
class TERRAINGENERATOR_API UTerrainLayoutSubsystem : public UTerrainBaseSubsystem
{
	GENERATED_BODY()

public:
	friend class STerrainEditorViewport;

	friend class FRoomLayoutWorker;
	friend class FRoomMovementWorker;
	friend class FCorridorLayoutWorker;
	friend class FWallLayoutWorker;
	friend class FBiomeMovementWorker;

	friend class UTerrainBiomeSubsystem;


	FNoParamsDelegateLayoutSubsystemSignature OnInitialLayoutGenerated;
	FNoParamsDelegateLayoutSubsystemSignature OnInitialLayoutMovementCompleted;
	FNoParamsDelegateLayoutSubsystemSignature OnCorridorLayoutGenerated;
	FNoParamsDelegateLayoutSubsystemSignature OnWallsLayoutGenerated;
	FNoParamsDelegateLayoutSubsystemSignature OnLayoutGenerated;

	FIntPoint GetInitialRoom() const;
	TMap <FIntPoint, FRoomLayout> GetRoomsLayoutMap() const;
	TMap <FIntPoint, FCellLayout> GetCellsLayoutMap() const;

	FVector GetCellWorldPosition(const FIntPoint& InCellsID, FVector2D InAnchor) const;
	float GetWorldDistanceBetweenRooms(const FIntPoint& RoomA, const FIntPoint& RoomB) const;
	
	void GenerateTerrainLayout(UTerrainData* InTerrainData);

protected:
	UPROPERTY(Transient)
	UTerrainLayoutData* TerrainLayoutData;

	/*The initial room selected for the current layout.*/
	UPROPERTY(Transient)
	FIntPoint InitialRoom;

	UPROPERTY(Transient)
	TMap <FIntPoint, FRoomLayout> RoomsLayoutMap;

	UPROPERTY(Transient)
	TMap <FIntPoint, FCellLayout> CellsLayoutMap;

	void GenerateInitialRoomsLayout();	
	void StartRoomLayoutGeneration();

	UFUNCTION()
	void OnRoomLayoutEnd();

	void StartRoomMovement();
	TArray<FIntPointPair> GetRoomsCentralCells(TArray<FIntPoint>& CentralCellsOut) const; 
	TArray<FIntPoint> GetRoomInitialCollisionCells(FIntPoint RoomID) const;

	UFUNCTION()
	void OnRoomMovementEnd();

	void StartCorridorsLayoutGeneration();
	TArray<FTerrain_RoomDistance> GenerateInitialCorridorsLayoutData();
	TArray<FTerrain_RoomDistance> GetAllRoomDistanceData() const;
	TArray<FTerrain_RoomDistance> GetRoomDistancesInNearArea(const FIntPoint& StartRoom) const;

	void AddUnusedCorridors(TArray<FTerrain_RoomDistance>& UnusedCorridorsIn, TArray<FTerrain_RoomDistance>& ThreeCorridorsOut);
	
	UFUNCTION()
	void OnCorridorLayoutEnd();

	void StartWallsLayoutGeneration();

	UFUNCTION()
	void OnWallsLayoutEnd();

	void OnLayoutGenerationEnd();

	void CalculateRoomsDungeonDepth();
	void CalculateCellsLayoutDepth();
};
