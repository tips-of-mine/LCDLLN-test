#include "engine/render/SettingsMenuRenderer.h"

#include <algorithm>
#include <cstdint>

namespace engine::render
{
	namespace
	{
		constexpr int32_t kPanelW   = 720;
		constexpr int32_t kPanelH   = 520;
		constexpr int32_t kSidebarW = 160; ///< Width of the left tab sidebar.
		constexpr int32_t kPad      =  10; ///< Inner content padding.
		constexpr int32_t kTabH     =  44; ///< Height of each sidebar tab button.
		constexpr int32_t kTabGap   =   2; ///< Vertical gap between tab buttons.
		constexpr int32_t kTabCount =   4; ///< Graphics / Audio / Controls / Gameplay.
		constexpr int32_t kHeaderH  =  32; ///< Reserved height at top of sidebar for a title.
		constexpr int32_t kFooterH  =  56; ///< Reserved height at bottom for Apply / Cancel buttons.
	}

	std::vector<AuthUiLayer> BuildSettingsMenuLayers(
		VkExtent2D extent,
		engine::client::SettingsTab activeTab,
		bool isOpen,
		const AuthUiTheme& theme)
	{
		std::vector<AuthUiLayer> layers;
		if (!isOpen || extent.width == 0 || extent.height == 0)
			return layers;

		const int32_t w = static_cast<int32_t>(extent.width);
		const int32_t h = static_cast<int32_t>(extent.height);

		auto addRect = [&layers](int32_t x, int32_t y, int32_t rw, int32_t rh,
			float r, float g, float b, float a)
		{
			if (rw <= 0 || rh <= 0) return;
			AuthUiLayer layer{};
			layer.color.float32[0] = r;
			layer.color.float32[1] = g;
			layer.color.float32[2] = b;
			layer.color.float32[3] = a;
			layer.rect.rect.offset = { x, y };
			layer.rect.rect.extent = { static_cast<uint32_t>(rw), static_cast<uint32_t>(rh) };
			layer.rect.baseArrayLayer = 0;
			layer.rect.layerCount = 1;
			layers.push_back(layer);
		};

		auto addThemeRect = [&addRect](int32_t x, int32_t y, int32_t rw, int32_t rh,
			const float color[4], float alphaScale = 1.0f)
		{
			addRect(x, y, rw, rh, color[0], color[1], color[2], color[3] * alphaScale);
		};

		// Dark modal veil
		addRect(0, 0, w, h, 0.f, 0.f, 0.f, 0.55f);

		// Outer panel
		const int32_t panelX = (w - kPanelW) / 2;
		const int32_t panelY = (h - kPanelH) / 2;
		addThemeRect(panelX - 2, panelY - 2, kPanelW + 4, kPanelH + 4, theme.border, 0.85f);
		addThemeRect(panelX, panelY, kPanelW, kPanelH, theme.panel, 0.97f);
		addThemeRect(panelX, panelY, kPanelW, 3, theme.accent, 1.0f);

		// Left sidebar background + right divider
		addThemeRect(panelX, panelY, kSidebarW, kPanelH, theme.surface, 0.90f);
		addThemeRect(panelX + kSidebarW, panelY, 1, kPanelH, theme.border, 0.65f);

		// Sidebar tab buttons
		const int32_t tabX       = panelX;
		const int32_t tabsStartY = panelY + kHeaderH;
		for (int32_t ti = 0; ti < kTabCount; ++ti)
		{
			const int32_t tabY  = tabsStartY + ti * (kTabH + kTabGap);
			const bool    active = (static_cast<int32_t>(activeTab) == ti);
			if (active)
			{
				addThemeRect(tabX, tabY, kSidebarW, kTabH, theme.primary, 0.85f);
				addThemeRect(tabX + kSidebarW - 3, tabY, 3, kTabH, theme.accent, 1.0f);
			}
			else
			{
				addThemeRect(tabX, tabY + kTabH - 1, kSidebarW, 1, theme.border, 0.35f);
			}
		}

		// Content area geometry
		const int32_t contentX = panelX + kSidebarW + 1;
		const int32_t contentY = panelY + 3;
		const int32_t contentW = kPanelW - kSidebarW - 1;
		const int32_t rowsStartY = contentY + kHeaderH + 10;

		// Section title underline
		addThemeRect(contentX + kPad, contentY + kHeaderH + 4, contentW - kPad * 2, 1, theme.border, 0.55f);

		// Per-tab content rows
		if (activeTab == engine::client::SettingsTab::Graphics)
		{
			// 5 rows: resolution, quality, vsync, fullscreen, FOV
			constexpr int32_t kRowH   = 32;
			constexpr int32_t kRowGap =  8;
			int32_t rowY = rowsStartY;
			for (int32_t ri = 0; ri < 5; ++ri)
			{
				addThemeRect(contentX + kPad, rowY, contentW - kPad * 2, kRowH, theme.surface, 0.45f);
				addThemeRect(contentX + kPad, rowY + kRowH - 1, contentW - kPad * 2, 1, theme.border, 0.30f);
				rowY += kRowH + kRowGap;
			}
		}
		else if (activeTab == engine::client::SettingsTab::Audio)
		{
			// 4 volume sliders: master / music / sfx / voice
			constexpr int32_t kSliderH  =  6;
			constexpr int32_t kRowH     = 40;
			constexpr int32_t kRowGap   = 12;
			int32_t rowY = rowsStartY;
			for (int32_t si = 0; si < 4; ++si)
			{
				const int32_t sliderY = rowY + kRowH - kSliderH;
				const int32_t sliderW = contentW - kPad * 2;
				addThemeRect(contentX + kPad, sliderY, sliderW, kSliderH, theme.surface, 0.72f);
				// Placeholder fill at three-quarters; glyph pass overlays actual value text
				addThemeRect(contentX + kPad, sliderY, sliderW * 3 / 4, kSliderH, theme.primary, 0.72f);
				rowY += kRowH + kRowGap;
			}
		}
		else if (activeTab == engine::client::SettingsTab::Controls)
		{
			// 8 keybinding rows, alternating surface shades
			constexpr int32_t kRowH   = 28;
			constexpr int32_t kRowGap =  4;
			int32_t rowY = rowsStartY;
			for (int32_t ri = 0; ri < 8; ++ri)
			{
				const float a = (ri % 2 == 0) ? 0.35f : 0.22f;
				addThemeRect(contentX + kPad, rowY, contentW - kPad * 2, kRowH, theme.surface, a);
				rowY += kRowH + kRowGap;
			}
		}
		else // SettingsTab::Gameplay
		{
			// 4 toggle rows: auto-loot, combat text, timestamps, camera distance
			constexpr int32_t kRowH   = 36;
			constexpr int32_t kRowGap =  8;
			int32_t rowY = rowsStartY;
			for (int32_t ri = 0; ri < 4; ++ri)
			{
				addThemeRect(contentX + kPad, rowY, contentW - kPad * 2, kRowH, theme.surface, 0.35f);
				addThemeRect(contentX + kPad, rowY + kRowH - 1, contentW - kPad * 2, 1, theme.border, 0.30f);
				rowY += kRowH + kRowGap;
			}
		}

		// Footer: Apply (primary) + Cancel (surface) buttons
		{
			const int32_t footerY = panelY + kPanelH - kFooterH;
			addThemeRect(contentX, footerY, contentW, 1, theme.border, 0.40f); // separator line

			constexpr int32_t kBtnW = 120;
			constexpr int32_t kBtnH =  36;
			const int32_t btnY = footerY + (kFooterH - kBtnH) / 2;
			// Apply
			addThemeRect(contentX + kPad, btnY, kBtnW, kBtnH, theme.primary, 0.90f);
			addThemeRect(contentX + kPad, btnY + kBtnH - 2, kBtnW, 2, theme.accent, 0.85f);
			// Cancel
			addThemeRect(contentX + kPad + kBtnW + 12, btnY, kBtnW, kBtnH, theme.surface, 0.80f);
			addThemeRect(contentX + kPad + kBtnW + 12, btnY + kBtnH - 2, kBtnW, 2, theme.border, 0.55f);
		}

		return layers;
	}

} // namespace engine::render
