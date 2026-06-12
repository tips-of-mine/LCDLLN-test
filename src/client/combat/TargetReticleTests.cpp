// Tests unitaires du réticule de ciblage au sol (TargetReticleGeometry) :
// paramétrage config, géométrie analytique (cercles, arc 120°, rayons aux
// bornes ±60°, rotation yaw), bake de texture et cycle de fondu.
// Logique pure CPU — aucune dépendance Vulkan (cf. lcdlln_add_simple_test).

// Les asserts doivent rester actifs même en build Release (piège NDEBUG).
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "src/client/combat/TargetReticleGeometry.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using engine::client::BakeReticleRgba;
using engine::client::EvaluateReticleAt;
using engine::client::ParseHexColorRgba;
using engine::client::ReticleCoverage;
using engine::client::ReticleHalfExtentMeters;
using engine::client::ReticleOuterRadiusMeters;
using engine::client::TargetReticleFade;
using engine::client::TargetReticleParams;
using engine::client::WorldOffsetToReticleLocal;

namespace
{
	constexpr float kPi = 3.14159265358979323846f;
	constexpr float kDegToRad = kPi / 180.0f;

	bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

	/// Évalue la couverture au point monde (centre + offset) pour un yaw donné,
	/// en passant par la même rotation que le shader (miroir CPU).
	ReticleCoverage EvalWorld(const TargetReticleParams& p, float yaw, float worldDx, float worldDz)
	{
		float lx = 0.0f, lz = 0.0f;
		WorldOffsetToReticleLocal(yaw, worldDx, worldDz, lx, lz);
		return EvaluateReticleAt(p, lx, lz, 0.0f);
	}
}

// Défauts (config vide) : rayon proposé 1.8 m, arc 120°, R_out = 1.11 × R_in,
// couleurs claire #CFE3F2 et foncée #5A87A8, fondus > 0.
static void TestDefaultParams()
{
	engine::core::Config cfg;
	const TargetReticleParams p = TargetReticleParams::FromConfig(cfg);

	assert(Near(p.radiusMeters, 1.8f));
	assert(Near(p.visionArcDegrees, 120.0f));
	assert(Near(p.ringGap, 0.11f));
	assert(p.colorLightRgba == 0xCFE3F2FFu);
	assert(p.colorVisionRgba == 0x5A87A8FFu);
	assert(p.fadeInSeconds > 0.0f && p.fadeOutSeconds > 0.0f);

	// R_out > R_in, conforme au ring_gap (≈ 1.11 × R_in).
	const float rOut = ReticleOuterRadiusMeters(p);
	assert(rOut > p.radiusMeters);
	assert(Near(rOut, p.radiusMeters * 1.11f, 1e-3f));
	// Le volume de projection couvre tout le réticule (cercle fin compris).
	assert(ReticleHalfExtentMeters(p) > rOut);
	std::puts("[OK] TestDefaultParams");
}

// Les clés target_reticle.* surchargent les défauts (et sont bornées).
static void TestConfigOverrides()
{
	engine::core::Config cfg;
	const bool loaded = cfg.LoadFromString(R"({
		"target_reticle": {
			"radius_meters": 2.5,
			"ring_gap": 0.2,
			"vision_arc_degrees": 90.0,
			"color_light": "#FF0000",
			"color_vision": "11223344",
			"fade_in_seconds": 0.5,
			"fade_out_seconds": 1.0
		}
	})");
	assert(loaded);

	const TargetReticleParams p = TargetReticleParams::FromConfig(cfg);
	assert(Near(p.radiusMeters, 2.5f));
	assert(Near(p.ringGap, 0.2f));
	assert(Near(p.visionArcDegrees, 90.0f));
	assert(p.colorLightRgba == 0xFF0000FFu);   // alpha opaque implicite.
	assert(p.colorVisionRgba == 0x11223344u);  // alpha explicite, sans '#'.
	assert(Near(p.fadeInSeconds, 0.5f));
	assert(Near(p.fadeOutSeconds, 1.0f));
	assert(Near(ReticleOuterRadiusMeters(p), 2.5f * 1.2f, 1e-3f));

	// Valeurs aberrantes bornées (jamais de géométrie dégénérée).
	engine::core::Config bad;
	assert(bad.LoadFromString(R"({"target_reticle": {"radius_meters": -5.0, "vision_arc_degrees": 720.0}})"));
	const TargetReticleParams pb = TargetReticleParams::FromConfig(bad);
	assert(pb.radiusMeters > 0.0f);
	assert(pb.visionArcDegrees < 360.0f);
	std::puts("[OK] TestConfigOverrides");
}

