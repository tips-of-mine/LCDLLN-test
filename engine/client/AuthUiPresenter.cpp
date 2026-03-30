#include "engine/client/AuthUiPresenter.h"

#include "engine/core/Log.h"

// Network headers are only available on Windows (NetClient is Win32-only).
#if defined(_WIN32)
#  include "engine/network/AuthRegisterPayloads.h"
#  include "engine/network/MasterShardClientFlow.h"
#  include "engine/network/NetClient.h"
#  include "engine/network/NetErrorCode.h"
#  include "engine/network/ProtocolV1Constants.h"
#  include "engine/network/RequestResponseDispatcher.h"
#endif

#include <chrono>
#include <sstream>
#include <thread>

namespace engine::client
{
	// -------------------------------------------------------------------------
	// Lifecycle
	// -------------------------------------------------------------------------

	AuthUiPresenter::~AuthUiPresenter()
	{
		Shutdown();
	}

	bool AuthUiPresenter::Init(std::string masterHost, uint16_t masterPort)
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[AuthUiPresenter] Init ignored: already initialized");
			return true;
		}

		if (masterHost.empty())
		{
			LOG_ERROR(Core, "[AuthUiPresenter] Init FAILED: masterHost is empty");
			return false;
		}

		m_masterHost  = std::move(masterHost);
		m_masterPort  = masterPort;
		m_state.store(AuthUiState::Login);
		m_preSubmitState  = AuthUiState::Login;
		m_resultReady.store(false);
		m_resultSuccess.store(false);
		m_resultAccountId.store(0);
		m_resultShardId.store(0);
		m_shutdown.store(false);

		{
			std::lock_guard<std::mutex> lk(m_fieldMutex);
			m_loginField.clear();
			m_passwordField.clear();
			m_emailField.clear();
		}

		m_initialized = true;
		LOG_INFO(Core, "[AuthUiPresenter] Init OK (master={}:{})", m_masterHost, m_masterPort);
		return true;
	}

	void AuthUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;

		m_shutdown.store(true);

		if (m_networkThread.joinable())
		{
			m_networkThread.join();
		}

		m_initialized = false;
		LOG_INFO(Core, "[AuthUiPresenter] Destroyed");
	}

	// -------------------------------------------------------------------------
	// Input field setters
	// -------------------------------------------------------------------------

	void AuthUiPresenter::SetLoginField(std::string value)
	{
		std::lock_guard<std::mutex> lk(m_fieldMutex);
		m_loginField = std::move(value);
	}

	void AuthUiPresenter::SetPasswordField(std::string value)
	{
		std::lock_guard<std::mutex> lk(m_fieldMutex);
		m_passwordField = std::move(value);
	}

	void AuthUiPresenter::SetEmailField(std::string value)
	{
		std::lock_guard<std::mutex> lk(m_fieldMutex);
		m_emailField = std::move(value);
	}

	// -------------------------------------------------------------------------
	// Screen transitions
	// -------------------------------------------------------------------------

	void AuthUiPresenter::SwitchToRegister()
	{
		AuthUiState expected = AuthUiState::Login;
		if (!m_state.compare_exchange_strong(expected, AuthUiState::Register))
		{
			LOG_WARN(Core, "[AuthUiPresenter] SwitchToRegister ignored: state is not Login");
			return;
		}
		LOG_INFO(Core, "[AuthUiPresenter] Switched to Register screen");
	}

	void AuthUiPresenter::SwitchToLogin()
	{
		const AuthUiState cur = m_state.load();
		if (cur != AuthUiState::Register && cur != AuthUiState::Error)
		{
			LOG_WARN(Core, "[AuthUiPresenter] SwitchToLogin ignored: state is not Register or Error");
			return;
		}
		m_state.store(AuthUiState::Login);
		LOG_INFO(Core, "[AuthUiPresenter] Switched to Login screen");
	}

	// -------------------------------------------------------------------------
	// Actions
	// -------------------------------------------------------------------------

	void AuthUiPresenter::SubmitLogin()
	{
		AuthUiState expected = AuthUiState::Login;
		if (!m_state.compare_exchange_strong(expected, AuthUiState::Submitting))
		{
			LOG_WARN(Core, "[AuthUiPresenter] SubmitLogin ignored: state is not Login");
			return;
		}

		// Guard: join any previous thread before launching a new one.
		if (m_networkThread.joinable())
			m_networkThread.join();

		m_preSubmitState = AuthUiState::Login;
		m_resultReady.store(false);

		LOG_INFO(Core, "[AuthUiPresenter] SubmitLogin → Submitting");
		m_networkThread = std::thread(&AuthUiPresenter::RunAuthThread, this);
	}

	void AuthUiPresenter::SubmitRegister()
	{
		AuthUiState expected = AuthUiState::Register;
		if (!m_state.compare_exchange_strong(expected, AuthUiState::Submitting))
		{
			LOG_WARN(Core, "[AuthUiPresenter] SubmitRegister ignored: state is not Register");
			return;
		}

		if (m_networkThread.joinable())
			m_networkThread.join();

		m_preSubmitState = AuthUiState::Register;
		m_resultReady.store(false);

		LOG_INFO(Core, "[AuthUiPresenter] SubmitRegister → Submitting");
		m_networkThread = std::thread(&AuthUiPresenter::RunRegisterThread, this);
	}

	void AuthUiPresenter::DismissError()
	{
		AuthUiState expected = AuthUiState::Error;
		if (!m_state.compare_exchange_strong(expected, m_preSubmitState))
		{
			LOG_WARN(Core, "[AuthUiPresenter] DismissError ignored: state is not Error");
			return;
		}
		LOG_INFO(Core, "[AuthUiPresenter] Error dismissed, returning to previous screen");
	}

	// -------------------------------------------------------------------------
	// Per-frame polling
	// -------------------------------------------------------------------------

	bool AuthUiPresenter::Poll()
	{
		if (m_state.load() != AuthUiState::Submitting)
			return false;

		if (!m_resultReady.load())
			return false;

		// Consume the result.
		m_resultReady.store(false);

		if (m_resultSuccess.load())
		{
			m_state.store(AuthUiState::Success);
			LOG_INFO(Core, "[AuthUiPresenter] Poll → Success (account_id={}, shard_id={})",
				m_resultAccountId.load(), m_resultShardId.load());
			return true;
		}

		// Failure: move to Error state with the message.
		{
			std::lock_guard<std::mutex> lk(m_resultMutex);
			LOG_WARN(Core, "[AuthUiPresenter] Poll → Error ({})", m_resultError);
		}
		m_state.store(AuthUiState::Error);
		return true;
	}

	// -------------------------------------------------------------------------
	// Accessors
	// -------------------------------------------------------------------------

	std::string AuthUiPresenter::GetErrorMessage() const
	{
		std::lock_guard<std::mutex> lk(m_resultMutex);
		return m_resultError;
	}

	std::string AuthUiPresenter::GetLoginField() const
	{
		std::lock_guard<std::mutex> lk(m_fieldMutex);
		return m_loginField;
	}

	// -------------------------------------------------------------------------
	// Text panel
	// -------------------------------------------------------------------------

	std::string AuthUiPresenter::BuildPanelText() const
	{
		const AuthUiState state = m_state.load();

		std::string login;
		{
			std::lock_guard<std::mutex> lk(m_fieldMutex);
			login = m_loginField;
		}

		std::ostringstream ss;

		switch (state)
		{
		case AuthUiState::Login:
			ss << "=== LOGIN ===\n";
			ss << "Identifiant : " << (login.empty() ? "<vide>" : login) << "\n";
			ss << "Mot de passe: ****\n";
			ss << "[Connexion]  [Creer un compte]\n";
			break;

		case AuthUiState::Register:
			ss << "=== INSCRIPTION ===\n";
			ss << "Identifiant : " << (login.empty() ? "<vide>" : login) << "\n";
			ss << "Email       : ****\n";
			ss << "Mot de passe: ****\n";
			ss << "[Inscrire]  [Retour connexion]\n";
			break;

		case AuthUiState::Submitting:
			ss << "=== CONNEXION EN COURS... ===\n";
			ss << "Veuillez patienter.\n";
			break;

		case AuthUiState::Error:
		{
			std::lock_guard<std::mutex> lk(m_resultMutex);
			ss << "=== ERREUR ===\n";
			ss << m_resultError << "\n";
			ss << "[OK — Fermer]\n";
			break;
		}

		case AuthUiState::Success:
			ss << "=== CONNEXION REUSSIE ===\n";
			ss << "Bienvenue, " << login << " !\n";
			ss << "Shard: " << m_resultShardId.load() << "\n";
			break;
		}

		return ss.str();
	}

	// -------------------------------------------------------------------------
	// Private helpers
	// -------------------------------------------------------------------------

	void AuthUiPresenter::SetResultSuccess(uint64_t accountId, uint32_t shardId)
	{
		m_resultAccountId.store(accountId);
		m_resultShardId.store(shardId);
		m_resultSuccess.store(true);
		m_resultReady.store(true);
	}

	void AuthUiPresenter::SetResultError(std::string errorMessage)
	{
		{
			std::lock_guard<std::mutex> lk(m_resultMutex);
			m_resultError = std::move(errorMessage);
		}
		m_resultSuccess.store(false);
		m_resultReady.store(true);
	}

	// static
	std::string AuthUiPresenter::MapErrorCode(uint32_t code)
	{
#if defined(_WIN32)
		using engine::network::NetErrorCode;
		switch (static_cast<NetErrorCode>(code))
		{
		case NetErrorCode::INVALID_CREDENTIALS:    return "Identifiants incorrects.";
		case NetErrorCode::ACCOUNT_LOCKED:         return "Compte verrouillé. Contactez le support.";
		case NetErrorCode::ACCOUNT_NOT_FOUND:      return "Compte introuvable.";
		case NetErrorCode::ALREADY_LOGGED_IN:      return "Ce compte est déjà connecté.";
		case NetErrorCode::REGISTRATION_DISABLED:  return "Les inscriptions sont désactivées.";
		case NetErrorCode::REGISTRATION_INVALID:   return "Données d'inscription invalides.";
		case NetErrorCode::LOGIN_ALREADY_TAKEN:    return "Ce nom d'utilisateur est déjà pris.";
		case NetErrorCode::INVALID_EMAIL:          return "Adresse email invalide.";
		case NetErrorCode::WEAK_PASSWORD:          return "Mot de passe trop faible.";
		case NetErrorCode::INVALID_LOGIN:          return "Nom d'utilisateur invalide.";
		case NetErrorCode::TIMEOUT:                return "Délai réseau dépassé. Réessayez.";
		case NetErrorCode::INTERNAL_ERROR:         return "Erreur serveur interne. Réessayez.";
		default:                                   break;
		}
#endif
		if (code == 0)
			return "Erreur inconnue.";
		return "Erreur serveur (" + std::to_string(code) + ").";
	}

	// -------------------------------------------------------------------------
	// Background threads (Win32 only)
	// -------------------------------------------------------------------------

