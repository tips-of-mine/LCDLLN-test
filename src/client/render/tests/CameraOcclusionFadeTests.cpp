#include "src/client/render/CameraOcclusionFade.h"

#include <cmath>
#include <cstdio>
#include <vector>

using engine::render::CameraOcclusionFade;
using engine::render::OccluderSphere;
using engine::math::Vec3;

namespace
{
	int g_fail = 0;
	void check(bool cond, const char* msg)
	{ if (!cond) { std::printf("FAIL: %s\n", msg); ++g_fail; } }

	CameraOcclusionFade::Config DefaultCfg()
	{
		CameraOcclusionFade::Config c{};
		c.fadeMin = 0.15f; c.radiusMargin = 0.5f;
		c.fadeInPerSec = 6.0f; c.fadeOutPerSec = 8.0f;
		c.playerProtectRadius = 0.6f;
		return c;
	}

	// Fait converger le lissage : N updates de dt fixe avec la même scène.
	float Converge(CameraOcclusionFade& f, const Vec3& cam, const Vec3& focus,
		const std::vector<OccluderSphere>& occ, std::uint32_t id, int frames, float dt)
	{
		for (int i = 0; i < frames; ++i) f.Update(cam, focus, occ, dt);
		return f.FadeFor(id);
	}
}

int main()
{
	const Vec3 cam{ 0, 0, 0 };
	const Vec3 focus{ 10, 0, 0 };

	// 1) Occulteur pile sur le segment, au centre -> converge vers fadeMin.
	{
		CameraOcclusionFade f; f.Init(DefaultCfg());
		std::vector<OccluderSphere> occ{ { 1, Vec3{ 5, 0, 0 }, 0.5f } };
		const float v = Converge(f, cam, focus, occ, 1, 100, 0.1f);
		check(std::fabs(v - 0.15f) < 0.02f, "1: coeur d'occlusion -> fadeMin");
	}

	// 2) Occulteur derrière le joueur (proj > segLen) -> opaque.
	{
		CameraOcclusionFade f; f.Init(DefaultCfg());
		std::vector<OccluderSphere> occ{ { 2, Vec3{ 12, 0, 0 }, 0.5f } };
		const float v = Converge(f, cam, focus, occ, 2, 100, 0.1f);
		check(std::fabs(v - 1.0f) < 1e-3f, "2: derriere le joueur -> opaque");
	}

	// 3) Occulteur derrière la caméra (proj < 0) -> opaque.
	{
		CameraOcclusionFade f; f.Init(DefaultCfg());
		std::vector<OccluderSphere> occ{ { 3, Vec3{ -2, 0, 0 }, 0.5f } };
		const float v = Converge(f, cam, focus, occ, 3, 100, 0.1f);
		check(std::fabs(v - 1.0f) < 1e-3f, "3: derriere la camera -> opaque");
	}

	// 4) Occulteur latéral (d > radius+margin) -> opaque.
	{
		CameraOcclusionFade f; f.Init(DefaultCfg());
		std::vector<OccluderSphere> occ{ { 4, Vec3{ 5, 0, 3 }, 0.5f } };
		const float v = Converge(f, cam, focus, occ, 4, 100, 0.1f);
		check(std::fabs(v - 1.0f) < 1e-3f, "4: lateral hors zone -> opaque");
	}

	// 5) Zone de transition : fade strictement entre fadeMin et 1.
	{
		CameraOcclusionFade f; f.Init(DefaultCfg());
		// d=0.75, r0=0.5, r1=1.0 -> target = 0.15 + 0.85*0.5 = 0.575.
		std::vector<OccluderSphere> occ{ { 5, Vec3{ 5, 0, 0.75f }, 0.5f } };
		const float v = Converge(f, cam, focus, occ, 5, 100, 0.1f);
		check(v > 0.16f && v < 0.99f, "5: transition -> fade intermediaire");
		check(std::fabs(v - 0.575f) < 0.03f, "5: transition -> valeur attendue ~0.575");
	}

	// 6) Garde joueur : occulteur collé au joueur -> jamais fondu.
	{
		CameraOcclusionFade f; f.Init(DefaultCfg());
		std::vector<OccluderSphere> occ{ { 6, Vec3{ 9.8f, 0, 0 }, 0.5f } }; // |toFocus|=0.2<0.6
		const float v = Converge(f, cam, focus, occ, 6, 100, 0.1f);
		check(std::fabs(v - 1.0f) < 1e-3f, "6: garde joueur -> opaque");
	}

	// 7) Lissage : un seul petit dt ne saute pas directement à fadeMin.
	{
		CameraOcclusionFade f; f.Init(DefaultCfg());
		std::vector<OccluderSphere> occ{ { 7, Vec3{ 5, 0, 0 }, 0.5f } };
		f.Update(cam, focus, occ, 0.01f); // 1 - 8*0.01 = 0.92
		const float v = f.FadeFor(7);
		check(v > 0.15f + 0.01f && v < 1.0f, "7: lissage progressif (pas de saut)");
	}

	// 8) Purge : occulteur présent puis absent -> revient à 1.0 et FadeFor=1.0.
	{
		CameraOcclusionFade f; f.Init(DefaultCfg());
		std::vector<OccluderSphere> occ{ { 8, Vec3{ 5, 0, 0 }, 0.5f } };
		Converge(f, cam, focus, occ, 8, 100, 0.1f); // converge à fadeMin
		const float back = Converge(f, cam, focus, {}, 8, 100, 0.1f); // plus d'occulteur
		check(std::fabs(back - 1.0f) < 1e-3f, "8: purge -> revient opaque");
	}

	// 9) Id inconnu -> 1.0.
	{
		CameraOcclusionFade f; f.Init(DefaultCfg());
		check(std::fabs(f.FadeFor(999) - 1.0f) < 1e-6f, "9: inconnu -> opaque");
	}

	if (g_fail == 0) std::printf("CameraOcclusionFadeTests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
