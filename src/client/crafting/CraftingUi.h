#pragma once

#include "src/client/ui_common/UIModel.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::client
{
	/// Pixel-space region for one row in the recipe list (M36.2).
	struct CraftRecipeRowRegion
	{
		uint32_t rowIndex = 0;
		float x      = 0.0f;
		float y      = 0.0f;
		float width  = 0.0f;
		float height = 0.0f;
	};

	/// Quality tier border colour (normalised RGB) for the crafted-item result (M36.3).
	struct QualityBorderColor
	{
		float r = 1.0f;
		float g = 1.0f;
		float b = 1.0f;
	};

	/// Resolved state for the full crafting panel (M36.2/M36.3).
	struct CraftingPanelState
	{
		/// Panel position and dimensions.
		float panelX      = 0.0f;
		float panelY      = 0.0f;
		float panelWidth  = 0.0f;
		float panelHeight = 0.0f;
		/// Profession tab labels (one per known profession).
		std::vector<std::string> professionTabs;
		/// Index of the active profession tab.
		uint32_t activeProfessionTab = 0;
		/// Recipe rows in the panel's list area.
		std::vector<CraftRecipeRowRegion> recipeRows;
		/// Index of the selected recipe (UINT32_MAX = none).
		uint32_t selectedRow = UINT32_MAX;
		/// Craft button bounds.
		float craftBtnX      = 0.0f;
		float craftBtnY      = 0.0f;
		float craftBtnWidth  = 0.0f;
		float craftBtnHeight = 0.0f;
		/// True when the craft button should be enabled.
		bool craftBtnEnabled = false;
		/// Cast bar fill fraction [0, 1] during a craft (0 = idle).
		float craftFillFraction = 0.0f;
		/// Human-readable status text below the recipe list.
		std::string statusText;
		/// Debug text dump of the current panel state.
		std::string debugText;
		/// M36.3 — quality tier of the last crafted item (0=Normal…3=Epic).
		uint8_t lastQualityTier = 0;
		/// M36.3 — human-readable quality tier label ("Normal", "Uncommon", "Rare", "Epic").
		std::string lastQualityLabel;
		/// M36.3 — border colour matching the quality tier (white/green/blue/purple).
		QualityBorderColor lastQualityColor{};
		bool layoutValid = false;
		bool isVisible   = false;
	};

	/// Builds the crafting panel layout from the UIModel crafting state (M36.2).
	///
	/// Layout:
	///   +— Crafting ——————————————+
	///   | [Blacksmithing] [Alchemy] |   ← profession tabs
	///   |  #1 Iron Sword  (skill 50) |  ← recipe rows
	///   |  #2 Iron Shield (skill 60) |
	///   |  Materials: ...            |
	///   |  [     Craft     ]         |  ← craft button
	///   |  [===cast bar====]         |  ← cast bar (when crafting)
	///   +————————————————————————+
	class CraftingUiPresenter final
	{
	public:
		CraftingUiPresenter() = default;
		~CraftingUiPresenter();

		CraftingUiPresenter(const CraftingUiPresenter&) = delete;
		CraftingUiPresenter& operator=(const CraftingUiPresenter&) = delete;

		/// Initialize the presenter.
		bool Init();

		/// Release resources.
		void Shutdown();

		/// Update viewport-dependent layout bounds.
		bool SetViewportSize(uint32_t width, uint32_t height);

		/// Rebuild the panel from the current UI model state.
		/// @param changeMask  Must include UIModelChangeCrafting for a rebuild to occur.
		bool ApplyModel(const UIModel& model, uint32_t changeMask);

		/// Advance the craft cast bar fill fraction each frame.
		bool Tick(float deltaSeconds);

		/// Return the immutable panel state for rendering.
		const CraftingPanelState& GetState() const { return m_state; }

		/// Hit-test: return recipe row index at (mouseX, mouseY), or -1.
		int HitTestRecipeRow(float mouseX, float mouseY) const;

		/// Hit-test: return true when (mouseX, mouseY) is over the craft button.
		bool HitCraftButton(float mouseX, float mouseY) const;

	private:
		/// Recompute pixel-space panel bounds after a viewport change.
		void RebuildLayout(const UIModel& model);

		/// Rebuild the text debug dump of the crafting state.
		void RebuildDebugText(const UIModel& model);

		CraftingPanelState m_state{};
		uint32_t           m_viewportWidth  = 0;
		uint32_t           m_viewportHeight = 0;
		bool               m_initialized   = false;
	};
}
