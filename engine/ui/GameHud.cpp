/**
 * @file GameHud.cpp
 * @brief Game HUD: ImGui overlay for player HP, target frame, combat log (M16.2).
 */

#include "engine/ui/GameHud.h"
#include "engine/core/Log.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <cstring>

namespace engine::ui {

struct GameHud::Impl {
    VkDevice         device = VK_NULL_HANDLE;
    VkRenderPass     renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkImage>       images;
    VkExtent2D       extent{0, 0};

    void DestroyFramebuffers() {
        for (VkFramebuffer fb : framebuffers) {
            if (fb != VK_NULL_HANDLE)
                vkDestroyFramebuffer(device, fb, nullptr);
        }
        framebuffers.clear();
        images.clear();
    }
};

GameHud::~GameHud() {
    Shutdown();
}

bool GameHud::Init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                   VkQueue queue, uint32_t queueFamily, GLFWwindow* window,
                   VkFormat swapchainFormat, uint32_t imageCount) {
    if (m_impl) return true;
    m_impl = new Impl();
    m_impl->device = device;

    VkAttachmentDescription att{};
    att.format         = swapchainFormat;
    att.samples        = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    att.storeOp         = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    VkRenderPassCreateInfo rpci{};
    rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments    = &att;
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    if (vkCreateRenderPass(device, &rpci, nullptr, &m_impl->renderPass) != VK_SUCCESS) {
        LOG_ERROR(Render, "GameHud: vkCreateRenderPass failed");
        delete m_impl;
        m_impl = nullptr;
        return false;
    }

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplGlfw_InitForVulkan(window);

    ImGui_ImplVulkan_InitInfo vii{};
    vii.ApiVersion     = VK_API_VERSION_1_0;
    vii.Instance       = instance;
    vii.PhysicalDevice = physicalDevice;
    vii.Device         = device;
    vii.QueueFamily    = queueFamily;
    vii.Queue          = queue;
    vii.DescriptorPool = VK_NULL_HANDLE;
    vii.DescriptorPoolSize = 1000;
    vii.MinImageCount  = 2;
    vii.ImageCount     = imageCount;
    vii.PipelineInfoMain.RenderPass  = m_impl->renderPass;
    vii.PipelineInfoMain.Subpass     = 0;
    vii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    vii.UseDynamicRendering = false;
    vii.CheckVkResultFn = [](VkResult err) {
        if (err != VK_SUCCESS)
            LOG_ERROR(Render, "GameHud ImGui Vulkan error: {}", static_cast<int>(err));
    };
    if (!ImGui_ImplVulkan_Init(&vii)) {
        vkDestroyRenderPass(device, m_impl->renderPass, nullptr);
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        delete m_impl;
        m_impl = nullptr;
        return false;
    }

    m_ready = true;
    return true;
}

void GameHud::Shutdown() {
    if (!m_impl) return;
    if (m_ready) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        m_ready = false;
    }
    m_impl->DestroyFramebuffers();
    if (m_impl->renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_impl->device, m_impl->renderPass, nullptr);
        m_impl->renderPass = VK_NULL_HANDLE;
    }
    delete m_impl;
    m_impl = nullptr;
}

bool GameHud::RecreateFramebuffers(VkDevice device, const VkImage* images, const VkImageView* views, uint32_t count, VkExtent2D extent) {
    if (!m_impl || !m_impl->renderPass) return false;
    m_impl->DestroyFramebuffers();
    m_impl->extent = extent;
    m_impl->framebuffers.resize(count);
    m_impl->images.assign(images, images + count);
    for (uint32_t i = 0; i < count; ++i) {
        VkFramebufferCreateInfo fbci{};
        fbci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass      = m_impl->renderPass;
        fbci.attachmentCount = 1;
        fbci.pAttachments    = &views[i];
        fbci.width           = extent.width;
        fbci.height          = extent.height;
        fbci.layers          = 1;
        if (vkCreateFramebuffer(device, &fbci, nullptr, &m_impl->framebuffers[i]) != VK_SUCCESS) {
            m_impl->DestroyFramebuffers();
            return false;
        }
    }
    return true;
}

