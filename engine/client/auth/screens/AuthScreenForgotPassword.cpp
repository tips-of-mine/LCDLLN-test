#include "engine/client/AuthUi.h"

#include "engine/core/Log.h"
#include "engine/network/AuthRegisterPayloads.h"
#include "engine/network/NetClient.h"
#include "engine/network/ProtocolV1Constants.h"
#include "engine/network/RequestResponseDispatcher.h"
#include "engine/platform/Input.h"
#include "engine/platform/Window.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace engine::client
{
#if defined(_WIN32)

	namespace
	{
		bool WaitConnected(engine::network::NetClient* c, uint32_t timeoutMs)
		{
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
			while (std::chrono::steady_clock::now() < deadline)
			{
				auto events = c->PollEvents();
				for (const auto& ev : events)
				{
					if (ev.type == engine::network::NetClientEventType::Connected)
					{
						return true;
					}
					if (ev.type == engine::network::NetClientEventType::Disconnected)
					{
						return false;
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
			return false;
		}

		void ApplyMasterTlsConfig(engine::network::NetClient& client, const std::string& fingerprintHex, bool allowInsecure)
		{
			if (!fingerprintHex.empty())
			{
				client.SetExpectedServerFingerprint(fingerprintHex);
			}
			client.SetAllowInsecureDev(allowInsecure);
		}
	} // namespace

	void AuthUiPresenter::ImGuiNavigateToForgotFromLogin()
	{
		if (m_phase != Phase::Login)
		{
			return;
		}
		m_userErrorText.clear();
		SetPhase(Phase::ForgotPassword);
		m_activeField = 0;
	}

	void AuthUiPresenter::ImGuiSubmitForgotPassword(const engine::core::Config& cfg, const char* emailUtf8)
	{
		if (m_phase != Phase::ForgotPassword)
		{
			return;
		}
		m_email = emailUtf8 ? std::string(emailUtf8) : std::string();
		SubmitCurrentPhase(cfg);
	}

	void AuthUiPresenter::ImGuiBackFromForgotToLogin()
	{
		if (m_phase != Phase::ForgotPassword)
		{
			return;
		}
		m_userErrorText.clear();
		m_activeField = 0;
		SetPhase(Phase::Login);
	}

	void AuthUiPresenter::BuildModel_ForgotPassword(RenderModel& model) const
	{
		model.sectionTitle = Tr("auth.section.forgot_password");
		{
			RenderField f{};
			f.label = Tr("common.email");
			f.value = m_email;
			f.active = m_activeField == 0u;
			f.hovered = m_hoveredFieldIndex == 0;
			f.secret = false;
			f.cyclePicker = false;
			model.fields.push_back(std::move(f));
		}
		{
			RenderAction submit{};
			submit.labelKey = "common.submit";
			submit.primary = true;
			submit.active = true;
			submit.emphasized = false;
			submit.hovered = m_hoveredActionIndex == 0;
			model.actions.push_back(std::move(submit));
		}
		{
			RenderAction back{};
			back.labelKey = "auth.hint.return_login";
			back.primary = false;
			back.active = true;
			back.emphasized = false;
			back.hovered = m_hoveredActionIndex == 1;
			model.actions.push_back(std::move(back));
		}
	}

	void AuthUiPresenter::Update_ForgotPassword(engine::platform::Input&, const engine::core::Config&, engine::platform::Window&,
		bool usingNativeAuth, bool /*authUiImguiMode*/)
	{
		if (usingNativeAuth || m_phase != Phase::ForgotPassword)
		{
			return;
		}
	}

	void AuthUiPresenter::StartForgotPasswordWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		const std::string host = cfg.GetEffectiveMasterHost("localhost");
		const uint16_t port = static_cast<uint16_t>(cfg.GetInt("client.master_port", 3840));
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const bool allowInsecure = cfg.GetBool("client.allow_insecure_dev", true);
		const std::string serverFingerprint = cfg.GetString("client.server_fingerprint", "");
		const std::string email = m_email;

		m_pendingAsyncKind = AsyncKind::ForgotPassword;
		{
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = {};
		}

		m_worker = std::thread([this, host, port, timeoutMs, allowInsecure, serverFingerprint, email]() {
			AsyncResult local{};
			engine::network::NetClient client;
			ApplyMasterTlsConfig(client, serverFingerprint, allowInsecure);
			client.Connect(host, port);
			if (!WaitConnected(&client, timeoutMs + 2000u))
			{
				local.ready = true;
				local.message = "Master connect failed or timeout.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
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
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
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
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = local;
		});
	}

#else

	void AuthUiPresenter::ImGuiSubmitForgotPassword(const engine::core::Config&, const char*) {}
	void AuthUiPresenter::ImGuiBackFromForgotToLogin() {}

	void AuthUiPresenter::BuildModel_ForgotPassword(RenderModel&) const {}

	void AuthUiPresenter::Update_ForgotPassword(engine::platform::Input&, const engine::core::Config&, engine::platform::Window&, bool, bool)
	{
	}

#endif
} // namespace engine::client
