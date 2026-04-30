#pragma once

#include "engine/editor/WorldMapEditDocument.h"
#include "engine/editor/TreeSpeciesCatalog.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::core
{
	class Config;
}

namespace engine::editor
{
	/// État éditeur (carte, chemins, brosses, grille) — logique sans ImGui.
	class WorldEditorSession final
	{
	public:
		WorldEditorSession();

		WorldMapEditDocument& MutableDoc() { return m_doc; }
		const WorldMapEditDocument& Doc() const { return m_doc; }

		std::string& Status() { return m_status; }
		const std::string& Status() const { return m_status; }

		void SetStatus(std::string_view message);

		/// Fichier JSON d’édition absolu (vide si jamais sauvegardé).
		const std::string& EditFileAbsolutePath() const { return m_editJsonAbsolutePath; }
		void SetEditFileAbsolutePath(std::string path) { m_editJsonAbsolutePath = std::move(path); }

		// Tampons pour l’UI (ImGui InputText)
		std::array<char, 128>& BufZoneId() { return m_bufZoneId; }
		std::array<char, 32>& BufSize() { return m_bufSize; }
		std::array<char, 32>& BufSeed() { return m_bufSeed; }
		std::array<char, 512>& BufLoadPath() { return m_bufLoadPath; }
		std::array<char, 512>& BufSavePath() { return m_bufSavePath; }
		std::array<char, 512>& BufPngPath() { return m_bufPngPath; }
		std::array<char, 160>& BufTexrName() { return m_bufTexrName; }
		std::array<char, 512>& BufAudioSrc() { return m_bufAudioSrc; }
		std::array<char, 256>& BufAudioDest() { return m_bufAudioDest; }

		bool& ShowGrid() { return m_showGrid; }
		float& GridCellMeters() { return m_gridCellMeters; }
		float& BrushRadius() { return m_brushRadius; }
		float& BrushStrength() { return m_brushStrength; }
		int& BrushOp() { return m_brushOp; } // 0 raise, 1 lower, 2 smooth, 3 flatten
		/// 0 = sculpt heightmap, 1 = splat, 2 = masque herbe (GRMS), 3 = placement instances, 4 = routes splat (011).
		int& TerrainEditMode() { return m_terrainEditMode; }
		/// Couche splat active [0,3] (herbe, terre, roc, neige).
		int& SplatLayer() { return m_splatLayer; }
		/// 0 = arbre (catalogue **013**), 1 = rocher legacy (`zones/zone_1/zone_1.gltf`).
		int& InstancePlacementKind() { return m_instancePlacementKind; }
		int& TreeSpeciesUiIndex() { return m_treeSpeciesUiIndex; }
		int& TreeShapeVariantUiIndex() { return m_treeShapeVariantUiIndex; }
		float& TreeScaleT01() { return m_treeScaleT01; }
		bool& TreeRandomizeScaleOnPlace() { return m_treeRandomizeScaleOnPlace; }
		[[nodiscard]] const TreeSpeciesCatalog& TreeCatalog() const { return m_treeCatalog; }
		/// Charge une fois `world_editor/tree_species_catalog.json` et resynchronise les instances (clamp).
		void EnsureTreeCatalogLoaded(const engine::core::Config& cfg);
		/// Sélection liste instances (−1 = aucune) : prochain clic pose une nouvelle instance, sinon déplace la sélectionnée.
		int& SelectedLayoutInstanceIndex() { return m_selectedLayoutInstance; }

		/// Mode herbe : si vrai, la brosse retire le masque au lieu de l’ajouter.
		bool& GrassMaskEraseBrush() { return m_grassMaskEraseBrush; }

		/// Brouillon polyligne route (011) : points monde XZ (clics terrain).
		std::vector<std::pair<double, double>>& RouteDraftPoints() { return m_routeDraftXz; }
		const std::vector<std::pair<double, double>>& RouteDraftPoints() const { return m_routeDraftXz; }
		void ClearRouteDraft();
		/// Ajoute un point (déjà clampé côté moteur aux limites du terrain).
		void AddRouteDraftPoint(double worldX, double worldZ);
		/// Largeur bande route (m) et couche splat cible pour l’application sur le SLAP.
		float& RouteStripWidthM() { return m_routeStripWidthM; }
		int& RouteSplatLayer() { return m_routeSplatLayer; }

		/// UI « Appliquer sur splat » : traité par l’Engine (peinture + flush + doc.routes).
		void RequestApplyRouteDraftToSplat();
		[[nodiscard]] bool ConsumeRouteApplyDraftRequest();

		/// Place une nouvelle instance ou déplace celle sélectionnée (coords monde, Y au sol).
		void PlaceOrMoveLayoutInstanceAtTerrainHit(const engine::core::Config& cfg, double worldX, double worldY, double worldZ);
		void RemoveLayoutInstance(size_t index);

