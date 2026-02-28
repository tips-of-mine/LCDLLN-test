/**
 * @file VkExposureReduce.cpp
 * @brief Luminance reduction + temporal exposure (M08.3).
 */

#include "engine/render/vk/VkExposureReduce.h"
#include "engine/core/Log.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace engine::render::vk {

namespace {

constexpr uint32_t kWorkgroupSize = 256u;

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

VkShaderModule CreateShaderModule(VkDevice device, const std::vector<uint8_t>& spirv) {
    if (spirv.empty() || (spirv.size() % 4) != 0) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spirv.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(spirv.data());
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS) return VK_NULL_HANDLE;
    return mod;
}

} // namespace

VkExposureReduce::~VkExposureReduce() { Shutdown(); }

void VkExposureReduce::DestroyResources() {
    if (m_device == VK_NULL_HANDLE) return;
    if (m_finalPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(m_device, m_finalPipeline, nullptr); m_finalPipeline = VK_NULL_HANDLE; }
    if (m_finalLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(m_device, m_finalLayout, nullptr); m_finalLayout = VK_NULL_HANDLE; }
    if (m_finalSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(m_device, m_finalSetLayout, nullptr); m_finalSetLayout = VK_NULL_HANDLE; }
    if (m_finalPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(m_device, m_finalPool, nullptr); m_finalPool = VK_NULL_HANDLE; }
    m_finalSet = VK_NULL_HANDLE;
    if (m_midPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(m_device, m_midPipeline, nullptr); m_midPipeline = VK_NULL_HANDLE; }
    if (m_midLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(m_device, m_midLayout, nullptr); m_midLayout = VK_NULL_HANDLE; }
    if (m_midSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(m_device, m_midSetLayout, nullptr); m_midSetLayout = VK_NULL_HANDLE; }
    if (m_midPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(m_device, m_midPool, nullptr); m_midPool = VK_NULL_HANDLE; }
    m_midSet = VK_NULL_HANDLE;
    if (m_reducePipeline != VK_NULL_HANDLE) { vkDestroyPipeline(m_device, m_reducePipeline, nullptr); m_reducePipeline = VK_NULL_HANDLE; }
    if (m_reduceLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(m_device, m_reduceLayout, nullptr); m_reduceLayout = VK_NULL_HANDLE; }
    if (m_reduceSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(m_device, m_reduceSetLayout, nullptr); m_reduceSetLayout = VK_NULL_HANDLE; }
    if (m_reducePool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(m_device, m_reducePool, nullptr); m_reducePool = VK_NULL_HANDLE; }
    m_reduceSet = VK_NULL_HANDLE;
    if (m_reduceSampler != VK_NULL_HANDLE) { vkDestroySampler(m_device, m_reduceSampler, nullptr); m_reduceSampler = VK_NULL_HANDLE; }
    for (int i = 0; i < 2; ++i) {
        if (m_exposureBuffers[i] != VK_NULL_HANDLE) { vkDestroyBuffer(m_device, m_exposureBuffers[i], nullptr); m_exposureBuffers[i] = VK_NULL_HANDLE; }
        if (m_exposureMemory[i] != VK_NULL_HANDLE) { vkFreeMemory(m_device, m_exposureMemory[i], nullptr); m_exposureMemory[i] = VK_NULL_HANDLE; }
    }
    if (m_reductionBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(m_device, m_reductionBuffer, nullptr); m_reductionBuffer = VK_NULL_HANDLE; }
    if (m_reductionMemory != VK_NULL_HANDLE) { vkFreeMemory(m_device, m_reductionMemory, nullptr); m_reductionMemory = VK_NULL_HANDLE; }
}

