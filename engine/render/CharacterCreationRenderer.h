#pragma once

#include "engine/client/CharacterCreationUi.h"
#include "engine/render/AuthUiRenderer.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace engine::render
{
	/// Resolved pixel geometry for the three-column character creation layout.
	struct CharacterCreationLayout
	{
		int32_t colY      = 0; ///< Top Y of all three columns (below the step bar).
		int32_t colH      = 0; ///< Height shared by all three columns.
		int32_t raceColX  = 0;
		int32_t raceColW  = 0;
		int32_t portraitX = 0;
		int32_t portraitW = 0;
		int32_t classColX = 0;
		int32_t classColW = 0;
	};

	/// Computes the three-column layout from the viewport extent.
	CharacterCreationLayout BuildCharacterCreationLayout(VkExtent2D extent);

	/// Builds Vulkan clear-rect layers for the character creation screen.
	/// @param raceCount   Total number of race entries (for selection highlight).
	/// @param classCount  Number of filtered class entries (for selection highlight).
	std::vector<AuthUiLayer> BuildCharacterCreationLayers(
		VkExtent2D extent,
		const engine::client::CharacterCreationState& state,
		uint32_t raceCount,
		uint32_t classCount,
		const AuthUiTheme& theme);
}