#if defined(_WIN32)

	void AuthUiPresenter::RunAuthThread()
	{
		// Read credentials under lock before starting the blocking flow.
		std::string login;
		std::string clientHash;
		{
			std::lock_guard<std::mutex> lk(m_fieldMutex);
			login      = m_loginField;
			clientHash = m_passwordField;
		}

		LOG_INFO(Net, "[AuthUiPresenter] RunAuthThread start (login='{}')", login);

		if (login.empty())
		{
			LOG_WARN(Net, "[AuthUiPresenter] RunAuthThread: login field is empty");
			SetResultError("L'identifiant ne peut pas être vide.");
			return;
		}

		if (m_shutdown.load())
		{
			SetResultError("Annulé.");
			return;
		}

		engine::network::NetClient masterClient;
		masterClient.SetAllowInsecureDev(true);

		engine::network::MasterShardClientFlow flow;
		flow.SetMasterAddress(m_masterHost, m_masterPort);
		flow.SetCredentials(login, clientHash);
		flow.SetTimeoutMs(5000u);

		engine::network::MasterShardFlowResult result = flow.Run(&masterClient);

		if (m_shutdown.load())
		{
			LOG_INFO(Net, "[AuthUiPresenter] RunAuthThread: shutdown requested, discarding result");
			SetResultError("Annulé.");
			return;
		}

		if (result.success)
		{
			LOG_INFO(Net, "[AuthUiPresenter] RunAuthThread: auth OK (account_id={}, shard_id={})",
				result.account_id, result.shard_id);
			SetResultSuccess(result.account_id, result.shard_id);
		}
		else
		{
			LOG_WARN(Net, "[AuthUiPresenter] RunAuthThread: auth FAILED ({})", result.errorMessage);
			SetResultError(result.errorMessage.empty()
				? "Échec de la connexion. Vérifiez vos identifiants."
				: result.errorMessage);
		}
	}

	void AuthUiPresenter::RunRegisterThread()
	{
		// Read fields under lock.
		std::string login;
		std::string clientHash;
		std::string email;
		{
			std::lock_guard<std::mutex> lk(m_fieldMutex);
			login      = m_loginField;
			clientHash = m_passwordField;
			email      = m_emailField;
		}

		LOG_INFO(Net, "[AuthUiPresenter] RunRegisterThread start (login='{}', email='{}')", login, email);

		if (login.empty())
		{
			LOG_WARN(Net, "[AuthUiPresenter] RunRegisterThread: login field is empty");
			SetResultError("L'identifiant ne peut pas être vide.");
			return;
		}
		if (email.empty())
		{
			LOG_WARN(Net, "[AuthUiPresenter] RunRegisterThread: email field is empty");
			SetResultError("L'adresse email ne peut pas être vide.");
			return;
		}

		if (m_shutdown.load())
		{
			SetResultError("Annulé.");
			return;
		}

		// ---- Step 1: send REGISTER_REQUEST to master ----

		engine::network::NetClient masterClient;
		masterClient.SetAllowInsecureDev(true);

		LOG_INFO(Net, "[AuthUiPresenter] RunRegisterThread: connecting to master {}:{}",
			m_masterHost, m_masterPort);
		masterClient.Connect(m_masterHost, m_masterPort);

		// Wait for connection (same pattern as MasterShardClientFlow).
		{
			constexpr uint32_t kConnectTimeoutMs = 7000u;
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kConnectTimeoutMs);
			bool connected = false;
			while (std::chrono::steady_clock::now() < deadline && !m_shutdown.load())
			{
				auto events = masterClient.PollEvents();
				for (const auto& ev : events)
				{
					if (ev.type == engine::network::NetClientEventType::Connected)
					{
						connected = true;
						break;
					}
					if (ev.type == engine::network::NetClientEventType::Disconnected)
					{
						LOG_ERROR(Net, "[AuthUiPresenter] RunRegisterThread: disconnected before connected: {}",
							ev.reason);
						SetResultError("Impossible de contacter le serveur.");
						return;
					}
				}
				if (connected) break;
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}

			if (!connected)
			{
				LOG_WARN(Net, "[AuthUiPresenter] RunRegisterThread: connect timeout");
				SetResultError("Délai de connexion dépassé. Vérifiez votre réseau.");
				return;
			}
		}

		if (m_shutdown.load())
		{
			SetResultError("Annulé.");
			return;
		}

		LOG_INFO(Net, "[AuthUiPresenter] RunRegisterThread: sending REGISTER_REQUEST");

		engine::network::RequestResponseDispatcher disp(&masterClient);

		bool registerDone = false;
		bool registerOk   = false;
		engine::network::NetErrorCode regErrorCode = engine::network::NetErrorCode::OK;

		auto regPayload = engine::network::BuildRegisterRequestPayload(login, email, clientHash);

		constexpr uint32_t kRequestTimeoutMs = 5000u;
		if (!disp.SendRequest(
			engine::network::kOpcodeRegisterRequest,
			std::span<const uint8_t>(regPayload.data(), regPayload.size()),
			[&](uint32_t /*reqId*/, bool timeout, std::vector<uint8_t> payload)
			{
				registerDone = true;
				if (timeout)
				{
					regErrorCode = engine::network::NetErrorCode::TIMEOUT;
					return;
				}
				if (!payload.empty())
				{
					auto parsed = engine::network::ParseRegisterResponsePayload(
						payload.data(), payload.size());
					if (parsed)
					{
						registerOk    = (parsed->success != 0);
						regErrorCode  = parsed->error_code;
					}
				}
			},
			kRequestTimeoutMs))
		{
			LOG_ERROR(Net, "[AuthUiPresenter] RunRegisterThread: SendRequest failed");
			SetResultError("Erreur d'envoi de la demande d'inscription.");
			return;
		}

		// Pump until response or timeout.
		auto deadline = std::chrono::steady_clock::now()
			+ std::chrono::milliseconds(kRequestTimeoutMs + 500u);
		while (!registerDone && std::chrono::steady_clock::now() < deadline && !m_shutdown.load())
		{
			disp.Pump();
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}

		if (m_shutdown.load())
		{
			SetResultError("Annulé.");
			return;
		}

		if (!registerDone)
		{
			LOG_WARN(Net, "[AuthUiPresenter] RunRegisterThread: REGISTER_REQUEST timeout");
			SetResultError(MapErrorCode(static_cast<uint32_t>(engine::network::NetErrorCode::TIMEOUT)));
			return;
		}

		if (!registerOk)
		{
			const std::string msg = MapErrorCode(static_cast<uint32_t>(regErrorCode));
			LOG_WARN(Net, "[AuthUiPresenter] RunRegisterThread: registration FAILED (code={}, msg={})",
				static_cast<uint32_t>(regErrorCode), msg);
			SetResultError(msg);
			return;
		}

		LOG_INFO(Net, "[AuthUiPresenter] RunRegisterThread: registration OK, proceeding to auth");

		// ---- Step 2: authentication flow (reuses MasterShardClientFlow) ----

		if (m_shutdown.load())
		{
			SetResultError("Annulé.");
			return;
		}

		engine::network::NetClient authClient;
		authClient.SetAllowInsecureDev(true);

		engine::network::MasterShardClientFlow flow;
		flow.SetMasterAddress(m_masterHost, m_masterPort);
		flow.SetCredentials(login, clientHash);
		flow.SetTimeoutMs(5000u);

		engine::network::MasterShardFlowResult result = flow.Run(&authClient);

		if (m_shutdown.load())
		{
			SetResultError("Annulé.");
			return;
		}

		if (result.success)
		{
			LOG_INFO(Net, "[AuthUiPresenter] RunRegisterThread: auth OK (account_id={}, shard_id={})",
				result.account_id, result.shard_id);
			SetResultSuccess(result.account_id, result.shard_id);
		}
		else
		{
			LOG_WARN(Net, "[AuthUiPresenter] RunRegisterThread: post-register auth FAILED ({})",
				result.errorMessage);
			SetResultError(result.errorMessage.empty()
				? "Inscription réussie, mais la connexion a échoué. Essayez de vous connecter."
				: result.errorMessage);
		}
	}

#else // !_WIN32

	void AuthUiPresenter::RunAuthThread()
	{
		LOG_WARN(Core, "[AuthUiPresenter] RunAuthThread: réseau non disponible sur cette plateforme");
		SetResultError("Réseau non disponible sur cette plateforme.");
	}

	void AuthUiPresenter::RunRegisterThread()
	{
		LOG_WARN(Core, "[AuthUiPresenter] RunRegisterThread: réseau non disponible sur cette plateforme");
		SetResultError("Réseau non disponible sur cette plateforme.");
	}

#endif // _WIN32

} // namespace engine::client
