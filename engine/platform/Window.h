#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace engine::platform
{
	/// Basic OS window abstraction (Win32 implementation).
	class Window final
	{
	public:
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

		/// Updates the native window title.
		void SetTitle(std::string_view title);

		/// Shows or hides a native overlay panel above the swapchain.
		void SetOverlayText(std::string_view text);

		/// Toggle fullscreen (optional; Win32 implementation).
		void ToggleFullscreen();

		/// Set callbacks for resize and close events.
		void SetOnResize(std::function<void(int /*w*/, int /*h*/)> cb);
		void SetOnClose(std::function<void()> cb);

		/// Provide a message handler hook (used by Input).
		void SetMessageHook(std::function<void(uint32_t msg, uint64_t wparam, int64_t lparam)> hook);

		/// Handle a native platform message (used by WndProc on Win32).
		intptr_t HandleMessage(uint32_t msg, uint64_t wparam, int64_t lparam);

	private:
		void UpdateOverlayLayout();

		void* m_hwnd = nullptr; // HWND
		void* m_overlayHwnd = nullptr; // HWND
		void* m_overlayFont = nullptr; // HFONT
		bool m_shouldClose = false;
		bool m_fullscreen = false;

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

