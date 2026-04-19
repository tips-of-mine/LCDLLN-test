#include "engine/render/CharacterCreationRenderer.h"

#include <algorithm>
#include <cstdint>

namespace engine::render
{
	namespace
	{
		// Design spec (CharacterAndHud.jsx):
		// gridTemplateColumns: '280px 1fr 320px', gap: 24, padding: 28
		constexpr int32_t kOuterPad   = 28; ///< Outer grid padding.
		constexpr int32_t kColGap     = 24; ///< Gap between the three columns.
		constexpr int32_t kRaceColW   = 280; ///< Fixed race list column width.
		constexpr int32_t kClassColW  = 320; ///< Fixed class list column width.
		constexpr int32_t kPanelPad   = 12; ///< Inner padding inside each column panel.
		constexpr int32_t kCardH      = 54; ///< Height of a single race/class card row (10+12px padding).
		constexpr int32_t kCardGap    =  4; ///< Vertical gap between cards.
		// Portrait dimensions (design: 340×420, centred in middle column)
		constexpr int32_t kPortraitW  = 340;
		constexpr int32_t kPortraitH  = 420;
	}

	CharacterCreationLayout BuildCharacterCreationLayout(VkExtent2D extent)
	{
		const int32_t w = static_cast<int32_t>(extent.width);
		const int32_t h = static_cast<int32_t>(extent.height);

		CharacterCreationLayout lay{};
		// No step bar — all 3 columns fill the viewport with outer padding.
		lay.colY  = kOuterPad;
		lay.colH  = h - kOuterPad * 2;

		// Fixed widths from design; centre column fills remaining space.
		lay.raceColW  = kRaceColW;
		lay.classColW = kClassColW;
		lay.portraitW = std::max(100, w - kRaceColW - kClassColW - kColGap * 2 - kOuterPad * 2);
		lay.raceColX  = kOuterPad;
		lay.portraitX = kOuterPad + kRaceColW + kColGap;
		lay.classColX = lay.portraitX + lay.portraitW + kColGap;
		return lay;
	}

