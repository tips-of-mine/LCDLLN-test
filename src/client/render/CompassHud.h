#pragma once
// Petite boussole HUD (ImGui) pour le client de jeu + arc temporel.
//
// Rôle :
//  - Boussole : le HAUT = direction regardée par la caméra ; une AIGUILLE rouge
//    indique toujours le NORD et tourne quand le joueur pivote. Lettres N/E/S/O.
//  - Arc temporel (demi-cercle au-dessus) : dégradé nuit↔jour avec un marqueur
//    (soleil le jour / lune la nuit) qui balaie l'arc selon l'heure du monde
//    (minuit à gauche → midi en haut → minuit à droite). Permet au joueur de
//    lire le moment de la journée.
//
// Effet de bord : dessine via ImGui::GetForegroundDrawList() (overlay, ne capture
// pas la souris, n'ouvre pas de fenêtre interactive).
// Contrainte : appeler DANS une frame ImGui (après NewFrame, avant Render), main thread.

#include <cmath>

#include "imgui.h"

namespace engine::render
{
	namespace detail
	{
		/// Couleur de l'arc temporel pour une heure donnée [0,24).
		inline ImU32 CompassTimeColor(float h)
		{
			if (h >= 6.0f && h < 8.0f)   return IM_COL32(235, 150, 70, 255);  // aube
			if (h >= 8.0f && h < 17.0f)  return IM_COL32(120, 180, 235, 255); // jour
			if (h >= 17.0f && h < 19.5f) return IM_COL32(235, 120, 55, 255);  // crépuscule
			return IM_COL32(34, 40, 80, 255);                                 // nuit
		}
	}

	/// Dessine la boussole HUD (aiguille Nord) + l'arc temporel.
	/// \param camFwdX,camFwdZ  Forward caméra projeté sur XZ (non normalisé OK).
	/// \param timeOfDay        Heure du monde [0,24).
	/// \param isDaytime        true = soleil levé (marqueur soleil), sinon lune.
	/// \param screenW          Largeur de la surface (px), pour centrer en haut.
	/// \param radiusPx         Rayon de la boussole (petite par défaut).
	inline void DrawCompassHud(float camFwdX, float camFwdZ,
	                           float timeOfDay, bool isDaytime,
	                           float /*screenW*/, float /*screenH*/,
	                           float radiusPx = 44.0f)
	{
		const float kPi = 3.14159265358979f;
		const float heading = std::atan2(camFwdX, camFwdZ);

		// On positionne avec la VRAIE taille d'affichage ImGui (et non l'extent
		// swapchain passé par l'appelant), sinon sur écran large/ultrawide ou DPI
		// scalé la boussole peut tomber hors écran. Coin HAUT-DROITE.
		// Déplaçable : ajuster center.x / center.y ci-dessous.
		const float dispW = ImGui::GetIO().DisplaySize.x;
		const float arcR = radiusPx + 16.0f;
		const ImVec2 center(dispW - arcR - 16.0f, arcR + 14.0f);
		ImDrawList* dl = ImGui::GetForegroundDrawList();

		// ---- Arc temporel (demi-cercle supérieur, θ de π=gauche à 0=droite) ----
		// t = heure/24 ; θ = π(1 - t). Minuit gauche, midi haut, minuit droite.
		const int kSeg = 48;
		for (int i = 0; i < kSeg; ++i)
		{
			const float t0 = static_cast<float>(i) / kSeg;
			const float t1 = static_cast<float>(i + 1) / kSeg;
			const float a0 = kPi * (1.0f - t0);
			const float a1 = kPi * (1.0f - t1);
			const ImVec2 p0(center.x + arcR * std::cos(a0), center.y - arcR * std::sin(a0));
			const ImVec2 p1(center.x + arcR * std::cos(a1), center.y - arcR * std::sin(a1));
			dl->AddLine(p0, p1, detail::CompassTimeColor(t0 * 24.0f), 7.0f);
		}
		// Repère "midi" fixe au sommet de l'arc.
		dl->AddTriangleFilled(
			ImVec2(center.x, center.y - arcR - 9.0f),
			ImVec2(center.x - 5.0f, center.y - arcR - 1.0f),
			ImVec2(center.x + 5.0f, center.y - arcR - 1.0f),
			IM_COL32(220, 190, 90, 255));

		// Marqueur temporel (soleil/lune) à la position de l'heure courante.
		{
			const float t = (timeOfDay < 0.0f ? 0.0f : (timeOfDay >= 24.0f ? 0.0f : timeOfDay)) / 24.0f;
			const float a = kPi * (1.0f - t);
			const ImVec2 mp(center.x + arcR * std::cos(a), center.y - arcR * std::sin(a));
			if (isDaytime)
			{
				dl->AddCircleFilled(mp, 6.0f, IM_COL32(255, 225, 70, 255), 16);
				for (int k = 0; k < 8; ++k) // petits rayons
				{
					const float ra = k * (kPi / 4.0f);
					const ImVec2 r0(mp.x + 7.0f * std::cos(ra), mp.y + 7.0f * std::sin(ra));
					const ImVec2 r1(mp.x + 10.0f * std::cos(ra), mp.y + 10.0f * std::sin(ra));
					dl->AddLine(r0, r1, IM_COL32(255, 225, 70, 255), 1.5f);
				}
			}
			else
			{
				dl->AddCircleFilled(mp, 5.5f, IM_COL32(225, 230, 245, 255), 16);
				dl->AddCircleFilled(ImVec2(mp.x + 2.2f, mp.y - 1.5f), 4.5f,
				                    IM_COL32(34, 40, 80, 255), 16); // croissant
			}
		}

		// ---- Boussole ----
		dl->AddCircleFilled(center, radiusPx + 4.0f, IM_COL32(10, 12, 18, 160), 48);
		dl->AddCircle(center, radiusPx, IM_COL32(220, 225, 235, 220), 48, 2.0f);

		// place : le HAUT = cap caméra. phi = bearing - heading ; up = -Y.
		auto place = [&](float bearing, float r) -> ImVec2
		{
			const float phi = bearing - heading;
			return ImVec2(center.x + r * std::sin(phi), center.y - r * std::cos(phi));
		};

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

		// Aiguille : pointe rouge vers le Nord (tourne avec le cap), queue grise.
		const ImVec2 north = place(0.0f, radiusPx - 6.0f);
		const ImVec2 south = place(kPi, radiusPx - 6.0f);
		// Base PERPENDICULAIRE à l'axe N-S de l'aiguille (axe = (-sin h, -cos h) ;
		// perpendiculaire = (cos h, -sin h)). Sinon l'épaisseur apparente de
		// l'aiguille varie avec l'orientation de la caméra.
		const ImVec2 sideA(center.x + 5.0f * std::cos(heading), center.y - 5.0f * std::sin(heading));
		const ImVec2 sideB(center.x - 5.0f * std::cos(heading), center.y + 5.0f * std::sin(heading));
		dl->AddTriangleFilled(sideA, sideB, south, IM_COL32(150, 155, 165, 235));
		dl->AddTriangleFilled(sideA, sideB, north, IM_COL32(230, 60, 60, 255));
		dl->AddCircleFilled(center, 3.0f, IM_COL32(245, 248, 252, 255), 12);
	}
}
