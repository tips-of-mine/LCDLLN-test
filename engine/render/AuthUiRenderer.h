#pragma once

#include "engine/client/AuthUi.h"
#include "engine/core/Config.h"

#include <vulkan/vulkan.h>

#include <vector>

namespace engine::render
{
	struct AuthUiLayer
	{
		VkClearColorValue color{};
		VkClearRect rect{};
	};

	struct AuthUiTheme
	{
		float primary[4]{ 0.23f, 0.43f, 0.65f, 1.0f };
		float secondary[4]{ 0.35f, 0.50f, 0.66f, 1.0f };
		float accent[4]{ 0.85f, 0.64f, 0.25f, 1.0f };
		float background[4]{ 0.06f, 0.09f, 0.11f, 1.0f };
		float surface[4]{ 0.09f, 0.13f, 0.17f, 1.0f };
		float panel[4]{ 0.10f, 0.16f, 0.21f, 1.0f };
		float text[4]{ 0.91f, 0.93f, 0.95f, 1.0f };
		float mutedText[4]{ 0.66f, 0.71f, 0.76f, 1.0f };
		float border[4]{ 0.19f, 0.27f, 0.34f, 1.0f };
	};

	struct AuthUiLayoutMetrics
	{
		int32_t panelW = 0;
		int32_t panelH = 0;
		int32_t panelX = 0;
		int32_t panelY = 0;
		int32_t innerX = 0;
		int32_t artW = 0;
		int32_t contentX = 0;
		int32_t contentW = 0;
		int32_t topOffset = 0;
		bool largeContent = false;
		bool compactSingleField = false;
	};

	AuthUiTheme LoadAuthUiTheme(const engine::core::Config& cfg);

	AuthUiLayoutMetrics BuildAuthUiLayoutMetrics(
		VkExtent2D extent,
		const engine::client::AuthUiPresenter::VisualState& state,
		const engine::client::AuthUiPresenter::RenderModel& model);

	/// Si \p calibrationOverlay est true, ajoute des bandes de référence (rouge=haut, vert=bas,
	/// bleu=gauche, jaune=droite, magenta=centre) pour diagnostiquer l’orientation sur une capture.
	/// Si \p usePhotoBackdrop est true, n’applique pas les grands aplats plein écran (fond déjà dessiné par blit).
	std::vector<AuthUiLayer> BuildAuthUiLayers(
		VkExtent2D extent,
		const engine::client::AuthUiPresenter::VisualState& state,
		const engine::client::AuthUiPresenter::RenderModel& model,
		const AuthUiTheme& theme,
		bool calibrationOverlay = false,
		bool usePhotoBackdrop = false);
}
