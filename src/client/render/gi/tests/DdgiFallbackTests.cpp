/**
 * M45.8 : tests unitaires CPU PURS pour les niveaux de qualité / fallback DDGI
 * (DdgiQuality). Aucune dépendance Vulkan : on ne teste QUE le parsing de la
 * config et la résolution en paramètres effectifs.
 *
 * Convention identique à DdgiVolumeTests.cpp / ProtocolV1Tests.cpp : main()
 * renvoie 0 si tout passe, non-zero (+ message sur stderr) au premier échec.
 *
 * Garantie centrale couverte ici : "off" ET "static-probes" => dynamic==false
 * (pas de DDGI dynamique => rendu probes statiques inchangé).
 */

#include "src/client/render/gi/DdgiQuality.h"

#include <cstdint>
#include <iostream>

namespace
{
	static int s_failCount = 0;

	void Assert(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}
}

using engine::render::gi::DdgiQuality;
using engine::render::gi::DdgiQualitySettings;
using engine::render::gi::ParseDdgiQuality;
using engine::render::gi::ResolveDdgiQuality;

static bool TestParse()
{
	// Valeurs canoniques.
	Assert(ParseDdgiQuality("off") == DdgiQuality::Off, "parse off");
	Assert(ParseDdgiQuality("static") == DdgiQuality::StaticProbes, "parse static");
	Assert(ParseDdgiQuality("static-probes") == DdgiQuality::StaticProbes, "parse static-probes");
	Assert(ParseDdgiQuality("dynamic-low") == DdgiQuality::DynamicLow, "parse dynamic-low");
	Assert(ParseDdgiQuality("dynamic-high") == DdgiQuality::DynamicHigh, "parse dynamic-high");

	// Insensible à la casse.
	Assert(ParseDdgiQuality("OFF") == DdgiQuality::Off, "parse OFF (casse)");
	Assert(ParseDdgiQuality("Static-Probes") == DdgiQuality::StaticProbes, "parse Static-Probes (casse)");
	Assert(ParseDdgiQuality("DYNAMIC-HIGH") == DdgiQuality::DynamicHigh, "parse DYNAMIC-HIGH (casse)");

	// Inconnu / vide -> Off (sécurité : pas d'activation accidentelle).
	Assert(ParseDdgiQuality("") == DdgiQuality::Off, "parse vide -> Off");
	Assert(ParseDdgiQuality("bidon") == DdgiQuality::Off, "parse inconnu -> Off");
	return s_failCount == 0;
}

static bool TestResolveFallback()
{
	// GARANTIE FALLBACK : Off et StaticProbes => dynamic == false.
	const DdgiQualitySettings off = ResolveDdgiQuality(DdgiQuality::Off);
	Assert(off.dynamic == false, "Off => dynamic false (rendu statique inchange)");
	Assert(off.intensity == 0.0f, "Off => intensity 0");

	const DdgiQualitySettings stat = ResolveDdgiQuality(DdgiQuality::StaticProbes);
	Assert(stat.dynamic == false, "StaticProbes => dynamic false (rendu statique inchange)");
	Assert(stat.intensity == 0.0f, "StaticProbes => intensity 0");
	return s_failCount == 0;
}

static bool TestResolveDynamic()
{
	const DdgiQualitySettings low = ResolveDdgiQuality(DdgiQuality::DynamicLow);
	Assert(low.dynamic == true, "DynamicLow => dynamic true");
	Assert(low.updateDivisor == 8u, "DynamicLow => divisor 8");
	Assert(low.intensity == 0.5f, "DynamicLow => intensity 0.5");

	const DdgiQualitySettings high = ResolveDdgiQuality(DdgiQuality::DynamicHigh);
	Assert(high.dynamic == true, "DynamicHigh => dynamic true");
	Assert(high.updateDivisor == 2u, "DynamicHigh => divisor 2");
	Assert(high.intensity == 1.0f, "DynamicHigh => intensity 1.0");
	return s_failCount == 0;
}

static bool TestDivisorNonZero()
{
	// Cohérence : updateDivisor toujours >= 1 (sinon division par zéro shader).
	const DdgiQuality all[] = {
		DdgiQuality::Off, DdgiQuality::StaticProbes,
		DdgiQuality::DynamicLow, DdgiQuality::DynamicHigh
	};
	for (DdgiQuality q : all)
	{
		const DdgiQualitySettings s = ResolveDdgiQuality(q);
		Assert(s.updateDivisor >= 1u, "updateDivisor >= 1 (anti division par zero)");
	}
	return s_failCount == 0;
}

int main()
{
	TestParse();
	TestResolveFallback();
	TestResolveDynamic();
	TestDivisorNonZero();
	if (s_failCount != 0)
	{
		std::cerr << "Total echecs : " << s_failCount << std::endl;
		return 1;
	}
	std::cout << "DdgiFallback tests : tout est passe." << std::endl;
	return 0;
}