// Parsing hex : #RRGGBB, #RRGGBBAA, sans '#', casse mixte, et fallback.
static void TestParseHexColor()
{
	assert(ParseHexColorRgba("#CFE3F2", 0u) == 0xCFE3F2FFu);
	assert(ParseHexColorRgba("#5a87a8", 0u) == 0x5A87A8FFu);
	assert(ParseHexColorRgba("11223344", 0u) == 0x11223344u);
	assert(ParseHexColorRgba("#xyz", 0xDEADBEEFu) == 0xDEADBEEFu);
	assert(ParseHexColorRgba("", 0xDEADBEEFu) == 0xDEADBEEFu);
	assert(ParseHexColorRgba("#12345", 0xDEADBEEFu) == 0xDEADBEEFu);
	std::puts("[OK] TestParseHexColor");
}

// Cercles : trait clair présent sur R_in et R_out, aire intérieure NON remplie.
static void TestCirclesAndEmptyInterior()
{
	engine::core::Config cfg;
	const TargetReticleParams p = TargetReticleParams::FromConfig(cfg);
	const float rIn = p.radiusMeters;
	const float rOut = ReticleOuterRadiusMeters(p);

	// Sur le grand cercle, côté ARRIÈRE (angle 180°, hors secteur) : clair pur.
	{
		const ReticleCoverage c = EvaluateReticleAt(p, 0.0f, -rIn);
		assert(c.light > 0.9f);
		assert(Near(c.vision, 0.0f));
	}
	// Sur le grand cercle, plein AVANT (angle 0°) : clair (l'arc foncé est sur
	// R_out, pas sur R_in ; les rayons sont à ±60°).
	{
		const ReticleCoverage c = EvaluateReticleAt(p, 0.0f, rIn);
		assert(c.light > 0.9f);
		assert(Near(c.vision, 0.0f));
	}
	// Sur le petit cercle extérieur, côté arrière : clair (trait fin).
	{
		const ReticleCoverage c = EvaluateReticleAt(p, 0.0f, -rOut);
		assert(c.light > 0.9f);
		assert(Near(c.vision, 0.0f));
	}
	// Aire du secteur NON remplie : point à mi-rayon plein avant (loin des
	// rayons à ±60° et des traits) → totalement transparent.
	{
		const ReticleCoverage c = EvaluateReticleAt(p, 0.0f, rIn * 0.5f);
		assert(Near(c.light, 0.0f));
		assert(Near(c.vision, 0.0f));
	}
	// Aire arrière (hors secteur), mi-rayon : transparent aussi.
	{
		const ReticleCoverage c = EvaluateReticleAt(p, 0.0f, -rIn * 0.5f);
		assert(Near(c.light, 0.0f));
		assert(Near(c.vision, 0.0f));
	}
	std::puts("[OK] TestCirclesAndEmptyInterior");
}

// Arc de vision : présent sur R_out dans ±60° (défaut 120°), absent au-delà
// (l'arc foncé prime sur le cercle clair, qui est creusé dans le secteur).
static void TestVisionArcBounds()
{
	engine::core::Config cfg;
	const TargetReticleParams p = TargetReticleParams::FromConfig(cfg);
	const float rOut = ReticleOuterRadiusMeters(p);

	const auto atAngle = [&](float deg) -> ReticleCoverage
	{
		const float a = deg * kDegToRad;
		return EvaluateReticleAt(p, rOut * std::sin(a), rOut * std::cos(a));
	};

	// Plein avant (0°) et juste à l'intérieur des bornes (±59°) : foncé,
	// et le clair est creusé (aucune double épaisseur à la jonction).
	for (float deg : { 0.0f, 59.0f, -59.0f })
	{
		const ReticleCoverage c = atAngle(deg);
		assert(c.vision > 0.9f);
		assert(Near(c.light, 0.0f));
	}
	// Nettement hors secteur (±65°, au-delà de l'épaisseur des rayons) :
	// plus de foncé, le trait clair du cercle fin réapparaît.
	for (float deg : { 65.0f, -65.0f, 180.0f })
	{
		const ReticleCoverage c = atAngle(deg);
		assert(Near(c.vision, 0.0f));
		assert(c.light > 0.9f);
	}
	std::puts("[OK] TestVisionArcBounds");
}

