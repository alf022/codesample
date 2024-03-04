#include "STerrainEditorViewport.h"
#include "SlateOptMacros.h"

#include "SCellsGrid.h"
#include "SCellsGridMesh.h"
#include "Widgets/SInvalidationPanel.h"

#include "Terrain/TerrainGeneratorSubsystem.h"
#include "Layout/TerrainLayoutSubsystem.h"
#include "Biomes/TerrainBiomeSubsystem.h"
#include "Mesh/TerrainMeshSubsystem.h"
#include "Layout/LayoutTypes.h"
#include "Biomes/TerrainBiomeLayoutData.h"
#include "Biomes/TerrainBiomeLayerData.h"
#include "Biomes/TerrainBiomeData.h"
#include "Layout/TerrainLayoutFunctionLibrary.h"

#include "GameplayTagContainer.h"
#include "Tags/TerrainTags.h"

#include "Delegates/DelegateSignatureImpl.inl"

#define LOCTEXT_NAMESPACE "STerrainEditorViewport"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void STerrainEditorViewport::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.ColorAndOpacity(FLinearColor::Gray)
		.Padding(2.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)			
			[			
				SAssignNew(InvalidationPanel, SInvalidationPanel)				
				[
					SAssignNew(CellsGridMesh, SCellsGridMesh)
					.LineThickness(0.f)										
				]			
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SAssignNew(InvalidationPanelMesh, SInvalidationPanel)
				[
					SAssignNew(CellsGrid, SCellsGrid)				
					.LineThickness(0.f)					
				]
			]
		]
	];

	InvalidationPanel.Get()->SetCanCache(true);
	InvalidationPanelMesh.Get()->SetCanCache(true);

	UTerrainLayoutSubsystem* TerrainLayoutSubsystem = GEngine->GetEngineSubsystem<UTerrainLayoutSubsystem>();
	if (!TerrainLayoutSubsystem)
	{
		return;
	}

	UTerrainBiomeSubsystem* TerrainBiomeSubsystem = GEngine->GetEngineSubsystem<UTerrainBiomeSubsystem>();
	if (!TerrainBiomeSubsystem)
	{
		return;
	}

	UTerrainMeshSubsystem* TerrainMeshSubsystem = GEngine->GetEngineSubsystem<UTerrainMeshSubsystem>();
	if (!TerrainMeshSubsystem)
	{
		return;
	}

	TerrainLayoutSubsystem->OnInitialLayoutGenerated.AddSP(this, &STerrainEditorViewport::DrawLayout);
	TerrainLayoutSubsystem->OnInitialLayoutMovementCompleted.AddSP(this, &STerrainEditorViewport::DrawLayout);
	TerrainLayoutSubsystem->OnCorridorLayoutGenerated.AddSP(this, &STerrainEditorViewport::DrawLayout);
	TerrainLayoutSubsystem->OnWallsLayoutGenerated.AddSP(this, &STerrainEditorViewport::DrawLayout);
	TerrainLayoutSubsystem->OnLayoutGenerated.AddSP(this, &STerrainEditorViewport::DrawLayout);
	
	TerrainBiomeSubsystem->OnBiomesLayoutGenerated.AddSP(this, &STerrainEditorViewport::DrawInitialBiomesLayout);
	TerrainBiomeSubsystem->OnBiomesLayoutMovementEnd.AddSP(this, &STerrainEditorViewport::DrawBiomesLayout);	
	TerrainBiomeSubsystem->OnBiomesLayersLayoutGenerated.AddSP(this, &STerrainEditorViewport::DrawBiomesLayersLayout);
	
	TerrainMeshSubsystem->OnVertexMapGenerated.AddSP(this, &STerrainEditorViewport::DrawVertexLayout);

	DrawLayout();
}

