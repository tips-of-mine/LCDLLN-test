#pragma once

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>

namespace engine::render::gi
{
	class DdgiVolume;

	/// M45.7 — Passe COMPUTE auto-gérée qui met à jour l'atlas d'irradiance d'un
	/// `DdgiVolume` chaque frame (« GI dynamique phase 2 »).
	///
	/// PORTÉE v1 ASSUMÉE (conservatrice, à régler visuellement — PAS une DDGI
	/// complète à rebonds) : le moteur n'a NI ray tracing NI voxelisation. Cette
	/// passe N'ÉCHANTILLONNE PAS la géométrie de la scène (aucun rebond). Pour
	/// chaque sonde, chaque texel octaédrique reçoit :
	///   - l'irradiance du CIEL (gradient procédural horizon→zénith par direction) ;
	///   - la contribution du SOLEIL (couleur * max(dot(dir, sunDir), 0)) ;
	///   - une occlusion soleil GROSSIÈRE via la shadow map cascade 0, évaluée au
	///     CENTRE de la sonde (pas par texel) ;
	/// le tout combiné temporellement (hysteresis) avec l'irradiance précédente.
	/// C'est une « irradiance de ciel/soleil dynamique par sonde ».
	///
	/// ARCHITECTURE : auto-gérée HORS frame graph pour l'écriture storage (comme
	/// `HiZPyramidPass`). La passe pose elle-même ses barrières d'image
	/// (UNDEFINED/SHADER_READ_ONLY -> GENERAL pour écrire, GENERAL ->
	/// SHADER_READ_ONLY pour que le LightingPass échantillonne ensuite). La
	/// shadow map cascade 0 est fournie en SAMPLED par l'appelant (sa barrière de
	/// lecture est gérée par le frame graph via un `read(...)` côté Engine).
	///
	/// WRITER UNIQUE : cette passe est le SEUL écrivain de l'atlas d'irradiance ;
	/// le LightingPass n'en est que lecteur.
	class DdgiUpdatePass
	{
	public:
		/// Push constants de la passe (112 octets, ≤128 → tient en push constants).
		/// Tous les vecteurs sont 4 floats (w = padding ou champ documenté).
		/// L'ordre doit rester aligné sur le bloc `push_constant` de ddgi_update.comp.
		struct DdgiUpdateParams
		{
			float gridOrigin[4];  ///< xyz = origine monde de la sonde (0,0,0) en mètres ; w inutilisé.
			float gridSpacing[4]; ///< xyz = espacement entre sondes par axe (mètres) ; w inutilisé.
			uint32_t counts[4];   ///< xyz = nb de sondes par axe (X,Y,Z) ; w = irradianceTexels (côté hors bordure).
			float sunDir[4];      ///< xyz = direction NORMALISÉE *vers* le soleil ; w inutilisé.
			float sunColor[4];    ///< xyz = couleur/intensité du soleil ; w inutilisé.
			float skyColor[4];    ///< xyz = couleur d'horizon du ciel (DayNightCycle skyHorizon) ; w inutilisé.
			float params[4];      ///< x = hysteresis [0..1] (élevé = lent/stable), y = atlasCols, z = tileSize (texels+2), w = indice de frame courant (M45.7b — amortissement : modulo kUpdateDivisor côté shader).
		};
		static_assert(sizeof(DdgiUpdateParams) == 112, "DdgiUpdateParams doit faire 112 octets (<=128 pour push constants)");

		DdgiUpdatePass() = default;
		DdgiUpdatePass(const DdgiUpdatePass&) = delete;
		DdgiUpdatePass& operator=(const DdgiUpdatePass&) = delete;

		/// Crée le pipeline compute (shader `ddgi_update.comp`), le descriptor set
		/// layout (binding 0 = storage image irradiance, binding 1 = sampled image
		/// shadow cascade 0), le pool/set et le sampler de lecture shadow.
		/// \param device          Device logique Vulkan.
		/// \param phys            Physical device (réservé ; non utilisé en v1).
		/// \param compSpirv       Mots SPIR-V du shader compute (non nullptr).
		/// \param compWordCount   Nombre de mots SPIR-V (> 0).
		/// \param pipelineCache   Cache de pipeline optionnel (warmup PSO).
		/// \return true si toutes les ressources sont prêtes.
		/// \note Doit être appelée en main thread, avant tout `Record`.
		bool Init(VkDevice device, VkPhysicalDevice phys,
			const uint32_t* compSpirv, size_t compWordCount,
			VkPipelineCache pipelineCache = VK_NULL_HANDLE);

		/// Enregistre la mise à jour de l'atlas d'irradiance dans `cmd`.
		/// Effet de bord : met à jour le descriptor set (vue storage du volume +
		/// shadow), transitionne l'atlas irradiance en GENERAL, dispatch (groupes
		/// 8x8 couvrant l'atlas), puis transitionne en SHADER_READ_ONLY_OPTIMAL.
		/// \param device          Device logique Vulkan.
		/// \param cmd             Command buffer en cours d'enregistrement.
		/// \param volume          Volume DDGI (doit être alloué : IrradianceView() valide).
		/// \param shadowCascade0  Vue de la shadow map cascade 0 (SHADER_READ_ONLY_OPTIMAL).
		/// \param shadowSamp      Sampler de lecture de la shadow map (peut être le sampler interne).
		/// \param params          Paramètres de mise à jour (ciel/soleil/hysteresis/layout).
		void Record(VkDevice device, VkCommandBuffer cmd, const DdgiVolume& volume,
			VkImageView shadowCascade0, VkSampler shadowSamp,
			const DdgiUpdateParams& params);

		/// Libère toutes les ressources Vulkan. Sûr même si Init a échoué.
		void Destroy(VkDevice device);

		/// true si Init a réussi (pipeline compute valide).
		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

		/// Sampler interne (NEAREST clamp) utilisable pour lire la shadow map si
		/// l'appelant n'en fournit pas un dédié. VK_NULL_HANDLE si non initialisé.
		VkSampler GetShadowSampler() const { return m_shadowSampler; }

	private:
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
		VkDescriptorSet       m_descriptorSet       = VK_NULL_HANDLE;
		VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
		VkPipeline            m_pipeline            = VK_NULL_HANDLE;
		VkSampler             m_shadowSampler       = VK_NULL_HANDLE; ///< NEAREST clamp, lecture shadow cascade 0.
	};
} // namespace engine::render::gi