// Rayons : segments foncés du centre à l'anneau, exactement aux bornes ±60°
// — et continuité : couverts du centre (t≈0) jusqu'à l'arc (t≈R_out).
static void TestVisionRays()
{
	engine::core::Config cfg;
	const TargetReticleParams p = TargetReticleParams::FromConfig(cfg);
	const float rOut = ReticleOuterRadiusMeters(p);
	const float halfArc = 0.5f * p.visionArcDegrees * kDegToRad; // 60°.

	for (float sign : { -1.0f, 1.0f })
	{
		const float dirX = std::sin(sign * halfArc);
		const float dirZ = std::cos(sign * halfArc);
		// Le long du rayon, du centre à l'anneau : foncé partout.
		for (float t : { 0.1f, 0.5f, 0.95f })
		{
			const ReticleCoverage c = EvaluateReticleAt(p, dirX * rOut * t, dirZ * rOut * t);
			assert(c.vision > 0.9f);
		}
		// Au-delà de l'anneau (1.5 × R_out, hors volume utile) : plus rien.
		{
			const ReticleCoverage c = EvaluateReticleAt(p, dirX * rOut * 1.5f, dirZ * rOut * 1.5f);
			assert(Near(c.vision, 0.0f));
		}
	}
	std::puts("[OK] TestVisionRays");
}

// Rotation yaw : pour un yaw donné, le secteur suit le regard du PNJ
// (convention jeu : regard = (sin yaw, 0, cos yaw)). Les rayons tombent aux
// angles monde yaw ± 60°, l'arrière (yaw + 180°) reste clair.
static void TestRotationFollowsYaw()
{
	engine::core::Config cfg;
	const TargetReticleParams p = TargetReticleParams::FromConfig(cfg);
	const float rOut = ReticleOuterRadiusMeters(p);
	const float halfArc = 0.5f * p.visionArcDegrees * kDegToRad;

	for (float yaw : { 0.0f, 1.1f, -2.4f, 3.0f })
	{
		// Devant le PNJ (angle monde = yaw), sur l'anneau : arc foncé.
		{
			const ReticleCoverage c = EvalWorld(p, yaw,
				rOut * std::sin(yaw), rOut * std::cos(yaw));
			assert(c.vision > 0.9f);
		}
		// Bornes du secteur (yaw ± 60°), à mi-rayon : sur les rayons foncés.
		for (float sign : { -1.0f, 1.0f })
		{
			const float a = yaw + sign * halfArc;
			const ReticleCoverage c = EvalWorld(p, yaw,
				0.5f * rOut * std::sin(a), 0.5f * rOut * std::cos(a));
			assert(c.vision > 0.9f);
		}
		// Dans le dos (yaw + 180°), sur l'anneau : clair, pas de foncé.
		{
			const float a = yaw + kPi;
			const ReticleCoverage c = EvalWorld(p, yaw,
				rOut * std::sin(a), rOut * std::cos(a));
			assert(Near(c.vision, 0.0f));
			assert(c.light > 0.9f);
		}
	}

	// Sanité de la rotation elle-même : le point devant le PNJ devient (0, r).
	{
		float lx = 0.0f, lz = 0.0f;
		const float yaw = 0.7f;
		WorldOffsetToReticleLocal(yaw, std::sin(yaw) * 2.0f, std::cos(yaw) * 2.0f, lx, lz);
		assert(Near(lx, 0.0f));
		assert(Near(lz, 2.0f));
	}
	std::puts("[OK] TestRotationFollowsYaw");
}

