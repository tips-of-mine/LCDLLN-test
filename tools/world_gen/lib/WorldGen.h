#pragma once

// tools/world_gen — point d'entrée public du générateur de monde.
//
// Usage typique (CLI `lcdlln_world_gen`) :
//   lcdlln_world_gen --content game/data --world royaumes
// Génère, depuis `game/data/world/<world>/world_master.json`, la couche macro
// du continent puis chaque CELLULE de 10 km listée dans `generated_cells` (ou
// passée par --cells). Déterministe : deux exécutions produisent des fichiers
// identiques octet pour octet, quel que soit le nombre de cœurs.

#include "WorldGenTypes.h"

#include <string>
#include <vector>

namespace tools::world_gen
{
	struct GenOptions
	{
		std::string contentRoot = "game/data"; ///< racine du contenu (paths.content)
		std::string worldId = "royaumes";      ///< sous-dossier world/<id>/
		std::string outputRoot;                ///< racine d'écriture (vide = contentRoot)
		bool dryRun = false;                   ///< true : génère en mémoire sans rien écrire
		/// Cellules à générer (identifiants du lore « N003_W009 ») ; vide = la
		/// liste `generated_cells` du master.
		std::vector<std::string> cells;
	};

	/// Exécute le pipeline complet. \return false + \p err en cas d'échec
	/// (master invalide, mesh manquant, ville noyée…). Effet de bord : écrit
	/// les fichiers listés dans \p outReport.writtenFiles (sauf dryRun).
	bool RunWorldGen(const GenOptions& opt, GenReport& outReport, std::string& err);
}