bool VkExposureReduce::Init(VkPhysicalDevice physicalDevice, VkDevice device, uint32_t maxPixels,
                            const std::vector<uint8_t>& compReduce,
                            const std::vector<uint8_t>& compMid,
                            const std::vector<uint8_t>& compFinal) {
    if (physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE || maxPixels == 0 ||
        compReduce.empty() || compMid.empty() || compFinal.empty())
        return false;

    m_physicalDevice = physicalDevice;
    m_device = device;
    m_maxPixels = maxPixels;

    m_numGroupsLevel0 = (maxPixels + kWorkgroupSize - 1u) / kWorkgroupSize;
    m_numGroupsLevel1 = (m_numGroupsLevel0 + kWorkgroupSize - 1u) / kWorkgroupSize;
    if (m_numGroupsLevel1 == 0u) m_numGroupsLevel1 = 1u;

    const VkDeviceSize level0Size = m_numGroupsLevel0 * 2u * sizeof(float);
    m_level0Offset = level0Size;
    m_level1Size = m_numGroupsLevel1 * 2u * sizeof(float);
    m_reductionSize = level0Size + m_level1Size;

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = m_reductionSize;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bci, nullptr, &m_reductionBuffer) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkExposureReduce: reduction buffer create failed");
        return false;
    }
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, m_reductionBuffer, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocInfo.memoryTypeIndex == UINT32_MAX) {
        vkDestroyBuffer(device, m_reductionBuffer, nullptr);
        m_reductionBuffer = VK_NULL_HANDLE;
        return false;
    }
    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_reductionMemory) != VK_SUCCESS ||
        vkBindBufferMemory(device, m_reductionBuffer, m_reductionMemory, 0) != VK_SUCCESS) {
        vkDestroyBuffer(device, m_reductionBuffer, nullptr);
        vkFreeMemory(device, m_reductionMemory, nullptr);
        m_reductionBuffer = VK_NULL_HANDLE;
        m_reductionMemory = VK_NULL_HANDLE;
        return false;
    }

    const VkDeviceSize exposureSize = 4u;
    for (int i = 0; i < 2; ++i) {
        VkBufferCreateInfo ebci{};
        ebci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ebci.size = exposureSize;
        ebci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        ebci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &ebci, nullptr, &m_exposureBuffers[i]) != VK_SUCCESS) goto fail_exposure;
        vkGetBufferMemoryRequirements(device, m_exposureBuffers[i], &memReqs);
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = FindMemoryType(physicalDevice, memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (allocInfo.memoryTypeIndex == UINT32_MAX) goto fail_exposure;
        if (vkAllocateMemory(device, &allocInfo, nullptr, &m_exposureMemory[i]) != VK_SUCCESS ||
            vkBindBufferMemory(device, m_exposureBuffers[i], m_exposureMemory[i], 0) != VK_SUCCESS)
            goto fail_exposure;
        void* ptr = nullptr;
        if (vkMapMemory(device, m_exposureMemory[i], 0, exposureSize, 0, &ptr) != VK_SUCCESS) goto fail_exposure;
        const float initVal = 1.0f;
        std::memcpy(ptr, &initVal, 4);
        vkUnmapMemory(device, m_exposureMemory[i]);
    }

    VkShaderModule reduceMod = CreateShaderModule(device, compReduce);
    VkShaderModule midMod = CreateShaderModule(device, compMid);
    VkShaderModule finalMod = CreateShaderModule(device, compFinal);
    if (reduceMod == VK_NULL_HANDLE || midMod == VK_NULL_HANDLE || finalMod == VK_NULL_HANDLE) {
        if (reduceMod != VK_NULL_HANDLE) vkDestroyShaderModule(device, reduceMod, nullptr);
        if (midMod != VK_NULL_HANDLE) vkDestroyShaderModule(device, midMod, nullptr);
        if (finalMod != VK_NULL_HANDLE) vkDestroyShaderModule(device, finalMod, nullptr);
        LOG_ERROR(Render, "VkExposureReduce: shader module failed");
        DestroyResources();
        return false;
    }

    std::array<VkDescriptorSetLayoutBinding, 2> reduceBindings{};
    reduceBindings[0].binding = 0;
    reduceBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    reduceBindings[0].descriptorCount = 1;
    reduceBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    reduceBindings[1].binding = 1;
    reduceBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    reduceBindings[1].descriptorCount = 1;
    reduceBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 2;
    dslci.pBindings = reduceBindings.data();
    if (vkCreateDescriptorSetLayout(device, &dslci, nullptr, &m_reduceSetLayout) != VK_SUCCESS) goto fail_reduce_setup;
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = 8;
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &m_reduceSetLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &m_reduceLayout) != VK_SUCCESS) goto fail_reduce_setup;
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 2;
    dpci.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device, &dpci, nullptr, &m_reducePool) != VK_SUCCESS) goto fail_reduce_setup;
    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = m_reducePool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &m_reduceSetLayout;
    if (vkAllocateDescriptorSets(device, &dsai, &m_reduceSet) != VK_SUCCESS) goto fail_reduce_setup;
    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &sci, nullptr, &m_reduceSampler) != VK_SUCCESS) goto fail_reduce_setup;
    VkComputePipelineCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = reduceMod;
    stage.pName = "main";
    cpci.stage = stage;
    cpci.layout = m_reduceLayout;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &m_reducePipeline) != VK_SUCCESS) goto fail_reduce_setup;

    std::array<VkDescriptorSetLayoutBinding, 2> midBindings{};
    midBindings[0].binding = 0;
    midBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    midBindings[0].descriptorCount = 1;
    midBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    midBindings[1].binding = 1;
    midBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    midBindings[1].descriptorCount = 1;
    midBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    dslci.bindingCount = 2;
    dslci.pBindings = midBindings.data();
    if (vkCreateDescriptorSetLayout(device, &dslci, nullptr, &m_midSetLayout) != VK_SUCCESS) goto fail_mid_setup;
    pcr.size = 4;
    plci.pSetLayouts = &m_midSetLayout;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &m_midLayout) != VK_SUCCESS) goto fail_mid_setup;
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 2;
    dpci.pPoolSizes = poolSizes;
    dpci.poolSizeCount = 1;
    dpci.maxSets = 1;
    if (vkCreateDescriptorPool(device, &dpci, nullptr, &m_midPool) != VK_SUCCESS) goto fail_mid_setup;
    dsai.descriptorPool = m_midPool;
    dsai.pSetLayouts = &m_midSetLayout;
    if (vkAllocateDescriptorSets(device, &dsai, &m_midSet) != VK_SUCCESS) goto fail_mid_setup;
    stage.module = midMod;
    cpci.layout = m_midLayout;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &m_midPipeline) != VK_SUCCESS) goto fail_mid_setup;

    std::array<VkDescriptorSetLayoutBinding, 3> finalBindings{};
    finalBindings[0].binding = 0;
    finalBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    finalBindings[0].descriptorCount = 1;
    finalBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    finalBindings[1].binding = 1;
    finalBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    finalBindings[1].descriptorCount = 1;
    finalBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    finalBindings[2].binding = 2;
    finalBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    finalBindings[2].descriptorCount = 1;
    finalBindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    dslci.bindingCount = 3;
    dslci.pBindings = finalBindings.data();
    if (vkCreateDescriptorSetLayout(device, &dslci, nullptr, &m_finalSetLayout) != VK_SUCCESS) goto fail_final_setup;
    pcr.size = 16;
    plci.pSetLayouts = &m_finalSetLayout;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &m_finalLayout) != VK_SUCCESS) goto fail_final_setup;
    poolSizes[0].descriptorCount = 3;
    dpci.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device, &dpci, nullptr, &m_finalPool) != VK_SUCCESS) goto fail_final_setup;
    dsai.descriptorPool = m_finalPool;
    dsai.pSetLayouts = &m_finalSetLayout;
    if (vkAllocateDescriptorSets(device, &dsai, &m_finalSet) != VK_SUCCESS) goto fail_final_setup;
    stage.module = finalMod;
    cpci.layout = m_finalLayout;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &m_finalPipeline) != VK_SUCCESS) goto fail_final_setup;

    vkDestroyShaderModule(device, reduceMod, nullptr);
    vkDestroyShaderModule(device, midMod, nullptr);
    vkDestroyShaderModule(device, finalMod, nullptr);
    return true;

