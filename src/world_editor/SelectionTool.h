#pragma once

// M100.34 — SelectionTool : sélection rectangulaire et lasso (géométrie PURE,
// testable headless). L'outil ne connaît ni ImGui ni le viewport : il reçoit
// les positions 2D (projection top-down X/Z) des entités sélectionnables et
// retourne les identifiants contenus dans la zone. Le câblage viewport (drag
// rectangle / main levée) et l'alimentation de la sélection multiple vivent
// dans le shell éditeur (2e passe UI).
//
// Ne dépend pas de EditorSelection (sélection simple existante) : SelectionTool
// produit une LISTE d'ids, que le shell peut ensuite appliquer.

#include <cstdint>
#include <utility>
#include <vector>

namespace engine::editor::world
{
	/// Un point sélectionnable : identifiant d'instance + position projetée X/Z.
	struct SelectablePoint
	{
		uint32_t id = 0;
		float    x  = 0.0f;
		float    z  = 0.0f;
	};

	/// Rectangle de sélection aligné aux axes (coordonnées monde X/Z).
	struct SelectionRect
	{
		float minX = 0.0f;
		float minZ = 0.0f;
		float maxX = 0.0f;
		float maxZ = 0.0f;

		/// Normalise pour garantir min ≤ max (drag dans n'importe quel sens).
		void Normalize();

		/// True si le point (x,z) est dans le rectangle (bornes incluses).
		bool Contains(float x, float z) const;
	};

	/// Sélectionne tous les points contenus dans `rect`. Le rectangle est
	/// normalisé localement (pas de mutation de l'argument).
	std::vector<uint32_t> SelectInRect(const std::vector<SelectablePoint>& points, SelectionRect rect);

	/// Sélectionne tous les points contenus dans le polygone lasso `polygon`
	/// (suite de sommets X/Z, fermeture implicite dernier→premier). Test
	/// d'inclusion par ray-casting (règle pair/impair). Un polygone de moins de
	/// 3 sommets ne sélectionne rien.
	/// \param polygon sommets {x,z} entrelacés dans l'ordre de tracé.
	std::vector<uint32_t> SelectInLasso(const std::vector<SelectablePoint>& points,
		const std::vector<std::pair<float, float>>& polygon);
}
