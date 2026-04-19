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
		const float fillColor[4])
	{
		// Track: rgba(255,255,255,.05) — design spec
		addRect(barX, barY, barW, barH, 1.f, 1.f, 1.f, 0.05f);
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
			kHpFill);
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
			kManaFill);
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
				kTgtFill);
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

	// Portrait area (52×52 inside player panel, top-left)
	{
		const int32_t px = toI(state.portraitBounds.x);
		const int32_t py = toI(state.portraitBounds.y);
		const int32_t pw = toI(state.portraitBounds.width);
		const int32_t ph = toI(state.portraitBounds.height);
		if (pw > 0 && ph > 0)
		{
			addThemeRect(px, py, pw, ph, theme.surface, 0.70f);
			addThemeRect(px, py, pw, 1, theme.border, 0.65f);
			addThemeRect(px, py, 1, ph, theme.border, 0.65f);
			addThemeRect(px + pw - 1, py, 1, ph, theme.border, 0.65f);
			addThemeRect(px, py + ph - 1, pw, 1, theme.border, 0.65f);
		}
	}

	// XP bar (thin bar below mana bar)
	{
		const int32_t bx = toI(state.playerXpBarBounds.x);
		const int32_t by = toI(state.playerXpBarBounds.y);
		const int32_t bw = toI(state.playerXpBarBounds.width);
		const int32_t bh = toI(state.playerXpBarBounds.height);
		if (bw > 0 && bh > 0)
		{
			// Track: near-transparent white (rgba(255,255,255,.05))
			addRect(bx, by, bw, bh, 1.f, 1.f, 1.f, 0.05f);
			if (state.playerXpPct > 0.f)
			{
				const int32_t fillW = static_cast<int32_t>(
					static_cast<float>(bw) * std::min(1.f, state.playerXpPct));
				if (fillW > 0)
				{
					// #E8A55C — warning/accent orange matching design spec Bar color
					addRect(bx, by, fillW, bh, 0.910f, 0.647f, 0.361f, 0.88f);
				}
			}
		}
	}

	// Minimap (top-right)
	if (state.minimapVisible)
	{
		const int32_t mx = toI(state.minimapBounds.x);
		const int32_t my = toI(state.minimapBounds.y);
		const int32_t mw = toI(state.minimapBounds.width);
		const int32_t mh = toI(state.minimapBounds.height);
		addThemeRect(mx - 1, my - 1, mw + 2, mh + 2, theme.border, 0.70f);
		addThemeRect(mx, my, mw, mh, theme.surface, 0.82f);

		for (const auto& blip : state.minimapBlips)
		{
			constexpr int32_t kBlipSz = 4;
			const int32_t bx = mx + static_cast<int32_t>(blip.xPct * static_cast<float>(mw));
			const int32_t by = my + static_cast<int32_t>(blip.yPct * static_cast<float>(mh));
			// kind 0 = hostile → #C44040 (red), kind 2 = quest → accent, kind 1 = friendly → primary
			if (blip.kind == 0)
				addRect(bx - kBlipSz / 2, by - kBlipSz / 2, kBlipSz, kBlipSz, 0.769f, 0.251f, 0.251f, 0.90f);
			else if (blip.kind == 2)
				addThemeRect(bx - kBlipSz / 2, by - kBlipSz / 2, kBlipSz, kBlipSz, theme.accent, 0.90f);
			else
				addThemeRect(bx - kBlipSz / 2, by - kBlipSz / 2, kBlipSz, kBlipSz, theme.primary, 0.90f);
		}

		// Player dot at centre
		constexpr int32_t kPlayerDot = 6;
		addThemeRect(mx + mw / 2 - kPlayerDot / 2, my + mh / 2 - kPlayerDot / 2,
			kPlayerDot, kPlayerDot, theme.accent, 1.0f);
	}

	// Action bar (bottom-centre, 10 slots)
	if (!state.actionSlots.empty())
	{
		const int32_t abx = toI(state.actionBarBounds.x);
		const int32_t aby = toI(state.actionBarBounds.y);
		const int32_t abw = toI(state.actionBarBounds.width);
		const int32_t abh = toI(state.actionBarBounds.height);
		addThemeRect(abx - 4, aby - 4, abw + 8, abh + 8, theme.border, 0.45f);
		addThemeRect(abx, aby, abw, abh, theme.panel, 0.85f);

		for (const auto& slot : state.actionSlots)
		{
			const int32_t sx = toI(slot.bounds.x);
			const int32_t sy = toI(slot.bounds.y);
			const int32_t sw = toI(slot.bounds.width);
			const int32_t sh = toI(slot.bounds.height);
			const float   bgA = slot.hasAbility ? 0.85f : 0.40f;
			addThemeRect(sx, sy, sw, sh, theme.surface, bgA);
			addThemeRect(sx, sy, sw, 1, theme.border, 0.50f);
			addThemeRect(sx, sy, 1, sh, theme.border, 0.50f);
			addThemeRect(sx + sw - 1, sy, 1, sh, theme.border, 0.50f);
			addThemeRect(sx, sy + sh - 1, sw, 1, theme.border, 0.50f);
			if (slot.cooldownPct > 0.f && slot.cooldownPct < 1.f)
			{
				const int32_t coolH = static_cast<int32_t>(static_cast<float>(sh) * slot.cooldownPct);
				if (coolH > 0)
					addRect(sx, sy, sw, coolH, 0.f, 0.f, 0.f, 0.55f);
			}
			if (slot.active)
				addThemeRect(sx, sy, sw, 2, theme.accent, 1.0f);
		}
	}

	return layers;
}

} // namespace engine::render