fail_exposure:
    DestroyResources();
    return false;
fail_reduce_setup:
    vkDestroyShaderModule(device, reduceMod, nullptr);
    vkDestroyShaderModule(device, midMod, nullptr);
    vkDestroyShaderModule(device, finalMod, nullptr);
    DestroyResources();
    return false;
fail_mid_setup:
    vkDestroyShaderModule(device, midMod, nullptr);
    vkDestroyShaderModule(device, finalMod, nullptr);
    DestroyResources();
    return false;
fail_final_setup:
    vkDestroyShaderModule(device, finalMod, nullptr);
    DestroyResources();
    return false;
}

bool VkExposureReduce::Recreate(uint32_t maxPixels) {
    if (m_device == VK_NULL_HANDLE || maxPixels == 0) return false;
    if (maxPixels == m_maxPixels) return true;
    m_maxPixels = maxPixels;
    m_numGroupsLevel0 = (maxPixels + kWorkgroupSize - 1u) / kWorkgroupSize;
    m_numGroupsLevel1 = (m_numGroupsLevel0 + kWorkgroupSize - 1u) / kWorkgroupSize;
    if (m_numGroupsLevel1 == 0u) m_numGroupsLevel1 = 1u;
    m_level0Offset = m_numGroupsLevel0 * 2u * sizeof(float);
    m_level1Size = m_numGroupsLevel1 * 2u * sizeof(float);
    m_reductionSize = m_level0Offset + m_level1Size;

    if (m_reductionBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, m_reductionBuffer, nullptr);
    if (m_reductionMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_reductionMemory, nullptr);
    m_reductionBuffer = VK_NULL_HANDLE;
    m_reductionMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = m_reductionSize;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bci, nullptr, &m_reductionBuffer) != VK_SUCCESS) return false;
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(m_device, m_reductionBuffer, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(m_physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocInfo.memoryTypeIndex == UINT32_MAX) {
        vkDestroyBuffer(m_device, m_reductionBuffer, nullptr);
        m_reductionBuffer = VK_NULL_HANDLE;
        return false;
    }
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_reductionMemory) != VK_SUCCESS ||
        vkBindBufferMemory(m_device, m_reductionBuffer, m_reductionMemory, 0) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, m_reductionBuffer, nullptr);
        vkFreeMemory(m_device, m_reductionMemory, nullptr);
        m_reductionBuffer = VK_NULL_HANDLE;
        m_reductionMemory = VK_NULL_HANDLE;
        return false;
    }

    VkDescriptorBufferInfo inBuf{m_reductionBuffer, 0, m_level0Offset};
    VkDescriptorBufferInfo outBuf{m_reductionBuffer, m_level0Offset, m_level1Size};
    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_midSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &inBuf;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_midSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &outBuf;
    vkUpdateDescriptorSets(m_device, 2, writes.data(), 0, nullptr);

    VkDescriptorBufferInfo level1Buf{m_reductionBuffer, m_level0Offset, m_level1Size};
    writes[0].dstSet = m_finalSet;
    writes[0].dstBinding = 0;
    writes[0].pBufferInfo = &level1Buf;
    vkUpdateDescriptorSets(m_device, 1, writes.data(), 0, nullptr);

    return true;
}

