#pragma once

#include "engine/core/Config.h"
#include "engine/math/Math.h"
#include "engine/render/AssetRegistry.h"
#include "engine/render/Camera.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::render
{
	/// One decal component definition describing its projected volume and material.
	struct DecalComponent
	{
		engine::math::Vec3 center{};
		engine::math::Vec3 halfExtents{ 0.5f, 0.5f, 0.5f };
		std::string albedoTexturePath;
		float lifetimeSeconds = 0.0f;
		float fadeDurationSeconds = 0.0f;
	};

	/// One active runtime decal with loaded material and fade state.
	struct ActiveDecal
	{
		DecalComponent component{};
		TextureHandle albedoTexture{};
		float ageSeconds = 0.0f;
		float fadeAlpha = 1.0f;
		bool active = false;
	};

	/// One visible decal sorted for rendering and already culled against camera distance.
	struct VisibleDecal
	{
		engine::math::Vec3 center{};
		engine::math::Vec3 halfExtents{};
		TextureAsset* texture = nullptr;
		float fadeAlpha = 1.0f;
		float distanceToCameraSq = 0.0f;
	};

	/// Runtime system managing decal lifetime, fade, distance culling, and sorted visible lists.
	class DecalSystem final
	{
	public:
		/// Construct an empty decal system.
		DecalSystem() = default;

		/// Shutdown the decal system on destruction.
		~DecalSystem();

		/// Initialize the system with config and asset registry access.
		bool Init(const engine::core::Config& config, AssetRegistry& assetRegistry);

		/// Release all decals and references to runtime dependencies.
		void Shutdown();

		/// Spawn one decal instance using a content-relative albedo texture.
		bool Spawn(const DecalComponent& component);

		/// Advance decal lifetimes and fade values.
		bool Tick(float deltaSeconds);

		/// Build a distance-culled, back-to-front visible decal list for the given camera.
		void BuildVisibleList(const Camera& camera, std::vector<VisibleDecal>& outVisibleDecals) const;

	private:
		const engine::core::Config* m_config = nullptr;
		AssetRegistry* m_assetRegistry = nullptr;
		std::vector<ActiveDecal> m_decals;
		float m_maxVisibleDistanceMeters = 64.0f;
		bool m_initialized = false;
	};
}