// Bake : buffer N²×4, texels opaques sur les traits, transparents ailleurs,
// couleurs conformes (claire sur l'arrière de l'anneau, foncée sur l'arc avant).
static void TestBakeTexture()
{
	engine::core::Config cfg;
	const TargetReticleParams p = TargetReticleParams::FromConfig(cfg);
	const uint32_t size = 128;
	std::vector<uint8_t> rgba;
	BakeReticleRgba(p, size, rgba);
	assert(rgba.size() == static_cast<size_t>(size) * size * 4u);

	const float halfExtent = ReticleHalfExtentMeters(p);
	const auto texelAt = [&](float lx, float lz) -> const uint8_t*
	{
		// Inverse de la convention du baker : lx = ((px+0.5)/N×2−1)×he.
		const int px = static_cast<int>((lx / halfExtent * 0.5f + 0.5f) * static_cast<float>(size));
		const int py = static_cast<int>((lz / halfExtent * 0.5f + 0.5f) * static_cast<float>(size));
		assert(px >= 0 && px < static_cast<int>(size));
		assert(py >= 0 && py < static_cast<int>(size));
		return &rgba[(static_cast<size_t>(py) * size + static_cast<size_t>(px)) * 4u];
	};

	// Trait clair (grand cercle, côté arrière) : opaque, couleur #CFE3F2.
	{
		const uint8_t* t = texelAt(0.0f, -p.radiusMeters);
		assert(t[3] > 200);
		assert(t[0] == 0xCF && t[1] == 0xE3 && t[2] == 0xF2);
	}
	// Arc foncé (R_out, plein avant) : opaque, couleur #5A87A8.
	{
		const uint8_t* t = texelAt(0.0f, ReticleOuterRadiusMeters(p));
		assert(t[3] > 200);
		assert(t[0] == 0x5A && t[1] == 0x87 && t[2] == 0xA8);
	}
	// Aire non remplie : mi-rayon arrière totalement transparent.
	{
		const uint8_t* t = texelAt(0.0f, -p.radiusMeters * 0.5f);
		assert(t[3] == 0);
	}
	// Coin de la texture (hors réticule) : transparent.
	assert(rgba[3] == 0);
	std::puts("[OK] TestBakeTexture");
}

// Cycle de vie du fondu : ciblage → montée vers 1 ; perte → descente vers 0
// (visible pendant le fade-out, inactif à la fin). Durées ≤ 0 = instantané.
static void TestFadeLifecycle()
{
	TargetReticleParams p{};
	p.fadeInSeconds = 0.1f;
	p.fadeOutSeconds = 0.2f;

	TargetReticleFade fade{};
	assert(!fade.IsVisible());

	// Ciblage : apparition progressive puis saturation à 1.
	fade.Update(true, p, 0.05f);
	assert(Near(fade.alpha, 0.5f));
	assert(fade.IsVisible());
	fade.Update(true, p, 0.05f);
	assert(Near(fade.alpha, 1.0f));
	fade.Update(true, p, 1.0f);
	assert(Near(fade.alpha, 1.0f));

	// Perte de cible : fade-out (encore visible à mi-course), puis inactif.
	fade.Update(false, p, 0.1f);
	assert(Near(fade.alpha, 0.5f));
	assert(fade.IsVisible());
	fade.Update(false, p, 0.1f);
	assert(Near(fade.alpha, 0.0f));
	assert(!fade.IsVisible());

	// dt négatif ignoré, durées nulles = transitions instantanées.
	fade.Update(true, p, -1.0f);
	assert(Near(fade.alpha, 0.0f));
	TargetReticleParams instant{};
	instant.fadeInSeconds = 0.0f;
	instant.fadeOutSeconds = 0.0f;
	fade.Update(true, instant, 0.001f);
	assert(Near(fade.alpha, 1.0f));
	fade.Update(false, instant, 0.001f);
	assert(Near(fade.alpha, 0.0f));
	std::puts("[OK] TestFadeLifecycle");
}

int main()
{
	TestDefaultParams();
	TestConfigOverrides();
	TestParseHexColor();
	TestCirclesAndEmptyInterior();
	TestVisionArcBounds();
	TestVisionRays();
	TestRotationFollowsYaw();
	TestBakeTexture();
	TestFadeLifecycle();
	std::puts("[ALL OK] TargetReticleTests");
	return 0;
}
