#include "src/client/render/skinned/SkinnedRenderer.h"

#include "src/shared/core/Log.h"

#include <algorithm>
#include <cstring>

namespace engine::render::skinned
{

namespace
{
    /// Taille du push constant (identique à GeometryPass) :
    ///   prevViewProj (64) + viewProj (64) + materialIndex + 3 padding uint = 144.
    /// Doit rester identique à `engine::render::GeometryPass::kPushConstantSize`
    /// car on réutilise gbuffer_geometry.frag.spv sans modification.
    constexpr uint32_t kPushConstantSize = 144u;

    /// Cherche un memory type compatible avec `typeBits` (bitmask renvoyé par
    /// `vkGetBufferMemoryRequirements`) qui contient au moins toutes les
    /// propriétés `wanted` (typiquement HOST_VISIBLE | HOST_COHERENT).
    ///
    /// \return Index dans `VkPhysicalDeviceMemoryProperties::memoryTypes`,
    ///         ou UINT32_MAX si aucun type n'est compatible (le caller doit
    ///         traiter ça comme un échec d'allocation).
    ///
    /// Duplication intentionnelle du helper homonyme dans SkinnedMesh.cpp :
    /// les helpers static cross-`.cpp` sont fragiles à lier ici (anonymous
    /// namespace), donc on duplique 6 lignes plutôt que d'exposer un helper
    /// dans le header.
    uint32_t FindMemoryType(VkPhysicalDevice phys, uint32_t typeBits, VkMemoryPropertyFlags wanted)
    {
        VkPhysicalDeviceMemoryProperties mp{};
        vkGetPhysicalDeviceMemoryProperties(phys, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
            if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & wanted) == wanted)
                return i;
        }
        return UINT32_MAX;
    }

    /// Crée un VkBuffer + VkDeviceMemory host-visible/coherent, sans écrire
    /// de données initiales. Utilisé pour le bone SSBO et le model instance
    /// buffer, dont le contenu est rewritten chaque frame par Record().
    ///
    /// \param usage  Flag d'usage du buffer (STORAGE_BUFFER_BIT pour le SSBO,
    ///               VERTEX_BUFFER_BIT pour l'instance buffer).
    /// \param bytes  Taille en octets (typiquement maxBones * 64 pour le SSBO,
    ///               64 pour la model instance).
    /// \param outBuf Pointeur vers le handle de sortie (initialisé à
    ///               VK_NULL_HANDLE en cas d'échec).
    /// \param outMem Pointeur vers la memory de sortie (idem).
    ///
    /// \return true si vkCreateBuffer + vkAllocateMemory + vkBindBufferMemory
    ///         ont réussi et qu'un memory type compatible a été trouvé.
    ///         false sinon (et tout est nettoyé / handles VK_NULL_HANDLE).
    ///
    /// Différence vs `CreateHostVisibleBuffer` dans SkinnedMesh.cpp : ce
    /// helper ne fait PAS de map+memcpy initial, car on n'a pas de données
    /// à écrire au moment de l'Init du renderer.
    bool CreateEmptyHostVisibleBuffer(VkDevice device, VkPhysicalDevice phys,
                                      VkBufferUsageFlags usage, VkDeviceSize bytes,
                                      VkBuffer* outBuf, VkDeviceMemory* outMem)
    {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = bytes;
        bi.usage = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bi, nullptr, outBuf) != VK_SUCCESS) {
            *outBuf = VK_NULL_HANDLE;
            *outMem = VK_NULL_HANDLE;
            return false;
        }

        VkMemoryRequirements mr{};
        vkGetBufferMemoryRequirements(device, *outBuf, &mr);

        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = FindMemoryType(phys, mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (ai.memoryTypeIndex == UINT32_MAX) {
            vkDestroyBuffer(device, *outBuf, nullptr);
            *outBuf = VK_NULL_HANDLE;
            *outMem = VK_NULL_HANDLE;
            return false;
        }

        if (vkAllocateMemory(device, &ai, nullptr, outMem) != VK_SUCCESS) {
            vkDestroyBuffer(device, *outBuf, nullptr);
            *outBuf = VK_NULL_HANDLE;
            *outMem = VK_NULL_HANDLE;
            return false;
        }
        if (vkBindBufferMemory(device, *outBuf, *outMem, 0) != VK_SUCCESS) {
            vkFreeMemory(device, *outMem, nullptr);
            vkDestroyBuffer(device, *outBuf, nullptr);
            *outBuf = VK_NULL_HANDLE;
            *outMem = VK_NULL_HANDLE;
            return false;
        }
        return true;
    }
}  // namespace

