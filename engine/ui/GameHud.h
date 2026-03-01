#pragma once

/**
 * @file GameHud.h
 * @brief Game HUD: player/target/combat (M16.2), inventory (M16.3), quest journal/tracker + minimap (M16.4).
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

/** @brief One quest for journal/tracker: title, progress, current step label (from game/quest defs). */
struct QuestDisplayEntry {
    uint32_t questId = 0u;
    std::string title;
    uint32_t stepIndex = 0u;
    uint32_t counter = 0u;
    bool completed = false;
    /** @brief Current step text (e.g. "Kill 5 wolves (3/5)"). */
    std::string currentStepLabel;
};

/** @brief Data fed to the HUD each frame (player/target; combat log; inventory; quests; minimap). */
struct HudData {
    uint32_t playerHp = 100u;
    uint32_t playerMaxHp = 100u;
    bool hasTarget = false;
    uint32_t targetEntityId = 0u;
    uint32_t targetHp = 0u;
    uint32_t targetMaxHp = 100u;
    /** @brief Last N combat log lines. */
    std::vector<std::string> combatLogLines;
    /** @brief Inventory slots for grid. */
    std::vector<InventorySlot> inventorySlots;
    /** @brief Optional itemId -> label for tooltip. */
    std::unordered_map<uint32_t, std::string> itemLabels;
    /** @brief Quests for journal panel (title, step, counter, completed, currentStepLabel). M16.4. */
    std::vector<QuestDisplayEntry> questEntries;
    /** @brief Quest ids to show in tracker HUD (e.g. first 3). M16.4. */
    std::vector<uint32_t> trackedQuestIds;
    /** @brief Player position XZ for minimap (world space). M16.4. */
    float playerPositionXZ[2] = {0.f, 0.f};
    /** @brief Zone size in world units (e.g. 4096 from M09.1) for xz->uv. M16.4. */
    float zoneSize = 4096.f;
    /** @brief Target position XZ (if hasTarget). M16.4. */
    float targetPositionXZ[2] = {0.f, 0.f};
    /** @brief Optional POI/trigger positions XZ for minimap. M16.4. */
    std::vector<std::pair<float, float>> poiPositions;
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

    /** @brief Draws HUD: player/target/combat log, inventory, quest journal + tracker, minimap (M16.2–M16.4). */
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
