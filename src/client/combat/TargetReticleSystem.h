#pragma once

#include "src/shared/core/Config.h"
#include "src/client/combat/TargetReticleGeometry.h"
#include "src/client/render/AssetRegistry.h"
#include "src/client/render/Camera.h"
#include "src/client/render/DecalSystem.h"
#include "src/client/ui_common/UIModel.h"

#include <cstdint>
#include <functional>

namespace engine::client
{
	/// Indicateur de ciblage au sol : projette sous l'ennemi ciblé un decal
	/// orienté (deux cercles concentriques clairs + secteur de vision 120°
	/// foncé) qui suit la position lissée et le yaw répliqué du PNJ, avec
	/// fondu d'apparition/disparition au ciblage / à la perte de cible.
	///
	/// Architecture (Option A du ticket) : la géométrie est rasterisée UNE fois
	/// au boot dans une texture procédurale (TargetReticleGeometry, pur CPU,
	/// testé unitairement), uploadée via AssetRegistry::CreateTextureFromMemory,
	/// puis affichée par un decal différé PERSISTANT (DecalSystem/DecalPass) :
	/// la projection sur le depth buffer épouse le terrain non plat sans
	/// z-fighting par construction, et la rotation yaw est appliquée par
	/// decal.frag (push constant).
	///
	/// Cycle de vie calqué sur AoEPreviewSystem : Init / Update (par frame) /
	/// Shutdown, dépendances injectées, aucun accès direct à Engine.
	class TargetReticleSystem final
	{
	public:
		/// Hauteur du sol client en (x, z) monde — source de vérité :
		/// TerrainRenderer::SampleHeightAtWorldXZ (le Y serveur des mobs vaut
		/// souvent 0, il ne faut PAS l'utiliser pour poser le réticule).
		using SampleGroundHeightFn = std::function<float(float worldX, float worldZ)>;

		/// Résolution optionnelle de l'état LISSÉ (x, z, yaw) d'une entité
		/// distante (Engine::m_remoteSmoothed). Retourne false si l'entité n'a
		/// pas d'état lissé : on retombe alors sur le snapshot brut (10 Hz).
		using ResolveSmoothedStateFn = std::function<bool(engine::server::EntityId entityId,
			float& outX, float& outZ, float& outYawRadians)>;

		TargetReticleSystem() = default;
		~TargetReticleSystem();

		/// Initialise le système : charge les paramètres `target_reticle.*`,
		/// rasterise la texture du réticule et crée le decal persistant (caché).
		/// \param sampleGroundHeight   Obligatoire (hauteur du sol client).
		/// \param resolveSmoothedState Optionnel (yaw/position lissés).
		/// \return false si la texture ou le decal n'ont pas pu être créés
		///         (ex. AssetRegistry sans device) — la feature est alors
		///         simplement désactivée, sans impact sur le reste du client.
		/// Effet de bord : crée une texture GPU (AssetRegistry) et un decal
		/// persistant (DecalSystem). Doit être appelé APRÈS leurs Init respectifs.
		bool Init(const engine::core::Config& config,
			engine::render::DecalSystem& decalSystem,
			engine::render::AssetRegistry& assetRegistry,
			SampleGroundHeightFn sampleGroundHeight,
			ResolveSmoothedStateFn resolveSmoothedState = nullptr);

		/// Retire le decal persistant et libère les références runtime.
		/// À appeler AVANT DecalSystem::Shutdown.
		void Shutdown();

		/// Mise à jour par frame : si une cible est sélectionnée et résolue dans
		/// les entités distantes, place le réticule au sol sous elle (hauteur
		/// échantillonnée autour du centre pour épouser la pente) et l'oriente
		/// sur le yaw du PNJ ; sinon déclenche le fondu de disparition.
		/// \param camera Réservé (le culling distance est déjà géré par DecalSystem).
		void Update(const UIModel& uiModel, const engine::render::Camera& camera, float dtSeconds);

		/// Vrai si le réticule est actuellement rendu (alpha > 0).
		bool IsVisible() const { return m_initialized && m_fade.IsVisible(); }

	private:
		bool m_initialized = false;
		engine::render::DecalSystem* m_decalSystem = nullptr;
		SampleGroundHeightFn m_sampleGroundHeight;
		ResolveSmoothedStateFn m_resolveSmoothedState;

		TargetReticleParams m_params{};
		TargetReticleFade m_fade{};
		/// Handle du decal persistant (0 = non créé).
		uint32_t m_decalHandle = 0;
		/// Demi-hauteur du volume de projection (m) : couvre la pente locale,
		/// clé `target_reticle.projection_half_height` (défaut 1.0 m).
		float m_projectionHalfHeightMeters = 1.0f;

		/// Dernière pose connue de la cible — conservée pendant le fade-out
		/// (la cible peut avoir disparu de l'AoI alors que le fondu joue encore).
		float m_lastX = 0.0f;
		float m_lastZ = 0.0f;
		float m_lastYaw = 0.0f;
	};
}
