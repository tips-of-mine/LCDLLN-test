#include "src/client/auth/AuthUi.h"
#include "src/client/render/AuthUiRenderer.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/AuthRegisterPayloads.h"
#include "src/shared/network/CharacterPayloads.h"
#include "src/shared/network/ChatPayloads.h"
#include "src/shared/network/NetClient.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/PacketView.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/network/RequestResponseDispatcher.h"
#include "src/shared/platform/FileSystem.h"
#include "src/shared/platform/Window.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace engine::client
{
	namespace
	{
		// Phase 5 reconnect — duplicates des helpers déjà présents en anonyme dans
		// AuthUiPresenterCore.cpp. Anonymous namespace pour ne pas polluer les TU.
		bool ReconnectWaitConnected(engine::network::NetClient* c, uint32_t timeoutMs)
		{
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
			while (std::chrono::steady_clock::now() < deadline)
			{
				auto events = c->PollEvents();
				for (const auto& ev : events)
				{
					if (ev.type == engine::network::NetClientEventType::Connected)
						return true;
					if (ev.type == engine::network::NetClientEventType::Disconnected)
						return false;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
			return false;
		}

		void ReconnectApplyTls(engine::network::NetClient& client, const std::string& fingerprintHex, bool allowInsecure)
		{
			if (!fingerprintHex.empty())
				client.SetExpectedServerFingerprint(fingerprintHex);
			client.SetAllowInsecureDev(allowInsecure);
		}
	}

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

	bool AuthUiPresenter::SendChatAsync(uint8_t channel, std::string_view targetToken, std::string_view text)
	{
		if (!m_masterClient || m_masterSessionId == 0u)
		{
			return false;
		}
		if (text.empty())
		{
			return false;
		}
		const std::vector<uint8_t> payload = engine::network::BuildChatSendRequestPayload(channel, targetToken, text);
		if (payload.empty())
		{
			LOG_WARN(Net, "[AuthUiPresenter] SendChatAsync: Build payload failed");
			return false;
		}
		engine::network::PacketBuilder builder;
		auto w = builder.PayloadWriter();
		if (!w.WriteBytes(payload.data(), payload.size()))
		{
			LOG_WARN(Net, "[AuthUiPresenter] SendChatAsync: WriteBytes failed");
			return false;
		}
		if (!builder.Finalize(engine::network::kOpcodeChatSendRequest, 0u, 0u, m_masterSessionId, payload.size()))
		{
			LOG_WARN(Net, "[AuthUiPresenter] SendChatAsync: Finalize failed");
			return false;
		}
		const auto& packet = builder.Data();
		if (packet.empty())
		{
			return false;
		}
		if (!m_masterClient->Send(std::span<const uint8_t>(packet.data(), packet.size())))
		{
			LOG_WARN(Net, "[AuthUiPresenter] SendChatAsync: Send failed");
			return false;
		}
		LOG_DEBUG(Net, "[AuthUiPresenter] SendChatAsync queued (channel={}, target_len={}, text_len={})",
			static_cast<unsigned>(channel), targetToken.size(), text.size());
		return true;
	}

	bool AuthUiPresenter::SendGenericRequestAsync(uint16_t opcode, const std::vector<uint8_t>& payload)
	{
		// CMANGOS.18 (Phase 3.18 step 4) + CMANGOS.23 (Phase 5.23 step 3+4) —
		// Wrapper generique pour les opcodes type-specific Mail (49, 51, 53, 55, 57)
		// et Quest (59, 61, 63, 65). Mirror de SendChatAsync : meme pattern
		// PacketBuilder / Send, requestId=0 (les reponses sont dispatched via le
		// push handler du master).
		if (!m_masterClient || m_masterSessionId == 0u)
		{
			LOG_WARN(Net, "[AuthUiPresenter] SendGenericRequestAsync: no master session (opcode={})",
				static_cast<unsigned>(opcode));
			return false;
		}
		engine::network::PacketBuilder builder;
		auto w = builder.PayloadWriter();
		if (!payload.empty() && !w.WriteBytes(payload.data(), payload.size()))
		{
			LOG_WARN(Net, "[AuthUiPresenter] SendGenericRequestAsync: WriteBytes failed (opcode={})",
				static_cast<unsigned>(opcode));
			return false;
		}
		if (!builder.Finalize(opcode, /*flags=*/0u, /*requestId=*/0u, m_masterSessionId, payload.size()))
		{
			LOG_WARN(Net, "[AuthUiPresenter] SendGenericRequestAsync: Finalize failed (opcode={})",
				static_cast<unsigned>(opcode));
			return false;
		}
		const auto& packet = builder.Data();
		if (packet.empty())
		{
			LOG_WARN(Net, "[AuthUiPresenter] SendGenericRequestAsync: empty packet (opcode={})",
				static_cast<unsigned>(opcode));
			return false;
		}
		if (!m_masterClient->Send(std::span<const uint8_t>(packet.data(), packet.size())))
		{
			LOG_WARN(Net, "[AuthUiPresenter] SendGenericRequestAsync: Send failed (opcode={})",
				static_cast<unsigned>(opcode));
			return false;
		}
		LOG_DEBUG(Net, "[AuthUiPresenter] SendGenericRequestAsync queued (opcode={}, payload_size={})",
			static_cast<unsigned>(opcode), payload.size());
		return true;
	}

	bool AuthUiPresenter::SendEnterWorldAsync(uint64_t characterId, std::string_view characterName)
	{
		if (!m_masterClient || m_masterSessionId == 0u || characterId == 0u || characterName.empty())
		{
			return false;
		}
		const std::vector<uint8_t> payload =
			engine::network::BuildCharacterEnterWorldRequestPayload(characterId, characterName);
		if (payload.empty())
		{
			LOG_WARN(Net, "[AuthUiPresenter] SendEnterWorldAsync: Build payload failed");
			return false;
		}
		engine::network::PacketBuilder builder;
		auto w = builder.PayloadWriter();
		if (!w.WriteBytes(payload.data(), payload.size()))
		{
			return false;
		}
		if (!builder.Finalize(engine::network::kOpcodeCharacterEnterWorldRequest, 0u, 0u, m_masterSessionId, payload.size()))
		{
			return false;
		}
		const auto& packet = builder.Data();
		if (packet.empty() || !m_masterClient->Send(std::span<const uint8_t>(packet.data(), packet.size())))
		{
			LOG_WARN(Net, "[AuthUiPresenter] SendEnterWorldAsync: Send failed");
			return false;
		}
		LOG_INFO(Net, "[AuthUiPresenter] SendEnterWorldAsync queued (character_id={}, name_len={})",
			characterId, characterName.size());
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

				// Phase 5 reconnect : si on était post-EnterWorld (un perso actif est connu),
				// on programme une tentative de reconnexion automatique au lieu de laisser
				// le client silencieusement perdre le chat / SAVE_POSITION. L'engine appelle
				// TickReconnect chaque frame pour exécuter la tentative quand m_reconnectNextAt
				// est dépassé.
				if (m_postEnterWorldCharacterId != 0u && !m_login.empty() && !m_password.empty())
				{
					m_reconnectInProgress = true;
					m_reconnectAttempt = 0;
					m_reconnectAsyncDone.store(false);
					m_reconnectAsyncSuccess = false;
					m_reconnectNextAt = std::chrono::steady_clock::now() + std::chrono::seconds(2);
					m_reconnectStatusText = Tr("auth.info.reconnect_in_progress");
					if (m_reconnectStatusText.empty())
						m_reconnectStatusText = "Connexion perdue, reconnexion en cours...";
					LOG_WARN(Net, "[AuthUiPresenter] Master perdu post-EnterWorld : tentative de reconnexion programmee (+2s)");
				}
				return;
			}
			if (ev.type == NetClientEventType::PacketReceived)
			{
				LOG_DEBUG(Net, "[AuthUiPresenter] Master post-auth packet received ({} bytes)", ev.packet.size());
				// Chat MVP : si l'engine a installé un push handler, on parse l'en-tête V1 et
				// on dispatche l'opcode + payload. Le handler décide quoi faire (ex. CHAT_RELAY
				// → ChatUiPresenter::PushNetworkLine).
				if (m_masterPushHandler && ev.packet.size() >= engine::network::kProtocolV1HeaderSize)
				{
					engine::network::PacketView view;
					if (engine::network::PacketView::Parse(ev.packet.data(), ev.packet.size(), view)
						== engine::network::PacketParseResult::Ok)
					{
						m_masterPushHandler(view.Opcode(), view.Payload(), view.PayloadSize());
					}
				}
			}
		}

		// Heartbeat périodique sur la connexion master tant que le joueur est en jeu.
		// Sans ça, le client n'émet plus rien après EnterWorld : le master applique
		// HeartbeatTimeout (~120s), ferme la session, et le joueur disparaît de
		// SessionCharacterMap → le compteur /status retombe à 0 alors qu'on joue.
		// (m_masterClient est garanti non-nul ici : le seul chemin qui le reset
		// ci-dessus fait un return immédiat.)
		if (m_masterSessionId != 0u)
		{
			const auto now = std::chrono::steady_clock::now();
			if (now - m_lastMasterHeartbeatAt >= std::chrono::seconds(30))
			{
				const auto hb = engine::network::BuildHeartbeatPacket(m_masterSessionId);
				if (!hb.empty()
					&& m_masterClient->Send(std::span<const uint8_t>(hb.data(), hb.size())))
				{
					m_lastMasterHeartbeatAt = now;
				}
			}
		}
	}

	void AuthUiPresenter::RememberPostEnterWorldCharacter(uint64_t characterId, std::string characterName)
	{
		m_postEnterWorldCharacterId = characterId;
		m_postEnterWorldCharacterName = std::move(characterName);
	}

	void AuthUiPresenter::TickReconnect(const engine::core::Config& cfg)
	{
		if (!m_reconnectInProgress)
			return;

		// Étape 1 : si un worker précédent vient de finir, on consomme le résultat.
		if (m_reconnectAsyncDone.load())
		{
			JoinWorker();
			m_reconnectAsyncDone.store(false);
			if (m_reconnectAsyncSuccess)
			{
				LOG_INFO(Net, "[AuthUiPresenter] Reconnect success (attempt={})", m_reconnectAttempt);
				m_reconnectInProgress = false;
				m_reconnectStatusText = Tr("auth.info.reconnect_success");
				if (m_reconnectStatusText.empty())
					m_reconnectStatusText = "Connexion retablie.";
				// L'engine effacera la bannière sur la frame suivante via IsReconnecting()=false.
				return;
			}
			// Échec : MVP = une seule tentative, on abandonne et on revient à l'écran login.
			if (m_reconnectAttempt >= m_reconnectMaxAttempts)
			{
				LOG_WARN(Net, "[AuthUiPresenter] Reconnect FAILED after {} attempt(s) ; back to login", m_reconnectAttempt);
				m_reconnectInProgress = false;
				m_reconnectStatusText.clear();
				// Routage vers l'écran login : le flow va recommencer au prochain Submit.
				EnterAuthErrorPhase(Phase::Login, Tr("auth.error.reconnect_failed_back_to_login"));
				return;
			}
			// (Phase 5.x si on étend à plusieurs tentatives : reprogrammer m_reconnectNextAt avec backoff.)
		}

		// Étape 2 : si pas encore l'heure ou si un worker tourne déjà, on attend.
		if (std::chrono::steady_clock::now() < m_reconnectNextAt)
			return;
		if (m_worker.joinable())
			return;
		if (m_reconnectAttempt >= m_reconnectMaxAttempts)
			return; // sera traité au prochain m_reconnectAsyncDone.

		// Étape 3 : on lance la tentative.
		++m_reconnectAttempt;
		LOG_INFO(Net, "[AuthUiPresenter] Reconnect attempt {}/{}", m_reconnectAttempt, m_reconnectMaxAttempts);

		// Capture des paramètres connexion + credentials (m_login + m_password sont conservés
		// jusqu'à Shutdown ; ComputeClientHash recalcule le hash via le sel serveur).
		const std::string host = cfg.GetEffectiveMasterHost("localhost");
		const uint16_t port = static_cast<uint16_t>(cfg.GetInt("client.master_port", 3840));
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const bool allowInsecure = cfg.GetBool("client.allow_insecure_dev", true);
		const std::string serverFingerprint = cfg.GetString("client.server_fingerprint", "");
		EnsurePasswordSalt(cfg);
		const std::string hash = ComputeClientHash(cfg);
		const std::string login = m_login;
		const uint64_t  charId = m_postEnterWorldCharacterId;
		const std::string charName = m_postEnterWorldCharacterName;

		if (hash.empty() || login.empty() || charId == 0u || charName.empty())
		{
			LOG_WARN(Net, "[AuthUiPresenter] Reconnect missing credentials/character ; abort");
			m_reconnectAsyncSuccess = false;
			m_reconnectAsyncDone.store(true);
			return;
		}

		// On (ré-)alloue m_masterClient en main thread pour qu'il survive après le worker
		// (même pattern que StartMasterFlowWorker post-hotfix #401).
		if (m_masterClient)
		{
			m_masterClient->Disconnect("reconnect_restart");
			m_masterClient.reset();
		}
		m_masterClient = std::make_unique<engine::network::NetClient>();
		ReconnectApplyTls(*m_masterClient, serverFingerprint, allowInsecure);
		engine::network::NetClient* const masterClient = m_masterClient.get();

		m_worker = std::thread([this, masterClient, host, port, timeoutMs, login, hash, charId, charName]() {
			masterClient->Connect(host, port);
			if (!ReconnectWaitConnected(masterClient, timeoutMs + 2000u))
			{
				LOG_WARN(Net, "[Reconnect] master connect timeout");
				m_reconnectAsyncSuccess = false;
				m_reconnectAsyncDone.store(true);
				return;
			}
			engine::network::RequestResponseDispatcher disp(masterClient);
			bool authDone = false;
			bool authOk = false;
			uint64_t newSession = 0;
			if (!disp.SendRequest(engine::network::kOpcodeAuthRequest,
				engine::network::BuildAuthRequestPayload(login, hash),
				[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
					authDone = true;
					if (timeout) return;
					auto auth = engine::network::ParseAuthResponsePayload(pl.data(), pl.size());
					if (auth && auth->success != 0)
					{
						authOk = true;
						newSession = auth->session_id;
					}
				}, timeoutMs))
			{
				LOG_WARN(Net, "[Reconnect] AUTH send failed");
				m_reconnectAsyncSuccess = false;
				m_reconnectAsyncDone.store(true);
				return;
			}
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
			while (!authDone && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			if (!authOk)
			{
				LOG_WARN(Net, "[Reconnect] AUTH failed/timeout");
				m_reconnectAsyncSuccess = false;
				m_reconnectAsyncDone.store(true);
				return;
			}
			m_masterSessionId = newSession;
			LOG_INFO(Net, "[Reconnect] AUTH OK (new session_id={})", newSession);

			// Re-ENTER_WORLD : fire-and-forget. Si le master rejette (perso supprimé
			// entre temps ?), le sender retombera sur le login, c'est non-fatal.
			(void)SendEnterWorldAsync(charId, charName);

			m_reconnectAsyncSuccess = true;
			m_reconnectAsyncDone.store(true);
		});
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
