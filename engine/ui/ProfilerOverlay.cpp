/**
 * @file ProfilerOverlay.cpp
 * @brief Debug overlay: CPU frame ms + GPU pass breakdown (M18.1).
 */

#include "engine/ui/ProfilerOverlay.h"

#include <imgui.h>

namespace engine::ui {

void DrawProfilerOverlay(float cpuFrameMs,
                         const std::vector<std::pair<std::string, float>>& gpuPassMs) {
    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280.f, 320.f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Profiler", nullptr, ImGuiWindowFlags_None)) {
        ImGui::End();
        return;
    }
    ImGui::Text("CPU frame: %.2f ms", cpuFrameMs);
    float gpuTotalMs = 0.f;
    for (const auto& p : gpuPassMs) {
        gpuTotalMs += p.second;
    }
    ImGui::Text("GPU total: %.2f ms", gpuTotalMs);
    ImGui::Separator();
    ImGui::Text("GPU passes:");
    ImGui::Indent();
    for (const auto& p : gpuPassMs) {
        ImGui::Text("%s: %.2f ms", p.first.c_str(), p.second);
    }
    ImGui::Unindent();
    ImGui::End();
}

} // namespace engine::ui