bool SkinnedRenderer::Init(VkDevice device,
                           VkPhysicalDevice physicalDevice,
                           VkFormat formatA, VkFormat formatB, VkFormat formatC,
                           VkFormat formatVelocity, VkFormat depthFormat,
                           const uint32_t* vertSpirv, size_t vertWordCount,
                           const uint32_t* fragSpirv, size_t fragWordCount,
                           VkDescriptorSetLayout materialLayout,
                           uint32_t maxBonesPerSkeleton)
{
    if (!device || !physicalDevice || !vertSpirv || vertWordCount == 0
        || !fragSpirv || fragWordCount == 0 || materialLayout == VK_NULL_HANDLE
        || maxBonesPerSkeleton == 0)
    {
        LOG_ERROR(Render, "[SkinnedRenderer] Init: invalid arguments (device={} phys={} "
                      "vertSpirv={} vertWords={} fragSpirv={} fragWords={} "
                      "materialLayout={} maxBones={})",
                      (void*)device, (void*)physicalDevice,
                      (void*)vertSpirv, vertWordCount,
                      (void*)fragSpirv, fragWordCount,
                      (void*)materialLayout, maxBonesPerSkeleton);
        return false;
    }

    m_device = device;
    m_physicalDevice = physicalDevice;
    m_maxBones = maxBonesPerSkeleton;

    // -------------------------------------------------------------------------
    // Render pass : 4 color attachments (GBuffer A/B/C/Velocity) + 1 depth.
    //
    // loadOp = LOAD sur tous les attachments : l'avatar est dessiné après le
    // terrain qui a déjà rempli le G-buffer. Adapté du `m_renderPassLoad` de
    // GeometryPass (cf. GeometryPass.cpp:166-240).
    // -------------------------------------------------------------------------
    VkAttachmentDescription attachments[5] = {};
    attachments[0].format         = formatA;
    attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[0].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    attachments[1].format         = formatB;
    attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[1].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    attachments[2].format         = formatC;
    attachments[2].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[2].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[2].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    attachments[3].format         = formatVelocity;
    attachments[3].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[3].loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[3].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[3].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[3].initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[3].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    attachments[4].format         = depthFormat;
    attachments[4].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[4].loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[4].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[4].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[4].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[4].initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[4].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRefs[4] = {
        { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
        { 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
        { 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
        { 3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }
    };
    VkAttachmentReference depthRef = { 4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 4;
    subpass.pColorAttachments       = colorRefs;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep = {};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                      | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                      | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                      | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                      | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
                      | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                      | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                      | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo = {};
    rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 5;
    rpInfo.pAttachments    = attachments;
    rpInfo.subpassCount    = 1;
    rpInfo.pSubpasses      = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies   = &dep;

    if (vkCreateRenderPass(device, &rpInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        LOG_ERROR(Render, "[SkinnedRenderer] vkCreateRenderPass failed");
        Destroy(device);
        return false;
    }

    // -------------------------------------------------------------------------
    // Descriptor set layout du SSBO bones (set 1, binding 0, STORAGE_BUFFER).
    // -------------------------------------------------------------------------
    VkDescriptorSetLayoutBinding boneBinding = {};
    boneBinding.binding         = 0;
    boneBinding.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boneBinding.descriptorCount = 1;
    boneBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo boneSetLayoutCi = {};
    boneSetLayoutCi.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    boneSetLayoutCi.bindingCount = 1;
    boneSetLayoutCi.pBindings    = &boneBinding;

    if (vkCreateDescriptorSetLayout(device, &boneSetLayoutCi, nullptr, &m_boneSetLayout) != VK_SUCCESS) {
        LOG_ERROR(Render, "[SkinnedRenderer] bone set layout creation failed");
        Destroy(device);
        return false;
    }

    // -------------------------------------------------------------------------
    // Pipeline layout : 2 set layouts (matériau set 0, bones set 1) + push
    // constants 144 octets (mêmes que GeometryPass, partage du frag shader).
    // -------------------------------------------------------------------------
    VkDescriptorSetLayout setLayouts[2] = { materialLayout, m_boneSetLayout };

    VkPushConstantRange pushRange = {};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset     = 0;
    pushRange.size       = kPushConstantSize;

    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = 2;
    layoutInfo.pSetLayouts            = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        LOG_ERROR(Render, "[SkinnedRenderer] vkCreatePipelineLayout failed");
        Destroy(device);
        return false;
    }

    // -------------------------------------------------------------------------
    // Shader modules (vert + frag). Identique à GeometryPass.
    // -------------------------------------------------------------------------
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
    {
        VkShaderModuleCreateInfo modInfo = {};
        modInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        modInfo.pCode    = vertSpirv;
        modInfo.codeSize = vertWordCount * sizeof(uint32_t);
        if (vkCreateShaderModule(device, &modInfo, nullptr, &vertModule) != VK_SUCCESS) {
            LOG_ERROR(Render, "[SkinnedRenderer] vertex shader module creation failed");
            Destroy(device);
            return false;
        }

        modInfo.pCode    = fragSpirv;
        modInfo.codeSize = fragWordCount * sizeof(uint32_t);
        if (vkCreateShaderModule(device, &modInfo, nullptr, &fragModule) != VK_SUCCESS) {
            LOG_ERROR(Render, "[SkinnedRenderer] fragment shader module creation failed");
            vkDestroyShaderModule(device, vertModule, nullptr);
            Destroy(device);
            return false;
        }
    }

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName  = "main";

    // -------------------------------------------------------------------------
    // Vertex input — extension de GeometryPass :
    //   binding 0 (stride 56) : SkinnedVertex = pos(12) + normal(12) + uv(8) +
    //                           boneIdx(8) + weights(16). Cf. SkinnedVertex
    //                           static_assert dans SkinnedMeshLoader.h.
    //   binding 1 (stride 64) : mat4 per-instance (model matrix).
    //
    // 9 attributes (vs 7 chez GeometryPass) — ajoute boneIdx@loc7 (uint16x4)
    // et weights@loc8 (vec4) sur binding 0 :
    //   loc0  pos          binding=0  offset=0   R32G32B32_SFLOAT
    //   loc1  normal       binding=0  offset=12  R32G32B32_SFLOAT
    //   loc2  uv           binding=0  offset=24  R32G32_SFLOAT
    //   loc3  instanceRow0 binding=1  offset=0   R32G32B32A32_SFLOAT
    //   loc4  instanceRow1 binding=1  offset=16  R32G32B32A32_SFLOAT
    //   loc5  instanceRow2 binding=1  offset=32  R32G32B32A32_SFLOAT
    //   loc6  instanceRow3 binding=1  offset=48  R32G32B32A32_SFLOAT
    //   loc7  boneIdx      binding=0  offset=32  R16G16B16A16_UINT
    //   loc8  weights      binding=0  offset=40  R32G32B32A32_SFLOAT
    // -------------------------------------------------------------------------
    VkVertexInputBindingDescription bindings[2] = {};
    bindings[0].binding   = 0;
    bindings[0].stride    = 56;
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindings[1].binding   = 1;
    bindings[1].stride    = 64;
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attrs[9] = {};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    0 };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT,    12 };
    attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,       24 };
    attrs[3] = { 3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0 };
    attrs[4] = { 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16 };
    attrs[5] = { 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32 };
    attrs[6] = { 6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48 };
    attrs[7] = { 7, 0, VK_FORMAT_R16G16B16A16_UINT,   32 };
    attrs[8] = { 8, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 40 };

    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 2;
    vi.pVertexBindingDescriptions      = bindings;
    vi.vertexAttributeDescriptionCount = 9;
    vi.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    // -------------------------------------------------------------------------
    // Rasterization — CCW (différent de GeometryPass qui est CW).
    //
    // La spec glTF impose le winding counter-clockwise pour les triangles
    // front-facing. Combiné à `Mat4::PerspectiveVulkan` qui inverse Y, un
    // mesh CCW en world-space reste CCW en framebuffer Vulkan. On utilise
    // donc CCW + BACK_BIT pour culler les back-faces correctement.
    //
    // NE PAS copier le `frontFace = CLOCKWISE` de GeometryPass : ce CW est
    // correct pour le mesh placeholder CW `avatar_placeholder.mesh`, mais
    // serait faux pour un mesh glTF (qui sort CCW de FBX2glTF/Mixamo).
    //
    // Cf. CLAUDE.md section "Convention winding / face culling".
    // -------------------------------------------------------------------------
    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_BACK_BIT;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;
    // Depth bias active (valeurs poussees dynamiquement par Record) : sert a
    // departager les couches qui se chevauchent sur l'avatar modulaire (peau du
    // corps SOUS l'habit, ~coplanaires sur les bras) -> sans biais elles
    // z-fightent et clignotent ("parait double") une fois texturees differemment
    // (peau vs habit). On pousse la PEAU legerement en arriere pour que l'habit
    // gagne la ou ils se superposent ; la peau exposee (mains) rend normalement.
    // depthBiasEnable seul ne biaise rien : tout vient des vkCmdSetDepthBias par
    // sous-maillage (0 pour l'habit, valeur config pour la peau).
    rs.depthBiasEnable = VK_TRUE;

    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds = {};
    ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState blendAtt[4] = {};
    for (auto& att : blendAtt) {
        att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                           | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }

    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 4;
    cb.pAttachments    = blendAtt;

    // DEPTH_BIAS dynamique : valeurs (re)poussees par Record selon que le
    // sous-maillage est de la peau (biais config) ou de l'habit (0).
    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                   VK_DYNAMIC_STATE_DEPTH_BIAS };
    VkPipelineDynamicStateCreateInfo dyn = {};
    dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 3;
    dyn.pDynamicStates    = dynStates;

    VkGraphicsPipelineCreateInfo gpInfo = {};
    gpInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpInfo.stageCount          = 2;
    gpInfo.pStages             = stages;
    gpInfo.pVertexInputState   = &vi;
    gpInfo.pInputAssemblyState = &ia;
    gpInfo.pViewportState      = &vp;
    gpInfo.pRasterizationState = &rs;
    gpInfo.pMultisampleState   = &ms;
    gpInfo.pDepthStencilState  = &ds;
    gpInfo.pColorBlendState    = &cb;
    gpInfo.pDynamicState       = &dyn;
    gpInfo.layout              = m_pipelineLayout;
    gpInfo.renderPass          = m_renderPass;
    gpInfo.subpass             = 0;

    const VkResult pipelineResult = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                                              &gpInfo, nullptr, &m_pipeline);

    // Les modules shader ne sont plus utiles après la création du pipeline.
    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    if (pipelineResult != VK_SUCCESS) {
        LOG_ERROR(Render, "[SkinnedRenderer] vkCreateGraphicsPipelines failed: {}",
                      static_cast<int>(pipelineResult));
        Destroy(device);
        return false;
    }

    // -------------------------------------------------------------------------
    // Bone SSBO (host-visible + coherent). Dimensionné pour maxBones mat4.
    // Réécrit chaque Record (Task 14).
    // -------------------------------------------------------------------------
    const VkDeviceSize boneBufferSize =
        static_cast<VkDeviceSize>(maxBonesPerSkeleton) * sizeof(float) * 16;
    if (!CreateEmptyHostVisibleBuffer(device, physicalDevice,
                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                      boneBufferSize, &m_boneSsbo, &m_boneSsboMemory)) {
        LOG_ERROR(Render, "[SkinnedRenderer] bone SSBO creation failed ({} bytes)",
                      static_cast<size_t>(boneBufferSize));
        Destroy(device);
        return false;
    }

    // -------------------------------------------------------------------------
    // Model instance buffer (1 mat4 = 64 octets). Réécrit chaque Record.
    // -------------------------------------------------------------------------
    if (!CreateEmptyHostVisibleBuffer(device, physicalDevice,
                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                      64u, &m_modelInstanceBuffer, &m_modelInstanceMemory)) {
        LOG_ERROR(Render, "[SkinnedRenderer] model instance buffer creation failed");
        Destroy(device);
        return false;
    }

    // -------------------------------------------------------------------------
    // Descriptor pool (1 SSBO max) + allocation du bone descriptor set +
    // binding du SSBO sur ce set.
    // -------------------------------------------------------------------------
    VkDescriptorPoolSize poolSize = {};
    poolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolCi = {};
    poolCi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCi.maxSets       = 1;
    poolCi.poolSizeCount = 1;
    poolCi.pPoolSizes    = &poolSize;

    if (vkCreateDescriptorPool(device, &poolCi, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        LOG_ERROR(Render, "[SkinnedRenderer] descriptor pool creation failed");
        Destroy(device);
        return false;
    }

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &m_boneSetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &m_boneDescriptorSet) != VK_SUCCESS) {
        LOG_ERROR(Render, "[SkinnedRenderer] bone descriptor set allocation failed");
        Destroy(device);
        return false;
    }

    VkDescriptorBufferInfo boneBufInfo = {};
    boneBufInfo.buffer = m_boneSsbo;
    boneBufInfo.offset = 0;
    boneBufInfo.range  = VK_WHOLE_SIZE;

    VkWriteDescriptorSet write = {};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = m_boneDescriptorSet;
    write.dstBinding      = 0;
    write.dstArrayElement = 0;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo     = &boneBufInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    LOG_INFO(Render, "[SkinnedRenderer] Init OK (maxBones={}, SSBO={} bytes)",
                 maxBonesPerSkeleton, static_cast<size_t>(boneBufferSize));
    return true;
}

