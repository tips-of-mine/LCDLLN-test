#pragma once

#include <string>

namespace engine::editor::world::help
{
	/// Définition d'un tooltip riche pour un paramètre d'outil (M100.47).
	/// Tous les champs textuels — éditables par PR sans recompiler (cf.
	/// contexte critique §1 du ticket).
	///
	/// L'`id` est de forme `"<toolId>.<paramName>"` (ex.
	/// `"hydraulic_erosion.numDroplets"`). C'est la clé de lookup dans
	/// `HelpContentStore::FindTooltip`.
	///
	/// `descriptionSimple` est affichée quand l'éditeur est en mode
	/// **Simple** (vulgarisé) ; `descriptionAdvanced` en mode Advanced
	/// (technique). Le mode actif vient de `WorldEditorShell` /
	/// `ToolPresetRegistry` (M100.45).
	struct TooltipDefinition
	{
		std::string id;                  ///< "hydraulic_erosion.numDroplets"
		std::string label;               ///< "Nombre de gouttes"
		std::string descriptionSimple;   ///< version Simple (vulgarisée)
		std::string descriptionAdvanced; ///< version Advanced (technique)
		std::string defaultValue;        ///< "100000"
		std::string range;               ///< "10000 - 500000"
		std::string docSectionId;        ///< "tool/hydraulic_erosion#numDroplets"
		/// SVG inline (`<svg ...>...</svg>`). Optionnel. Non chargé en
		/// incrément 1 (le parser hand-rolled supporte mal les strings JSON
		/// riches en quotes — sera ajouté quand le store accueillera un
		/// JSON DOM générique).
		std::string svgInline;
	};
}
