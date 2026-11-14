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
		/// Nombre de cellules générées EN MÊME TEMPS (option --jobs). 0 = automatique
		/// (nombre de cœurs, 4 au plus). Les sorties ne dépendent pas de ce nombre
		/// (fichiers identiques octet pour octet) : chaque cellule est indépendante et
		/// les bilans sont fusionnés dans l'ordre des cellules.
		int jobs = 0;
		/// v1.0.121 — option --graveyard-report <fichier> : recensement des cimetières
		/// par cellule (une ligne « cellule nombre id1@x:z id2@x:z … », position monde en m, « @? » pour
		/// une ligne importée), écrit APRÈS la
		/// génération des cellules et AVANT le contrôle de `graveyards.target` (il existe
		/// donc aussi quand le total est faux : il sert à choisir les hameaux de
		/// `without_graveyard`). Écrit même en --dry-run. Vide = pas de rapport.
		std::string graveyardReport;
	};

	/// Exécute le pipeline complet. \return false + \p err en cas d'échec
	/// (master invalide, mesh manquant, ville noyée…). Effet de bord : écrit
	/// les fichiers listés dans \p outReport.writtenFiles (sauf dryRun).
	bool RunWorldGen(const GenOptions& opt, GenReport& outReport, std::string& err);
}
