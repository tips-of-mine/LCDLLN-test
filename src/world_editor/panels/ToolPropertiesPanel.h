#pragma once
#include "src/world_editor/core/IPanel.h"
#include "src/world_editor/terrain/StampLibrary.h"

#include <string>
#include <vector>

namespace engine::editor::world
{
	class WorldEditorShell;
}
namespace engine::editor::world { class LakeTool; class RiverTool; class MountainRangeTool; class ValleyChainTool; class RiverNetworkTool; class CoastlineEditorTool; namespace erosion { class HydraulicErosionTool; class ThermalWindErosionTool; } namespace volumes::caves { class CaveTool; } namespace volumes::overhangs { class OverhangTool; } }

namespace engine::editor::world::panels
{
	/// Panneau Tool Properties du shell éditeur monde (M100.1 → M100.7).
	/// M100.1 : placeholder uniquement.
	/// M100.6 : si l'outil actif est TerrainSculpt, rend les paramètres de
	///         brosse (mode radio, sliders radius/strength/falloff, sliders
	///         noise + checkboxes mirror).
	/// M100.7 : si l'outil actif est TerrainStamp, rend les paramètres de
	///         stamp (radio source library/procedural, combos, sliders
	///         footprint/strength/rotation, radio mode, boutons Apply/Cancel).
	/// La référence au shell est passée via `SetShell` après l'instanciation
	/// (le shell construit le panneau dans `m_panels` puis injecte son adresse).
	class ToolPropertiesPanel final : public IPanel
	{
	public:
		const char* GetName() const override { return "Tool Properties"; }

		/// Rend le panneau Tool Properties. Si aucun outil n'est actif (ou
		/// si `m_shell` est null), affiche le placeholder M100.1.
		/// Effet de bord : crée une window ImGui nommée "Tool Properties".
		void Render() override;

		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

		/// M100.6 — Injecte la référence au shell pour que le panneau puisse
		/// lire `GetActiveTool()` et muter les paramètres de la brosse via
		/// `MutableSculptTool()`. Doit être appelé après l'instanciation,
		/// idéalement depuis `WorldEditorShell::Init`. Le shell garantit la
		/// durée de vie : il possède le panneau et l'outil.
		void SetShell(WorldEditorShell* shell) { m_shell = shell; }

	private:
		/// M100.7 — Recharge la liste des stamps depuis `m_stampLibraryDir`
		/// (par défaut `assets/editor/stamps`). Appelée à la demande via le
		/// bouton "Refresh", et automatiquement à la première ouverture du
		/// panneau Stamp si `m_stampLibraryLoaded` est false.
		/// Effet de bord : remplit `m_stampLibrary`.
		void RefreshStampLibrary();

		void RenderLakeParams(engine::editor::world::WorldEditorShell& shell,
			engine::editor::world::LakeTool& tool);
		void RenderRiverParams(engine::editor::world::WorldEditorShell& shell,
			engine::editor::world::RiverTool& tool);
		/// M100.35 — Bloc UI "Macro polyline" pour l'outil Mountain Range.
		/// Affiche la liste des vertices posés, les paramètres globaux
		/// (mode Loop, profil, seed/freq bruit), le bloc du vertex sélectionné
		/// (largeur, hauteur, bruit, asymétrie) et les boutons Apply/Cancel.
		void RenderMountainRangeParams(
			engine::editor::world::WorldEditorShell& shell,
			engine::editor::world::MountainRangeTool& tool);
		/// M100.35 — Identique à `RenderMountainRangeParams` pour Valley Chain
		/// (defaults différents côté UI, sémantique soustractive côté outil).
		void RenderValleyChainParams(
			engine::editor::world::WorldEditorShell& shell,
			engine::editor::world::ValleyChainTool& tool);

		/// M100.36 — Bloc UI "River Network" pour la simulation watershed :
		/// liste des sources, sliders sea level (binding direct
		/// `WaterDocument::OceanSettings`), threshold, simplification,
		/// auto-lakes, carving, boutons Simulate / Apply / Cancel.
		void RenderRiverNetworkParams(
			engine::editor::world::WorldEditorShell& shell,
			engine::editor::world::RiverNetworkTool& tool);

		/// M100.37 — Bloc UI "Coastline" pour l'édition du niveau de mer
		/// et la génération automatique de l'océan : sliders sea level
		/// (binding direct `WaterDocument::OceanSettings`), couleur de fond,
		/// turbidité, smoothing / falaises optionnels, statistiques live.
		void RenderCoastlineParams(
			engine::editor::world::WorldEditorShell& shell,
			engine::editor::world::CoastlineEditorTool& tool);

		/// M100.38 — Bloc UI "Hydraulic Erosion" : sliders physique
		/// (sediment capacity, erosion/deposition rates, gravity, inertia,
		/// evaporation), distribution de seeding, paramètres de bornes,
		/// boutons Simulate / Apply / Cancel / Re-simulate, stats du
		/// dernier résultat.
		void RenderHydraulicErosionParams(
			engine::editor::world::WorldEditorShell& shell,
			engine::editor::world::erosion::HydraulicErosionTool& tool);

		/// M100.39 — Bloc UI "Thermal / Wind Erosion" (clôt la Phase 2.5).
		/// Radio sous-mode (Thermal / Wind / Both), deux sections de
		/// paramètres physiques, encart workflow recommandé, stats résultat.
		void RenderThermalWindErosionParams(
			engine::editor::world::WorldEditorShell& shell,
			engine::editor::world::erosion::ThermalWindErosionTool& tool);

		/// M100.40 — Bloc UI "Cave" (démarre la Phase 11 « Volumes 3D »).
		/// Catalogue de grottes glTF (sélection par id) + sliders position,
		/// rotation, scale, camouflage splat « rocher », flags volume
		/// intérieur / reverb / water ingress, intensité probe lumière.
		void RenderCaveParams(
			engine::editor::world::WorldEditorShell& shell,
			engine::editor::world::volumes::caves::CaveTool& tool);

		/// M100.41 — Bloc UI "Overhang" (Phase 11). Catalogue de surplombs
		/// glTF + sliders position, yaw normal mur, tilt latéral, scale,
		/// validation manuelle de la slope locale (en attendant un raycast
		/// normal automatique de M100.17).
		void RenderOverhangParams(
			engine::editor::world::WorldEditorShell& shell,
			engine::editor::world::volumes::overhangs::OverhangTool& tool);

		bool m_visible = true;
		WorldEditorShell* m_shell = nullptr;

		// M100.7 — Cache des entrées library + état UI
		std::string m_stampLibraryDir = "assets/editor/stamps";
		std::vector<engine::editor::world::StampEntry> m_stampLibrary;
		bool m_stampLibraryLoaded = false;
		int m_stampLibrarySelected = 0;
	};
}
