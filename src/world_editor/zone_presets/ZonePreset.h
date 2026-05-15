#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace engine::editor::world::zone_presets
{
	/// Chaîne localisée FR/EN (M100.46). Le format JSON est extensible
	/// (de, es…) ; le MVP ne porte que `fr` et `en`.
	struct LocalizedString
	{
		std::string fr;
		std::string en;

		/// Renvoie la traduction pour `lang` ("fr"/"en"). Fallback : `fr`,
		/// puis `en`, puis chaîne vide.
		const std::string& Get(const std::string& lang) const;
	};

	/// Une opération d'un zone preset — correspond à l'exécution d'un
	/// outil éditeur (M100.35-43) sur la zone en construction.
	///
	/// MVP M100.46 incrément 1 : seuls `type`, `toolPresetId` et
	/// `affectedBy` sont parsés en structuré. Le reste des paramètres
	/// (polyline, polygon, worldPosition, scalaires…) est conservé tel
	/// quel dans `rawJson` — le futur `OperationDispatcher` (incrément 2)
	/// les extraira de manière typée selon `type`. Cela évite d'introduire
	/// un DOM JSON générique récursif dans cet incrément.
	struct ZonePresetOperation
	{
		std::string              type;          ///< "mountain_macro", "hydraulic_erosion"…
		std::string              toolPresetId;  ///< optionnel — réfère un tool preset M100.45
		std::vector<std::string> affectedBy;    ///< "relief" / "water_density" / "dryness"
		std::string              rawJson;       ///< objet opération complet, texte brut
	};

	/// Template de zone jouable prêt à l'emploi (M100.46). Décrit une
	/// séquence ordonnée d'opérations éditeur à exécuter sur une zone vide.
	///
	/// La section `decoration` est **réservée à la Phase 13** : pour le
	/// MVP elle est vide et seul son nombre d'entrées est mémorisé
	/// (`decorationEntryCount`) — un preset livré aujourd'hui doit avoir
	/// `decorationEntryCount == 0`.
	struct ZonePreset
	{
		int             version = 1;
		std::string     id;
		LocalizedString displayName;
		LocalizedString description;
		std::string     thumbnailPath;
		std::vector<std::string> tags;
		float           estimatedExecutionSeconds = 0.0f;

		std::vector<ZonePresetOperation> operations;
		size_t          decorationEntryCount = 0u;  ///< Phase 13 — 0 attendu en MVP

		/// Validation structurelle : id non vide, version cohérente, au
		/// moins une opération, chaque `type` parmi les types connus,
		/// chaque `affectedBy` parmi {relief, water_density, dryness}.
		/// \return false + `errorOut` renseigné sur la première erreur.
		bool Validate(std::string& errorOut) const;
	};

	/// true si `type` est un type d'opération reconnu par M100.46
	/// (les 14 outils M100.35-43 listés dans le spec A.3).
	bool IsKnownOperationType(const std::string& type);

	/// true si `tag` est un multiplicateur de customisation valide
	/// (`relief`, `water_density`, `dryness`).
	bool IsKnownAffectedByTag(const std::string& tag);
}
