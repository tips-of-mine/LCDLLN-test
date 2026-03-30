#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace engine::client
{
	/// State of the authentication/registration UI screen (STAB.13).
	enum class AuthUiState : uint8_t
	{
		Login,       ///< Login form is the active screen (default entry point at boot).
		Register,    ///< Registration form is the active screen.
		Submitting,  ///< A network operation is in progress; the form is non-interactive.
		Error,       ///< An error occurred; the error message is displayed to the user.
		Success      ///< Authentication flow completed successfully; the game can proceed.
	};

	/// Presenter for the login/registration UI screen (STAB.13).
	///
	/// Manages a state machine over Login / Register / Submitting / Error / Success.
	/// Network operations (AUTH_REQUEST, REGISTER_REQUEST) are dispatched on a
	/// background thread; results are delivered safely to the main thread via Poll().
	///
	/// Password is forwarded as client_hash per the existing protocol (M20.2/M22.6).
	/// The full auth flow (AUTH → SERVER_LIST → TICKET → SHARD) reuses MasterShardClientFlow.
	///
	/// Asset paths for background images follow the relative convention:
	///   engine/assets/ui/login/background.png
	///   engine/assets/ui/register/background.png
	/// They are exposed via GetLoginBgPath() / GetRegisterBgPath() for the renderer.
	class AuthUiPresenter final
	{
	public:
		AuthUiPresenter() = default;
		~AuthUiPresenter();

		// -----------------------------------------------------------------------
		// Lifecycle
		// -----------------------------------------------------------------------

		/// Initialize the presenter with the master server address read from config.
		/// \param masterHost  Hostname or IP of the master server.
		/// \param masterPort  TCP port of the master server.
		/// \return true on success; emits LOG_ERROR and returns false if already initialized.
		bool Init(std::string masterHost, uint16_t masterPort);

		/// Release resources and join any pending background network thread.
		/// Safe to call even if Init() was never called or returned false.
		void Shutdown();

		// -----------------------------------------------------------------------
		// Input field setters (call from main thread)
		// -----------------------------------------------------------------------

		/// Set the login/username field value.
		void SetLoginField(std::string value);

		/// Set the password field value (forwarded as client_hash per protocol).
		void SetPasswordField(std::string value);

		/// Set the email field value (used by the registration form only).
		void SetEmailField(std::string value);

		// -----------------------------------------------------------------------
		// Screen transitions (call from main thread)
		// -----------------------------------------------------------------------

		/// Switch from the Login screen to the Register screen.
		/// No-op if the current state is not Login.
		void SwitchToRegister();

		/// Switch from the Register or Error screen back to the Login screen.
		/// No-op if the current state is not Register or Error.
		void SwitchToLogin();

		// -----------------------------------------------------------------------
		// Actions (call from main thread)
		// -----------------------------------------------------------------------

		/// Submit the login form.
		/// Transitions to Submitting and launches the background auth network thread.
		/// No-op if the current state is not Login.
		void SubmitLogin();

		/// Submit the registration form.
		/// Transitions to Submitting and launches the background register network thread.
		/// No-op if the current state is not Register.
		void SubmitRegister();

		/// Dismiss the current error and return to the pre-submission screen (Login or Register).
		/// No-op if the current state is not Error.
		void DismissError();

		// -----------------------------------------------------------------------
		// Per-frame polling (call from main thread, e.g. in Engine::Update)
		// -----------------------------------------------------------------------

		/// Poll for a network result from the background thread.
		/// Must be called from the main thread each frame while in Submitting state.
		/// Transitions state to Success or Error when the background work completes.
		/// \return true if the state changed during this call.
		bool Poll();

		// -----------------------------------------------------------------------
		// Accessors
		// -----------------------------------------------------------------------

		/// Current UI state (atomic read; safe from any thread).
		AuthUiState GetState() const { return static_cast<AuthUiState>(m_state.load()); }

		/// Human-readable error message; valid when state == Error.
		std::string GetErrorMessage() const;

		/// Login field value (guarded read).
		std::string GetLoginField() const;

		/// Account id received after a successful authentication (valid in Success state).
		uint64_t GetAccountId() const { return m_resultAccountId.load(); }

		/// Shard id received after a successful authentication (valid in Success state).
		uint32_t GetShardId() const { return m_resultShardId.load(); }

		bool IsInitialized() const { return m_initialized; }

		/// Relative path to the login background asset (engine/assets/ui/login/background.png).
		static const char* GetLoginBgPath()    { return "engine/assets/ui/login/background.png"; }
		/// Relative path to the register background asset (engine/assets/ui/register/background.png).
		static const char* GetRegisterBgPath() { return "engine/assets/ui/register/background.png"; }

		// -----------------------------------------------------------------------
		// Text panel for the debug overlay / HUD renderer
		// -----------------------------------------------------------------------

		/// Build a multi-line text panel suitable for a debug overlay or HUD renderer.
		/// Shows the current screen, field labels, and any error or success message.
		std::string BuildPanelText() const;

	private:
		// ---- Background thread bodies (Win32 only; no-op on other platforms) ----

		/// Background thread: runs the full MasterShardClientFlow (AUTH → SERVER_LIST → TICKET → SHARD).
		void RunAuthThread();

		/// Background thread: sends REGISTER_REQUEST to master, then on success runs the full auth flow.
		void RunRegisterThread();

		// ---- Helpers ----

		/// Map a raw NetErrorCode uint32 to a localized user-facing message string.
		static std::string MapErrorCode(uint32_t code);

		/// Write a successful result (called from background thread).
		void SetResultSuccess(uint64_t accountId, uint32_t shardId);

		/// Write a failure result (called from background thread).
		void SetResultError(std::string errorMessage);

		// ---- Config ----
		std::string m_masterHost;
		uint16_t    m_masterPort = 0;

		// ---- Input fields (guarded by m_fieldMutex) ----
		mutable std::mutex m_fieldMutex;
		std::string        m_loginField;
		std::string        m_passwordField; ///< Forwarded as client_hash per protocol.
		std::string        m_emailField;

		// ---- State machine ----
		/// State stored as uint8_t for guaranteed lock-free atomic support on all MSVC/x86-64 targets.
		/// Use GetState() / SetState() helpers to convert to/from AuthUiState.
		std::atomic<uint8_t> m_state{ static_cast<uint8_t>(AuthUiState::Login) };
		AuthUiState          m_preSubmitState = AuthUiState::Login; ///< State to restore on error.

		// ---- Background result (written by network thread, read by Poll()) ----
		std::atomic<bool>     m_resultReady{ false };
		std::atomic<bool>     m_resultSuccess{ false };
		std::atomic<uint64_t> m_resultAccountId{ 0 };
		std::atomic<uint32_t> m_resultShardId{ 0 };
		mutable std::mutex    m_resultMutex;
		std::string           m_resultError; ///< Guarded by m_resultMutex.

		// ---- Background thread ----
		std::thread       m_networkThread;
		std::atomic<bool> m_shutdown{ false };

		bool m_initialized = false;
	};

} // namespace engine::client
