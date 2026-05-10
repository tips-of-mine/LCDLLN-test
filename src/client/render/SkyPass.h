#pragma once
// SkyPass : pipeline Vulkan qui dessine le ciel + disque lunaire procedural.
// Consomme les shaders game/data/shaders/sky.vert et sky.frag (existants
// jusqu'ici non wires). Le fragment shader recoit en push-constants la
// matrice inverse view-projection, les directions soleil/lune, les couleurs
// zenith/horizon, et les parametres lunaires (phase + illumination + intensity).
//
// La pass est dessinee EN PREMIER dans la frame (avant le geometry pass)
// avec depthTest = false / depthWrite = false pour servir de fond.
// Couts negligeables (1 fullscreen quad genere via gl_VertexIndex).
//
// Pattern aligne sur LightingPass et autres passes Vulkan du repo :
//   - Init  : cree pipeline layout + pipeline + shader modules.
//   - Record: bind pipeline + push constants + draw 3 vertices (fullscreen tri).
//   - Shutdown : destroy pipeline + layout.
//
// Thread-safety : les methodes Init/Shutdown doivent etre appelees sur le
// thread main (le device Vulkan n'est pas thread-safe par defaut). Record
// peut etre appelee depuis un thread render dedie tant qu'on respecte
// l'ordre dans le command buffer.

#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstddef>

namespace engine::render
{
	/// Pipeline Vulkan dedie a la pass ciel + disque lunaire (Phase 5 Lunar).
	class SkyPass
	{
	public:
		/// Push-constants (144 bytes total). Doit matcher exactement le bloc
		/// `SkyPC` dans sky.frag (alignement std140 vec3 = 16 bytes via _padN).
		struct PushConstants
		{
			float invViewProj[16];   ///< mat4
			float lightDir[3];
			float _pad0;
			float zenithColor[3];
			float _pad1;
			float horizonColor[3];
			float _pad2;
			float moonDir[3];
			float moonIntensity;
			float moonPhase;
			float moonIllumination;
			float _pad3[2];
		};
		static_assert(sizeof(PushConstants) == 144, "SkyPass push constants size mismatch");

		/// Cree le pipeline layout + pipeline + shader modules.
		/// \param device         Device Vulkan logique.
		/// \param renderPass     RenderPass parent (typiquement le main scene pass).
		/// \param subpass        Indice du subpass (typiquement 0).
		/// \param vertSpirv      Pointeur sur le SPIR-V vertex (sky.vert.spv).
		/// \param vertWordCount  Nombre de uint32 dans le SPIR-V vertex.
		/// \param fragSpirv      Pointeur sur le SPIR-V fragment (sky.frag.spv).
		/// \param fragWordCount  Nombre de uint32 dans le SPIR-V fragment.
		/// \return true si le pipeline est cree avec succes.
		bool Init(VkDevice device,
		          VkRenderPass renderPass,
		          uint32_t subpass,
		          const uint32_t* vertSpirv, size_t vertWordCount,
		          const uint32_t* fragSpirv, size_t fragWordCount);

		/// Destroy pipeline + layout. Appelable plusieurs fois (idempotent).
		void Shutdown(VkDevice device);

		/// Enregistre le draw fullscreen quad avec les push-constants.
		/// Doit etre appelee dans un command buffer ouvert avec le bon
		/// renderPass actif. No-op si Init a echoue.
		/// \param cmd Command buffer Vulkan en cours d'enregistrement.
		/// \param pc  Push constants a binder pour ce draw.
		void Record(VkCommandBuffer cmd, const PushConstants& pc);

		/// True si le pipeline est cree et utilisable.
		bool IsInitialized() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		VkPipeline       m_pipeline       = VK_NULL_HANDLE;
		VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	};
}
