// Tests directions soleil/lune du DayNightCycle (Partie A) + invariants nuit
// (Partie B). Pure logique CPU — pas de Vulkan. Retourne 0 si OK.

#include "src/client/render/DayNightCycle.h"

#include <cmath>
#include <cstdio>

namespace
{
	int s_fail = 0;
	void Check(bool cond, const char* msg)
	{
		if (!cond) { ++s_fail; std::fprintf(stderr, "[FAIL] %s\n", msg); }
	}
	float Dot(const float a[3], const float b[3])
	{
		return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
	}
}

int main()
{
	using engine::render::DayNightCycle;
	DayNightCycle dn;
	DayNightCycle::Params p;
	p.timeScale = 60.0f;
	dn.Init(p);

	// --- Midi : soleil quasi au zénith, lune quasi au nadir ---
	dn.SetTime(12.0f);
	{
		const auto& s = dn.GetState();
		Check(s.isDaytime, "midi: isDaytime");
		Check(s.sunDir[1] > 0.99f, "midi: sunDir pointe quasi droit en haut (pas de cos(sin) bug)");
		Check(s.moonDir[1] < -0.90f, "midi: moonDir sous l'horizon");
	}

	// --- Minuit : soleil sous l'horizon, lune au-dessus ---
	dn.SetTime(0.0f);
	{
		const auto& s = dn.GetState();
		Check(!s.isDaytime, "minuit: nuit");
		Check(s.sunDir[1] < -0.90f, "minuit: sunDir sous l'horizon");
		Check(s.moonDir[1] > 0.90f, "minuit: moonDir au-dessus de l'horizon");
	}

	// --- Soleil et lune opposés à toute heure ---
	for (float t = 0.0f; t < 24.0f; t += 3.0f)
	{
		dn.SetTime(t);
		const auto& s = dn.GetState();
		// Invariant robuste : moonElev = -sunElev (décalage 12 h) ⇒ les composantes
		// verticales sont exactement opposées, quel que soit l'azimut. (Le modèle
		// d'azimut simplifié ne garantit PAS l'opposition 3D complète hors heures
		// cardinales — d'où l'invariant sur la seule élévation.)
		Check(std::fabs(s.sunDir[1] + s.moonDir[1]) < 1e-3f, "elevations soleil/lune opposees");
		const float lenSun = std::sqrt(Dot(s.sunDir, s.sunDir));
		Check(std::fabs(lenSun - 1.0f) < 1e-3f, "sunDir normalise");
		const float lenMoon = std::sqrt(Dot(s.moonDir, s.moonDir));
		Check(std::fabs(lenMoon - 1.0f) < 1e-3f, "moonDir normalise");
	}

	// --- Nuit sombre mais pas noire, et moins claire que le jour ---
	auto luminance = [](const float c[3]) { return 0.2126f*c[0] + 0.7152f*c[1] + 0.0722f*c[2]; };
	dn.SetTime(0.0f);
	const float ambNight = luminance(dn.GetState().ambientColor);
	const float zenNight = luminance(dn.GetState().skyZenith);
	dn.SetTime(12.0f);
	const float ambDay = luminance(dn.GetState().ambientColor);
	Check(ambNight > 0.0f, "nuit: ambiant non nul (navigable)");
	Check(ambNight < ambDay, "nuit: ambiant plus sombre que le jour");
	Check(zenNight < 0.02f, "nuit: zenith ciel sombre");

	return s_fail == 0 ? 0 : 1;
}
