#pragma once

namespace engine::editor::world
{
	class TerrainDocument;
	class WaterDocument;
	namespace volumes
	{
		class MeshInsertDocument;
		namespace dungeons { class DungeonPortalDocument; }
	}
}

namespace engine::editor::world::zone_presets
{
	/// Vide intégralement les 4 documents éditeur d'une zone (M100.46
	/// §D.3). Utilisé par `ZonePresetExecutor` avant l'exécution d'un
	/// preset sur une zone non vide.
	///
	/// Effet : `Reset()` est appelé sur chacun des documents. Les chunks
	/// terrain reviennent à plat à 0 m via lazy load, la scène water est
	/// vidée (lakes/rivers/ocean défaut), les mesh inserts et portails
	/// sont supprimés.
	///
	/// Note MVP : ce helper ne pousse PAS de `ResetZoneCommand` sur le
	/// CommandStack (spec §D.3 le prévoit pour Ctrl+Z après preset).
	/// Le reset reste destructif en RAM, MAIS depuis le P0 audit 6.1 le
	/// point d'appel (`ZonePresetDialog::RunSelectedPreset`) écrit un filet
	/// de sécurité disque AVANT `Execute` et restaure la carte si le preset
	/// échoue ou est annulé — plus de perte irréversible.
	void ResetEditedZoneDocuments(
		engine::editor::world::TerrainDocument& terrain,
		engine::editor::world::WaterDocument& water,
		engine::editor::world::volumes::MeshInsertDocument& meshInserts,
		engine::editor::world::volumes::dungeons::DungeonPortalDocument& dungeonPortals);
}
