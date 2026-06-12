#pragma once

#include "src/shared/core/Config.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace engine::client
{
	/// Paramétrage du réticule de ciblage au sol (indicateur sous l'ennemi ciblé) :
	/// deux cercles concentriques clairs (grand cercle intérieur épais + petit cercle
	/// extérieur fin) et un secteur de vision (deux rayons + arc périphérique, ton
	/// foncé, aire NON remplie) qui tourne avec le yaw du PNJ.
	/// Longueurs en mètres, angles en degrés (sauf mention contraire).
	/// Chargé depuis les clés `target_reticle.*` de config.json (cf. \ref FromConfig).
	struct TargetReticleParams
	{
		/// Rayon au sol R_in du grand cercle intérieur. Défaut proposé : 1.8 m
		/// (ticket : 1.5–2.0 m).
		float radiusMeters = 1.8f;
		/// Épaisseur du trait du grand cercle intérieur (trait épais). Sert aussi
		/// d'épaisseur à l'arc de vision et aux deux rayons (même gabarit → aucune
		/// cassure visuelle à la jonction rayon/arc).
		float innerThicknessMeters = 0.12f;
		/// Épaisseur du trait du petit cercle extérieur (trait fin).
		float outerThicknessMeters = 0.05f;
		/// Écart relatif entre les deux cercles : R_out = R_in × (1 + ringGap).
		/// Défaut 0.11 → R_out ≈ 1.11 × R_in (ticket).
		float ringGap = 0.11f;
		/// Ouverture totale du cône de vision du PNJ (secteur). Défaut 120°.
		float visionArcDegrees = 120.0f;
		/// Couleur claire des deux cercles, format 0xRRGGBBAA. Défaut #CFE3F2.
		uint32_t colorLightRgba = 0xCFE3F2FFu;
		/// Couleur foncée de l'arc de vision et des rayons, format 0xRRGGBBAA.
		/// Défaut #5A87A8.
		uint32_t colorVisionRgba = 0x5A87A8FFu;
		/// Durée du fondu d'apparition au ciblage (secondes).
		float fadeInSeconds = 0.12f;
		/// Durée du fondu de disparition à la perte de cible (secondes).
		float fadeOutSeconds = 0.25f;

		/// Construit le paramétrage depuis la config (clés `target_reticle.*`),
		/// avec les défauts ci-dessus pour toute clé absente. Les valeurs sont
		/// bornées à des plages saines (rayon > 0, arc dans ]0°, 360°[…) pour
		/// qu'une config malformée ne produise jamais une géométrie dégénérée.
		static TargetReticleParams FromConfig(const engine::core::Config& config);
	};

	/// Parse une couleur hexadécimale "#RRGGBB" ou "#RRGGBBAA" (le « # » est
	/// optionnel, casse indifférente) vers le format 0xRRGGBBAA (alpha 0xFF si
	/// absent). Retourne \p fallback si le texte n'est pas parsable.
	uint32_t ParseHexColorRgba(std::string_view text, uint32_t fallback);

	/// Rayon R_out du petit cercle extérieur : R_in × (1 + ringGap).
	float ReticleOuterRadiusMeters(const TargetReticleParams& params);

	/// Demi-étendue XZ (en mètres) du volume de projection du decal : couvre
	/// R_out plus l'épaisseur du trait extérieur et une petite marge d'anti-
	/// crénelage, pour que rien ne soit rogné au bord de la texture.
	float ReticleHalfExtentMeters(const TargetReticleParams& params);

	/// Transforme un offset monde XZ (point − centre du réticule) vers le repère
	/// LOCAL du réticule pour un yaw donné (rotation de −yaw autour de Y).
	/// Convention jeu : la direction de regard du PNJ est (sin yaw, 0, cos yaw) ;
	/// dans le repère local, « devant » est +Z. C'est l'EXACT miroir CPU de la
	/// rotation appliquée par le fragment shader decal (decal.frag) — toute
	/// modification doit être répercutée des deux côtés.
	void WorldOffsetToReticleLocal(float yawRadians, float worldDx, float worldDz,
		float& outLocalX, float& outLocalZ);

	/// Couvertures (0..1) des deux familles de traits du réticule en un point.
	struct ReticleCoverage
	{
		float light = 0.0f;  ///< Cercles clairs (creusés là où le foncé passe dessus).
		float vision = 0.0f; ///< Secteur foncé : arc périphérique + deux rayons.
	};

	/// Évalue analytiquement la couverture du réticule au point local
	/// (localX, localZ) en mètres (repère local : « devant » = +Z, cf.
	/// \ref WorldOffsetToReticleLocal). \p featherMeters est la largeur de la
	/// transition douce d'anti-crénelage ajoutée au bord des traits (0 = bords
	/// nets, utilisé par les tests ; le baker passe ~1 texel).
	/// Le secteur foncé prime sur le clair (dessiné « par-dessus ») : la
	/// couverture claire est creusée d'autant — aucune cassure à la jonction.
	ReticleCoverage EvaluateReticleAt(const TargetReticleParams& params,
		float localX, float localZ, float featherMeters = 0.0f);

	/// Rasterise le réticule dans un buffer RGBA8 carré de \p textureSize texels
	/// de côté (couleurs sRGB non prémultipliées, alpha = couverture). Le texel
	/// (px, py) couvre le point local lx = ((px+0.5)/N×2−1)×halfExtent,
	/// lz = ((py+0.5)/N×2−1)×halfExtent — la même convention UV que decal.frag.
	/// Pur CPU (aucune dépendance Vulkan), testable unitairement.
	/// Effet de bord : \p outRgba est redimensionné à textureSize²×4 octets.
	void BakeReticleRgba(const TargetReticleParams& params, uint32_t textureSize,
		std::vector<uint8_t>& outRgba);

	/// Machine à états du fondu apparition/disparition du réticule (pure,
	/// testable sans rendu) : alpha → 1 quand une cible est résolue (durée
	/// fadeInSeconds), alpha → 0 sinon (durée fadeOutSeconds).
	struct TargetReticleFade
	{
		/// Opacité courante du réticule, 0 (caché) à 1 (pleinement visible).
		float alpha = 0.0f;

		/// Avance le fondu de \p dtSeconds selon la présence d'une cible.
		/// Une durée de fondu ≤ 0 rend la transition instantanée.
		void Update(bool hasTarget, const TargetReticleParams& params, float dtSeconds);

		/// Vrai tant que le réticule doit être rendu (alpha > 0).
		bool IsVisible() const { return alpha > 0.0f; }
	};
}
