#include "src/client/combat/TargetReticleSystem.h"

#include "src/shared/core/Log.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace engine::client
{
	namespace
	{
		/// Résolution de la texture procédurale du réticule (texels de côté).
		/// 256 suffit pour un decal de ~2 m vu en 3e personne (≈ 7 mm/texel).
		constexpr uint32_t kReticleTextureSize = 256u;
	}

	TargetReticleSystem::~TargetReticleSystem()
	{
		Shutdown();
	}

	bool TargetReticleSystem::Init(const engine::core::Config& config,
		engine::render::DecalSystem& decalSystem,
		engine::render::AssetRegistry& assetRegistry,
		SampleGroundHeightFn sampleGroundHeight,
		ResolveSmoothedStateFn resolveSmoothedState)
	{
		if (m_initialized)
		{
			LOG_WARN(Render, "[TargetReticleSystem] Init ignored: already initialized");
			return true;
		}
		if (!sampleGroundHeight)
		{
			LOG_ERROR(Render, "[TargetReticleSystem] Init FAILED: missing ground height sampler");
			return false;
		}

		m_params = TargetReticleParams::FromConfig(config);
		m_projectionHalfHeightMeters = std::clamp(
			static_cast<float>(config.GetDouble("target_reticle.projection_half_height", 1.0)),
			0.1f, 10.0f);

		// Texture procédurale (sRGB) rasterisée depuis le paramétrage config.
		std::vector<uint8_t> pixels;
		BakeReticleRgba(m_params, kReticleTextureSize, pixels);
		engine::render::TextureHandle texture = assetRegistry.CreateTextureFromMemory(
			pixels.data(), kReticleTextureSize, kReticleTextureSize, /*useSrgb=*/true);
		if (!texture.IsValid())
		{
			LOG_WARN(Render, "[TargetReticleSystem] Init FAILED: reticle texture creation failed");
			return false;
		}

		// Decal persistant, caché (alpha 0) tant qu'aucune cible n'est sélectionnée.
		engine::render::DecalComponent component{};
		const float halfExtent = ReticleHalfExtentMeters(m_params);
		component.halfExtents = engine::math::Vec3(halfExtent, m_projectionHalfHeightMeters, halfExtent);
		m_decalHandle = decalSystem.SpawnPersistent(component, texture);
		if (m_decalHandle == 0)
		{
			LOG_WARN(Render, "[TargetReticleSystem] Init FAILED: persistent decal spawn failed");
			return false;
		}

		m_decalSystem = &decalSystem;
		m_sampleGroundHeight = std::move(sampleGroundHeight);
		m_resolveSmoothedState = std::move(resolveSmoothedState);
		m_fade = TargetReticleFade{};
		m_initialized = true;

		LOG_INFO(Render, "[TargetReticleSystem] Init OK (radius={:.2f} m, arc={:.0f} deg, handle={})",
			m_params.radiusMeters, m_params.visionArcDegrees, m_decalHandle);
		return true;
	}

	void TargetReticleSystem::Shutdown()
	{
		if (!m_initialized)
			return;

		if (m_decalSystem != nullptr && m_decalHandle != 0)
			m_decalSystem->DespawnPersistent(m_decalHandle);

		m_initialized = false;
		m_decalSystem = nullptr;
		m_sampleGroundHeight = nullptr;
		m_resolveSmoothedState = nullptr;
		m_decalHandle = 0;
		m_fade = TargetReticleFade{};

		LOG_INFO(Render, "[TargetReticleSystem] Destroyed");
	}

	void TargetReticleSystem::Update(const UIModel& uiModel, const engine::render::Camera& camera, float dtSeconds)
	{
		// Le culling distance et le tri sont gérés par DecalSystem::BuildVisibleList ;
		// la caméra reste dans la signature (parité AoEPreviewSystem, évolutions futures).
		(void)camera;

		if (!m_initialized || m_decalSystem == nullptr)
			return;

		// --- Résolution de la cible : entité distante du snapshot (position +
		// yaw 10 Hz), surclassée par l'état lissé si disponible (yaw fluide).
		bool targetResolved = false;
		if (uiModel.targetStats.hasTarget)
		{
			for (const UIRemoteEntity& remote : uiModel.remoteEntities)
			{
				if (remote.entityId != uiModel.targetStats.entityId)
					continue;
				float x = remote.positionX;
				float z = remote.positionZ;
				float yaw = remote.yawRadians;
				if (m_resolveSmoothedState)
				{
					float sx = 0.0f, sz = 0.0f, syaw = 0.0f;
					if (m_resolveSmoothedState(remote.entityId, sx, sz, syaw))
					{
						x = sx;
						z = sz;
						yaw = syaw;
					}
				}
				m_lastX = x;
				m_lastZ = z;
				m_lastYaw = yaw;
				targetResolved = true;
				break;
			}
			// Repli : cible sortie de l'AoI mais position connue côté combat
			// (UITargetStats) — yaw figé sur la dernière valeur résolue.
			if (!targetResolved && uiModel.targetStats.hasPosition)
			{
				m_lastX = uiModel.targetStats.positionX;
				m_lastZ = uiModel.targetStats.positionZ;
				targetResolved = true;
			}
		}

		m_fade.Update(targetResolved, m_params, dtSeconds);

		if (!m_fade.IsVisible())
		{
			// Caché : alpha 0 → exclu de la liste visible (aucun draw).
			if (!m_decalSystem->UpdatePersistent(m_decalHandle,
				engine::math::Vec3(m_lastX, 0.0f, m_lastZ),
				engine::math::Vec3(0.0f, 0.0f, 0.0f), m_lastYaw, 0.0f))
			{
				// Handle perdu (ex. DecalSystem ré-initialisé) : on se désactive
				// proprement plutôt que de spammer un warn à chaque frame.
				LOG_WARN(Render, "[TargetReticleSystem] Decal handle perdu — réticule désactivé");
				Shutdown();
			}
			return;
		}

		// --- Pose au sol : hauteur échantillonnée au centre ET aux 4 points
		// cardinaux du rayon extérieur — le volume de projection couvre ainsi
		// la variation de pente locale (le decal projeté sur le depth épouse
		// ensuite exactement le relief, sans z-fighting par construction).
		const float halfExtent = ReticleHalfExtentMeters(m_params);
		float minY = m_sampleGroundHeight(m_lastX, m_lastZ);
		float maxY = minY;
		const float offsets[4][2] = {
			{ halfExtent, 0.0f }, { -halfExtent, 0.0f },
			{ 0.0f, halfExtent }, { 0.0f, -halfExtent } };
		for (const float* offset : offsets)
		{
			const float y = m_sampleGroundHeight(m_lastX + offset[0], m_lastZ + offset[1]);
			minY = std::min(minY, y);
			maxY = std::max(maxY, y);
		}

		const engine::math::Vec3 center(m_lastX, (minY + maxY) * 0.5f, m_lastZ);
		const engine::math::Vec3 halfExtents(
			halfExtent,
			(maxY - minY) * 0.5f + m_projectionHalfHeightMeters,
			halfExtent);
		if (!m_decalSystem->UpdatePersistent(m_decalHandle, center, halfExtents, m_lastYaw, m_fade.alpha))
		{
			LOG_WARN(Render, "[TargetReticleSystem] Decal handle perdu — réticule désactivé");
			Shutdown();
		}
	}
}
