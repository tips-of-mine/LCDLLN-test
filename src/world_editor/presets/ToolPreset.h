#pragma once

#include <string>
#include <unordered_map>

namespace engine::editor::world::presets
{
	/// Un preset de paramètres pour un outil éditeur (M100.45, Phase 12).
	///
	/// Les `parameters` sont un sac clé→valeur **numérique** (le repo n'a
	/// pas de dépendance JSON générique ; tous les paramètres d'outils
	/// exposés en preset sont des float/int — cf. exemples du ticket
	/// M100.45). Un outil consomme `parameters` en lisant les clés qu'il
	/// connaît via `GetParam` ; une clé absente laisse la valeur courante
	/// de l'outil inchangée (validation tolérante).
	struct ToolPreset
	{
		std::string id;          ///< "realistic", "intense"… (stable, clé)
		std::string displayName; ///< "Réaliste" (FR, affiché dans le dropdown)
		std::string description; ///< tooltip
		std::unordered_map<std::string, double> parameters;

		/// Lit un paramètre numérique. \return `fallback` si la clé est
		/// absente — l'outil garde alors sa valeur courante pour ce champ.
		double GetParam(const std::string& key, double fallback) const
		{
			auto it = parameters.find(key);
			return (it == parameters.end()) ? fallback : it->second;
		}

		bool HasParam(const std::string& key) const
		{
			return parameters.find(key) != parameters.end();
		}
	};
}