	std::vector<AuthUiLayer> BuildCharacterCreationLayers(
		VkExtent2D extent,
		const engine::client::CharacterCreationState& state,
		uint32_t raceCount,
		uint32_t classCount,
		const AuthUiTheme& theme)
	{
		std::vector<AuthUiLayer> layers;
		if (extent.width == 0 || extent.height == 0)
			return layers;

		const int32_t w = static_cast<int32_t>(extent.width);
		const int32_t h = static_cast<int32_t>(extent.height);
		const CharacterCreationLayout lay = BuildCharacterCreationLayout(extent);

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

		// Background (radial gradient simulation — dark blue-green)
		addThemeRect(0, 0, w, h, theme.background, 0.96f);

		// Column visibility: race always shown, class dimmed when not on ClassSelect
		using Step = engine::client::CharacterCreationStep;
		const bool activeOnClass  = (state.step == Step::ClassSelect);
		const bool showNameOverlay = (state.step == Step::NameInput
			|| state.step == Step::Confirming
			|| state.step == Step::Done
			|| state.step == Step::Error);
		const bool showCustom = (state.step == Step::Customization);
		const float classColA = activeOnClass ? 1.0f : 0.45f;

		// Race column
		addThemeRect(lay.raceColX, lay.colY, lay.raceColW, lay.colH, theme.panel, 0.88f);
		addThemeRect(lay.raceColX, lay.colY, lay.raceColW, 2, theme.border, 0.70f);

		if (raceCount > 0u)
		{
			const int32_t cardW = (lay.raceColW - kPanelPad * 2 - kCardGap) / kCardCols;

			// Hover highlight (lighter, no accent border)
			if (state.hoveredRaceIndex >= 0
				&& static_cast<uint32_t>(state.hoveredRaceIndex) != state.selectedRaceIndex)
			{
				const int32_t hr  = state.hoveredRaceIndex / kCardCols;
				const int32_t hc  = state.hoveredRaceIndex % kCardCols;
				const int32_t hcX = lay.raceColX + kPanelPad + hc * (cardW + kCardGap);
				const int32_t hcY = lay.colY + kPanelPad + hr * (kCardH + kCardGap);
				if (hcY + kCardH <= lay.colY + lay.colH)
					addThemeRect(hcX, hcY, cardW, kCardH, theme.primary, 0.15f);
			}

			// Selection highlight
			const int32_t row   = static_cast<int32_t>(state.selectedRaceIndex) / kCardCols;
			const int32_t col   = static_cast<int32_t>(state.selectedRaceIndex) % kCardCols;
			const int32_t cardX = lay.raceColX + kPanelPad + col * (cardW + kCardGap);
			const int32_t cardY = lay.colY + kPanelPad + row * (kCardH + kCardGap);
			if (cardY + kCardH <= lay.colY + lay.colH)
			{
				addThemeRect(cardX - 1, cardY - 1, cardW + 2, kCardH + 2, theme.accent, 0.75f);
				addThemeRect(cardX, cardY, cardW, kCardH, theme.primary, 0.25f);
			}
		}

		// Centre column — flex column, centered
		addThemeRect(lay.portraitX, lay.colY, lay.portraitW, lay.colH, theme.surface, 0.20f);
		// Portrait (340×420 or fit in column, vertically centred)
		{
			const int32_t portW = std::min(kPortraitW, lay.portraitW - kPanelPad * 2);
			const int32_t portH = std::min(kPortraitH, lay.colH - 80); // leave room for name field
			const int32_t portX = lay.portraitX + (lay.portraitW - portW) / 2;
			const int32_t portY = lay.colY + (lay.colH - portH - 50) / 2; // 50 = name field
			addThemeRect(portX, portY, portW, portH, theme.surface, 0.72f);
			addThemeRect(portX, portY, portW, 1, theme.border, 0.70f);
			addThemeRect(portX, portY, 1, portH, theme.border, 0.70f);
			addThemeRect(portX + portW - 1, portY, 1, portH, theme.border, 0.70f);
			addThemeRect(portX, portY + portH - 1, portW, 1, theme.border, 0.70f);
			// Name input field below portrait (width=340, height=36)
			const int32_t nameY = portY + portH + 16;
			addThemeRect(portX, nameY, portW, 36, theme.surface, 0.60f);
			addThemeRect(portX, nameY + 35, portW, 1, theme.border, 0.65f);
			// Gender toggle (F/M) at bottom-right of portrait — 28×28 each, gap 6
			const int32_t tgY = portY + portH - 28 - 12;
			const int32_t tgX = portX + portW - 28 * 2 - 6 - 12;
			const bool femaleActive = (state.gender == 'F');
			// F button
			addThemeRect(tgX, tgY, 28, 28,
				femaleActive ? theme.primary : theme.surface,
				femaleActive ? 0.90f : 0.60f);
			// M button
			addThemeRect(tgX + 28 + 6, tgY, 28, 28,
				femaleActive ? theme.surface : theme.primary,
				femaleActive ? 0.60f : 0.90f);
		}

		// Customization sliders (face / hair / skin / hair color / eye color)
		if (showCustom)
		{
			constexpr int32_t kSliderCount = 5;
			constexpr int32_t kSliderH     = 6;
			constexpr int32_t kSliderRow   = 24; ///< Row height per slider (gap included).
			const int32_t sliderW = lay.portraitW - kPanelPad * 4;
			const int32_t sliderX = lay.portraitX + kPanelPad * 2;
			int32_t sliderY = lay.colY + lay.colH - kSliderCount * kSliderRow - kPanelPad;

			const uint8_t vals[kSliderCount] = {
				state.faceType, state.hairStyle,
				state.skinColorIdx, state.hairColorIdx, state.eyeColorIdx
			};
			// Upper-bound index values per attribute: face=3, hair=7, color indices=15.
			const int32_t maxs[kSliderCount] = { 3, 7, 15, 15, 15 };

			for (int32_t si = 0; si < kSliderCount; ++si)
			{
				addThemeRect(sliderX, sliderY, sliderW, kSliderH, theme.surface, 0.80f);
				if (maxs[si] > 0)
				{
					const int32_t fillW = static_cast<int32_t>(
						static_cast<float>(sliderW) * static_cast<float>(vals[si]) / static_cast<float>(maxs[si]));
					if (fillW > 0)
						addThemeRect(sliderX, sliderY, fillW, kSliderH, theme.accent, 0.85f);
				}
				sliderY += kSliderRow;
			}
		}

		// Class column
		addThemeRect(lay.classColX, lay.colY, lay.classColW, lay.colH, theme.panel, 0.88f * classColA);
		addThemeRect(lay.classColX, lay.colY, lay.classColW, 2, theme.border, 0.70f * classColA);

		if (classCount > 0u && activeOnClass)
		{
			const int32_t cardW = lay.classColW - kPanelPad * 2;
			const int32_t cardX = lay.classColX + kPanelPad;

			// Hover highlight
			if (state.hoveredClassIndex >= 0
				&& static_cast<uint32_t>(state.hoveredClassIndex) != state.selectedClassIndex)
			{
				const int32_t hcY = lay.colY + kPanelPad
					+ state.hoveredClassIndex * (kCardH + kCardGap);
				if (hcY + kCardH <= lay.colY + lay.colH)
					addThemeRect(cardX, hcY, cardW, kCardH, theme.primary, 0.15f);
			}

			// Selection highlight
			const int32_t cardY = lay.colY + kPanelPad
				+ static_cast<int32_t>(state.selectedClassIndex) * (kCardH + kCardGap);
			if (cardY + kCardH <= lay.colY + lay.colH)
			{
				addThemeRect(cardX - 1, cardY - 1, cardW + 2, kCardH + 2, theme.accent, 0.75f);
				addThemeRect(cardX, cardY, cardW, kCardH, theme.primary, 0.25f);
			}
		}

		// Name input overlay (NameInput / Confirming / Done / Error)
		if (showNameOverlay)
		{
			addRect(0, 0, w, h, 0.f, 0.f, 0.f, 0.55f);

			constexpr int32_t kBoxW = 420;
			constexpr int32_t kBoxH = 160;
			const int32_t boxX = (w - kBoxW) / 2;
			const int32_t boxY = (h - kBoxH) / 2;
			addThemeRect(boxX - 2, boxY - 2, kBoxW + 4, kBoxH + 4, theme.border, 0.90f);
			addThemeRect(boxX, boxY, kBoxW, kBoxH, theme.panel, 0.97f);
			addThemeRect(boxX, boxY, kBoxW, 3, theme.accent, 1.0f);

			const int32_t fieldX = boxX + kPanelPad;
			const int32_t fieldY = boxY + 48;
			const int32_t fieldW = kBoxW - kPanelPad * 2;
			addThemeRect(fieldX, fieldY, fieldW, kAuthUiFieldBoxHeightPx, theme.surface, 0.88f);
			addThemeRect(fieldX, fieldY, fieldW, 1, theme.border, 0.70f);
			// Bottom border: primary when valid, error-red when invalid and non-empty
			if (!state.nameValid && !state.characterName.empty())
			{
				constexpr float kErrColor[4]{ 0.80f, 0.18f, 0.18f, 1.f };
				addRect(fieldX, fieldY + kAuthUiFieldBoxHeightPx - 2, fieldW, 2,
					kErrColor[0], kErrColor[1], kErrColor[2], kErrColor[3]);
			}
			else
			{
				addThemeRect(fieldX, fieldY + kAuthUiFieldBoxHeightPx - 2, fieldW, 2, theme.primary, 0.85f);
			}
		}

		// Error banner (step == Error, below the name box)
		if (state.step == Step::Error)
		{
			constexpr int32_t kErrBannerW = 380;
			constexpr int32_t kErrBannerH =  40;
			const int32_t errX = (w - kErrBannerW) / 2;
			const int32_t errY = h / 2 + 100;
			constexpr float kErrBg[4]{ 0.80f, 0.18f, 0.18f, 1.f };
			addRect(errX, errY, kErrBannerW, kErrBannerH,
				kErrBg[0], kErrBg[1], kErrBg[2], 0.22f);
			addRect(errX, errY, kErrBannerW, 2,
				kErrBg[0], kErrBg[1], kErrBg[2], 0.85f);
		}

		return layers;
	}

} // namespace engine::render
