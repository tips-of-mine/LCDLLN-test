#include "src/client/combat/TargetReticleGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace engine::client
{
	namespace
	{
		constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

		/// Borne une valeur dans [lo, hi] (helper local, évite d'inclure <bit> etc.).
		float ClampF(float v, float lo, float hi)
		{
			return std::min(std::max(v, lo), hi);
		}

		/// Couverture (0..1) d'une bande de demi-largeur \p halfWidth à la distance
		/// \p distance de son axe : 1 dans la bande, transition linéaire de largeur
		/// \p feather au-delà (0 = bord net). Brique commune des cercles/arc/rayons.
		float BandCoverage(float distance, float halfWidth, float feather)
		{
			if (feather <= 0.0f)
				return (distance <= halfWidth) ? 1.0f : 0.0f;
			return ClampF((halfWidth + feather - distance) / feather, 0.0f, 1.0f);
		}

		/// Parse un caractère hexadécimal ; retourne -1 si invalide.
		int HexNibble(char c)
		{
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
			if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
			return -1;
		}
	}

	TargetReticleParams TargetReticleParams::FromConfig(const engine::core::Config& config)
	{
		TargetReticleParams p{};
		p.radiusMeters = static_cast<float>(config.GetDouble("target_reticle.radius_meters", p.radiusMeters));
		p.innerThicknessMeters = static_cast<float>(config.GetDouble("target_reticle.inner_thickness", p.innerThicknessMeters));
		p.outerThicknessMeters = static_cast<float>(config.GetDouble("target_reticle.outer_thickness", p.outerThicknessMeters));
		p.ringGap = static_cast<float>(config.GetDouble("target_reticle.ring_gap", p.ringGap));
		p.visionArcDegrees = static_cast<float>(config.GetDouble("target_reticle.vision_arc_degrees", p.visionArcDegrees));
		p.colorLightRgba = ParseHexColorRgba(config.GetString("target_reticle.color_light", "#CFE3F2"), 0xCFE3F2FFu);
		p.colorVisionRgba = ParseHexColorRgba(config.GetString("target_reticle.color_vision", "#5A87A8"), 0x5A87A8FFu);
		p.fadeInSeconds = static_cast<float>(config.GetDouble("target_reticle.fade_in_seconds", p.fadeInSeconds));
		p.fadeOutSeconds = static_cast<float>(config.GetDouble("target_reticle.fade_out_seconds", p.fadeOutSeconds));

		// Bornes saines : une config malformée ne doit jamais produire une
		// géométrie dégénérée (rayon nul, arc plein tour, épaisseur négative…).
		p.radiusMeters = ClampF(p.radiusMeters, 0.1f, 50.0f);
		p.innerThicknessMeters = ClampF(p.innerThicknessMeters, 0.005f, p.radiusMeters);
		p.outerThicknessMeters = ClampF(p.outerThicknessMeters, 0.005f, p.radiusMeters);
		p.ringGap = ClampF(p.ringGap, 0.01f, 2.0f);
		p.visionArcDegrees = ClampF(p.visionArcDegrees, 1.0f, 359.0f);
		p.fadeInSeconds = ClampF(p.fadeInSeconds, 0.0f, 10.0f);
		p.fadeOutSeconds = ClampF(p.fadeOutSeconds, 0.0f, 10.0f);
		return p;
	}

	uint32_t ParseHexColorRgba(std::string_view text, uint32_t fallback)
	{
		if (!text.empty() && text.front() == '#')
			text.remove_prefix(1);
		if (text.size() != 6 && text.size() != 8)
			return fallback;

		uint32_t value = 0;
		for (char c : text)
		{
			const int nibble = HexNibble(c);
			if (nibble < 0)
				return fallback;
			value = (value << 4) | static_cast<uint32_t>(nibble);
		}
		if (text.size() == 6)
			value = (value << 8) | 0xFFu; // alpha opaque implicite.
		return value;
	}

	float ReticleOuterRadiusMeters(const TargetReticleParams& params)
	{
		return params.radiusMeters * (1.0f + params.ringGap);
	}

	float ReticleHalfExtentMeters(const TargetReticleParams& params)
	{
		// R_out + demi-trait le plus épais + 4 % de marge anti-crénelage.
		const float maxHalfThickness = 0.5f * std::max(params.innerThicknessMeters, params.outerThicknessMeters);
		return (ReticleOuterRadiusMeters(params) + maxHalfThickness) * 1.04f;
	}

	void WorldOffsetToReticleLocal(float yawRadians, float worldDx, float worldDz,
		float& outLocalX, float& outLocalZ)
	{
		// Rotation de −yaw autour de Y : le point monde (sin yaw, cos yaw)
		// (= devant le PNJ) devient le local (0, 1) (= +Z local).
		const float cy = std::cos(yawRadians);
		const float sy = std::sin(yawRadians);
		outLocalX = cy * worldDx - sy * worldDz;
		outLocalZ = sy * worldDx + cy * worldDz;
	}

	ReticleCoverage EvaluateReticleAt(const TargetReticleParams& params,
		float localX, float localZ, float featherMeters)
	{
		const float rIn = params.radiusMeters;
		const float rOut = ReticleOuterRadiusMeters(params);
		const float innerHalf = 0.5f * params.innerThicknessMeters;
		const float outerHalf = 0.5f * params.outerThicknessMeters;
		const float halfArcRad = 0.5f * params.visionArcDegrees * kDegToRad;

		const float r = std::sqrt(localX * localX + localZ * localZ);

		// --- Cercles clairs : grand cercle intérieur épais + petit cercle fin.
		float light = std::max(
			BandCoverage(std::fabs(r - rIn), innerHalf, featherMeters),
			BandCoverage(std::fabs(r - rOut), outerHalf, featherMeters));

		// --- Arc de vision foncé : portion du cercle R_out dans le secteur
		// ±halfArc autour de +Z local (« devant »). L'adoucissement angulaire
		// est exprimé en longueur d'arc (feather/r) pour rester homogène.
		float vision = 0.0f;
		{
			const float angle = std::fabs(std::atan2(localX, localZ)); // 0 = devant.
			float angularCoverage = 1.0f;
			if (r > 1e-4f)
			{
				const float angFeather = featherMeters / r;
				angularCoverage = (angFeather <= 0.0f)
					? ((angle <= halfArcRad) ? 1.0f : 0.0f)
					: ClampF((halfArcRad + angFeather - angle) / angFeather, 0.0f, 1.0f);
			}
			vision = BandCoverage(std::fabs(r - rOut), innerHalf, featherMeters) * angularCoverage;
		}

		// --- Deux rayons foncés : segments du centre jusqu'à l'arc (longueur
		// R_out), aux bornes ±halfArc du secteur. Même épaisseur que l'arc.
		for (int side = 0; side < 2; ++side)
		{
			const float boundary = (side == 0) ? -halfArcRad : halfArcRad;
			const float dirX = std::sin(boundary);
			const float dirZ = std::cos(boundary);
			const float t = ClampF(localX * dirX + localZ * dirZ, 0.0f, rOut);
			const float dx = localX - t * dirX;
			const float dz = localZ - t * dirZ;
			const float distToRay = std::sqrt(dx * dx + dz * dz);
			vision = std::max(vision, BandCoverage(distToRay, innerHalf, featherMeters));
		}

		// Le foncé est dessiné « par-dessus » : il creuse le clair, ce qui évite
		// toute double-accumulation d'alpha à la jonction (aucune cassure).
		light *= (1.0f - vision);
		return ReticleCoverage{ light, vision };
	}

	void BakeReticleRgba(const TargetReticleParams& params, uint32_t textureSize,
		std::vector<uint8_t>& outRgba)
	{
		outRgba.assign(static_cast<size_t>(textureSize) * textureSize * 4u, 0u);
		if (textureSize == 0u)
			return;

		const float halfExtent = ReticleHalfExtentMeters(params);
		// Anti-crénelage : transition douce d'environ un texel.
		const float feather = (2.0f * halfExtent) / static_cast<float>(textureSize);

		const float lightR = static_cast<float>((params.colorLightRgba >> 24) & 0xFFu);
		const float lightG = static_cast<float>((params.colorLightRgba >> 16) & 0xFFu);
		const float lightB = static_cast<float>((params.colorLightRgba >> 8) & 0xFFu);
		const float lightA = static_cast<float>(params.colorLightRgba & 0xFFu) / 255.0f;
		const float visionR = static_cast<float>((params.colorVisionRgba >> 24) & 0xFFu);
		const float visionG = static_cast<float>((params.colorVisionRgba >> 16) & 0xFFu);
		const float visionB = static_cast<float>((params.colorVisionRgba >> 8) & 0xFFu);
		const float visionA = static_cast<float>(params.colorVisionRgba & 0xFFu) / 255.0f;

		for (uint32_t py = 0; py < textureSize; ++py)
		{
			const float lz = ((static_cast<float>(py) + 0.5f) / static_cast<float>(textureSize) * 2.0f - 1.0f) * halfExtent;
			for (uint32_t px = 0; px < textureSize; ++px)
			{
				const float lx = ((static_cast<float>(px) + 0.5f) / static_cast<float>(textureSize) * 2.0f - 1.0f) * halfExtent;
				const ReticleCoverage cov = EvaluateReticleAt(params, lx, lz, feather);
				const float wLight = cov.light * lightA;
				const float wVision = cov.vision * visionA;
				const float alpha = ClampF(wLight + wVision, 0.0f, 1.0f);
				if (alpha <= 0.0f)
					continue; // texel transparent (déjà à zéro).

				// Couleur = moyenne pondérée des deux tons (non prémultipliée).
				const float invW = 1.0f / (wLight + wVision);
				const float red = (lightR * wLight + visionR * wVision) * invW;
				const float green = (lightG * wLight + visionG * wVision) * invW;
				const float blue = (lightB * wLight + visionB * wVision) * invW;

				const size_t idx = (static_cast<size_t>(py) * textureSize + px) * 4u;
				outRgba[idx + 0] = static_cast<uint8_t>(ClampF(red, 0.0f, 255.0f) + 0.5f);
				outRgba[idx + 1] = static_cast<uint8_t>(ClampF(green, 0.0f, 255.0f) + 0.5f);
				outRgba[idx + 2] = static_cast<uint8_t>(ClampF(blue, 0.0f, 255.0f) + 0.5f);
				outRgba[idx + 3] = static_cast<uint8_t>(alpha * 255.0f + 0.5f);
			}
		}
	}

	void TargetReticleFade::Update(bool hasTarget, const TargetReticleParams& params, float dtSeconds)
	{
		const float dt = std::max(0.0f, dtSeconds);
		if (hasTarget)
		{
			alpha = (params.fadeInSeconds > 0.0f)
				? std::min(1.0f, alpha + dt / params.fadeInSeconds)
				: 1.0f;
		}
		else
		{
			alpha = (params.fadeOutSeconds > 0.0f)
				? std::max(0.0f, alpha - dt / params.fadeOutSeconds)
				: 0.0f;
		}
	}
}
