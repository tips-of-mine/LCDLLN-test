#include "engine/render/DecalSystem.h"

#include "engine/core/Log.h"

#include <algorithm>

namespace engine::render
{
	DecalSystem::~DecalSystem()
	{
		Shutdown();
	}

	bool DecalSystem::Init(const engine::core::Config& config, AssetRegistry& assetRegistry)
	{
		Shutdown();
		m_config = &config;
		m_assetRegistry = &assetRegistry;
		m_maxVisibleDistanceMeters = static_cast<float>(config.GetDouble("decals.max_visible_distance_m", 64.0));
		m_initialized = true;
		LOG_INFO(Render, "[DecalSystem] Init OK (max_visible_distance_m={:.2f})", m_maxVisibleDistanceMeters);
		return true;
	}

	void DecalSystem::Shutdown()
	{
		m_decals.clear();
		m_config = nullptr;
		m_assetRegistry = nullptr;
		m_maxVisibleDistanceMeters = 64.0f;
		m_initialized = false;
		LOG_INFO(Render, "[DecalSystem] Shutdown complete");
	}

	bool DecalSystem::Spawn(const DecalComponent& component)
	{
		if (!m_initialized || m_assetRegistry == nullptr)
		{
			LOG_WARN(Render, "[DecalSystem] Spawn ignored: system not initialized");
			return false;
		}

		if (component.albedoTexturePath.empty())
		{
			LOG_WARN(Render, "[DecalSystem] Spawn ignored: missing albedo texture path");
			return false;
		}

		TextureHandle texture = m_assetRegistry->LoadTexture(component.albedoTexturePath, true);
		if (!texture.IsValid())
		{
			LOG_ERROR(Render, "[DecalSystem] Spawn FAILED: unable to load texture '{}'", component.albedoTexturePath);
			return false;
		}

		ActiveDecal decal{};
		decal.component = component;
		decal.albedoTexture = texture;
		decal.fadeAlpha = 1.0f;
		decal.active = true;
		m_decals.push_back(std::move(decal));

		LOG_INFO(Render, "[DecalSystem] Spawned decal (texture='{}', lifetime={:.2f}, fade={:.2f})",
			component.albedoTexturePath, component.lifetimeSeconds, component.fadeDurationSeconds);
		return true;
	}

	bool DecalSystem::Tick(float deltaSeconds)
	{
		if (!m_initialized)
		{
			LOG_WARN(Render, "[DecalSystem] Tick ignored: system not initialized");
			return false;
		}

		if (deltaSeconds <= 0.0f)
		{
			LOG_WARN(Render, "[DecalSystem] Tick ignored: invalid dt={:.4f}", deltaSeconds);
			return false;
		}

		uint32_t activeCount = 0;
		for (ActiveDecal& decal : m_decals)
		{
			if (!decal.active)
			{
				continue;
			}

			decal.ageSeconds += deltaSeconds;
			if (decal.component.lifetimeSeconds > 0.0f && decal.ageSeconds >= decal.component.lifetimeSeconds)
			{
				decal.active = false;
				continue;
			}

			decal.fadeAlpha = 1.0f;
			if (decal.component.fadeDurationSeconds > 0.0f && decal.component.lifetimeSeconds > 0.0f)
			{
				const float fadeStart = std::max(0.0f, decal.component.lifetimeSeconds - decal.component.fadeDurationSeconds);
				if (decal.ageSeconds > fadeStart)
				{
					const float remaining = decal.component.lifetimeSeconds - decal.ageSeconds;
					decal.fadeAlpha = std::clamp(remaining / decal.component.fadeDurationSeconds, 0.0f, 1.0f);
				}
			}

			++activeCount;
		}

		LOG_DEBUG(Render, "[DecalSystem] Tick OK (active_decals={}, dt={:.4f})", activeCount, deltaSeconds);
		return true;
	}

	void DecalSystem::BuildVisibleList(const Camera& camera, std::vector<VisibleDecal>& outVisibleDecals) const
	{
		outVisibleDecals.clear();
		if (!m_initialized)
		{
			LOG_WARN(Render, "[DecalSystem] BuildVisibleList ignored: system not initialized");
			return;
		}

		const float maxDistanceSq = m_maxVisibleDistanceMeters * m_maxVisibleDistanceMeters;
		for (const ActiveDecal& decal : m_decals)
		{
			if (!decal.active || !decal.albedoTexture.IsValid())
			{
				continue;
			}

			TextureAsset* texture = decal.albedoTexture.Get();
			if (texture == nullptr || texture->view == VK_NULL_HANDLE)
			{
				LOG_WARN(Render, "[DecalSystem] Skipping decal: invalid texture view");
				continue;
			}

			const engine::math::Vec3 toCamera = camera.position - decal.component.center;
			const float distanceSq = toCamera.LengthSq();
			if (distanceSq > maxDistanceSq)
			{
				continue;
			}

			VisibleDecal visible{};
			visible.center = decal.component.center;
			visible.halfExtents = decal.component.halfExtents;
			visible.texture = texture;
			visible.fadeAlpha = decal.fadeAlpha;
			visible.distanceToCameraSq = distanceSq;
			outVisibleDecals.push_back(visible);
		}

		std::sort(outVisibleDecals.begin(), outVisibleDecals.end(),
			[](const VisibleDecal& a, const VisibleDecal& b)
			{
				return a.distanceToCameraSq > b.distanceToCameraSq;
			});

		LOG_DEBUG(Render, "[DecalSystem] Built visible list (count={})", static_cast<uint32_t>(outVisibleDecals.size()));
	}
}
