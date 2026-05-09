// src/world_editor/world/CollisionPreviewCamera.cpp
#include "src/world_editor/world/CollisionPreviewCamera.h"

#include <algorithm>
#include <cmath>

namespace engine::editor::world
{
	namespace
	{
		constexpr float kDragSensitivity = 0.01f;
		constexpr float kZoomFactor      = 0.9f;
		constexpr float kMinDistance     = 0.5f;
		constexpr float kMaxDistance     = 20.0f;
		constexpr float kPitchClamp      = 1.5208f; // π/2 - 0.05
		constexpr float kFovYRad         = 1.0472f; // 60° in radians
		constexpr float kNearZ           = 0.1f;
		constexpr float kFarZ            = 100.0f;
	}

	void CollisionPreviewCamera::HandleDrag(float deltaX, float deltaY) noexcept
	{
		m_yaw   -= deltaX * kDragSensitivity;
		m_pitch -= deltaY * kDragSensitivity;
		m_pitch  = std::clamp(m_pitch, -kPitchClamp, kPitchClamp);
	}

	void CollisionPreviewCamera::HandleZoom(float deltaWheel) noexcept
	{
		if (deltaWheel > 0.0f) m_distance *= kZoomFactor;
		else if (deltaWheel < 0.0f) m_distance /= kZoomFactor;
		m_distance = std::clamp(m_distance, kMinDistance, kMaxDistance);
	}

	void CollisionPreviewCamera::Reset() noexcept
	{
		m_yaw      = 0.7f;
		m_pitch    = 0.4f;
		m_distance = 3.0f;
	}

	float CollisionPreviewCamera::GetYawDegrees() const noexcept
	{
		return m_yaw * 57.29578f;  // 180/π
	}

	float CollisionPreviewCamera::GetPitchDegrees() const noexcept
	{
		return m_pitch * 57.29578f;
	}

	bool CollisionPreviewCamera::Project(engine::math::Vec3 worldPos,
		float viewportW, float viewportH,
		float& outScreenX, float& outScreenY) const
	{
		// Caméra orbite l'origine. Position de la caméra :
		//   eye = (sin(yaw)*cos(pitch), sin(pitch), cos(yaw)*cos(pitch)) * distance
		// Cible = (0,0,0). Up monde = (0,1,0).
		const float cy = std::cos(m_yaw);
		const float sy = std::sin(m_yaw);
		const float cp = std::cos(m_pitch);
		const float sp = std::sin(m_pitch);

		// Forward (eye → origin, normalisé)
		const float fx = -sy * cp;
		const float fy = -sp;
		const float fz = -cy * cp;

		// Right = normalize(cross(worldUp=(0,1,0), forward)) — composante uniquement xz
		const float rxLen = std::sqrt(fz * fz + fx * fx);
		const float rx = (rxLen > 1e-6f) ? ( fz / rxLen) : 1.0f;
		const float rz = (rxLen > 1e-6f) ? (-fx / rxLen) : 0.0f;

		// Up = cross(forward, right)
		const float ux = fy * rz - fz * 0.0f;
		const float uy = fz * rx - fx * rz;
		const float uz = fx * 0.0f - fy * rx;

		// Eye position (world space)
		const float ex = sy * cp * m_distance;
		const float ey = sp * m_distance;
		const float ez = cy * cp * m_distance;

		// View-space transform : (worldPos - eye) projeté sur right/up/forward
		const float dx = worldPos.x - ex;
		const float dy = worldPos.y - ey;
		const float dz = worldPos.z - ez;

		const float vx = dx * rx + dz * rz;       // right (no y component)
		const float vy = dx * ux + dy * uy + dz * uz;  // up
		const float vz = dx * fx + dy * fy + dz * fz;  // forward

		// Early exit si point hors de la bande [near, far] : ne pas écrire
		// les out-params pour un point invisible.
		if (vz <= kNearZ || vz >= kFarZ) return false;

		// Perspective : x'/z' * cot(fov/2) / aspect, y'/z' * cot(fov/2)
		const float aspect = (viewportH > 0.0f) ? (viewportW / viewportH) : 1.0f;
		const float t = 1.0f / std::tan(kFovYRad * 0.5f);
		const float ndcX = (vx * t / aspect) / vz;
		const float ndcY = (vy * t) / vz;

		// NDC [-1, 1] → pixel space top-left origin
		outScreenX = (ndcX * 0.5f + 0.5f) * viewportW;
		outScreenY = (1.0f - (ndcY * 0.5f + 0.5f)) * viewportH;

		return outScreenX >= 0.0f && outScreenX <= viewportW
		    && outScreenY >= 0.0f && outScreenY <= viewportH;
	}
}