// -----------------------------------------------------------------------------
// SkinnedRenderer::Record — implémentation Task 14.
//
// Enregistre une draw call skinnée dans `cmd`. L'appelant est responsable de
// vkCmdBeginRenderPass / vkCmdEndRenderPass autour de Record (même convention
// que `GeometryPass::Record`). Les paramètres `renderPass` / `framebuffer`
// restent dans la signature pour symétrie et usage futur éventuel.
//
// Caveat known FIF (frame-in-flight) : si l'engine a FIF >= 2, les buffers
// host-coherent réécrits chaque frame peuvent racer avec la draw précédente.
// Pour l'unique avatar du scope A c'est peu visible, mais le fix propre
// (duplication des buffers par frame) appartient à une PR de suivi.
// -----------------------------------------------------------------------------

void SkinnedRenderer::Record(VkDevice device, VkCommandBuffer cmd,
                             VkExtent2D extent,
                             VkRenderPass /*renderPass*/, VkFramebuffer /*framebuffer*/,
                             const float* prevViewProj, const float* viewProj,
                             const SkinnedMesh& mesh,
                             const std::vector<engine::math::Mat4>& finalBoneMatrices,
                             VkDescriptorSet materialDescriptorSet,
                             const float* modelMatrixColumnMajor4x4,
                             uint32_t materialIndex,
                             const std::vector<uint32_t>& submeshMaterialIndices,
                             uint32_t skinMaterialIndex,
                             float skinDepthBiasConstant,
                             float skinDepthBiasSlope)
{
    // 1. Upload des matrices de bones dans le SSBO host-visible.
    //    Clamp au max alloué dans Init pour ne jamais déborder (256 bones par
    //    défaut). Si la liste est vide on saute l'upload (mesh sans skin
    //    valide ne devrait normalement pas arriver mais on reste défensif).
    {
        const size_t boneCountClamped = std::min<size_t>(finalBoneMatrices.size(), m_maxBones);
        const size_t boneBytes = boneCountClamped * sizeof(engine::math::Mat4);
        if (boneBytes > 0) {
            void* mapped = nullptr;
            if (vkMapMemory(device, m_boneSsboMemory, 0, boneBytes, 0, &mapped) == VK_SUCCESS) {
                std::memcpy(mapped, finalBoneMatrices.data(), boneBytes);
                vkUnmapMemory(device, m_boneSsboMemory);
            } else {
                LOG_WARN(Render, "[SkinnedRenderer] vkMapMemory failed for bone SSBO; skipping draw");
                return;
            }
        }
    }

    // 2. Upload de la matrice modèle (64 octets) dans le per-instance buffer.
    {
        void* mapped = nullptr;
        if (vkMapMemory(device, m_modelInstanceMemory, 0, 64, 0, &mapped) == VK_SUCCESS) {
            std::memcpy(mapped, modelMatrixColumnMajor4x4, 64);
            vkUnmapMemory(device, m_modelInstanceMemory);
        } else {
            LOG_WARN(Render, "[SkinnedRenderer] vkMapMemory failed for model instance buffer; skipping draw");
            return;
        }
    }

    // 3. Bind du pipeline graphique.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    // 4. Viewport + scissor dynamiques (le pipeline a été créé avec
    //    DYNAMIC_STATE_VIEWPORT/SCISSOR).
    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = static_cast<float>(extent.width);
    vp.height = static_cast<float>(extent.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // 5. Bind des descriptor sets : set 0 = matériau (fourni par l'appelant),
    //    set 1 = bone SSBO (interne, mis à jour à l'Init).
    VkDescriptorSet sets[2] = { materialDescriptorSet, m_boneDescriptorSet };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                             /*firstSet*/ 0, /*setCount*/ 2, sets, 0, nullptr);

    // 6. Push constants — layout identique à gbuffer_geometry.vert :
    //    prevViewProj (mat4, 64) + viewProj (mat4, 64) + materialIndex (uint,
    //    +12 padding pour aligner sur 16) = 144 octets. Doit matcher
    //    `kPushConstantSize` utilisé pour le pipeline layout.
    //    Le champ materialIndex est (re)poussé par sous-maillage plus bas ; les
    //    deux matrices restent constantes pour tous les draws de cet avatar.
    struct PushConstants {
        float prevViewProj[16];
        float viewProj[16];
        uint32_t materialIndex;
        uint32_t pad0;
        uint32_t pad1;
        uint32_t pad2;
    } pc;
    static_assert(sizeof(PushConstants) == 144, "PushConstants must match shader layout");
    std::memcpy(pc.prevViewProj, prevViewProj, sizeof(float) * 16);
    std::memcpy(pc.viewProj, viewProj, sizeof(float) * 16);
    pc.pad0 = pc.pad1 = pc.pad2 = 0;

    // 7. Bind des vertex buffers : per-vertex @ binding 0 (mesh), per-instance
    //    @ binding 1 (matrice modèle). Cf. layout vertex input dans Init.
    VkBuffer vbufs[2] = { mesh.vertexBuffer, m_modelInstanceBuffer };
    VkDeviceSize offsets[2] = { 0, 0 };
    vkCmdBindVertexBuffers(cmd, 0, 2, vbufs, offsets);

    // 8. Bind de l'index buffer (UINT32 — cf. SkinnedMeshCpuData::indices).
    vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    // 9. Draw call(s), 1 instance (1 avatar).
    //    Chemin multi-matériaux : si on a un index matériau par sous-maillage
    //    (parallèle à mesh.submeshes), on dessine chaque plage avec son propre
    //    materialIndex (habit vs peau). Le descriptor set (set 0) est bindless
    //    et déjà bindé : seul le push constant materialIndex change entre draws.
    //    Sinon, chemin mono-draw historique (mesh.indexCount d'un coup).
    // Le pipeline a depthBiasEnable=TRUE + DEPTH_BIAS dynamique : il FAUT donc
    // appeler vkCmdSetDepthBias avant chaque draw (sinon valeur indefinie). On
    // pose 0 par defaut, et le biais peau (skinDepthBias*) pour les sous-maillages
    // dont l'index materiau figure dans skinMaterialIndices.
    auto isSkin = [&](uint32_t matIndex) -> bool {
        return skinMaterialIndex != 0u && matIndex == skinMaterialIndex;
    };

    const bool perSubmesh =
        !mesh.submeshes.empty() && submeshMaterialIndices.size() == mesh.submeshes.size();
    if (perSubmesh) {
        for (size_t s = 0; s < mesh.submeshes.size(); ++s) {
            const SkinnedSubMesh& sub = mesh.submeshes[s];
            if (sub.indexCount == 0) continue;
            const uint32_t matIdx = submeshMaterialIndices[s];
            const bool skin = isSkin(matIdx);
            vkCmdSetDepthBias(cmd, skin ? skinDepthBiasConstant : 0.0f, 0.0f,
                              skin ? skinDepthBiasSlope : 0.0f);
            pc.materialIndex = matIdx;
            vkCmdPushConstants(cmd, m_pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(PushConstants), &pc);
            vkCmdDrawIndexed(cmd, sub.indexCount, /*instanceCount*/ 1,
                             sub.firstIndex, 0, 0);
        }
    } else {
        vkCmdSetDepthBias(cmd, 0.0f, 0.0f, 0.0f);
        pc.materialIndex = materialIndex;
        vkCmdPushConstants(cmd, m_pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(PushConstants), &pc);
        vkCmdDrawIndexed(cmd, mesh.indexCount, /*instanceCount*/ 1, 0, 0, 0);
    }
}

