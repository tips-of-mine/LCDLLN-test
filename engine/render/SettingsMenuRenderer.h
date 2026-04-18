#pragma once

#include "engine/client/SettingsMenuUi.h"
#include "engine/render/AuthUiRenderer.h"

#include <vulkan/vulkan.h>

#include <vector>

namespace engine::render
{
	/// Builds Vulkan clear-rect layers for the in-game settings menu overlay.
	/// Returns an empty vector when \p isOpen is false.
	std::vector<AuthUiLayer> BuildSettingsMenuLayers(
		VkExtent2D extent,
		engine::client::SettingsTab activeTab,
		bool isOpen,
		const AuthUiTheme& theme);
}
