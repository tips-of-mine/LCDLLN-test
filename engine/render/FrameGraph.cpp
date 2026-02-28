/**
 * @file FrameGraph.cpp
 * @brief Frame Graph API implementation: Registry, PassBuilder, FrameGraph.
 *
 * M02.2: Dependency graph (writer→reader), topological sort (Kahn), cycle detection.
 * M02.3: Usage tracking and vkCmdPipelineBarrier between passes (layout + access).
 */

#include "engine/render/FrameGraph.h"
#include "engine/core/Log.h"

#include <cassert>
#include <queue>

namespace engine::render {

// ---------------------------------------------------------------------------
// Image usage -> layout/stage/access (M02.3)
// ---------------------------------------------------------------------------

ImageUsageState GetImageUsageState(ImageUsage usage) {
    switch (usage) {
    case ImageUsage::ColorWrite:
        return {
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        };
    case ImageUsage::DepthWrite:
        return {
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        };
    case ImageUsage::SampledRead:
        return {
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT,
        };
    case ImageUsage::TransferSrc:
        return {
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
        };
    case ImageUsage::TransferDst:
        return {
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,
        };
    }
    return { VK_IMAGE_LAYOUT_UNDEFINED, 0, 0 };
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

void Registry::SetImage(ResourceId id, VkImage image) {
    m_images[id] = image;
}

void Registry::SetBuffer(ResourceId id, VkBuffer buffer) {
    m_buffers[id] = buffer;
}

void Registry::SetView(ResourceId id, VkImageView view) {
    m_views[id] = view;
}

VkImage Registry::GetImage(ResourceId id) const {
    auto it = m_images.find(id);
    return it != m_images.end() ? it->second : VK_NULL_HANDLE;
}

VkBuffer Registry::GetBuffer(ResourceId id) const {
    auto it = m_buffers.find(id);
    return it != m_buffers.end() ? it->second : VK_NULL_HANDLE;
}

VkImageView Registry::GetView(ResourceId id) const {
    auto it = m_views.find(id);
    return it != m_views.end() ? it->second : VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
// PassBuilder
// ---------------------------------------------------------------------------

PassBuilder::PassBuilder(FrameGraph* graph, size_t passIndex)
    : m_graph(graph), m_passIndex(passIndex) {}

PassBuilder& PassBuilder::Read(ResourceId id, ImageUsage usage) {
    if (m_graph && m_passIndex < m_graph->m_passes.size()) {
        m_graph->m_passes[m_passIndex].reads.push_back(id);
        m_graph->m_passes[m_passIndex].readUsages.push_back(usage);
    }
    return *this;
}

PassBuilder& PassBuilder::Write(ResourceId id, ImageUsage usage) {
    if (m_graph && m_passIndex < m_graph->m_passes.size()) {
        m_graph->m_passes[m_passIndex].writes.push_back(id);
        m_graph->m_passes[m_passIndex].writeUsages.push_back(usage);
    }
    return *this;
}

PassBuilder& PassBuilder::Execute(std::function<void(VkCommandBuffer, Registry&)> fn) {
    if (m_graph && m_passIndex < m_graph->m_passes.size()) {
        m_graph->m_passes[m_passIndex].execute = std::move(fn);
    }
    return *this;
}

// ---------------------------------------------------------------------------
// FrameGraph
// ---------------------------------------------------------------------------

ResourceId FrameGraph::CreateImage(const ImageDesc& desc, std::string_view name) {
    ResourceId id = static_cast<ResourceId>(m_resources.size());
    ResourceEntry e;
    e.imageDesc = desc;
    e.isImage = true;
    e.name.assign(name);
    m_resources.push_back(std::move(e));
    return id;
}

ResourceId FrameGraph::CreateBuffer(const BufferDesc& desc, std::string_view name) {
    ResourceId id = static_cast<ResourceId>(m_resources.size());
    ResourceEntry e;
    e.bufferDesc = desc;
    e.isImage = false;
    e.name.assign(name);
    m_resources.push_back(std::move(e));
    return id;
}

PassBuilder FrameGraph::AddPass(std::string_view name) {
    PassData pass;
    pass.name.assign(name);
    m_passes.push_back(std::move(pass));
    return PassBuilder(this, m_passes.size() - 1);
}

bool FrameGraph::Compile() {
    m_executionOrder.clear();
    m_compiled = false;

    const size_t numPasses = m_passes.size();
    if (numPasses == 0) {
        m_compiled = true;
        return true;
    }

    const size_t numResources = m_resources.size();
    std::vector<size_t> lastWriter(numResources, numPasses);
    std::vector<std::vector<size_t>> successors(numPasses);

    for (size_t i = 0; i < numPasses; ++i) {
        const PassData& pass = m_passes[i];

        for (ResourceId rid : pass.reads) {
            if (rid >= numResources) continue;
            const size_t prev = lastWriter[rid];
            if (prev < numPasses) {
                successors[prev].push_back(i);
            }
        }

        for (ResourceId rid : pass.writes) {
            if (rid >= numResources) continue;
            const size_t prev = lastWriter[rid];
            assert((prev >= numPasses || prev == i) && "FrameGraph: multi-writer not allowed (MVP)");
            lastWriter[rid] = i;
        }
    }

    std::vector<size_t> inDegree(numPasses, 0);
    for (const auto& succ : successors) {
        for (size_t j : succ) {
            ++inDegree[j];
        }
    }

    std::queue<size_t> q;
    for (size_t i = 0; i < numPasses; ++i) {
        if (inDegree[i] == 0) {
            q.push(i);
        }
    }

    m_executionOrder.reserve(numPasses);
    while (!q.empty()) {
        const size_t u = q.front();
        q.pop();
        m_executionOrder.push_back(u);
        for (size_t v : successors[u]) {
            if (--inDegree[v] == 0) {
                q.push(v);
            }
        }
    }

    if (m_executionOrder.size() != numPasses) {
        LOG_FATAL(Render, "FrameGraph: cycle detected in pass dependencies");
        return false;
    }

    m_compiled = true;
    return true;
}

void FrameGraph::Execute(VkCommandBuffer cmd, Registry& registry) {
    assert(m_compiled && "FrameGraph::Execute: call Compile() first");
    if (!m_compiled) {
        return;
    }

    const size_t numResources = m_resources.size();
    std::vector<VkImageLayout>        lastLayout(numResources, VK_IMAGE_LAYOUT_UNDEFINED);
    std::vector<VkPipelineStageFlags> lastStage(numResources, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    std::vector<VkAccessFlags>        lastAccess(numResources, 0);

    for (size_t idx : m_executionOrder) {
        if (idx >= m_passes.size()) continue;

        const PassData& pass = m_passes[idx];

        std::vector<VkImageMemoryBarrier> barriers;
        VkPipelineStageFlags srcStageMask = 0;
        VkPipelineStageFlags dstStageMask = 0;

        for (size_t i = 0; i < pass.reads.size() && i < pass.readUsages.size(); ++i) {
            const ResourceId rid = pass.reads[i];
            if (rid >= numResources || !m_resources[rid].isImage) continue;
            const VkImage image = registry.GetImage(rid);
            if (image == VK_NULL_HANDLE) continue;

            const ImageUsageState req = GetImageUsageState(pass.readUsages[i]);
            if (lastLayout[rid] != req.layout || lastAccess[rid] != req.access) {
                VkImageMemoryBarrier bar = {};
                bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                bar.oldLayout = lastLayout[rid];
                bar.newLayout = req.layout;
                bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                bar.image = image;
                bar.subresourceRange.aspectMask = (pass.readUsages[i] == ImageUsage::DepthWrite)
                    ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                    : VK_IMAGE_ASPECT_COLOR_BIT;
                bar.subresourceRange.baseMipLevel = 0;
                bar.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
                bar.subresourceRange.baseArrayLayer = 0;
                bar.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
                bar.srcAccessMask = lastAccess[rid];
                bar.dstAccessMask = req.access;
                barriers.push_back(bar);
                srcStageMask |= lastStage[rid];
                dstStageMask |= req.stage;
            }
        }

        for (size_t i = 0; i < pass.writes.size() && i < pass.writeUsages.size(); ++i) {
            const ResourceId rid = pass.writes[i];
            if (rid >= numResources || !m_resources[rid].isImage) continue;
            const VkImage image = registry.GetImage(rid);
            if (image == VK_NULL_HANDLE) continue;

            const ImageUsageState req = GetImageUsageState(pass.writeUsages[i]);
            if (lastLayout[rid] != req.layout || lastAccess[rid] != req.access) {
                VkImageMemoryBarrier bar = {};
                bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                bar.oldLayout = lastLayout[rid];
                bar.newLayout = req.layout;
                bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                bar.image = image;
                bar.subresourceRange.aspectMask = (pass.writeUsages[i] == ImageUsage::DepthWrite)
                    ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                    : VK_IMAGE_ASPECT_COLOR_BIT;
                bar.subresourceRange.baseMipLevel = 0;
                bar.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
                bar.subresourceRange.baseArrayLayer = 0;
                bar.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
                bar.srcAccessMask = lastAccess[rid];
                bar.dstAccessMask = req.access;
                barriers.push_back(bar);
                srcStageMask |= lastStage[rid];
                dstStageMask |= req.stage;
            }
        }

        if (!barriers.empty()) {
            if (srcStageMask == 0) {
                srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            }
            vkCmdPipelineBarrier(cmd, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr,
                                static_cast<uint32_t>(barriers.size()), barriers.data());
        }

        for (size_t i = 0; i < pass.reads.size() && i < pass.readUsages.size(); ++i) {
            const ResourceId rid = pass.reads[i];
            if (rid < numResources && m_resources[rid].isImage) {
                const ImageUsageState s = GetImageUsageState(pass.readUsages[i]);
                lastLayout[rid] = s.layout;
                lastStage[rid] = s.stage;
                lastAccess[rid] = s.access;
            }
        }
        for (size_t i = 0; i < pass.writes.size() && i < pass.writeUsages.size(); ++i) {
            const ResourceId rid = pass.writes[i];
            if (rid < numResources && m_resources[rid].isImage) {
                const ImageUsageState s = GetImageUsageState(pass.writeUsages[i]);
                lastLayout[rid] = s.layout;
                lastStage[rid] = s.stage;
                lastAccess[rid] = s.access;
            }
        }

        if (pass.execute) {
            pass.execute(cmd, registry);
        }
    }
}

std::string_view FrameGraph::GetResourceName(ResourceId id) const {
    if (id >= m_resources.size()) {
        return "";
    }
    return m_resources[id].name;
}

std::string_view FrameGraph::GetPassName(size_t index) const {
    if (index >= m_passes.size()) {
        return "";
    }
    return m_passes[index].name;
}

} // namespace engine::render
