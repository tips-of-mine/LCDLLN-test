#pragma once

#include "src/world_editor/WorldMapEditDocument.h"
#include "src/world_editor/TreeSpeciesCatalog.h"

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
	/// Etat editeur (carte, chemins, brosses, grille) - logique sans ImGui.
	class WorldEditorSession final
	{
	public:
		WorldEditorSession();

		WorldMapEditDocument& MutableDoc() { return m_doc; }
		const WorldMapEditDocument& Doc() const { return m_doc; }

		std::string& Status() { return m_status; }
		const std::string& Status() const { return m_status; }

		void SetStatus(std::string_view message);

		/// Fichier JSON d'edition absolu (vide si jamais sauvegarde).
		const std::string& EditFileAbsolutePath() const { return m_editJsonAbsolutePath; }
		void SetEditFileAbsolutePath(std::string path) { m_editJsonAbsolutePath = std::move(path); }

		// Tampons pour l'UI (ImGui InputText)
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
		/// Selection liste instances (-1 = aucune) : prochain clic pose une nouvelle instance, sinon deplace la selectionnee.
		int& SelectedLayoutInstanceIndex() { return m_selectedLayoutInstance; }

		/// Mode herbe : si vrai, la brosse retire le masque au lieu de l'ajouter.
		bool& GrassMaskEraseBrush() { return m_grassMaskEraseBrush; }

		/// Brouillon polyligne route (011) : points monde XZ (clics terrain).
		std::vector<std::pair<double, double>>& RouteDraftPoints() { return m_routeDraftXz; }
		const std::vector<std::pair<double, double>>& RouteDraftPoints() const { return m_routeDraftXz; }
		void ClearRouteDraft();
		/// Ajoute un point (deja clampe cote moteur aux limites du terrain).
		void AddRouteDraftPoint(double worldX, double worldZ);
		/// Largeur bande route (m) et couche splat cible pour l'application sur le SLAP.
		float& RouteStripWidthM() { return m_routeStripWidthM; }
		int& RouteSplatLayer() { return m_routeSplatLayer; }

		/// UI " Appliquer sur splat " : traite par l'Engine (peinture + flush + doc.routes).
		void RequestApplyRouteDraftToSplat();
		[[nodiscard]] bool ConsumeRouteApplyDraftRequest();

		/// Place une nouvelle instance ou deplace celle selectionnee (coords monde, Y au sol).
		void PlaceOrMoveLayoutInstanceAtTerrainHit(const engine::core::Config& cfg, double worldX, double worldY, double worldZ);
		void RemoveLayoutInstance(size_t index);

		/// Appele avant l'ecriture du JSON d'edition pour persister heightmap / splat sur disque (Vulkan).
		void SetTerrainSaveHook(std::function<bool(const engine::core::Config&, const WorldMapEditDocument&)> hook);

		/// Cree une carte plate sous \c world_editor/maps/<id>/ (content).
		bool ActionNewMap(const engine::core::Config& cfg);

		bool ActionSaveEditJson(const engine::core::Config& cfg);
		bool ActionLoadEditJson(const engine::core::Config& cfg);
		bool ActionExportRuntime(const engine::core::Config& cfg);
		bool ActionImportTexture(const engine::core::Config& cfg);
		bool ActionImportAudio(const engine::core::Config& cfg);

		/// Sauvegarde dans le chemin canonique \c world_editor/maps/<zone_id>/map.lcdlln_edit.json
		/// (deduit de \c m_doc.zoneId - pas besoin de saisir un chemin).
		bool ActionSaveCurrentMap(const engine::core::Config& cfg);

		/// Charge \c world_editor/maps/<zoneId>/map.lcdlln_edit.json (chemin canonique).
		bool ActionLoadMapByZoneId(const engine::core::Config& cfg, std::string_view zoneId);

		/// Sous-repertoire du content ou vivent toutes les cartes de l'editeur.
		static constexpr const char* kMapsContentRelativeDir = "world_editor/maps";
		/// Nom de fichier canonique du JSON d'edition d'une carte.
		static constexpr const char* kEditDocFilename = "map.lcdlln_edit.json";

		/// Chemin canonique absolu de la carte \p zoneId (inexistant != erreur ; aucune E/S ici).
		static std::filesystem::path CanonicalMapJsonPath(const engine::core::Config& cfg, std::string_view zoneId);

		/// Re-scan de \c world_editor/maps/ : remplit \ref AvailableMapIds() (zoneIds tries alphabetiquement).
		void RefreshAvailableMaps(const engine::core::Config& cfg);
		const std::vector<std::string>& AvailableMapIds() const { return m_availableMapIds; }
		int& SelectedAvailableMapIndex() { return m_selectedAvailableMapIndex; }

		void SyncBuffersFromDoc();
		void SyncDocIdFromBuffer();

		/// Demande un rechargement du terrain GPU (apres nouvelle carte / chargement JSON).
		void RequestTerrainGpuReload();
		/// \return true une fois par demande consommee (pour l'Engine).
		bool ConsumeTerrainGpuReloadRequest();

		/// Marque les references de textures de layer (splatLayerTextureRefs)
		/// comme modifiees. A appeler par les UIs apres avoir change un element
		/// du tableau (combo Peindre, vignette Bibliotheque). Engine::ProcessSplatRefsDirty
		/// consomme ce flag chaque frame pour reuploader le splat array GPU.
		void MarkSplatRefsDirty() { m_splatRefsDirty = true; }

		/// Lit + reset le flag splat refs. Utilise par Engine cote frame loop.
		bool ConsumeSplatRefsDirty() { const bool d = m_splatRefsDirty; m_splatRefsDirty = false; return d; }

		/// Liste des .texr (chemins content-relatifs) reimportees depuis la
		/// derniere consommation. Engine consomme via ConsumeRecentlyImportedTextures
		/// et appelle TexturePreviewCache::Invalidate sur chaque entree.
		const std::vector<std::string>& RecentlyImportedTextures() const { return m_recentlyImported; }

		/// Vide la file (a appeler apres avoir invalide les vignettes correspondantes).
		void ClearRecentlyImportedTextures() { m_recentlyImported.clear(); }

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
		// PR25 (M??.?) : valeur d'origine 8 m -> 5 m. Demande utilisateur pour
		// avoir une maille plus fine (repere visuel au sol). La cellule reste
		// modifiable a la volee dans l'UI (panneau "Affichage & grille").
		float m_gridCellMeters = 5.f;
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
		bool m_splatRefsDirty = false;

		std::vector<std::string> m_recentlyImported;

		std::vector<std::string> m_availableMapIds;
		int m_selectedAvailableMapIndex = 0;
		bool m_availableMapsScanned = false;

	public:
		bool AvailableMapsScanned() const { return m_availableMapsScanned; }
	};

} // namespace engine::editor
