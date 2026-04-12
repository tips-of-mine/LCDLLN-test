#include "engine/render/AuthUiRenderer.h"

#include "engine/platform/FileSystem.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <string_view>

namespace engine::render
{
	namespace
	{
		bool ParseHexColor(std::string_view hex, float out[4])
		{
			if (hex.size() != 7 || hex[0] != '#')
			{
				return false;
			}

			auto hexValue = [](char c) -> int
			{
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
				if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
				return -1;
			};

			for (int i = 0; i < 3; ++i)
			{
				const int hi = hexValue(hex[1 + i * 2]);
				const int lo = hexValue(hex[2 + i * 2]);
				if (hi < 0 || lo < 0)
				{
					return false;
				}
				out[i] = static_cast<float>((hi << 4) | lo) / 255.0f;
			}
			out[3] = 1.0f;
			return true;
		}

		void OverrideThemeColor(const engine::core::Config& cfg, std::string_view key, float out[4])
		{
			const std::string value = cfg.GetString(key, {});
			if (!value.empty())
			{
				ParseHexColor(value, out);
			}
		}
	}

	AuthUiTheme LoadAuthUiTheme(const engine::core::Config& cfg)
	{
		AuthUiTheme theme{};
		const std::string themeId = cfg.GetString("ui.theme.default_id", "default");
		const std::string themePath = "ui/themes/" + themeId + "/theme.json";
		const std::filesystem::path resolvedPath = engine::platform::FileSystem::ResolveContentPath(cfg, themePath);
		engine::core::Config themeCfg;
		if (!themeCfg.LoadFromFile(resolvedPath.string()))
		{
			return theme;
		}

		OverrideThemeColor(themeCfg, "palette.primary", theme.primary);
		OverrideThemeColor(themeCfg, "palette.secondary", theme.secondary);
		OverrideThemeColor(themeCfg, "palette.accent", theme.accent);
		OverrideThemeColor(themeCfg, "palette.background", theme.background);
		OverrideThemeColor(themeCfg, "palette.surface", theme.surface);
		OverrideThemeColor(themeCfg, "palette.panel", theme.panel);
		OverrideThemeColor(themeCfg, "palette.text", theme.text);
		OverrideThemeColor(themeCfg, "palette.mutedText", theme.mutedText);
		OverrideThemeColor(themeCfg, "palette.border", theme.border);
		return theme;
	}

