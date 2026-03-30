#pragma once

#include "engine/core/Config.h"
#include "engine/platform/Input.h"

#include <mutex>
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
	/// Assets reference: \c engine/assets/ui/login and \c engine/assets/ui/register (documented in panel text; no absolute paths).
	class AuthUiPresenter final
	{
	public:
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

		/// Escape: back from register to login, or clear error; returns true if consumed.
		bool OnEscape();

		bool SetViewportSize(uint32_t width, uint32_t height);

	private:
		void AppendPasswordStars(std::string& out, size_t len) const;
		void EnsurePasswordSalt(const engine::core::Config& cfg);
		std::string ComputeClientHash(const engine::core::Config& cfg) const;
		void StartRegisterWorker(const engine::core::Config& cfg);
		void StartMasterFlowWorker(const engine::core::Config& cfg);
		void PollAsyncResult(const engine::core::Config& cfg);
		void UpdateWindowTitle(engine::platform::Window& window) const;
		void JoinWorker();

		enum class Phase
		{
			Login,
			Register,
			Submitting,
			Error
		};

		bool m_initialized = false;
		bool m_flowComplete = false;
		bool m_authEnabled = true;
		Phase m_phase = Phase::Login;

		std::string m_login;
		std::string m_password;
		std::string m_email;
		uint32_t m_activeField = 0;
		std::string m_userErrorText;
		std::string m_infoBanner;

		std::vector<uint8_t> m_argonSalt{};
		uint32_t m_viewportW = 0;
		uint32_t m_viewportH = 0;

		/// Background worker for REGISTER_REQUEST or MasterShardClientFlow (TCP); join before destroy.
		struct AsyncResult
		{
			bool ready = false;
			bool success = false;
			std::string message;
		};
		AsyncResult m_asyncResult{};
		std::thread m_worker{};
		bool m_pendingAsyncIsRegister = false;
		std::mutex m_asyncMutex{};
	};

}

