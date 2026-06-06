#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <vector>

namespace engine::render
{
	/// IrradiancePass (IBL diffus) : convolue une cubemap d'environnement HDR
	/// source en une cubemap d'irradiance diffuse (cosine-weighted hemisphere).
	///
	/// Calque quasi-identique de SpecularPrefilterPass mais : 1 SEUL mip, faces
	/// de 32² (l'irradiance diffuse est basse fréquence), convolution cosine au
	/// lieu de GGX. Sortie : cubemap RGBA16F 32² échantillonnable le long de la
	/// normale pour le terme diffus split-sum. L'irradiance produite est
	/// pré-intégrée avec un facteur PI (convention LearnOpenGL : `lighting.frag`
	/// fait `irradiance * albedo` sans /PI). Generate() requiert une vue+sampler
	/// de cubemap source valides ; sinon Generate() retourne false.
	class IrradiancePass
	{
	public:
		IrradiancePass() = default;
		IrradiancePass(const IrradiancePass&) = delete;
		IrradiancePass& operator=(const IrradiancePass&) = delete;

		/// Initialise la cubemap de sortie (1 mip), le descriptor layout/pool
		/// (2 bindings : source samplerCube + image storage), et le pipeline
		/// compute. Ne requiert pas de cubemap source.
		/// \param faceSize    Taille d'une face (typiquement 32).
		/// \param compSpirv   SPIR-V du compute irradiance_convolve.comp.
		/// \param compWordCount Nombre de mots 32 bits dans compSpirv.
		/// \param vmaAllocator Allocateur GPU centralisé (VMA) ; conservé pour
		///        compat (l'alloc réelle reste Vulkan brut DEVICE_LOCAL).
		/// \return true en cas de succès. false propre (Destroy interne) si le
		///         format RGBA16F ne supporte pas STORAGE_IMAGE ou si une
		///         création Vulkan échoue.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			void* vmaAllocator,
			uint32_t faceSize,
			const uint32_t* compSpirv, size_t compWordCount,
			uint32_t queueFamilyIndex,
			VkPipelineCache pipelineCache = VK_NULL_HANDLE);

		/// Exécute la convolution : pour chaque face, échantillonne la cubemap
		/// source et écrit l'irradiance, puis transitionne la cubemap en
		/// SHADER_READ_ONLY_OPTIMAL. submit + waitIdle (one-shot au boot).
		/// \return true en cas de succès ; false si vue/sampler source nuls ou
		///         si la soumission échoue.
		bool Generate(VkDevice device, VkQueue queue,
			VkImageView sourceCubemapView, VkSampler sourceCubemapSampler);

		/// Libère toutes les ressources Vulkan. Sûr même si Init a échoué.
		void Destroy(VkDevice device);

		/// Vue cubemap couvrant les 6 faces (pour l'échantillonnage diffus).
		VkImageView GetImageView() const { return m_cubeView; }

		/// Sampler de la cubemap d'irradiance (linéaire, sans mip).
		VkSampler GetSampler() const { return m_sampler; }

		/// Indique si la passe est initialisée et utilisable (Generate peut ne
		/// pas encore avoir tourné si aucune source n'a été fournie).
		bool IsValid() const { return m_image != VK_NULL_HANDLE && m_cubeView != VK_NULL_HANDLE; }

	private:
		void*                      m_vmaAllocator = nullptr;
		VkImage                    m_image       = VK_NULL_HANDLE;
		void*                      m_allocation   = nullptr; ///< VkDeviceMemory
		VkImageView                m_cubeView     = VK_NULL_HANDLE;
		VkSampler                  m_sampler     = VK_NULL_HANDLE;
		std::vector<VkImageView>   m_faceViews;  ///< 6 vues 2D (une par face) pour l'écriture.
		VkDescriptorSetLayout      m_setLayout    = VK_NULL_HANDLE;
		VkDescriptorPool           m_descPool    = VK_NULL_HANDLE;
		VkDescriptorSet            m_descSet     = VK_NULL_HANDLE;
		VkPipelineLayout           m_pipelineLayout = VK_NULL_HANDLE;
		VkPipeline                 m_pipeline    = VK_NULL_HANDLE;
		VkCommandPool              m_cmdPool     = VK_NULL_HANDLE;
		uint32_t                   m_size        = 0;
	};
}
