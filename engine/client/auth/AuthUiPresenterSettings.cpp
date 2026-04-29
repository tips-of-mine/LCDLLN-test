#include "engine/client/AuthUi.h"
#include "engine/render/AuthUiRenderer.h"
#include "engine/core/DefaultClientEndpoints.h"
#include "engine/core/Log.h"
#include "engine/network/CharacterPayloads.h"
#include "engine/network/NetClient.h"
#include "engine/network/PacketBuilder.h"
#include "engine/network/ProtocolV1Constants.h"
#include "engine/platform/FileSystem.h"
#include "engine/platform/Window.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace engine::client
{
#if defined(_WIN32)
	AuthUiPresenter::LanguageOptionsImGuiMirror AuthUiPresenter::BuildLanguageOptionsImGuiMirror() const
	{
		LanguageOptionsImGuiMirror m{};
		m.videoFullscreen = m_videoFullscreenPending;
		m.videoVsync = m_videoVsyncPending;
		m.videoResWidth = m_videoResWidthPending;
		m.videoResHeight = m_videoResHeightPending;
		m.videoQualityPreset = m_videoQualityPresetPending;
		m.videoFovDegrees = m_videoFovDegreesPending;
		m.audioMaster01 = m_audioMasterVolumePending;
		m.audioMusic01 = m_audioMusicVolumePending;
		m.audioSfx01 = m_audioSfxVolumePending;
		m.audioUi01 = m_audioUiVolumePending;
		m.mouseSensitivity = m_mouseSensitivityPending;
		m.invertY = m_invertYPending;
		m.useZqsd = m_useZqsdPending;
		m.gameplayUdpEnabled = m_gameplayUdpEnabledPending;
		m.allowInsecureDev = m_allowInsecureDevPending;
		m.authTimeoutMs = m_authTimeoutMsPending;
		m.languageSelectionIndex = m_languageSelectionIndex;
		m.uiScalePercent = m_uiScalePercentPending;
		m.panelOpacityPercent = m_panelOpacityPercentPending;
		m.showTooltipUi = m_showTooltipUiPending;
		m.preferredServerIndex = m_preferredServerIndexPending;
		return m;
	}

	void AuthUiPresenter::ImGuiApplyLanguageOptionsMenu(const engine::core::Config& cfg, const LanguageOptionsImGuiMirror& mirror)
	{
		if (m_phase != Phase::LanguageOptions)
		{
			return;
		}
		const auto& locales = m_localization.GetAvailableLocales();
		if (!locales.empty())
		{
			const uint32_t maxIdx = static_cast<uint32_t>(locales.size() - 1u);
			m_languageSelectionIndex = mirror.languageSelectionIndex > maxIdx ? maxIdx : mirror.languageSelectionIndex;
		}
		m_videoFullscreenPending = mirror.videoFullscreen;
		m_videoVsyncPending = mirror.videoVsync;
		m_videoResWidthPending = mirror.videoResWidth > 0 ? mirror.videoResWidth : m_videoResWidthPending;
		m_videoResHeightPending = mirror.videoResHeight > 0 ? mirror.videoResHeight : m_videoResHeightPending;
		m_videoQualityPresetPending = static_cast<int32_t>(std::clamp<int32_t>(mirror.videoQualityPreset, 0, 3));
		m_videoFovDegreesPending = std::clamp(mirror.videoFovDegrees, 60.f, 120.f);
		m_audioMasterVolumePending = mirror.audioMaster01;
		m_audioMusicVolumePending = mirror.audioMusic01;
		m_audioSfxVolumePending = mirror.audioSfx01;
		m_audioUiVolumePending = mirror.audioUi01;
		m_mouseSensitivityPending = mirror.mouseSensitivity;
		m_invertYPending = mirror.invertY;
		m_useZqsdPending = mirror.useZqsd;
		m_gameplayUdpEnabledPending = mirror.gameplayUdpEnabled;
		m_allowInsecureDevPending = mirror.allowInsecureDev;
		m_authTimeoutMsPending = std::clamp(mirror.authTimeoutMs, 2000u, 10000u);
		m_uiScalePercentPending = std::clamp(mirror.uiScalePercent, 80.f, 140.f);
		m_panelOpacityPercentPending = std::clamp(mirror.panelOpacityPercent, 40.f, 100.f);
		m_showTooltipUiPending = mirror.showTooltipUi;
		m_preferredServerIndexPending = mirror.preferredServerIndex > 2u ? 2u : mirror.preferredServerIndex;
		CommitLanguageOptionsMenuApply(cfg);
	}

	void AuthUiPresenter::ImGuiCloseLanguageOptionsWithoutApply()
	{
		if (m_phase != Phase::LanguageOptions)
		{
			return;
		}
		m_phase = m_phaseBeforeOptions;
		LOG_INFO(Core, "[AuthUiPresenter] ImGui: options fermees sans appliquer");
	}
	void AuthUiPresenter::OpenLanguageOptions()
	{
		const auto& locales = m_localization.GetAvailableLocales();
		if (locales.empty())
		{
			LOG_WARN(Core, "[AuthUiPresenter] OpenLanguageOptions ignored: no locales");
			return;
		}
		m_phaseBeforeOptions = m_phase;
		SetPhase(Phase::LanguageOptions);
		m_selectedLocale = CurrentLocale();
		m_videoFullscreenPending = m_videoFullscreen;
		m_videoVsyncPending = m_videoVsync;
		m_videoResWidthPending = m_videoResWidth;
		m_videoResHeightPending = m_videoResHeight;
		m_videoQualityPresetPending = m_videoQualityPreset;
		m_videoFovDegreesPending = m_videoFovDegrees;
		m_audioMasterVolumePending = m_audioMasterVolume;
		m_audioMusicVolumePending = m_audioMusicVolume;
		m_audioSfxVolumePending = m_audioSfxVolume;
		m_audioUiVolumePending = m_audioUiVolume;
		m_mouseSensitivityPending = m_mouseSensitivity;
		m_invertYPending = m_invertY;
		m_useZqsdPending = m_useZqsd;
		m_gameplayUdpEnabledPending = m_gameplayUdpEnabled;
		m_allowInsecureDevPending = m_allowInsecureDev;
		m_authTimeoutMsPending = m_authTimeoutMs;
		m_uiScalePercentPending = m_uiScalePercent;
		m_panelOpacityPercentPending = m_panelOpacityPercent;
		m_showTooltipUiPending = m_showTooltipUi;
		m_preferredServerIndexPending = m_preferredServerIndex;
		m_optionsSubMenu = OptionsSubMenu::Root;
		m_optionsRootSelection = 0;
		m_optionsSubSelection = 0;
		auto it = std::find(locales.begin(), locales.end(), m_selectedLocale);
		m_languageSelectionIndex = it != locales.end() ? static_cast<uint32_t>(std::distance(locales.begin(), it)) : 0u;
		LOG_INFO(Core, "[AuthUiPresenter] Options opened (locale={}, fullscreen={}, vsync={}, sens={:.4f}, invert_y={}, layout={}, gameplay_udp={}, allow_insecure_dev={}, timeout_ms={})",
			m_selectedLocale, m_videoFullscreenPending, m_videoVsyncPending,
			m_mouseSensitivityPending, m_invertYPending, m_useZqsdPending ? "zqsd" : "wasd",
			m_gameplayUdpEnabledPending, m_allowInsecureDevPending, m_authTimeoutMsPending);
	}

	uint32_t AuthUiPresenter::OptionsSubmenuLineCount(OptionsSubMenu sub)
	{
		switch (sub)
		{
		case OptionsSubMenu::Root:
			return 0;
		case OptionsSubMenu::Language:
			return 2;
		case OptionsSubMenu::Video:
			return 2;
		case OptionsSubMenu::Audio:
			return 4;
		case OptionsSubMenu::Controls:
			return 3;
		case OptionsSubMenu::Game:
			return 3;
		default:
			return 0;
		}
	}

	void AuthUiPresenter::EnterOptionsSubmenuFromRoot(uint32_t categoryIndex)
	{
		switch (categoryIndex)
		{
		case 0:
			m_optionsSubMenu = OptionsSubMenu::Language;
			break;
		case 1:
			m_optionsSubMenu = OptionsSubMenu::Video;
			break;
		case 2:
			m_optionsSubMenu = OptionsSubMenu::Audio;
			break;
		case 3:
			m_optionsSubMenu = OptionsSubMenu::Controls;
			break;
		case 4:
			m_optionsSubMenu = OptionsSubMenu::Game;
			break;
		default:
			return;
		}
		m_optionsSubSelection = 0;
	}

	void AuthUiPresenter::CommitLanguageOptionsMenuApply(const engine::core::Config& cfg)
	{
		(void)cfg;
		m_videoFullscreen = m_videoFullscreenPending;
		m_videoVsync = m_videoVsyncPending;
		m_videoResWidth = m_videoResWidthPending;
		m_videoResHeight = m_videoResHeightPending;
		m_videoQualityPreset = m_videoQualityPresetPending;
		m_videoFovDegrees = m_videoFovDegreesPending;
		m_audioMasterVolume = m_audioMasterVolumePending;
		m_audioMusicVolume = m_audioMusicVolumePending;
		m_audioSfxVolume = m_audioSfxVolumePending;
		m_audioUiVolume = m_audioUiVolumePending;
		m_mouseSensitivity = m_mouseSensitivityPending;
		m_invertY = m_invertYPending;
		m_useZqsd = m_useZqsdPending;
		m_gameplayUdpEnabled = m_gameplayUdpEnabledPending;
		m_allowInsecureDev = m_allowInsecureDevPending;
		m_authTimeoutMs = m_authTimeoutMsPending;
		m_uiScalePercent = m_uiScalePercentPending;
		m_panelOpacityPercent = m_panelOpacityPercentPending;
		m_showTooltipUi = m_showTooltipUiPending;
		m_preferredServerIndex = m_preferredServerIndexPending;
		m_pendingVideoSettings.applyRequested = true;
		m_pendingVideoSettings.fullscreen = m_videoFullscreen;
		m_pendingVideoSettings.vsync = m_videoVsync;
		m_pendingVideoSettings.resolutionWidth = m_videoResWidth;
		m_pendingVideoSettings.resolutionHeight = m_videoResHeight;
		m_pendingVideoSettings.qualityPreset = m_videoQualityPreset;
		m_pendingVideoSettings.fovDegrees = m_videoFovDegrees;
		m_pendingAudioSettings.applyRequested = true;
		m_pendingAudioSettings.masterVolume = m_audioMasterVolume;
		m_pendingAudioSettings.musicVolume = m_audioMusicVolume;
		m_pendingAudioSettings.sfxVolume = m_audioSfxVolume;
		m_pendingAudioSettings.uiVolume = m_audioUiVolume;
		m_pendingControlSettings.applyRequested = true;
		m_pendingControlSettings.mouseSensitivity = m_mouseSensitivity;
		m_pendingControlSettings.invertY = m_invertY;
		m_pendingControlSettings.useZqsd = m_useZqsd;
		m_pendingGameSettings.applyRequested = true;
		m_pendingGameSettings.gameplayUdpEnabled = m_gameplayUdpEnabled;
		m_pendingGameSettings.allowInsecureDev = m_allowInsecureDev;
		m_pendingGameSettings.authTimeoutMs = m_authTimeoutMs;
		ApplyLocaleSelection(false);
	}
#endif

	AuthUiPresenter::VideoSettingsCommand AuthUiPresenter::ConsumePendingVideoSettings()
	{
		const VideoSettingsCommand cmd = m_pendingVideoSettings;
		m_pendingVideoSettings = {};
		return cmd;
	}

	AuthUiPresenter::EnterWorldCommand AuthUiPresenter::ConsumePendingEnterWorldCommand()
	{
		EnterWorldCommand cmd = m_pendingEnterWorld;
		m_pendingEnterWorld = {};
		return cmd;
	}

	bool AuthUiPresenter::SavePositionAsync(uint64_t characterId, float x, float y, float z, float yawDeg, float pitchDeg)
	{
		if (!m_masterClient || m_masterSessionId == 0u || characterId == 0u)
		{
			return false;
		}

		// Construit le paquet CHARACTER_SAVE_POSITION_REQUEST en sérialisant le payload
		// puis en l'enveloppant dans un PacketBuilder. requestId = 0 (fire-and-forget) :
		// la réponse master arrive en PollEvents sur m_masterClient mais on ne fait pas
		// le matching, on s'en remet aux logs côté serveur en cas d'erreur.
		const std::vector<uint8_t> payload = engine::network::BuildCharacterSavePositionRequestPayload(
			characterId, x, y, z, yawDeg, pitchDeg);
		if (payload.empty())
		{
			LOG_WARN(Net, "[AuthUiPresenter] SavePositionAsync: Build payload failed");
			return false;
		}

		engine::network::PacketBuilder builder;
		auto w = builder.PayloadWriter();
		if (!w.WriteBytes(payload.data(), payload.size()))
		{
			LOG_WARN(Net, "[AuthUiPresenter] SavePositionAsync: WriteBytes failed");
			return false;
		}
		if (!builder.Finalize(engine::network::kOpcodeCharacterSavePositionRequest, 0u, 0u, m_masterSessionId, payload.size()))
		{
			LOG_WARN(Net, "[AuthUiPresenter] SavePositionAsync: Finalize failed");
			return false;
		}
		const auto& packet = builder.Data();
		if (packet.empty())
		{
			LOG_WARN(Net, "[AuthUiPresenter] SavePositionAsync: empty packet");
			return false;
		}
		if (!m_masterClient->Send(std::span<const uint8_t>(packet.data(), packet.size())))
		{
			LOG_WARN(Net, "[AuthUiPresenter] SavePositionAsync: Send failed (queue full / disconnected ?)");
			return false;
		}
		LOG_DEBUG(Net, "[AuthUiPresenter] SavePositionAsync queued (character_id={}, pos=({:.1f},{:.1f},{:.1f}))",
			characterId, x, y, z);
		return true;
	}

	void AuthUiPresenter::PumpPostAuthEvents()
	{
		if (!m_masterClient)
			return;
		auto events = m_masterClient->PollEvents();
		for (const auto& ev : events)
		{
			using engine::network::NetClientEventType;
			if (ev.type == NetClientEventType::Disconnected)
			{
				LOG_INFO(Net, "[AuthUiPresenter] Master connection disconnected post-auth (reason='{}')", ev.reason);
				// La session est morte ; on libère pour que les futurs SavePositionAsync échouent
				// proprement (au lieu de Send sur une socket fermée).
				m_masterSessionId = 0;
				m_masterClient.reset();
				return;
			}
			// PacketReceived (réponses SAVE_POSITION par exemple) : on log au niveau debug
			// et on ne fait pas de matching de request_id (fire-and-forget).
			if (ev.type == NetClientEventType::PacketReceived)
			{
				LOG_DEBUG(Net, "[AuthUiPresenter] Master post-auth packet received ({} bytes)", ev.packet.size());
			}
		}
	}

	AuthUiPresenter::AudioSettingsCommand AuthUiPresenter::ConsumePendingAudioSettings()
	{
		const AudioSettingsCommand cmd = m_pendingAudioSettings;
		m_pendingAudioSettings = {};
		return cmd;
	}

	AuthUiPresenter::ControlSettingsCommand AuthUiPresenter::ConsumePendingControlSettings()
	{
		const ControlSettingsCommand cmd = m_pendingControlSettings;
		m_pendingControlSettings = {};
		return cmd;
	}

	AuthUiPresenter::GameSettingsCommand AuthUiPresenter::ConsumePendingGameSettings()
	{
		const GameSettingsCommand cmd = m_pendingGameSettings;
		m_pendingGameSettings = {};
		return cmd;
	}

}
