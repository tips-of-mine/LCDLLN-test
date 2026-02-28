#pragma once

/**
 * @file FrameGraph.h
 * @brief Frame Graph API: logical resources, passes with read/write, registry for Vulkan handles.
 *
 * Ticket: M02.1 — Frame Graph: API ressources + passes + registry.
 *
 * Resources are transient by default (frame lifetime). Pass = setup + execute(cmd, registry).
 * Resources and passes are named for debug.
 * Passes execute in compiled (topological) order after Compile() (M02.2).
 */

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::render {

/// Opaque frame-graph resource ID.
using ResourceId = uint32_t;

constexpr ResourceId kInvalidResourceId = ~0u;

// ---------------------------------------------------------------------------
// Descriptors (logical resource description)
// ---------------------------------------------------------------------------

/**
 * @brief Descriptor for a frame-graph image resource (logical).
 */
struct ImageDesc {
    uint32_t width  = 0;
    uint32_t height = 0;
    uint32_t layers = 1;
    VkFormat format = VK_FORMAT_UNDEFINED;
};

/**
 * @brief Descriptor for a frame-graph buffer resource (logical).
 */
struct BufferDesc {
    size_t size = 0;
};

// ---------------------------------------------------------------------------
// Registry: resolve ResourceId -> VkImage / VkBuffer / VkImageView
// ---------------------------------------------------------------------------

/**
 * @brief Maps frame-graph resource IDs to Vulkan handles.
 *
 * Filled by the backend when resources are created. Valid for the current frame.
 */
class Registry {
public:
    Registry() = default;

    /**
     * @brief Binds a VkImage to a resource ID.
     */
    void SetImage(ResourceId id, VkImage image);

    /**
     * @brief Binds a VkBuffer to a resource ID.
     */
    void SetBuffer(ResourceId id, VkBuffer buffer);

    /**
     * @brief Binds a VkImageView to a resource ID (view = image view for sampling/attachment).
     */
    void SetView(ResourceId id, VkImageView view);

    /**
     * @brief Returns the VkImage for the given resource ID, or VK_NULL_HANDLE.
     */
    [[nodiscard]] VkImage GetImage(ResourceId id) const;

    /**
     * @brief Returns the VkBuffer for the given resource ID, or VK_NULL_HANDLE.
     */
    [[nodiscard]] VkBuffer GetBuffer(ResourceId id) const;

    /**
     * @brief Returns the VkImageView for the given resource ID, or VK_NULL_HANDLE.
     */
    [[nodiscard]] VkImageView GetView(ResourceId id) const;

private:
    std::unordered_map<ResourceId, VkImage>     m_images;
    std::unordered_map<ResourceId, VkBuffer>     m_buffers;
    std::unordered_map<ResourceId, VkImageView>   m_views;
};

// ---------------------------------------------------------------------------
// PassBuilder: declare reads/writes and set execute callback
// ---------------------------------------------------------------------------

/**
 * @brief Builds a single pass: declare read/write resources and execute callback.
 */
class PassBuilder {
public:
    /**
     * @brief Declares a resource as read by this pass.
     */
    PassBuilder& Read(ResourceId id);

    /**
     * @brief Declares a resource as written by this pass.
     */
    PassBuilder& Write(ResourceId id);

    /**
     * @brief Sets the execute callback for this pass (cmd, registry).
     */
    PassBuilder& Execute(std::function<void(VkCommandBuffer, Registry&)> fn);

private:
    friend class FrameGraph;
    PassBuilder(FrameGraph* graph, size_t passIndex);
    FrameGraph* m_graph = nullptr;
    size_t      m_passIndex = 0;
};

// ---------------------------------------------------------------------------
// FrameGraph: createImage/createBuffer, addPass, execute passes in order
// ---------------------------------------------------------------------------

/**
 * @brief Frame graph: logical resources and passes, executed in add order.
 */
class FrameGraph {
public:
    FrameGraph() = default;

    /**
     * @brief Declares a logical image resource.
     *
     * @param desc  Image descriptor.
     * @param name  Debug name.
     * @return      Resource ID (kInvalidResourceId on error).
     */
    [[nodiscard]] ResourceId CreateImage(const ImageDesc& desc, std::string_view name);

    /**
     * @brief Declares a logical buffer resource.
     *
     * @param desc  Buffer descriptor.
     * @param name  Debug name.
     * @return      Resource ID (kInvalidResourceId on error).
     */
    [[nodiscard]] ResourceId CreateBuffer(const BufferDesc& desc, std::string_view name);

    /**
     * @brief Adds a pass and returns a builder to declare reads/writes and execute callback.
     *
     * @param name  Debug name for the pass.
     * @return      PassBuilder for this pass.
     */
    PassBuilder AddPass(std::string_view name);

    /**
     * @brief Builds dependency graph (writer→reader), topo-sorts passes, detects cycles.
     *
     * Multi-writer on the same resource triggers assert (MVP). Cycle triggers LOG_FATAL.
     *
     * @return  true if compilation succeeded, false on cycle or multi-writer.
     */
    [[nodiscard]] bool Compile();

    /**
     * @brief Executes passes in compiled (topological) order.
     *
     * Compile() must have been called and succeeded. Each pass receives cmd and registry.
     *
     * @param cmd      Command buffer to record into.
     * @param registry Registry to resolve resource IDs to Vulkan handles.
     */
    void Execute(VkCommandBuffer cmd, Registry& registry);

    /**
     * @brief Returns the debug name of a resource, or empty if invalid.
     */
    [[nodiscard]] std::string_view GetResourceName(ResourceId id) const;

    /**
     * @brief Returns the debug name of a pass by index.
     */
    [[nodiscard]] std::string_view GetPassName(size_t index) const;

    /**
     * @brief Returns whether the graph has been successfully compiled.
     */
    [[nodiscard]] bool IsCompiled() const noexcept { return m_compiled; }

private:
    friend class PassBuilder;

    struct ResourceEntry {
        ImageDesc  imageDesc;
        BufferDesc bufferDesc;
        bool       isImage = true;
        std::string name;
    };

    struct PassData {
        std::string name;
        std::vector<ResourceId> reads;
        std::vector<ResourceId> writes;
        std::function<void(VkCommandBuffer, Registry&)> execute;
    };

    std::vector<ResourceEntry> m_resources;
    std::vector<PassData>     m_passes;

    /// Pass indices in topological (compiled) order; valid after Compile() succeeds.
    std::vector<size_t> m_executionOrder;
    bool m_compiled = false;
};

} // namespace engine::render
