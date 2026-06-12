#pragma once

#include "src/shared/core/Config.h"
#include "src/shared/math/Math.h"
#include "src/client/render/AssetRegistry.h"
#include "src/client/render/Camera.h"

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
		/// Rotation du decal autour de Y (radians). 0 = texture alignée monde
		/// (comportement historique). Appliquée par decal.frag (rotation de −yaw
		/// du repère monde vers le repère local avant le test de boîte et l'UV).
		float yawRadians = 0.0f;
	};

	/// One active runtime decal with loaded material and fade state.
	struct ActiveDecal
	{
		DecalComponent component{};
		TextureHandle albedoTexture{};
		float ageSeconds = 0.0f;
		float fadeAlpha = 1.0f;
		bool active = false;
		/// Decal persistant piloté par un système (réticule de ciblage) : pas de
		/// vieillissement par Tick, position/yaw/alpha mis à jour via
		/// \ref DecalSystem::UpdatePersistent. 0 = decal classique (fire-and-forget).
		uint32_t persistentHandle = 0;
	};

	/// One visible decal sorted for rendering and already culled against camera distance.
	struct VisibleDecal
	{
		engine::math::Vec3 center{};
		engine::math::Vec3 halfExtents{};
		TextureAsset* texture = nullptr;
		float fadeAlpha = 1.0f;
		float distanceToCameraSq = 0.0f;
		/// Rotation autour de Y (radians), recopiée du composant (cf. DecalComponent).
		float yawRadians = 0.0f;
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

		/// Crée un decal PERSISTANT piloté par l'appelant (pas de lifetime : il
		/// vit jusqu'à \ref DespawnPersistent ou \ref Shutdown) avec une texture
		/// déjà résolue (ex. texture procédurale via AssetRegistry::CreateTextureFromMemory).
		/// Le decal naît invisible (alpha 0) ; l'appelant le pilote frame par frame
		/// via \ref UpdatePersistent. \return handle (> 0) ou 0 en cas d'échec.
		uint32_t SpawnPersistent(const DecalComponent& component, TextureHandle albedoTexture);

		/// Met à jour position/étendue/yaw/alpha d'un decal persistant.
		/// \param yawRadians Rotation autour de Y (cf. DecalComponent::yawRadians).
		/// \param alpha      Opacité 0..1 (0 = caché, exclu de la liste visible).
		/// \return false si le handle est inconnu ou le système non initialisé.
		bool UpdatePersistent(uint32_t handle, const engine::math::Vec3& center,
			const engine::math::Vec3& halfExtents, float yawRadians, float alpha);

		/// Retire définitivement un decal persistant (no-op si handle inconnu).
		void DespawnPersistent(uint32_t handle);

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
		/// Prochain handle de decal persistant (0 réservé = invalide).
		uint32_t m_nextPersistentHandle = 1;
	};
}
