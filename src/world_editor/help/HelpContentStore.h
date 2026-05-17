#pragma once

#include "src/world_editor/help/TooltipDefinition.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace engine::editor::world::help
{
	/// Singleton qui détient les contenus d'aide chargés depuis le content
	/// path (M100.47 — incrément 1 : tooltips seulement ; les sections doc
	/// markdown + l'index BM25 viennent en incréments 2 et 3).
	///
	/// Chargement **tolérant** (cf. ToolPresetRegistry / ZonePresetRegistry) :
	/// un fichier JSON malformé ou un tooltip sans `id` est loggé en warning
	/// et ignoré ; les autres sont chargés normalement. L'éditeur démarre
	/// toujours, même catalogue partiel.
	///
	/// La clé de lookup est de forme `"<toolId>.<paramName>"`.
	/// Ex. `FindTooltip("hydraulic_erosion.numDroplets")`.
	class HelpContentStore
	{
	public:
		static HelpContentStore& Instance();

		/// Charge tous les `*.json` de `<contentRoot>/editor/tooltips/`.
		/// Remplace l'index courant. \return le nombre de tooltips
		/// valides chargés (somme sur tous les fichiers).
		size_t LoadFromContentPath(const std::string& contentRoot);

		/// Recharge depuis le dernier `contentRoot` (hot-reload dev).
		size_t Reload();

		/// Recherche par id (`"toolId.paramName"`). nullptr si introuvable.
		const TooltipDefinition* FindTooltip(const std::string& id) const;

		/// Tous les tooltips chargés (ordre d'insertion non garanti — utiliser
		/// pour l'introspection / debug, pas pour l'UI).
		const std::unordered_map<std::string, TooltipDefinition>& All() const
		{
			return m_tooltips;
		}

		size_t Size() const { return m_tooltips.size(); }

		/// Vide le store. Utilisé par les tests pour isoler des cas.
		void ResetForTesting();

	private:
		HelpContentStore() = default;

		std::unordered_map<std::string, TooltipDefinition> m_tooltips;
		std::string                                        m_lastContentRoot;
	};

	/// Container intermédiaire renvoyé par le parser : 1 fichier JSON =
	/// 1 toolId + N définitions de tooltips. La clé `id` finale est
	/// reconstruite côté store comme `toolId + "." + paramName`.
	struct TooltipFileContents
	{
		std::string toolId;
		/// Map `paramName` → définition (sans le préfixe toolId).
		std::unordered_map<std::string, TooltipDefinition> tooltips;
	};

	/// Parse un fichier JSON tooltip (cf. spec ticket §D.1). Tolérant aux
	/// clés inconnues ; renseigne `outError` en cas d'échec structurel
	/// (toolId manquant, JSON non terminé). \return false sur échec.
	///
	/// L'incrément 1 ignore le champ `svgInline` (parser hand-rolled
	/// fragile sur strings JSON contenant des quotes et brackets — à
	/// porter quand le projet aura un JSON DOM générique).
	bool ParseTooltipFileJson(const std::string& jsonText,
		TooltipFileContents& out, std::string& outError);
}
