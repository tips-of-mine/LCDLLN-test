#include "engine/client/AoEPreviewSystem.h"

#include "engine/core/Log.h"

#include <algorithm>
#include <cmath>

namespace engine::client
{
	AoEPreviewSystem::~AoEPreviewSystem()
	{
		Shutdown();
	}

	bool AoEPreviewSystem::Init(const engine::core::Config& config, engine::render::DecalSystem& decalSystem, engine::gameplay::SkillSystem& skillSystem)
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[AoEPreviewSystem] Init ignored: already initialized");
			return true;
		}

		m_config = &config;
		m_decalSystem = &decalSystem;
		m_skillSystem = &skillSystem;

		m_localPlayerId = 0;
		m_activeSkillId.clear();
		m_haveLastPreviewPos = false;
		m_timeSinceLastDecalSpawn = 0.0f;
		m_initialized = true;

		LOG_INFO(Core, "[AoEPreviewSystem] Init OK");
		return true;
	}

	void AoEPreviewSystem::Shutdown()
	{
		if (!m_initialized)
			return;

		m_initialized = false;
		m_config = nullptr;
		m_decalSystem = nullptr;
		m_skillSystem = nullptr;
		m_localPlayerId = 0;
		m_activeSkillId.clear();
		m_haveLastPreviewPos = false;
		m_timeSinceLastDecalSpawn = 0.0f;

		LOG_INFO(Core, "[AoEPreviewSystem] Destroyed");
	}

	bool AoEPreviewSystem::SetActiveSkill(uint32_t localPlayerId, std::string_view skillId)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[AoEPreviewSystem] SetActiveSkill FAILED: system not initialized");
			return false;
		}

		const engine::gameplay::SkillSystem::SkillDefinition* def = m_skillSystem->FindSkill(skillId);
		if (def == nullptr)
		{
			LOG_WARN(Core, "[AoEPreviewSystem] SetActiveSkill rejected: unknown skill '{}'", skillId);
			return false;
		}

		if (def->aoe.shape == engine::gameplay::SkillSystem::AoEShapeType::None)
		{
			LOG_WARN(Core, "[AoEPreviewSystem] SetActiveSkill rejected: skill '{}' has no AoE shape", def->id);
			return false;
		}

		m_localPlayerId = localPlayerId;
		m_activeSkillId = std::string(skillId);
		m_haveLastPreviewPos = false;
		m_timeSinceLastDecalSpawn = 0.0f;

		LOG_INFO(Core, "[AoEPreviewSystem] Active skill set (player_id={}, skill={})", m_localPlayerId, m_activeSkillId);
		return true;
	}

	void AoEPreviewSystem::Update(const engine::platform::Input& input, const engine::render::Camera& camera, uint32_t viewportWidth, uint32_t viewportHeight, float dtSeconds)
	{
		if (!m_initialized || m_decalSystem == nullptr || m_skillSystem == nullptr || m_activeSkillId.empty())
			return;

		if (viewportWidth == 0 || viewportHeight == 0)
		{
			LOG_WARN(Core, "[AoEPreviewSystem] Update skipped: invalid viewport {}x{}", viewportWidth, viewportHeight);
			return;
		}

		const engine::gameplay::SkillSystem::SkillDefinition* def = m_skillSystem->FindSkill(m_activeSkillId);
		if (def == nullptr)
		{
			LOG_WARN(Core, "[AoEPreviewSystem] Update skipped: missing skill definition '{}'", m_activeSkillId);
			return;
		}

		const float groundY = static_cast<float>(m_config->GetDouble("skills.aoe_preview_ground_y", 0.0));
		engine::math::Vec3 hitPos{};
		if (!TryRaycastMouseToGround(input, camera, viewportWidth, viewportHeight, groundY, hitPos))
		{
			// No hit: keep previous decal until it expires.
			return;
		}

		// Compute placement direction from camera forward in XZ plane.
		engine::math::Vec3 dirXZ = ComputeCameraForwardXZ(camera);
		if (dirXZ.LengthSq() <= 0.0000001f)
		{
			dirXZ = engine::math::Vec3(0.0f, 0.0f, 1.0f);
		}
		dirXZ = dirXZ.Normalized();

		// Spawn preview decal at a limited cadence.
		const float spawnInterval = static_cast<float>(m_config->GetDouble("skills.aoe_preview_decal_spawn_interval_s", 0.08));
		m_timeSinceLastDecalSpawn += std::max(0.0f, dtSeconds);

		const bool posChanged = !m_haveLastPreviewPos
			|| (hitPos - m_lastPreviewPos).LengthSq() > 0.01f;

		if (posChanged && m_timeSinceLastDecalSpawn >= spawnInterval)
		{
			m_timeSinceLastDecalSpawn = 0.0f;
			m_lastPreviewPos = hitPos;
			m_haveLastPreviewPos = true;

			const float previewRadius = ResolvePreviewRadiusMeters(*def);
			const float decalY = groundY + static_cast<float>(m_config->GetDouble("skills.aoe_preview_decal_center_y", 0.05));
			const float decalHalfY = static_cast<float>(m_config->GetDouble("skills.aoe_preview_decal_half_extents_y", 0.05));

			engine::render::DecalComponent decal{};
			decal.center = engine::math::Vec3(hitPos.x, decalY, hitPos.z);
			decal.halfExtents = engine::math::Vec3(previewRadius, decalHalfY, previewRadius);
			decal.albedoTexturePath = m_config->GetString("skills.aoe_preview_decal_texture_path", "textures/test.texr");
			decal.lifetimeSeconds = static_cast<float>(m_config->GetDouble("skills.aoe_preview_decal_lifetime_s", 0.2));
			decal.fadeDurationSeconds = static_cast<float>(m_config->GetDouble("skills.aoe_preview_decal_fade_s", 0.08));

			if (!m_decalSystem->Spawn(decal))
			{
				LOG_WARN(Core, "[AoEPreviewSystem] Preview decal spawn failed (skill={}, pos=({:.2f},{:.2f},{:.2f}))",
					def->id, hitPos.x, hitPos.y, hitPos.z);
			}
		}

		// Confirm placement on left click.
		if (input.WasMousePressed(engine::platform::MouseButton::Left))
		{
			if (!m_skillSystem->UseAoESkill(m_localPlayerId, m_activeSkillId, hitPos, dirXZ))
				LOG_WARN(Core, "[AoEPreviewSystem] Confirm failed: UseAoESkill rejected (player_id={}, skill={})",
					m_localPlayerId, m_activeSkillId);
		}
	}

	bool AoEPreviewSystem::TryRaycastMouseToGround(const engine::platform::Input& input, const engine::render::Camera& camera, uint32_t viewportWidth, uint32_t viewportHeight, float groundY, engine::math::Vec3& outHitPos) const
	{
		const Ray ray = BuildCameraRay(camera, viewportWidth, viewportHeight, input.MouseX(), input.MouseY());

		const float denom = ray.direction.y;
		if (std::abs(denom) <= 0.000001f)
			return false;

		const float t = (groundY - ray.origin.y) / denom;
		if (t < 0.0f)
			return false;

		outHitPos = ray.origin + ray.direction * t;
		return true;
	}

	AoEPreviewSystem::Ray AoEPreviewSystem::BuildCameraRay(const engine::render::Camera& camera, uint32_t viewportWidth, uint32_t viewportHeight, int mouseX, int mouseY) const
	{
		// Reuse the same ray math as EditorMode: unproject from screen via camera yaw/pitch.
		constexpr float kPi = 3.14159265f;
		const float aspect = viewportHeight > 0
			? static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight)
			: camera.aspect;
		const float halfTan = std::tan((camera.fovYDeg * kPi / 180.0f) * 0.5f);

		const float ndcX = ((static_cast<float>(mouseX) + 0.5f) / static_cast<float>(viewportWidth)) * 2.0f - 1.0f;
		const float ndcY = 1.0f - ((static_cast<float>(mouseY) + 0.5f) / static_cast<float>(viewportHeight));

		const float cy = std::cos(camera.yaw);
		const float sy = std::sin(camera.yaw);
		const float cp = std::cos(camera.pitch);
		const float sp = std::sin(camera.pitch);

		const engine::math::Vec3 forward(-sy * cp, -sp, -cy * cp);
		engine::math::Vec3 right(forward.z, 0.0f, -forward.x);
		const float rightLen = right.Length();
		right = rightLen > 0.0f ? right * (1.0f / rightLen) : engine::math::Vec3(1.0f, 0.0f, 0.0f);

		engine::math::Vec3 up(
			forward.y * right.z - forward.z * right.y,
			forward.z * right.x - forward.x * right.z,
			forward.x * right.y - forward.y * right.x);
		const float upLen = up.Length();
		up = upLen > 0.0f ? up * (1.0f / upLen) : engine::math::Vec3(0.0f, 1.0f, 0.0f);

		engine::math::Vec3 direction = (forward + right * (ndcX * aspect * halfTan) + up * (ndcY * halfTan)).Normalized();
		return { camera.position, direction };
	}

	engine::math::Vec3 AoEPreviewSystem::ComputeCameraForwardXZ(const engine::render::Camera& camera) const
	{
		const float cy = std::cos(camera.yaw);
		const float sy = std::sin(camera.yaw);
		const float cp = std::cos(camera.pitch);
		const float sp = std::sin(camera.pitch);
		(void)sp;

		// forward is (X,Y,Z) with camera looking along -Z (see EditorMode).
		const engine::math::Vec3 forward(-sy * cp, -sp, -cy * cp);
		return engine::math::Vec3(forward.x, 0.0f, forward.z);
	}

	float AoEPreviewSystem::ResolvePreviewRadiusMeters(const engine::gameplay::SkillSystem::SkillDefinition& def) const
	{
		switch (def.aoe.shape)
		{
		case engine::gameplay::SkillSystem::AoEShapeType::Circle:
			return std::max(0.01f, def.aoe.radiusMeters);
		case engine::gameplay::SkillSystem::AoEShapeType::Cone:
		{
			// Approximate cone base radius for preview: r = tan(angle/2)*range.
			const float halfAngleRad = (def.aoe.angleDeg * 0.5f) * (3.14159265f / 180.0f);
			return std::max(0.01f, std::tan(halfAngleRad) * def.aoe.rangeMeters);
		}
		case engine::gameplay::SkillSystem::AoEShapeType::Line:
		{
			// Approximate by using the max dimension as radius on the ground.
			const float r = std::max(def.aoe.widthMeters * 0.5f, def.aoe.lengthMeters * 0.5f);
			return std::max(0.01f, r);
		}
		case engine::gameplay::SkillSystem::AoEShapeType::Ring:
			return std::max(0.01f, def.aoe.outerRadiusMeters);
		default:
			return 1.0f;
		}
	}
}

