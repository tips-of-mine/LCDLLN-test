#pragma once

// Monde « Royaumes » — chargement EN TÂCHE DE FOND des données d'une cellule.
//
// Tout ce qui est lecture disque et calcul CPU (zone.json, heightmap 2049²,
// normal map, splat map, masque d'herbe, scatter.bin, buildings.bin,
// bibliothèque de bâtiments) est fait sur un thread de travail ; le main thread
// ne fait plus que les envois GPU et la pose des instances (Engine::LoadCellTile).
//
// Un seul thread de travail : les cellules arrivent une à une, la plus proche
// d'abord (c'est l'appelant qui choisit l'ordre et limite le nombre en vol).
// Les résultats sont récupérés par Poll() au main thread ; un résultat devenu
// inutile (joueur reparti) est simplement jeté par l'appelant.

#include "src/client/render/terrain/TerrainRenderer.h"
#include "src/client/world/cells/CellHeightFields.h"
#include "src/client/world/cells/CellSurfaceMap.h"
#include "src/client/world/instances/BuildingTemplateLibrary.h"
#include "src/client/world/instances/Buildings.h"
#include "src/client/world/instances/ScatterInstances.h"
#include "src/shared/core/Config.h"
#include "src/shared/world/WorldCells.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace engine::world::cells
{
	/// Demande de chargement d'une cellule.
	struct CellAssetJob
	{
		CellCoord cell{};
		/// true = aussi les données de rendu (terrain, décor, bâtiments) ;
		/// false = seulement zone.json + heightmap CPU (collision, pose au sol).
		bool wantRender = false;
		/// Configuration de base (copie de la config moteur, prise au main thread) :
		/// chemin du contenu, réglages terrain par défaut. Le zone.json de la cellule
		/// y est fusionné.
		engine::core::Config baseConfig;
	};

	/// Données d'une cellule prêtes à être consommées au main thread.
	struct CellAssets
	{
		CellCoord cell{};
		bool wantRender = false;

		/// zone.json + heightmap lus (sinon la cellule est inutilisable).
		bool heightsOk = false;
		/// Raison de l'échec (journal).
		std::string error;
		/// Config de base + zone.json de la cellule (positions GLOBALES).
		engine::core::Config config;
		/// zone.json SEUL (interactibles, réglages propres à la cellule).
		engine::core::Config zoneOnly;
		/// Heightmap CPU (collision, pose au sol) — coin min GLOBAL, taille, échelle.
		CellHeightTile heights;

		/// Données de rendu préparées (si wantRender && heightsOk).
		bool renderOk = false;
		/// Terrain : copie de la heightmap, normal map calculée, splat, herbe.
		engine::render::terrain::TerrainPreloadedData terrain;
		/// SND.2 — couche dominante du splat par texel (sons de pas), construite
		/// sur le thread de travail ; nulle si la cellule n'a pas de splat.
		std::shared_ptr<CellSurfaceMap> surface;
		/// Décor instancié décodé (absent = cellule sans décor).
		bool hasScatter = false;
		engine::world::instances::ScatterFile scatter;
		/// Bâtiments décodés + bibliothèque des types pour les résoudre.
		std::vector<engine::world::instances::BuildingPlacement> buildings;
		engine::world::instances::BuildingTemplateLibrary buildingLibrary;
	};

	/// Charge les données de \p job (tout thread, aucune ressource GPU). Utilisée
	/// par le thread de travail et, au boot / à une téléportation, directement au
	/// main thread (chargement synchrone). \return out.heightsOk.
	bool LoadCellAssets(const CellAssetJob& job, CellAssets& out);

	/// Thread de travail des cellules. Main thread : Start, Request, Poll, Stop.
	class CellAssetLoader
	{
	public:
		~CellAssetLoader();

		/// Démarre le thread (idempotent).
		void Start();
		/// Arrêt : vide la file, attend la fin du travail en cours, joint le thread.
		/// Idempotent. Les résultats non récupérés sont perdus.
		void Stop();

		/// Ajoute une demande (retour immédiat). Ignorée si la même cellule est déjà
		/// demandée avec au moins autant de données (wantRender).
		void Request(CellAssetJob job);
		/// Vrai si \p cell est en file, en cours ou prête (non récupérée), avec au
		/// moins \p wantRender.
		bool IsPending(CellCoord cell, bool wantRender) const;
		/// Nombre de demandes en file + en cours + prêtes non récupérées.
		size_t InFlight() const;
		/// Récupère (déplace) les résultats prêts dans \p out (ajoutés à la fin).
		void Poll(std::vector<CellAssets>& out);

	private:
		void WorkerLoop();

		std::thread m_thread;
		mutable std::mutex m_mutex;
		std::condition_variable m_cv;
		bool m_stop = false;
		std::deque<CellAssetJob> m_queue;
		/// Demande en cours de traitement (cell + wantRender), valide si m_busy.
		bool m_busy = false;
		CellCoord m_busyCell{};
		bool m_busyRender = false;
		std::vector<CellAssets> m_done;
	};
}
