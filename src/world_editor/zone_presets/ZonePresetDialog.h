#pragma once

#include "src/world_editor/zone_presets/CustomizationApplier.h"
#include "src/world_editor/zone_presets/ZonePresetExecutor.h"

#include <cstdint>
#include <string>

namespace engine::core
{
	class Config;
}

namespace engine::editor::world
{
	class WorldEditorShell;
}

namespace engine::editor::world::zone_presets
{
	/// Dialog modal pour appliquer un Zone Preset à la carte courante
	/// (M100.46 incrément 3). Encapsule :
	///   - sélection d'un preset dans `ZonePresetRegistry`,
	///   - sliders de customisation (relief / water_density / dryness)
	///     + champ seed,
	///   - bouton « Appliquer » qui exécute synchronone (le main thread
	///     ImGui est bloqué pendant la durée d'exécution — convention
	///     éditeur single-thread, cf. ZonePresetExecutor.h),
	///   - écran de résumé post-exécution (commandes poussées / skipped /
	///     failed / annulé).
	///
	/// Cycle de vie :
	///   1. `Open()` ouvre la popup au prochain frame.
	///   2. `Draw(shell)` est appelée chaque frame depuis WorldEditorImGui ;
	///      no-op si la popup est fermée.
	///   3. Au clic « Appliquer », `Draw` exécute le preset et passe en
	///      écran « Résultat » sans fermer la popup (l'utilisateur ferme
	///      via « OK » ou la croix).
	///
	/// Contraintes thread/timing : doit être appelée depuis le main thread
	/// (rendu ImGui). Les accès aux 4 documents du Shell ne sont pas
	/// thread-safe.
	class ZonePresetDialog
	{
	public:
		ZonePresetDialog();
		~ZonePresetDialog();

		ZonePresetDialog(const ZonePresetDialog&)            = delete;
		ZonePresetDialog& operator=(const ZonePresetDialog&) = delete;

		/// Demande l'ouverture de la popup au prochain `Draw()`. No-op si
		/// déjà ouverte.
		void Open();

		/// Dessine la popup si elle est ouverte. Reçoit le Shell pour
		/// résoudre les documents (terrain / water / mesh / dungeon) et
		/// les 4 catalogs (caves / overhangs / arches / dungeons), plus
		/// la `Config` requise par les 4 ops simulation (incrément 2e).
		/// `cfg` peut être null mais les ops `hydraulic_erosion`,
		/// `thermal_wind_erosion`, `river_network`, `coastline` renverront
		/// alors `Failed`.
		void Draw(engine::editor::world::WorldEditorShell& shell,
			const engine::core::Config* cfg);

	private:
		/// État interne de la popup. Pas exposé en public pour garder le
		/// header léger.
		enum class Screen : uint8_t
		{
			Select  = 0,  ///< choix du preset + sliders
			Result  = 1,  ///< résumé post-exécution
		};

		void DrawSelectScreen(engine::editor::world::WorldEditorShell& shell,
			const engine::core::Config* cfg);
		void DrawResultScreen();

		/// Exécute le preset sélectionné. Renseigne `m_lastSummary` et
		/// bascule l'écran à `Result`. Le main thread ImGui est bloqué
		/// pendant la durée — comportement attendu.
		void RunSelectedPreset(engine::editor::world::WorldEditorShell& shell,
			const engine::core::Config* cfg);

		/// `true` au prochain frame quand `Open()` a été appelé — pousse
		/// `ImGui::OpenPopup` une seule fois puis remet à `false`.
		bool m_openRequested = false;
		bool m_isOpen        = false;
		Screen m_screen      = Screen::Select;

		/// Index du preset sélectionné dans `ZonePresetRegistry::Presets()`
		/// (size_t MAX = aucune sélection au premier ouvert).
		size_t m_selectedIndex = static_cast<size_t>(-1);

		/// Curseurs customisation MVP (UI). Convertis en `CustomizationParams`
		/// au moment de l'exécution.
		float m_relief       = 1.0f;
		float m_waterDensity = 1.0f;
		float m_dryness      = 1.0f;
		uint32_t m_seed      = 12345u;

		/// Renseigné après une exécution. `commandsPushed` = 0 et
		/// `totalSteps` = 0 → pas d'exécution encore lancée.
		ExecutionSummary m_lastSummary{};
		/// Id du preset effectivement exécuté (pour l'écran résultat).
		std::string m_lastPresetId;
		/// Durée wall-clock observée côté UI (millisecondes).
		double m_lastDurationMs = 0.0;
	};
}