void STerrainEditorViewport::DrawLayout() const
{	
	UTerrainLayoutSubsystem* TerrainLayoutSubsystem = GEngine->GetEngineSubsystem<UTerrainLayoutSubsystem>();
	if (!TerrainLayoutSubsystem)
	{
		return;
	}

	TArray <FCellGridData> CellsDataGenerated;
	
	TArray <FIntPoint> CellsIDs = TArray <FIntPoint>();
	TerrainLayoutSubsystem->CellsLayoutMap.GetKeys(CellsIDs);
	const FIntPoint MinGridSize = UTerrainLayoutFunctionLibrary::GetMinGridSize(CellsIDs);
	
	for (const TPair<FIntPoint, FCellLayout>& pair : TerrainLayoutSubsystem->CellsLayoutMap)
	{				
		FCellGridData Cell = FCellGridData();
		Cell.GridID = pair.Key;

		Cell.GridID.X -= MinGridSize.X;
		Cell.GridID.Y -= MinGridSize.Y;

		if (pair.Value.Tags.HasTag(TAG_TERRAIN_CELL_LAYOUT_INITIAL) && bDrawInitialCell)
		{	
			Cell.Color = FLinearColor::Red;
		}
		else if (pair.Value.Tags.HasTag(TAG_TERRAIN_CELL_LAYOUT_CENTRAL) && bDrawCentralCell)
		{
			Cell.Color = FLinearColor(1.f, 0.2f, 0.f);
		}
		else if (pair.Value.RoomID == TerrainLayoutSubsystem->GetInitialRoom() && pair.Value.Tags.HasTag(TAG_TERRAIN_CELL_TYPE_ROOM) && bDrawInitialRoom)
		{
			Cell.Color = FLinearColor(.0f, 5.f, 0.f);
		}	
		else if (pair.Value.Tags.HasTag(TAG_TERRAIN_CELL_LAYOUT_BORDER) && bDrawRoomBorders)
		{
			Cell.Color = FLinearColor(.5f, 5.f, 0.f);
		}
		else if (pair.Value.Tags.HasTag(TAG_TERRAIN_CELL_LAYOUT_BORDERCOLLISION) && bDrawRoomCollision)
		{
			Cell.Color = FLinearColor::Yellow;
		}
		else if (pair.Value.Tags.HasTag(TAG_TERRAIN_CELL_LAYOUT_DOOR) && bDrawDoors)
		{
			Cell.Color = FLinearColor(0.f, 0.f, .1f);
		}
		else if (pair.Value.Tags.HasTag(TAG_TERRAIN_CELL_TYPE_CORRIDOR))
		{
			if (bDrawCorridors)
			{				
				Cell.Color = FLinearColor(0.15f, 0.15f, .15f);
			}
			else
			{
				continue;
			}			
		}	
		else if(pair.Value.Tags.HasTag(TAG_TERRAIN_CELL_TYPE_WALL))
		{
			if (bDrawWalls)
			{
				Cell.Color = FLinearColor(0.001f, 0.001f, .001f);
				
			}
			else
			{
				continue;
			}		
		}
		else if (bDrawCells)
		{
			Cell.Color = FLinearColor(0.5f, .5f, .5f);
		}

		CellsDataGenerated.Add(Cell);				
	}

	DrawViewport(CellsDataGenerated);
}

