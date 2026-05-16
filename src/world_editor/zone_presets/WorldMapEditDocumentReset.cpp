#include "src/world_editor/zone_presets/WorldMapEditDocumentReset.h"

#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/world_editor/volumes/MeshInsertDocument.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalDocument.h"
#include "src/world_editor/water/WaterDocument.h"

namespace engine::editor::world::zone_presets
{
	void ResetEditedZoneDocuments(
		engine::editor::world::TerrainDocument& terrain,
		engine::editor::world::WaterDocument& water,
		engine::editor::world::volumes::MeshInsertDocument& meshInserts,
		engine::editor::world::volumes::dungeons::DungeonPortalDocument& dungeonPortals)
	{
		terrain.Reset();
		water.Reset();
		meshInserts.Reset();
		dungeonPortals.Reset();
	}
}
