#pragma once

#include "engine/client/LocalizationService.h"
#include "engine/core/Config.h"
#include "engine/network/NetClient.h"
#include "engine/platform/Input.h"

#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace engine::platform
{
	class Window;
}

namespace engine::client
{
	/// STAB.13 — Login / register UI state machine; drives M20.5/M22.6 master flow without duplicating protocol.
	/// Assets reference: \c game/data/ui/login and \c game/data/ui/register (documented in panel text; no absolute paths).
	class AuthUiPresenter final
	{
	public:
		struct VideoSettingsCommand
		{
			bool applyRequested = false;
			bool fullscreen = false;
			bool vsync = true;
		};

		struct AudioSettingsCommand
		{
			bool applyRequested = false;
			float masterVolume = 1.0f;
			float musicVolume = 1.0f;
			float sfxVolume = 1.0f;
			float uiVolume = 1.0f;
		};

		struct ControlSettingsCommand
		{
			bool applyRequested = false;
			float mouseSensitivity = 0.002f;
			bool invertY = false;
			bool useZqsd = false;
		};

		struct GameSettingsCommand
		{
			bool applyRequested = false;
			bool gameplayUdpEnabled = false;
			bool allowInsecureDev = true;
			uint32_t authTimeoutMs = 5000;
		};

		struct VisualState
		{
			bool active = false;
			bool login = false;
			bool registerMode = false;
			bool verifyEmail = false;
			bool forgotPassword = false;
			bool terms = false;
			bool characterCreate = false;
			bool languageSelection = false;
			bool languageOptions = false;
			bool submitting = false;
			bool error = false;
		};

		struct RenderField
		{
			std::string label;
			std::string value;
			bool active = false;
			bool hovered = false;
			bool secret = false;
		};

		struct RenderAction
		{
			std::string label;
			bool primary = false;
			bool active = false;
			bool hovered = false;
		};

		struct RenderBodyLine
		{
			std::string text;
			bool active = false;
			bool hovered = false;
		};

		struct RenderModel
		{
			bool visible = false;
			std::string titleLine1;
			std::string titleLine2;
			std::string sectionTitle;
			std::string infoBanner;
			std::string errorText;
			std::string footerHint;
			std::vector<RenderField> fields;
			std::vector<RenderAction> actions;
			std::vector<RenderBodyLine> bodyLines;
			int32_t visibleBodyLineStart = 0;
			int32_t visibleBodyLineCount = 0;
		};

		AuthUiPresenter() = default;
		~AuthUiPresenter();

		/// Load config flags; may mark flow complete immediately when auth UI is disabled or on non-Windows stubs.
		bool Init(const engine::core::Config& cfg);

		void Shutdown();

		bool IsInitialized() const { return m_initialized; }

		/// When false, the master/shard gate has passed and the world loop should run normally.
		bool IsFlowComplete() const { return m_flowComplete; }

		/// While the auth gate is active, gameplay camera and chat should not consume input.
		bool BlocksWorldInput() const;

		/// Per-frame: text input, Tab/Enter, register toggle, async worker completion, window title.
		void Update(engine::platform::Input& input, float deltaSeconds, engine::platform::Window& window,
			const engine::core::Config& cfg);

		/// Multi-line panel for HUD / logs (full form + asset paths + errors).
		std::string BuildPanelText() const;
		RenderModel BuildRenderModel() const;
		VisualState GetVisualState() const;
		bool IsUsingNativeAuthScreen() const { return m_usingNativeAuthScreen; }
		VideoSettingsCommand ConsumePendingVideoSettings();
		AudioSettingsCommand ConsumePendingAudioSettings();
		ControlSettingsCommand ConsumePendingControlSettings();
		GameSettingsCommand ConsumePendingGameSettings();

		/// Escape: back from register to login, or clear error; returns true if consumed.
		bool OnEscape();

		bool SetViewportSize(uint32_t width, uint32_t height);

	private:
		void AppendPasswordStars(std::string& out, size_t len) const;
		void EnsurePasswordSalt(const engine::core::Config& cfg);
		std::string ComputeClientHash(const engine::core::Config& cfg) const;
		void StartRegisterWorker(const engine::core::Config& cfg);
		void StartLoginWorker(const engine::core::Config& cfg);
		void StartVerifyEmailWorker(const engine::core::Config& cfg);
		void StartForgotPasswordWorker(const engine::core::Config& cfg);
		void StartTermsStatusWorker(const engine::core::Config& cfg);
		void StartTermsAcceptWorker(const engine::core::Config& cfg);
		void StartCharacterCreateWorker(const engine::core::Config& cfg);
		void ResetMasterSession();
		void StartMasterFlowWorker(const engine::core::Config& cfg);
		void PollAsyncResult(const engine::core::Config& cfg);
		void LoadRememberPreference();
		void SaveRememberPreference();
		void ApplyLocaleSelection(bool firstRun);
		void OpenLanguageOptions();
		std::string Tr(std::string_view key, const LocalizationService::Params& params = {}) const;
		std::string CurrentLocale() const;
		std::string LocalizedLanguageName(std::string_view localeTag) const;
		bool HandleNativeAuthScreen(engine::platform::Window& window, const engine::core::Config& cfg);
		void SubmitCurrentPhase(const engine::core::Config& cfg);
		void UpdateWindowTitle(engine::platform::Window& window) const;
		void JoinWorker();

