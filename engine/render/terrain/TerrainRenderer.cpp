#include "engine/render/terrain/TerrainRenderer.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace engine::render::terrain
{
    // ─────────────────────────────────────────────────────────────────────────────
    // Framebuffer key comparison / hash
    // ─────────────────────────────────────────────────────────────────────────────

    bool TerrainRenderer::FramebufferKey::operator==(const FramebufferKey& o) const
    {
        if (renderPass != o.renderPass || width != o.width || height != o.height)
            return false;
        for (int i = 0; i < 5; ++i)
            if (views[i] != o.views[i]) return false;
        return true;
    }

    size_t TerrainRenderer::FramebufferKeyHash::operator()(const FramebufferKey& k) const
    {
        size_t h = reinterpret_cast<size_t>(k.renderPass);
        for (int i = 0; i < 5; ++i)
            h ^= reinterpret_cast<size_t>(k.views[i]) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= static_cast<size_t>(k.width)  * 0x45d9f3bu;
        h ^= static_cast<size_t>(k.height) * 0x2c6fe96eu;
        return h;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Internal helpers
    // ─────────────────────────────────────────────────────────────────────────────
    namespace
    {
        uint32_t FindMemType(VkPhysicalDevice physDev,
                             uint32_t typeBits,
                             VkMemoryPropertyFlags desired)
        {
            VkPhysicalDeviceMemoryProperties props{};
            vkGetPhysicalDeviceMemoryProperties(physDev, &props);
            for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
            {
                if ((typeBits & (1u << i)) &&
                    (props.memoryTypes[i].propertyFlags & desired) == desired)
                    return i;
            }
            return UINT32_MAX;
        }
    } // namespace

    // ─────────────────────────────────────────────────────────────────────────────
    // TerrainRenderer::Init
    // ─────────────────────────────────────────────────────────────────────────────

    bool TerrainRenderer::Init(VkDevice device, VkPhysicalDevice physDev,
                               const engine::core::Config& config,
                               const std::string& heightmapRelPath,
                               const std::string& splatmapRelPath,
                               VkFormat fmtA, VkFormat fmtB, VkFormat fmtC,
                               VkFormat fmtVelocity, VkFormat fmtDepth,
                               VkQueue queue, uint32_t queueFamilyIndex,
                               ShaderLoaderFn loadSpirv)
    {
        LOG_INFO(Render, "[TerrainRenderer] Init begin (heightmap='{}' splatmap='{}')",
                 heightmapRelPath, splatmapRelPath);

        if (device == VK_NULL_HANDLE || physDev == VK_NULL_HANDLE || !loadSpirv)
        {
            LOG_ERROR(Render, "[TerrainRenderer] Init: invalid parameters");
            return false;
        }

        // ── Read terrain config ───────────────────────────────────────────────────
        m_terrainWorldSize = static_cast<float>(config.GetDouble("terrain.world_size", 1024.0));
        m_heightScale      = static_cast<float>(config.GetDouble("terrain.height_scale", 200.0));
        m_terrainOriginX   = static_cast<float>(config.GetDouble("terrain.origin_x", -512.0));
        m_terrainOriginZ   = static_cast<float>(config.GetDouble("terrain.origin_z", -512.0));

        if (m_terrainWorldSize <= 0.0f)
        {
            LOG_ERROR(Render, "[TerrainRenderer] terrain.world_size must be > 0 (got {})",
                      m_terrainWorldSize);
            return false;
        }

        // vertStepWorld = world units per local vertex step at LOD 0
        // For a 1025-pixel heightmap, there are 1024 inter-pixel gaps = kPatchQuads * patchCount gaps.
        m_vertStepWorld = m_terrainWorldSize / static_cast<float>(1024u); // 1024 = 64×16

        // ── Load heightmap from file ──────────────────────────────────────────────
        {
            const std::string contentPath = config.GetString("paths.content", "game/data");
            const std::string fullPath    = contentPath + "/" + heightmapRelPath;

            if (!HeightmapLoader::LoadFromFile(fullPath, m_heightmapData))
            {
                LOG_WARN(Render,
                    "[TerrainRenderer] Heightmap '{}' not found — terrain will be flat placeholder",
                    heightmapRelPath);
                // Synthesise a 1025×1025 flat (zero) heightmap as fallback
                m_heightmapData.width  = 1025u;
                m_heightmapData.height = 1025u;
                m_heightmapData.heights.assign(1025u * 1025u, 0u);
            }
        }

        // ── Upload heightmap to GPU ───────────────────────────────────────────────
        if (!HeightmapLoader::UploadHeightmap(device, physDev, m_heightmapData,
                                              queue, queueFamilyIndex, m_heightmapGpu))
        {
            LOG_ERROR(Render, "[TerrainRenderer] Failed to upload heightmap to GPU");
            Destroy(device);
            return false;
        }

        // ── Generate & upload normal map ──────────────────────────────────────────
        if (!HeightmapLoader::GenerateAndUploadNormalMap(device, physDev, m_heightmapData,
                                                         m_heightScale, m_vertStepWorld,
                                                         queue, queueFamilyIndex, m_normalMapGpu))
        {
            LOG_ERROR(Render, "[TerrainRenderer] Failed to generate/upload normal map");
            Destroy(device);
            return false;
        }

        // ── Generate patch mesh (shared vertex buffer + per-LOD index buffers) ────
        if (!TerrainMesh::Generate(device, physDev, m_meshGpu))
        {
            LOG_ERROR(Render, "[TerrainRenderer] Failed to generate terrain mesh");
            Destroy(device);
            return false;
        }

        // ── Initialise texture splatting (M34.2) ─────────────────────────────────
        if (!m_splatting.Init(device, physDev, config, splatmapRelPath, queue, queueFamilyIndex))
        {
            LOG_WARN(Render,
                "[TerrainRenderer] TerrainSplatting Init failed — terrain disabled");
            Destroy(device);
            return false;
        }

        // ── Build patch list ──────────────────────────────────────────────────────
        // Number of patches: (heightmap_pixels - 1) / kPatchQuads per axis
        m_patchCountX = (m_heightmapData.width  - 1u) / kPatchQuads;
        m_patchCountZ = (m_heightmapData.height - 1u) / kPatchQuads;
        const uint32_t totalPatches = m_patchCountX * m_patchCountZ;
        m_patches.resize(totalPatches);

        const float patchWorldSize = static_cast<float>(kPatchQuads) * m_vertStepWorld;

        for (uint32_t pz = 0; pz < m_patchCountZ; ++pz)
        {
            for (uint32_t px = 0; px < m_patchCountX; ++px)
            {
                TerrainPatchInfo& p = m_patches[pz * m_patchCountX + px];
                p.originX = m_terrainOriginX + static_cast<float>(px) * patchWorldSize;
                p.originZ = m_terrainOriginZ + static_cast<float>(pz) * patchWorldSize;
                p.centerX = p.originX + patchWorldSize * 0.5f;
                p.centerZ = p.originZ + patchWorldSize * 0.5f;

                // Compute Y bounds by sampling the 4 patch corners
                const uint32_t hx0 = px * kPatchQuads;
                const uint32_t hz0 = pz * kPatchQuads;
                const uint32_t hx1 = hx0 + kPatchQuads;
                const uint32_t hz1 = hz0 + kPatchQuads;

                const float h00 = m_heightmapData.Sample(hx0, hz0) * m_heightScale;
                const float h10 = m_heightmapData.Sample(hx1, hz0) * m_heightScale;
                const float h01 = m_heightmapData.Sample(hx0, hz1) * m_heightScale;
                const float h11 = m_heightmapData.Sample(hx1, hz1) * m_heightScale;

                p.minY = std::min({ h00, h10, h01, h11 });
                p.maxY = std::max({ h00, h10, h01, h11 });
                // Add a margin for interpolated heights between corners
                const float margin = m_heightScale * 0.05f;
                p.minY -= margin;
                p.maxY += margin;
            }
        }
        LOG_DEBUG(Render, "[TerrainRenderer] Patch list built: {}×{} = {} patches",
                  m_patchCountX, m_patchCountZ, totalPatches);

        // ── Create render pass ────────────────────────────────────────────────────
        {
            // 5 attachments: A, B, C, Velocity (color), Depth
            VkAttachmentDescription attachments[5]{};

            // Color attachments 0-3
            const VkFormat colorFmts[4] = { fmtA, fmtB, fmtC, fmtVelocity };
            for (int i = 0; i < 4; ++i)
            {
                attachments[i].format         = colorFmts[i];
                attachments[i].samples        = VK_SAMPLE_COUNT_1_BIT;
                attachments[i].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
                attachments[i].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
                attachments[i].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                attachments[i].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
                attachments[i].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
            // Depth attachment
            attachments[4].format         = fmtDepth;
            attachments[4].samples        = VK_SAMPLE_COUNT_1_BIT;
            attachments[4].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[4].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[4].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[4].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[4].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[4].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkAttachmentReference colorRefs[4]{};
            for (int i = 0; i < 4; ++i)
            {
                colorRefs[i].attachment = static_cast<uint32_t>(i);
                colorRefs[i].layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
            VkAttachmentReference depthRef{};
            depthRef.attachment = 4;
            depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount    = 4;
            subpass.pColorAttachments       = colorRefs;
            subpass.pDepthStencilAttachment = &depthRef;

            VkSubpassDependency dep{};
            dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
            dep.dstSubpass    = 0;
            dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dep.srcAccessMask = 0;
            dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            VkRenderPassCreateInfo rpCI{};
            rpCI.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            rpCI.attachmentCount = 5;
            rpCI.pAttachments    = attachments;
            rpCI.subpassCount    = 1;
            rpCI.pSubpasses      = &subpass;
            rpCI.dependencyCount = 1;
            rpCI.pDependencies   = &dep;

            if (vkCreateRenderPass(device, &rpCI, nullptr, &m_renderPass) != VK_SUCCESS ||
                m_renderPass == VK_NULL_HANDLE)
            {
                LOG_ERROR(Render, "[TerrainRenderer] vkCreateRenderPass failed");
                Destroy(device);
                return false;
            }
        }

        // ── Descriptor set layout ─────────────────────────────────────────────────
        {
            VkDescriptorSetLayoutBinding bindings[7]{};
            // binding 0: heightmap sampler (vertex + fragment)
            bindings[0].binding            = 0;
            bindings[0].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[0].descriptorCount    = 1;
            bindings[0].stageFlags         = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            // binding 1: normal map sampler (fragment)
            bindings[1].binding            = 1;
            bindings[1].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[1].descriptorCount    = 1;
            bindings[1].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
            // binding 2: per-frame UBO (vertex + fragment)
            bindings[2].binding            = 2;
            bindings[2].descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            bindings[2].descriptorCount    = 1;
            bindings[2].stageFlags         = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            // binding 3: splat map sampler (fragment) — R=grass,G=dirt,B=rock,A=snow
            bindings[3].binding            = 3;
            bindings[3].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[3].descriptorCount    = 1;
            bindings[3].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
            // binding 4: albedo texture array (fragment) — 4 layers
            bindings[4].binding            = 4;
            bindings[4].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[4].descriptorCount    = 1;
            bindings[4].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
            // binding 5: normal texture array (fragment) — 4 layers
            bindings[5].binding            = 5;
            bindings[5].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[5].descriptorCount    = 1;
            bindings[5].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
            // binding 6: ORM texture array (fragment) — 4 layers (R=AO, G=Roughness, B=Metallic)
            bindings[6].binding            = 6;
            bindings[6].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[6].descriptorCount    = 1;
            bindings[6].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutCreateInfo dslCI{};
            dslCI.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            dslCI.bindingCount = 7;
            dslCI.pBindings    = bindings;

            if (vkCreateDescriptorSetLayout(device, &dslCI, nullptr, &m_descSetLayout) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainRenderer] vkCreateDescriptorSetLayout failed");
                Destroy(device);
                return false;
            }
        }

        // ── Pipeline layout ───────────────────────────────────────────────────────
        {
            VkPushConstantRange pcRange{};
            pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            pcRange.offset     = 0;
            pcRange.size       = sizeof(PushConstants); // 16 bytes

            VkPipelineLayoutCreateInfo plCI{};
            plCI.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            plCI.setLayoutCount         = 1;
            plCI.pSetLayouts            = &m_descSetLayout;
            plCI.pushConstantRangeCount = 1;
            plCI.pPushConstantRanges    = &pcRange;

            if (vkCreatePipelineLayout(device, &plCI, nullptr, &m_pipelineLayout) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainRenderer] vkCreatePipelineLayout failed");
                Destroy(device);
                return false;
            }
        }

        // ── Load shaders ──────────────────────────────────────────────────────────
        std::vector<uint32_t> vertSpirv = loadSpirv("shaders/terrain.vert.spv");
        std::vector<uint32_t> fragSpirv = loadSpirv("shaders/terrain.frag.spv");

        if (vertSpirv.empty() || fragSpirv.empty())
        {
            LOG_WARN(Render,
                "[TerrainRenderer] terrain shaders not found (vert={} frag={}) — skipping Init",
                vertSpirv.empty() ? "MISSING" : "OK",
                fragSpirv.empty() ? "MISSING" : "OK");
            // Graceful skip: terrain disabled if shaders are absent
            Destroy(device);
            return false;
        }

        VkShaderModule vertMod = VK_NULL_HANDLE, fragMod = VK_NULL_HANDLE;
        {
            VkShaderModuleCreateInfo smCI{};
            smCI.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            smCI.codeSize = vertSpirv.size() * sizeof(uint32_t);
            smCI.pCode    = vertSpirv.data();
            if (vkCreateShaderModule(device, &smCI, nullptr, &vertMod) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainRenderer] vkCreateShaderModule (vert) failed");
                Destroy(device);
                return false;
            }
            smCI.codeSize = fragSpirv.size() * sizeof(uint32_t);
            smCI.pCode    = fragSpirv.data();
            if (vkCreateShaderModule(device, &smCI, nullptr, &fragMod) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainRenderer] vkCreateShaderModule (frag) failed");
                vkDestroyShaderModule(device, vertMod, nullptr);
                Destroy(device);
                return false;
            }
        }

        // ── Create pipeline ───────────────────────────────────────────────────────
        {
            VkPipelineShaderStageCreateInfo stages[2]{};
            stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vertMod;
            stages[0].pName  = "main";
            stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fragMod;
            stages[1].pName  = "main";

            // Vertex input: binding 0, stride=sizeof(TerrainVertex), per-vertex
            VkVertexInputBindingDescription vertBinding{};
            vertBinding.binding   = 0;
            vertBinding.stride    = sizeof(TerrainVertex); // 8 bytes
            vertBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            VkVertexInputAttributeDescription vertAttr{};
            vertAttr.location = 0;
            vertAttr.binding  = 0;
            vertAttr.format   = VK_FORMAT_R32G32_SFLOAT;
            vertAttr.offset   = 0;

            VkPipelineVertexInputStateCreateInfo visCI{};
            visCI.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            visCI.vertexBindingDescriptionCount   = 1;
            visCI.pVertexBindingDescriptions      = &vertBinding;
            visCI.vertexAttributeDescriptionCount = 1;
            visCI.pVertexAttributeDescriptions    = &vertAttr;

            VkPipelineInputAssemblyStateCreateInfo iasCI{};
            iasCI.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            iasCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            VkPipelineViewportStateCreateInfo vpCI{};
            vpCI.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            vpCI.viewportCount = 1;
            vpCI.scissorCount  = 1;

            VkPipelineRasterizationStateCreateInfo rasCI{};
            rasCI.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasCI.polygonMode = VK_POLYGON_MODE_FILL;
            rasCI.cullMode    = VK_CULL_MODE_BACK_BIT;
            rasCI.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasCI.lineWidth   = 1.0f;

            VkPipelineMultisampleStateCreateInfo msCI{};
            msCI.sType               = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            msCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo dsCI{};
            dsCI.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            dsCI.depthTestEnable  = VK_TRUE;
            dsCI.depthWriteEnable = VK_TRUE;
            dsCI.depthCompareOp   = VK_COMPARE_OP_LESS;

            // 4 color attachments (A, B, C, Velocity) — no blending
            VkPipelineColorBlendAttachmentState blendAtts[4]{};
            for (int i = 0; i < 4; ++i)
            {
                blendAtts[i].colorWriteMask =
                    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                blendAtts[i].blendEnable = VK_FALSE;
            }
            VkPipelineColorBlendStateCreateInfo cbCI{};
            cbCI.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            cbCI.attachmentCount = 4;
            cbCI.pAttachments    = blendAtts;

            VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
            VkPipelineDynamicStateCreateInfo dynCI{};
            dynCI.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynCI.dynamicStateCount = 2;
            dynCI.pDynamicStates    = dynStates;

            VkGraphicsPipelineCreateInfo gpCI{};
            gpCI.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            gpCI.stageCount          = 2;
            gpCI.pStages             = stages;
            gpCI.pVertexInputState   = &visCI;
            gpCI.pInputAssemblyState = &iasCI;
            gpCI.pViewportState      = &vpCI;
            gpCI.pRasterizationState = &rasCI;
            gpCI.pMultisampleState   = &msCI;
            gpCI.pDepthStencilState  = &dsCI;
            gpCI.pColorBlendState    = &cbCI;
            gpCI.pDynamicState       = &dynCI;
            gpCI.layout              = m_pipelineLayout;
            gpCI.renderPass          = m_renderPass;
            gpCI.subpass             = 0;

            const VkResult pipeRes =
                vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpCI, nullptr, &m_pipeline);

            vkDestroyShaderModule(device, vertMod, nullptr);
            vkDestroyShaderModule(device, fragMod, nullptr);

            if (pipeRes != VK_SUCCESS || m_pipeline == VK_NULL_HANDLE)
            {
                LOG_ERROR(Render, "[TerrainRenderer] vkCreateGraphicsPipelines failed");
                Destroy(device);
                return false;
            }
        }

        // ── Create descriptor pool ────────────────────────────────────────────────
        {
            VkDescriptorPoolSize poolSizes[2]{};
            poolSizes[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            poolSizes[0].descriptorCount = 6; // heightmap + normalmap + splatmap + albedoArr + normalArr + ormArr
            poolSizes[1].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            poolSizes[1].descriptorCount = 1;

            VkDescriptorPoolCreateInfo dpCI{};
            dpCI.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            dpCI.maxSets       = 1;
            dpCI.poolSizeCount = 2;
            dpCI.pPoolSizes    = poolSizes;

            if (vkCreateDescriptorPool(device, &dpCI, nullptr, &m_descPool) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainRenderer] vkCreateDescriptorPool failed");
                Destroy(device);
                return false;
            }

            VkDescriptorSetAllocateInfo dsAI{};
            dsAI.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            dsAI.descriptorPool     = m_descPool;
            dsAI.descriptorSetCount = 1;
            dsAI.pSetLayouts        = &m_descSetLayout;

            if (vkAllocateDescriptorSets(device, &dsAI, &m_descSet) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainRenderer] vkAllocateDescriptorSets failed");
                Destroy(device);
                return false;
            }
        }

        // ── Create per-frame UBO ──────────────────────────────────────────────────
        {
            const VkDeviceSize uboSize = sizeof(FrameUbo);
            VkBufferCreateInfo bi{};
            bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bi.size        = uboSize;
            bi.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateBuffer(device, &bi, nullptr, &m_uboBuffer) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainRenderer] vkCreateBuffer (UBO) failed");
                Destroy(device);
                return false;
            }

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device, m_uboBuffer, &req);

            const uint32_t memType = FindMemType(physDev, req.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (memType == UINT32_MAX)
            {
                LOG_ERROR(Render, "[TerrainRenderer] No HOST_VISIBLE memory for UBO");
                Destroy(device);
                return false;
            }

            VkMemoryAllocateInfo ai{};
            ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize  = req.size;
            ai.memoryTypeIndex = memType;

            if (vkAllocateMemory(device, &ai, nullptr, &m_uboMemory) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainRenderer] vkAllocateMemory (UBO) failed");
                Destroy(device);
                return false;
            }

            if (vkBindBufferMemory(device, m_uboBuffer, m_uboMemory, 0) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainRenderer] vkBindBufferMemory (UBO) failed");
                Destroy(device);
                return false;
            }
        }

        // ── Write descriptor set ──────────────────────────────────────────────────
        {
            VkDescriptorImageInfo heightmapInfo{};
            heightmapInfo.sampler     = m_heightmapGpu.sampler;
            heightmapInfo.imageView   = m_heightmapGpu.view;
            heightmapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo normalmapInfo{};
            normalmapInfo.sampler     = m_normalMapGpu.sampler;
            normalmapInfo.imageView   = m_normalMapGpu.view;
            normalmapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorBufferInfo uboInfo{};
            uboInfo.buffer = m_uboBuffer;
            uboInfo.offset = 0;
            uboInfo.range  = sizeof(FrameUbo);

            VkDescriptorImageInfo splatmapInfo{};
            splatmapInfo.sampler     = m_splatting.GetSplatMap().sampler;
            splatmapInfo.imageView   = m_splatting.GetSplatMap().view;
            splatmapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo albedoArrayInfo{};
            albedoArrayInfo.sampler     = m_splatting.GetAlbedoArray().sampler;
            albedoArrayInfo.imageView   = m_splatting.GetAlbedoArray().view;
            albedoArrayInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo normalArrayInfo{};
            normalArrayInfo.sampler     = m_splatting.GetNormalArray().sampler;
            normalArrayInfo.imageView   = m_splatting.GetNormalArray().view;
            normalArrayInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo ormArrayInfo{};
            ormArrayInfo.sampler     = m_splatting.GetORMArray().sampler;
            ormArrayInfo.imageView   = m_splatting.GetORMArray().view;
            ormArrayInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet writes[7]{};
            writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet          = m_descSet;
            writes[0].dstBinding      = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].pImageInfo      = &heightmapInfo;

            writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet          = m_descSet;
            writes[1].dstBinding      = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].pImageInfo      = &normalmapInfo;

            writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet          = m_descSet;
            writes[2].dstBinding      = 2;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[2].pBufferInfo     = &uboInfo;

            writes[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[3].dstSet          = m_descSet;
            writes[3].dstBinding      = 3;
            writes[3].descriptorCount = 1;
            writes[3].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[3].pImageInfo      = &splatmapInfo;

            writes[4].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[4].dstSet          = m_descSet;
            writes[4].dstBinding      = 4;
            writes[4].descriptorCount = 1;
            writes[4].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[4].pImageInfo      = &albedoArrayInfo;

            writes[5].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[5].dstSet          = m_descSet;
            writes[5].dstBinding      = 5;
            writes[5].descriptorCount = 1;
            writes[5].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[5].pImageInfo      = &normalArrayInfo;

            writes[6].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[6].dstSet          = m_descSet;
            writes[6].dstBinding      = 6;
            writes[6].descriptorCount = 1;
            writes[6].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[6].pImageInfo      = &ormArrayInfo;

            vkUpdateDescriptorSets(device, 7, writes, 0, nullptr);
        }

        LOG_INFO(Render, "[TerrainRenderer] Init OK ({}×{} patches, worldSize={} heightScale={})",
                 m_patchCountX, m_patchCountZ, m_terrainWorldSize, m_heightScale);
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // TerrainRenderer::Destroy
    // ─────────────────────────────────────────────────────────────────────────────

    void TerrainRenderer::Destroy(VkDevice device)
    {
        InvalidateFramebufferCache(device);

        if (m_pipeline       != VK_NULL_HANDLE) { vkDestroyPipeline(device, m_pipeline, nullptr);             m_pipeline       = VK_NULL_HANDLE; }
        if (m_pipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }
        if (m_descPool       != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device, m_descPool, nullptr);       m_descPool       = VK_NULL_HANDLE; m_descSet = VK_NULL_HANDLE; }
        if (m_descSetLayout  != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device, m_descSetLayout, nullptr); m_descSetLayout = VK_NULL_HANDLE; }
        if (m_renderPass     != VK_NULL_HANDLE) { vkDestroyRenderPass(device, m_renderPass, nullptr);         m_renderPass     = VK_NULL_HANDLE; }

        if (m_uboBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(device, m_uboBuffer, nullptr); m_uboBuffer = VK_NULL_HANDLE; }
        if (m_uboMemory != VK_NULL_HANDLE) { vkFreeMemory(device, m_uboMemory, nullptr);    m_uboMemory = VK_NULL_HANDLE; }

        HeightmapLoader::DestroyHeightmap(device, m_heightmapGpu);
        HeightmapLoader::DestroyNormalMap(device, m_normalMapGpu);
        TerrainMesh::Destroy(device, m_meshGpu);
        m_splatting.Destroy(device);

        m_patches.clear();
        m_heightmapData.heights.clear();

        LOG_INFO(Render, "[TerrainRenderer] Destroyed");
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // TerrainRenderer::InvalidateFramebufferCache
    // ─────────────────────────────────────────────────────────────────────────────

    void TerrainRenderer::InvalidateFramebufferCache(VkDevice device)
    {
        for (auto& [key, fb] : m_fbCache)
            if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device, fb, nullptr);
        m_fbCache.clear();
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // TerrainRenderer::Record
    // ─────────────────────────────────────────────────────────────────────────────

    void TerrainRenderer::Record(VkDevice device, VkCommandBuffer cmd,
                                 engine::render::Registry& registry,
                                 VkExtent2D extent,
                                 engine::render::ResourceId idA,
                                 engine::render::ResourceId idB,
                                 engine::render::ResourceId idC,
                                 engine::render::ResourceId idVelocity,
                                 engine::render::ResourceId idDepth,
                                 const float* prevViewProjMat4,
                                 const float* viewProjMat4,
                                 const engine::math::Vec3& cameraPos,
                                 const engine::math::Frustum& frustum)
    {
        if (m_pipeline == VK_NULL_HANDLE) return;
        if (extent.width == 0 || extent.height == 0) return;

        // ── Update per-frame UBO ──────────────────────────────────────────────────
        {
            FrameUbo ubo{};
            std::memcpy(ubo.viewProj,     viewProjMat4,     sizeof(ubo.viewProj));
            std::memcpy(ubo.prevViewProj, prevViewProjMat4, sizeof(ubo.prevViewProj));
            ubo.cameraPos[0] = cameraPos.x;
            ubo.cameraPos[1] = cameraPos.y;
            ubo.cameraPos[2] = cameraPos.z;
            ubo.cameraPos[3] = 0.0f;
            ubo.terrainParams[0] = m_terrainWorldSize;
            ubo.terrainParams[1] = m_heightScale;
            ubo.terrainParams[2] = m_vertStepWorld;
            ubo.terrainParams[3] = 0.0f;
            ubo.terrainOrigin[0] = m_terrainOriginX;
            ubo.terrainOrigin[1] = m_terrainOriginZ;
            ubo.terrainOrigin[2] = 0.0f;
            ubo.terrainOrigin[3] = 0.0f;
            ubo.layerTiling[0]   = m_splatting.GetLayerTiling(0); // grass
            ubo.layerTiling[1]   = m_splatting.GetLayerTiling(1); // dirt
            ubo.layerTiling[2]   = m_splatting.GetLayerTiling(2); // rock
            ubo.layerTiling[3]   = m_splatting.GetLayerTiling(3); // snow

            void* mapped = nullptr;
            if (vkMapMemory(device, m_uboMemory, 0, sizeof(FrameUbo), 0, &mapped) == VK_SUCCESS)
            {
                std::memcpy(mapped, &ubo, sizeof(FrameUbo));
                vkUnmapMemory(device, m_uboMemory);
            }
        }

        // ── CPU LOD selection & frustum culling ───────────────────────────────────
        // Collect per-LOD draw lists: [(patchIndex, morphFactor)]
        struct DrawEntry { uint32_t patchIdx; float morphFactor; };
        std::vector<DrawEntry> drawLists[kTerrainLodCount];

        const float patchWorldSize = static_cast<float>(kPatchQuads) * m_vertStepWorld;

        for (uint32_t i = 0; i < static_cast<uint32_t>(m_patches.size()); ++i)
        {
            const TerrainPatchInfo& p = m_patches[i];

            // AABB in world space
            const engine::math::Vec3 bMin{ p.originX,                  p.minY, p.originZ };
            const engine::math::Vec3 bMax{ p.originX + patchWorldSize,  p.maxY, p.originZ + patchWorldSize };

            if (!frustum.TestAABB(bMin, bMax)) continue;

            // Distance from camera to patch centre (XZ only)
            const float dx   = p.centerX - cameraPos.x;
            const float dz   = p.centerZ - cameraPos.z;
            const float dist = std::sqrt(dx * dx + dz * dz);

            // Select LOD
            uint32_t lod = 0;
            for (; lod < kTerrainLodCount - 1u; ++lod)
                if (dist < kLodDistances[lod]) break;

            // Morph factor: blend towards next (coarser) LOD in the transition zone.
            // Transition zone = last 20% of the current LOD range.
            float morphFactor = 0.0f;
            if (lod < kTerrainLodCount - 1u)
            {
                const float rangeLow  = (lod > 0) ? kLodDistances[lod - 1] : 0.0f;
                const float rangeHigh = kLodDistances[lod];
                const float morphStart = rangeLow + (rangeHigh - rangeLow) * 0.8f;
                const float morphRange = rangeHigh - morphStart;
                if (morphRange > 0.0f)
                    morphFactor = std::max(0.0f, std::min(1.0f, (dist - morphStart) / morphRange));
            }

            drawLists[lod].push_back({ i, morphFactor });
        }

        // ── Create / retrieve framebuffer ─────────────────────────────────────────
        VkImageView views[5] = {
            registry.getImageView(idA),
            registry.getImageView(idB),
            registry.getImageView(idC),
            registry.getImageView(idVelocity),
            registry.getImageView(idDepth)
        };

        FramebufferKey fbKey{};
        fbKey.renderPass = m_renderPass;
        for (int i = 0; i < 5; ++i) fbKey.views[i] = views[i];
        fbKey.width  = extent.width;
        fbKey.height = extent.height;

        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        auto it = m_fbCache.find(fbKey);
        if (it != m_fbCache.end())
        {
            framebuffer = it->second;
        }
        else
        {
            VkFramebufferCreateInfo fbCI{};
            fbCI.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbCI.renderPass      = m_renderPass;
            fbCI.attachmentCount = 5;
            fbCI.pAttachments    = views;
            fbCI.width           = extent.width;
            fbCI.height          = extent.height;
            fbCI.layers          = 1;

            if (vkCreateFramebuffer(device, &fbCI, nullptr, &framebuffer) != VK_SUCCESS ||
                framebuffer == VK_NULL_HANDLE)
            {
                LOG_ERROR(Render, "[TerrainRenderer] vkCreateFramebuffer failed");
                return;
            }
            m_fbCache[fbKey] = framebuffer;
        }

        // ── Begin render pass ─────────────────────────────────────────────────────
        VkClearValue clearValues[5]{};
        clearValues[0].color        = {{ 0.0f, 0.0f, 0.0f, 0.0f }};
        clearValues[1].color        = {{ 0.5f, 0.5f, 1.0f, 0.0f }};
        clearValues[2].color        = {{ 1.0f, 0.8f, 0.0f, 0.0f }};
        clearValues[3].color        = {{ 0.0f, 0.0f, 0.0f, 0.0f }};
        clearValues[4].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rpBI{};
        rpBI.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBI.renderPass        = m_renderPass;
        rpBI.framebuffer       = framebuffer;
        rpBI.renderArea.offset = { 0, 0 };
        rpBI.renderArea.extent = extent;
        rpBI.clearValueCount   = 5;
        rpBI.pClearValues      = clearValues;

        vkCmdBeginRenderPass(cmd, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

        // ── Set dynamic viewport / scissor ────────────────────────────────────────
        VkViewport vp{};
        vp.x        = 0.0f;
        vp.y        = 0.0f;
        vp.width    = static_cast<float>(extent.width);
        vp.height   = static_cast<float>(extent.height);
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = extent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // ── Bind pipeline & descriptor set ───────────────────────────────────────
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_pipelineLayout, 0, 1, &m_descSet, 0, nullptr);

        // ── Bind shared vertex buffer ─────────────────────────────────────────────
        VkDeviceSize vbOffset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m_meshGpu.vertexBuffer, &vbOffset);

        // ── Draw patches, grouped by LOD to minimise index buffer rebinds ─────────
        for (uint32_t lod = 0; lod < kTerrainLodCount; ++lod)
        {
            if (drawLists[lod].empty()) continue;

            // Bind index buffer for this LOD
            vkCmdBindIndexBuffer(cmd, m_meshGpu.lod[lod].buffer, 0, VK_INDEX_TYPE_UINT16);

            const uint32_t indexCount = m_meshGpu.lod[lod].indexCount;

            for (const DrawEntry& entry : drawLists[lod])
            {
                const TerrainPatchInfo& p = m_patches[entry.patchIdx];

                PushConstants pc{};
                pc.patchOriginX = p.originX;
                pc.patchOriginZ = p.originZ;
                pc.morphFactor  = entry.morphFactor;
                pc.lodLevel     = static_cast<int32_t>(lod);

                vkCmdPushConstants(cmd, m_pipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(PushConstants), &pc);

                vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
            }
        }

        vkCmdEndRenderPass(cmd);
    }

} // namespace engine::render::terrain
