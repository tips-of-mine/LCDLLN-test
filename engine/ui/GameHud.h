#pragma once

/**
 * @file GameHud.h
 * @brief Game HUD: player HP bar, target frame, combat log (M16.2), inventory grid (M16.3).
 */

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace engine::ui {

/** @brief One inventory slot: itemId and stack count. */
struct InventorySlot {
    uint32_t itemId = 0u;
    uint32_t count = 0u;
};

/** @brief Data fed to the HUD each frame (player/target HP; combat log; inventory for panel). */
struct HudData {
    uint32_t playerHp = 100u;
    uint32_t playerMaxHp = 100u;
    bool hasTarget = false;
    uint32_t targetEntityId = 0u;
    uint32_t targetHp = 0u;
    uint32_t targetMaxHp = 100u;
    /** @brief Last N combat log lines (e.g. "You hit for 10" / "Target took 10"). */
    std::vector<std::string> combatLogLines;
    /** @brief Inventory slots to display in grid (itemId + count per slot). Updated via InventoryDelta on game side. */
    std::vector<InventorySlot> inventorySlots;
    /** @brief Optional itemId -> label for tooltip (e.g. from game/data items.json). */
    std::unordered_map<uint32_t, std::string> itemLabels;
};

/**
 * @brief Game HUD: ImGui overlay for player stats bar, target frame, and mini combat log.
 * Init when not in editor mode; same Vulkan/ImGui pattern as EditorUI.
 */
class GameHud {
public:
    GameHud() = default;
    ~GameHud();

    GameHud(const GameHud&) = delete;
    GameHud& operator=(const GameHud&) = delete;

    /** @brief Initializes ImGui context and Vulkan/GLFW backends (same pattern as EditorUI). */
    bool Init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
              VkQueue queue, uint32_t queueFamily, GLFWwindow* window,
              VkFormat swapchainFormat, uint32_t imageCount);

    /** @brief Releases ImGui and Vulkan resources. */
    void Shutdown();

    /** @brief Recreates framebuffers when swapchain is recreated. */
    bool RecreateFramebuffers(VkDevice device, const VkImage* images, const VkImageView* views, uint32_t count, VkExtent2D extent);

    /** @brief Call once per frame before Draw (ImGui new frame). */
    void BeginFrame();

    /** @brief Draws HUD panels: player HP bar, target frame (if hasTarget), combat log mini, inventory grid + tooltip (M16.3). Layout responsive. */
    void Draw(const HudData& data);

    /** @brief Call after Draw to finalize ImGui draw data. */
    void EndFrame();

    /** @brief Records ImGui draw commands into the given command buffer (after main scene). */
    void Render(VkCommandBuffer cmdBuffer, uint32_t imageIndex, VkExtent2D extent);

    /** @brief Returns true if ImGui wants to capture mouse. */
    bool WantCaptureMouse() const;

    /** @brief Returns true if Init succeeded. */
    bool IsReady() const noexcept { return m_ready; }

private:
    struct Impl;
    Impl* m_impl = nullptr;
    bool m_ready = false;
};

} // namespace engine::ui
