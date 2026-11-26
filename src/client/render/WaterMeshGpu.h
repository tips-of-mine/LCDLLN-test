// src/client/render/WaterMeshGpu.h
#pragma once

#include "src/shared/world/water/WaterSurfaces.h"
#include "src/client/world/water/SeaGrid.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::render
{
	/// Vertex format pour les meshes d'eau M100.14, étendu EAU.6
	/// (36 B = pos3 + uv2 + flowDir2 + sea2).
	/// Distinct de `engine::world::water::WaterVertex` (M100.13, position seule, 12 B)
	/// qui est le format CPU-only produit par WaterMeshBuilder.
	/// Format GPU interne au client (aucun fichier) : pas de version à gérer.
	struct WaterVertex
	{
		float position[3];
		float uv[2];
		float flowDir[2];
		/// EAU.6 — sea[0] = profondeur d'eau sous le sommet (m, niveau − sol),
		/// sea[1] = fondu des vagues (1 = vagues entières, 0 = surface plate).
		/// Lacs, rivières et mer hors grille : { kSeaDeepWaterM, 0 } (jamais soulevés).
		float sea[2];
	};
	static_assert(sizeof(WaterVertex) == 36, "WaterVertex must be 36 bytes");

	/// Info de draw call par instance (lac OU riviere).
	/// `paramsIndex` est unifie :
	///   0..N_lakes-1                            = lacs (index dans `scene.lakes`)
	///   N_lakes..N_lakes+N_rivers-1             = rivieres (offset = paramsIndex - N_lakes dans `scene.rivers`)
	struct WaterInstanceDrawInfo
	{
		uint32_t firstIndex;   ///< Offset dans IBO global
		uint32_t indexCount;   ///< Nombre d'indices pour cette instance
		int32_t  vertexOffset; ///< Base vertex pour cette instance
		uint32_t paramsIndex;  ///< Voir doc struct
	};

	/// Helper CPU testable sans device : transforme une WaterScene en
	/// vertex + index arrays + drawInfos. Lacs en tete, rivieres ensuite.
	/// \param outVertices Concatene 9 floats par vertex (position3 + uv2 + flowDir2 + sea2).
	/// \param outIndices  Indices uint32_t globaux ; chaque instance lit
	///                    [firstIndex, firstIndex+indexCount) en y ajoutant vertexOffset.
	/// \param outDrawInfos Une entree par lake puis par river ; EAU.6 : une nappe
	///                    de mer recoupee par la grille donne jusqu'a 4 entrees
	///                    (meme paramsIndex), puis la grille vient en DERNIER
	///                    (paramsIndex = seaGrid->oceanLakeIndex).
	/// \param seaGrid     EAU.6 — grille de mer autour du joueur (nullptr ou vide :
	///                    rendu d'avant, nappes entieres). Les nappes de mer
	///                    rectangulaires sont privees du carre de la grille pour
	///                    qu'aucune zone ne soit dessinee deux fois.
	void BuildDrawInfos(const engine::world::water::WaterScene& scene,
		std::vector<float>& outVertices,
		std::vector<uint32_t>& outIndices,
		std::vector<WaterInstanceDrawInfo>& outDrawInfos,
		const engine::world::water::SeaGridCpu* seaGrid = nullptr);

	/// Buffer GPU contenant tous les meshes d'eau (lakes + rivers concatenes).
	/// Reconstruit a la demande depuis une WaterScene CPU via staging raw Vulkan.
	/// N'utilise pas VMA (compatible STAB.7 — VMA desactive).
	class WaterMeshGpu final
	{
	public:
		WaterMeshGpu() = default;
		WaterMeshGpu(const WaterMeshGpu&) = delete;
		WaterMeshGpu& operator=(const WaterMeshGpu&) = delete;

		/// Initialise l'objet. Pas d'allocation buffer ici — Rebuild s'en charge.
		/// \param physicalDevice Requis pour la recherche de type memoire (AllocateMemory).
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice);

		/// Reconstruit VBO/IBO depuis la scene. Realloue si capacite insuffisante,
		/// reutilise l'allocation existante sinon. Retourne false en cas d'erreur
		/// (logue ERROR, garde l'etat precedent intact).
		///
		/// \param transferPool  Command pool sur la queue transfer (ou graphics).
		/// \param transferQueue Queue de soumission (sera attendue via fence).
		/// \param seaGrid       EAU.6 — grille de mer (cf. BuildDrawInfos) ; nullptr = aucune.
		/// \return true si l'upload a reussi.
		///
		/// EAU.6 : la grille de mer est reconstruite quand le joueur se deplace
		/// (tous les ~64 m) ; la copie attend donc la fin des lectures des frames
		/// precedentes (barriere avant copie) et, si un buffer doit etre realloue,
		/// la queue est videe avant de detruire l'ancien (encore lu en vol sinon).
		bool Rebuild(VkCommandPool transferPool, VkQueue transferQueue,
		             const engine::world::water::WaterScene& scene,
		             const engine::world::water::SeaGridCpu* seaGrid = nullptr);

		/// Detruit toutes les ressources Vulkan. Safe sur etat non-init.
		void Destroy();

		VkBuffer GetVertexBuffer() const { return m_vbo; }
		VkBuffer GetIndexBuffer()  const { return m_ibo; }
		const std::vector<WaterInstanceDrawInfo>& GetDrawInfos() const { return m_drawInfos; }
		size_t GetInstanceCount() const { return m_drawInfos.size(); }
		bool IsValid() const { return m_vbo != VK_NULL_HANDLE && m_ibo != VK_NULL_HANDLE; }

		/// Retourne true apres un Init() reussi (m_device stocke).
		/// Distinct de IsValid() qui n'est vrai qu'apres le premier Rebuild reussi.
		bool IsInitialized() const { return m_device != VK_NULL_HANDLE; }

	private:
		/// Alloue ou realloue VBO/IBO si la nouvelle taille depasse la capacite actuelle.
		/// Reutilise les buffers existants si la capacite est suffisante.
		/// \return false si l'allocation Vulkan echoue (logue ERROR).
		bool EnsureCapacity(VkDeviceSize newVboSize, VkDeviceSize newIboSize);
		/// True si EnsureCapacity devra reallouer pour ces tailles.
		bool NeedsRealloc(VkDeviceSize newVboSize, VkDeviceSize newIboSize) const
		{
			return newVboSize > m_vboCapacity || newIboSize > m_iboCapacity;
		}

		VkDevice         m_device         = VK_NULL_HANDLE;
		VkPhysicalDevice m_physicalDevice  = VK_NULL_HANDLE;

		VkBuffer       m_vbo         = VK_NULL_HANDLE;
		VkDeviceMemory m_vboMemory   = VK_NULL_HANDLE;
		VkDeviceSize   m_vboCapacity = 0;

		VkBuffer       m_ibo         = VK_NULL_HANDLE;
		VkDeviceMemory m_iboMemory   = VK_NULL_HANDLE;
		VkDeviceSize   m_iboCapacity = 0;

		std::vector<WaterInstanceDrawInfo> m_drawInfos;
	};
}
