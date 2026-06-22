#include "src/client/render/CameraOcclusionFade.h"

#include <algorithm>
#include <cmath>

namespace engine::render
{
	namespace
	{
		float Dot(const engine::math::Vec3& a, const engine::math::Vec3& b)
		{ return a.x * b.x + a.y * b.y + a.z * b.z; }

		float Length(const engine::math::Vec3& v)
		{ return std::sqrt(Dot(v, v)); }

		float Clamp01(float v)
		{ return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
	}

	void CameraOcclusionFade::Init(const Config& cfg)
	{
		m_cfg = cfg;
		m_fade.clear();
	}

	void CameraOcclusionFade::Update(const engine::math::Vec3& cameraPos,
		const engine::math::Vec3& focusPoint,
		const std::vector<OccluderSphere>& occluders,
		float dt)
	{
		if (dt < 0.0f) dt = 0.0f;

		const engine::math::Vec3 seg = focusPoint - cameraPos;
		const float segLen = Length(seg);
		const engine::math::Vec3 dir = (segLen > 1e-4f)
			? engine::math::Vec3{ seg.x / segLen, seg.y / segLen, seg.z / segLen }
			: engine::math::Vec3{ 0.0f, 0.0f, 1.0f };

		// Nouvelle table : on n'y garde que les ids encore "vivants" (vus cette
		// frame, ou en train de revenir à l'opaque). Évite la croissance mémoire.
		std::unordered_map<std::uint32_t, float> next;
		next.reserve(occluders.size());

		for (const auto& occ : occluders)
		{
			float target = 1.0f; // opaque par défaut

			// Garde joueur : occulteur collé au joueur -> jamais fondu.
			const float distToFocus = Length(occ.center - focusPoint);
			if (distToFocus >= m_cfg.playerProtectRadius)
			{
				// Projection sur le segment caméra->joueur.
				const engine::math::Vec3 toOcc = occ.center - cameraPos;
				const float proj = Dot(toOcc, dir);
				if (segLen > 1e-4f && proj > 0.0f && proj < segLen)
				{
					const engine::math::Vec3 closest{
						cameraPos.x + dir.x * proj,
						cameraPos.y + dir.y * proj,
						cameraPos.z + dir.z * proj };
					const float d = Length(occ.center - closest);
					const float r0 = occ.radius;
					const float r1 = occ.radius + m_cfg.radiusMargin;
					if (d <= r0)
						target = m_cfg.fadeMin;
					else if (d < r1)
						target = m_cfg.fadeMin + (1.0f - m_cfg.fadeMin) * ((d - r0) / (r1 - r0));
					// d >= r1 -> target reste 1.0
				}
			}

			float current = 1.0f;
			auto it = m_fade.find(occ.id);
			if (it != m_fade.end()) current = it->second;

			if (target < current)
				current = std::max(target, current - m_cfg.fadeOutPerSec * dt);
			else if (target > current)
				current = std::min(target, current + m_cfg.fadeInPerSec * dt);

			next[occ.id] = Clamp01(current);
		}

		// Ids non vus cette frame : on les ramène vers l'opaque ; une fois ~opaques
		// on les laisse tomber (purge) pour que FadeFor renvoie 1.0 par défaut.
		for (const auto& kv : m_fade)
		{
			if (next.find(kv.first) != next.end()) continue;
			const float current = std::min(1.0f, kv.second + m_cfg.fadeInPerSec * dt);
			if (current < 1.0f - 1e-3f)
				next[kv.first] = current;
		}

		m_fade.swap(next);
	}

	float CameraOcclusionFade::FadeFor(std::uint32_t id) const
	{
		auto it = m_fade.find(id);
		return (it != m_fade.end()) ? it->second : 1.0f;
	}
}
