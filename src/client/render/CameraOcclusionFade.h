#pragma once

#include "src/shared/math/Math.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::render
{
	/// Sphère englobante d'un occulteur potentiel (prop, pièce de bâtiment).
	struct OccluderSphere
	{
		std::uint32_t id = 0;         ///< identifiant stable de l'objet (clé de suivi du fondu)
		engine::math::Vec3 center{};  ///< centre monde de la sphère
		float radius = 0.5f;          ///< rayon monde (m)
	};

	/// Calcule, par frame, un facteur de fondu (transparence tramée) pour chaque
	/// objet occultant la vue entre la caméra et le joueur, avec lissage temporel.
	/// Math pure (aucune dépendance Vulkan) → testable en ctest.
	class CameraOcclusionFade
	{
	public:
		struct Config
		{
			float fadeMin = 0.15f;            ///< opacité mini au cœur de l'occlusion (0 = invisible)
			float radiusMargin = 0.5f;        ///< marge ajoutée au rayon pour la transition (m)
			float fadeInPerSec = 6.0f;        ///< vitesse de retour vers l'opaque (1.0)
			float fadeOutPerSec = 8.0f;       ///< vitesse de passage vers fadeMin
			float playerProtectRadius = 0.6f; ///< occulteur à moins de ça du joueur : jamais fondu
		};

		/// Configure le module (réinitialise l'état de fondu).
		void Init(const Config& cfg);

		/// Met à jour les fondus. \p occluders est reconstruite chaque frame ;
		/// \p focusPoint = point regardé (tête joueur) ; \p dt en secondes.
		/// Effet de bord : met à jour la table interne id→fondu.
		void Update(const engine::math::Vec3& cameraPos,
			const engine::math::Vec3& focusPoint,
			const std::vector<OccluderSphere>& occluders,
			float dt);

		/// Fondu lissé courant d'un objet ; 1.0 (opaque) si l'id est inconnu.
		float FadeFor(std::uint32_t id) const;

	private:
		Config m_cfg{};
		std::unordered_map<std::uint32_t, float> m_fade; ///< id -> fondu lissé ∈ [fadeMin,1]
	};
}
