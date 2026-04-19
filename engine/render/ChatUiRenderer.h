#pragma once

#include "engine/client/ChatUi.h"
#include "engine/render/AuthUiRenderer.h"

#include <vulkan/vulkan.h>

#include <vector>

namespace engine::render
{
	/// Builds Vulkan clear-rect layers for the bottom-left chat panel (HudOverlay design spec).
	/// Returns an empty vector when \p state.layoutValid is false.
	std::vector<AuthUiLayer> BuildChatUiLayers(
		VkExtent2D extent,
		const engine::client::ChatPanelState& state,
		const AuthUiTheme& theme);
}
