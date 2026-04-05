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
		metrics.panelH = metrics.largeContent ? std::clamp(h * 74 / 100, 440, std::max(780, h * 82 / 100)) : std::clamp(h * 62 / 100, 380, std::max(660, h * 72 / 100));
		metrics.panelX = (w - metrics.panelW) / 2;
		metrics.panelY = (h - metrics.panelH) / 2;
		metrics.innerX = metrics.panelX + 28;
		metrics.artW = std::clamp(metrics.panelW / 3, 150, 240);
		if (state.login && state.minimalChrome && !state.loginArtColumn)
		{
			metrics.artW = 0;
		}
		{
			const int32_t artGap = metrics.artW > 0 ? 18 : 8;
			metrics.contentX = metrics.innerX + metrics.artW + artGap;
			metrics.contentW = std::max(180, metrics.panelW - (metrics.contentX - metrics.panelX) - 28);
		}
		metrics.compactSingleField = model.fields.size() <= 1u;
		{
			const int32_t bodyScale = std::clamp(metrics.panelW / 260, 2, 4);
			const int32_t bodyLineStep = 7 * bodyScale + 2 * bodyScale;
			const int32_t compactFieldStep = std::max(48, bodyLineStep + 18);
			const int32_t regularFieldStep = std::max(42, bodyLineStep + 14);
			metrics.fieldRowStepPx = metrics.compactSingleField ? compactFieldStep : regularFieldStep;
		}
		// Aligner le premier champ sous le bloc titre + section (RecordModel). Sur grands panels,
		// titleScale/bodyScale augmentent : l’ancien offset fixe (104) faisait chevaucher labels et titres.
		{
			const int32_t bodyScale = std::clamp(metrics.panelW / 260, 2, 4);
			const int32_t titleScale = std::clamp(bodyScale + 1, 3, 5);
			const int32_t bodyLineStep = 7 * bodyScale + 2 * bodyScale;
			const int32_t sectionTopPad = (state.languageSelection || state.languageOptions) ? 50 : 38;
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
			// Pas de addRect plein écran ici : vkCmdClearAttachments remplace les pixels (pas de blending).
			// Un voile plein écran effaçait entièrement le blit du PNG → fond bleu-gris uni.
			// On ne garde que des vignettes sur les bords pour foncer légèrement les marges.
			const int32_t vg = std::clamp(w / 40, 18, 56);
			const float ve = 0.03f;
			const float vn = 0.05f;
			const float vb = 0.10f;
			addRect(0, 0, vg, h, ve, vn, vb, 0.14f);
			addRect(w - vg, 0, vg, h, ve, vn, vb, 0.14f);
			addRect(0, 0, w, vg / 2, ve, vn, vb, 0.10f);
			addRect(0, h - vg / 2, w, vg / 2, ve, vn, vb, 0.12f);
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

		if (!model.infoBanner.empty())
		{
			// Aligner le fond avec le texte de la bannière (AuthGlyphPass : panelY + topOffset - 38).
			// L'ancien hardcode panelY+72 plaçait le fond dans la zone du titre, créant un trait parasite.
			const int32_t bannerBgY = panelY + layout.topOffset - 42;
			addThemeRect(contentX, bannerBgY, contentW, 34, theme.secondary, 0.72f);
			addThemeRect(contentX + 14, bannerBgY + 13, std::max(80, contentW - 28), 4, theme.text, 0.70f);
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
			addRect(contentX, panelY + 108, contentW, 84, 0.28f, 0.10f, 0.10f, 0.98f);
			addRect(contentX, panelY + 108, 10, 84, 0.82f, 0.22f, 0.18f, 1.0f);
			addThemeRect(contentX + 22, panelY + 126, contentW - 44, 12, theme.text, 0.78f);
			addThemeRect(contentX, panelY + 214, contentW, 58, theme.surface, 0.98f);
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
			const int32_t actionW = std::max(110, (contentW - (actionCount - 1) * 10) / actionCount);
			addThemeRect(contentX, panelY + panelH - 92, contentW, 58, theme.surface, 0.98f);
			for (int32_t i = 0; i < actionCount; ++i)
			{
				const int32_t x = contentX + i * (actionW + 10);
				const bool primary = static_cast<size_t>(i) < model.actions.size() ? model.actions[static_cast<size_t>(i)].primary : false;
				addThemeRect(x, panelY + panelH - 54, actionW, 28, primary ? theme.primary : theme.accent, 0.95f);
			}
			int32_t metaY = termsBoxY + 18;
			metaY += std::min<int32_t>(4, static_cast<int32_t>(model.bodyLines.size())) * 16;
			if (model.bodyLines.size() >= 7u)
			{
				const bool activeAck = model.bodyLines[6].active;
				if (activeAck)
				{
					addThemeRect(contentX + 8, panelY + panelH - 136, contentW - 16, 18, theme.accent, 0.16f);
				}
			}
			return layers;
		}

		const int32_t fieldCount = std::max<int32_t>(0, static_cast<int32_t>(model.fields.size()));
		const int32_t topOffset = layout.topOffset;

		const int32_t fieldRowStep = layout.fieldRowStepPx;
		for (int32_t i = 0; i < fieldCount; ++i)
		{
			const int32_t y = panelY + topOffset + i * fieldRowStep;
			addThemeRect(contentX, y, contentW, 32, theme.surface, 0.98f);
			const bool activeField = static_cast<size_t>(i) < model.fields.size() ? model.fields[static_cast<size_t>(i)].active : false;
			const bool hoveredField = static_cast<size_t>(i) < model.fields.size() ? model.fields[static_cast<size_t>(i)].hovered : false;
			if (hoveredField && !activeField)
			{
				addThemeRect(contentX, y, contentW, 32, theme.primary, 0.18f);
			}
			addRect(contentX, y, 5, 32,
				activeField ? 0.72f : 0.26f,
				activeField ? 0.58f : 0.38f,
				activeField ? 0.24f : 0.68f,
				1.0f);
			addThemeRect(contentX + 18, y + 11, std::max(60, contentW - 54 - i * 10), 3, theme.text, activeField ? 0.78f : (hoveredField ? 0.66f : 0.50f));
		}

		const int32_t bodyScaleMetrics = std::clamp(panelW / 260, 2, 4);
		const int32_t bodyLineStepMetrics = 7 * bodyScaleMetrics + 2 * bodyScaleMetrics;
		const int32_t bodyLinePitch = std::max(28, bodyLineStepMetrics + 10);
		const int32_t bodyStartY = panelY + topOffset + fieldCount * fieldRowStep + 18;
		for (int32_t localIdx = 0; localIdx < model.visibleBodyLineCount; ++localIdx)
		{
			const int32_t bodyIndex = model.visibleBodyLineStart + localIdx;
			const int32_t y = bodyStartY + localIdx * bodyLinePitch;
			const bool activeBodyLine = static_cast<size_t>(bodyIndex) < model.bodyLines.size() ? model.bodyLines[static_cast<size_t>(bodyIndex)].active : false;
			const bool hoveredBodyLine = static_cast<size_t>(bodyIndex) < model.bodyLines.size() ? model.bodyLines[static_cast<size_t>(bodyIndex)].hovered : false;
			const bool checkboxLine = static_cast<size_t>(bodyIndex) < model.bodyLines.size() && model.bodyLines[static_cast<size_t>(bodyIndex)].checkbox;
			if (checkboxLine)
			{
				const bool checked = model.bodyLines[static_cast<size_t>(bodyIndex)].checkboxChecked;
				addThemeRect(contentX + 2, y - 4, 16, 16, theme.surface, 0.98f);
				addThemeRect(contentX + 2, y - 4, 16, 2, theme.border, 0.85f);
				addThemeRect(contentX + 2, y + 10, 16, 2, theme.border, 0.85f);
				addThemeRect(contentX + 2, y - 4, 2, 16, theme.border, 0.85f);
				addThemeRect(contentX + 16, y - 4, 2, 16, theme.border, 0.85f);
				if (checked)
				{
					addThemeRect(contentX + 5, y - 1, 10, 10, theme.accent, 0.90f);
				}
				if (hoveredBodyLine || activeBodyLine)
				{
					addThemeRect(contentX + 22, y - 6, contentW - 28, 18, theme.primary, 0.10f);
				}
				addThemeRect(contentX + 22, y, std::max(80, contentW - 32), 4, theme.text,
					activeBodyLine ? 0.88f : (hoveredBodyLine ? 0.72f : 0.76f));
			}
			else
			{
				if (activeBodyLine)
				{
					addThemeRect(contentX - 4, y - 6, contentW, 14, theme.accent, 0.18f);
					addRect(contentX - 4, y - 6, 3, 14, 0.86f, 0.65f, 0.22f, 1.0f);
				}
				else if (hoveredBodyLine)
				{
					addThemeRect(contentX - 4, y - 6, contentW, 14, theme.primary, 0.12f);
				}
				addThemeRect(contentX, y, std::max(120, contentW - localIdx * 12), 4, theme.text,
					activeBodyLine ? 0.88f : (hoveredBodyLine ? 0.68f : (localIdx == 0 ? 0.76f : 0.46f)));
			}
		}

		const int32_t actionCount = std::max<int32_t>(1, static_cast<int32_t>(model.actions.size()));
		const int32_t gap = 10;
		AuthLoginTwoRowLayout loginTwoRow{};
		const bool loginTwoRows = TryGetLoginTwoRowLayout(layout, state, model, loginTwoRow);
		if (loginTwoRows)
		{
			for (int32_t row = 0; row < 2; ++row)
			{
				const int32_t rowY = (row == 0) ? loginTwoRow.secondaryRowY : loginTwoRow.primaryRowY;
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
					// Même couleur de fond pour les deux boutons secondaires (Inscription / Options).
					const float* fill = primary ? theme.primary : (emphasized ? theme.secondary : theme.surface);
					addThemeRect(x, rowY, btnW, 40, fill, hovered ? 1.0f : 0.94f);
					if (hovered)
					{
						addThemeRect(x, rowY, btnW, 3, theme.accent, 1.0f);
					}
					addThemeRect(x + 12, rowY + 14, std::max(48, btnW - 24), 4, theme.text,
						primary ? (hovered ? 0.78f : 0.64f) : (hovered ? 0.62f : 0.48f));
				}
			}
		}
		else
		{
			const int32_t buttonY = std::min(panelY + panelH - 84, bodyStartY + model.visibleBodyLineCount * bodyLinePitch + 20);
			const int32_t actionW = std::max(100, (contentW - (actionCount - 1) * gap) / actionCount);
			for (int32_t i = 0; i < actionCount; ++i)
			{
				const int32_t x = contentX + i * (actionW + gap);
				const bool primary = static_cast<size_t>(i) < model.actions.size() ? model.actions[static_cast<size_t>(i)].primary : (i == 0);
				const bool emphasized = static_cast<size_t>(i) < model.actions.size() ? model.actions[static_cast<size_t>(i)].emphasized : false;
				const bool hovered = static_cast<size_t>(i) < model.actions.size() ? model.actions[static_cast<size_t>(i)].hovered : false;
				const float* fill = primary ? theme.primary : (emphasized ? theme.accent : theme.surface);
				addThemeRect(x, buttonY, actionW, 42, fill, hovered ? 1.0f : 0.96f);
				if (hovered)
				{
					addThemeRect(x, buttonY, actionW, 3, theme.accent, 1.0f);
				}
				addThemeRect(x + 18, buttonY + 15, std::max(48, actionW - 36), 4, theme.text, primary ? (hovered ? 0.72f : 0.60f) : (hovered ? 0.58f : 0.42f));
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
}
