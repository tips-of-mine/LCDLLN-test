// Tests du helper pur QuestUi WorldToRadarUv — projection d'une position
// monde (plan XZ) vers les coordonnées UV du radar minimap SP3, centré sur
// le joueur. Fonction libre, sans dépendance ImGui ni Config, donc testable
// directement (voir doc dans QuestUi.h).

#include "src/client/quest/QuestUi.h"

#include <cmath>
#include <iostream>

using engine::client::ShouldShowQuestInJournal;
using engine::client::WorldToRadarUv;
using engine::client::ClampZoomIndex;
using engine::client::StepZoomIndex;
using engine::client::RadiusForZoomIndex;
using engine::client::ComputeRadarScreenRect;
using engine::client::RadarZoomTickPos;
using engine::client::RadarScreenRect;
using engine::client::ScreenPoint;

namespace
{
	int g_failures = 0;

	void Check(bool condition, const char* label)
	{
		if (!condition)
		{
			std::cerr << "[FAIL] " << label << "\n";
			++g_failures;
		}
	}

	bool NearlyEqual(float a, float b, float epsilon = 0.0001f)
	{
		return std::abs(a - b) <= epsilon;
	}
}

int main()
{
	// Joueur en (0,0), rayon 60 : POI au centre exact -> (0.5,0.5), pas off-radar.
	{
		float u = 0.0f;
		float v = 0.0f;
		bool offRadar = true;
		WorldToRadarUv(0.0f, 0.0f, 0.0f, 0.0f, 60.0f, u, v, offRadar);
		Check(NearlyEqual(u, 0.5f), "POI(0,0) -> u==0.5");
		Check(NearlyEqual(v, 0.5f), "POI(0,0) -> v==0.5");
		Check(!offRadar, "POI(0,0) -> pas off-radar");
	}

	// Joueur en (0,0), rayon 60 : POI (30,0) -> u=0.75, pas off-radar.
	{
		float u = 0.0f;
		float v = 0.0f;
		bool offRadar = true;
		WorldToRadarUv(30.0f, 0.0f, 0.0f, 0.0f, 60.0f, u, v, offRadar);
		Check(NearlyEqual(u, 0.75f), "POI(30,0) -> u==0.75");
		Check(NearlyEqual(v, 0.5f), "POI(30,0) -> v==0.5");
		Check(!offRadar, "POI(30,0) -> pas off-radar");
	}

	// Joueur en (0,0), rayon 60 : POI (120,0) -> off-radar, u clampé <= 1.
	{
		float u = 0.0f;
		float v = 0.0f;
		bool offRadar = false;
		WorldToRadarUv(120.0f, 0.0f, 0.0f, 0.0f, 60.0f, u, v, offRadar);
		Check(offRadar, "POI(120,0) -> off-radar");
		Check(u <= 1.0f, "POI(120,0) -> u clampe <= 1");
		Check(NearlyEqual(u, 1.0f), "POI(120,0) -> u clampe == 1 (borne du cadre)");
	}

	// Joueur décentré (10,-5) : le POI au même point que le joueur reste au centre du radar.
	{
		float u = 0.0f;
		float v = 0.0f;
		bool offRadar = true;
		WorldToRadarUv(10.0f, -5.0f, 10.0f, -5.0f, 60.0f, u, v, offRadar);
		Check(NearlyEqual(u, 0.5f), "POI==joueur decentre -> u==0.5");
		Check(NearlyEqual(v, 0.5f), "POI==joueur decentre -> v==0.5");
		Check(!offRadar, "POI==joueur decentre -> pas off-radar");
	}

	// Garde radiusM <= 0 : pas de division par zéro, sort au centre en off-radar.
	{
		float u = 0.0f;
		float v = 0.0f;
		bool offRadar = false;
		WorldToRadarUv(5.0f, 5.0f, 0.0f, 0.0f, 0.0f, u, v, offRadar);
		Check(NearlyEqual(u, 0.5f), "radiusM<=0 -> u==0.5 (garde)");
		Check(NearlyEqual(v, 0.5f), "radiusM<=0 -> v==0.5 (garde)");
		Check(offRadar, "radiusM<=0 -> off-radar force a true");
	}

	// Filtre du journal : seules les quêtes ACCEPTÉES (Active=2, ReadyToTurnIn=3)
	// y apparaissent. Une quête juste proposée (Offered=1) ou verrouillée (Locked=0)
	// n'y figure PAS — le joueur doit l'accepter chez le PNJ d'abord. Completed (4)
	// (rendue) en sort aussi.
	{
		Check(!ShouldShowQuestInJournal(0u), "Locked(0) -> pas dans le journal");
		Check(!ShouldShowQuestInJournal(1u), "Offered(1) -> pas dans le journal (dispo PNJ seulement)");
		Check(ShouldShowQuestInJournal(2u), "Active(2) -> dans le journal");
		Check(ShouldShowQuestInJournal(3u), "ReadyToTurnIn(3) -> dans le journal");
		Check(!ShouldShowQuestInJournal(4u), "Completed(4) -> pas dans le journal (rendue)");
	}

	// --- Contrôle de zoom du radar (helpers purs) ---
	// ClampZoomIndex borne [0, 4].
	Check(ClampZoomIndex(-3) == 0, "ClampZoomIndex(-3)==0");
	Check(ClampZoomIndex(0) == 0, "ClampZoomIndex(0)==0");
	Check(ClampZoomIndex(4) == 4, "ClampZoomIndex(4)==4");
	Check(ClampZoomIndex(9) == 4, "ClampZoomIndex(9)==4 (clamp haut)");

	// RadiusForZoomIndex : 0->200 … 4->1000 (clamp hors borne).
	Check(NearlyEqual(RadiusForZoomIndex(0), 200.0f), "zoom 0 -> 200 m");
	Check(NearlyEqual(RadiusForZoomIndex(2), 600.0f), "zoom 2 -> 600 m (defaut)");
	Check(NearlyEqual(RadiusForZoomIndex(4), 1000.0f), "zoom 4 -> 1000 m");
	Check(NearlyEqual(RadiusForZoomIndex(99), 1000.0f), "zoom hors borne -> 1000 m (clamp)");

	// StepZoomIndex : molette haut (>0) = zoom IN = index decroit ; clamp.
	Check(StepZoomIndex(2, 1) == 1, "step +1 depuis 2 -> 1 (zoom in)");
	Check(StepZoomIndex(2, -1) == 3, "step -1 depuis 2 -> 3 (zoom out)");
	Check(StepZoomIndex(0, 1) == 0, "step +1 depuis 0 -> 0 (clamp min)");
	Check(StepZoomIndex(4, -1) == 4, "step -1 depuis 4 -> 4 (clamp max)");
	Check(StepZoomIndex(4, 2) == 2, "step +2 depuis 4 -> 2 (multi-cran)");

	// ComputeRadarScreenRect : coin haut-droit, sous le degagement HUD.
	{
		engine::core::Config cfg;
		cfg.SetDefault("client.quest.minimap.enabled", true);
		cfg.SetDefault("client.quest.minimap.size_px", static_cast<int64_t>(200));
		const RadarScreenRect r = ComputeRadarScreenRect(cfg, 1280.0f, 720.0f);
		Check(r.enabled, "radar rect enabled");
		Check(NearlyEqual(r.size, 200.0f), "radar size == size_px");
		Check(NearlyEqual(r.x0, 1280.0f - 200.0f - 16.0f), "radar x0 = W - size - marge");
		Check(NearlyEqual(r.y0, 16.0f + 116.0f + 8.0f), "radar y0 = marge + degagement + 8");
	}
	{
		engine::core::Config cfg;
		cfg.SetDefault("client.quest.minimap.enabled", false);
		cfg.SetDefault("client.quest.minimap.size_px", static_cast<int64_t>(200));
		const RadarScreenRect r = ComputeRadarScreenRect(cfg, 1280.0f, 720.0f);
		Check(!r.enabled, "radar desactive -> rect non enabled");
	}

	// RadarZoomTickPos : repères sur la moitié haute, symétriques, au-dessus du centre.
	{
		RadarScreenRect r;
		r.enabled = true;
		r.x0 = 100.0f;
		r.y0 = 100.0f;
		r.size = 200.0f;
		const float cx = r.x0 + r.size * 0.5f; // 200
		const float cy = r.y0 + r.size * 0.5f; // 200
		const ScreenPoint t0 = RadarZoomTickPos(r, 0); // gauche
		const ScreenPoint t2 = RadarZoomTickPos(r, 2); // haut-centre
		const ScreenPoint t4 = RadarZoomTickPos(r, 4); // droite
		Check(t0.x < cx, "repere 0 a gauche du centre");
		Check(t4.x > cx, "repere 4 a droite du centre");
		Check(NearlyEqual(t2.x, cx, 0.01f), "repere 2 centre en x (haut)");
		Check(t0.y < cy && t2.y < cy && t4.y < cy, "tous les reperes au-dessus du centre");
		Check(NearlyEqual(t0.x - cx, -(t4.x - cx), 0.01f), "reperes 0 et 4 symetriques en x");
	}

	if (g_failures != 0)
	{
		std::cerr << g_failures << " assertion(s) échouée(s)\n";
		return 1;
	}
	std::cout << "OK\n";
	return 0;
}
