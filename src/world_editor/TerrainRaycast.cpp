#include "src/world_editor/world/TerrainRaycast.h"

#include <algorithm>
#include <cmath>

namespace engine::editor::world
{
	namespace
	{
		/// Reconstruit le triplet (right, up, forward) en repère monde depuis
		/// la caméra. Cohérent avec `Camera::ComputeViewMatrix` (PR26.5) :
		///   forward = (-sin(yaw)*cos(pitch), -sin(pitch), -cos(yaw)*cos(pitch))
		///   right   = normalize((-forward.z, 0, forward.x))
		///   up      = cross(right, forward) (LH Vulkan convention).
		void BuildCameraBasis(const engine::render::Camera& cam,
			engine::math::Vec3& outForward,
			engine::math::Vec3& outRight,
			engine::math::Vec3& outUp)
		{
			const float cy = std::cos(cam.yaw);
			const float sy = std::sin(cam.yaw);
			const float cp = std::cos(cam.pitch);
			const float sp = std::sin(cam.pitch);
			engine::math::Vec3 forward(-sy * cp, -sp, -cy * cp);
			engine::math::Vec3 right(-forward.z, 0.0f, forward.x);
			float rlen = right.Length();
			if (rlen > 0.0f) right = right * (1.0f / rlen);
			else right = engine::math::Vec3(1.0f, 0.0f, 0.0f);
			engine::math::Vec3 up(
				right.y * forward.z - right.z * forward.y,
				right.z * forward.x - right.x * forward.z,
				right.x * forward.y - right.y * forward.x);
			float ulen = up.Length();
			if (ulen > 0.0f) up = up * (1.0f / ulen);
			else up = engine::math::Vec3(0.0f, 1.0f, 0.0f);
			outForward = forward;
			outRight = right;
			outUp = up;
		}
	}

	TerrainHit RaycastTerrain(const engine::render::Camera& cam,
		int sx, int sy, int vw, int vh,
		const std::function<float(float, float)>& sampleHeightAt,
		float maxRangeMeters)
	{
		TerrainHit miss;
		if (vw <= 0 || vh <= 0 || !sampleHeightAt) return miss;

		// Coordonnées NDC dans [-1, 1]. Origine pixel top-left → ndcY inversé.
		const float ndcX = (2.0f * (static_cast<float>(sx) + 0.5f) / static_cast<float>(vw)) - 1.0f;
		const float ndcY = 1.0f - (2.0f * (static_cast<float>(sy) + 0.5f) / static_cast<float>(vh));

		engine::math::Vec3 forward, right, up;
		BuildCameraBasis(cam, forward, right, up);

		engine::math::Vec3 rayOrigin = cam.position;
		engine::math::Vec3 rayDir;

		if (cam.ortho)
		{
			// Mode ortho : tous les rayons sont parallèles à `forward`,
			// l'origine se décale sur le plan de la caméra.
			const float halfH = cam.orthoExtent;
			const float halfW = halfH * cam.aspect;
			rayOrigin = cam.position
			          + right * (ndcX * halfW)
			          + up    * (ndcY * halfH);
			rayDir = forward;
		}
		else
		{
			// Mode perspective : reconstruit la direction monde depuis
			// l'angle vertical (fovY) et l'aspect.
			const float fovYRad = cam.fovYDeg * 3.14159265f / 180.0f;
			const float t = std::tan(fovYRad * 0.5f);
			const float halfH = t;
			const float halfW = t * cam.aspect;
			rayDir = (forward
			       + right * (ndcX * halfW)
			       + up    * (ndcY * halfH)).Normalized();
		}

		// Si le rayon est plat (rayDir.y ≈ 0), pas d'intersection garantie ;
		// on bail tôt pour éviter une marche vaine.
		if (std::fabs(rayDir.y) < 1e-6f)
		{
			return miss;
		}

		// Marche linéaire avec pas de 1 m. On cherche le premier `t` où la
		// hauteur du rayon descend sous la hauteur sampled.
		const float step = 1.0f;
		float t0 = 0.0f;
		float h0_ray = rayOrigin.y;
		float h0_ground = sampleHeightAt(rayOrigin.x, rayOrigin.z);
		float diff0 = h0_ray - h0_ground;

		// Si on commence DÉJÀ en dessous du sol, considérer que l'origine
		// touche le sol (cas extrême : caméra sous le terrain).
		if (diff0 <= 0.0f)
		{
			TerrainHit h{true, rayOrigin.x, h0_ground, rayOrigin.z};
			return h;
		}

		float t1 = step;
		while (t1 <= maxRangeMeters)
		{
			const float rx = rayOrigin.x + rayDir.x * t1;
			const float rz = rayOrigin.z + rayDir.z * t1;
			const float ry = rayOrigin.y + rayDir.y * t1;
			const float gy = sampleHeightAt(rx, rz);
			const float diff1 = ry - gy;
			if (diff1 <= 0.0f)
			{
				// Bracket trouvé : raffinement Newton 4 itérations dans
				// l'intervalle [t0, t1] sur f(t) = ry(t) - gy(t).
				float a = t0, b = t1;
				float fa = diff0, fb = diff1;
				for (int i = 0; i < 4; ++i)
				{
					const float denom = (fa - fb);
					float tm;
					if (std::fabs(denom) < 1e-6f)
					{
						tm = 0.5f * (a + b);
					}
					else
					{
						tm = a + fa * (b - a) / denom;
						if (tm < a || tm > b) tm = 0.5f * (a + b);
					}
					const float rxm = rayOrigin.x + rayDir.x * tm;
					const float rzm = rayOrigin.z + rayDir.z * tm;
					const float rym = rayOrigin.y + rayDir.y * tm;
					const float gym = sampleHeightAt(rxm, rzm);
					const float fm = rym - gym;
					if (fm > 0.0f) { a = tm; fa = fm; }
					else           { b = tm; fb = fm; }
				}
				const float tHit = 0.5f * (a + b);
				TerrainHit h;
				h.hit = true;
				h.worldX = rayOrigin.x + rayDir.x * tHit;
				h.worldZ = rayOrigin.z + rayDir.z * tHit;
				h.worldY = sampleHeightAt(h.worldX, h.worldZ);
				return h;
			}
			t0 = t1;
			diff0 = diff1;
			t1 += step;
		}
		return miss;
	}
}
