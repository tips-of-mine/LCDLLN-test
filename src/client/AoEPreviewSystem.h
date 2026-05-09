#pragma once

#include "engine/core/Config.h"
#include "engine/gameplay/SkillSystem.h"
#include "engine/platform/Input.h"
#include "engine/render/Camera.h"
#include "engine/render/DecalSystem.h"

#include <cstdint>
#include <string>

namespace engine::client
{
	/// Minimal client-side AoE targeting preview:
	/// - raycast mouse -> ground plane
	/// - spawn a projected decal preview at the placement
	///
	/// This is intentionally minimal and UI-agnostic: the caller drives which skill/player is previewed.
	class AoEPreviewSystem final
	{
	public:
		AoEPreviewSystem() = default;
		~AoEPreviewSystem();

		/// Initialize preview system with runtime dependencies.
		bool Init(const engine::core::Config& config, engine::render::DecalSystem& decalSystem, engine::gameplay::SkillSystem& skillSystem);

		/// Shutdown and release runtime references.
		void Shutdown();

		/// Set the local player and active skill to preview.
		bool SetActiveSkill(uint32_t localPlayerId, std::string_view skillId);

		/// Update preview placement from mouse and optionally confirm on click.
		void Update(const engine::platform::Input& input, const engine::render::Camera& camera, uint32_t viewportWidth, uint32_t viewportHeight, float dtSeconds);

		bool IsPreviewActive() const { return m_initialized && !m_activeSkillId.empty(); }

	private:
		struct Ray
		{
			engine::math::Vec3 origin{};
			engine::math::Vec3 direction{};
		};

		bool TryRaycastMouseToGround(const engine::platform::Input& input, const engine::render::Camera& camera, uint32_t viewportWidth, uint32_t viewportHeight, float groundY, engine::math::Vec3& outHitPos) const;
		Ray BuildCameraRay(const engine::render::Camera& camera, uint32_t viewportWidth, uint32_t viewportHeight, int mouseX, int mouseY) const;
		engine::math::Vec3 ComputeCameraForwardXZ(const engine::render::Camera& camera) const;
		float ResolvePreviewRadiusMeters(const engine::gameplay::SkillSystem::SkillDefinition& def) const;

	private:
		bool m_initialized = false;
		const engine::core::Config* m_config = nullptr;
		engine::render::DecalSystem* m_decalSystem = nullptr;
		engine::gameplay::SkillSystem* m_skillSystem = nullptr;

		uint32_t m_localPlayerId = 0;
		std::string m_activeSkillId;

		engine::math::Vec3 m_lastPreviewPos{};
		bool m_haveLastPreviewPos = false;
		float m_timeSinceLastDecalSpawn = 0.0f;
	};
}

