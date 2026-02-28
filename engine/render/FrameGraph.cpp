/**
 * @file FrameGraph.cpp
 * @brief Frame Graph API implementation: Registry, PassBuilder, FrameGraph.
 */

#include "engine/render/FrameGraph.h"

namespace engine::render {

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

PassBuilder& PassBuilder::Read(ResourceId id) {
    if (m_graph && m_passIndex < m_graph->m_passes.size()) {
        m_graph->m_passes[m_passIndex].reads.push_back(id);
    }
    return *this;
}

PassBuilder& PassBuilder::Write(ResourceId id) {
    if (m_graph && m_passIndex < m_graph->m_passes.size()) {
        m_graph->m_passes[m_passIndex].writes.push_back(id);
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

void FrameGraph::Execute(VkCommandBuffer cmd, Registry& registry) {
    for (PassData& pass : m_passes) {
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
