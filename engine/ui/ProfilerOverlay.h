#pragma once

/**
 * @file ProfilerOverlay.h
 * @brief Debug overlay: CPU frame ms + GPU pass breakdown (M18.1).
 *
 * Draws an ImGui window with total frame time and per-pass GPU times.
 * Call Draw() during the same ImGui frame as GameHud or EditorUI.
 */

#include <string>
#include <utility>
#include <vector>

namespace engine::ui {

/**
 * @brief Renders the profiler HUD: total ms and top passes breakdown.
 *
 * @param cpuFrameMs   Last frame CPU time in milliseconds.
 * @param gpuPassMs    Pairs of (pass name, pass duration in ms) for each frame-graph pass.
 */
void DrawProfilerOverlay(float cpuFrameMs,
                         const std::vector<std::pair<std::string, float>>& gpuPassMs);

} // namespace engine::ui