void GameHud::BeginFrame() {
    if (!m_ready || !m_impl) return;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void GameHud::Draw(const HudData& data) {
    if (!m_ready) return;
    const float displayW = ImGui::GetIO().DisplaySize.x;
    const float displayH = ImGui::GetIO().DisplaySize.y;
    const float pad = 20.f;
    const float barW = std::min(220.f, displayW * 0.25f);
    const float barH = 22.f;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove;

    float playerPct = (data.playerMaxHp > 0u) ? (static_cast<float>(data.playerHp) / static_cast<float>(data.playerMaxHp)) : 0.f;
    ImGui::SetNextWindowPos(ImVec2(pad, displayH - pad - barH - 10.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(barW + 30.f, barH + 30.f), ImGuiCond_Always);
    if (ImGui::Begin("##PlayerBar", nullptr, flags)) {
        ImGui::Text("HP");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.6f, 0.2f, 1.f));
        ImGui::ProgressBar(playerPct, ImVec2(barW, barH), "");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::Text("%u / %u", data.playerHp, data.playerMaxHp);
        ImGui::End();
    }

    if (data.hasTarget) {
        float targetPct = (data.targetMaxHp > 0u) ? (static_cast<float>(data.targetHp) / static_cast<float>(data.targetMaxHp)) : 0.f;
        ImGui::SetNextWindowPos(ImVec2(displayW - pad - barW - 30.f, pad), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(barW + 30.f, barH + 30.f), ImGuiCond_Always);
        if (ImGui::Begin("##TargetFrame", nullptr, flags)) {
            ImGui::Text("Target");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.7f, 0.2f, 0.2f, 1.f));
            ImGui::ProgressBar(targetPct, ImVec2(barW, barH), "");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::Text("%u / %u", data.targetHp, data.targetMaxHp);
            ImGui::End();
        }
    }

    if (!data.combatLogLines.empty()) {
        const size_t maxLines = 8u;
        size_t start = data.combatLogLines.size() > maxLines ? data.combatLogLines.size() - maxLines : 0u;
        float logW = std::min(280.f, displayW * 0.3f);
        float logH = (static_cast<float>(std::min(data.combatLogLines.size(), maxLines)) * 18.f) + 20.f;
        ImGui::SetNextWindowPos(ImVec2(pad, displayH - pad - barH - 20.f - logH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(logW, logH), ImGuiCond_Always);
        if (ImGui::Begin("##CombatLog", nullptr, flags)) {
            for (size_t i = start; i < data.combatLogLines.size(); ++i)
                ImGui::TextUnformatted(data.combatLogLines[i].c_str());
            ImGui::End();
        }
    }
}

void GameHud::EndFrame() {
    if (!m_ready) return;
    ImGui::Render();
}

void GameHud::Render(VkCommandBuffer cmdBuffer, uint32_t imageIndex, VkExtent2D extent) {
    if (!m_ready || !m_impl || imageIndex >= m_impl->framebuffers.size()) return;
    if (m_impl->images.empty() || imageIndex >= m_impl->images.size()) return;

    VkImage img = m_impl->images[imageIndex];
    VkImageMemoryBarrier toColor{};
    toColor.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toColor.oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toColor.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toColor.image               = img;
    toColor.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toColor.srcAccessMask       = 0;
    toColor.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &toColor);

    VkRenderPassBeginInfo rpbi{};
    rpbi.sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass  = m_impl->renderPass;
    rpbi.framebuffer = m_impl->framebuffers[imageIndex];
    rpbi.renderArea  = {{0, 0}, {extent.width, extent.height}};
    rpbi.clearValueCount = 0;
    vkCmdBeginRenderPass(cmdBuffer, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuffer);
    vkCmdEndRenderPass(cmdBuffer);

    VkImageMemoryBarrier toPresent{};
    toPresent.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toPresent.oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toPresent.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image               = img;
    toPresent.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toPresent.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toPresent.dstAccessMask       = 0;
    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPresent);
}

bool GameHud::WantCaptureMouse() const {
    if (!m_ready) return false;
    return ImGui::GetIO().WantCaptureMouse;
}

} // namespace engine::ui
