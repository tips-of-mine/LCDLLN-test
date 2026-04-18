#include "engine/render/CharacterCreationRenderer.h"

#include <algorithm>
#include <cstdint>

namespace engine::render
{
	namespace
	{
		constexpr int32_t kColGap        = 12; ///< Horizontal gap between the three columns.
		constexpr int32_t kPanelPad      = 10; ///< Inner padding inside each column panel.
		constexpr int32_t kCardH         = 64; ///< Height of a single race/class card row.
		constexpr int32_t kCardGap       =  8; ///< Vertical gap between cards.
		constexpr int32_t kCardCols      =  2; ///< Race grid: 2-column layout within the left panel.
		constexpr int32_t kStepBarH      = 32; ///< Height of the step indicator dot row.
		constexpr int32_t kStepBarMargin = 12; ///< Vertical margin above/below the step bar.
		constexpr int32_t kTotalSteps    =  4; ///< Race → Class → Customization → Name.
	}

	CharacterCreationLayout BuildCharacterCreationLayout(VkExtent2D extent)
	{
		const int32_t w = static_cast<int32_t>(extent.width);
		const int32_t h = static_cast<int32_t>(extent.height);

		CharacterCreationLayout lay{};
		lay.colY = kStepBarH + kStepBarMargin * 2;
		lay.colH = h - lay.colY - kStepBarMargin;

		const int32_t totalW = std::min(w - 40, 1200);
		const int32_t startX = (w - totalW) / 2;
		lay.raceColW  = totalW * 28 / 100;
		lay.portraitW = totalW * 38 / 100;
		lay.classColW = totalW - lay.raceColW - lay.portraitW - kColGap * 2;
		lay.raceColX  = startX;
		lay.portraitX = startX + lay.raceColW + kColGap;
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

		// Background
		addThemeRect(0, 0, w, h, theme.background, 0.96f);

		// Step bar background strip
		addThemeRect(0, 0, w, kStepBarH + kStepBarMargin * 2, theme.surface, 0.85f);

		// Step indicator dots + separators
		{
			constexpr int32_t kStepSlotW = 100;
			constexpr int32_t kSepW      = 24;
			constexpr int32_t kDotSz     = 14;
			const int32_t totalBarW = kTotalSteps * kStepSlotW + (kTotalSteps - 1) * kSepW;
			int32_t stepX = (w - totalBarW) / 2;
			const int32_t dotY = kStepBarMargin + (kStepBarH - kDotSz) / 2;

			using Step = engine::client::CharacterCreationStep;
			int32_t activeStep = 0;
			switch (state.step)
			{
				case Step::RaceSelect:    activeStep = 0; break;
				case Step::ClassSelect:   activeStep = 1; break;
				case Step::Customization: activeStep = 2; break;
				default:                  activeStep = 3; break;
			}

			for (int32_t si = 0; si < kTotalSteps; ++si)
			{
				const int32_t dotX = stepX + (kStepSlotW - kDotSz) / 2;
				if (si < activeStep)
					addThemeRect(dotX, dotY, kDotSz, kDotSz, theme.accent, 0.85f);
				else if (si == activeStep)
					addThemeRect(dotX, dotY, kDotSz, kDotSz, theme.primary, 1.0f);
				else
					addThemeRect(dotX, dotY, kDotSz, kDotSz, theme.border, 0.60f);

				if (si < kTotalSteps - 1)
				{
					const int32_t sepX = stepX + kStepSlotW;
					const int32_t sepY = dotY + kDotSz / 2;
					const float*  sc   = (si < activeStep) ? theme.accent : theme.border;
					addThemeRect(sepX, sepY, kSepW, 1, sc, 0.70f);
				}
				stepX += kStepSlotW + kSepW;
			}
		}

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

		// Portrait / center column
		addThemeRect(lay.portraitX, lay.colY, lay.portraitW, lay.colH, theme.surface, 0.72f);
		{
			constexpr int32_t kPortraitH = 280;
			const int32_t portW = std::min(lay.portraitW - kPanelPad * 2, 200);
			const int32_t portX = lay.portraitX + (lay.portraitW - portW) / 2;
			const int32_t portY = lay.colY + kPanelPad + 24;
			addThemeRect(portX, portY, portW, kPortraitH, theme.border, 0.45f);
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