// -----------------------------------------------------------------------------
// SkinnedRenderer::Destroy — libère tout en ordre inverse, idempotent.
// -----------------------------------------------------------------------------

void SkinnedRenderer::Destroy(VkDevice device)
{
    if (device == VK_NULL_HANDLE) {
        // Aucun device fourni — on ne peut rien détruire, mais on reset les
        // handles internes pour rester cohérent si l'appelant relance un Init.
        m_device = VK_NULL_HANDLE;
        m_physicalDevice = VK_NULL_HANDLE;
        m_renderPass = VK_NULL_HANDLE;
        m_pipelineLayout = VK_NULL_HANDLE;
        m_pipeline = VK_NULL_HANDLE;
        m_boneSetLayout = VK_NULL_HANDLE;
        m_descriptorPool = VK_NULL_HANDLE;
        m_boneDescriptorSet = VK_NULL_HANDLE;
        m_boneSsbo = VK_NULL_HANDLE;
        m_boneSsboMemory = VK_NULL_HANDLE;
        m_modelInstanceBuffer = VK_NULL_HANDLE;
        m_modelInstanceMemory = VK_NULL_HANDLE;
        m_maxBones = 0;
        return;
    }

    // Le descriptor set est libéré automatiquement par la destruction du pool.
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
        m_boneDescriptorSet = VK_NULL_HANDLE;
    }

    if (m_modelInstanceBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_modelInstanceBuffer, nullptr);
        m_modelInstanceBuffer = VK_NULL_HANDLE;
    }
    if (m_modelInstanceMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_modelInstanceMemory, nullptr);
        m_modelInstanceMemory = VK_NULL_HANDLE;
    }

    if (m_boneSsbo != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_boneSsbo, nullptr);
        m_boneSsbo = VK_NULL_HANDLE;
    }
    if (m_boneSsboMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_boneSsboMemory, nullptr);
        m_boneSsboMemory = VK_NULL_HANDLE;
    }

    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_boneSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_boneSetLayout, nullptr);
        m_boneSetLayout = VK_NULL_HANDLE;
    }
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

    m_device = VK_NULL_HANDLE;
    m_physicalDevice = VK_NULL_HANDLE;
    m_maxBones = 0;
}

}  // namespace engine::render::skinned
