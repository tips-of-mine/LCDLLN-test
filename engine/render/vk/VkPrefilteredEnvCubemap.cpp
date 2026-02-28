/**
 * @file VkPrefilteredEnvCubemap.cpp
 * @brief Prefiltered specular cubemap: GGX convolution per mip (roughness).
 *
 * Ticket: M05.3 — IBL: prefiltered specular cubemap (mips).
 */

#include "engine/render/vk/VkPrefilteredEnvCubemap.h"
#include "engine/core/Log.h"

#include <cmath>
#include <cstring>

namespace engine::render::vk {

namespace {

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice,
                        uint32_t typeFilter,
                        VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

uint32_t MipLevelsForSize(uint32_t size) {
    if (size == 0) return 0;
    return static_cast<uint32_t>(std::floor(std::log2(static_cast<double>(size)))) + 1u;
}

} // namespace

VkPrefilteredEnvCubemap::~VkPrefilteredEnvCubemap() {
    Shutdown();
}

void VkPrefilteredEnvCubemap::DestroyResources() {
    if (m_device == VK_NULL_HANDLE) return;

    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    m_descriptorSet = VK_NULL_HANDLE;
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_setLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
        m_setLayout = VK_NULL_HANDLE;
    }
    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
    if (m_viewCube != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_viewCube, nullptr);
        m_viewCube = VK_NULL_HANDLE;
    }
    for (VkImageView v : m_viewStorageMips) {
        if (v != VK_NULL_HANDLE)
            vkDestroyImageView(m_device, v, nullptr);
    }
    m_viewStorageMips.clear();
    if (m_image != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
    }
    if (m_memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }
}

bool VkPrefilteredEnvCubemap::Init(VkPhysicalDevice physicalDevice,
                                   VkDevice         device,
                                   const std::vector<uint8_t>& compSpirv,
                                   uint32_t         size) {
    if (physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE || compSpirv.empty() || size == 0) {
        return false;
    }
    if ((compSpirv.size() % 4) != 0) return false;

    m_physicalDevice = physicalDevice;
    m_device         = device;
    m_size           = size;
    m_mipCount       = MipLevelsForSize(size);
    if (m_mipCount == 0) return false;

    constexpr uint32_t numFaces = 6u;
    const VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = format;
    ici.extent        = { size, size, 1 };
    ici.mipLevels     = m_mipCount;
    ici.arrayLayers   = numFaces;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &ici, nullptr, &m_image) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkPrefilteredEnvCubemap: vkCreateImage failed");
        DestroyResources();
        return false;
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, m_image, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(physicalDevice, memReqs.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocInfo.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(device, &allocInfo, nullptr, &m_memory) != VK_SUCCESS ||
        vkBindImageMemory(device, m_image, m_memory, 0) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkPrefilteredEnvCubemap: memory alloc/bind failed");
        DestroyResources();
        return false;
    }

    m_viewStorageMips.resize(m_mipCount);
    VkImageViewCreateInfo ivci{};
    ivci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image   = m_image;
    ivci.format  = format;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    ivci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, numFaces };
    for (uint32_t mip = 0; mip < m_mipCount; ++mip) {
        ivci.subresourceRange.baseMipLevel = mip;
        ivci.subresourceRange.levelCount   = 1;
        if (vkCreateImageView(device, &ivci, nullptr, &m_viewStorageMips[mip]) != VK_SUCCESS) {
            LOG_ERROR(Render, "VkPrefilteredEnvCubemap: storage view mip {} failed", mip);
            DestroyResources();
            return false;
        }
    }

    ivci.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    ivci.subresourceRange.baseMipLevel = 0;
    ivci.subresourceRange.levelCount   = m_mipCount;
    if (vkCreateImageView(device, &ivci, nullptr, &m_viewCube) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkPrefilteredEnvCubemap: cube view failed");
        DestroyResources();
        return false;
    }

    VkSamplerCreateInfo sci{};
    sci.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter     = VK_FILTER_LINEAR;
    sci.minFilter     = VK_FILTER_LINEAR;
    sci.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod        = static_cast<float>(m_mipCount - 1);
    if (vkCreateSampler(device, &sci, nullptr, &m_sampler) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkPrefilteredEnvCubemap: sampler failed");
        DestroyResources();
        return false;
    }

    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 2;
    dslci.pBindings    = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &dslci, nullptr, &m_setLayout) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkPrefilteredEnvCubemap: set layout failed");
        DestroyResources();
        return false;
    }

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset     = 0;
    pcr.size       = sizeof(float);

    VkPipelineLayoutCreateInfo plci{};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &m_setLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkPrefilteredEnvCubemap: pipeline layout failed");
        DestroyResources();
        return false;
    }

    VkShaderModuleCreateInfo smci{};
    smci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = compSpirv.size();
    smci.pCode    = reinterpret_cast<const uint32_t*>(compSpirv.data());
    VkShaderModule compModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &smci, nullptr, &compModule) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkPrefilteredEnvCubemap: compute module failed");
        DestroyResources();
        return false;
    }

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = compModule;
    stage.pName  = "main";

    VkComputePipelineCreateInfo cpci{};
    cpci.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage  = stage;
    cpci.layout = m_pipelineLayout;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &m_pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device, compModule, nullptr);
        LOG_ERROR(Render, "VkPrefilteredEnvCubemap: compute pipeline failed");
        DestroyResources();
        return false;
    }
    vkDestroyShaderModule(device, compModule, nullptr);

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets       = 1;
    dpci.poolSizeCount = 2;
    dpci.pPoolSizes    = poolSizes.data();
    if (vkCreateDescriptorPool(device, &dpci, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkPrefilteredEnvCubemap: descriptor pool failed");
        DestroyResources();
        return false;
    }

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool     = m_descriptorPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts        = &m_setLayout;
    if (vkAllocateDescriptorSets(device, &dsai, &m_descriptorSet) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkPrefilteredEnvCubemap: allocate set failed");
        DestroyResources();
        return false;
    }

    return true;
}

