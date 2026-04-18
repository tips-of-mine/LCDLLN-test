#include "engine/render/CombatHudRenderer.h"

#include <algorithm>
#include <cstdint>

namespace engine::render
{

std::vector<AuthUiLayer> BuildCombatHudLayers(
	VkExtent2D extent,
	const engine::client::CombatHudState& state,
	const AuthUiTheme& theme)
{
	std::vector<AuthUiLayer> layers;
	if (!state.layoutValid || extent.width == 0 || extent.height == 0)
		return layers;

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

	auto addBar = [&](int32_t barX, int32_t barY, int32_t barW, int32_t barH,
		uint32_t cur, uint32_t maxVal,
		const float fillColor[4], const float trackColor[4])
	{
		addThemeRect(barX, barY, barW, barH, trackColor, 0.82f);
		if (maxVal > 0u)
		{
			const int32_t fillW = static_cast<int32_t>(
				static_cast<float>(barW) * std::min(1.f, static_cast<float>(cur) / static_cast<float>(maxVal)));
			if (fillW > 0)
				addThemeRect(barX, barY, fillW, barH, fillColor, 0.92f);
		}
	};

	const auto toI = [](float v) { return static_cast<int32_t>(v); };

	// Player panel background + left accent stripe
	{
		const int32_t px = toI(state.panelBounds.x);
		const int32_t py = toI(state.panelBounds.y);
		const int32_t pw = toI(state.panelBounds.width);
		const int32_t ph = toI(state.panelBounds.height);
		addThemeRect(px, py, pw, ph, theme.panel, 0.88f);
		addThemeRect(px, py, 2, ph, theme.accent, 0.85f);
	}

	// HP bar (semantic red)
	if (state.playerHealthBar.visible)
	{
		const int32_t bx = toI(state.playerHealthBar.bounds.x);
		const int32_t by = toI(state.playerHealthBar.bounds.y);
		const int32_t bw = toI(state.playerHealthBar.bounds.width);
		const int32_t bh = toI(state.playerHealthBar.bounds.height);
		constexpr float kHpFill[4]{ 0.545f, 0.118f, 0.176f, 1.f };
		addBar(bx, by, bw, bh,
			state.playerHealthBar.currentValue, state.playerHealthBar.maxValue,
			kHpFill, theme.surface);
	}

	// Mana bar (semantic blue)
	if (state.playerManaBar.visible)
	{
		const int32_t bx = toI(state.playerManaBar.bounds.x);
		const int32_t by = toI(state.playerManaBar.bounds.y);
		const int32_t bw = toI(state.playerManaBar.bounds.width);
		const int32_t bh = toI(state.playerManaBar.bounds.height);
		constexpr float kManaFill[4]{ 0.243f, 0.408f, 0.620f, 1.f };
		addBar(bx, by, bw, bh,
			state.playerManaBar.currentValue, state.playerManaBar.maxValue,
			kManaFill, theme.surface);
	}

	// Combo point dots (below mana bar)
	if (state.playerHasCombo && state.playerMaxComboPoints > 0u)
	{
		const int32_t bx = toI(state.playerManaBar.bounds.x);
		const int32_t by = toI(state.playerManaBar.bounds.y);
		const int32_t bh = toI(state.playerManaBar.bounds.height);
		constexpr int32_t kDotSz  = 10;
		constexpr int32_t kDotGap =  4;
		const int32_t dotY = by + bh + 6;
		for (uint32_t i = 0u; i < state.playerMaxComboPoints; ++i)
		{
			const int32_t dotX   = bx + static_cast<int32_t>(i) * (kDotSz + kDotGap);
			const float*  dotCol = (i < state.playerComboPoints) ? theme.accent : theme.border;
			const float   dotA   = (i < state.playerComboPoints) ? 0.90f : 0.45f;
			addThemeRect(dotX, dotY, kDotSz, kDotSz, dotCol, dotA);
		}
	}

	// Combat log background
	{
		const int32_t lx = toI(state.combatLogBounds.x);
		const int32_t ly = toI(state.combatLogBounds.y);
		const int32_t lw = toI(state.combatLogBounds.width);
		const int32_t lh = toI(state.combatLogBounds.height);
		addThemeRect(lx, ly, lw, lh, theme.surface, 0.65f);
		addThemeRect(lx, ly, 2, lh, theme.primary, 0.55f);
	}

	// Target frame (top-center)
	if (state.targetVisible)
	{
		const int32_t tx = toI(state.targetFrameBounds.x);
		const int32_t ty = toI(state.targetFrameBounds.y);
		const int32_t tw = toI(state.targetFrameBounds.width);
		const int32_t th = toI(state.targetFrameBounds.height);
		addThemeRect(tx, ty, tw, th, theme.panel, 0.88f);
		addThemeRect(tx, ty, tw, 2, theme.primary, 0.85f);

		if (state.targetHealthBar.visible)
		{
			const int32_t bx = toI(state.targetHealthBar.bounds.x);
			const int32_t by = toI(state.targetHealthBar.bounds.y);
			const int32_t bw = toI(state.targetHealthBar.bounds.width);
			const int32_t bh = toI(state.targetHealthBar.bounds.height);
			constexpr float kTgtFill[4]{ 0.545f, 0.118f, 0.176f, 1.f };
			addBar(bx, by, bw, bh,
				state.targetHealthBar.currentValue, state.targetHealthBar.maxValue,
				kTgtFill, theme.surface);
		}
	}

	// Cooldown slots (fill bottom-up as elapsed time grows)
	for (const auto& cd : state.cooldowns)
	{
		const int32_t cx = toI(cd.bounds.x);
		const int32_t cy = toI(cd.bounds.y);
		const int32_t cw = toI(cd.bounds.width);
		const int32_t ch = toI(cd.bounds.height);
		addThemeRect(cx, cy, cw, ch, theme.border, 0.75f);
		if (cd.active && cd.durationSeconds > 0.f)
		{
			const float   elapsed = cd.durationSeconds - cd.remainingSeconds;
			const float   pct     = std::min(1.f, elapsed / cd.durationSeconds);
			const int32_t fillH   = static_cast<int32_t>(static_cast<float>(ch) * pct);
			if (fillH > 0)
				addThemeRect(cx, cy + ch - fillH, cw, fillH, theme.accent, 0.55f);
		}
	}

	// Wallet strip (top-right)
	if (state.walletVisible)
	{
		const int32_t wx = toI(state.walletBounds.x);
		const int32_t wy = toI(state.walletBounds.y);
		const int32_t ww = toI(state.walletBounds.width);
		const int32_t wh = toI(state.walletBounds.height);
		addThemeRect(wx, wy, ww, wh, theme.panel, 0.82f);
		addThemeRect(wx, wy + wh - 2, ww, 2, theme.accent, 0.60f);
	}

	return layers;
}

} // namespace engine::render