void STerrainEditorViewport::DrawInitialBiomesLayout() const
{
	UTerrainBiomeSubsystem* TerrainBiomeSubsystem = GEngine->GetEngineSubsystem<UTerrainBiomeSubsystem>();
	if (!TerrainBiomeSubsystem)
	{
		return;
	}

	//Cells of layouts generated	
	UTerrainBiomeLayoutData* BiomeData = TerrainBiomeSubsystem->GetBiomeLayoutData();
	TArray <FCellGridData> CellsDataGenerated;
	TArray <FIntPoint> CellsIDs = TArray <FIntPoint>();

	for (const TPair<FIntPoint, FBiomeLayout>& pair : TerrainBiomeSubsystem->BiomesLayoutMap)
	{
		FCellGridData CellGrid = FCellGridData();		
	
		if (BiomeData->Biomes.IsValidIndex(pair.Value.BiomeIndex))
		{
			CellGrid.Color = BiomeData->Biomes[pair.Value.BiomeIndex]->BiomeColor;
		}
		else
		{
			CellGrid.Color = FLinearColor::White;
		}

		for (const FIntPoint& cell : pair.Value.BiomeCells)
		{			
			CellGrid.GridID = cell;	
			CellsIDs.Add(cell);
			CellsDataGenerated.Add(CellGrid);
		}
	}

	//Cells that did not get into any layout
	for (const TPair<FIntPoint, FBiomeCellLayout>& pair : TerrainBiomeSubsystem->CellsBiomeLayoutMap)
	{
		if (!CellsIDs.Contains(pair.Key))
		{
			FCellGridData CellGrid = FCellGridData();
			CellGrid.GridID = pair.Key;
			CellGrid.Color = FLinearColor::White;

			CellsIDs.Add(CellGrid.GridID);
			CellsDataGenerated.Add(CellGrid);
		}
	}
	
	//Fix the offset
	const FIntPoint MinGridSize = UTerrainLayoutFunctionLibrary::GetMinGridSize(CellsIDs);

	for (FCellGridData& Cell : CellsDataGenerated)
	{
		Cell.GridID.X -= MinGridSize.X;
		Cell.GridID.Y -= MinGridSize.Y;
	}

	DrawViewport(CellsDataGenerated);
}

void STerrainEditorViewport::DrawBiomesLayout() const
{
	UTerrainBiomeSubsystem* TerrainBiomeSubsystem = GEngine->GetEngineSubsystem<UTerrainBiomeSubsystem>();
	if (!TerrainBiomeSubsystem)
	{
		return;
	}

	TArray <FCellGridData> CellsDataGenerated;
	
	TArray <FIntPoint> CellsIDs = TArray <FIntPoint>();
	TerrainBiomeSubsystem->CellsBiomeLayoutMap.GetKeys(CellsIDs);
	const FIntPoint MinGridSize = UTerrainLayoutFunctionLibrary::GetMinGridSize(CellsIDs);

	UTerrainBiomeLayoutData* BiomeData = TerrainBiomeSubsystem->GetBiomeLayoutData();

	for (const TPair<FIntPoint, FBiomeCellLayout>& pair : TerrainBiomeSubsystem->CellsBiomeLayoutMap)
	{
		FCellGridData Cell = FCellGridData();
		Cell.GridID = pair.Key;

		Cell.GridID.X -= MinGridSize.X;
		Cell.GridID.Y -= MinGridSize.Y;
				
		if (BiomeData->Biomes.IsValidIndex(pair.Value.BiomeIndex))
		{
			Cell.Color = BiomeData->Biomes[pair.Value.BiomeIndex]->BiomeColor;
		}
		else
		{
			Cell.Color = FLinearColor::White;
		}

		CellsDataGenerated.Add(Cell);
	}

	DrawViewport(CellsDataGenerated);
}

void STerrainEditorViewport::DrawBiomesLayersLayout() const
{
	UTerrainBiomeSubsystem* TerrainBiomeSubsystem = GEngine->GetEngineSubsystem<UTerrainBiomeSubsystem>();
	if (!TerrainBiomeSubsystem)
	{
		return;
	}

	TArray <FCellGridData> CellsDataGenerated;	

	TArray <FIntPoint> CellsIDs = TArray <FIntPoint>();
	TerrainBiomeSubsystem->CellsBiomeLayoutMap.GetKeys(CellsIDs);
	const FIntPoint MinGridSize = UTerrainLayoutFunctionLibrary::GetMinGridSize(CellsIDs);
			
	for (const TPair<FIntPoint, FBiomeCellLayout>& pair : TerrainBiomeSubsystem->CellsBiomeLayoutMap)
	{
		FCellGridData Cell = FCellGridData();
		Cell.GridID = pair.Key;

		Cell.GridID.X -= MinGridSize.X;
		Cell.GridID.Y -= MinGridSize.Y;

		if (TerrainBiomeSubsystem->GetCellLayerData(pair.Key))
		{
			Cell.Color = TerrainBiomeSubsystem->GetCellLayerData(pair.Key)->LayerColor;
		}
		else
		{
			Cell.Color = FLinearColor::White;
		}

		CellsDataGenerated.Add(Cell);
	}

	DrawViewport(CellsDataGenerated);
}

