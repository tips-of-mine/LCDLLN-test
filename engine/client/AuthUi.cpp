#include "engine/client/AuthUi.h"

#include "engine/core/Log.h"
#include "engine/network/NetClient.h"
#include "engine/platform/FileSystem.h"
#include "engine/platform/Window.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#	include "engine/auth/Argon2Hash.h"
#	include "engine/network/AuthRegisterPayloads.h"
#	include "engine/network/ErrorPacket.h"
#	include "engine/network/CharacterPayloads.h"
#	include "engine/network/MasterShardClientFlow.h"
#	include "engine/network/ProtocolV1Constants.h"
#	include "engine/network/RequestResponseDispatcher.h"
#	include "engine/network/TermsPayloads.h"
#endif

namespace engine::client
{
	namespace
	{
		constexpr std::string_view kUserSettingsPath = "user_settings.json";
		constexpr std::string_view kLoginBackgroundPath = "ui/loading/background.png";
		constexpr std::string_view kLoginLogoPath = "";
		constexpr std::string_view kRegisterBackgroundPath = "ui/loading/background.png";
		constexpr std::string_view kRegisterInfoPath = "";

		std::string JsonBool(bool value)
		{
			return value ? "true" : "false";
		}

		std::string EscapeJsonString(std::string_view value)
		{
			std::string out;
			out.reserve(value.size() + 8u);
			for (char c : value)
			{
				switch (c)
				{
				case '\\': out += "\\\\"; break;
				case '"': out += "\\\""; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default: out.push_back(c); break;
				}
			}
			return out;
		}

		std::string BuildUserSettingsJson(bool rememberLogin, std::string_view locale, bool fullscreen, bool vsync)
		{
			return std::string("{\n  \"client\": {\n    \"locale\": \"")
				+ EscapeJsonString(locale)
				+ "\",\n    \"auth_ui\": {\n      \"remember_login\": "
				+ JsonBool(rememberLogin)
				+ ",\n      \"timeout_ms\": 5000\n    },\n    \"allow_insecure_dev\": true,\n    \"gameplay_udp\": {\n      \"enabled\": false\n    }\n  },\n  \"render\": {\n    \"fullscreen\": "
				+ JsonBool(fullscreen)
				+ ",\n    \"vsync\": "
				+ JsonBool(vsync)
				+ "\n  },\n  \"audio\": {\n    \"master_volume\": 1.0,\n    \"music_volume\": 1.0,\n    \"sfx_volume\": 1.0,\n    \"ui_volume\": 1.0\n  },\n  \"camera\": {\n    \"mouse_sensitivity\": 0.002\n  },\n  \"controls\": {\n    \"invert_y\": false,\n    \"movement_layout\": \"wasd\"\n  }\n}\n";
		}

		void ReplaceAudioSettings(std::string& json, float masterVolume, float musicVolume, float sfxVolume, float uiVolume)
		{
			auto replaceValue = [&json](std::string_view key, float value)
			{
				const std::string needle = std::string("\"") + std::string(key) + "\": ";
				const size_t start = json.find(needle);
				if (start == std::string::npos)
					return;
				const size_t valueStart = start + needle.size();
				size_t valueEnd = valueStart;
				while (valueEnd < json.size() && (std::isdigit(static_cast<unsigned char>(json[valueEnd])) != 0 || json[valueEnd] == '.'))
					++valueEnd;
				json.replace(valueStart, valueEnd - valueStart, std::to_string(value));
			};

			replaceValue("master_volume", masterVolume);
			replaceValue("music_volume", musicVolume);
			replaceValue("sfx_volume", sfxVolume);
			replaceValue("ui_volume", uiVolume);
		}

		float ClampOptionStep(float value)
		{
			const float clamped = std::clamp(value, 0.0f, 1.0f);
			const int scaled = static_cast<int>(clamped * 10.0f + 0.5f);
			return static_cast<float>(scaled) / 10.0f;
		}

		void ReplaceControlSettings(std::string& json, float mouseSensitivity, bool invertY, bool useZqsd)
		{
			auto replaceBool = [&json](std::string_view key, bool value)
			{
				const std::string needle = std::string("\"") + std::string(key) + "\": ";
				const size_t start = json.find(needle);
				if (start == std::string::npos)
					return;
				const size_t valueStart = start + needle.size();
				size_t valueEnd = valueStart;
				while (valueEnd < json.size() && std::isalpha(static_cast<unsigned char>(json[valueEnd])) != 0)
					++valueEnd;
				json.replace(valueStart, valueEnd - valueStart, value ? "true" : "false");
			};
			auto replaceString = [&json](std::string_view key, std::string_view value)
			{
				const std::string needle = std::string("\"") + std::string(key) + "\": \"";
				const size_t start = json.find(needle);
				if (start == std::string::npos)
					return;
				const size_t valueStart = start + needle.size();
				const size_t valueEnd = json.find('"', valueStart);
				if (valueEnd == std::string::npos)
					return;
				json.replace(valueStart, valueEnd - valueStart, value);
			};
			auto replaceNumber = [&json](std::string_view key, double value)
			{
				const std::string needle = std::string("\"") + std::string(key) + "\": ";
				const size_t start = json.find(needle);
				if (start == std::string::npos)
					return;
				const size_t valueStart = start + needle.size();
				size_t valueEnd = valueStart;
				while (valueEnd < json.size() && (std::isdigit(static_cast<unsigned char>(json[valueEnd])) != 0 || json[valueEnd] == '.'))
					++valueEnd;
				json.replace(valueStart, valueEnd - valueStart, std::to_string(value));
			};

			replaceNumber("mouse_sensitivity", mouseSensitivity);
			replaceBool("invert_y", invertY);
			replaceString("movement_layout", useZqsd ? "zqsd" : "wasd");
		}

		void ReplaceGameSettings(std::string& json, bool gameplayUdpEnabled, bool allowInsecureDev, uint32_t authTimeoutMs)
		{
			auto replaceBoolByNeedle = [&json](std::string_view needle, bool value)
			{
				const size_t start = json.find(needle);
				if (start == std::string::npos)
					return;
				const size_t valueStart = start + needle.size();
				size_t valueEnd = valueStart;
				while (valueEnd < json.size() && std::isalpha(static_cast<unsigned char>(json[valueEnd])) != 0)
					++valueEnd;
				json.replace(valueStart, valueEnd - valueStart, value ? "true" : "false");
			};
			auto replaceNumberByNeedle = [&json](std::string_view needle, uint32_t value)
			{
				const size_t start = json.find(needle);
				if (start == std::string::npos)
					return;
				const size_t valueStart = start + needle.size();
				size_t valueEnd = valueStart;
				while (valueEnd < json.size() && std::isdigit(static_cast<unsigned char>(json[valueEnd])) != 0)
					++valueEnd;
				json.replace(valueStart, valueEnd - valueStart, std::to_string(value));
			};

			replaceBoolByNeedle("\"allow_insecure_dev\": ", allowInsecureDev);
			replaceNumberByNeedle("\"timeout_ms\": ", authTimeoutMs);
			replaceBoolByNeedle("\"gameplay_udp\": {\n      \"enabled\": ", gameplayUdpEnabled);
		}

		std::string ResolvePasswordRecoveryUrl(const engine::core::Config& cfg)
		{
			return cfg.GetString("client.web_portal_reset_url", "http://127.0.0.1:3000/password-recovery");
		}

		bool IsAsciiDigits(std::string_view text)
		{
			if (text.empty())
				return false;
			for (unsigned char c : text)
			{
				if (c < '0' || c > '9')
					return false;
			}
			return true;
		}

		[[maybe_unused]] bool IsValidBirthDateFields(std::string_view day, std::string_view month, std::string_view year)
		{
			if (!IsAsciiDigits(day) || !IsAsciiDigits(month) || !IsAsciiDigits(year))
				return false;
			const int d = std::stoi(std::string(day));
			const int m = std::stoi(std::string(month));
			const int y = std::stoi(std::string(year));
			if (y < 1900 || y > 2100)
				return false;
			if (m < 1 || m > 12)
				return false;
			if (d < 1 || d > 31)
				return false;
			return true;
		}

		[[maybe_unused]] bool IsValidVerificationCode(std::string_view code)
		{
			return code.size() == 6u && IsAsciiDigits(code);
		}

		[[maybe_unused]] bool IsValidCharacterNameLocal(std::string_view name)
		{
			if (name.size() < 3u || name.size() > 32u)
				return false;
			for (unsigned char c : name)
			{
				const bool ok = std::isalnum(c) || c == '_';
				if (!ok)
					return false;
			}
			return true;
		}

#if defined(_WIN32)
		bool WaitConnected(engine::network::NetClient* c, uint32_t timeoutMs)
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

		const char* NetErrorLabel(engine::network::NetErrorCode c)
		{
			using engine::network::NetErrorCode;
			switch (c)
			{
			case NetErrorCode::OK:
				return "OK";
			case NetErrorCode::INVALID_CREDENTIALS:
				return "Invalid credentials";
			case NetErrorCode::ACCOUNT_NOT_FOUND:
				return "Account not found";
			case NetErrorCode::ACCOUNT_LOCKED:
				return "Account locked";
			case NetErrorCode::ALREADY_LOGGED_IN:
				return "Already logged in";
			case NetErrorCode::LOGIN_ALREADY_TAKEN:
				return "Login already taken";
			case NetErrorCode::INVALID_EMAIL:
				return "Invalid email";
			case NetErrorCode::WEAK_PASSWORD:
				return "Weak password";
			case NetErrorCode::INVALID_LOGIN:
				return "Invalid login";
			case NetErrorCode::EMAIL_VERIFICATION_REQUIRED:
				return "Email verification required";
			case NetErrorCode::EMAIL_ALREADY_VERIFIED:
				return "Email already verified";
			case NetErrorCode::VERIFICATION_CODE_INVALID:
				return "Verification code invalid";
			case NetErrorCode::REGISTRATION_DISABLED:
				return "Registration disabled";
			case NetErrorCode::REGISTRATION_INVALID:
				return "Registration invalid";
			case NetErrorCode::TIMEOUT:
				return "Timeout";
			default:
				return "Network error";
			}
		}
#endif
	}

	AuthUiPresenter::~AuthUiPresenter()
	{
		Shutdown();
	}

#if !defined(_WIN32)

	bool AuthUiPresenter::Init(const engine::core::Config&)
	{
		m_initialized = true;
		m_flowComplete = true;
		LOG_INFO(Core, "[AuthUiPresenter] Init OK (auth UI skipped on non-Windows build)");
		return true;
	}

	void AuthUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		m_usingNativeAuthScreen = false;
		m_initialized = false;
		LOG_INFO(Core, "[AuthUiPresenter] Destroyed");
	}

	bool AuthUiPresenter::BlocksWorldInput() const
	{
		return false;
	}

	bool AuthUiPresenter::SetViewportSize(uint32_t width, uint32_t height)
	{
		m_viewportW = width;
		m_viewportH = height;
		return width > 0 && height > 0;
	}

	void AuthUiPresenter::SaveRememberPreference()
	{
	}

	void AuthUiPresenter::ApplyLocaleSelection(bool)
	{
	}

	void AuthUiPresenter::OpenLanguageOptions()
	{
	}

	std::string AuthUiPresenter::Tr(std::string_view key, const LocalizationService::Params&) const
	{
		return std::string(key);
	}

	std::string AuthUiPresenter::CurrentLocale() const
	{
		return "en";
	}

	std::string AuthUiPresenter::LocalizedLanguageName(std::string_view localeTag) const
	{
		return std::string(localeTag);
	}

	void AuthUiPresenter::SubmitCurrentPhase(const engine::core::Config&)
	{
	}

	void AuthUiPresenter::AppendPasswordStars(std::string&, size_t) const {}
	void AuthUiPresenter::EnsurePasswordSalt(const engine::core::Config&) {}
	std::string AuthUiPresenter::ComputeClientHash(const engine::core::Config&) const { return {}; }
	void AuthUiPresenter::StartRegisterWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartLoginWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartVerifyEmailWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartForgotPasswordWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartTermsStatusWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartTermsAcceptWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartCharacterCreateWorker(const engine::core::Config&) {}
	void AuthUiPresenter::ResetMasterSession() {}
	void AuthUiPresenter::StartMasterFlowWorker(const engine::core::Config&) {}
	void AuthUiPresenter::PollAsyncResult(const engine::core::Config&) {}
	void AuthUiPresenter::UpdateWindowTitle(engine::platform::Window&) const {}
	void AuthUiPresenter::JoinWorker() {}

