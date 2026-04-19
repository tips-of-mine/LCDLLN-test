#include "engine/render/ChatUiRenderer.h"

#include <cstdint>

namespace engine::render
{
	namespace
	{
		// Design spec (CharacterAndHud.jsx HudOverlay — chat panel):
		// position: absolute, bottom: 18, left: 18, width: 360
		// background: rgba(10,13,18,.75), border: 1px solid var(--ln-border)
		// padding: 10; tabs row height ~22px; input field height 28px
		constexpr int32_t kTabRowH  = 22; ///< Tab labels strip height.
		constexpr int32_t kInputH   = 28; ///< Input draft field height.
		constexpr int32_t kPad      =  6; ///< Inner horizontal/vertical padding.
		constexpr int32_t kTabCount =  4; ///< General / Trade / Guild / Whisper.
	}

	std::vector<AuthUiLayer> BuildChatUiLayers(
		VkExtent2D extent,
		const engine::client::ChatPanelState& state,
		const AuthUiTheme& theme)
	{
		std::vector<AuthUiLayer> layers;
		if (!state.layoutValid || extent.width == 0 || extent.height == 0)
			return layers;

		const int32_t px = static_cast<int32_t>(state.panelX);
		const int32_t py = static_cast<int32_t>(state.panelY);
		const int32_t pw = static_cast<int32_t>(state.panelWidth);
		const int32_t ph = static_cast<int32_t>(state.panelHeight);
		if (pw <= 0 || ph <= 0) return layers;

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

		// Panel background: rgba(10,13,18,.75) ≈ background at 75%
		addThemeRect(px, py, pw, ph, theme.background, 0.75f);

		// 1px border
		addThemeRect(px, py, pw, 1, theme.border, 0.65f);
		addThemeRect(px, py + ph - 1, pw, 1, theme.border, 0.65f);
		addThemeRect(px, py, 1, ph, theme.border, 0.65f);
		addThemeRect(px + pw - 1, py, 1, ph, theme.border, 0.65f);

		// Tab strip — 4 equal-width tabs
		const int32_t tabW = pw / kTabCount;
		const int32_t activeTabIdx = static_cast<int32_t>(state.activeTab);
		for (int32_t ti = 0; ti < kTabCount; ++ti)
		{
			const int32_t tabX = px + ti * tabW;
			if (ti == activeTabIdx)
			{
				// Active tab: accent underline (design: color: var(--ln-accent))
				addThemeRect(tabX + 2, py + kTabRowH - 2, tabW - 4, 2, theme.accent, 0.85f);
			}
		}
		// Tab row bottom separator
		addThemeRect(px, py + kTabRowH, pw, 1, theme.border, 0.40f);

		// Input field at bottom
		const int32_t inputY = py + ph - kInputH - kPad;
		const float inputBgA = state.focused ? 0.85f : 0.45f;
		addThemeRect(px + kPad, inputY, pw - kPad * 2, kInputH, theme.surface, inputBgA);
		// Bottom accent / border on input: primary when focused, border otherwise
		if (state.focused)
			addThemeRect(px + kPad, inputY + kInputH - 2, pw - kPad * 2, 2, theme.primary, 0.85f);
		else
			addThemeRect(px + kPad, inputY + kInputH - 1, pw - kPad * 2, 1, theme.border, 0.40f);

		return layers;
	}

} // namespace engine::render
