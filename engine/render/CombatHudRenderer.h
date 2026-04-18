#pragma once

#include "engine/client/CombatHud.h"
#include "engine/render/AuthUiRenderer.h"

#include <vulkan/vulkan.h>

#include <vector>

namespace engine::render
{
	/// Builds the Vulkan clear-rect layers for the combat HUD overlay from the resolved presenter state.
	std::vector<AuthUiLayer> BuildCombatHudLayers(
		VkExtent2D extent,
		const engine::client::CombatHudState& state,
		const AuthUiTheme& theme);
}
