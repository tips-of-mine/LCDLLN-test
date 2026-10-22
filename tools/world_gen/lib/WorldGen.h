#pragma once

// tools/world_gen — point d'entrée public du générateur de monde.
//
// Usage typique (CLI `lcdlln_world_gen`) :
//   lcdlln_world_gen --content game/data --world royaumes
// Régénère TOUTES les données dérivées du monde depuis
// `game/data/world/<world>/world_master.json`. Déterministe : deux exécutions
// sur la même machine produisent des fichiers identiques octet pour octet.

#include "WorldGenTypes.h"

#include <string>

namespace tools::world_gen
{
	struct GenOptions
	{
		std::string contentRoot = "game/data"; ///< racine du contenu (paths.content)
		std::string worldId = "royaumes";      ///< sous-dossier world/<id>/
		std::string outputRoot;                ///< racine d'écriture (vide = contentRoot)
		bool dryRun = false;                   ///< true : génère en mémoire sans rien écrire
	};

	/// Exécute le pipeline complet. \return false + \p err en cas d'échec
	/// (master invalide, mesh manquant, ville noyée…). Effet de bord : écrit
	/// les fichiers listés dans \p outReport.writtenFiles (sauf dryRun).
	bool RunWorldGen(const GenOptions& opt, GenReport& outReport, std::string& err);
}
