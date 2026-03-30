#include "engine/client/AuthUi.h"

#include "engine/core/Log.h"
#include "engine/platform/Window.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#	include "engine/auth/Argon2Hash.h"
#	include "engine/network/AuthRegisterPayloads.h"
#	include "engine/network/ErrorPacket.h"
#	include "engine/network/MasterShardClientFlow.h"
#	include "engine/network/NetClient.h"
#	include "engine/network/ProtocolV1Constants.h"
#	include "engine/network/RequestResponseDispatcher.h"
#endif

namespace engine::client
{
	namespace
	{
		constexpr const char* kLoginAssetsRel = "engine/assets/ui/login";
		constexpr const char* kRegisterAssetsRel = "engine/assets/ui/register";

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
		m_activeField = 0;
		m_userErrorText.clear();
		m_infoBanner.clear();
		m_argonSalt.clear();
		m_asyncResult = {};
		JoinWorker();

		m_initialized = true;
		LOG_INFO(Core, "[AuthUiPresenter] Init OK (master host from client.master_host / client.master_port)");
		return true;
	}

	void AuthUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		JoinWorker();
		m_password.clear();
		m_initialized = false;
		LOG_INFO(Core, "[AuthUiPresenter] Destroyed");
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
		AsyncResult copy{};
		{
			std::lock_guard<std::mutex> lock(m_asyncMutex);
			if (!m_asyncResult.ready)
				return;
			copy = m_asyncResult;
			m_asyncResult = {};
		}
		JoinWorker();

		const bool wasRegister = m_pendingAsyncIsRegister;
		m_pendingAsyncIsRegister = false;

