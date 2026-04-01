#include "engine/client/AuthUi.h"

#include "engine/core/Log.h"
#include "engine/network/NetClient.h"
#include "engine/platform/FileSystem.h"
#include "engine/platform/Window.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
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
		constexpr std::string_view kLoginBackgroundPath = "engine/assets/ui/login/background.png";
		constexpr std::string_view kLoginLogoPath = "engine/assets/ui/login/logo_login.png";
		constexpr std::string_view kRegisterBackgroundPath = "engine/assets/ui/register/background.png";
		constexpr std::string_view kRegisterInfoPath = "engine/assets/ui/register/info.png";

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
		m_initialized = false;
		LOG_INFO(Core, "[AuthUiPresenter] Destroyed");
	}

	bool AuthUiPresenter::BlocksWorldInput() const
	{
		return false;
	}

	void AuthUiPresenter::Update(engine::platform::Input&, float, engine::platform::Window&, const engine::core::Config&)
	{
	}

	std::string AuthUiPresenter::BuildPanelText() const
	{
		return {};
	}

	AuthUiPresenter::VisualState AuthUiPresenter::GetVisualState() const
	{
		return {};
	}

	bool AuthUiPresenter::OnEscape()
	{
		return false;
	}

	bool AuthUiPresenter::SetViewportSize(uint32_t width, uint32_t height)
	{
		m_viewportW = width;
		m_viewportH = height;
		return width > 0 && height > 0;
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
		m_argonSalt.clear();
		m_asyncResult = {};
		m_pendingAsyncKind = AsyncKind::None;
		m_masterSessionId = 0;
		m_masterClient.reset();
		JoinWorker();
		LoadRememberPreference();

		m_initialized = true;
		LOG_INFO(Core, "[AuthUiPresenter] Init OK (master host from client.master_host / client.master_port)");
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
			LOG_INFO(Core, "[AuthUiPresenter] Remember preference loaded: {}", m_rememberLogin);
			return;
		}
		m_rememberLogin = false;
		m_savedRememberLogin = false;
		LOG_INFO(Core, "[AuthUiPresenter] Remember preference defaulted to unchecked");
	}

	void AuthUiPresenter::SaveRememberPreference()
	{
		const std::string json = std::string("{\n  \"client\": {\n    \"auth_ui\": {\n      \"remember_login\": ")
			+ (m_rememberLogin ? "true" : "false")
			+ "\n    }\n  }\n}\n";
		if (!engine::platform::FileSystem::WriteAllText(std::string(kUserSettingsPath), json))
		{
			LOG_WARN(Core, "[AuthUiPresenter] Failed to persist remember preference");
			return;
		}
		m_savedRememberLogin = m_rememberLogin;
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
		std::string t = "LCDLLN | ";
		switch (m_phase)
		{
		case Phase::Login:
			t += "Login";
			break;
		case Phase::Register:
			t += "Register";
			break;
		case Phase::VerifyEmail:
			t += "Verify email";
			break;
		case Phase::ForgotPassword:
			t += "Forgot password";
			break;
		case Phase::Terms:
			t += "Terms";
			break;
		case Phase::CharacterCreate:
			t += "Character creation";
			break;
		case Phase::Submitting:
			t += "Connecting...";
			break;
		case Phase::Error:
			t += "Error";
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
				m_infoBanner = copy.message.empty() ? "Registration OK. Enter the verification code sent by email." : copy.message;
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
					m_infoBanner = copy.message.empty() ? "Terms acceptance required." : copy.message;
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
				m_infoBanner = copy.message.empty() ? "Email verified. You can now log in." : copy.message;
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
				m_infoBanner = copy.message.empty() ? "If the email exists, a reset message has been sent." : copy.message;
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
					m_infoBanner = "All terms accepted. Choose your character name.";
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
					m_infoBanner = "All terms accepted. Choose your character name.";
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
				m_infoBanner = copy.message.empty() ? "Character created. Continuing login..." : copy.message;
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
			m_userErrorText = "Could not hash password (Argon2).";
			LOG_ERROR(Core, "[AuthUiPresenter] Register aborted: empty client_hash");
			return;
		}

		const std::string host = cfg.GetString("client.master_host", "localhost");
		const uint16_t port = static_cast<uint16_t>(cfg.GetInt("client.master_port", 3840));
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const bool allowInsecure = cfg.GetBool("client.allow_insecure_dev", true);
		const std::string locale = cfg.GetString("client.locale", "");
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
			m_userErrorText = "Could not hash password (Argon2).";
			return;
		}

		ResetMasterSession();
		m_masterClient = std::make_unique<engine::network::NetClient>();
		const std::string host = cfg.GetString("client.master_host", "localhost");
		const uint16_t port = static_cast<uint16_t>(cfg.GetInt("client.master_port", 3840));
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const bool allowInsecure = cfg.GetBool("client.allow_insecure_dev", true);
		const std::string login = m_login;
		const std::string locale = cfg.GetString("client.locale", "");

		m_pendingAsyncKind = AsyncKind::AuthOnly;
		{
			std::lock_guard<std::mutex> lock(m_asyncMutex);
			m_asyncResult = {};
		}

		m_worker = std::thread([this, host, port, timeoutMs, allowInsecure, login, hash, locale]() {
			AsyncResult local{};
			if (!m_masterClient)
			{
				local.ready = true;
				local.message = "Internal error: master client missing.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			m_masterClient->SetAllowInsecureDev(allowInsecure);
			m_masterClient->Connect(host, port);
			if (!WaitConnected(m_masterClient.get(), timeoutMs + 2000u))
			{
				local.ready = true;
				local.message = "Master connect failed or timeout.";
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
				return;
			}

			engine::network::RequestResponseDispatcher disp(m_masterClient.get());
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
				m_masterClient->Disconnect("auth_failed");
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
				m_masterClient->Disconnect("terms_status_send_failed");
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
				m_masterClient->Disconnect("terms_status_timeout");
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
					m_masterClient->Disconnect("terms_content_send_failed");
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
					m_masterClient->Disconnect("terms_content_timeout");
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
			m_userErrorText = "Could not hash password (Argon2).";
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
			m_userErrorText = "Terms session is not active.";
			return;
		}
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const std::string locale = cfg.GetString("client.locale", "");

		m_pendingAsyncKind = AsyncKind::TermsStatus;
		{
			std::lock_guard<std::mutex> lock(m_asyncMutex);
			m_asyncResult = {};
		}

		m_worker = std::thread([this, timeoutMs, locale]() {
			AsyncResult local{};
			engine::network::RequestResponseDispatcher disp(m_masterClient.get());
			disp.SetSessionId(m_masterSessionId);
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
			m_userErrorText = "Terms session is not active.";
			return;
		}
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const std::string locale = cfg.GetString("client.locale", "");
		const uint64_t editionId = m_pendingTermsEditionId;

		m_pendingAsyncKind = AsyncKind::TermsAccept;
		{
			std::lock_guard<std::mutex> lock(m_asyncMutex);
			m_asyncResult = {};
		}

		m_worker = std::thread([this, timeoutMs, locale, editionId]() {
			AsyncResult local{};
			engine::network::RequestResponseDispatcher disp(m_masterClient.get());
			disp.SetSessionId(m_masterSessionId);
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
			m_userErrorText = "Character creation session is not active.";
			return;
		}
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const std::string characterName = m_characterName;

		m_pendingAsyncKind = AsyncKind::CharacterCreate;
		{
			std::lock_guard<std::mutex> lock(m_asyncMutex);
			m_asyncResult = {};
		}

		m_worker = std::thread([this, timeoutMs, characterName]() {
			AsyncResult local{};
			engine::network::RequestResponseDispatcher disp(m_masterClient.get());
			disp.SetSessionId(m_masterSessionId);
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
			SubmitCurrentPhase(cfg);
			break;
		case engine::platform::Window::AuthScreenCommand::Quit:
			window.RequestClose();
			break;
		case engine::platform::Window::AuthScreenCommand::OpenRegister:
			m_phase = Phase::Register;
			m_activeField = 0;
			m_userErrorText.clear();
			break;
		case engine::platform::Window::AuthScreenCommand::OpenForgotPassword:
			m_phase = Phase::ForgotPassword;
			m_activeField = 0;
			m_userErrorText.clear();
			break;
		case engine::platform::Window::AuthScreenCommand::BackToLogin:
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
		state.titleLine1 = "Les Chroniques De La";
		state.titleLine2 = "Lune Noire";
		state.sectionTitle = m_phase == Phase::Login ? "Connexion"
			: (m_phase == Phase::ForgotPassword ? "Recuperation du mot de passe" : "Inscription");
		state.primaryLabel = m_phase == Phase::Register ? "" : "Login / Email";
		state.primaryValue = (m_phase == Phase::Login) ? m_login : m_email;
		state.passwordValue = m_password;
		state.submitLabel = m_phase == Phase::Register ? "" : "Valider";
		state.backgroundImagePath = m_phase == Phase::Register ? std::string(kRegisterBackgroundPath) : std::string(kLoginBackgroundPath);
		state.logoImagePath = m_phase == Phase::Login ? std::string(kLoginLogoPath) : "";
		state.infoImagePath = m_phase == Phase::Register ? std::string(kRegisterInfoPath) : "";
		window.SetAuthScreenState(state);
		return true;
	}
#endif
	window.SetAuthScreenState({});
	(void)cfg;
	return false;
}

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
			m_userErrorText = "Enter login and password.";
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
			m_userErrorText = "Enter login, password, email, first name, last name, and birth date.";
			LOG_WARN(Core, "[AuthUiPresenter] Register submit rejected: empty fields");
			return;
		}
		if (!IsValidBirthDateFields(m_birthDay, m_birthMonth, m_birthYear))
		{
			m_phase = Phase::Error;
			m_userErrorText = "Birth date must use valid numeric day/month/year values.";
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
			m_userErrorText = "Enter the verification code from the email.";
			return;
		}
		if (!IsValidVerificationCode(m_verifyCode))
		{
			m_phase = Phase::Error;
			m_userErrorText = "Verification code must contain exactly 6 digits.";
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
			m_userErrorText = "Enter the email address for password recovery.";
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
			m_userErrorText = "Scroll to the end of the terms, then check the acknowledgement box.";
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
			m_userErrorText = "Enter a character name.";
			return;
		}
		if (!IsValidCharacterNameLocal(m_characterName))
		{
			m_phase = Phase::Error;
			m_userErrorText = "Character name must be 3-32 characters and use only letters, digits, or underscore.";
			return;
		}
		m_phase = Phase::Submitting;
		StartCharacterCreateWorker(cfg);
	}
}

	void AuthUiPresenter::Update(engine::platform::Input& input, float deltaSeconds, engine::platform::Window& window,
		const engine::core::Config& cfg)
	{
		(void)deltaSeconds;
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

	const bool usingNativeAuth = HandleNativeAuthScreen(window, cfg);

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
			default:
				return nullptr;
			}
		};

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

	if (!usingNativeAuth && input.WasPressed(engine::platform::Key::R) && m_phase == Phase::Login)
		{
			m_phase = Phase::Register;
			m_activeField = 0;
			m_userErrorText.clear();
			LOG_INFO(Core, "[AuthUiPresenter] Switched to Register screen");
		}
	if (!usingNativeAuth && input.WasPressed(engine::platform::Key::F) && m_phase == Phase::Login)
		{
			m_phase = Phase::ForgotPassword;
			m_activeField = 0;
			m_userErrorText.clear();
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
		SubmitCurrentPhase(cfg);
		}

		UpdateWindowTitle(window);
	}

	std::string AuthUiPresenter::BuildPanelText() const
	{
#if defined(_WIN32)
	if (m_phase == Phase::Login || m_phase == Phase::ForgotPassword || m_phase == Phase::Register)
	{
		return {};
	}
#endif
		std::string s;
	s += "Les Chroniques De La\n";
	s += "Lune Noire\n\n";
		if (!m_infoBanner.empty())
		{
			s += "Information\n";
			s += m_infoBanner;
			s += "\n\n";
		}
		switch (m_phase)
		{
		case Phase::Login:
			s += "Connexion\n\n";
			s += "Identifiant\n";
			s += m_login;
			s += (m_activeField == 0 ? "|\n" : "\n");
			s += "\nMot de passe\n";
			AppendPasswordStars(s, m_password.size());
			s += (m_activeField == 1 ? "|\n" : "\n");
			s += "\nEntrer pour se connecter\nR creer un compte\nF mot de passe oublie\n";
			break;
		case Phase::Register:
			s += "Creation de compte\n\n";
			s += "Identifiant\n";
			s += m_login;
			s += (m_activeField == 0 ? "|\n" : "\n");
			s += "\nMot de passe\n";
			AppendPasswordStars(s, m_password.size());
			s += (m_activeField == 1 ? "|\n" : "\n");
			s += "\nEmail\n";
			s += m_email;
			s += (m_activeField == 2 ? "|\n" : "\n");
			s += "\nPrenom\n";
			s += m_firstName;
			s += (m_activeField == 3 ? "|\n" : "\n");
			s += "\nNom\n";
			s += m_lastName;
			s += (m_activeField == 4 ? "|\n" : "\n");
			s += "\nJour de naissance\n";
			s += m_birthDay;
			s += (m_activeField == 5 ? "|\n" : "\n");
			s += "\nMois de naissance\n";
			s += m_birthMonth;
			s += (m_activeField == 6 ? "|\n" : "\n");
			s += "\nAnnee de naissance\n";
			s += m_birthYear;
			s += (m_activeField == 7 ? "|\n" : "\n");
			s += "\nEntrer pour valider\nL retour connexion\n";
			break;
		case Phase::VerifyEmail:
			s += "Verification email\n\n";
			s += "Compte\n";
			s += std::to_string(m_pendingVerifyAccountId);
			s += "\n\nCode a 6 chiffres\n";
			s += m_verifyCode;
			s += "|\n";
			s += "\nEntrer pour confirmer\nL retour\n";
			break;
		case Phase::ForgotPassword:
			s += "Recuperation du mot de passe\n\n";
			s += "Email\n";
			s += m_email;
			s += "|\n";
			s += "\nEntrer pour envoyer\nL retour\n";
			break;
		case Phase::Terms:
		{
			s += "Conditions d'utilisation\n\n";
			s += "Edition ";
			s += std::to_string(m_pendingTermsEditionId);
			s += "  Version ";
			s += m_termsVersionLabel;
			s += "\nTitre ";
			s += m_termsTitle;
			s += "\nLangue ";
			s += m_termsLocale;
			s += "\n\n";
			const size_t start = static_cast<size_t>(std::min<uint32_t>(m_termsScrollOffset, static_cast<uint32_t>(m_termsContent.size())));
			const size_t count = std::min<size_t>(900u, m_termsContent.size() - start);
			s.append(m_termsContent.data() + start, count);
			s += "\n\n";
			s += m_termsScrolledToBottom ? "[x] Fin atteinte. " : "[ ] Descendez jusqu'a la fin. ";
			s += m_termsAcknowledgeChecked ? "[x] J'accepte les conditions.\n" : "[ ] J'accepte les conditions.\n";
			s += "Fleches/PageUp/PageDown pour defiler\nEspace cocher, Entrer valider\n";
			break;
		}
		case Phase::CharacterCreate:
			s += "Creation du personnage\n\n";
			s += "Nom\n";
			s += m_characterName;
			s += "|\n";
			s += "\n3 a 32 caracteres. Lettres, chiffres et underscore.\n";
			break;
		case Phase::Submitting:
			s += "Connexion au serveur en cours...\n";
			break;
		case Phase::Error:
			s += "Erreur\n\n";
			s += m_userErrorText;
			s += "\n\nEntrer pour fermer\n";
			break;
		}
		s += "\nTab changer de champ  Echap retour\n";
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
		state.submitting = m_phase == Phase::Submitting;
		state.error = m_phase == Phase::Error;
		return state;
	}

	bool AuthUiPresenter::OnEscape()
	{
		if (m_phase == Phase::Submitting)
			return true;
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
			m_infoBanner = "Character creation cancelled. Back to login.";
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

#endif
}