void STerrainEditorViewport::DrawVertexLayout() const
{
	UTerrainMeshSubsystem* TerrainMeshSubsystem = GEngine->GetEngineSubsystem<UTerrainMeshSubsystem>();
	if (!TerrainMeshSubsystem)
	{
		return;
	}

	TArray <FCellGridData> CellsDataGenerated;

	TArray <FIntPoint> CellsIDs = TArray <FIntPoint>();
	TerrainMeshSubsystem->VertexMap.GetKeys(CellsIDs);
	const FIntPoint MinGridSize = UTerrainLayoutFunctionLibrary::GetMinGridSize(CellsIDs);

	for (const TPair<FIntPoint, FTerrainVertex>& pair : TerrainMeshSubsystem->VertexMap)
	{
		FCellGridData Cell = FCellGridData();
		Cell.GridID = pair.Key;

		Cell.GridID.X -= MinGridSize.X;
		Cell.GridID.Y -= MinGridSize.Y;

		if (TerrainMeshSubsystem->GetVertexLayerData(pair.Key))
		{
			Cell.Color = TerrainMeshSubsystem->GetVertexLayerData(pair.Key)->LayerColor;
		}
		else
		{
			Cell.Color = FLinearColor::White;
		}

		CellsDataGenerated.Add(Cell);
	}

	//Cannot use grid for this, too many lines it fails.
	CellsGridMesh->SetCellsData(CellsDataGenerated);
	CellsGridMesh.Get()->SetVisibility(EVisibility::SelfHitTestInvisible);
	CellsGrid.Get()->SetVisibility(EVisibility::Collapsed);
}

void STerrainEditorViewport::DrawViewport(const TArray<FCellGridData>& Data) const
{
	if (bUseGrid)
	{
		CellsGrid->SetCellsData(Data);
		CellsGrid.Get()->SetVisibility(EVisibility::SelfHitTestInvisible);
	}
	else
	{
		CellsGrid.Get()->SetVisibility(EVisibility::Collapsed);
	}

	if (bUseGridMesh)
	{
		CellsGridMesh->SetCellsData(Data);
		CellsGridMesh.Get()->SetVisibility(EVisibility::SelfHitTestInvisible);
	}
	else
	{
		CellsGridMesh.Get()->SetVisibility(EVisibility::Collapsed);
	}
}

void STerrainEditorViewport::SetDrawConfiguration(bool DrawCentralCell, bool DrawInitialCell, bool DrawInitialRoom, bool DrawRoomBorders, bool DrawRoomCollision, bool DrawCorridors, bool DrawWalls, bool DrawDoors, bool DrawCells)
{
	bDrawCentralCell = DrawCentralCell;
	bDrawInitialCell = DrawInitialCell;
	bDrawInitialRoom = DrawInitialRoom;
	bDrawRoomBorders = DrawRoomBorders;
	bDrawRoomCollision = DrawRoomCollision;
	bDrawCorridors = DrawCorridors;
	bDrawWalls = DrawWalls;
	bDrawDoors = DrawDoors;
	bDrawCells = DrawCells;

	DrawLayout();
}

void STerrainEditorViewport::SetUseGrid(bool IsChecked)
{
	bUseGrid = IsChecked;
	DrawLayout();
}

void STerrainEditorViewport::SetUseGridMesh(bool IsChecked)
{
	bUseGridMesh = IsChecked;
	DrawLayout();
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

#undef LOCTEXT_NAMESPACE