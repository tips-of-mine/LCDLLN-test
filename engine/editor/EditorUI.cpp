/**
 * @file EditorUI.cpp
 * @brief Editor UI: ImGui Vulkan/GLFW integration, Scene/Inspector/Asset Browser panels, gizmo (M12.1).
 */

#include "EditorUI.h"

#include "engine/core/Log.h"
#include "engine/math/Ray.h"
#include "engine/world/World.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace engine::editor {

struct EditorUI::Impl {
    VkDevice         device = VK_NULL_HANDLE;
    VkRenderPass     renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkImage>       images;  // swapchain images for barriers
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

EditorUI::~EditorUI() {
    Shutdown();
}

bool EditorUI::Init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                    VkQueue queue, uint32_t queueFamily, GLFWwindow* window,
                    VkFormat swapchainFormat, uint32_t imageCount) {
    if (m_impl) return true;
    m_impl = new Impl();
    m_impl->device = device;

    VkAttachmentDescription att{};
    att.format         = swapchainFormat;
    att.samples        = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
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
        LOG_ERROR(Render, "EditorUI: vkCreateRenderPass failed");
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
            LOG_ERROR(Render, "ImGui Vulkan error: {}", static_cast<int>(err));
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

void EditorUI::Shutdown() {
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

bool EditorUI::RecreateFramebuffers(VkDevice device, const VkImage* images, const VkImageView* views, uint32_t count, VkExtent2D extent) {
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

void EditorUI::BeginFrame() {
    if (!m_ready || !m_impl) return;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

namespace {
constexpr int kMaxLayers = 16;
constexpr float kPi = 3.14159265358979323846f;

/** @brief Snap value to grid step. */
float SnapTranslate(float v, float step) {
    if (step <= 0.f) return v;
    return std::round(v / step) * step;
}

/** @brief Snap angle (degrees) to step (5 or 15). */
float SnapRotationDeg(float deg, float stepDeg) {
    if (stepDeg <= 0.f) return deg;
    return std::round(deg / stepDeg) * stepDeg;
}

/** @brief Extract rotation Y (radians) from column-major 4x4 (Y-up, rotation around Y). */
float GetRotationYRad(const float* m) {
    return std::atan2(m[8], m[0]);
}

/** @brief Set rotation Y in column-major 4x4 (preserve position and scale). */
void SetRotationYRad(float* m, float rad) {
    float c = std::cos(rad), s = std::sin(rad);
    float scale = std::sqrt(m[0]*m[0] + m[1]*m[1] + m[2]*m[2]);
    if (scale < 1e-6f) scale = 1.f;
    m[0] = c * scale; m[1] = 0; m[2] = -s * scale;
    m[4] = 0; m[5] = scale; m[6] = 0;
    m[8] = s * scale; m[9] = 0; m[10] = c * scale;
}
} // namespace

void EditorUI::DrawPanels(std::vector<::engine::world::ZoneChunkInstance>* instances,
                          int* selectedIndex,
                          bool* layerVisible,
                          bool* layerLocked,
                          bool* outDirty,
                          bool* outSaveRequested,
                          const std::string* instancesPath,
                          std::vector<::engine::world::GameplayVolume>* volumes,
                          int* selectedVolumeIndex,
                          const std::string* zoneBasePath,
                          bool* outExportVolumesRequested,
                          bool* outExportLayoutRequested,
                          const float cameraPosition[3],
                          const float cameraViewCol[16], const float cameraProjCol[16],
                          int viewportWidth, int viewportHeight) {
    if (!m_ready || !instances || !selectedIndex) return;
    if (!layerVisible || !layerLocked) return;
    if (!outDirty) outDirty = nullptr;
    if (!outSaveRequested) outSaveRequested = nullptr;
    if (!volumes) volumes = nullptr;
    if (!selectedVolumeIndex) selectedVolumeIndex = nullptr;
    if (!zoneBasePath) zoneBasePath = nullptr;
    if (!outExportVolumesRequested) outExportVolumesRequested = nullptr;
    if (!outExportLayoutRequested) outExportLayoutRequested = nullptr;
    if (!cameraPosition) cameraPosition = nullptr;
    const float vpW = static_cast<float>(viewportWidth > 0 ? viewportWidth : 1280);
    const float vpH = static_cast<float>(viewportHeight > 0 ? viewportHeight : 720);

    static float s_translateStep = 1.f;   // 0.5 or 1 m (M12.2)
    static float s_rotationStepDeg = 15.f; // 5 or 15 deg (M12.2)

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(viewportWidth), static_cast<float>(viewportHeight)), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("Viewport (click to select object or volume)");
        ImGui::End();
    }

    // M12.3: debug draw volume wireframes (lines) over the full viewport.
    if (volumes && !volumes->empty() && cameraViewCol && cameraProjCol) {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(vpW, vpH), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.f));
        const ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav
            | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (ImGui::Begin("##VolumeOverlay", nullptr, overlayFlags)) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImU32 colTrigger = IM_COL32(0, 255, 128, 220);
            const ImU32 colSpawn = IM_COL32(128, 200, 255, 220);
            const ImU32 colTransition = IM_COL32(255, 200, 0, 220);
            for (const auto& vol : *volumes) {
                ImU32 col = colTrigger;
                if (vol.type == ::engine::world::VolumeType::SpawnArea) col = colSpawn;
                else if (vol.type == ::engine::world::VolumeType::ZoneTransition) col = colTransition;
                if (vol.shape == ::engine::world::VolumeShape::Box) {
                    float min[3] = { vol.position[0] - vol.halfExtents[0], vol.position[1] - vol.halfExtents[1], vol.position[2] - vol.halfExtents[2] };
                    float max[3] = { vol.position[0] + vol.halfExtents[0], vol.position[1] + vol.halfExtents[1], vol.position[2] + vol.halfExtents[2] };
                    float corners[8][3] = {
                        { min[0], min[1], min[2] }, { max[0], min[1], min[2] }, { max[0], max[1], min[2] }, { min[0], max[1], min[2] },
                        { min[0], min[1], max[2] }, { max[0], min[1], max[2] }, { max[0], max[1], max[2] }, { min[0], max[1], max[2] }
                    };
                    const int edges[12][2] = { {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7} };
                    for (int e = 0; e < 12; ++e) {
                        int a = edges[e][0], b = edges[e][1];
                        float sa0, sa1, sb0, sb1;
                        if (::engine::math::WorldToScreen(corners[a][0], corners[a][1], corners[a][2], cameraViewCol, cameraProjCol, vpW, vpH, &sa0, &sa1) &&
                            ::engine::math::WorldToScreen(corners[b][0], corners[b][1], corners[b][2], cameraViewCol, cameraProjCol, vpW, vpH, &sb0, &sb1))
                            dl->AddLine(ImVec2(sa0, sa1), ImVec2(sb0, sb1), col, 2.f);
                    }
                } else {
                    const int kSegs = 16;
                    const int ringAxes[3][2] = { {1, 2}, {0, 2}, {0, 1} };  // YZ, XZ, XY
                    for (int ring = 0; ring < 3; ++ring) {
                        int ax0 = ringAxes[ring][0], ax1 = ringAxes[ring][1];
                        for (int i = 0; i < kSegs; ++i) {
                            float t0 = (float)i * (6.28318530718f / (float)kSegs), t1 = (float)(i + 1) * (6.28318530718f / (float)kSegs);
                            float p0[3] = { vol.position[0], vol.position[1], vol.position[2] };
                            float p1[3] = { vol.position[0], vol.position[1], vol.position[2] };
                            p0[ax0] += vol.radius * std::cos(t0); p0[ax1] += vol.radius * std::sin(t0);
                            p1[ax0] += vol.radius * std::cos(t1); p1[ax1] += vol.radius * std::sin(t1);
                            float s0x, s0y, s1x, s1y;
                            if (::engine::math::WorldToScreen(p0[0], p0[1], p0[2], cameraViewCol, cameraProjCol, vpW, vpH, &s0x, &s0y) &&
                                ::engine::math::WorldToScreen(p1[0], p1[1], p1[2], cameraViewCol, cameraProjCol, vpW, vpH, &s1x, &s1y))
                                dl->AddLine(ImVec2(s0x, s0y), ImVec2(s1x, s1y), col, 2.f);
                        }
                    }
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }

    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(viewportWidth) - 320.f, 20.f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 480), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse)) {
        if (ImGui::CollapsingHeader("Snapping", ImGuiTreeNodeFlags_DefaultOpen)) {
            int ts = (s_translateStep < 0.75f) ? 0 : 1;
            if (ImGui::Combo("Grid step", &ts, "0.5 m\01 m\0")) {
                s_translateStep = (ts == 0) ? 0.5f : 1.f;
            }
            int rs = (s_rotationStepDeg < 10.f) ? 0 : 1;
            if (ImGui::Combo("Rotation step", &rs, "5 deg\015 deg\0")) {
                s_rotationStepDeg = (rs == 0) ? 5.f : 15.f;
            }
        }
        if (selectedVolumeIndex && *selectedVolumeIndex >= 0 && volumes && static_cast<size_t>(*selectedVolumeIndex) < volumes->size()) {
            ::engine::world::GameplayVolume& vol = (*volumes)[static_cast<size_t>(*selectedVolumeIndex)];
            int typeIdx = (vol.type == ::engine::world::VolumeType::Trigger) ? 0 : (vol.type == ::engine::world::VolumeType::SpawnArea) ? 1 : 2;
            if (ImGui::Combo("Type", &typeIdx, "Trigger\0Spawn Area\0Zone Transition\0")) {
                vol.type = (typeIdx == 0) ? ::engine::world::VolumeType::Trigger
                    : (typeIdx == 1) ? ::engine::world::VolumeType::SpawnArea : ::engine::world::VolumeType::ZoneTransition;
                if (outDirty) *outDirty = true;
            }
            int shapeIdx = (vol.shape == ::engine::world::VolumeShape::Sphere) ? 1 : 0;
            if (ImGui::Combo("Shape", &shapeIdx, "Box\0Sphere\0")) {
                vol.shape = (shapeIdx == 0) ? ::engine::world::VolumeShape::Box : ::engine::world::VolumeShape::Sphere;
                if (outDirty) *outDirty = true;
            }
            if (ImGui::DragFloat3("Position", vol.position, 0.1f, 0.f, 0.f, "%.3f")) {
                vol.position[0] = SnapTranslate(vol.position[0], s_translateStep);
                vol.position[1] = SnapTranslate(vol.position[1], s_translateStep);
                vol.position[2] = SnapTranslate(vol.position[2], s_translateStep);
                if (outDirty) *outDirty = true;
            }
            if (vol.shape == ::engine::world::VolumeShape::Box) {
                if (ImGui::DragFloat3("Half extents", vol.halfExtents, 0.1f, 0.01f, 1000.f, "%.2f"))
                    if (outDirty) *outDirty = true;
            } else {
                if (ImGui::DragFloat("Radius", &vol.radius, 0.1f, 0.01f, 1000.f, "%.2f"))
                    if (outDirty) *outDirty = true;
            }
            char actionBuf[256];
            std::snprintf(actionBuf, sizeof(actionBuf), "%.255s", vol.actionId.c_str());
            if (ImGui::InputText("Action ID", actionBuf, sizeof(actionBuf))) {
                vol.actionId = actionBuf;
                if (outDirty) *outDirty = true;
            }
        } else if (*selectedIndex >= 0 && static_cast<size_t>(*selectedIndex) < instances->size()) {
            ::engine::world::ZoneChunkInstance& inst = (*instances)[static_cast<size_t>(*selectedIndex)];
            const uint32_t layer = inst.flags & 0x0Fu;
            const bool locked = (layer < kMaxLayers && layerLocked[layer]);

            int layerIdx = static_cast<int>(layer);
            if (ImGui::SliderInt("Layer", &layerIdx, 0, kMaxLayers - 1)) {
                layerIdx = (layerIdx < 0) ? 0 : (layerIdx >= kMaxLayers ? kMaxLayers - 1 : layerIdx);
                inst.flags = (inst.flags & ~0x0Fu) | (static_cast<uint32_t>(layerIdx) & 0x0Fu);
                if (outDirty) *outDirty = true;
            }

            float* t = inst.transform;
            float pos[3] = { t[12], t[13], t[14] };
            if (ImGui::DragFloat3("Position", pos, 0.1f, 0.f, 0.f, "%.3f", locked ? ImGuiSliderFlags_ReadOnly : 0)) {
                if (!locked) {
                    t[12] = SnapTranslate(pos[0], s_translateStep);
                    t[13] = SnapTranslate(pos[1], s_translateStep);
                    t[14] = SnapTranslate(pos[2], s_translateStep);
                    if (outDirty) *outDirty = true;
                }
            }
            float rotYRad = GetRotationYRad(t);
            float rotYDeg = rotYRad * 180.f / kPi;
            if (ImGui::DragFloat("Rotation Y (deg)", &rotYDeg, 1.f, -180.f, 180.f, "%.1f", locked ? ImGuiSliderFlags_ReadOnly : 0)) {
                if (!locked) {
                    rotYDeg = SnapRotationDeg(rotYDeg, s_rotationStepDeg);
                    SetRotationYRad(t, rotYDeg * kPi / 180.f);
                    if (outDirty) *outDirty = true;
                }
            }
            if (ImGui::Button("Align to ground")) {
                if (!locked) {
                    t[13] = 0.f;
                    if (outDirty) *outDirty = true;
                }
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(Y=0)");
            ImGui::Text("AssetId: %u", inst.assetId);
        } else {
            ImGui::Text("No selection");
        }
        ImGui::End();
    }

    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(220, 320), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Layers", nullptr, ImGuiWindowFlags_NoCollapse)) {
        for (int i = 0; i < kMaxLayers; ++i) {
            ImGui::PushID(i);
            bool vis = layerVisible[i];
            bool lock = layerLocked[i];
            if (ImGui::Checkbox("##vis", &vis)) { layerVisible[i] = vis; }
            ImGui::SameLine();
            if (ImGui::Checkbox("##lock", &lock)) { layerLocked[i] = lock; }
            ImGui::SameLine();
            ImGui::Text("Layer %d", i);
            ImGui::PopID();
        }
        ImGui::End();
    }

    ImGui::SetNextWindowPos(ImVec2(20, 350), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(220, 80), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Save", nullptr, ImGuiWindowFlags_NoCollapse)) {
        bool dirty = outDirty && *outDirty;
        if (ImGui::Button("Save", ImVec2(120, 0)) && outSaveRequested) {
            *outSaveRequested = true;
        }
        if (dirty) { ImGui::SameLine(); ImGui::TextUnformatted("(dirty)"); }
        ImGui::End();
    }

    ImGui::SetNextWindowPos(ImVec2(250, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Asset Browser", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("Asset Browser (MVP)");
        ImGui::Text("Placeholder");
        ImGui::End();
    }

    // M12.3: Volumes panel — list, add trigger/spawn/transition, export.
    if (volumes && selectedVolumeIndex) {
        ImGui::SetNextWindowPos(ImVec2(20, 440), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(260, 280), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Volumes", nullptr, ImGuiWindowFlags_NoCollapse)) {
            const char* typeNames[] = { "Trigger", "Spawn", "Transition" };
            for (size_t i = 0; i < volumes->size(); ++i) {
                const auto& v = (*volumes)[i];
                const char* name = (v.type == ::engine::world::VolumeType::Trigger) ? typeNames[0]
                    : (v.type == ::engine::world::VolumeType::SpawnArea) ? typeNames[1] : typeNames[2];
                if (ImGui::Selectable((std::string(name) + " ##" + std::to_string(i)).c_str(), *selectedVolumeIndex == static_cast<int>(i)))
                    *selectedVolumeIndex = static_cast<int>(i);
            }
            if (ImGui::Button("Add Trigger")) {
                ::engine::world::GameplayVolume vol;
                vol.type = ::engine::world::VolumeType::Trigger;
                vol.shape = ::engine::world::VolumeShape::Box;
                vol.position[0] = vol.position[1] = vol.position[2] = 0.f;
                vol.halfExtents[0] = vol.halfExtents[1] = vol.halfExtents[2] = 2.f;
                vol.radius = 2.f;
                volumes->push_back(vol);
                *selectedVolumeIndex = static_cast<int>(volumes->size() - 1);
                if (outDirty) *outDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Spawn")) {
                ::engine::world::GameplayVolume vol;
                vol.type = ::engine::world::VolumeType::SpawnArea;
                vol.shape = ::engine::world::VolumeShape::Box;
                vol.position[0] = vol.position[1] = vol.position[2] = 0.f;
                vol.halfExtents[0] = vol.halfExtents[1] = vol.halfExtents[2] = 2.f;
                vol.radius = 2.f;
                volumes->push_back(vol);
                *selectedVolumeIndex = static_cast<int>(volumes->size() - 1);
                if (outDirty) *outDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Transition")) {
                ::engine::world::GameplayVolume vol;
                vol.type = ::engine::world::VolumeType::ZoneTransition;
                vol.shape = ::engine::world::VolumeShape::Box;
                vol.position[0] = vol.position[1] = vol.position[2] = 0.f;
                vol.halfExtents[0] = vol.halfExtents[1] = vol.halfExtents[2] = 2.f;
                vol.radius = 2.f;
                volumes->push_back(vol);
                *selectedVolumeIndex = static_cast<int>(volumes->size() - 1);
                if (outDirty) *outDirty = true;
            }
            if (zoneBasePath && !zoneBasePath->empty() && outExportVolumesRequested && ImGui::Button("Export volumes")) {
                *outExportVolumesRequested = true;
            }
            if (zoneBasePath && !zoneBasePath->empty() && outExportLayoutRequested && ImGui::Button("Export layout")) {
                *outExportLayoutRequested = true;
            }
            ImGui::End();
        }
    }

    // M12.4: Chunk overlay debug (toggle) — grid 256m chunks + 64m cells + ring highlight.
    static bool s_chunkOverlayVisible = false;
    if (cameraPosition && cameraViewCol && cameraProjCol) {
        ImGui::SetNextWindowPos(ImVec2(20, 730), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Debug overlay", nullptr, ImGuiWindowFlags_NoCollapse)) {
            if (ImGui::Checkbox("Chunk grid (256m + 64m cells)", &s_chunkOverlayVisible)) {}
            ImGui::End();
        }
        if (s_chunkOverlayVisible) {
            constexpr float kChunkSizeM = 256.f;
            constexpr float kCellSizeM = 64.f;
            constexpr int kRangeChunks = 10;
            const float camX = cameraPosition[0], camZ = cameraPosition[2];
            const float minX = camX - kRangeChunks * kChunkSizeM, maxX = camX + kRangeChunks * kChunkSizeM;
            const float minZ = camZ - kRangeChunks * kChunkSizeM, maxZ = camZ + kRangeChunks * kChunkSizeM;
            const float y = 0.f;
            ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(vpW, vpH), ImGuiCond_Always);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.f));
            if (ImGui::Begin("##ChunkOverlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus)) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImU32 colChunk = IM_COL32(100, 100, 100, 180);
                const ImU32 colCell = IM_COL32(60, 60, 60, 120);
                const ImU32 colRing = IM_COL32(255, 200, 0, 220);
                for (float x = std::floor(minX / kChunkSizeM) * kChunkSizeM; x <= maxX + 1.f; x += kChunkSizeM) {
                    float sx0, sy0, sx1, sy1;
                    if (::engine::math::WorldToScreen(x, y, minZ, cameraViewCol, cameraProjCol, vpW, vpH, &sx0, &sy0) &&
                        ::engine::math::WorldToScreen(x, y, maxZ, cameraViewCol, cameraProjCol, vpW, vpH, &sx1, &sy1))
                        dl->AddLine(ImVec2(sx0, sy0), ImVec2(sx1, sy1), colChunk, 1.5f);
                }
                for (float z = std::floor(minZ / kChunkSizeM) * kChunkSizeM; z <= maxZ + 1.f; z += kChunkSizeM) {
                    float sx0, sy0, sx1, sy1;
                    if (::engine::math::WorldToScreen(minX, y, z, cameraViewCol, cameraProjCol, vpW, vpH, &sx0, &sy0) &&
                        ::engine::math::WorldToScreen(maxX, y, z, cameraViewCol, cameraProjCol, vpW, vpH, &sx1, &sy1))
                        dl->AddLine(ImVec2(sx0, sy0), ImVec2(sx1, sy1), colChunk, 1.5f);
                }
                for (float x = std::floor(minX / kCellSizeM) * kCellSizeM; x <= maxX + 1.f; x += kCellSizeM) {
                    float rem = std::fmod(std::fabs(x) + 0.001f, kChunkSizeM);
                    if (rem < 0.002f || rem > kChunkSizeM - 0.002f) continue;
                    float sx0, sy0, sx1, sy1;
                    if (::engine::math::WorldToScreen(x, y, minZ, cameraViewCol, cameraProjCol, vpW, vpH, &sx0, &sy0) &&
                        ::engine::math::WorldToScreen(x, y, maxZ, cameraViewCol, cameraProjCol, vpW, vpH, &sx1, &sy1))
                        dl->AddLine(ImVec2(sx0, sy0), ImVec2(sx1, sy1), colCell, 1.f);
                }
                for (float z = std::floor(minZ / kCellSizeM) * kCellSizeM; z <= maxZ + 1.f; z += kCellSizeM) {
                    float rem = std::fmod(std::fabs(z) + 0.001f, kChunkSizeM);
                    if (rem < 0.002f || rem > kChunkSizeM - 0.002f) continue;
                    float sx0, sy0, sx1, sy1;
                    if (::engine::math::WorldToScreen(minX, y, z, cameraViewCol, cameraProjCol, vpW, vpH, &sx0, &sy0) &&
                        ::engine::math::WorldToScreen(maxX, y, z, cameraViewCol, cameraProjCol, vpW, vpH, &sx1, &sy1))
                        dl->AddLine(ImVec2(sx0, sy0), ImVec2(sx1, sy1), colCell, 1.f);
                }
                ::engine::world::ChunkCoord center = ::engine::world::WorldToChunkCoord(camX, camZ);
                std::vector<::engine::world::ChunkCoord> ringChunks;
                ::engine::world::GetChunksForRing(center, ::engine::world::RingType::Active, ringChunks);
                for (const auto& c : ringChunks) {
                    ::engine::world::ChunkBoundsResult b = ::engine::world::ChunkBounds(c);
                    float sx0, sy0, sx1, sy1;
                    if (::engine::math::WorldToScreen(b.minX, y, b.minZ, cameraViewCol, cameraProjCol, vpW, vpH, &sx0, &sy0) &&
                        ::engine::math::WorldToScreen(b.maxX, y, b.minZ, cameraViewCol, cameraProjCol, vpW, vpH, &sx1, &sy1))
                        dl->AddLine(ImVec2(sx0, sy0), ImVec2(sx1, sy1), colRing, 2.5f);
                    if (::engine::math::WorldToScreen(b.maxX, y, b.minZ, cameraViewCol, cameraProjCol, vpW, vpH, &sx0, &sy0) &&
                        ::engine::math::WorldToScreen(b.maxX, y, b.maxZ, cameraViewCol, cameraProjCol, vpW, vpH, &sx1, &sy1))
                        dl->AddLine(ImVec2(sx0, sy0), ImVec2(sx1, sy1), colRing, 2.5f);
                    if (::engine::math::WorldToScreen(b.maxX, y, b.maxZ, cameraViewCol, cameraProjCol, vpW, vpH, &sx0, &sy0) &&
                        ::engine::math::WorldToScreen(b.minX, y, b.maxZ, cameraViewCol, cameraProjCol, vpW, vpH, &sx1, &sy1))
                        dl->AddLine(ImVec2(sx0, sy0), ImVec2(sx1, sy1), colRing, 2.5f);
                    if (::engine::math::WorldToScreen(b.minX, y, b.maxZ, cameraViewCol, cameraProjCol, vpW, vpH, &sx0, &sy0) &&
                        ::engine::math::WorldToScreen(b.minX, y, b.minZ, cameraViewCol, cameraProjCol, vpW, vpH, &sx1, &sy1))
                        dl->AddLine(ImVec2(sx0, sy0), ImVec2(sx1, sy1), colRing, 2.5f);
                }
            }
            ImGui::End();
            ImGui::PopStyleColor();
        }
    }
}

void EditorUI::EndFrame() {
    if (!m_ready) return;
    ImGui::Render();
}

void EditorUI::Render(VkCommandBuffer cmdBuffer, uint32_t imageIndex, VkExtent2D extent) {
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
    toPresent.subresourceRange   = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toPresent.srcAccessMask      = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toPresent.dstAccessMask      = 0;
    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPresent);
}

bool EditorUI::WantCaptureMouse() const {
    return m_ready && ImGui::GetIO().WantCaptureMouse;
}

} // namespace engine::editor