bool VkPrefilteredEnvCubemap::Prefilter(VkDevice device,
                                        VkQueue queue,
                                        uint32_t queueFamilyIndex,
                                        VkImageView sourceCubeView,
                                        VkSampler sourceSampler) {
    if (m_device == VK_NULL_HANDLE || m_pipeline == VK_NULL_HANDLE || device != m_device) {
        return false;
    }
    if (sourceCubeView == VK_NULL_HANDLE || sourceSampler == VK_NULL_HANDLE) {
        return false;
    }

    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo cpci{};
    cpci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags             = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    cpci.queueFamilyIndex  = queueFamilyIndex;
    if (vkCreateCommandPool(device, &cpci, nullptr, &cmdPool) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkPrefilteredEnvCubemap: command pool failed");
        return false;
    }

    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool        = cmdPool;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &cbai, &cmdBuffer) != VK_SUCCESS) {
        vkDestroyCommandPool(device, cmdPool, nullptr);
        return false;
    }

    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(device, &fci, nullptr, &fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuffer);
        vkDestroyCommandPool(device, cmdPool, nullptr);
        return false;
    }

    VkCommandBufferBeginInfo cbbi{};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmdBuffer, &cbbi) != VK_SUCCESS) {
        vkDestroyFence(device, fence, nullptr);
        vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuffer);
        vkDestroyCommandPool(device, cmdPool, nullptr);
        return false;
    }

    const uint32_t numFaces = 6u;
    const float roughnessStep = (m_mipCount > 1u) ? (1.0f / static_cast<float>(m_mipCount - 1u)) : 0.0f;

    for (uint32_t mip = 0; mip < m_mipCount; ++mip) {
        VkImageMemoryBarrier toGeneral{};
        toGeneral.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toGeneral.srcAccessMask       = 0;
        toGeneral.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        toGeneral.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        toGeneral.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.image               = m_image;
        toGeneral.subresourceRange   = { VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, numFaces };
        vkCmdPipelineBarrier(cmdBuffer,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toGeneral);

        VkDescriptorImageInfo srcInfo{};
        srcInfo.sampler     = sourceSampler;
        srcInfo.imageView   = sourceCubeView;
        srcInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorImageInfo dstInfo{};
        dstInfo.imageView   = m_viewStorageMips[mip];
        dstInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet writes[2];
        writes[0].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet           = m_descriptorSet;
        writes[0].dstBinding       = 0;
        writes[0].dstArrayElement  = 0;
        writes[0].descriptorCount  = 1;
        writes[0].descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo       = &srcInfo;
        writes[1].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet           = m_descriptorSet;
        writes[1].dstBinding       = 1;
        writes[1].dstArrayElement  = 0;
        writes[1].descriptorCount  = 1;
        writes[1].descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo       = &dstInfo;
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

        float roughness = (m_mipCount > 1u) ? (static_cast<float>(mip) * roughnessStep) : 0.0f;
        roughness = (roughness < 0.04f) ? 0.04f : roughness;

        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout,
                                0, 1, &m_descriptorSet, 0, nullptr);
        vkCmdPushConstants(cmdBuffer, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float), &roughness);

        const uint32_t wg = 8u;
        const uint32_t mipSize = (m_size >> mip) > 0 ? (m_size >> mip) : 1u;
        const uint32_t groupsX = (mipSize + wg - 1) / wg;
        const uint32_t groupsY = (mipSize + wg - 1) / wg;
        vkCmdDispatch(cmdBuffer, groupsX, groupsY, numFaces);

        VkImageMemoryBarrier toRead{};
        toRead.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toRead.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        toRead.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        toRead.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
        toRead.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.image               = m_image;
        toRead.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, numFaces };
        vkCmdPipelineBarrier(cmdBuffer,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toRead);
    }

    if (vkEndCommandBuffer(cmdBuffer) != VK_SUCCESS) {
        vkDestroyFence(device, fence, nullptr);
        vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuffer);
        vkDestroyCommandPool(device, cmdPool, nullptr);
        return false;
    }

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmdBuffer;
    if (vkQueueSubmit(queue, 1, &si, fence) != VK_SUCCESS) {
        vkDestroyFence(device, fence, nullptr);
        vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuffer);
        vkDestroyCommandPool(device, cmdPool, nullptr);
        LOG_ERROR(Render, "VkPrefilteredEnvCubemap: queue submit failed");
        return false;
    }

    if (vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        vkDestroyFence(device, fence, nullptr);
        vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuffer);
        vkDestroyCommandPool(device, cmdPool, nullptr);
        return false;
    }

    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuffer);
    vkDestroyCommandPool(device, cmdPool, nullptr);
    return true;
}

void VkPrefilteredEnvCubemap::Shutdown() {
    DestroyResources();
    m_physicalDevice = VK_NULL_HANDLE;
    m_device         = VK_NULL_HANDLE;
    m_size           = 0;
    m_mipCount       = 0;
}

} // namespace engine::render::vk
