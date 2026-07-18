#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <vector>

namespace engine::render
{
	/// Paramètres ciel pour la capture IBL, remplis par l'Engine depuis
	/// `DayNightCycle::State` au boot. Servent à porter la logique de `sky.frag`
	/// dans le compute `sky_capture.comp` (gradient + sun glow + disque lunaire).
	///
	/// `moonDir` = -lightDir (convention Engine) ; `moonIntensity` = 0 le jour,
	/// 1 la nuit ; `moonPhase` ∈ [0,15] ; `moonIllumination` ∈ [0,1].
	struct SkyCaptureParams
	{
		float lightDir[3]     = {0.0f, 1.0f, 0.0f};
		float zenithColor[3]  = {0.0f, 0.0f, 0.0f};
		float horizonColor[3] = {0.0f, 0.0f, 0.0f};
		float moonDir[3]      = {0.0f, -1.0f, 0.0f};
		float moonIntensity   = 0.0f;
		float moonPhase       = 0.0f;
		float moonIllumination = 0.0f;
		/// Chantier ciel 2026-07-18 (lot 2) — sélecteur de modèle de ciel
		/// pour la capture IBL : 0 = dégradé legacy, 1 = diffusion
		/// analytique Rayleigh+Mie (même modèle que sky.frag). Rempli par
		/// l'Engine depuis `client.sky.analytic` : l'ambiance IBL reste
		/// alignée sur le ciel réellement affiché.
		float skyModel        = 0.0f;
	};

	/// SkyCubeCapturePass (IBL) : capture le ciel procédural dans une cubemap
	/// RGBA16F (128² par face, 1 mip) via un compute one-shot au boot.
	///
	/// Modèle calqué sur `SpecularPrefilterPass` (image cube storage+sampled,
	/// vue cube pour le sampling + 6 vues 2D par face pour l'écriture storage) et
	/// `BrdfLutPass` (passe compute one-shot, command pool interne, submit +
	/// waitIdle). La cubemap produite alimente ensuite `SpecularPrefilterPass` et
	/// l'irradiance. `Generate()` dispatche le compute une fois par face.
	class SkyCubeCapturePass
	{
	public:
		SkyCubeCapturePass() = default;
		SkyCubeCapturePass(const SkyCubeCapturePass&) = delete;
		SkyCubeCapturePass& operator=(const SkyCubeCapturePass&) = delete;

		/// Initialise la cubemap de sortie (storage+sampled), ses vues (cube +
		/// 6 faces 2D), le sampler linéaire, le descriptor layout (1 binding
		/// STORAGE_IMAGE compute), le range push-constant et le pipeline compute.
		///
		/// Vérifie le support `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT` du format
		/// `R16G16B16A16_SFLOAT` avant de créer l'image ; en cas d'absence,
		/// retourne false (repli géré par l'appelant). Tout échec d'init appelle
		/// `Destroy` et retourne false.
		///
		/// \param vmaAllocator      Alloc GPU centralisé (VMA) ; conservé pour
		///                          compat mais non utilisé (alloc Vulkan brute).
		/// \param faceSize          Taille d'une face en texels (ex. 128).
		/// \param compSpirv         SPIR-V du compute `sky_capture.comp`.
		/// \param compWordCount     Nombre de mots 32 bits dans compSpirv.
		/// \param queueFamilyIndex  Famille de file pour le command pool interne.
		/// \return true en cas de succès.
		/// \note À appeler en phase de warmup (crée un pipeline compute).
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			void* vmaAllocator,
			uint32_t faceSize,
			const uint32_t* compSpirv, size_t compWordCount,
			uint32_t queueFamilyIndex,
			VkPipelineCache pipelineCache = VK_NULL_HANDLE);

		/// Exécute la capture : pour chaque face (0..5), met à jour le descriptor
		/// (vue storage de la face), transitionne UNDEFINED→GENERAL, push les
		/// paramètres ciel + dispatch, puis transitionne GENERAL→SHADER_READ_ONLY.
		/// Soumet le command buffer et attend l'inactivité de la file.
		///
		/// \param params Paramètres ciel (lightDir, couleurs, lune) figés au boot.
		/// \return true en cas de succès.
		bool Generate(VkDevice device, VkQueue queue, const SkyCaptureParams& params);

		/// Libère toutes les ressources Vulkan. Sûr même si Init a échoué.
		void Destroy(VkDevice device);

		/// Vue cube couvrant les 6 faces (pour le sampling en passes aval).
		VkImageView GetImageView() const { return m_cubeView; }

		/// Sampler de la cubemap (linéaire, mipmapMode NEAREST, maxLod 0).
		VkSampler GetSampler() const { return m_sampler; }

		/// Indique si la passe est initialisée et utilisable.
		bool IsValid() const { return m_image != VK_NULL_HANDLE && m_cubeView != VK_NULL_HANDLE; }

	private:
		void*                    m_vmaAllocator   = nullptr;
		VkImage                  m_image          = VK_NULL_HANDLE;
		void*                    m_allocation     = nullptr; ///< VkDeviceMemory stocké en void*
		VkImageView              m_cubeView       = VK_NULL_HANDLE;
		VkSampler                m_sampler        = VK_NULL_HANDLE;
		std::vector<VkImageView> m_faceViews;     ///< 6 vues 2D (une par face) pour l'écriture storage
		VkDescriptorSetLayout    m_setLayout      = VK_NULL_HANDLE;
		VkDescriptorPool         m_descPool       = VK_NULL_HANDLE;
		VkDescriptorSet          m_descSet        = VK_NULL_HANDLE;
		VkPipelineLayout         m_pipelineLayout = VK_NULL_HANDLE;
		VkPipeline               m_pipeline       = VK_NULL_HANDLE;
		VkCommandPool            m_cmdPool        = VK_NULL_HANDLE;
		uint32_t                 m_faceSize       = 0;
	};
}