	AuthUiLayoutMetrics BuildAuthUiLayoutMetrics(
		VkExtent2D extent,
		const engine::client::AuthUiPresenter::VisualState& state,
		const engine::client::AuthUiPresenter::RenderModel& model)
	{
		AuthUiLayoutMetrics metrics{};
		if (extent.width == 0 || extent.height == 0)
		{
			return metrics;
		}

		const int32_t w = static_cast<int32_t>(extent.width);
		const int32_t h = static_cast<int32_t>(extent.height);
		metrics.panelW = std::clamp(w * 42 / 100, 540, std::max(820, w * 24 / 100));
		metrics.largeContent = state.terms || model.bodyLines.size() > 6u || model.fields.size() > 5u;
		const bool tallRegister = state.registerMode && model.fields.size() >= 7u;
		metrics.panelH = metrics.largeContent
			? (tallRegister ? std::clamp(h * 86 / 100, 520, std::max(880, h * 92 / 100))
				: std::clamp(h * 74 / 100, 440, std::max(780, h * 82 / 100)))
			: std::clamp(h * 62 / 100, 380, std::max(660, h * 72 / 100));
		metrics.panelX = (w - metrics.panelW) / 2;
		metrics.panelY = (h - metrics.panelH) / 2;
		metrics.innerX = metrics.panelX + 28;
		metrics.artW = std::clamp(metrics.panelW / 3, 150, 240);
		if ((state.login || state.registerMode || state.error) && state.minimalChrome && !state.loginArtColumn)
		{
			metrics.artW = 0;
		}
		{
			const int32_t artGap = metrics.artW > 0 ? 18 : 8;
			metrics.contentX = metrics.innerX + metrics.artW + artGap;
			metrics.contentW = std::max(180, metrics.panelW - (metrics.contentX - metrics.panelX) - 28);
		}
		metrics.authStatusBannerBesideLogo =
			state.login && state.minimalChrome && !state.loginArtColumn && state.authLogoSpin
			&& !model.infoBanner.empty();
		metrics.compactSingleField = model.fields.size() <= 1u;
		const int32_t bodyScale = std::clamp(metrics.panelW / 260, 2, 4);
		const int32_t bodyLineStep = 7 * bodyScale + 2 * bodyScale;
		const int32_t smallScale = std::max(2, bodyScale - 1);
		const int32_t labelAboveFieldPx = smallScale * 11 + 6;
		const int32_t minStepFromOverlap = kAuthUiFieldBoxHeightPx + 8 + labelAboveFieldPx;
		{
			const int32_t compactFieldStep = std::max(52, bodyLineStep + 24);
			const int32_t regularFieldStep = std::max(48, bodyLineStep + 22);
			const int32_t baseStep = metrics.compactSingleField ? compactFieldStep : regularFieldStep;
			metrics.fieldRowStepPx = std::max(baseStep, minStepFromOverlap) + model.layoutAuthFieldRowExtraPx;
		}

		const bool bigTitle = state.languageSelection || state.languageOptions || state.login || state.registerMode
			|| state.verifyEmail || state.forgotPassword || state.characterCreate || state.error || state.submitting;
		const int32_t titleScale = std::clamp(bodyScale + (bigTitle ? 4 : 1), 6, 9);
		const bool centeredLanguage = state.languageSelection || state.languageOptions;

		auto legacyTitleAndTopOffset = [&](int32_t sectionTopPad)
		{
			if (!model.titleLine2.empty())
			{
				metrics.authTitleLine1OffsetFromPanelTopPx = 22;
				metrics.authTitleLine2OffsetFromPanelTopPx = 30 + titleScale * 10;
			}
			else
			{
				metrics.authTitleLine1OffsetFromPanelTopPx = 24;
				metrics.authTitleLine2OffsetFromPanelTopPx = 0;
			}
			metrics.authSectionTitleOffsetFromPanelTopPx = sectionTopPad + titleScale * 14;
			const int32_t sectionBottom = sectionTopPad + titleScale * 14 + bodyLineStep;
			const int32_t minFieldAnchor = sectionBottom + 20;
			if (!model.infoBanner.empty())
			{
				metrics.topOffset = std::max(146, minFieldAnchor + 44);
			}
			else
			{
				metrics.topOffset = std::max(104, minFieldAnchor);
			}
		};

		metrics.authTitleUseViewportWidth = false;
		if (centeredLanguage)
		{
			metrics.authTitleUseViewportWidth = true;
			legacyTitleAndTopOffset(50);
		}
		else if (state.terms)
		{
			const int32_t sectionTopPad = model.titleLine2.empty() ? 30 : 38;
			legacyTitleAndTopOffset(sectionTopPad);
		}
		else
		{
			const bool minimalAuthWide =
				(state.login || state.registerMode || state.verifyEmail
				 || state.forgotPassword || state.characterCreate || state.error)
				&& state.minimalChrome && !state.loginArtColumn;
			metrics.authTitleUseViewportWidth = minimalAuthWide && model.layoutAuthTitleCenterViewportWidth;

			// Remonter le titre principal (~1 ligne) par rapport à la config.
			constexpr int32_t kAuthMainTitleLiftFromPanelTopPx = 20;
			const int32_t gapTitleSection = model.layoutAuthGapTitleToSectionPx;
			const int32_t t1 = std::clamp(
				model.layoutAuthTitleLine1FromPanelTopPx - kAuthMainTitleLiftFromPanelTopPx,
				-40,
				120);
			metrics.authTitleLine1OffsetFromPanelTopPx = t1;
			if (model.titleLine2.empty())
			{
				metrics.authTitleLine2OffsetFromPanelTopPx = 0;
				const int32_t afterTitles = t1 + titleScale * 11;
				metrics.authSectionTitleOffsetFromPanelTopPx = afterTitles + gapTitleSection;
			}
			else
			{
				const int32_t line2 = t1 + 8 + titleScale * 10;
				metrics.authTitleLine2OffsetFromPanelTopPx = line2;
				const int32_t afterTitles = line2 + bodyScale * 10;
				metrics.authSectionTitleOffsetFromPanelTopPx = afterTitles + gapTitleSection;
			}
			// Connexion / création de compte / erreur : titre de section un peu plus haut.
			if (state.login || state.registerMode || state.error)
			{
				constexpr int32_t kAuthLoginRegisterSectionLiftPx = 5;
				metrics.authSectionTitleOffsetFromPanelTopPx -= kAuthLoginRegisterSectionLiftPx;
			}
			const int32_t afterSection = metrics.authSectionTitleOffsetFromPanelTopPx + bodyLineStep;
			const int32_t sectionTitleGlyphH = 7 * bodyScale;
			const int32_t minTopFromSection =
				metrics.authSectionTitleOffsetFromPanelTopPx + sectionTitleGlyphH + 6 + labelAboveFieldPx;
			if (!model.infoBanner.empty() && !metrics.authStatusBannerBesideLogo)
			{
				// Bannière : fond à panelY + topOffset - 42, hauteur 34 ; marge sous la section avant la bannière.
				metrics.topOffset = std::max({ afterSection + 48, 146, minTopFromSection });
			}
			else
			{
				metrics.topOffset = std::max({ afterSection + 12, 88, minTopFromSection });
			}
		}

		if (state.error && !model.errorText.empty())
		{
			const int32_t bodyScaleErr = std::clamp(metrics.panelW / 260, 2, 4);
			const int32_t bodyLineStepErr = 7 * bodyScaleErr + 2 * bodyScaleErr;
			const int32_t sectionBottom = metrics.authSectionTitleOffsetFromPanelTopPx + bodyLineStepErr;
			constexpr int32_t kGapAfterSection = 12;
			constexpr int32_t kErrorBoxH = 84;
			constexpr int32_t kGapBoxFooter = 22;
			constexpr int32_t kErrorFooterBarH = 58;
			int32_t boxTop = sectionBottom + kGapAfterSection;
			int32_t footerTop = boxTop + kErrorBoxH + kGapBoxFooter;
			const int32_t maxFooterTop = std::max(sectionBottom + 40, metrics.panelH - kErrorFooterBarH - 20);
			if (footerTop > maxFooterTop)
			{
				footerTop = maxFooterTop;
				boxTop = footerTop - kGapBoxFooter - kErrorBoxH;
				if (boxTop < sectionBottom + 8)
				{
					boxTop = sectionBottom + 8;
				}
			}
			metrics.authErrorBoxTopFromPanelTopPx = boxTop;
			metrics.authErrorBoxHeightPx = kErrorBoxH;
			metrics.authErrorFooterTopFromPanelTopPx = footerTop;
		}

		return metrics;
	}