void VkExposureReduce::Shutdown() {
    DestroyResources();
    m_physicalDevice = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
    m_maxPixels = 0u;
}

void VkExposureReduce::SetHDRView(VkImageView hdrView) {
    if (m_device == VK_NULL_HANDLE || m_reduceSet == VK_NULL_HANDLE) return;
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = m_reduceSampler;
    imageInfo.imageView = hdrView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_reductionBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = m_reductionSize;
    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_reduceSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &imageInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_reduceSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(m_device, 2, writes.data(), 0, nullptr);

    VkDescriptorBufferInfo inBuf{};
    inBuf.buffer = m_reductionBuffer;
    inBuf.offset = 0;
    inBuf.range = m_level0Offset;
    VkDescriptorBufferInfo outBuf{};
    outBuf.buffer = m_reductionBuffer;
    outBuf.offset = m_level0Offset;
    outBuf.range = m_level1Size;
    writes[0].dstSet = m_midSet;
    writes[0].dstBinding = 0;
    writes[0].pBufferInfo = &inBuf;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pImageInfo = nullptr;
    writes[1].dstSet = m_midSet;
    writes[1].dstBinding = 1;
    writes[1].pBufferInfo = &outBuf;
    vkUpdateDescriptorSets(m_device, 2, writes.data(), 0, nullptr);

    const uint32_t prevIdx = m_exposureWriteIndex ^ 1u;
    const uint32_t nextIdx = m_exposureWriteIndex;
    VkDescriptorBufferInfo level1Buf{};
    level1Buf.buffer = m_reductionBuffer;
    level1Buf.offset = m_level0Offset;
    level1Buf.range = m_level1Size;
    VkDescriptorBufferInfo prevBuf{};
    prevBuf.buffer = m_exposureBuffers[prevIdx];
    prevBuf.offset = 0;
    prevBuf.range = 4;
    VkDescriptorBufferInfo nextBuf{};
    nextBuf.buffer = m_exposureBuffers[nextIdx];
    nextBuf.offset = 0;
    nextBuf.range = 4;
    std::array<VkWriteDescriptorSet, 3> finalWrites{};
    finalWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    finalWrites[0].dstSet = m_finalSet;
    finalWrites[0].dstBinding = 0;
    finalWrites[0].descriptorCount = 1;
    finalWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    finalWrites[0].pBufferInfo = &level1Buf;
    finalWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    finalWrites[1].dstSet = m_finalSet;
    finalWrites[1].dstBinding = 1;
    finalWrites[1].descriptorCount = 1;
    finalWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    finalWrites[1].pBufferInfo = &prevBuf;
    finalWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    finalWrites[2].dstSet = m_finalSet;
    finalWrites[2].dstBinding = 2;
    finalWrites[2].descriptorCount = 1;
    finalWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    finalWrites[2].pBufferInfo = &nextBuf;
    vkUpdateDescriptorSets(m_device, 3, finalWrites.data(), 0, nullptr);
}