		/// Appelé avant l’écriture du JSON d’édition pour persister heightmap / splat sur disque (Vulkan).
		void SetTerrainSaveHook(std::function<bool(const engine::core::Config&, const WorldMapEditDocument&)> hook);

		/// Crée une carte plate sous \c world_editor/maps/<id>/ (content).
		bool ActionNewMap(const engine::core::Config& cfg);

		bool ActionSaveEditJson(const engine::core::Config& cfg);
		bool ActionLoadEditJson(const engine::core::Config& cfg);
		bool ActionExportRuntime(const engine::core::Config& cfg);
		bool ActionImportTexture(const engine::core::Config& cfg);
		bool ActionImportAudio(const engine::core::Config& cfg);

		/// Sauvegarde dans le chemin canonique \c world_editor/maps/<zone_id>/map.lcdlln_edit.json
		/// (déduit de \c m_doc.zoneId — pas besoin de saisir un chemin).
		bool ActionSaveCurrentMap(const engine::core::Config& cfg);

		/// Charge \c world_editor/maps/<zoneId>/map.lcdlln_edit.json (chemin canonique).
		bool ActionLoadMapByZoneId(const engine::core::Config& cfg, std::string_view zoneId);

		/// Sous-répertoire du content où vivent toutes les cartes de l'éditeur.
		static constexpr const char* kMapsContentRelativeDir = "world_editor/maps";
		/// Nom de fichier canonique du JSON d'édition d'une carte.
		static constexpr const char* kEditDocFilename = "map.lcdlln_edit.json";

		/// Chemin canonique absolu de la carte \p zoneId (inexistant ≠ erreur ; aucune E/S ici).
		static std::filesystem::path CanonicalMapJsonPath(const engine::core::Config& cfg, std::string_view zoneId);

		/// Re-scan de \c world_editor/maps/ : remplit \ref AvailableMapIds() (zoneIds triés alphabétiquement).
		void RefreshAvailableMaps(const engine::core::Config& cfg);
		const std::vector<std::string>& AvailableMapIds() const { return m_availableMapIds; }
		int& SelectedAvailableMapIndex() { return m_selectedAvailableMapIndex; }

		void SyncBuffersFromDoc();
		void SyncDocIdFromBuffer();

		/// Demande un rechargement du terrain GPU (après nouvelle carte / chargement JSON).
		void RequestTerrainGpuReload();
		/// \return true une fois par demande consommée (pour l’Engine).
		bool ConsumeTerrainGpuReloadRequest();

	private:
		void SanitizeAllLayoutInstancesAgainstTreeCatalog();

		WorldMapEditDocument m_doc;
		std::string m_editJsonAbsolutePath;
		std::string m_status;

		std::array<char, 128> m_bufZoneId{};
		std::array<char, 32> m_bufSize{};
		std::array<char, 32> m_bufSeed{};
		std::array<char, 512> m_bufLoadPath{};
		std::array<char, 512> m_bufSavePath{};
		std::array<char, 512> m_bufPngPath{};
		std::array<char, 160> m_bufTexrName{};
		std::array<char, 512> m_bufAudioSrc{};
		std::array<char, 256> m_bufAudioDest{};

		bool m_showGrid = true;
		float m_gridCellMeters = 8.f;
		float m_brushRadius = 10.f;
		float m_brushStrength = 0.1f;
		int m_brushOp = 0;
		int m_terrainEditMode = 0;
		int m_splatLayer = 0;
		int m_instancePlacementKind = 0;
		int m_treeSpeciesUiIndex = 0;
		int m_treeShapeVariantUiIndex = 0;
		float m_treeScaleT01 = 0.5f;
		bool m_treeRandomizeScaleOnPlace = false;
		TreeSpeciesCatalog m_treeCatalog;
		bool m_treeCatalogLoadAttempted = false;
		std::mt19937 m_treeRng;

		int m_selectedLayoutInstance = -1;
		bool m_grassMaskEraseBrush = false;

		std::vector<std::pair<double, double>> m_routeDraftXz;
		bool   m_routeApplyDraftRequested = false;
		float  m_routeStripWidthM         = 4.f;
		int    m_routeSplatLayer          = 1;

		std::function<bool(const engine::core::Config&, const WorldMapEditDocument&)> m_terrainSaveHook;

		bool m_terrainGpuReloadRequested = false;

		std::vector<std::string> m_availableMapIds;
		int m_selectedAvailableMapIndex = 0;
		bool m_availableMapsScanned = false;

	public:
		bool AvailableMapsScanned() const { return m_availableMapsScanned; }
	};

} // namespace engine::editor
