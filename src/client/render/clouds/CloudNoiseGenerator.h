#pragma once
// CloudNoiseGenerator — génération CPU des textures 3D de bruit des nuages
// volumétriques (chantier ciel 2026-07-17, spec docs/superpowers/specs/
// 2026-07-17-sky-clouds-upgrade-design.md, PR A).
//
// Remplace le value-noise FBM calculé in-shader (clouds.frag historique) par
// des textures pré-calculées type Schneider (« The Real-Time Volumetric
// Cloudscapes of Horizon Zero Dawn ») :
//   - base 64³ RGBA8 : R = fBm de Perlin, G/B/A = Worley 8/16/32 cellules ;
//   - détail 32³ RGBA8 : R/G/B = Worley 4/8/16 cellules (érosion des bords).
// Tous les bruits sont PÉRIODIQUES (la texture tuile sans couture, sampler
// REPEAT) et DÉTERMINISTES (hash entier maison — aucun rand()).
//
// Module 100 % CPU, sans dépendance Vulkan/ImGui : testable sous ctest
// Linux (cloud_noise_generator_tests).

#include <cstdint>
#include <vector>

namespace engine::render::clouds
{
	/// Côté (texels par axe) de la texture 3D de forme de base.
	inline constexpr int kBaseNoiseSize = 64;
	/// Côté (texels par axe) de la texture 3D d'érosion de détail.
	inline constexpr int kDetailNoiseSize = 32;

	/// Textures générées, prêtes à uploader en R8G8B8A8_UNORM.
	struct CloudNoiseData
	{
		/// kBaseNoiseSize³ × 4 octets (R=Perlin fBm, G/B/A=Worley 8/16/32).
		std::vector<uint8_t> baseRgba;
		/// kDetailNoiseSize³ × 4 octets (R/G/B=Worley 4/8/16, A=255).
		std::vector<uint8_t> detailRgba;
	};

	/// Bruit de Worley 3D périodique « inversé » (1 au centre des cellules,
	/// 0 aux frontières) — aspect cotonneux. \param x,y,z coordonnées
	/// normalisées (période 1 : f(x)==f(x+1)). \param cells nombre de
	/// cellules par axe. \return valeur dans [0,1]. Pur, thread-safe.
	float TileableWorley(float x, float y, float z, int cells, uint32_t seed);

	/// fBm de Perlin 3D périodique. \param basePeriod période de la première
	/// octave (en cellules) ; chaque octave double fréquence ET période
	/// (le tuilage est préservé). \param octaves nombre d'octaves (≥1).
	/// \return valeur normalisée ~[0,1]. Pur, thread-safe.
	float TileablePerlinFbm(float x, float y, float z, int basePeriod,
		int octaves, uint32_t seed);

	/// Génère les deux textures. Déterministe pour un seed donné (mêmes
	/// octets sur toutes plateformes). Coût : quelques centaines de ms
	/// (appelée une fois au boot par CloudPass::Init, hors boucle frame).
	CloudNoiseData GenerateCloudNoise(uint32_t seed);
}