	bool TryGetLoginTwoRowLayout(
		const AuthUiLayoutMetrics& lay,
		const engine::client::AuthUiPresenter::VisualState& state,
		const engine::client::AuthUiPresenter::RenderModel& model,
		AuthLoginTwoRowLayout& out)
	{
		if (!state.login || model.actions.size() != 4)
		{
			return false;
		}
		const int32_t panelY = lay.panelY;
		const int32_t panelH = lay.panelH;
		const int32_t contentW = lay.contentW;
		const int32_t topOffset = lay.topOffset;
		const int32_t fieldStep = lay.fieldRowStepPx;
		const int32_t bodyScale = std::clamp(lay.panelW / 260, 2, 4);
		const int32_t bodyLineStep = 7 * bodyScale + 2 * bodyScale;
		const int32_t bodyLinePitch = std::max(28, bodyLineStep + 10);
		const int32_t bodyStartY = panelY + topOffset + static_cast<int32_t>(model.fields.size()) * fieldStep + 18;
		const int32_t bodyBottom = bodyStartY + model.visibleBodyLineCount * bodyLinePitch + 8;
		out.primaryRowY = panelY + panelH - 68;
		out.secondaryRowY = out.primaryRowY - 50;
		if (out.secondaryRowY < bodyBottom)
		{
			out.secondaryRowY = bodyBottom;
		}
		out.buttonHalfWidth = std::max(120, (contentW - 10) / 2);
		const int32_t gap = 10;
		out.primarySubmitWidth = std::max(168, (contentW - gap) * 62 / 100);
		out.primaryQuitWidth = std::max(100, contentW - gap - out.primarySubmitWidth);
		if (out.primarySubmitWidth + gap + out.primaryQuitWidth > contentW)
		{
			out.primaryQuitWidth = std::max(100, contentW - gap - out.primarySubmitWidth);
		}
		return true;
	}

