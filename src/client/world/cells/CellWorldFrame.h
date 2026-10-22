#pragma once

// Monde « Royaumes » côté client — repère de RENDU et choix des cellules.
//
// Le monde est continu, en coordonnées GLOBALES (m, double côté calcul) jusqu'à
// ±200 km : un float n'y garde qu'environ 1,6 cm de précision, assez pour le
// réseau mais pas pour le rendu (tremblements de la caméra, du TAA, des
// ombres). Le client travaille donc dans un repère de RENDU dont l'origine est
// le CENTRE de la cellule où se trouve le joueur : toutes les positions vues
// par le moteur (joueur, caméra, entités, terrain, décor) restent à moins
// d'une quinzaine de km de (0,0), comme l'ancienne zone de test.
//
// Quand le joueur passe dans une autre cellule, l'origine saute au centre de
// la nouvelle (« rebase ») : le décalage est un multiple exact de 10 km.
//
// Module PUR (WorldCells uniquement) → testé en CI (cell_world_frame_tests).

#include "src/shared/world/WorldCells.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace engine::world::cells
{
	/// Repère de rendu : origine = centre d'une cellule (m globaux, exacts en double).
	class RenderFrame final
	{
	public:
		/// Place l'origine au centre de \p cell.
		void SetOriginCell(CellCoord cell);
		CellCoord OriginCell() const { return m_originCell; }
		double OriginX() const { return m_originX; }
		double OriginZ() const { return m_originZ; }

		/// Global (m) → rendu (m, float).
		float ToRenderX(double globalX) const { return static_cast<float>(globalX - m_originX); }
		float ToRenderZ(double globalZ) const { return static_cast<float>(globalZ - m_originZ); }
		/// Rendu (m) → global (m).
		double ToGlobalX(double renderX) const { return renderX + m_originX; }
		double ToGlobalZ(double renderZ) const { return renderZ + m_originZ; }

		/// Cellule du point exprimé en coordonnées de rendu.
		CellCoord CellOfRender(double renderX, double renderZ) const;
		/// Centre de \p cell en coordonnées de rendu (décalage d'une donnée stockée
		/// en coordonnées locales à sa cellule).
		CellPoint CellCenterRender(CellCoord cell) const;

		/// Décalage (m) à AJOUTER à une position de rendu exprimée dans le repère
		/// courant pour l'exprimer dans le repère centré sur \p newOrigin.
		CellPoint RebaseDelta(CellCoord newOrigin) const;

	private:
		CellCoord m_originCell{};
		double m_originX = 0.0;
		double m_originZ = 0.0;
	};

	/// Ensemble des cellules générées (jouables) du monde.
	class GeneratedCellSet final
	{
	public:
		void Clear() { m_ids.clear(); m_cells.clear(); }
		/// Ajoute une cellule (doublons ignorés). Hors carte du lore : ignorée.
		void Add(CellCoord cell);
		/// Vrai si la cellule est générée.
		bool Contains(CellCoord cell) const;
		/// Vrai si le point GLOBAL est dans une cellule générée.
		bool ContainsGlobal(double globalX, double globalZ) const;
		const std::vector<CellCoord>& Cells() const { return m_cells; }
		bool Empty() const { return m_cells.empty(); }

	private:
		std::unordered_set<uint32_t> m_ids;
		std::vector<CellCoord> m_cells;
	};

	/// Cellules générées dont le rectangle est à moins de \p radiusMeters du point
	/// GLOBAL (gx, gz) (celle qui le contient comprise si générée), triées de la
	/// plus proche à la plus lointaine (ordre de chargement).
	void SelectCellsInRange(double globalX, double globalZ, double radiusMeters,
		const GeneratedCellSet& generated, std::vector<CellCoord>& out);

	/// Mur invisible : vrai si le déplacement du point global (fromX, fromZ) vers
	/// (toX, toZ) reste dans le monde généré. Un point de départ déjà hors du monde
	/// (téléportation du serveur, données anciennes) n'est jamais bloqué : on ne
	/// fige pas un joueur qui doit pouvoir revenir.
	bool IsMoveAllowed(const GeneratedCellSet& generated, double fromX, double fromZ, double toX, double toZ);
}
