#pragma once

#include "engine/render/FrameGraph.h"
#include "engine/render/WaterMeshGpu.h"
#include "engine/world/water/WaterSurfaces.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace engine::render
{
	/// Push constants par-instance pour la passe water (128 B exacts).
	/// Layout aligné std140 (les vec3 prennent 16 B avec padding).
	/// Offsets vérifiés par tests offsetof — toute modification doit aussi
	/// mettre à jour le layout GLSL `push_constant` correspondant dans
	/// engine/render/shaders/water.vert + water.frag.
	struct WaterPassPushConstants
	{
		float viewProj[16];        // offset   0, size 64
		float cameraPos[3];        // offset  64, size 12
		float timeSeconds;         // offset  76, size  4
		float bottomColor[3];      // offset  80, size 12
		float turbidity;           // offset  92, size  4
		float flowDirection[2];    // offset  96, size  8
		float flowSpeed;           // offset 104, size  4
		float refractionAmount;    // offset 108, size  4
		float fresnelPower;        // offset 112, size  4
		float reflectionStrength;  // offset 116, size  4
		float screenSize[2];       // offset 120, size  8
	};
	static_assert(sizeof(WaterPassPushConstants) == 128, "WaterPassPushConstants must be exactly 128 bytes");

	// API complète Init/Record/Destroy ajoutée en Tasks 5-6.
	class WaterPass final
	{
	public:
		WaterPass() = default;
		WaterPass(const WaterPass&) = delete;
		WaterPass& operator=(const WaterPass&) = delete;

		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		VkPipeline m_pipeline = VK_NULL_HANDLE;
		// Champs Vulkan complets ajoutés en Tasks 5-6.
	};
}
