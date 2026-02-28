/**
 * @file VkBrdfLut.cpp
 * @brief BRDF LUT 256x256 RG16F, compute-generated for GGX split-sum.
 *
 * Ticket: M05.1 — IBL: BRDF LUT (compute) generation.
 */

#include "engine/render/vk/VkBrdfLut.h"
#include "engine/core/Log.h"

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

} // namespace

VkBrdfLut::~VkBrdfLut() {
    Shutdown();
}

void VkBrdfLut::DestroyResources() {
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
    if (m_imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_imageView, nullptr);
        m_imageView = VK_NULL_HANDLE;
    }
    if (m_image != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
    }
    if (m_memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }
}

bool VkBrdfLut::Init(VkPhysicalDevice physicalDevice,
                     VkDevice         device,
                     const std::vector<uint8_t>& compSpirv) {
    if (physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE || compSpirv.empty()) {
        return false;
    }
    if ((compSpirv.size() % 4) != 0) {
        return false;
    }

    m_physicalDevice = physicalDevice;
    m_device         = device;

    const VkFormat format = VK_FORMAT_R16G16_SFLOAT;
    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = format;
    ici.extent        = { kBrdfLutSize, kBrdfLutSize, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &ici, nullptr, &m_image) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkBrdfLut: vkCreateImage failed");
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
    if (allocInfo.memoryTypeIndex == UINT32_MAX) {
        LOG_ERROR(Render, "VkBrdfLut: no suitable memory type");
        DestroyResources();
        return false;
    }
    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_memory) != VK_SUCCESS ||
        vkBindImageMemory(device, m_image, m_memory, 0) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkBrdfLut: allocate/bind memory failed");
        DestroyResources();
        return false;
    }

    VkImageViewCreateInfo ivci{};
    ivci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image    = m_image;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format   = format;
    ivci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    ivci.subresourceRange.baseMipLevel   = 0;
    ivci.subresourceRange.levelCount     = 1;
    ivci.subresourceRange.baseArrayLayer = 0;
    ivci.subresourceRange.layerCount     = 1;
    if (vkCreateImageView(device, &ivci, nullptr, &m_imageView) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkBrdfLut: vkCreateImageView failed");
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
    if (vkCreateSampler(device, &sci, nullptr, &m_sampler) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkBrdfLut: vkCreateSampler failed");
        DestroyResources();
        return false;
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binding.descriptorCount= 1;
    binding.stageFlags     = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 1;
    dslci.pBindings    = &binding;
    if (vkCreateDescriptorSetLayout(device, &dslci, nullptr, &m_setLayout) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkBrdfLut: descriptor set layout failed");
        DestroyResources();
        return false;
    }

    VkPipelineLayoutCreateInfo plci{};
    plci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount  = 1;
    plci.pSetLayouts     = &m_setLayout;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkBrdfLut: pipeline layout failed");
        DestroyResources();
        return false;
    }

    VkShaderModuleCreateInfo smci{};
    smci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = compSpirv.size();
    smci.pCode    = reinterpret_cast<const uint32_t*>(compSpirv.data());
    VkShaderModule compModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &smci, nullptr, &compModule) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkBrdfLut: compute shader module failed");
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
        LOG_ERROR(Render, "VkBrdfLut: compute pipeline failed");
        DestroyResources();
        return false;
    }
    vkDestroyShaderModule(device, compModule, nullptr);

    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets       = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes    = &poolSize;
    if (vkCreateDescriptorPool(device, &dpci, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkBrdfLut: descriptor pool failed");
        DestroyResources();
        return false;
    }

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool     = m_descriptorPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts        = &m_setLayout;
    if (vkAllocateDescriptorSets(device, &dsai, &m_descriptorSet) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkBrdfLut: allocate descriptor set failed");
        DestroyResources();
        return false;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView   = m_imageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet write{};
    write.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet           = m_descriptorSet;
    write.dstBinding       = 0;
    write.dstArrayElement  = 0;
    write.descriptorCount  = 1;
    write.descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo       = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    return true;
}

bool VkBrdfLut::Generate(VkDevice device, VkQueue queue, uint32_t queueFamilyIndex) {
    if (m_device == VK_NULL_HANDLE || m_pipeline == VK_NULL_HANDLE || device != m_device) {
        return false;
    }

    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo cpci{};
    cpci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags             = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    cpci.queueFamilyIndex  = queueFamilyIndex;
    if (vkCreateCommandPool(device, &cpci, nullptr, &cmdPool) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkBrdfLut: command pool failed");
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
        LOG_ERROR(Render, "VkBrdfLut: allocate command buffer failed");
        return false;
    }

    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(device, &fci, nullptr, &fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuffer);
        vkDestroyCommandPool(device, cmdPool, nullptr);
        LOG_ERROR(Render, "VkBrdfLut: fence failed");
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

    VkImageMemoryBarrier barrierToGeneral{};
    barrierToGeneral.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrierToGeneral.srcAccessMask       = 0;
    barrierToGeneral.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    barrierToGeneral.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    barrierToGeneral.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    barrierToGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrierToGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrierToGeneral.image               = m_image;
    barrierToGeneral.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmdBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrierToGeneral);

    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout,
                            0, 1, &m_descriptorSet, 0, nullptr);
    const uint32_t wgSize = 8u;
    vkCmdDispatch(cmdBuffer, (kBrdfLutSize + wgSize - 1) / wgSize, (kBrdfLutSize + wgSize - 1) / wgSize, 1);

    VkImageMemoryBarrier barrierToRead{};
    barrierToRead.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrierToRead.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    barrierToRead.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    barrierToRead.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
    barrierToRead.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrierToRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrierToRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrierToRead.image               = m_image;
    barrierToRead.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmdBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrierToRead);

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
        LOG_ERROR(Render, "VkBrdfLut: queue submit failed");
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

void VkBrdfLut::Shutdown() {
    DestroyResources();
    m_physicalDevice = VK_NULL_HANDLE;
    m_device         = VK_NULL_HANDLE;
}

} // namespace engine::render::vk