		enum class Phase
		{
			Login,
			Register,
			VerifyEmail,
			ForgotPassword,
			Terms,
			CharacterCreate,
			LanguageSelectionFirstRun,
			LanguageOptions,
			Submitting,
			Error
		};

		bool m_initialized = false;
		bool m_flowComplete = false;
		bool m_authEnabled = true;
		bool m_usingNativeAuthScreen = false;
		Phase m_phase = Phase::Login;

		std::string m_login;
		std::string m_password;
		std::string m_email;
		std::string m_firstName;
		std::string m_lastName;
		std::string m_birthDay;
		std::string m_birthMonth;
		std::string m_birthYear;
		std::string m_verifyCode;
		std::string m_termsTitle;
		std::string m_termsVersionLabel;
		std::string m_termsLocale;
		std::string m_termsContent;
		std::string m_characterName;
		uint32_t m_activeField = 0;
		int32_t m_hoveredFieldIndex = -1;
		int32_t m_hoveredBodyLineIndex = -1;
		int32_t m_hoveredActionIndex = -1;
		uint32_t m_termsScrollOffset = 0;
		uint32_t m_termsTotalLength = 0;
		std::string m_userErrorText;
		std::string m_infoBanner;
		uint64_t m_pendingVerifyAccountId = 0;
		uint64_t m_pendingTermsEditionId = 0;
		bool m_termsScrolledToBottom = false;
		bool m_termsAcknowledgeChecked = false;
		bool m_rememberLogin = false;
		bool m_savedRememberLogin = false;
		bool m_hasPersistedLocale = false;
		uint32_t m_languageSelectionIndex = 0;
		uint32_t m_optionsSelectionIndex = 0;
		Phase m_phaseBeforeOptions = Phase::Login;
		std::string m_selectedLocale;
		std::string m_persistedLocale;
		bool m_videoFullscreen = false;
		bool m_videoVsync = true;
		bool m_videoFullscreenPending = false;
		bool m_videoVsyncPending = true;
		VideoSettingsCommand m_pendingVideoSettings{};
		float m_audioMasterVolume = 1.0f;
		float m_audioMusicVolume = 1.0f;
		float m_audioSfxVolume = 1.0f;
		float m_audioUiVolume = 1.0f;
		float m_audioMasterVolumePending = 1.0f;
		float m_audioMusicVolumePending = 1.0f;
		float m_audioSfxVolumePending = 1.0f;
		float m_audioUiVolumePending = 1.0f;
		AudioSettingsCommand m_pendingAudioSettings{};
		float m_mouseSensitivity = 0.002f;
		float m_mouseSensitivityPending = 0.002f;
		bool m_invertY = false;
		bool m_invertYPending = false;
		bool m_useZqsd = false;
		bool m_useZqsdPending = false;
		ControlSettingsCommand m_pendingControlSettings{};
		bool m_gameplayUdpEnabled = false;
		bool m_gameplayUdpEnabledPending = false;
		bool m_allowInsecureDev = true;
		bool m_allowInsecureDevPending = true;
		uint32_t m_authTimeoutMs = 5000u;
		uint32_t m_authTimeoutMsPending = 5000u;
		GameSettingsCommand m_pendingGameSettings{};
		LocalizationService m_localization;

		std::vector<uint8_t> m_argonSalt{};
		uint32_t m_viewportW = 0;
		uint32_t m_viewportH = 0;

		/// Background worker for REGISTER_REQUEST or MasterShardClientFlow (TCP); join before destroy.
		struct AsyncResult
		{
			bool ready = false;
			bool success = false;
			uint64_t accountId = 0;
			uint64_t sessionId = 0;
			uint64_t termsEditionId = 0;
			uint32_t termsPendingCount = 0;
			uint32_t totalLength = 0;
			std::string termsTitle;
			std::string termsVersionLabel;
			std::string termsLocale;
			std::string termsContent;
			std::string message;
		};
		AsyncResult m_asyncResult{};
		std::thread m_worker{};
		enum class AsyncKind
		{
			None,
			Register,
			AuthOnly,
			VerifyEmail,
			ForgotPassword,
			TermsStatus,
			TermsAccept,
			CharacterCreate,
			Login
		};
		AsyncKind m_pendingAsyncKind = AsyncKind::None;
		uint64_t m_masterSessionId = 0;
		std::unique_ptr<engine::network::NetClient> m_masterClient;
		std::mutex m_asyncMutex{};
	};

}

