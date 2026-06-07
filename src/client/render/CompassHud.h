#pragma once
// Petite boussole HUD (ImGui) pour le client de jeu.
//
// Rôle : afficher le cap de la caméra (N/E/S/O) et surtout un marqueur SOLEIL,
// pour aider à valider visuellement quand le joueur regarde vers le soleil
// (le marqueur soleil arrive en HAUT de la boussole quand la caméra est alignée
// avec le soleil). Le haut de la boussole = direction regardée par la caméra.
//
// La position relative du soleil est calculée à partir des vrais vecteurs monde
// (forward caméra + direction soleil), donc correcte quelle que soit la
// convention de Nord du moteur.
//
// Effet de bord : dessine via ImGui::GetForegroundDrawList() (overlay, ne capture
// pas la souris, n'ouvre pas de fenêtre interactive).
// Contrainte : doit être appelée DANS une frame ImGui (après ImGui::NewFrame(),
// avant ImGui::Render()), en main thread.

#include <cmath>

#include "imgui.h"

namespace engine::render
{
	/// Dessine la boussole HUD.
	/// \param camFwdX,camFwdZ  Forward caméra projeté sur le plan horizontal (XZ), non normalisé OK.
	/// \param sunX,sunZ        Direction VERS le soleil projetée sur XZ (non normalisée OK).
	/// \param screenW,screenH  Dimensions de la surface (px), pour positionner la boussole.
	/// \param radiusPx         Rayon de la boussole en pixels (petite par défaut).
	inline void DrawCompassHud(float camFwdX, float camFwdZ,
	                           float sunX, float sunZ,
	                           float screenW, float screenH,
	                           float radiusPx = 46.0f)
	{
		// Cap de la caméra et azimut du soleil (radians). atan2(x, z) : +Z = "Nord".
		const float heading = std::atan2(camFwdX, camFwdZ);
		const float sunAz   = std::atan2(sunX, sunZ);

		// Centre : en haut, légèrement décalé du bord (déplaçable plus tard).
		const ImVec2 center(screenW * 0.5f, radiusPx + 24.0f);
		ImDrawList* dl = ImGui::GetForegroundDrawList();

		// Disque de fond + anneau.
		dl->AddCircleFilled(center, radiusPx + 4.0f, IM_COL32(10, 12, 18, 150), 48);
		dl->AddCircle(center, radiusPx, IM_COL32(220, 225, 235, 220), 48, 2.0f);

		// Place un point cardinal (bearing monde) sur la boussole : le HAUT = cap caméra.
		// Position écran (y vers le bas) : up = -Y. phi = bearing - heading.
		auto place = [&](float bearing, float r) -> ImVec2
		{
			const float phi = bearing - heading;
			return ImVec2(center.x + r * std::sin(phi), center.y - r * std::cos(phi));
		};

		// Lettres cardinales (N=+Z, E=+X, S=-Z, O=-X).
		const float kPi = 3.14159265358979f;
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

		// Repère "direction regardée" : petit triangle fixe au sommet.
		dl->AddTriangleFilled(
			ImVec2(center.x, center.y - radiusPx - 7.0f),
			ImVec2(center.x - 5.0f, center.y - radiusPx + 2.0f),
			ImVec2(center.x + 5.0f, center.y - radiusPx + 2.0f),
			IM_COL32(255, 90, 90, 240));

		// Marqueur SOLEIL : disque jaune. En HAUT de la boussole = caméra alignée au soleil.
		const ImVec2 sp = place(sunAz, radiusPx - 4.0f);
		dl->AddCircleFilled(sp, 6.0f, IM_COL32(255, 220, 60, 255), 16);
		dl->AddCircle(sp, 6.0f, IM_COL32(120, 90, 0, 255), 16, 1.5f);

		// Angle caméra↔soleil (0° = pile vers le soleil), aide au diagnostic nuages.
		float rel = sunAz - heading;
		while (rel >  kPi) rel -= 2.0f * kPi;
		while (rel < -kPi) rel += 2.0f * kPi;
		const int deg = static_cast<int>(std::abs(rel) * 180.0f / kPi + 0.5f);
		char buf[32];
		std::snprintf(buf, sizeof(buf), "soleil %d", deg);
		const ImVec2 lts = ImGui::CalcTextSize(buf);
		dl->AddText(ImVec2(center.x - lts.x * 0.5f, center.y + radiusPx + 6.0f),
		            IM_COL32(255, 220, 60, 235), buf);
	}
}
