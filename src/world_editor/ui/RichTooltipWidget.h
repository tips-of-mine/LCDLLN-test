#pragma once

#include <string>

namespace engine::editor::world::ui
{
	/// À appeler **immédiatement après** un widget ImGui (slider, checkbox,
	/// bouton…). Si l'item est survolé au-delà du délai standard ImGui
	/// (`HoverDelayNormal`, ~0.4 s), affiche un tooltip riche pris dans le
	/// `HelpContentStore` (M100.47).
	///
	/// L'`id` doit être de forme `"<toolId>.<paramName>"` (ex.
	/// `"hydraulic_erosion.numDroplets"`).
	///
	/// Contenu du tooltip :
	///   - label (titre),
	///   - description **Simple** OU **Advanced** selon le mode courant
	///     (`EditorModeRegistry::GetCurrentMode()`),
	///   - défaut + plage de valeurs.
	///
	/// Si `id` est introuvable dans le store, le tooltip affiche un
	/// indicateur discret `(tooltip manquant : <id>)` — visible pour le
	/// développeur, pas pour l'utilisateur final qui ne verra rien tant
	/// que l'id n'est pas câblé (alternative : silencieux en release).
	///
	/// No-op hors Windows (l'éditeur monde est Windows-only).
	///
	/// Contraintes thread/timing : appeler depuis le main thread ImGui,
	/// après le widget cible, avant tout autre widget.
	void RichTooltip(const std::string& tooltipId);
}