	std::vector<AuthUiLayer> BuildAuthUiLayers(const VkExtent2D extent, const engine::client::AuthUiPresenter::VisualState& state,
		const engine::client::AuthUiPresenter::RenderModel& model, const AuthUiTheme& theme, bool calibrationOverlay, bool usePhotoBackdrop)
	{
		std::vector<AuthUiLayer> layers;
		if (extent.width == 0 || extent.height == 0 || !state.active || !model.visible)
		{
			return layers;
		}

		const int32_t w = static_cast<int32_t>(extent.width);
		const int32_t h = static_cast<int32_t>(extent.height);
		// vkCmdClearAttachments : VkRect2D est en coordonnées framebuffer Vulkan : origine haut-gauche,
		// Y vers le bas (comme le layout UI). Ne pas convertir avec (h - y - rh) — cela inversait l’image.
		const AuthUiLayoutMetrics layout = BuildAuthUiLayoutMetrics(extent, state, model);
		const int32_t panelW = layout.panelW;
		const int32_t panelH = layout.panelH;
		const int32_t panelX = layout.panelX;
		const int32_t panelY = layout.panelY;
		const int32_t contentX = layout.contentX;
		const int32_t contentW = layout.contentW;
		const int32_t artW = layout.artW;
		const bool largeContent = layout.largeContent;

		auto addRect = [&layers](int32_t x, int32_t y, int32_t rw, int32_t rh, float r, float g, float b, float a)
		{
			if (rw <= 0 || rh <= 0)
			{
				return;
			}

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

		auto addThemeRect = [&addRect](int32_t x, int32_t y, int32_t rw, int32_t rh, const float color[4], float alphaScale = 1.0f)
		{
			addRect(x, y, rw, rh, color[0], color[1], color[2], color[3] * alphaScale);
		};

		// Bouton auth unifié : cadre 1px, léger éclaircissement haut, fond, barre d’accent bas (plus marquée si primaire / survol).
		auto addAuthActionButton = [&](int32_t x, int32_t y, int32_t bw, int32_t bh,
			bool primary, bool emphasized, bool hovered)
		{
			if (bw <= 2 || bh <= 2)
			{
				return;
			}
			const float* fillBase = primary ? theme.primary : (emphasized ? theme.secondary : theme.surface);
			const float fillAlpha = hovered ? 1.0f : (primary ? 0.97f : 0.94f);
			const float borderA = hovered ? 0.92f : 0.78f;
			addThemeRect(x, y, bw, 1, theme.border, borderA);
			addThemeRect(x, y + bh - 1, bw, 1, theme.border, borderA);
			addThemeRect(x, y, 1, bh, theme.border, borderA);
			addThemeRect(x + bw - 1, y, 1, bh, theme.border, borderA);
			const int32_t ix = x + 1;
			const int32_t iy = y + 1;
			const int32_t iw = bw - 2;
			const int32_t ih = bh - 2;
			const int32_t hi = 2;
			const int32_t barH = (primary || hovered) ? 3 : 2;
			const int32_t midH = std::max(1, ih - hi - barH);
			const float t = 0.11f;
			addRect(ix, iy, iw, hi,
				fillBase[0] * (1.f - t) + t,
				fillBase[1] * (1.f - t) + t,
				fillBase[2] * (1.f - t) + t,
				1.0f);
			addThemeRect(ix, iy + hi, iw, midH, fillBase, fillAlpha);
			const float accentA = hovered ? 1.0f : (primary ? 0.90f : 0.52f);
			addThemeRect(ix, iy + ih - barH, iw, barH, theme.accent, accentA);
		};
		if (!usePhotoBackdrop)
		{
			addThemeRect(0, 0, w, h, theme.background, 0.92f);
			addThemeRect(0, 0, w, std::max(90, h / 4), theme.primary, 0.34f);
			addThemeRect(0, h - std::max(96, h / 5), w, std::max(96, h / 5), theme.surface, 0.28f);
			addRect(0, 0, w, 28, 0.01f, 0.02f, 0.03f, 0.55f);
			addRect(0, h - 28, w, 28, 0.01f, 0.02f, 0.03f, 0.55f);
			addRect(0, 0, 28, h, 0.01f, 0.02f, 0.03f, 0.50f);
			addRect(w - 28, 0, 28, h, 0.01f, 0.02f, 0.03f, 0.50f);
		}
		else
		{
			// Photo backdrop: no vignette borders - the background image fills the entire screen.
		}
		if (!state.minimalChrome)
		{
			addRect(panelX - 22, panelY - 22, panelW + 44, panelH + 44, 0.01f, 0.02f, 0.03f, usePhotoBackdrop ? 0.45f : 0.60f);
			addThemeRect(panelX - 8, panelY - 8, panelW + 16, panelH + 16, theme.border, usePhotoBackdrop ? 0.35f : 0.22f);
			addThemeRect(panelX, panelY, panelW, panelH, theme.panel, usePhotoBackdrop ? 0.78f : 0.96f);
			addThemeRect(panelX, panelY, panelW, 4, theme.accent, 1.0f);
			addThemeRect(panelX + 22, panelY + 24, std::max(80, panelW / 3), 6, theme.primary, 0.86f);
			addThemeRect(panelX + 22, panelY + 40, artW, panelH - 68, theme.surface, 0.98f);
			addThemeRect(panelX + 22, panelY + 40, 8, panelH - 68, theme.accent, 0.95f);

			if (artW > 0 && state.login)
			{
				addThemeRect(panelX + 38, panelY + 76, artW - 32, 92, theme.primary, 0.72f);
				addThemeRect(panelX + 52, panelY + 92, artW - 62, 16, theme.accent, 0.96f);
				addThemeRect(panelX + 52, panelY + 118, artW - 74, 10, theme.text, 0.80f);
				addThemeRect(panelX + 52, panelY + 136, artW - 94, 10, theme.mutedText, 0.62f);
			}
			else if (artW > 0 && state.registerMode)
			{
				addThemeRect(panelX + 38, panelY + 72, artW - 30, 126, theme.secondary, 0.56f);
				addThemeRect(panelX + 54, panelY + 88, artW - 62, 18, theme.accent, 0.98f);
				addThemeRect(panelX + 54, panelY + 118, artW - 74, 12, theme.primary, 0.96f);
				addThemeRect(panelX + 54, panelY + 140, artW - 90, 12, theme.text, 0.76f);
				addThemeRect(panelX + 54, panelY + 162, artW - 104, 12, theme.mutedText, 0.58f);
			}
			else if (artW > 0)
			{
				addThemeRect(panelX + 38, panelY + 78, artW - 34, 110, theme.secondary, 0.48f);
				addThemeRect(panelX + 52, panelY + 96, artW - 64, 18, theme.primary, 0.95f);
				addThemeRect(panelX + 52, panelY + 124, artW - 84, 12, theme.accent, 0.90f);
			}

		}

		if (!model.infoBanner.empty() && !layout.authStatusBannerBesideLogo)
		{
			// Aligner le fond avec le texte de la bannière (AuthGlyphPass : panelY + topOffset - 38).
			// L'ancien hardcode panelY+72 plaçait le fond dans la zone du titre, créant un trait parasite.
			const int32_t bannerBgY = panelY + layout.topOffset - 42;
			addThemeRect(contentX, bannerBgY, contentW, 34, theme.secondary, 0.72f);
			// Banner text bar removed to avoid "horizontal line" artifacts
		}

		if (state.submitting)
		{
			const int32_t submitY = panelY + (!model.infoBanner.empty() ? 128 : 118);
			addThemeRect(contentX, submitY, contentW, 66, theme.surface, 0.98f);
			addThemeRect(contentX + 18, submitY + 20, contentW - 36, 10, theme.primary, 0.94f);
			addThemeRect(contentX + 18, submitY + 38, std::max(96, contentW - 92), 10, theme.accent, 0.94f);
			addThemeRect(contentX + 18, submitY + 88, contentW - 36, 38, theme.surface, 0.95f);
			return layers;
		}

		if (!model.errorText.empty())
		{
			const int32_t boxTop = panelY + layout.authErrorBoxTopFromPanelTopPx;
			const int32_t boxH = std::max(48, layout.authErrorBoxHeightPx);
			const int32_t footerTop = panelY + layout.authErrorFooterTopFromPanelTopPx;
			constexpr int32_t kErrorFooterBarH = 58;
			addRect(contentX, boxTop, contentW, boxH, 0.28f, 0.10f, 0.10f, 0.98f);
			addRect(contentX, boxTop, 10, boxH, 0.82f, 0.22f, 0.18f, 1.0f);
			addThemeRect(contentX, footerTop, contentW, kErrorFooterBarH, theme.surface, 0.98f);
			if (!model.actions.empty())
			{
				const int32_t actionCount = std::max<int32_t>(1, static_cast<int32_t>(model.actions.size()));
				const int32_t gap = 10;
				const int32_t actionW = std::max(120, (contentW - (actionCount - 1) * gap) / actionCount);
				const int32_t btnY = footerTop + (kErrorFooterBarH - kAuthUiActionButtonHeightPx) / 2;
				for (int32_t i = 0; i < actionCount; ++i)
				{
					if (static_cast<size_t>(i) >= model.actions.size())
					{
						break;
					}
					const int32_t x = contentX + i * (actionW + gap);
					const bool primary = model.actions[static_cast<size_t>(i)].primary;
					const bool emphasized = model.actions[static_cast<size_t>(i)].emphasized;
					const bool hovered = model.actions[static_cast<size_t>(i)].hovered;
					addAuthActionButton(x, btnY, actionW, kAuthUiActionButtonHeightPx, primary, emphasized, hovered);
				}
			}
			return layers;
		}

		if (state.terms)
		{
			const int32_t termsBoxY = panelY + 92;
			const int32_t termsBoxH = std::max(180, panelH - 210);
			addThemeRect(contentX, termsBoxY, contentW, termsBoxH, theme.background, 0.98f);
			addThemeRect(contentX + 18, termsBoxY + 16, contentW - 36, 18, theme.surface, 0.95f);
			addThemeRect(contentX + contentW - 12, termsBoxY + 28, 4, std::max(80, panelH - 248), theme.border, 0.98f);
			addThemeRect(contentX + contentW - 12, termsBoxY + 64, 4, 92, theme.accent, 1.0f);
			const int32_t actionCount = std::max<int32_t>(1, static_cast<int32_t>(model.actions.size()));
			const int32_t gapTerms = 10;
			const int32_t actionW = std::max(110, (contentW - (actionCount - 1) * gapTerms) / actionCount);
			const int32_t footerBarY = panelY + panelH - 92;
			const int32_t footerBarH = 58;
			addThemeRect(contentX, footerBarY, contentW, footerBarH, theme.surface, 0.98f);
			const int32_t termsBtnY = footerBarY + (footerBarH - kAuthUiActionButtonHeightPx) / 2;
			for (int32_t i = 0; i < actionCount; ++i)
			{
				const int32_t x = contentX + i * (actionW + gapTerms);
				const bool primary = static_cast<size_t>(i) < model.actions.size() ? model.actions[static_cast<size_t>(i)].primary : false;
				const bool emphasized = static_cast<size_t>(i) < model.actions.size() ? model.actions[static_cast<size_t>(i)].emphasized : false;
				const bool hovered = static_cast<size_t>(i) < model.actions.size() ? model.actions[static_cast<size_t>(i)].hovered : false;
				addAuthActionButton(x, termsBtnY, actionW, kAuthUiActionButtonHeightPx, primary, emphasized, hovered);
			}
			int32_t metaY = termsBoxY + 18;
			metaY += std::min<int32_t>(4, static_cast<int32_t>(model.bodyLines.size())) * 16;
			if (model.bodyLines.size() >= 7u)
			{
				const bool activeAck = model.bodyLines[6].active;
				if (activeAck)
				{
					const int32_t ackY = panelY + panelH - 136;
					addThemeRect(contentX + 8, ackY, 4, 18, theme.accent, 0.75f);
				}
			}
			return layers;
		}

		const int32_t fieldCount = std::max<int32_t>(0, static_cast<int32_t>(model.fields.size()));
		const int32_t topOffset = layout.topOffset;

		const int32_t fieldRowStep = layout.fieldRowStepPx;

		// Calcul des lignes logiques pour rendu grille.
		// Chaque fois que gridColumn revient à 0 (ou un champ sans grille), c'est une nouvelle ligne.
		std::vector<int32_t> fieldLogicalRow(static_cast<size_t>(fieldCount), 0);
		{
			int32_t row = -1;
			int32_t lastCol = kAuthUiGridColumns; // force une nouvelle ligne pour le premier champ
			for (int32_t i = 0; i < fieldCount; ++i)
			{
				const auto& f = (model.fields.size() > static_cast<size_t>(i))
				    ? model.fields[static_cast<size_t>(i)] : engine::client::AuthUiPresenter::RenderField{};
				if (f.gridColumn < 0 || f.gridColumn <= lastCol)
				    ++row;
				lastCol = (f.gridColumn < 0) ? kAuthUiGridColumns : f.gridColumn;
				fieldLogicalRow[static_cast<size_t>(i)] = row;
			}
		}

		for (int32_t i = 0; i < fieldCount; ++i)
		{
			const auto& field = (static_cast<size_t>(i) < model.fields.size())
			    ? model.fields[static_cast<size_t>(i)] : engine::client::AuthUiPresenter::RenderField{};

			// Position grille
			int32_t fieldX = contentX;
			int32_t fieldW = contentW;
			if (field.gridColumn >= 0)
			{
				AuthUiGridFieldGeometry(contentX, contentW, field.gridColumn, field.gridSpan, fieldX, fieldW);
			}
			const int32_t y = panelY + topOffset + fieldLogicalRow[static_cast<size_t>(i)] * fieldRowStep;

			addThemeRect(fieldX, y, fieldW, kAuthUiFieldBoxHeightPx, theme.surface, 0.98f);
			const bool activeField = field.active;
			const bool hoveredField = field.hovered;
			// Survol : plus de rectangle semi-opaque sur tout le champ (effet « bande » / ligne qui masque le texte).
			// Léger voile + barre gauche plus marquée ; le texte reste dessiné par-dessus (AuthGlyphPass).
			if (hoveredField && !activeField)
			{
				addThemeRect(fieldX, y, fieldW, kAuthUiFieldBoxHeightPx, theme.primary, 0.045f);
			}
			const int32_t stripeW = (hoveredField && !activeField) ? 7 : 5;
			const float sr = activeField ? 0.72f : (hoveredField ? 0.52f : 0.26f);
			const float sg = activeField ? 0.58f : (hoveredField ? 0.48f : 0.38f);
			const float sb = activeField ? 0.24f : (hoveredField ? 0.30f : 0.68f);
			addRect(fieldX, y, stripeW, kAuthUiFieldBoxHeightPx, sr, sg, sb, 1.0f);

			// Indicateur correspondance mots de passe
			if (field.passwordMatchState != 0)
			{
				const float r = (field.passwordMatchState > 0) ? 0.3f : 0.85f;
				const float g = (field.passwordMatchState > 0) ? 0.72f : 0.25f;
				const float b = (field.passwordMatchState > 0) ? 0.42f : 0.25f;
				addRect(fieldX, y + kAuthUiFieldBoxHeightPx - 2, fieldW, 2, r, g, b, 1.0f);
			}

			// Plan C : indicateur disponibilite username (Idle=0 = pas de barre).
			if (field.usernameCheckState != 0)
			{
				constexpr int32_t kBarH = 2;
				const int32_t barY = y + kAuthUiFieldBoxHeightPx - kBarH;
				// 1=Pending->gris, 2=Available->vert, 3=Taken->rouge
				float ur = 0.53f, ug = 0.53f, ub = 0.53f;
				if (field.usernameCheckState == 2) { ur = 0.13f; ug = 0.80f; ub = 0.27f; }
				if (field.usernameCheckState == 3) { ur = 0.80f; ug = 0.13f; ub = 0.13f; }
				addRect(fieldX, barY, fieldW, kBarH, ur, ug, ub, 1.0f);
			}

			if (!field.tooltipText.empty())
			{
				const int32_t bodyScaleHint = AuthUiClassicTextScaleFromPanelW(panelW);
				const int32_t smallScaleHint = std::max(2, bodyScaleHint - 1);
				const int32_t labelAboveFieldPx = smallScaleHint * 11 + 6;
				const int32_t iconX = std::max(fieldX + 10, fieldX + fieldW - 36);
				const int32_t iconY = y - labelAboveFieldPx;
				addThemeRect(iconX, iconY, 18, 18, theme.accent, model.hoveredFieldInfoIndex == i ? 1.0f : 0.65f);
			}
		}

		const int32_t bodyScaleMetrics = std::clamp(panelW / 260, 2, 4);
		const int32_t bodyLineStepMetrics = 7 * bodyScaleMetrics + 2 * bodyScaleMetrics;
		const bool centeredLanguageSelection = state.languageSelection || state.languageOptions;
		const int32_t bodyLinePitch = centeredLanguageSelection
			? std::max(36, bodyLineStepMetrics + 16)
			: std::max(28, bodyLineStepMetrics + 10);
		const int32_t afterFieldsGap = centeredLanguageSelection ? 34 : 18;
		const int32_t logicalRowCount = fieldCount > 0
		    ? fieldLogicalRow[static_cast<size_t>(fieldCount - 1)] + 1
		    : 0;
		const int32_t bodyStartY = panelY + topOffset + logicalRowCount * fieldRowStep + afterFieldsGap;
		for (int32_t localIdx = 0; localIdx < model.visibleBodyLineCount; ++localIdx)
		{
			const int32_t bodyIndex = model.visibleBodyLineStart + localIdx;
			const int32_t y = bodyStartY + localIdx * bodyLinePitch - 4;
			const bool activeBodyLine = static_cast<size_t>(bodyIndex) < model.bodyLines.size() ? model.bodyLines[static_cast<size_t>(bodyIndex)].active : false;
			const bool hoveredBodyLine = static_cast<size_t>(bodyIndex) < model.bodyLines.size() ? model.bodyLines[static_cast<size_t>(bodyIndex)].hovered : false;
			const bool checkboxLine = static_cast<size_t>(bodyIndex) < model.bodyLines.size() && model.bodyLines[static_cast<size_t>(bodyIndex)].checkbox;
			if (checkboxLine)
			{
				const bool checked = model.bodyLines[static_cast<size_t>(bodyIndex)].checkboxChecked;
				const int32_t cbx = contentX + 2;
				const int32_t cby = y - 5;
				const int32_t outer = kAuthUiCheckboxOuterPx;
				addThemeRect(cbx, cby, outer, outer, theme.surface, 0.98f);
				addThemeRect(cbx, cby, outer, 2, theme.border, 0.85f);
				addThemeRect(cbx, cby + outer - 2, outer, 2, theme.border, 0.85f);
				addThemeRect(cbx, cby, 2, outer, theme.border, 0.85f);
				addThemeRect(cbx + outer - 2, cby, 2, outer, theme.border, 0.85f);
				if (checked)
				{
					addThemeRect(cbx + 3, cby + 3, outer - 6, outer - 6, theme.accent, 0.52f);
					// Coche claire (forme en L) plus lisible qu’un simple carré plein.
					addRect(cbx + 5, cby + 7, 3, 8, 0.96f, 0.94f, 0.82f, 1.0f);
					addRect(cbx + 6, cby + 12, 9, 3, 0.96f, 0.94f, 0.82f, 1.0f);
				}
				// Survol / focus : marqueur vertical uniquement (pas de bandeau horizontal sur le libellé).
				if (activeBodyLine || hoveredBodyLine)
				{
					const int32_t markX = contentX + kAuthUiCheckboxLabelOffsetX - 5;
					addThemeRect(markX, y - 5, 3, 16, theme.accent, activeBodyLine ? 0.95f : 0.55f);
				}
			}
			else
			{
				if (activeBodyLine)
				{
					addRect(contentX - 4, y - 6, 3, 14, 0.86f, 0.65f, 0.22f, 1.0f);
				}
				else if (hoveredBodyLine)
				{
					addThemeRect(contentX - 4, y - 4, 3, 12, theme.accent, 0.55f);
				}
			}
		}

		// Boutons : uniquement fond / bordure (pas de texte). Les libellés sont dessinés par AuthGlyphPass à partir du modèle i18n.
		const int32_t actionCount = std::max<int32_t>(1, static_cast<int32_t>(model.actions.size()));
		const int32_t gap = 10;
		AuthLoginTwoRowLayout loginTwoRow{};
		const bool loginTwoRows = TryGetLoginTwoRowLayout(layout, state, model, loginTwoRow);
		if (loginTwoRows)
		{
			for (int32_t row = 0; row < 2; ++row)
			{
				const int32_t rowY = (row == 0) ? loginTwoRow.secondaryRowY : loginTwoRow.primaryRowY;
				int32_t colX = contentX;
				for (int32_t col = 0; col < 2; ++col)
				{
					const int32_t i = row * 2 + col;
					if (i >= actionCount)
					{
						break;
					}
					int32_t btnW = loginTwoRow.buttonHalfWidth;
					int32_t x = contentX + col * (loginTwoRow.buttonHalfWidth + gap);
					if (row == 1)
					{
						btnW = (col == 0) ? loginTwoRow.primarySubmitWidth : loginTwoRow.primaryQuitWidth;
						x = (col == 0) ? contentX : (contentX + loginTwoRow.primarySubmitWidth + gap);
					}
					const bool primary = model.actions[static_cast<size_t>(i)].primary;
					const bool emphasized = model.actions[static_cast<size_t>(i)].emphasized;
					const bool hovered = model.actions[static_cast<size_t>(i)].hovered;
					addAuthActionButton(x, rowY, btnW, kAuthUiActionButtonHeightPx, primary, emphasized, hovered);
				}
			}
		}
		else
		{
			const int32_t buttonPadAfterBody = centeredLanguageSelection ? 28 : 20;
			const int32_t buttonY = std::min(panelY + panelH - 86,
				bodyStartY + model.visibleBodyLineCount * bodyLinePitch + buttonPadAfterBody);
			const int32_t actionW = std::max(100, (contentW - (actionCount - 1) * gap) / actionCount);
			for (int32_t i = 0; i < actionCount; ++i)
			{
				const int32_t x = contentX + i * (actionW + gap);
				const bool primary = static_cast<size_t>(i) < model.actions.size() ? model.actions[static_cast<size_t>(i)].primary : (i == 0);
				const bool emphasized = static_cast<size_t>(i) < model.actions.size() ? model.actions[static_cast<size_t>(i)].emphasized : false;
				const bool hovered = static_cast<size_t>(i) < model.actions.size() ? model.actions[static_cast<size_t>(i)].hovered : false;
				addAuthActionButton(x, buttonY, actionW, kAuthUiActionButtonHeightPx, primary, emphasized, hovered);
			}
		}

		if (calibrationOverlay)
		{
			// Overlay volontairement minimal : uniquement un carré magenta au centre.
			// Objectif: diagnostique d’orientation sans “bordure” visible sur l’UI.
			const int32_t box = std::clamp(std::min(w, h) / 10, 48, 120);
			const int32_t cx = (w - box) / 2;
			const int32_t cy = (h - box) / 2;
			addRect(cx, cy, box, box, 1.0f, 0.0f, 1.0f, 1.0f);
		}

		return layers;
	}

	std::vector<AuthFieldInfoIconLayout> BuildAuthFieldInfoIconLayouts(
		VkExtent2D extent,
		const engine::client::AuthUiPresenter::VisualState& state,
		const engine::client::AuthUiPresenter::RenderModel& model)
	{
		std::vector<AuthFieldInfoIconLayout> out;
		if (extent.width == 0 || extent.height == 0 || !model.visible)
		{
			return out;
		}

		const AuthUiLayoutMetrics layout = BuildAuthUiLayoutMetrics(extent, state, model);
		const int32_t panelY = layout.panelY;
		const int32_t contentX = layout.contentX;
		const int32_t contentW = layout.contentW;
		const int32_t topOffset = layout.topOffset;
		const int32_t fieldStep = layout.fieldRowStepPx;
		const int32_t bodyScale = AuthUiClassicTextScaleFromPanelW(layout.panelW);
		const int32_t smallScale = std::max(2, bodyScale - 1);
		const int32_t labelAboveFieldPx = smallScale * 11 + 6;

		out.resize(model.fields.size());
		for (size_t i = 0; i < model.fields.size(); ++i)
		{
			if (model.fields[i].tooltipText.empty())
			{
				continue;
			}
			const int32_t y = panelY + topOffset + static_cast<int32_t>(i) * fieldStep;
			const int32_t ix = std::max(contentX + 10, contentX + contentW - 36);
			const int32_t iy = y - labelAboveFieldPx;
			out[i].valid = true;
			out[i].centerXPx = static_cast<float>(ix + 9);
			out[i].centerYPx = static_cast<float>(iy + 9);
			out[i].halfExtentPx = 9.f;
		}
		return out;
	}
}