#else

	bool AuthUiPresenter::Init(const engine::core::Config& cfg)
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[AuthUiPresenter] Init ignored: already initialized");
			return true;
		}

		m_authEnabled = cfg.GetBool("client.auth_ui.enabled", true);
		if (!m_authEnabled)
		{
			m_flowComplete = true;
			m_initialized = true;
			LOG_INFO(Core, "[AuthUiPresenter] Init OK (disabled via client.auth_ui.enabled=false)");
			return true;
		}

		m_flowComplete = false;
		m_phase = Phase::Login;
		m_login.clear();
		m_password.clear();
		m_email.clear();
		m_firstName.clear();
		m_lastName.clear();
		m_birthDay.clear();
		m_birthMonth.clear();
		m_birthYear.clear();
		m_verifyCode.clear();
		m_termsTitle.clear();
		m_termsVersionLabel.clear();
		m_termsLocale.clear();
		m_termsContent.clear();
		m_characterName.clear();
		m_activeField = 0;
		m_termsScrollOffset = 0;
		m_termsTotalLength = 0;
		m_userErrorText.clear();
		m_infoBanner.clear();
		m_pendingVerifyAccountId = 0;
		m_pendingTermsEditionId = 0;
		m_termsScrolledToBottom = false;
		m_termsAcknowledgeChecked = false;
		m_rememberLogin = false;
		m_savedRememberLogin = false;
		m_hasPersistedLocale = false;
		m_languageSelectionIndex = 0;
		m_optionsSelectionIndex = 0;
		m_phaseBeforeOptions = Phase::Login;
		m_selectedLocale.clear();
		m_persistedLocale.clear();
		m_videoFullscreen = cfg.GetBool("render.fullscreen", true);
		m_videoVsync = cfg.GetBool("render.vsync", true);
		m_videoFullscreenPending = m_videoFullscreen;
		m_videoVsyncPending = m_videoVsync;
		m_pendingVideoSettings = {};
		m_audioMasterVolume = ClampOptionStep(static_cast<float>(cfg.GetDouble("audio.master_volume", 1.0)));
		m_audioMusicVolume = ClampOptionStep(static_cast<float>(cfg.GetDouble("audio.music_volume", 1.0)));
		m_audioSfxVolume = ClampOptionStep(static_cast<float>(cfg.GetDouble("audio.sfx_volume", 1.0)));
		m_audioUiVolume = ClampOptionStep(static_cast<float>(cfg.GetDouble("audio.ui_volume", 1.0)));
		m_audioMasterVolumePending = m_audioMasterVolume;
		m_audioMusicVolumePending = m_audioMusicVolume;
		m_audioSfxVolumePending = m_audioSfxVolume;
		m_audioUiVolumePending = m_audioUiVolume;
		m_pendingAudioSettings = {};
		m_mouseSensitivity = static_cast<float>(cfg.GetDouble("camera.mouse_sensitivity", 0.002));
		m_mouseSensitivityPending = m_mouseSensitivity;
		m_invertY = cfg.GetBool("controls.invert_y", false);
		m_invertYPending = m_invertY;
		m_useZqsd = cfg.GetString("controls.movement_layout", "wasd") == "zqsd";
		m_useZqsdPending = m_useZqsd;
		m_pendingControlSettings = {};
		m_gameplayUdpEnabled = cfg.GetBool("client.gameplay_udp.enabled", false);
		m_gameplayUdpEnabledPending = m_gameplayUdpEnabled;
		m_allowInsecureDev = cfg.GetBool("client.allow_insecure_dev", true);
		m_allowInsecureDevPending = m_allowInsecureDev;
		m_authTimeoutMs = static_cast<uint32_t>(std::clamp<int64_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000), 1000, 15000));
		m_authTimeoutMsPending = m_authTimeoutMs;
		m_pendingGameSettings = {};
		m_argonSalt.clear();
		m_asyncResult = {};
		m_pendingAsyncKind = AsyncKind::None;
		m_masterSessionId = 0;
		JoinWorker();
		m_masterClient.reset();
		LoadRememberPreference();
		const std::string configLocale = LocalizationService::NormalizeLocaleTag(cfg.GetString("client.locale", ""));
		const std::string requestedLocale = !m_persistedLocale.empty() ? m_persistedLocale : configLocale;
		if (!m_localization.Init(cfg, requestedLocale.empty() ? LocalizationService::DetectSystemLocaleTag() : requestedLocale))
		{
			LOG_ERROR(Core, "[AuthUiPresenter] Init FAILED: localization init failed");
			return false;
		}
		if (requestedLocale.empty())
		{
			m_selectedLocale = m_localization.GetCurrentLocale();
			const auto& locales = m_localization.GetAvailableLocales();
			auto it = std::find(locales.begin(), locales.end(), m_selectedLocale);
			m_languageSelectionIndex = it != locales.end() ? static_cast<uint32_t>(std::distance(locales.begin(), it)) : 0u;
			m_phase = Phase::LanguageSelectionFirstRun;
			LOG_INFO(Core, "[AuthUiPresenter] First run locale selection required (detected={})", m_selectedLocale);
		}
		else
		{
			m_selectedLocale = m_localization.GetCurrentLocale();
			LOG_INFO(Core, "[AuthUiPresenter] Initial locale retained ({})", m_selectedLocale);
		}

		m_initialized = true;
		LOG_INFO(Core, "[AuthUiPresenter] Init OK (master host from client.master_host / client.master_port, locale={})", m_localization.GetCurrentLocale());
		return true;
	}

	void AuthUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		JoinWorker();
		ResetMasterSession();
		SaveRememberPreference();
		m_password.clear();
		m_localization.Shutdown();
		m_initialized = false;
		LOG_INFO(Core, "[AuthUiPresenter] Destroyed");
	}

	void AuthUiPresenter::LoadRememberPreference()
	{
		engine::core::Config persisted;
		if (persisted.LoadFromFile(kUserSettingsPath))
		{
			m_rememberLogin = persisted.GetBool("client.auth_ui.remember_login", false);
			m_savedRememberLogin = m_rememberLogin;
			m_persistedLocale = LocalizationService::NormalizeLocaleTag(persisted.GetString("client.locale", ""));
			m_hasPersistedLocale = !m_persistedLocale.empty();
			m_videoFullscreen = persisted.GetBool("render.fullscreen", m_videoFullscreen);
			m_videoVsync = persisted.GetBool("render.vsync", m_videoVsync);
			m_videoFullscreenPending = m_videoFullscreen;
			m_videoVsyncPending = m_videoVsync;
			m_audioMasterVolume = ClampOptionStep(static_cast<float>(persisted.GetDouble("audio.master_volume", m_audioMasterVolume)));
			m_audioMusicVolume = ClampOptionStep(static_cast<float>(persisted.GetDouble("audio.music_volume", m_audioMusicVolume)));
			m_audioSfxVolume = ClampOptionStep(static_cast<float>(persisted.GetDouble("audio.sfx_volume", m_audioSfxVolume)));
			m_audioUiVolume = ClampOptionStep(static_cast<float>(persisted.GetDouble("audio.ui_volume", m_audioUiVolume)));
			m_audioMasterVolumePending = m_audioMasterVolume;
			m_audioMusicVolumePending = m_audioMusicVolume;
			m_audioSfxVolumePending = m_audioSfxVolume;
			m_audioUiVolumePending = m_audioUiVolume;
			m_mouseSensitivity = static_cast<float>(persisted.GetDouble("camera.mouse_sensitivity", m_mouseSensitivity));
			m_mouseSensitivityPending = m_mouseSensitivity;
			m_invertY = persisted.GetBool("controls.invert_y", m_invertY);
			m_invertYPending = m_invertY;
			m_useZqsd = persisted.GetString("controls.movement_layout", m_useZqsd ? "zqsd" : "wasd") == "zqsd";
			m_useZqsdPending = m_useZqsd;
			m_gameplayUdpEnabled = persisted.GetBool("client.gameplay_udp.enabled", m_gameplayUdpEnabled);
			m_gameplayUdpEnabledPending = m_gameplayUdpEnabled;
			m_allowInsecureDev = persisted.GetBool("client.allow_insecure_dev", m_allowInsecureDev);
			m_allowInsecureDevPending = m_allowInsecureDev;
			m_authTimeoutMs = static_cast<uint32_t>(std::clamp<int64_t>(persisted.GetInt("client.auth_ui.timeout_ms", m_authTimeoutMs), 1000, 15000));
			m_authTimeoutMsPending = m_authTimeoutMs;
			LOG_INFO(Core, "[AuthUiPresenter] Preferences loaded (remember_login={}, locale={}, fullscreen={}, vsync={}, master={:.1f}, music={:.1f}, sfx={:.1f}, ui={:.1f}, sens={:.4f}, invert_y={}, layout={}, gameplay_udp={}, allow_insecure_dev={}, timeout_ms={})",
				m_rememberLogin, m_persistedLocale, m_videoFullscreen, m_videoVsync,
				m_audioMasterVolume, m_audioMusicVolume, m_audioSfxVolume, m_audioUiVolume,
				m_mouseSensitivity, m_invertY, m_useZqsd ? "zqsd" : "wasd",
				m_gameplayUdpEnabled, m_allowInsecureDev, m_authTimeoutMs);
			return;
		}
		m_rememberLogin = false;
		m_savedRememberLogin = false;
		m_persistedLocale.clear();
		m_hasPersistedLocale = false;
		LOG_INFO(Core, "[AuthUiPresenter] Preferences defaulted (remember_login=false, locale=unset, fullscreen={}, vsync={}, master={:.1f}, music={:.1f}, sfx={:.1f}, ui={:.1f}, sens={:.4f}, invert_y={}, layout={}, gameplay_udp={}, allow_insecure_dev={}, timeout_ms={})",
			m_videoFullscreen, m_videoVsync, m_audioMasterVolume, m_audioMusicVolume, m_audioSfxVolume, m_audioUiVolume,
			m_mouseSensitivity, m_invertY, m_useZqsd ? "zqsd" : "wasd",
			m_gameplayUdpEnabled, m_allowInsecureDev, m_authTimeoutMs);
	}

	void AuthUiPresenter::SaveRememberPreference()
	{
		const std::filesystem::path settingsPath{ kUserSettingsPath };
		if (engine::platform::FileSystem::Exists(settingsPath))
		{
			LOG_INFO(Core, "[AuthUiPresenter] Preferences persist skipped (existing user_settings.json preserved)");
			return;
		}

		const std::string locale = CurrentLocale();
		std::string json = BuildUserSettingsJson(m_rememberLogin, locale, m_videoFullscreen, m_videoVsync);
		ReplaceAudioSettings(json, m_audioMasterVolume, m_audioMusicVolume, m_audioSfxVolume, m_audioUiVolume);
		ReplaceControlSettings(json, m_mouseSensitivity, m_invertY, m_useZqsd);
		ReplaceGameSettings(json, m_gameplayUdpEnabled, m_allowInsecureDev, m_authTimeoutMs);
		if (!engine::platform::FileSystem::WriteAllText(settingsPath, json))
		{
			LOG_WARN(Core, "[AuthUiPresenter] Failed to persist preferences");
			return;
		}
		m_savedRememberLogin = m_rememberLogin;
		m_persistedLocale = locale;
		m_hasPersistedLocale = !locale.empty();
		LOG_INFO(Core, "[AuthUiPresenter] Preferences persisted (remember_login={}, locale={}, fullscreen={}, vsync={}, master={:.1f}, music={:.1f}, sfx={:.1f}, ui={:.1f}, sens={:.4f}, invert_y={}, layout={}, gameplay_udp={}, allow_insecure_dev={}, timeout_ms={})",
			m_rememberLogin, locale, m_videoFullscreen, m_videoVsync,
			m_audioMasterVolume, m_audioMusicVolume, m_audioSfxVolume, m_audioUiVolume,
			m_mouseSensitivity, m_invertY, m_useZqsd ? "zqsd" : "wasd",
			m_gameplayUdpEnabled, m_allowInsecureDev, m_authTimeoutMs);
	}

	bool AuthUiPresenter::BlocksWorldInput() const
	{
		return m_initialized && !m_flowComplete;
	}

	void AuthUiPresenter::JoinWorker()
	{
		if (m_worker.joinable())
		{
			m_worker.join();
			LOG_INFO(Core, "[AuthUiPresenter] Background worker joined");
		}
	}

	void AuthUiPresenter::ResetMasterSession()
	{
		JoinWorker();
		m_masterSessionId = 0;
		if (m_masterClient)
		{
			m_masterClient->Disconnect("auth_ui_reset");
			m_masterClient.reset();
		}
	}

	void AuthUiPresenter::AppendPasswordStars(std::string& out, size_t len) const
	{
		for (size_t i = 0; i < len; ++i)
			out.push_back('*');
	}

	void AuthUiPresenter::EnsurePasswordSalt(const engine::core::Config& cfg)
	{
		if (!m_argonSalt.empty())
			return;
		m_argonSalt = engine::auth::GenerateSalt(engine::auth::kArgon2SaltLength);
		if (m_argonSalt.empty())
		{
			LOG_ERROR(Core, "[AuthUiPresenter] Password salt generation FAILED");
			return;
		}
		(void)cfg;
		LOG_INFO(Core, "[AuthUiPresenter] Client-side Argon2 salt ready (len={})", m_argonSalt.size());
	}

	std::string AuthUiPresenter::ComputeClientHash(const engine::core::Config& cfg) const
	{
		engine::auth::Argon2Params params;
		params.time_cost = static_cast<uint32_t>(cfg.GetInt("client.argon2.time_cost", static_cast<int64_t>(params.time_cost)));
		params.memory_kib = static_cast<uint32_t>(cfg.GetInt("client.argon2.memory_kib", static_cast<int64_t>(params.memory_kib)));
		params.parallelism = static_cast<uint32_t>(cfg.GetInt("client.argon2.parallelism", static_cast<int64_t>(params.parallelism)));
		params.hash_len = static_cast<uint32_t>(cfg.GetInt("client.argon2.hash_len", static_cast<int64_t>(params.hash_len)));
		return engine::auth::Hash(m_password, m_argonSalt, params);
	}

	void AuthUiPresenter::UpdateWindowTitle(engine::platform::Window& window) const
	{
		std::string t = Tr("app.title");
		t += " | ";
		switch (m_phase)
		{
		case Phase::Login:
			t += Tr("auth.phase.login");
			break;
		case Phase::Register:
			t += Tr("auth.phase.register");
			break;
		case Phase::VerifyEmail:
			t += Tr("auth.phase.verify_email");
			break;
		case Phase::ForgotPassword:
			t += Tr("auth.phase.forgot_password");
			break;
		case Phase::Terms:
			t += Tr("auth.phase.terms");
			break;
		case Phase::CharacterCreate:
			t += Tr("auth.phase.character_create");
			break;
		case Phase::LanguageSelectionFirstRun:
			t += Tr("language.first_run.title");
			break;
		case Phase::LanguageOptions:
			t += Tr("language.options.title");
			break;
		case Phase::Submitting:
			t += Tr("auth.phase.submitting");
			break;
		case Phase::Error:
			t += Tr("auth.phase.error");
			break;
		}
		if (!m_userErrorText.empty() && m_phase == Phase::Error)
		{
			t += " — ";
			const size_t kMax = 80;
			if (m_userErrorText.size() > kMax)
				t.append(m_userErrorText.data(), kMax);
			else
				t += m_userErrorText;
		}
		window.SetTitle(t);
	}

	void AuthUiPresenter::ApplyLocaleSelection(bool firstRun)
	{
		const auto& locales = m_localization.GetAvailableLocales();
		if (locales.empty())
		{
			LOG_WARN(Core, "[AuthUiPresenter] ApplyLocaleSelection ignored: no available locales");
			return;
		}

		if (m_languageSelectionIndex >= locales.size())
			m_languageSelectionIndex = 0;
		m_selectedLocale = locales[m_languageSelectionIndex];
		if (!m_localization.SetLocale(m_selectedLocale))
		{
			LOG_WARN(Core, "[AuthUiPresenter] Locale apply failed for '{}'", m_selectedLocale);
			return;
		}

		SaveRememberPreference();
		m_infoBanner = Tr("language.apply_success", { { "language", LocalizedLanguageName(m_selectedLocale) } });
		LOG_INFO(Core, "[AuthUiPresenter] Locale selection applied (locale={}, first_run={})", m_selectedLocale, firstRun);
		if (firstRun)
		{
			m_phase = Phase::Login;
			m_activeField = 0;
		}
		else
		{
			m_phase = m_phaseBeforeOptions;
		}
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
		m_phase = Phase::LanguageOptions;
		m_selectedLocale = CurrentLocale();
		m_videoFullscreenPending = m_videoFullscreen;
		m_videoVsyncPending = m_videoVsync;
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
		m_optionsSelectionIndex = 0;
		auto it = std::find(locales.begin(), locales.end(), m_selectedLocale);
		m_languageSelectionIndex = it != locales.end() ? static_cast<uint32_t>(std::distance(locales.begin(), it)) : 0u;
		LOG_INFO(Core, "[AuthUiPresenter] Options opened (locale={}, fullscreen={}, vsync={}, sens={:.4f}, invert_y={}, layout={}, gameplay_udp={}, allow_insecure_dev={}, timeout_ms={})",
			m_selectedLocale, m_videoFullscreenPending, m_videoVsyncPending,
			m_mouseSensitivityPending, m_invertYPending, m_useZqsdPending ? "zqsd" : "wasd",
			m_gameplayUdpEnabledPending, m_allowInsecureDevPending, m_authTimeoutMsPending);
	}

	std::string AuthUiPresenter::Tr(std::string_view key, const LocalizationService::Params& params) const
	{
		return m_localization.Translate(key, params);
	}

	std::string AuthUiPresenter::CurrentLocale() const
	{
		return m_localization.GetCurrentLocale();
	}

	std::string AuthUiPresenter::LocalizedLanguageName(std::string_view localeTag) const
	{
		return Tr(std::string("language.name.") + std::string(localeTag));
	}

	void AuthUiPresenter::PollAsyncResult(const engine::core::Config& cfg)
	{
		// Fast-path: on the initial login screen no background worker has been started yet,
		// so there is no async state to synchronize. Avoid touching the mutex unnecessarily.
		if (!m_worker.joinable() && !m_asyncResult.ready)
		{
			return;
		}

		AsyncResult copy{};
		{
			std::lock_guard<std::mutex> lock(m_asyncMutex);
			if (!m_asyncResult.ready)
			{
				return;
			}
			copy = m_asyncResult;
			m_asyncResult = {};
		}
		JoinWorker();

		const AsyncKind kind = m_pendingAsyncKind;
		m_pendingAsyncKind = AsyncKind::None;

		if (kind == AsyncKind::Register)
		{
			if (copy.success)
			{
				m_pendingVerifyAccountId = copy.accountId;
				m_phase = Phase::VerifyEmail;
				m_userErrorText.clear();
				m_infoBanner = copy.message.empty() ? Tr("auth.info.register_ok") : copy.message;
				LOG_INFO(Core, "[AuthUiPresenter] Register finished OK: {}", m_infoBanner);
			}
			else
			{
				m_phase = Phase::Error;
				m_userErrorText = copy.message;
				LOG_WARN(Core, "[AuthUiPresenter] Register FAILED: {}", copy.message);
			}
			(void)cfg;
			return;
		}

		if (kind == AsyncKind::AuthOnly)
		{
			if (copy.success)
			{
				m_masterSessionId = copy.sessionId;
				if (copy.termsPendingCount > 0)
				{
					m_pendingTermsEditionId = copy.termsEditionId;
					m_termsTitle = copy.termsTitle;
					m_termsVersionLabel = copy.termsVersionLabel;
					m_termsLocale = copy.termsLocale;
					m_termsContent = copy.termsContent;
					m_termsTotalLength = copy.totalLength;
					m_termsScrollOffset = 0;
					m_termsScrolledToBottom = false;
					m_termsAcknowledgeChecked = false;
					m_phase = Phase::Terms;
					m_infoBanner = copy.message.empty() ? Tr("auth.info.terms_required") : copy.message;
				}
				else
				{
					ResetMasterSession();
					m_phase = Phase::Submitting;
					StartMasterFlowWorker(cfg);
				}
			}
			else
			{
				m_phase = Phase::Error;
				m_userErrorText = copy.message;
			}
			return;
		}

		if (kind == AsyncKind::VerifyEmail)
		{
			if (copy.success)
			{
				m_phase = Phase::Login;
				m_userErrorText.clear();
				m_infoBanner = copy.message.empty() ? Tr("auth.info.email_verified") : copy.message;
				LOG_INFO(Core, "[AuthUiPresenter] VerifyEmail OK: {}", m_infoBanner);
			}
			else
			{
				m_phase = Phase::Error;
				m_userErrorText = copy.message;
				LOG_WARN(Core, "[AuthUiPresenter] VerifyEmail FAILED: {}", copy.message);
			}
			return;
		}

		if (kind == AsyncKind::ForgotPassword)
		{
			m_phase = Phase::Login;
			if (copy.success)
			{
				m_userErrorText.clear();
				m_infoBanner = copy.message.empty() ? Tr("auth.info.forgot_password") : copy.message;
			}
			else
			{
				m_infoBanner = copy.message;
			}
			return;
		}

		if (kind == AsyncKind::TermsStatus)
		{
			if (copy.success)
			{
				if (copy.termsPendingCount == 0)
				{
					m_infoBanner = Tr("auth.info.terms_done");
					m_phase = Phase::CharacterCreate;
				}
				else
				{
					m_pendingTermsEditionId = copy.termsEditionId;
					m_termsTitle = copy.termsTitle;
					m_termsVersionLabel = copy.termsVersionLabel;
					m_termsLocale = copy.termsLocale;
					m_termsContent = copy.termsContent;
					m_termsTotalLength = copy.totalLength;
					m_termsScrollOffset = 0;
					m_termsScrolledToBottom = false;
					m_termsAcknowledgeChecked = false;
					m_phase = Phase::Terms;
					m_infoBanner = copy.message;
				}
			}
			else
			{
				ResetMasterSession();
				m_phase = Phase::Error;
				m_userErrorText = copy.message;
			}
			return;
		}

		if (kind == AsyncKind::TermsAccept)
		{
			if (copy.success)
			{
				if (copy.termsPendingCount == 0)
				{
					m_infoBanner = Tr("auth.info.terms_done");
					m_phase = Phase::CharacterCreate;
				}
				else
				{
					m_pendingTermsEditionId = copy.termsEditionId;
					m_termsTitle = copy.termsTitle;
					m_termsVersionLabel = copy.termsVersionLabel;
					m_termsLocale = copy.termsLocale;
					m_termsContent = copy.termsContent;
					m_termsTotalLength = copy.totalLength;
					m_termsScrollOffset = 0;
					m_termsScrolledToBottom = false;
					m_termsAcknowledgeChecked = false;
					m_phase = Phase::Terms;
					m_infoBanner = copy.message;
				}
			}
			else
			{
				ResetMasterSession();
				m_phase = Phase::Error;
				m_userErrorText = copy.message;
			}
			return;
		}

		if (kind == AsyncKind::CharacterCreate)
		{
			if (copy.success)
			{
				ResetMasterSession();
				m_infoBanner = copy.message.empty() ? Tr("auth.info.character_created") : copy.message;
				m_phase = Phase::Submitting;
				StartMasterFlowWorker(cfg);
			}
			else
			{
				m_phase = Phase::Error;
				m_userErrorText = copy.message;
			}
			return;
		}

		if (copy.success)
		{
			m_userErrorText.clear();
			m_infoBanner = copy.message;
			LOG_INFO(Core, "[AuthUiPresenter] Master/shard flow OK: {}", copy.message);
			m_flowComplete = true;
			m_phase = Phase::Login;
			return;
		}
		m_phase = Phase::Error;
		m_userErrorText = copy.message;
		LOG_WARN(Core, "[AuthUiPresenter] Master/shard flow FAILED: {}", copy.message);
	}

	void AuthUiPresenter::StartRegisterWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		EnsurePasswordSalt(cfg);
		const std::string hash = ComputeClientHash(cfg);
		if (hash.empty())
		{
			m_phase = Phase::Error;
			m_userErrorText = Tr("auth.error.hash_password_failed");
			LOG_ERROR(Core, "[AuthUiPresenter] Register aborted: empty client_hash");
			return;
		}

		const std::string host = cfg.GetString("client.master_host", "localhost");
		const uint16_t port = static_cast<uint16_t>(cfg.GetInt("client.master_port", 3840));
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const bool allowInsecure = cfg.GetBool("client.allow_insecure_dev", true);
		const std::string locale = CurrentLocale();
		const std::string login = m_login;
		const std::string email = m_email;
		const std::string firstName = m_firstName;
		const std::string lastName = m_lastName;
		const std::string birthDate = m_birthYear + "-" + m_birthMonth + "-" + m_birthDay;

		m_pendingAsyncKind = AsyncKind::Register;
		{
			std::lock_guard<std::mutex> lock(m_asyncMutex);
			m_asyncResult = {};
		}

		m_worker = std::thread([this, host, port, timeoutMs, login, email, firstName, lastName, birthDate, hash, allowInsecure, locale]() {
			AsyncResult local{};
			engine::network::NetClient client;
			client.SetAllowInsecureDev(allowInsecure);
			LOG_INFO(Net, "[AuthUiPresenter] Register worker: connecting {}:{}", host, port);
			client.Connect(host, port);
			if (!WaitConnected(&client, timeoutMs + 2000u))
			{
				local.ready = true;
				local.success = false;
				local.message = "Master connect failed or timeout.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				LOG_ERROR(Net, "[AuthUiPresenter] Register worker: connect FAILED");
				return;
			}
			engine::network::RequestResponseDispatcher disp(&client);
			std::vector<uint8_t> payload = engine::network::BuildRegisterRequestPayload(login, email, hash, firstName, lastName, birthDate, {}, locale);
			if (payload.empty())
			{
				local.ready = true;
				local.success = false;
				local.message = "REGISTER payload build failed.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				LOG_ERROR(Net, "[AuthUiPresenter] Register worker: BuildRegisterRequestPayload FAILED");
				client.Disconnect("payload");
				return;
			}

			bool done = false;
			bool ok = false;
			std::string errMsg;
			if (!disp.SendRequest(engine::network::kOpcodeRegisterRequest, payload,
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						done = true;
						if (timeout)
						{
							errMsg = "REGISTER timeout.";
							return;
						}
						auto reg = engine::network::ParseRegisterResponsePayload(pl.data(), pl.size());
						if (reg && reg->success != 0)
						{
							local.accountId = reg->account_id;
							ok = true;
							return;
						}
						if (reg && reg->success == 0)
						{
							errMsg = NetErrorLabel(reg->error_code);
							return;
						}
						auto er = engine::network::ParseErrorPayload(pl.data(), pl.size());
						if (er)
							errMsg = std::string(NetErrorLabel(er->errorCode)) + (er->message.empty() ? "" : (": " + er->message));
						else
							errMsg = "REGISTER response parse failed.";
					},
					timeoutMs))
			{
				local.ready = true;
				local.success = false;
				local.message = "Send REGISTER failed.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				client.Disconnect("send");
				LOG_ERROR(Net, "[AuthUiPresenter] Register worker: SendRequest FAILED");
				return;
			}

			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
			while (!done && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			if (!done)
				errMsg = "REGISTER timeout.";
			client.Disconnect("register_done");

			local.ready = true;
			local.success = ok;
			local.message = ok ? std::string("Registration OK. Check your email for the verification code.") : (errMsg.empty() ? "REGISTER failed." : errMsg);
			{
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
			}
			LOG_INFO(Net, "[AuthUiPresenter] Register worker finished (success={})", (int)ok);
		});
	}

	void AuthUiPresenter::StartLoginWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		EnsurePasswordSalt(cfg);
		const std::string hash = ComputeClientHash(cfg);
		if (hash.empty())
		{
			m_phase = Phase::Error;
			m_userErrorText = Tr("auth.error.hash_password_failed");
			return;
		}

		ResetMasterSession();
		m_masterClient = std::make_unique<engine::network::NetClient>();
		const std::string host = cfg.GetString("client.master_host", "localhost");
		const uint16_t port = static_cast<uint16_t>(cfg.GetInt("client.master_port", 3840));
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const bool allowInsecure = cfg.GetBool("client.allow_insecure_dev", true);
		const std::string login = m_login;
		const std::string locale = CurrentLocale();

		m_pendingAsyncKind = AsyncKind::AuthOnly;
		{
			std::lock_guard<std::mutex> lock(m_asyncMutex);
			m_asyncResult = {};
		}

		engine::network::NetClient* const masterClient = m_masterClient.get();
		m_worker = std::thread([this, masterClient, host, port, timeoutMs, allowInsecure, login, hash, locale]() {
			AsyncResult local{};
			if (masterClient == nullptr)
			{
				local.ready = true;
				local.message = "Internal error: master client missing.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			masterClient->SetAllowInsecureDev(allowInsecure);
			masterClient->Connect(host, port);
			if (!WaitConnected(masterClient, timeoutMs + 2000u))
			{
				local.ready = true;
				local.message = "Master connect failed or timeout.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				return;
			}

			engine::network::RequestResponseDispatcher disp(masterClient);
			bool authDone = false;
			bool authOk = false;
			std::string errMsg;
			if (!disp.SendRequest(engine::network::kOpcodeAuthRequest, engine::network::BuildAuthRequestPayload(login, hash),
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						authDone = true;
						if (timeout)
						{
							errMsg = "AUTH timeout.";
							return;
						}
						auto auth = engine::network::ParseAuthResponsePayload(pl.data(), pl.size());
						if (auth && auth->success != 0)
						{
							local.sessionId = auth->session_id;
							authOk = true;
							return;
						}
						auto er = engine::network::ParseErrorPayload(pl.data(), pl.size());
						if (er)
							errMsg = std::string(NetErrorLabel(er->errorCode)) + (er->message.empty() ? "" : (": " + er->message));
						else
							errMsg = "AUTH failed.";
					},
					timeoutMs))
			{
				local.ready = true;
				local.message = "Send AUTH failed.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
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
				masterClient->Disconnect("auth_failed");
				local.ready = true;
				local.message = errMsg.empty() ? "AUTH failed." : errMsg;
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				return;
			}

			disp.SetSessionId(local.sessionId);
			bool statusDone = false;
			if (!disp.SendRequest(engine::network::kOpcodeTermsStatusRequest, engine::network::BuildTermsStatusRequestPayload(locale),
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						statusDone = true;
						if (timeout)
						{
							errMsg = "TERMS status timeout.";
							return;
						}
						auto terms = engine::network::ParseTermsStatusResponsePayload(pl.data(), pl.size());
						if (!terms)
						{
							errMsg = "TERMS status parse failed.";
							return;
						}
						local.termsPendingCount = terms->pending_count;
						local.termsEditionId = terms->next_edition_id;
						local.termsTitle = terms->title;
						local.termsVersionLabel = terms->version_label;
						local.termsLocale = terms->resolved_locale;
					},
					timeoutMs))
			{
				masterClient->Disconnect("terms_status_send_failed");
				local.ready = true;
				local.message = "Send TERMS_STATUS failed.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
			while (!statusDone && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			if (!statusDone)
			{
				masterClient->Disconnect("terms_status_timeout");
				local.ready = true;
				local.message = "TERMS status timeout.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				return;
			}

			if (local.termsPendingCount > 0 && local.termsEditionId != 0)
			{
				bool contentDone = false;
				if (!disp.SendRequest(engine::network::kOpcodeTermsContentRequest,
						engine::network::BuildTermsContentRequestPayload(local.termsEditionId, local.termsLocale, 0u, 8192u),
						[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
							contentDone = true;
							if (timeout)
							{
								errMsg = "TERMS content timeout.";
								return;
							}
							auto content = engine::network::ParseTermsContentResponsePayload(pl.data(), pl.size());
							if (!content)
							{
								errMsg = "TERMS content parse failed.";
								return;
							}
							local.totalLength = content->total_length;
							local.termsContent = content->chunk;
						},
						timeoutMs))
				{
					masterClient->Disconnect("terms_content_send_failed");
					local.ready = true;
					local.message = "Send TERMS_CONTENT failed.";
					std::lock_guard<std::mutex> lock(m_asyncMutex);
					m_asyncResult = local;
					return;
				}
				deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
				while (!contentDone && std::chrono::steady_clock::now() < deadline)
				{
					disp.Pump();
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
				}
				if (!contentDone)
				{
					masterClient->Disconnect("terms_content_timeout");
					local.ready = true;
					local.message = "TERMS content timeout.";
					std::lock_guard<std::mutex> lock(m_asyncMutex);
					m_asyncResult = local;
					return;
				}
				local.message = "Terms acceptance required before entering the game.";
			}
			else
			{
				local.message = "Authentication successful.";
			}

			local.ready = true;
			local.success = true;
			std::lock_guard<std::mutex> lock(m_asyncMutex);
			m_asyncResult = local;
		});
	}

	void AuthUiPresenter::StartMasterFlowWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		EnsurePasswordSalt(cfg);
		const std::string hash = ComputeClientHash(cfg);
		if (hash.empty())
		{
			m_phase = Phase::Error;
			m_userErrorText = Tr("auth.error.hash_password_failed");
			LOG_ERROR(Core, "[AuthUiPresenter] Master flow aborted: empty client_hash");
			return;
		}

		const std::string host = cfg.GetString("client.master_host", "localhost");
		const uint16_t port = static_cast<uint16_t>(cfg.GetInt("client.master_port", 3840));
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const bool allowInsecure = cfg.GetBool("client.allow_insecure_dev", true);
		const std::string login = m_login;

		m_pendingAsyncKind = AsyncKind::Login;
		{
			std::lock_guard<std::mutex> lock(m_asyncMutex);
			m_asyncResult = {};
		}

		m_worker = std::thread([this, host, port, timeoutMs, login, hash, allowInsecure]() {
			AsyncResult local{};
			engine::network::NetClient masterClient;
			masterClient.SetAllowInsecureDev(allowInsecure);
			engine::network::MasterShardClientFlow flow;
			flow.SetMasterAddress(host, port);
			flow.SetCredentials(login, hash);
			flow.SetTimeoutMs(timeoutMs);
			LOG_INFO(Net, "[AuthUiPresenter] MasterShardClientFlow starting (login='{}')", login);
			engine::network::MasterShardFlowResult r = flow.Run(&masterClient);
			local.ready = true;
			local.success = r.success;
			local.message = r.success ? (std::string("Shard ready (shard_id=") + std::to_string(r.shard_id) + ").") : r.errorMessage;
			{
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
			}
			LOG_INFO(Net, "[AuthUiPresenter] MasterShardClientFlow finished (success={})", (int)r.success);
		});
	}

	void AuthUiPresenter::StartVerifyEmailWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		const std::string host = cfg.GetString("client.master_host", "localhost");
		const uint16_t port = static_cast<uint16_t>(cfg.GetInt("client.master_port", 3840));
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const bool allowInsecure = cfg.GetBool("client.allow_insecure_dev", true);
		const uint64_t accountId = m_pendingVerifyAccountId;
		const std::string code = m_verifyCode;

		m_pendingAsyncKind = AsyncKind::VerifyEmail;
		{
			std::lock_guard<std::mutex> lock(m_asyncMutex);
			m_asyncResult = {};
		}

		m_worker = std::thread([this, host, port, timeoutMs, allowInsecure, accountId, code]() {
			AsyncResult local{};
			engine::network::NetClient client;
			client.SetAllowInsecureDev(allowInsecure);
			client.Connect(host, port);
			if (!WaitConnected(&client, timeoutMs + 2000u))
			{
				local.ready = true;
				local.message = "Master connect failed or timeout.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			engine::network::RequestResponseDispatcher disp(&client);
			std::vector<uint8_t> payload = engine::network::BuildVerifyEmailRequestPayload(accountId, code);
			bool done = false;
			bool ok = false;
			std::string errMsg;
			if (!disp.SendRequest(engine::network::kOpcodeVerifyEmailRequest, payload,
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						done = true;
						if (timeout)
						{
							errMsg = "VERIFY_EMAIL timeout.";
							return;
						}
						auto er = engine::network::ParseErrorPayload(pl.data(), pl.size());
						if (er)
						{
							errMsg = std::string(NetErrorLabel(er->errorCode));
							return;
						}
						ok = true;
					},
					timeoutMs))
			{
				local.ready = true;
				local.message = "Send VERIFY_EMAIL failed.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
			while (!done && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			local.ready = true;
			local.success = ok;
			local.message = ok ? std::string("Email verified successfully.") : (errMsg.empty() ? "VERIFY_EMAIL failed." : errMsg);
			std::lock_guard<std::mutex> lock(m_asyncMutex);
			m_asyncResult = local;
		});
	}

	void AuthUiPresenter::StartTermsStatusWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		if (!m_masterClient || m_masterSessionId == 0)
		{
			m_phase = Phase::Error;
			m_userErrorText = Tr("auth.error.terms_session_inactive");
			return;
		}
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const std::string locale = CurrentLocale();

		m_pendingAsyncKind = AsyncKind::TermsStatus;
		{
			std::lock_guard<std::mutex> lock(m_asyncMutex);
			m_asyncResult = {};
		}

		engine::network::NetClient* const masterClient = m_masterClient.get();
		const uint64_t sessionId = m_masterSessionId;
		m_worker = std::thread([this, masterClient, sessionId, timeoutMs, locale]() {
			AsyncResult local{};
			if (masterClient == nullptr)
			{
				local.ready = true;
				local.message = "Internal error: master client missing.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			engine::network::RequestResponseDispatcher disp(masterClient);
			disp.SetSessionId(sessionId);
			bool statusDone = false;
			std::string errMsg;
			if (!disp.SendRequest(engine::network::kOpcodeTermsStatusRequest, engine::network::BuildTermsStatusRequestPayload(locale),
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						statusDone = true;
						if (timeout)
						{
							errMsg = "TERMS status timeout.";
							return;
						}
						auto terms = engine::network::ParseTermsStatusResponsePayload(pl.data(), pl.size());
						if (!terms)
						{
							errMsg = "TERMS status parse failed.";
							return;
						}
						local.termsPendingCount = terms->pending_count;
						local.termsEditionId = terms->next_edition_id;
						local.termsTitle = terms->title;
						local.termsVersionLabel = terms->version_label;
						local.termsLocale = terms->resolved_locale;
					}, timeoutMs))
			{
				local.ready = true;
				local.message = "Send TERMS_STATUS failed.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
			while (!statusDone && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			if (!statusDone)
			{
				local.ready = true;
				local.message = "TERMS status timeout.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			if (local.termsPendingCount > 0 && local.termsEditionId != 0)
			{
				bool contentDone = false;
				if (!disp.SendRequest(engine::network::kOpcodeTermsContentRequest,
						engine::network::BuildTermsContentRequestPayload(local.termsEditionId, local.termsLocale, 0u, 8192u),
						[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
							contentDone = true;
							if (timeout)
							{
								errMsg = "TERMS content timeout.";
								return;
							}
							auto content = engine::network::ParseTermsContentResponsePayload(pl.data(), pl.size());
							if (!content)
							{
								errMsg = "TERMS content parse failed.";
								return;
							}
							local.totalLength = content->total_length;
							local.termsContent = content->chunk;
						}, timeoutMs))
				{
					local.ready = true;
					local.message = "Send TERMS_CONTENT failed.";
					std::lock_guard<std::mutex> lock(m_asyncMutex);
					m_asyncResult = local;
					return;
				}
				deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
				while (!contentDone && std::chrono::steady_clock::now() < deadline)
				{
					disp.Pump();
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
				}
				if (!contentDone)
				{
					local.ready = true;
					local.message = errMsg.empty() ? "TERMS content timeout." : errMsg;
					std::lock_guard<std::mutex> lock(m_asyncMutex);
					m_asyncResult = local;
					return;
				}
			}
			local.ready = true;
			local.success = true;
			local.message = local.termsPendingCount > 0 ? "Please review and accept the pending terms." : "No pending terms.";
			std::lock_guard<std::mutex> lock(m_asyncMutex);
			m_asyncResult = local;
		});
	}

	void AuthUiPresenter::StartTermsAcceptWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		if (!m_masterClient || m_masterSessionId == 0 || m_pendingTermsEditionId == 0)
		{
			m_phase = Phase::Error;
			m_userErrorText = Tr("auth.error.terms_session_inactive");
			return;
		}
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const std::string locale = CurrentLocale();
		const uint64_t editionId = m_pendingTermsEditionId;

		m_pendingAsyncKind = AsyncKind::TermsAccept;
		{
			std::lock_guard<std::mutex> lock(m_asyncMutex);
			m_asyncResult = {};
		}

		engine::network::NetClient* const masterClient = m_masterClient.get();
		const uint64_t sessionId = m_masterSessionId;
		m_worker = std::thread([this, masterClient, sessionId, timeoutMs, locale, editionId]() {
			AsyncResult local{};
			if (masterClient == nullptr)
			{
				local.ready = true;
				local.message = "Internal error: master client missing.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			engine::network::RequestResponseDispatcher disp(masterClient);
			disp.SetSessionId(sessionId);
			bool acceptDone = false;
			std::string errMsg;
			if (!disp.SendRequest(engine::network::kOpcodeTermsAcceptRequest, engine::network::BuildTermsAcceptRequestPayload(editionId, 1u),
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						acceptDone = true;
						if (timeout)
						{
							errMsg = "TERMS accept timeout.";
							return;
						}
						if (!pl.empty() && pl[0] == 0)
							errMsg = "TERMS accept failed.";
					}, timeoutMs))
			{
				local.ready = true;
				local.message = "Send TERMS_ACCEPT failed.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
			while (!acceptDone && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			if (!acceptDone || !errMsg.empty())
			{
				local.ready = true;
				local.message = errMsg.empty() ? "TERMS accept timeout." : errMsg;
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				return;
			}

			bool statusDone = false;
			if (!disp.SendRequest(engine::network::kOpcodeTermsStatusRequest, engine::network::BuildTermsStatusRequestPayload(locale),
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						statusDone = true;
						if (timeout)
						{
							errMsg = "TERMS status timeout.";
							return;
						}
						auto terms = engine::network::ParseTermsStatusResponsePayload(pl.data(), pl.size());
						if (!terms)
						{
							errMsg = "TERMS status parse failed.";
							return;
						}
						local.termsPendingCount = terms->pending_count;
						local.termsEditionId = terms->next_edition_id;
						local.termsTitle = terms->title;
						local.termsVersionLabel = terms->version_label;
						local.termsLocale = terms->resolved_locale;
					}, timeoutMs))
			{
				local.ready = true;
				local.message = "Send TERMS_STATUS failed.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
			while (!statusDone && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			if (!statusDone)
			{
				local.ready = true;
				local.message = "TERMS status timeout.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			if (local.termsPendingCount > 0 && local.termsEditionId != 0)
			{
				bool contentDone = false;
				if (!disp.SendRequest(engine::network::kOpcodeTermsContentRequest,
						engine::network::BuildTermsContentRequestPayload(local.termsEditionId, local.termsLocale, 0u, 8192u),
						[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
							contentDone = true;
							if (timeout)
							{
								errMsg = "TERMS content timeout.";
								return;
							}
							auto content = engine::network::ParseTermsContentResponsePayload(pl.data(), pl.size());
							if (!content)
							{
								errMsg = "TERMS content parse failed.";
								return;
							}
							local.totalLength = content->total_length;
							local.termsContent = content->chunk;
						}, timeoutMs))
				{
					local.ready = true;
					local.message = "Send TERMS_CONTENT failed.";
					std::lock_guard<std::mutex> lock(m_asyncMutex);
					m_asyncResult = local;
					return;
				}
				deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
				while (!contentDone && std::chrono::steady_clock::now() < deadline)
				{
					disp.Pump();
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
				}
			}
			local.ready = true;
			local.success = true;
			local.message = local.termsPendingCount > 0 ? "Next pending terms loaded." : "All terms accepted.";
			std::lock_guard<std::mutex> lock(m_asyncMutex);
			m_asyncResult = local;
		});
	}

	void AuthUiPresenter::StartCharacterCreateWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		if (!m_masterClient || m_masterSessionId == 0)
		{
			m_phase = Phase::Error;
			m_userErrorText = Tr("auth.error.character_session_inactive");
			return;
		}
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const std::string characterName = m_characterName;

		m_pendingAsyncKind = AsyncKind::CharacterCreate;
		{
			std::lock_guard<std::mutex> lock(m_asyncMutex);
			m_asyncResult = {};
		}

		engine::network::NetClient* const masterClient = m_masterClient.get();
		const uint64_t sessionId = m_masterSessionId;
		m_worker = std::thread([this, masterClient, sessionId, timeoutMs, characterName]() {
			AsyncResult local{};
			if (masterClient == nullptr)
			{
				local.ready = true;
				local.message = "Internal error: master client missing.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			engine::network::RequestResponseDispatcher disp(masterClient);
			disp.SetSessionId(sessionId);
			bool done = false;
			std::string errMsg;
			if (!disp.SendRequest(engine::network::kOpcodeCharacterCreateRequest,
					engine::network::BuildCharacterCreateRequestPayload(characterName),
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						done = true;
						if (timeout)
						{
							errMsg = "CHARACTER_CREATE timeout.";
							return;
						}
						auto resp = engine::network::ParseCharacterCreateResponsePayload(pl.data(), pl.size());
						if (resp && resp->success != 0)
						{
							local.accountId = resp->character_id;
							return;
						}
						auto er = engine::network::ParseErrorPayload(pl.data(), pl.size());
						if (er)
							errMsg = er->message.empty() ? std::string(NetErrorLabel(er->errorCode)) : er->message;
						else
							errMsg = "Character creation failed.";
					},
					timeoutMs))
			{
				local.ready = true;
				local.message = "Send CHARACTER_CREATE failed.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
			while (!done && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			local.ready = true;
			local.success = done && errMsg.empty();
			local.message = local.success ? std::string("Character created successfully.") : (errMsg.empty() ? "Character creation timeout." : errMsg);
			std::lock_guard<std::mutex> lock(m_asyncMutex);
			m_asyncResult = local;
		});
	}

	void AuthUiPresenter::StartForgotPasswordWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		const std::string host = cfg.GetString("client.master_host", "localhost");
		const uint16_t port = static_cast<uint16_t>(cfg.GetInt("client.master_port", 3840));
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const bool allowInsecure = cfg.GetBool("client.allow_insecure_dev", true);
		const std::string email = m_email;

		m_pendingAsyncKind = AsyncKind::ForgotPassword;
		{
			std::lock_guard<std::mutex> lock(m_asyncMutex);
			m_asyncResult = {};
		}

		m_worker = std::thread([this, host, port, timeoutMs, allowInsecure, email]() {
			AsyncResult local{};
			engine::network::NetClient client;
			client.SetAllowInsecureDev(allowInsecure);
			client.Connect(host, port);
			if (!WaitConnected(&client, timeoutMs + 2000u))
			{
				local.ready = true;
				local.message = "Master connect failed or timeout.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			engine::network::RequestResponseDispatcher disp(&client);
			std::vector<uint8_t> payload = engine::network::BuildForgotPasswordRequestPayload(email);
			bool done = false;
			if (!disp.SendRequest(engine::network::kOpcodeForgotPasswordRequest, payload,
					[&](uint32_t, bool, std::vector<uint8_t>) {
						done = true;
					},
					timeoutMs))
			{
				local.ready = true;
				local.message = "Send FORGOT_PASSWORD failed.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
			while (!done && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			local.ready = true;
			local.success = done;
			local.message = done ? "If the email exists, a reset message has been sent." : "FORGOT_PASSWORD timeout.";
			std::lock_guard<std::mutex> lock(m_asyncMutex);
			m_asyncResult = local;
		});
	}

	bool AuthUiPresenter::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0)
		{
			LOG_WARN(Core, "[AuthUiPresenter] SetViewportSize FAILED: {}x{}", width, height);
			return false;
		}
		m_viewportW = width;
		m_viewportH = height;
		LOG_INFO(Core, "[AuthUiPresenter] Viewport {}x{}", width, height);
		return true;
	}

bool AuthUiPresenter::HandleNativeAuthScreen(engine::platform::Window& window, const engine::core::Config& cfg)
{
#if defined(_WIN32)
	auto phaseName = [](Phase phase) -> const char*
	{
		switch (phase)
		{
		case Phase::Login: return "Login";
		case Phase::Register: return "Register";
		case Phase::ForgotPassword: return "ForgotPassword";
		case Phase::VerifyEmail: return "VerifyEmail";
		case Phase::LanguageSelectionFirstRun: return "LanguageSelectionFirstRun";
		case Phase::LanguageOptions: return "LanguageOptions";
		case Phase::Submitting: return "Submitting";
		case Phase::Error: return "Error";
		default: return "Unknown";
		}
	};

	if (m_phase == Phase::Login || m_phase == Phase::ForgotPassword || m_phase == Phase::Register)
	{
		if (m_phase == Phase::Login)
		{
			m_login = window.GetAuthPrimaryValue();
			m_password = window.GetAuthPasswordValue();
			m_rememberLogin = window.GetAuthRememberChecked();
			if (m_rememberLogin != m_savedRememberLogin)
			{
				SaveRememberPreference();
			}
		}
		else if (m_phase == Phase::ForgotPassword)
		{
			m_email = window.GetAuthPrimaryValue();
		}

		switch (window.ConsumeAuthScreenCommand())
		{
		case engine::platform::Window::AuthScreenCommand::Submit:
			LOG_INFO(Core, "[AuthUiPresenter] Submit requested (phase={}, login_empty={}, password_empty={}, email_empty={})",
				phaseName(m_phase), m_login.empty(), m_password.empty(), m_email.empty());
			SubmitCurrentPhase(cfg);
			break;
		case engine::platform::Window::AuthScreenCommand::Quit:
			window.RequestClose();
			break;
		case engine::platform::Window::AuthScreenCommand::OpenRegister:
			LOG_INFO(Core, "[AuthUiPresenter] Phase change: {} -> Register", phaseName(m_phase));
			m_phase = Phase::Register;
			m_activeField = 0;
			m_userErrorText.clear();
			break;
		case engine::platform::Window::AuthScreenCommand::OpenForgotPassword:
		{
			const std::string resetUrl = ResolvePasswordRecoveryUrl(cfg);
			LOG_INFO(Core, "[AuthUiPresenter] Open password recovery portal from phase={} url={}", phaseName(m_phase), resetUrl);
			if (!window.OpenExternalUrl(resetUrl))
			{
				m_phase = Phase::Error;
				m_userErrorText = Tr("auth.error.open_recovery_portal");
			}
			break;
		}
		case engine::platform::Window::AuthScreenCommand::BackToLogin:
			LOG_INFO(Core, "[AuthUiPresenter] Phase change: {} -> Login", phaseName(m_phase));
			m_phase = Phase::Login;
			m_activeField = 0;
			m_userErrorText.clear();
			break;
		case engine::platform::Window::AuthScreenCommand::None:
		default:
			break;
		}

		engine::platform::Window::AuthScreenState state{};
		state.visible = true;
		state.showPassword = m_phase == Phase::Login;
		state.showRemember = m_phase == Phase::Login;
		state.showForgot = m_phase == Phase::Login;
		state.showRegister = m_phase == Phase::Login;
		state.showBack = false;
		state.showQuit = true;
		state.showInfoImage = m_phase == Phase::Register;
		state.rememberChecked = m_rememberLogin;
		state.focusPrimary = m_activeField == 0;
		state.focusPassword = (m_phase == Phase::Login) && (m_activeField == 1);
		state.titleLine1 = Tr("auth.title_line1");
		state.titleLine2 = Tr("auth.title_line2");
		state.sectionTitle = m_phase == Phase::Login ? Tr("auth.section.login")
			: (m_phase == Phase::ForgotPassword ? Tr("auth.section.forgot_password") : Tr("auth.section.register"));
		state.primaryLabel = m_phase == Phase::Register ? "" : Tr("common.login_or_email");
		state.primaryValue = (m_phase == Phase::Login) ? m_login : m_email;
		state.passwordLabel = Tr("auth.label.password");
		state.passwordValue = m_password;
		state.rememberLabel = Tr("auth.checkbox.remember");
		state.forgotLabel = Tr("auth.button.forgot_password");
		state.registerLabel = Tr("auth.button.register");
		state.submitLabel = m_phase == Phase::Register ? "" : Tr("common.submit");
		state.quitLabel = Tr("common.quit");
		state.backgroundImagePath = m_phase == Phase::Register ? std::string(kRegisterBackgroundPath) : std::string(kLoginBackgroundPath);
		state.logoImagePath = m_phase == Phase::Login ? std::string(kLoginLogoPath) : "";
		state.infoImagePath = m_phase == Phase::Register ? std::string(kRegisterInfoPath) : "";
		window.SetAuthScreenState(state);
		return true;
	}
	window.SetAuthScreenState({});
	(void)cfg;
	return false;
}
#endif
void AuthUiPresenter::SubmitCurrentPhase(const engine::core::Config& cfg)
{
	if (m_phase == Phase::Error)
	{
		m_phase = Phase::Login;
		m_userErrorText.clear();
		LOG_INFO(Core, "[AuthUiPresenter] Error acknowledged, back to Login");
		return;
	}
	if (m_phase == Phase::Login)
	{
		if (m_login.empty() || m_password.empty())
		{
			m_phase = Phase::Error;
			m_userErrorText = Tr("auth.error.enter_login_password");
			LOG_WARN(Core, "[AuthUiPresenter] Submit rejected: empty fields");
			return;
		}
		m_phase = Phase::Submitting;
		StartLoginWorker(cfg);
		return;
	}
	if (m_phase == Phase::Register)
	{
		if (m_login.empty() || m_password.empty() || m_email.empty() || m_firstName.empty() || m_lastName.empty()
			|| m_birthDay.empty() || m_birthMonth.empty() || m_birthYear.empty())
		{
			m_phase = Phase::Error;
			m_userErrorText = Tr("auth.error.enter_register_fields");
			LOG_WARN(Core, "[AuthUiPresenter] Register submit rejected: empty fields");
			return;
		}
		if (!IsValidBirthDateFields(m_birthDay, m_birthMonth, m_birthYear))
		{
			m_phase = Phase::Error;
			m_userErrorText = Tr("auth.error.invalid_birth_date");
			return;
		}
		m_phase = Phase::Submitting;
		StartRegisterWorker(cfg);
		return;
	}
	if (m_phase == Phase::VerifyEmail)
	{
		if (m_pendingVerifyAccountId == 0 || m_verifyCode.empty())
		{
			m_phase = Phase::Error;
			m_userErrorText = Tr("auth.error.enter_verify_code");
			return;
		}
		if (!IsValidVerificationCode(m_verifyCode))
		{
			m_phase = Phase::Error;
			m_userErrorText = Tr("auth.error.invalid_verify_code");
			return;
		}
		m_phase = Phase::Submitting;
		StartVerifyEmailWorker(cfg);
		return;
	}
	if (m_phase == Phase::ForgotPassword)
	{
		if (m_email.empty())
		{
			m_phase = Phase::Error;
			m_userErrorText = Tr("auth.error.enter_recovery_email");
			return;
		}
		m_phase = Phase::Submitting;
		StartForgotPasswordWorker(cfg);
		return;
	}
	if (m_phase == Phase::Terms)
	{
		if (!m_termsScrolledToBottom || !m_termsAcknowledgeChecked)
		{
			m_phase = Phase::Error;
			m_userErrorText = Tr("auth.error.accept_terms");
			return;
		}
		m_phase = Phase::Submitting;
		StartTermsAcceptWorker(cfg);
		return;
	}
	if (m_phase == Phase::CharacterCreate)
	{
		if (m_characterName.empty())
		{
			m_phase = Phase::Error;
			m_userErrorText = Tr("auth.error.enter_character_name");
			return;
		}
		if (!IsValidCharacterNameLocal(m_characterName))
		{
			m_phase = Phase::Error;
			m_userErrorText = Tr("auth.error.invalid_character_name");
			return;
		}
		m_phase = Phase::Submitting;
		StartCharacterCreateWorker(cfg);
	}
}
#endif

	void AuthUiPresenter::Update(engine::platform::Input& input, float deltaSeconds, engine::platform::Window& window,
		const engine::core::Config& cfg)
	{
		(void)deltaSeconds;
		m_usingNativeAuthScreen = false;
		window.SetAuthScreenState({});
		if (!m_initialized || m_flowComplete || !m_authEnabled)
			return;

		PollAsyncResult(cfg);
		if (m_flowComplete)
			return;

		if (m_phase == Phase::Submitting)
		{
			window.SetAuthScreenState({});
			UpdateWindowTitle(window);
			return;
		}

		const bool usingNativeAuth = false;
		(void)cfg;

		auto currentField = [this]() -> std::string* {
			switch (m_phase)
			{
			case Phase::Login:
				return m_activeField == 0 ? &m_login : &m_password;
			case Phase::Register:
				switch (m_activeField)
				{
				case 0: return &m_login;
				case 1: return &m_password;
				case 2: return &m_email;
				case 3: return &m_firstName;
				case 4: return &m_lastName;
				case 5: return &m_birthDay;
				case 6: return &m_birthMonth;
				default: return &m_birthYear;
				}
			case Phase::VerifyEmail:
				return &m_verifyCode;
			case Phase::ForgotPassword:
				return &m_email;
			case Phase::Terms:
				return nullptr;
			case Phase::CharacterCreate:
				return &m_characterName;
			case Phase::LanguageSelectionFirstRun:
			case Phase::LanguageOptions:
			default:
				return nullptr;
			}
		};

		auto applyPrimaryAction = [&]()
		{
			if (m_phase == Phase::LanguageSelectionFirstRun)
			{
				ApplyLocaleSelection(true);
			}
			else if (m_phase == Phase::LanguageOptions)
			{
				m_videoFullscreen = m_videoFullscreenPending;
				m_videoVsync = m_videoVsyncPending;
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
				m_pendingVideoSettings.applyRequested = true;
				m_pendingVideoSettings.fullscreen = m_videoFullscreen;
				m_pendingVideoSettings.vsync = m_videoVsync;
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
			else if (m_phase == Phase::Terms)
			{
				if (!m_termsScrolledToBottom)
					return;
				if (!m_termsAcknowledgeChecked)
				{
					m_termsAcknowledgeChecked = true;
					return;
				}
				SubmitCurrentPhase(cfg);
			}
			else
			{
				SubmitCurrentPhase(cfg);
			}
		};

		if (!usingNativeAuth)
		{
			const RenderModel model = BuildRenderModel();
			if (model.visible && m_viewportW > 0 && m_viewportH > 0)
			{
				const int32_t w = static_cast<int32_t>(m_viewportW);
				const int32_t h = static_cast<int32_t>(m_viewportH);
				const int32_t panelW = std::clamp(w * 42 / 100, 540, 820);
				const bool largeContent = (m_phase == Phase::Terms) || model.bodyLines.size() > 6u || model.fields.size() > 5u;
				const int32_t panelH = largeContent ? std::clamp(h * 74 / 100, 440, 780) : std::clamp(h * 62 / 100, 380, 660);
				const int32_t panelX = (w - panelW) / 2;
				const int32_t panelY = (h - panelH) / 2;
				const int32_t artW = std::clamp(panelW / 3, 150, 240);
				const int32_t contentX = panelX + 28 + artW + 18;
				const int32_t contentW = std::max(180, panelW - (contentX - panelX) - 28);
				const bool compactSingleField = model.fields.size() <= 1u;
				const int32_t topOffset = !model.infoBanner.empty() ? 146 : 104;
				const int32_t fieldStep = compactSingleField ? 48 : 42;
				const int32_t mx = input.MouseX();
				const int32_t my = input.MouseY();
				m_hoveredFieldIndex = -1;
				m_hoveredBodyLineIndex = -1;
				m_hoveredActionIndex = -1;

				auto contains = [](int32_t px, int32_t py, int32_t x, int32_t y, int32_t rw, int32_t rh)
				{
					return px >= x && py >= y && px < (x + rw) && py < (y + rh);
				};

				if (m_phase == Phase::Terms && input.MouseScrollDelta() != 0)
				{
					const int scrollDir = input.MouseScrollDelta() > 0 ? -24 : 24;
					const int next = static_cast<int>(m_termsScrollOffset) + scrollDir;
					m_termsScrollOffset = static_cast<uint32_t>(std::max(0, next));
				}

				for (size_t i = 0; i < model.fields.size(); ++i)
				{
					const int32_t y = panelY + topOffset + static_cast<int32_t>(i) * fieldStep;
					if (contains(mx, my, contentX, y, contentW, 32))
					{
						m_hoveredFieldIndex = static_cast<int32_t>(i);
						break;
					}
				}

				const int32_t bodyStartY = panelY + topOffset + static_cast<int32_t>(model.fields.size()) * fieldStep + 16;
				for (int32_t localIdx = 0; localIdx < model.visibleBodyLineCount; ++localIdx)
				{
					const int32_t bodyIndex = model.visibleBodyLineStart + localIdx;
					const int32_t y = bodyStartY + localIdx * 18;
					if (contains(mx, my, contentX - 4, y - 6, contentW, 14))
					{
						m_hoveredBodyLineIndex = bodyIndex;
						break;
					}
				}
				const int32_t actionCount = std::max<int32_t>(1, static_cast<int32_t>(model.actions.size()));
				const int32_t gap = 10;
				const int32_t buttonY = std::min(panelY + panelH - 84, bodyStartY + static_cast<int32_t>(model.bodyLines.size()) * 18 + 20);
				const int32_t actionW = std::max(100, (contentW - (actionCount - 1) * gap) / actionCount);
				for (int32_t i = 0; i < actionCount; ++i)
				{
					const int32_t x = contentX + i * (actionW + gap);
					if (contains(mx, my, x, buttonY, actionW, 42))
					{
						m_hoveredActionIndex = i;
						break;
					}
				}

				const bool leftClick = input.WasMousePressed(engine::platform::MouseButton::Left);
				const bool rightClick = input.WasMousePressed(engine::platform::MouseButton::Right);
				if (leftClick || rightClick)
				{
					for (size_t i = 0; i < model.fields.size(); ++i)
					{
						const int32_t y = panelY + topOffset + static_cast<int32_t>(i) * fieldStep;
						if (contains(mx, my, contentX, y, contentW, 32))
						{
							m_activeField = static_cast<uint32_t>(i);
							break;
						}
					}

					if (m_hoveredBodyLineIndex >= 0)
					{
						if (m_phase == Phase::Login && m_hoveredBodyLineIndex == 0)
						{
							m_rememberLogin = !m_rememberLogin;
							SaveRememberPreference();
						}
						else if (m_phase == Phase::LanguageSelectionFirstRun)
						{
							const int32_t localeIdx = m_hoveredBodyLineIndex - 1;
							const auto& locales = m_localization.GetAvailableLocales();
							if (localeIdx >= 0 && static_cast<size_t>(localeIdx) < locales.size())
							{
								m_languageSelectionIndex = static_cast<uint32_t>(localeIdx);
								m_selectedLocale = locales[static_cast<size_t>(localeIdx)];
							}
							else if (rightClick && !locales.empty())
							{
								m_languageSelectionIndex = (m_languageSelectionIndex == 0u)
									? static_cast<uint32_t>(locales.size() - 1u)
									: (m_languageSelectionIndex - 1u);
								m_selectedLocale = locales[m_languageSelectionIndex];
							}
						}
						else if (m_phase == Phase::LanguageOptions)
						{
							const auto& locales = m_localization.GetAvailableLocales();
							if (m_hoveredBodyLineIndex >= 1 && static_cast<size_t>(m_hoveredBodyLineIndex - 1) < locales.size())
							{
								m_optionsSelectionIndex = 0u;
								m_languageSelectionIndex = static_cast<uint32_t>(m_hoveredBodyLineIndex - 1);
								m_selectedLocale = locales[static_cast<size_t>(m_hoveredBodyLineIndex - 1)];
							}
							else
							{
								const int32_t optionLine = m_hoveredBodyLineIndex - (1 + static_cast<int32_t>(locales.size()));
								if (optionLine >= 0 && optionLine <= 11)
								{
									m_optionsSelectionIndex = static_cast<uint32_t>(optionLine + 1);
									switch (optionLine)
									{
									case 0: m_videoFullscreenPending = !m_videoFullscreenPending; break;
									case 1: m_videoVsyncPending = !m_videoVsyncPending; break;
									case 2: m_audioMasterVolumePending = ClampOptionStep(m_audioMasterVolumePending + (rightClick ? -0.1f : 0.1f)); break;
									case 3: m_audioMusicVolumePending = ClampOptionStep(m_audioMusicVolumePending + (rightClick ? -0.1f : 0.1f)); break;
									case 4: m_audioSfxVolumePending = ClampOptionStep(m_audioSfxVolumePending + (rightClick ? -0.1f : 0.1f)); break;
									case 5: m_audioUiVolumePending = ClampOptionStep(m_audioUiVolumePending + (rightClick ? -0.1f : 0.1f)); break;
									case 6: m_mouseSensitivityPending = rightClick ? std::max(0.001f, m_mouseSensitivityPending - 0.001f) : std::min(0.010f, m_mouseSensitivityPending + 0.001f); break;
									case 7: m_invertYPending = !m_invertYPending; break;
									case 8: m_useZqsdPending = !m_useZqsdPending; break;
									case 9: m_gameplayUdpEnabledPending = !m_gameplayUdpEnabledPending; break;
									case 10: m_allowInsecureDevPending = !m_allowInsecureDevPending; break;
									case 11: m_authTimeoutMsPending = rightClick
										? ((m_authTimeoutMsPending > 1000u) ? (m_authTimeoutMsPending - 1000u) : 1000u)
										: std::min<uint32_t>(15000u, m_authTimeoutMsPending + 1000u); break;
									default: break;
									}
								}
							}
						}
						else if (m_phase == Phase::Terms && m_hoveredBodyLineIndex >= 0 && rightClick && m_termsScrolledToBottom)
						{
							m_termsAcknowledgeChecked = !m_termsAcknowledgeChecked;
						}
					}

					for (int32_t i = 0; i < actionCount && leftClick; ++i)
					{
						const int32_t x = contentX + i * (actionW + gap);
						if (!contains(mx, my, x, buttonY, actionW, 42))
							continue;

						switch (m_phase)
						{
						case Phase::Login:
							if (i == 0) applyPrimaryAction();
							else if (i == 1) { m_phase = Phase::Register; m_activeField = 0; m_userErrorText.clear(); }
							else if (i == 2) { m_phase = Phase::ForgotPassword; m_activeField = 0; m_userErrorText.clear(); }
							else if (i == 3) { OpenLanguageOptions(); }
							break;
						case Phase::Register:
						case Phase::VerifyEmail:
						case Phase::ForgotPassword:
							if (i == 0) applyPrimaryAction();
							else if (i == 1) { m_phase = Phase::Login; m_activeField = 0; m_userErrorText.clear(); }
							break;
						case Phase::LanguageSelectionFirstRun:
						case Phase::LanguageOptions:
						case Phase::CharacterCreate:
						case Phase::Submitting:
						case Phase::Error:
							if (i == 0) applyPrimaryAction();
							break;
						case Phase::Terms:
							if (i == 0) applyPrimaryAction();
							break;
						}
						break;
					}
				}
			}
		}

		std::string text;
		if (!usingNativeAuth)
		{
			input.ConsumePendingTextUtf8(text);
			if (!text.empty())
			{
				if (std::string* field = currentField())
				{
				for (unsigned char c : text)
				{
					if (c < 32 && c != '\t')
						continue;
					const bool digitsOnlyField =
						(m_phase == Phase::Register && (m_activeField == 5 || m_activeField == 6 || m_activeField == 7)) ||
						(m_phase == Phase::VerifyEmail);
					if (digitsOnlyField && (c < '0' || c > '9'))
						continue;
					const size_t maxLen =
						(m_phase == Phase::Register && (m_activeField == 5 || m_activeField == 6)) ? 2u :
						(m_phase == Phase::Register && m_activeField == 7) ? 4u :
						(m_phase == Phase::VerifyEmail) ? 6u :
						(m_phase == Phase::CharacterCreate) ? 32u : 256u;
					if (field->size() >= maxLen)
						continue;
					field->push_back(static_cast<char>(c));
				}
				}
			}
		}

	if (!usingNativeAuth && input.WasPressed(engine::platform::Key::Backspace))
		{
			auto popLast = [](std::string& s) {
				while (!s.empty())
				{
					const unsigned char back = static_cast<unsigned char>(s.back());
					s.pop_back();
					if ((back & 0xC0u) != 0x80u)
						break;
				}
			};
			if (std::string* field = currentField())
			{
				popLast(*field);
			}
		}

	if (!usingNativeAuth && input.WasPressed(engine::platform::Key::Tab))
		{
			if (m_phase == Phase::Login)
				m_activeField = (m_activeField + 1u) % 2u;
			else if (m_phase == Phase::Register)
				m_activeField = (m_activeField + 1u) % 8u;
			else if (m_phase == Phase::VerifyEmail || m_phase == Phase::ForgotPassword)
				m_activeField = 0;
			else
				m_activeField = 0;
			LOG_DEBUG(Core, "[AuthUiPresenter] Focus field={}", m_activeField);
		}

		if (m_phase == Phase::Terms)
		{
			const uint32_t kStep = 12u;
			if (input.WasPressed(engine::platform::Key::Down))
				m_termsScrollOffset += kStep;
			if (input.WasPressed(engine::platform::Key::PageDown))
				m_termsScrollOffset += kStep * 2u;
			if (input.WasPressed(engine::platform::Key::Up))
				m_termsScrollOffset = (m_termsScrollOffset > kStep) ? (m_termsScrollOffset - kStep) : 0u;
			if (input.WasPressed(engine::platform::Key::PageUp))
				m_termsScrollOffset = (m_termsScrollOffset > (kStep * 2u)) ? (m_termsScrollOffset - (kStep * 2u)) : 0u;
			const uint32_t visibleChars = 900u;
			if (m_termsTotalLength <= visibleChars || m_termsScrollOffset + visibleChars >= m_termsTotalLength)
				m_termsScrolledToBottom = true;
			if (m_termsScrolledToBottom && input.WasPressed(engine::platform::Key::Space))
				m_termsAcknowledgeChecked = !m_termsAcknowledgeChecked;
		}
		if (!usingNativeAuth && (m_phase == Phase::LanguageSelectionFirstRun || m_phase == Phase::LanguageOptions))
		{
			const auto& locales = m_localization.GetAvailableLocales();
			if (m_phase == Phase::LanguageSelectionFirstRun)
			{
				if (!locales.empty())
				{
					if (input.WasPressed(engine::platform::Key::Up) || input.WasPressed(engine::platform::Key::Left))
					{
						m_languageSelectionIndex = (m_languageSelectionIndex == 0u)
							? static_cast<uint32_t>(locales.size() - 1u)
							: (m_languageSelectionIndex - 1u);
						m_selectedLocale = locales[m_languageSelectionIndex];
						LOG_INFO(Core, "[AuthUiPresenter] Locale selection moved to {}", m_selectedLocale);
					}
					if (input.WasPressed(engine::platform::Key::Down) || input.WasPressed(engine::platform::Key::Right))
					{
						m_languageSelectionIndex = (m_languageSelectionIndex + 1u) % static_cast<uint32_t>(locales.size());
						m_selectedLocale = locales[m_languageSelectionIndex];
						LOG_INFO(Core, "[AuthUiPresenter] Locale selection moved to {}", m_selectedLocale);
					}
				}
			}
			else
			{
				const uint32_t kOptionCount = 13u;
				if (input.WasPressed(engine::platform::Key::Up))
				{
					m_optionsSelectionIndex = (m_optionsSelectionIndex == 0u) ? (kOptionCount - 1u) : (m_optionsSelectionIndex - 1u);
					LOG_INFO(Core, "[AuthUiPresenter] Options selection={}", m_optionsSelectionIndex);
				}
				if (input.WasPressed(engine::platform::Key::Down))
				{
					m_optionsSelectionIndex = (m_optionsSelectionIndex + 1u) % kOptionCount;
					LOG_INFO(Core, "[AuthUiPresenter] Options selection={}", m_optionsSelectionIndex);
				}
				if (!locales.empty() && m_optionsSelectionIndex == 0u
					&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
				{
					if (input.WasPressed(engine::platform::Key::Left))
					{
						m_languageSelectionIndex = (m_languageSelectionIndex == 0u)
							? static_cast<uint32_t>(locales.size() - 1u)
							: (m_languageSelectionIndex - 1u);
					}
					else
					{
						m_languageSelectionIndex = (m_languageSelectionIndex + 1u) % static_cast<uint32_t>(locales.size());
					}
					m_selectedLocale = locales[m_languageSelectionIndex];
					LOG_INFO(Core, "[AuthUiPresenter] Options locale candidate={}", m_selectedLocale);
				}
				if (m_optionsSelectionIndex == 1u
					&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
				{
					m_videoFullscreenPending = !m_videoFullscreenPending;
					LOG_INFO(Core, "[AuthUiPresenter] Options fullscreen candidate={}", m_videoFullscreenPending);
				}
				if (m_optionsSelectionIndex == 2u
					&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
				{
					m_videoVsyncPending = !m_videoVsyncPending;
					LOG_INFO(Core, "[AuthUiPresenter] Options vsync candidate={}", m_videoVsyncPending);
				}
				auto adjustVolume = [&](float& value, std::string_view label)
				{
					if (input.WasPressed(engine::platform::Key::Left))
						value = ClampOptionStep(value - 0.1f);
					else
						value = ClampOptionStep(value + 0.1f);
					LOG_INFO(Core, "[AuthUiPresenter] Options {} candidate={:.1f}", label, value);
				};
				if (m_optionsSelectionIndex == 3u
					&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
				{
					adjustVolume(m_audioMasterVolumePending, "master");
				}
				if (m_optionsSelectionIndex == 4u
					&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
				{
					adjustVolume(m_audioMusicVolumePending, "music");
				}
				if (m_optionsSelectionIndex == 5u
					&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
				{
					adjustVolume(m_audioSfxVolumePending, "sfx");
				}
				if (m_optionsSelectionIndex == 6u
					&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
				{
					adjustVolume(m_audioUiVolumePending, "ui");
				}
				if (m_optionsSelectionIndex == 7u
					&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
				{
					if (input.WasPressed(engine::platform::Key::Left))
						m_mouseSensitivityPending = std::max(0.001f, m_mouseSensitivityPending - 0.001f);
					else
						m_mouseSensitivityPending = std::min(0.010f, m_mouseSensitivityPending + 0.001f);
					LOG_INFO(Core, "[AuthUiPresenter] Options mouse sensitivity candidate={:.4f}", m_mouseSensitivityPending);
				}
				if (m_optionsSelectionIndex == 8u
					&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
				{
					m_invertYPending = !m_invertYPending;
					LOG_INFO(Core, "[AuthUiPresenter] Options invert_y candidate={}", m_invertYPending);
				}
				if (m_optionsSelectionIndex == 9u
					&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
				{
					m_useZqsdPending = !m_useZqsdPending;
					LOG_INFO(Core, "[AuthUiPresenter] Options movement layout candidate={}", m_useZqsdPending ? "zqsd" : "wasd");
				}
				if (m_optionsSelectionIndex == 10u
					&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
				{
					m_gameplayUdpEnabledPending = !m_gameplayUdpEnabledPending;
					LOG_INFO(Core, "[AuthUiPresenter] Options gameplay_udp candidate={}", m_gameplayUdpEnabledPending);
				}
				if (m_optionsSelectionIndex == 11u
					&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
				{
					m_allowInsecureDevPending = !m_allowInsecureDevPending;
					LOG_INFO(Core, "[AuthUiPresenter] Options allow_insecure_dev candidate={}", m_allowInsecureDevPending);
				}
				if (m_optionsSelectionIndex == 12u
					&& (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Right)))
				{
					if (input.WasPressed(engine::platform::Key::Left))
						m_authTimeoutMsPending = (m_authTimeoutMsPending > 1000u) ? (m_authTimeoutMsPending - 1000u) : 1000u;
					else
						m_authTimeoutMsPending = std::min<uint32_t>(15000u, m_authTimeoutMsPending + 1000u);
					LOG_INFO(Core, "[AuthUiPresenter] Options auth timeout candidate={}ms", m_authTimeoutMsPending);
				}
			}
		}

	if (!usingNativeAuth && input.WasPressed(engine::platform::Key::R) && m_phase == Phase::Login)
		{
			m_phase = Phase::Register;
			m_activeField = 0;
			m_userErrorText.clear();
			LOG_INFO(Core, "[AuthUiPresenter] Switched to Register screen");
		}
	if (!usingNativeAuth && input.WasPressed(engine::platform::Key::F) && m_phase == Phase::Login)
		{
			const std::string resetUrl = ResolvePasswordRecoveryUrl(cfg);
			LOG_INFO(Core, "[AuthUiPresenter] Keyboard shortcut opens password recovery portal ({})", resetUrl);
			if (!window.OpenExternalUrl(resetUrl))
			{
				m_phase = Phase::Error;
				m_userErrorText = Tr("auth.error.open_recovery_portal");
			}
		}
	if (!usingNativeAuth && input.WasPressed(engine::platform::Key::O)
		&& m_phase != Phase::LanguageSelectionFirstRun
		&& m_phase != Phase::LanguageOptions
		&& m_phase != Phase::Submitting
		&& m_phase != Phase::Terms)
		{
			OpenLanguageOptions();
		}

	if (!usingNativeAuth && input.WasPressed(engine::platform::Key::L) && (m_phase == Phase::Register || m_phase == Phase::ForgotPassword || m_phase == Phase::VerifyEmail))
		{
			m_phase = Phase::Login;
			m_activeField = 0;
			m_userErrorText.clear();
			LOG_INFO(Core, "[AuthUiPresenter] Switched to Login screen");
		}

	if ((!usingNativeAuth && input.WasPressed(engine::platform::Key::Enter))
		|| (usingNativeAuth && m_phase == Phase::Error))
		{
			applyPrimaryAction();
		}

		UpdateWindowTitle(window);
	}

	AuthUiPresenter::RenderModel AuthUiPresenter::BuildRenderModel() const
	{
		RenderModel model{};
		model.visible = m_initialized && !m_flowComplete && m_authEnabled;
		model.titleLine1 = Tr("auth.title_line1");
		model.titleLine2 = Tr("auth.title_line2");
		model.infoBanner = m_infoBanner;
		model.errorText = (m_phase == Phase::Error) ? m_userErrorText : std::string{};
		model.footerHint = Tr("common.tab_escape_hint");

		auto addField = [this, &model](std::string label, std::string value, bool active, bool secret = false)
		{
			RenderField field{};
			field.label = std::move(label);
			field.value = std::move(value);
			field.active = active;
			field.hovered = static_cast<int32_t>(model.fields.size()) == m_hoveredFieldIndex;
			field.secret = secret;
			model.fields.push_back(std::move(field));
		};
		auto addAction = [this, &model](std::string label, bool primary, bool active = true)
		{
			RenderAction action{};
			action.label = std::move(label);
			action.primary = primary;
			action.active = active;
			action.hovered = static_cast<int32_t>(model.actions.size()) == m_hoveredActionIndex;
			model.actions.push_back(std::move(action));
		};
		auto addBodyLine = [this, &model](std::string text, bool active = false)
		{
			RenderBodyLine line{};
			line.text = std::move(text);
			line.active = active;
			line.hovered = static_cast<int32_t>(model.bodyLines.size()) == m_hoveredBodyLineIndex;
			model.bodyLines.push_back(std::move(line));
		};
		auto maskedPassword = [this]() -> std::string
		{
			std::string out;
			AppendPasswordStars(out, m_password.size());
			return out;
		};

		switch (m_phase)
		{
		case Phase::Login:
			model.sectionTitle = Tr("auth.section.login");
			addField(Tr("auth.label.login"), m_login, m_activeField == 0);
			addField(Tr("auth.label.password"), maskedPassword(), m_activeField == 1, true);
			addBodyLine(Tr("auth.checkbox.remember") + ": " + Tr(m_rememberLogin ? "options.value.on" : "options.value.off"), true);
			addAction(Tr("common.submit"), true);
			addAction(Tr("auth.button.register"), false);
			addAction(Tr("auth.button.forgot_password"), false);
			addAction(Tr("language.options.title"), false);
			break;
		case Phase::Register:
			model.sectionTitle = Tr("auth.panel.register");
			addField(Tr("auth.label.login"), m_login, m_activeField == 0);
			addField(Tr("auth.label.password"), maskedPassword(), m_activeField == 1, true);
			addField(Tr("common.email"), m_email, m_activeField == 2);
			addField(Tr("auth.label.first_name"), m_firstName, m_activeField == 3);
			addField(Tr("auth.label.last_name"), m_lastName, m_activeField == 4);
			addField(Tr("auth.label.birth_day"), m_birthDay, m_activeField == 5);
			addField(Tr("auth.label.birth_month"), m_birthMonth, m_activeField == 6);
			addField(Tr("auth.label.birth_year"), m_birthYear, m_activeField == 7);
			addAction(Tr("common.submit"), true);
			addAction(Tr("auth.hint.return_login"), false);
			break;
		case Phase::VerifyEmail:
			model.sectionTitle = Tr("auth.phase.verify_email");
			addBodyLine(Tr("auth.label.account") + ": " + std::to_string(m_pendingVerifyAccountId));
			addField(Tr("auth.label.verify_code"), m_verifyCode, true);
			addAction(Tr("common.submit"), true);
			addAction(Tr("auth.hint.return_login"), false);
			break;
		case Phase::ForgotPassword:
			model.sectionTitle = Tr("auth.section.forgot_password");
			addField(Tr("common.email"), m_email, true);
			addAction(Tr("common.submit"), true);
			addAction(Tr("auth.hint.return_login"), false);
			break;
		case Phase::Terms:
		{
			model.sectionTitle = Tr("auth.panel.terms");
			addBodyLine(Tr("auth.panel.edition") + " " + std::to_string(m_pendingTermsEditionId));
			addBodyLine(Tr("auth.panel.version") + " " + m_termsVersionLabel);
			addBodyLine(Tr("auth.panel.title") + " " + m_termsTitle);
			addBodyLine(Tr("auth.panel.language") + " " + m_termsLocale);
			const size_t start = static_cast<size_t>(std::min<uint32_t>(m_termsScrollOffset, static_cast<uint32_t>(m_termsContent.size())));
			const size_t count = std::min<size_t>(900u, m_termsContent.size() - start);
			addBodyLine(std::string(m_termsContent.data() + start, count));
			addBodyLine(m_termsScrolledToBottom ? Tr("auth.panel.end_reached") : Tr("auth.panel.end_not_reached"));
			addBodyLine(m_termsAcknowledgeChecked ? Tr("auth.panel.accept_checked") : Tr("auth.panel.accept_unchecked"), true);
			addAction(Tr("auth.hint.terms.accept"), true);
			break;
		}
		case Phase::CharacterCreate:
			model.sectionTitle = Tr("auth.panel.character_create");
			addField(Tr("auth.label.character_name"), m_characterName, true);
			addBodyLine(Tr("auth.hint.character.rules"));
			addAction(Tr("common.submit"), true);
			break;
		case Phase::LanguageSelectionFirstRun:
		case Phase::LanguageOptions:
		{
			model.sectionTitle = (m_phase == Phase::LanguageSelectionFirstRun) ? Tr("language.first_run.title") : Tr("language.options.title");
			addBodyLine(Tr("language.current", { { "language", LocalizedLanguageName(CurrentLocale()) } }));
			const auto& locales = m_localization.GetAvailableLocales();
			for (size_t i = 0; i < locales.size(); ++i)
			{
				const bool selected = ((m_phase == Phase::LanguageSelectionFirstRun && i == m_languageSelectionIndex)
					|| (m_phase == Phase::LanguageOptions && m_optionsSelectionIndex == 0u && i == m_languageSelectionIndex));
				addBodyLine(std::string(selected ? "> " : "  ") + LocalizedLanguageName(locales[i]) + " (" + locales[i] + ")",
					m_optionsSelectionIndex == 0u && static_cast<uint32_t>(i) == m_languageSelectionIndex);
			}
			if (m_phase == Phase::LanguageOptions)
			{
				addBodyLine(Tr("options.video.fullscreen") + ": " + Tr(m_videoFullscreenPending ? "options.value.on" : "options.value.off"), m_optionsSelectionIndex == 1u);
				addBodyLine(Tr("options.video.vsync") + ": " + Tr(m_videoVsyncPending ? "options.value.on" : "options.value.off"), m_optionsSelectionIndex == 2u);
				addBodyLine(Tr("options.audio.master") + ": " + std::to_string(static_cast<int>(m_audioMasterVolumePending * 100.0f + 0.5f)) + "%", m_optionsSelectionIndex == 3u);
				addBodyLine(Tr("options.audio.music") + ": " + std::to_string(static_cast<int>(m_audioMusicVolumePending * 100.0f + 0.5f)) + "%", m_optionsSelectionIndex == 4u);
				addBodyLine(Tr("options.audio.sfx") + ": " + std::to_string(static_cast<int>(m_audioSfxVolumePending * 100.0f + 0.5f)) + "%", m_optionsSelectionIndex == 5u);
				addBodyLine(Tr("options.audio.ui") + ": " + std::to_string(static_cast<int>(m_audioUiVolumePending * 100.0f + 0.5f)) + "%", m_optionsSelectionIndex == 6u);
				addBodyLine(Tr("options.controls.mouse_sensitivity") + ": " + std::to_string(static_cast<int>(m_mouseSensitivityPending * 10000.0f + 0.5f)), m_optionsSelectionIndex == 7u);
				addBodyLine(Tr("options.controls.invert_y") + ": " + Tr(m_invertYPending ? "options.value.on" : "options.value.off"), m_optionsSelectionIndex == 8u);
				addBodyLine(Tr("options.controls.movement_layout") + ": " + Tr(m_useZqsdPending ? "options.controls.layout.zqsd" : "options.controls.layout.wasd"), m_optionsSelectionIndex == 9u);
				addBodyLine(Tr("options.game.gameplay_udp") + ": " + Tr(m_gameplayUdpEnabledPending ? "options.value.on" : "options.value.off"), m_optionsSelectionIndex == 10u);
				addBodyLine(Tr("options.game.allow_insecure_dev") + ": " + Tr(m_allowInsecureDevPending ? "options.value.on" : "options.value.off"), m_optionsSelectionIndex == 11u);
				addBodyLine(Tr("options.game.auth_timeout") + ": " + std::to_string(m_authTimeoutMsPending) + " ms", m_optionsSelectionIndex == 12u);
			}
			addAction((m_phase == Phase::LanguageSelectionFirstRun) ? Tr("language.first_run.confirm") : Tr("language.options.apply_hint"), true);
			break;
		}
		case Phase::Submitting:
			model.sectionTitle = Tr("auth.panel.submitting");
			addBodyLine(Tr("auth.panel.submitting"), true);
			break;
		case Phase::Error:
			model.sectionTitle = Tr("auth.panel.error");
			addBodyLine(m_userErrorText, true);
			addAction(Tr("common.continue"), true);
			break;
		}

		if (!model.bodyLines.empty())
		{
			const int32_t total = static_cast<int32_t>(model.bodyLines.size());
			const int32_t maxVisible = 6;
			int32_t focusIndex = 0;
			for (int32_t i = 0; i < total; ++i)
			{
				if (model.bodyLines[static_cast<size_t>(i)].hovered || model.bodyLines[static_cast<size_t>(i)].active)
				{
					focusIndex = i;
					break;
				}
			}
			int32_t start = std::max(0, focusIndex - (maxVisible / 2));
			if (start + maxVisible > total)
			{
				start = std::max(0, total - maxVisible);
			}
			model.visibleBodyLineStart = start;
			model.visibleBodyLineCount = std::min(maxVisible, total - start);
		}

		return model;
	}

	std::string AuthUiPresenter::BuildPanelText() const
	{
		const RenderModel model = BuildRenderModel();
		if (!model.visible)
		{
			return {};
		}

		std::string s;
		s += model.titleLine1;
		s += "\n";
		s += model.titleLine2;
		s += "\n\n";
		if (!model.infoBanner.empty())
		{
			s += Tr("common.information");
			s += "\n";
			s += model.infoBanner;
			s += "\n\n";
		}
		if (!model.sectionTitle.empty())
		{
			s += model.sectionTitle;
			s += "\n\n";
		}
		for (const RenderField& field : model.fields)
		{
			s += field.label;
			s += "\n";
			s += field.value;
			s += field.active ? "|\n" : "\n";
			s += "\n";
		}
		for (int32_t i = 0; i < model.visibleBodyLineCount; ++i)
		{
			const RenderBodyLine& line = model.bodyLines[static_cast<size_t>(model.visibleBodyLineStart + i)];
			if (line.text.empty())
				continue;
			s += line.text;
			s += "\n";
		}
		if (!model.bodyLines.empty())
		{
			s += "\n";
		}
		if (!model.errorText.empty())
		{
			s += "\n\n";
			s += model.errorText;
			s += "\n";
		}
		for (const RenderAction& action : model.actions)
		{
			s += action.primary ? "[*] " : "[ ] ";
			s += action.label;
			s += "\n";
		}
		s += "\n";
		s += model.footerHint;
		s += "\n";
		return s;
	}

	AuthUiPresenter::VisualState AuthUiPresenter::GetVisualState() const
	{
		VisualState state{};
		state.active = m_initialized && !m_flowComplete && m_authEnabled;
		state.login = m_phase == Phase::Login;
		state.registerMode = m_phase == Phase::Register;
		state.verifyEmail = m_phase == Phase::VerifyEmail;
		state.forgotPassword = m_phase == Phase::ForgotPassword;
		state.terms = m_phase == Phase::Terms;
		state.characterCreate = m_phase == Phase::CharacterCreate;
		state.languageSelection = m_phase == Phase::LanguageSelectionFirstRun;
		state.languageOptions = m_phase == Phase::LanguageOptions;
		state.submitting = m_phase == Phase::Submitting;
		state.error = m_phase == Phase::Error;
		return state;
	}

	AuthUiPresenter::VideoSettingsCommand AuthUiPresenter::ConsumePendingVideoSettings()
	{
		const VideoSettingsCommand cmd = m_pendingVideoSettings;
		m_pendingVideoSettings = {};
		return cmd;
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

	bool AuthUiPresenter::OnEscape()
	{
		if (m_phase == Phase::Submitting)
			return true;
		if (m_phase == Phase::LanguageSelectionFirstRun)
		{
			return true;
		}
		if (m_phase == Phase::LanguageOptions)
		{
			m_phase = m_phaseBeforeOptions;
			LOG_INFO(Core, "[AuthUiPresenter] Escape: Language options closed");
			return true;
		}
		if (m_phase == Phase::Register || m_phase == Phase::ForgotPassword || m_phase == Phase::VerifyEmail)
		{
			m_phase = Phase::Login;
			m_userErrorText.clear();
			LOG_INFO(Core, "[AuthUiPresenter] Escape: Auth sub-screen -> Login");
			return true;
		}
		if (m_phase == Phase::Terms)
		{
			return true;
		}
		if (m_phase == Phase::CharacterCreate)
		{
			m_phase = Phase::Login;
			m_userErrorText.clear();
			m_infoBanner = Tr("auth.info.character_cancelled");
			ResetMasterSession();
			return true;
		}
		if (m_phase == Phase::Error)
		{
			m_phase = Phase::Login;
			m_userErrorText.clear();
			LOG_INFO(Core, "[AuthUiPresenter] Escape: Error -> Login");
			return true;
		}
		return false;
	}

}
