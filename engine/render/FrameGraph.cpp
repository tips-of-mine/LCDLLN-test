/**
 * @file FrameGraph.cpp
 * @brief Frame Graph API implementation: Registry, PassBuilder, FrameGraph.
 *
 * M02.2: Dependency graph (writer→reader), topological sort (Kahn), cycle detection.
 */

#include "engine/render/FrameGraph.h"
#include "engine/core/Log.h"

#include <cassert>
#include <queue>

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
    for (size_t idx : m_executionOrder) {
        if (idx < m_passes.size() && m_passes[idx].execute) {
            m_passes[idx].execute(cmd, registry);
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
