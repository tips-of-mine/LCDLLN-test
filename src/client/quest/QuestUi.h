#pragma once

#include "src/client/quest/QuestPoiTable.h"
#include "src/client/ui_common/UIModel.h"
#include "src/shared/core/Config.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::client
{
	/// Convertit une position monde (plan XZ) en coordonnées UV normalisées du
	/// radar minimap, centré sur le joueur (SP3). `outU`/`outV` = 0.5 au centre
	/// (position du joueur), croissant vers +x/+z, échelle `radiusM` mètres du
	/// centre au bord du radar. `outOffRadar` = vrai si le POI est hors du
	/// disque de rayon `radiusM` (auquel cas `outU`/`outV` sont clampés dans
	/// [0,1], donc ramenés sur le bord du cadre carré). Garde `radiusM <= 0` :
	/// pas de division par zéro, sort au centre avec `outOffRadar=true`.
	/// Fonction pure (aucun état, aucune dépendance ImGui) — exposée en dehors
	/// de tout namespace anonyme pour être testable directement.
	void WorldToRadarUv(float px, float pz, float playerX, float playerZ, float radiusM,
		float& outU, float& outV, bool& outOffRadar);

	/// Vrai si une quête de statut \p status doit apparaître dans le JOURNAL du
	/// joueur : uniquement les quêtes acceptées et en cours — `Active` (2) et
	/// `ReadyToTurnIn` (3). Les quêtes seulement proposées par un PNJ (`Offered` = 1)
	/// ou verrouillées (`Locked` = 0), et les quêtes déjà rendues (`Completed` = 4),
	/// n'y figurent PAS : le joueur doit d'abord accepter la quête chez le PNJ (le
	/// journal reflète ce qu'on a accepté, pas ce qui est simplement disponible).
	/// \p status = statut wire système B copié par `UIModelBinding::ApplyQuestDelta`
	/// (cf. `QuestStatus` dans `QuestRuntime.h`). Fonction pure, testable directement.
	bool ShouldShowQuestInJournal(uint8_t status);

	// --- Contrôle de zoom du radar minimap (cf. spec 2026-07-08) ---

	/// Nombre de crans de zoom du radar.
	inline constexpr int kMinimapZoomLevelCount = 5;
	/// Rayons affichés par cran (mètres, centre joueur -> bord). Index 0 = le plus
	/// zoomé (200 m), index 4 = le plus large (1000 m).
	inline constexpr float kMinimapZoomLevelsM[kMinimapZoomLevelCount] =
		{ 200.0f, 400.0f, 600.0f, 800.0f, 1000.0f };
	/// Cran par défaut (600 m).
	inline constexpr int kMinimapZoomDefaultIndex = 2;

	/// Borne un index de cran dans [0, kMinimapZoomLevelCount-1]. Pure/testable.
	int ClampZoomIndex(int index);

	/// Applique une rotation de molette à un index de cran. \p wheelDelta > 0
	/// (molette vers le haut) = zoom IN (rayon plus petit, index décroît), comme
	/// le zoom caméra. Retourne l'index borné. Pure/testable.
	int StepZoomIndex(int index, int wheelDelta);

	/// Rayon affiché (mètres) pour un index de cran (borné). Pure/testable.
	float RadiusForZoomIndex(int index);

	/// Géométrie écran du radar minimap (coin haut-gauche + taille du cadre carré,
	/// pixels). \c enabled reflète `client.quest.minimap.enabled`. Partagée entre
	/// le rendu (arc/repères/POI) et le hit-test souris (Engine) pour éviter toute
	/// dérive de géométrie.
	struct RadarScreenRect
	{
		bool  enabled = false;
		float x0 = 0.0f;
		float y0 = 0.0f;
		float size = 0.0f;
	};

	/// Calcule la géométrie écran du radar depuis la config et le viewport.
	/// Ancrage : coin haut-droit, sous le bandeau météo (cf. RenderMinimap). Pure
	/// (aucune dépendance ImGui), testable. \c enabled = false (et size = 0) si le
	/// radar est désactivé ou `size_px <= 0`.
	RadarScreenRect ComputeRadarScreenRect(const engine::core::Config& cfg,
		float displayW, float displayH);

	/// Point écran (pixels).
	struct ScreenPoint
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	/// Position écran du repère de zoom d'index \p tickIndex (0..4) sur l'arc de la
	/// moitié haute du radar. Les 5 repères s'étalent de 150° (haut-gauche, index 0
	/// = 200 m) à 30° (haut-droite, index 4 = 1000 m), juste à l'extérieur de la
	/// bordure du radar. Partagée entre le rendu (dessin des repères) et le hit-test
	/// du clic (Engine). Pure/testable. \p rect doit être `enabled`.
	ScreenPoint RadarZoomTickPos(const RadarScreenRect& rect, int tickIndex);

	/// Pixel-space rectangle used by the quest UI presenter layout.
	struct QuestUiRect
	{
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
	};

	/// One quest list entry ready for a journal panel.
	struct QuestJournalEntryView
	{
		std::string questId;
		uint8_t status = 0;
		uint32_t completedSteps = 0;
		uint32_t totalSteps = 0;
		bool selected = false;
	};

	/// One quest step line ready for detail and tracker panels.
	struct QuestStepView
	{
		std::string label;
		uint32_t currentCount = 0;
		uint32_t requiredCount = 0;
		bool completed = false;
	};

	/// One minimap POI marker in normalized UV coordinates.
	struct MinimapPoiView
	{
		float u = 0.0f;
		float v = 0.0f;
		std::string label;
		bool visible = false;
		/// SP3 Task 3 — miroir de `engine::server::QuestStepType` (Kill=1,
		/// Collect=2, Talk=3, Enter=4). Sert uniquement a teinter le marqueur
		/// cote renderer (QuestImGuiRenderer::RenderMinimap) ; 0 = inconnu/non
		/// renseigne (couleur de repli grise).
		uint8_t stepType = 0;
	};

	/// Zone metadata loaded from content for minimap rendering.
	struct MinimapZoneMetadata
	{
		uint32_t zoneId = 0;
		std::string texturePath;
		float zoneSizeMeters = 0.0f;
	};

	/// Fully resolved quest journal + tracker + minimap state.
	struct QuestUiState
	{
		QuestUiRect journalPanelBounds{};
		QuestUiRect trackerBounds{};
		QuestUiRect minimapBounds{};
		std::vector<QuestJournalEntryView> journalEntries;
		std::string selectedQuestId;
		uint8_t selectedQuestStatus = 0;
		std::vector<QuestStepView> selectedQuestSteps;
		std::vector<QuestStepView> trackerSteps;
		std::string minimapTexturePath;
		uint32_t minimapZoneId = 0;
		MinimapPoiView playerMarker{};
		MinimapPoiView targetMarker{};
		std::vector<MinimapPoiView> questPois;
		std::string debugText;
		bool layoutValid = false;
	};

	/// Builds quest journal, quest tracker and minimap widget state from the UI model.
	class QuestUiPresenter final
	{
	public:
		/// Construct an uninitialized quest presenter.
		QuestUiPresenter() = default;

		/// Release quest presenter resources.
		~QuestUiPresenter();

		/// Initialize the presenter and load minimap zone metadata from content.
		bool Init(const engine::core::Config& config);

		/// Shutdown the presenter and release cached state.
		void Shutdown();

		/// Update viewport-dependent layout for journal, tracker and minimap widgets.
		bool SetViewportSize(uint32_t width, uint32_t height);

		/// Apply one UI model snapshot and rebuild quest journal, tracker and minimap state.
		bool ApplyModel(const UIModel& model, uint32_t changeMask);

		/// Select one quest to display in the journal detail panel.
		bool SelectQuest(std::string_view questId);

		/// Return the immutable resolved quest UI state.
		const QuestUiState& GetState() const { return m_state; }

		/// SP3 — Injecte la table de POI minimap (positions par targetId de
		/// quête). Référence non possédée : \p table doit survivre au
		/// presenter (typiquement un singleton chargé au boot). `nullptr`
		/// desactive les marqueurs de POI de quête sur la minimap.
		void SetPoiTable(const QuestPoiTable* table) { m_poiTable = table; }

		/// SP3 Task 3 — Redéfinit le rayon du radar minimap (mètres, centre
		/// joueur -> bord). Câblé depuis `client.quest.minimap.radius_m` au
		/// boot (Engine). Ignoré si \p radiusM <= 0 (garde de \ref WorldToRadarUv).
		void SetMinimapRadius(float radiusM) { m_minimapRadiusM = radiusM; }

	private:
		/// Load minimap zone metadata from the configured content-relative file.
		bool LoadZoneMetadata(const engine::core::Config& config);

		/// Parse one metadata line using the `zone|texture|size` format.
		bool ParseZoneMetadataLine(std::string_view line, MinimapZoneMetadata& outMetadata) const;

		/// Recompute widget rectangles after a viewport change.
		void RebuildLayout();

		/// Rebuild the journal list and keep the selected quest valid.
		void RebuildJournal(const UIModel& model);

		/// Rebuild the tracker HUD from active quests.
		void RebuildTracker(const UIModel& model);

		/// SP3 — Reconstruit le radar minimap centré sur le joueur (0.5,0.5).
		/// Pour chaque étape de quête `Active` non terminée, résout ses
		/// positions via \ref m_poiTable (targetId) et les projette en UV
		/// radar avec \ref WorldToRadarUv (rayon \ref m_minimapRadiusM).
		void RebuildMinimap(const UIModel& model);

		/// Return metadata for one zone id, or null when missing.
		const MinimapZoneMetadata* FindZoneMetadata(uint32_t zoneId) const;

		/// Build a human-readable label for one quest step.
		std::string BuildQuestStepLabel(const UIQuestStep& step) const;

		/// Rebuild the textual dump of the resolved quest UI state.
		void RebuildDebugText();

		QuestUiState m_state{};
		UIModel m_lastModel{};
		std::vector<MinimapZoneMetadata> m_zoneMetadata;
		std::string m_selectedQuestId;
		uint32_t m_viewportWidth = 0;
		uint32_t m_viewportHeight = 0;
		std::string m_relativeZoneMetadataPath;
		bool m_initialized = false;

		/// SP3 — table de POI minimap (positions par targetId), injectée via
		/// \ref SetPoiTable. Non possédée ; `nullptr` = pas de marqueurs de POI.
		const QuestPoiTable* m_poiTable = nullptr;
		/// SP3 — rayon du radar minimap en mètres (centre joueur -> bord du
		/// cadre). Valeur par défaut ; le câblage config (client.minimap.*)
		/// est repoussé à la Task 3 du plan SP3.
		float m_minimapRadiusM = 60.0f;
	};
}