void VkExposureReduce::Dispatch(VkCommandBuffer cmd, uint32_t width, uint32_t height, float dt, float key, float speed) {
    if (m_reducePipeline == VK_NULL_HANDLE || width == 0 || height == 0) return;

    uint32_t numGroups0 = (width * height + kWorkgroupSize - 1u) / kWorkgroupSize;
    if (numGroups0 == 0u) numGroups0 = 1u;
    uint32_t numGroups1 = (numGroups0 + kWorkgroupSize - 1u) / kWorkgroupSize;
    if (numGroups1 == 0u) numGroups1 = 1u;

    uint32_t pushData[2];
    pushData[0] = width;
    pushData[1] = height;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_reducePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_reduceLayout, 0, 1, &m_reduceSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_reduceLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 8, pushData);
    vkCmdDispatch(cmd, numGroups0, 1, 1);

    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    pushData[0] = numGroups0;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_midPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_midLayout, 0, 1, &m_midSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_midLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, pushData);
    vkCmdDispatch(cmd, numGroups1, 1, 1);

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    struct FinalPush { uint32_t numGroups; float key; float speed; float dt; } finalPush;
    finalPush.numGroups = numGroups1;
    finalPush.key = key;
    finalPush.speed = speed;
    finalPush.dt = dt;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_finalPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_finalLayout, 0, 1, &m_finalSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_finalLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(finalPush), &finalPush);
    vkCmdDispatch(cmd, 1, 1, 1);

    m_exposureWriteIndex ^= 1u;
}

VkBuffer VkExposureReduce::GetExposureBuffer() const noexcept {
    return m_exposureBuffers[m_exposureWriteIndex ^ 1u];
}

} // namespace engine::render::vk
