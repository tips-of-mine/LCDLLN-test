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
		Check(Dot(s.sunDir, s.moonDir) < -0.95f, "soleil/lune opposes");
		const float lenSun = std::sqrt(Dot(s.sunDir, s.sunDir));
		Check(std::fabs(lenSun - 1.0f) < 1e-3f, "sunDir normalise");
	}

	return s_fail == 0 ? 0 : 1;
}
