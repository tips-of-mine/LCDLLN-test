#pragma once

/**
 * @file EditorUI.h
 * @brief Editor mode UI: ImGui integration, panels (Scene/Inspector/Asset Browser), selection, gizmos (M12.1).
 *
 * Opaque implementation; no ImGui/Vulkan includes in header.
 */

#include "engine/world/ZoneBuildFormat.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct GLFWwindow;

namespace engine::editor {

/**
 * @brief Editor UI state and ImGui/Vulkan resources.
 *
 * Init once when entering editor mode; Shutdown when leaving or at exit.
 * RecreateFramebuffers when swapchain is recreated.
 */
class EditorUI {
public:
    EditorUI() = default;
    ~EditorUI();

    EditorUI(const EditorUI&) = delete;
    EditorUI& operator=(const EditorUI&) = delete;

    /**
     * @brief Initializes ImGui context and Vulkan/GLFW backends.
     *
     * @param instance         Vulkan instance.
     * @param physicalDevice  Physical device.
     * @param device          Logical device.
     * @param queue           Graphics queue.
     * @param queueFamily     Queue family index.
     * @param window          GLFW window.
     * @param swapchainFormat Swapchain image format.
     * @param imageCount      Number of swapchain images.
     * @return                true on success.
     */
    bool Init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
              VkQueue queue, uint32_t queueFamily, GLFWwindow* window,
              VkFormat swapchainFormat, uint32_t imageCount);

    /** @brief Releases ImGui and Vulkan resources. */
    void Shutdown();

    /**
     * @brief Recreates framebuffers for ImGui overlay (call when swapchain is created or recreated).
     *
     * @param device   Logical device.
     * @param images   Swapchain images (for layout barriers when rendering ImGui).
     * @param views    Swapchain image views (count = imageCount from Init).
     * @param count    Number of images/views.
     * @param extent   Swapchain extent.
     * @return         true on success.
     */
    bool RecreateFramebuffers(VkDevice device, const VkImage* images, const VkImageView* views, uint32_t count, VkExtent2D extent);

    /** @brief Call once per frame before drawing panels (ImGui new frame). */
    void BeginFrame();

    /**
     * @brief Draws editor panels: Scene, Inspector (transform + snap + align ground), Layers, Asset Browser, Save (M12.2).
     *
     * @param instances         Zone chunk instances (selected instance may be modified).
     * @param selectedIndex     Currently selected instance index (-1 = none).
     * @param layerVisible      Per-layer visibility (16 entries); toggles in Layers panel.
     * @param layerLocked      Per-layer lock (16 entries); toggles in Layers panel.
     * @param outDirty         Set to true when any instance or layer state changes.
     * @param outSaveRequested Set to true when user clicks Save.
     * @param instancesPath    Path to instances.bin (for Save; empty disables Save).
     * @param cameraViewCol    View matrix (column-major 16 floats).
     * @param cameraProjCol    Projection matrix (column-major 16 floats).
     * @param viewportWidth    Full framebuffer width.
     * @param viewportHeight   Full framebuffer height.
     */
    void DrawPanels(std::vector<::engine::world::ZoneChunkInstance>* instances,
                   int* selectedIndex,
                   bool* layerVisible,
                   bool* layerLocked,
                   bool* outDirty,
                   bool* outSaveRequested,
                   const std::string* instancesPath,
                   const float cameraViewCol[16], const float cameraProjCol[16],
                   int viewportWidth, int viewportHeight);

    /** @brief Call after DrawPanels to finalize ImGui draw data. */
    void EndFrame();

    /**
     * @brief Records ImGui draw commands into the given command buffer.
     *
     * Call after the main scene has been drawn to the swapchain image.
     * Transitions image to color attachment, renders ImGui, transitions to present.
     *
     * @param cmdBuffer   Command buffer (must be recording).
     * @param imageIndex  Current swapchain image index.
     * @param extent      Framebuffer extent.
     */
    void Render(VkCommandBuffer cmdBuffer, uint32_t imageIndex, VkExtent2D extent);

    /**
     * @brief Returns true if ImGui wants to capture mouse (e.g. over a panel).
     */
    bool WantCaptureMouse() const;

    /**
     * @brief Returns true if Init succeeded and the UI is ready to render.
     */
    bool IsReady() const noexcept { return m_ready; }

private:
    struct Impl;
    Impl* m_impl = nullptr;
    bool m_ready = false;
};

} // namespace engine::editor
