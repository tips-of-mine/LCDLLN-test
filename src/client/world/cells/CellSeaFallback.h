#pragma once

// Lot D (bug 3) — « mer de repli » des cellules NON générées du monde en cellules.
//
// Constat : 245 cellules voisines de cellules générées ne sont pas générées
// (toutes de la mer : '-' mer peu profonde, ' ' mer profonde, ou hors grille ;
// 2 seulement touchent une côte). Les générer coûterait ~7 Go. Sans contenu,
// le client n'y dessinait RIEN : vide gris/blanc au-delà de la côte (ouest de
// Lhynn, N001_W012) et bord coupé net du terrain de la cellule voisine.
//
// Choix (client seul, aucune donnée nouvelle) : chaque cellule manquante proche
// du joueur reçoit la MÊME nappe de mer qu'une cellule maritime générée
// (tools/world_gen Output.cpp : lac `isOcean` carré couvrant la cellule + 2 m de
// marge, au niveau de la mer, mêmes couleur de fond et turbidité). Elle passe par
// le rendu d'eau existant (WaterPass : houle, reflets, écume) et par la nage
// (TerrainCollider::QueryWater). Pas de fond marin en géométrie : sans rien
// derrière la nappe, water.frag voit une colonne d'eau « infinie » et donne la
// couleur du large (opaque) ; sous l'eau, UnderwaterPass noie la même profondeur
// dans son brouillard bleu. Le joueur, lui, ne peut pas entrer dans une cellule
// manquante (mur invisible IsMoveAllowed + IsMoveAllowedAvoiding).
//
// Module PUR (WorldCells + types d'eau) → testé en CI (cell_sea_fallback_tests).

#include "src/shared/world/WorldCells.h"
#include "src/shared/world/water/WaterSurfaces.h"

#include <functional>
#include <vector>

namespace engine::world::cells
{
	/// Marge (m) de recouvrement de la nappe de repli au-delà de la cellule : la même
	/// que celle du générateur (pas de couture visible avec la mer voisine).
	inline constexpr double kFallbackSeaMarginM = 2.0;
	/// Rayon maximal (cellules) de la sélection, quelle que soit la distance de vue.
	inline constexpr int kFallbackSeaMaxRadiusCells = 4;

	/// Rayon de sélection (distance de Tchebychev, en cellules, autour de la cellule
	/// du joueur) pour couvrir une distance de vue \p viewDistanceM (m) : le joueur
	/// peut être n'importe où dans sa cellule, d'où la cellule de plus.
	/// Borné à [1, kFallbackSeaMaxRadiusCells]. Pur.
	int FallbackSeaRadiusCells(double viewDistanceM);

	/// Cellules « manquantes » (\p isMissing vrai : non générée, ou chargement en
	/// échec) à au plus \p radiusCells cellules (Tchebychev) de \p center, dans un
	/// ordre déterministe (n décroissant puis e croissant : comparables d'une frame
	/// à l'autre). \p out est vidé puis rempli. Pur.
	void SelectFallbackSeaCells(CellCoord center, int radiusCells,
		const std::function<bool(CellCoord)>& isMissing, std::vector<CellCoord>& out);

	/// Nappe de mer de repli de \p cell, en coordonnées de RENDU (origine globale
	/// \p originX / \p originZ soustraite en double avant conversion float) :
	/// carré de la cellule élargi de kFallbackSeaMarginM, à \p seaLevelY (m),
	/// `isOcean` vrai, couleur de fond et turbidité de la mer du générateur.
	/// Sommets dans le même ordre que world_gen (même triangulation). Pur.
	engine::world::water::LakeInstance MakeFallbackSeaLake(CellCoord cell, float seaLevelY,
		double originX, double originZ);

	/// Niveau de la mer d'une liste de lacs : celui du premier lac `isOcean`.
	/// \return false si aucun lac n'est une mer (ex. carte des Enfers : pas de mer
	/// de repli, l'au-delà y est un abîme). Pur.
	bool FindSeaLevel(const std::vector<engine::world::water::LakeInstance>& lakes, float& outSeaLevelY);

	/// Mur invisible généralisé : vrai si le déplacement du point GLOBAL (fromX,
	/// fromZ) vers (toX, toZ) n'entre pas dans une cellule pour laquelle
	/// \p isMissing est vrai. Un départ déjà dans une cellule manquante n'est
	/// jamais bloqué (on ne fige pas un joueur placé là par le serveur). Pur.
	bool IsMoveAllowedAvoiding(const std::function<bool(CellCoord)>& isMissing,
		double fromX, double fromZ, double toX, double toZ);
}