		if (wasRegister)
		{
			if (copy.success)
			{
				m_phase = Phase::Login;
				m_userErrorText.clear();
				m_infoBanner = copy.message.empty() ? "Registration OK — press Enter to authenticate." : copy.message;
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

		m_pendingAsyncIsRegister = true;
		{
			std::lock_guard<std::mutex> lock(m_asyncMutex);
			m_asyncResult = {};
		}

		m_worker = std::thread([this, host, port, timeoutMs, login, email, hash, allowInsecure, locale]() {
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
			std::vector<uint8_t> payload = engine::network::BuildRegisterRequestPayload(login, email, hash, {}, locale);
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
			local.message = ok ? std::string("Registration OK — Enter to log in (same password).") : (errMsg.empty() ? "REGISTER failed." : errMsg);
			{
				std::lock_guard<std::mutex> lock(m_asyncMutex);
				m_asyncResult = local;
			}
			LOG_INFO(Net, "[AuthUiPresenter] Register worker finished (success={})", (int)ok);
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

		m_pendingAsyncIsRegister = false;
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
			UpdateWindowTitle(window);
			return;
		}

		std::string text;
		input.ConsumePendingTextUtf8(text);
		if (!text.empty())
		{
			for (unsigned char c : text)
			{
				if (c < 32 && c != '\t')
					continue;
				if (m_phase == Phase::Login)
				{
					if (m_activeField == 0)
						m_login.push_back(static_cast<char>(c));
					else
						m_password.push_back(static_cast<char>(c));
				}
				else
				{
					if (m_activeField == 0)
						m_login.push_back(static_cast<char>(c));
					else if (m_activeField == 1)
						m_email.push_back(static_cast<char>(c));
					else
						m_password.push_back(static_cast<char>(c));
				}
			}
		}

		if (input.WasPressed(engine::platform::Key::Backspace))
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
			if (m_phase == Phase::Login)
			{
				if (m_activeField == 0)
					popLast(m_login);
				else
					popLast(m_password);
			}
			else
			{
				if (m_activeField == 0)
					popLast(m_login);
				else if (m_activeField == 1)
					popLast(m_email);
				else
					popLast(m_password);
			}
		}

		if (input.WasPressed(engine::platform::Key::Tab))
		{
			if (m_phase == Phase::Login)
				m_activeField = (m_activeField + 1u) % 2u;
			else
				m_activeField = (m_activeField + 1u) % 3u;
			LOG_DEBUG(Core, "[AuthUiPresenter] Focus field={}", m_activeField);
		}

		if (input.WasPressed(engine::platform::Key::R) && m_phase == Phase::Login)
		{
			m_phase = Phase::Register;
			m_activeField = 0;
			m_userErrorText.clear();
			LOG_INFO(Core, "[AuthUiPresenter] Switched to Register screen");
		}

		if (input.WasPressed(engine::platform::Key::L) && m_phase == Phase::Register)
		{
			m_phase = Phase::Login;
			m_activeField = 0;
			m_userErrorText.clear();
			LOG_INFO(Core, "[AuthUiPresenter] Switched to Login screen");
		}

		if (input.WasPressed(engine::platform::Key::Enter))
		{
			if (m_phase == Phase::Error)
			{
				m_phase = Phase::Login;
				m_userErrorText.clear();
				LOG_INFO(Core, "[AuthUiPresenter] Error acknowledged, back to Login");
			}
			else if (m_phase == Phase::Login)
			{
				if (m_login.empty() || m_password.empty())
				{
					m_phase = Phase::Error;
					m_userErrorText = "Enter login and password.";
					LOG_WARN(Core, "[AuthUiPresenter] Submit rejected: empty fields");
				}
				else
				{
					m_phase = Phase::Submitting;
					StartMasterFlowWorker(cfg);
				}
			}
			else if (m_phase == Phase::Register)
			{
				if (m_login.empty() || m_password.empty() || m_email.empty())
				{
					m_phase = Phase::Error;
					m_userErrorText = "Enter login, email, and password.";
					LOG_WARN(Core, "[AuthUiPresenter] Register submit rejected: empty fields");
				}
				else
				{
					m_phase = Phase::Submitting;
					StartRegisterWorker(cfg);
				}
			}
		}

		UpdateWindowTitle(window);
	}

	std::string AuthUiPresenter::BuildPanelText() const
	{
		std::string s;
		s += "=== AUTH (STAB.13) ===\n";
		s += "Background assets (repo-relative): ";
		s += kLoginAssetsRel;
		s += " | ";
		s += kRegisterAssetsRel;
		s += "\n";
		if (!m_infoBanner.empty())
		{
			s += "[INFO] ";
			s += m_infoBanner;
			s += "\n";
		}
		switch (m_phase)
		{
		case Phase::Login:
			s += "[LOGIN]  R=Register screen\n";
			s += "login> ";
			s += m_login;
			s += (m_activeField == 0 ? "|\n" : "\n");
			s += "pass>  ";
			AppendPasswordStars(s, m_password.size());
			s += (m_activeField == 1 ? "|\n" : "\n");
			break;
		case Phase::Register:
			s += "[REGISTER]  L=Back to login\n";
			s += "login> ";
			s += m_login;
			s += (m_activeField == 0 ? "|\n" : "\n");
			s += "email> ";
			s += m_email;
			s += (m_activeField == 1 ? "|\n" : "\n");
			s += "pass>  ";
			AppendPasswordStars(s, m_password.size());
			s += (m_activeField == 2 ? "|\n" : "\n");
			break;
		case Phase::Submitting:
			s += "Submitting / connecting to master…\n";
			break;
		case Phase::Error:
			s += "[ERROR]\n";
			s += m_userErrorText;
			s += "\n(Enter to dismiss)\n";
			break;
		}
		s += "Tab=next field  Enter=submit\n";
		return s;
	}

	bool AuthUiPresenter::OnEscape()
	{
		if (m_phase == Phase::Submitting)
			return true;
		if (m_phase == Phase::Register)
		{
			m_phase = Phase::Login;
			m_userErrorText.clear();
			LOG_INFO(Core, "[AuthUiPresenter] Escape: Register -> Login");
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
