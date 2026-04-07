#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::platform
{
	/// Basic OS window abstraction (Win32 implementation).
	class Window final
	{
	public:
		struct AuthScreenState
		{
			bool visible = false;
			bool showPassword = true;
			bool showRemember = true;
			bool showForgot = true;
			bool showRegister = true;
			bool showBack = false;
			bool showQuit = true;
			bool showInfoImage = false;
			bool focusPrimary = false;
			bool focusPassword = false;
			bool rememberChecked = false;
			std::string titleLine1;
			std::string titleLine2;
			std::string sectionTitle;
			std::string primaryLabel;
			std::string primaryValue;
			std::string passwordLabel;
			std::string passwordValue;
			std::string rememberLabel;
			std::string forgotLabel;
			std::string registerLabel;
			std::string submitLabel;
			std::string quitLabel;
			std::string backgroundImagePath;
			std::string logoImagePath;
			std::string infoImagePath;
		};

		enum class AuthScreenCommand : uint8_t
		{
			None,
			Submit,
			Quit,
			OpenRegister,
			OpenForgotPassword,
			BackToLogin
		};

		struct CreateDesc
		{
			/// Window title (UTF-8).
			std::string_view title = "Engine";
			/// Client area width in pixels.
			int width = 1280;
			/// Client area height in pixels.
			int height = 720;
		};

		Window() = default;
		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;

		/// Create the OS window.
		bool Create(const CreateDesc& desc);

		/// Destroy the OS window (safe to call multiple times).
		void Destroy();

		/// Poll OS events (non-blocking) and dispatch callbacks.
		void PollEvents();

		/// Returns true if a close was requested (e.g., user clicked X).
		bool ShouldClose() const;

		/// Request the window to close.
		void RequestClose();

		/// Returns current client size in pixels.
		void GetClientSize(int& outWidth, int& outHeight) const;
		void* GetNativeHandle() const { return m_hwnd; }

		/// Updates the native window title.
		void SetTitle(std::string_view title);
		bool OpenExternalUrl(std::string_view url) const;

		/// Shows or hides a native overlay panel above the swapchain.
		void SetOverlayText(std::string_view text);
		void SetAuthScreenState(const AuthScreenState& state);
		AuthScreenCommand ConsumeAuthScreenCommand();
		std::string GetAuthPrimaryValue() const;
		std::string GetAuthPasswordValue() const;
		bool GetAuthRememberChecked() const;

		/// Toggle fullscreen (optional; Win32 implementation).
		void ToggleFullscreen();
		bool IsFullscreen() const { return m_fullscreen; }

		/// Set callbacks for resize and close events.
		void SetOnResize(std::function<void(int /*w*/, int /*h*/)> cb);
		void SetOnClose(std::function<void()> cb);

		/// Provide a message handler hook (used by Input).
		void SetMessageHook(std::function<void(uint32_t msg, uint64_t wparam, int64_t lparam)> hook);

		/// Optional: resolve auth UI image paths to raw bytes (e.g. `FileSystem::ReadAllBytesContent` + `.texr`). Empty loader = file paths only.
		void SetAuthImageBytesLoader(std::function<std::vector<uint8_t>(std::string_view)> loader);

		/// Handle a native platform message (used by WndProc on Win32).
		intptr_t HandleMessage(uint32_t msg, uint64_t wparam, int64_t lparam);

	private:
		void UpdateOverlayLayout();
		void UpdateAuthScreenLayout();

		void* m_hwnd = nullptr; // HWND
		void* m_overlayHwnd = nullptr; // HWND
		void* m_overlayFont = nullptr; // HFONT
		void* m_authTitleLine1Hwnd = nullptr; // HWND
		void* m_authTitleLine2Hwnd = nullptr; // HWND
		void* m_authBackgroundHwnd = nullptr; // HWND
		void* m_authLogoHwnd = nullptr; // HWND
		void* m_authInfoHwnd = nullptr; // HWND
		void* m_authSectionTitleHwnd = nullptr; // HWND
		void* m_authPrimaryLabelHwnd = nullptr; // HWND
		void* m_authPrimaryEditHwnd = nullptr; // HWND
		void* m_authRememberCheckboxHwnd = nullptr; // HWND
		void* m_authPasswordLabelHwnd = nullptr; // HWND
		void* m_authPasswordEditHwnd = nullptr; // HWND
		void* m_authForgotButtonHwnd = nullptr; // HWND
		void* m_authRegisterButtonHwnd = nullptr; // HWND
		void* m_authBackButtonHwnd = nullptr; // HWND
		void* m_authSubmitButtonHwnd = nullptr; // HWND
		void* m_authQuitButtonHwnd = nullptr; // HWND
		void* m_authTitleFont = nullptr; // HFONT
		void* m_authUiFont = nullptr; // HFONT
		void* m_authBackgroundBitmap = nullptr; // HBITMAP
		void* m_authLogoBitmap = nullptr; // HBITMAP
		void* m_authInfoBitmap = nullptr; // HBITMAP
		bool m_shouldClose = false;
		bool m_fullscreen = false;
		bool m_authVisible = false;
		bool m_authRememberChecked = false;
		bool m_authPrimaryDirty = false;
		bool m_authPasswordDirty = false;
		bool m_authSyncInProgress = false;
		std::string m_authPrimaryValue;
		std::string m_authPasswordValue;
		std::string m_authBackgroundPath;
		std::string m_authLogoPath;
		std::string m_authInfoPath;
		int m_authBackgroundWidth = 0;
		int m_authBackgroundHeight = 0;
		int m_authLogoWidth = 0;
		int m_authLogoHeight = 0;
		int m_authInfoWidth = 0;
		int m_authInfoHeight = 0;
		AuthScreenCommand m_pendingAuthCommand = AuthScreenCommand::None;

		std::function<void(int, int)> m_onResize;
		std::function<void()> m_onClose;
		std::function<void(uint32_t, uint64_t, int64_t)> m_msgHook;

		// Stored windowed rect/style for fullscreen toggle.
		uint64_t m_windowedStyle = 0;
		int m_windowedX = 0;
		int m_windowedY = 0;
		int m_windowedW = 0;
		int m_windowedH = 0;
	};
}

