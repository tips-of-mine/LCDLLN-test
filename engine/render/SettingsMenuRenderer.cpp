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
		constexpr int32_t kTabCount =   8; ///< Graphics / Audio / Controls / Gameplay / Language / Interface / Network / Account.
		constexpr int32_t kHeaderH  =  32; ///< Reserved height at top of sidebar for a title.
		constexpr int32_t kFooterH  =  56; ///< Reserved height at bottom for Apply / Cancel buttons.
	}

	std::vector<AuthUiLayer> BuildSettingsMenuLayers(
		VkExtent2D extent,
		engine::client::SettingsTab activeTab,
		bool isOpen,
		bool hasUnsavedChanges,
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
		using Tab = engine::client::SettingsTab;
		if (activeTab == Tab::Graphics)
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
		else if (activeTab == Tab::Audio)
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
				addThemeRect(contentX + kPad, sliderY, sliderW * 3 / 4, kSliderH, theme.primary, 0.72f);
				rowY += kRowH + kRowGap;
			}
		}
		else if (activeTab == Tab::Controls)
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
		else if (activeTab == Tab::Gameplay)
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
		else if (activeTab == Tab::Language)
		{
			// 1 locale dropdown row
			constexpr int32_t kRowH = 36;
			addThemeRect(contentX + kPad, rowsStartY, contentW - kPad * 2, kRowH, theme.surface, 0.45f);
			addThemeRect(contentX + kPad, rowsStartY + kRowH - 1, contentW - kPad * 2, 1, theme.border, 0.30f);
			// Restart-required notice strip
			constexpr float kNoticeColor[4]{ 0.85f, 0.64f, 0.25f, 1.f };
			addRect(contentX + kPad, rowsStartY + kRowH + 12,
				contentW - kPad * 2, 28,
				kNoticeColor[0], kNoticeColor[1], kNoticeColor[2], 0.12f);
			addRect(contentX + kPad, rowsStartY + kRowH + 12,
				3, 28,
				kNoticeColor[0], kNoticeColor[1], kNoticeColor[2], 0.80f);
		}
		else if (activeTab == Tab::Interface)
		{
			// 3 rows: uiScalePct (slider), panelOpacityPct (slider), showTooltips (toggle)
			constexpr int32_t kSliderH =  6;
			constexpr int32_t kRowH    = 40;
			constexpr int32_t kRowGap  = 12;
			int32_t rowY = rowsStartY;
			const int32_t sliderW = contentW - kPad * 2;
			for (int32_t si = 0; si < 2; ++si)
			{
				const int32_t sliderY = rowY + kRowH - kSliderH;
				addThemeRect(contentX + kPad, sliderY, sliderW, kSliderH, theme.surface, 0.72f);
				addThemeRect(contentX + kPad, sliderY, sliderW / 2, kSliderH, theme.primary, 0.72f);
				rowY += kRowH + kRowGap;
			}
			// Toggle row (showTooltips)
			addThemeRect(contentX + kPad, rowY, contentW - kPad * 2, 36, theme.surface, 0.35f);
			addThemeRect(contentX + kPad, rowY + 35, contentW - kPad * 2, 1, theme.border, 0.30f);
		}
		else if (activeTab == Tab::Network)
		{
			// 2 rows: preferredServer dropdown, gameplayUdp toggle
			constexpr int32_t kRowH   = 36;
			constexpr int32_t kRowGap = 10;
			int32_t rowY = rowsStartY;
			for (int32_t ri = 0; ri < 2; ++ri)
			{
				addThemeRect(contentX + kPad, rowY, contentW - kPad * 2, kRowH, theme.surface, 0.45f);
				addThemeRect(contentX + kPad, rowY + kRowH - 1, contentW - kPad * 2, 1, theme.border, 0.30f);
				rowY += kRowH + kRowGap;
			}
		}
		else // Tab::Account (read-only identity + action buttons)
		{
			// Identity block: login + TAG-ID (2 label rows)
			constexpr int32_t kIdRowH   = 28;
			constexpr int32_t kIdRowGap =  6;
			int32_t rowY = rowsStartY;
			for (int32_t ri = 0; ri < 2; ++ri)
			{
				addThemeRect(contentX + kPad, rowY, contentW - kPad * 2, kIdRowH, theme.surface, 0.30f);
				addThemeRect(contentX + kPad, rowY + kIdRowH - 1, contentW - kPad * 2, 1, theme.border, 0.25f);
				rowY += kIdRowH + kIdRowGap;
			}
			// Action buttons: change password, change email, logout
			constexpr int32_t kBtnH   = 32;
			constexpr int32_t kBtnGap = 10;
			rowY += 8;
			for (int32_t bi = 0; bi < 3; ++bi)
			{
				const float bA = (bi == 2) ? 0.65f : 0.80f; // logout dimmer
				const float* bCol = (bi == 2) ? theme.secondary : theme.primary;
				addThemeRect(contentX + kPad, rowY, contentW - kPad * 2, kBtnH, bCol, bA);
				addThemeRect(contentX + kPad, rowY + kBtnH - 2, contentW - kPad * 2, 2, theme.accent, 0.55f);
				rowY += kBtnH + kBtnGap;
			}
		}

		// Footer: Apply (primary) + Cancel (surface) buttons + optional dirty banner
		{
			const int32_t footerY = panelY + kPanelH - kFooterH;
			addThemeRect(contentX, footerY, contentW, 1, theme.border, 0.40f);

			constexpr int32_t kBtnW = 120;
			constexpr int32_t kBtnH =  36;
			const int32_t btnY = footerY + (kFooterH - kBtnH) / 2;
			addThemeRect(contentX + kPad, btnY, kBtnW, kBtnH, theme.primary, 0.90f);
			addThemeRect(contentX + kPad, btnY + kBtnH - 2, kBtnW, 2, theme.accent, 0.85f);
			addThemeRect(contentX + kPad + kBtnW + 12, btnY, kBtnW, kBtnH, theme.surface, 0.80f);
			addThemeRect(contentX + kPad + kBtnW + 12, btnY + kBtnH - 2, kBtnW, 2, theme.border, 0.55f);

			// Dirty banner ("Modifications non enregistrées") — right-aligned in footer
			if (hasUnsavedChanges)
			{
				constexpr float kDirtyColor[4]{ 0.85f, 0.64f, 0.25f, 1.f };
				const int32_t bannerX = contentX + kPad + kBtnW * 2 + 24;
				const int32_t bannerW = contentX + contentW - kPad - bannerX;
				if (bannerW > 20)
				{
					addRect(bannerX, btnY, bannerW, kBtnH,
						kDirtyColor[0], kDirtyColor[1], kDirtyColor[2], 0.10f);
					addRect(bannerX, btnY, bannerW, 2,
						kDirtyColor[0], kDirtyColor[1], kDirtyColor[2], 0.80f);
				}
			}
		}

		return layers;
	}

} // namespace engine::render
