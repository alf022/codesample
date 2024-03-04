#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SCellsGrid;
class SCellsGridMesh;
class SInvalidationPanel;
class UTerrainGeneratorSubsystem;

struct FCellGridData;

class STerrainEditorViewport : public SCompoundWidget
{		
public:
	SLATE_BEGIN_ARGS(STerrainEditorViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	
	void DrawLayout() const;

	void SetDrawConfiguration(
		bool DrawInitialCell = true,
		bool DrawCentralCell = true,
		bool DrawInitialRoom = true,
		bool DrawRoomBorders = true,
		bool DrawRoomCollision = true,
		bool DrawCorridors = true,
		bool DrawWalls = true,
		bool DrawDoors = true,
		bool DrawCells = true);

	void SetUseGrid(bool IsChecked);
	void SetUseGridMesh(bool IsChecked);

protected:
	bool bUseGridMesh = true;
	bool bUseGrid = true;

	bool bDrawCentralCell = true;
	bool bDrawInitialCell = false;
	bool bDrawInitialRoom = true;
	bool bDrawRoomBorders = false;
	bool bDrawRoomCollision = false;
	bool bDrawCorridors = true;
	bool bDrawWalls = true;
	bool bDrawDoors = false;
	bool bDrawCells = true;

	TSharedPtr<SCellsGrid> CellsGrid;
	TSharedPtr<SCellsGridMesh> CellsGridMesh;
	TSharedPtr<SInvalidationPanel> InvalidationPanel;
	TSharedPtr<SInvalidationPanel> InvalidationPanelMesh;

	UTerrainGeneratorSubsystem* TerrainGeneratorSubsystem = nullptr;

	void DrawInitialBiomesLayout() const;
	void DrawBiomesLayout() const;

	void DrawBiomesLayersLayout() const;

	void DrawVertexLayout() const;

	void DrawViewport(const TArray<FCellGridData>& Data) const;
};