#pragma once

#include "src/world_editor/modes/EditorMode.h"
#include "src/world_editor/presets/ToolPreset.h"

#include <vector>

namespace engine::editor::world::ui
{
	/// Interface que chaque outil éditeur expose pour rendre son panneau
	/// de propriétés de manière mode-aware (M100.45, Phase 12).
	///
	/// Phase A (M100.45) : l'interface est définie ; aucun outil ne
	/// l'implémente encore. Phase B migre les 13 outils un par un — un
	/// outil non encore migré retourne `IsModeAwareInPropertiesPanel() ==
	/// false` et reste rendu comme aujourd'hui (toujours en Advanced),
	/// ce qui garantit la backward-compat pendant la migration.
	class IToolPropertiesPanelContent
	{
	public:
		virtual ~IToolPropertiesPanelContent() = default;

		/// Titre affiché du panneau (ex : "Hydraulic Erosion").
		virtual const char* GetDisplayName() const = 0;

		/// Identifiant stable pour les presets et `user_prefs.json`
		/// (ex : "hydraulic_erosion"). Doit matcher le `toolId` du fichier
		/// `tool_presets/<id>.json`.
		virtual const char* GetToolId() const = 0;

		/// True si l'outil sait masquer ses paramètres avancés selon le
		/// mode. Pendant la migration Phase B, un outil non migré retourne
		/// false → le panel le rend en Advanced quel que soit le mode.
		virtual bool IsModeAwareInPropertiesPanel() const = 0;

		/// Rend le contenu du panneau. Le `mode` courant est fourni pour
		/// que l'outil sache quels paramètres masquer (Simple) ou tout
		/// exposer (Advanced).
		virtual void RenderPropertiesPanel(modes::EditorMode mode) = 0;

		/// Presets disponibles pour cet outil (issus de
		/// `ToolPresetRegistry`). Vide si l'outil n'a pas de fichier de
		/// presets.
		virtual const std::vector<presets::ToolPreset>& GetAvailablePresets() const = 0;

		/// Applique un preset aux paramètres internes de l'outil.
		/// Validation tolérante : une clé absente du preset laisse la
		/// valeur courante de l'outil inchangée (cf. `ToolPreset::GetParam`).
		virtual void ApplyPreset(const presets::ToolPreset& preset) = 0;
	};
}
