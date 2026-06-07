#pragma once
// Petite boussole HUD (ImGui) pour le client de jeu.
//
// Rôle : afficher le cap de la caméra. Le HAUT de la boussole = direction
// regardée par la caméra ; une AIGUILLE rouge indique toujours le NORD et tourne
// au fur et à mesure que le joueur pivote/se déplace. Les lettres N/E/S/O
// tournent de même.
//
// Effet de bord : dessine via ImGui::GetForegroundDrawList() (overlay, ne capture
// pas la souris, n'ouvre pas de fenêtre interactive).
// Contrainte : doit être appelée DANS une frame ImGui (après ImGui::NewFrame(),
// avant ImGui::Render()), en main thread.

#include <cmath>

#include "imgui.h"

namespace engine::render
{
	/// Dessine la boussole HUD (aiguille pointant le Nord).
	/// \param camFwdX,camFwdZ  Forward caméra projeté sur le plan horizontal (XZ), non normalisé OK.
	/// \param screenW,screenH  Dimensions de la surface (px), pour positionner la boussole.
	/// \param radiusPx         Rayon de la boussole en pixels (petite par défaut).
	inline void DrawCompassHud(float camFwdX, float camFwdZ,
	                           float screenW, float /*screenH*/,
	                           float radiusPx = 46.0f)
	{
		// Cap de la caméra (radians). atan2(x, z) : +Z = "Nord".
		const float heading = std::atan2(camFwdX, camFwdZ);
		const float kPi = 3.14159265358979f;

		// Centre : en haut, légèrement décalé du bord (déplaçable plus tard).
		const ImVec2 center(screenW * 0.5f, radiusPx + 24.0f);
		ImDrawList* dl = ImGui::GetForegroundDrawList();

		// Disque de fond + anneau.
		dl->AddCircleFilled(center, radiusPx + 4.0f, IM_COL32(10, 12, 18, 150), 48);
		dl->AddCircle(center, radiusPx, IM_COL32(220, 225, 235, 220), 48, 2.0f);

		// Place un point d'azimut monde sur la boussole : le HAUT = cap caméra.
		// Position écran (y vers le bas) : up = -Y. phi = bearing - heading.
		auto place = [&](float bearing, float r) -> ImVec2
		{
			const float phi = bearing - heading;
			return ImVec2(center.x + r * std::sin(phi), center.y - r * std::cos(phi));
		};

		// Lettres cardinales (N=+Z, E=+X, S=-Z, O=-X), tournent avec le cap.
		struct Card { float bearing; const char* label; };
		const Card cards[4] = {
			{ 0.0f, "N" }, { kPi * 0.5f, "E" }, { kPi, "S" }, { -kPi * 0.5f, "O" }
		};
		for (const Card& c : cards)
		{
			const ImVec2 p = place(c.bearing, radiusPx - 11.0f);
			const ImVec2 ts = ImGui::CalcTextSize(c.label);
			dl->AddText(ImVec2(p.x - ts.x * 0.5f, p.y - ts.y * 0.5f),
			            IM_COL32(235, 238, 245, 235), c.label);
		}

		// Index fixe "direction regardée" : petit repère au sommet de l'anneau.
		dl->AddTriangleFilled(
			ImVec2(center.x, center.y - radiusPx - 7.0f),
			ImVec2(center.x - 5.0f, center.y - radiusPx + 2.0f),
			ImVec2(center.x + 5.0f, center.y - radiusPx + 2.0f),
			IM_COL32(235, 238, 245, 235));

		// AIGUILLE NORD : pointe rouge vers le Nord (tourne avec le cap), queue grise.
		const ImVec2 north = place(0.0f, radiusPx - 6.0f);
		const ImVec2 south = place(kPi, radiusPx - 6.0f);
		// Base de l'aiguille (perpendiculaire) pour un losange fin.
		const float perp = heading; // direction "droite" sur la boussole = est local
		const ImVec2 sideA(center.x + 5.0f * std::cos(perp), center.y + 5.0f * std::sin(perp));
		const ImVec2 sideB(center.x - 5.0f * std::cos(perp), center.y - 5.0f * std::sin(perp));
		// Queue grise (vers le sud).
		dl->AddTriangleFilled(sideA, sideB, south, IM_COL32(150, 155, 165, 235));
		// Pointe rouge (vers le nord).
		dl->AddTriangleFilled(sideA, sideB, north, IM_COL32(230, 60, 60, 255));
		// Moyeu central.
		dl->AddCircleFilled(center, 3.0f, IM_COL32(245, 248, 252, 255), 12);
	}
}
