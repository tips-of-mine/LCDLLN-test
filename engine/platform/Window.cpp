#include "engine/platform/Window.h"

#include "engine/core/Log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace engine::platform
{
	namespace
	{
		const wchar_t* kClassName = L"LCDLLN_Engine_Window";

		std::wstring ToWide(std::string_view utf8)
		{
			if (utf8.empty())
			{
				return {};
			}
			const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
			std::wstring out(static_cast<size_t>(needed), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), needed);
			return out;
		}

		LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
		{
			auto* wnd = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
			if (wnd)
			{
				return static_cast<LRESULT>(wnd->HandleMessage(
					static_cast<uint32_t>(msg),
					static_cast<uint64_t>(wparam),
					static_cast<int64_t>(lparam)));
			}
			return DefWindowProcW(hwnd, msg, wparam, lparam);
		}

		void EnsureClassRegistered()
		{
			static bool registered = false;
			if (registered)
			{
				return;
			}

			WNDCLASSEXW wc{};
			wc.cbSize = sizeof(wc);
			wc.style = CS_HREDRAW | CS_VREDRAW;
			wc.lpfnWndProc = &WndProc;
			wc.hInstance = GetModuleHandleW(nullptr);
			wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
			wc.lpszClassName = kClassName;

			RegisterClassExW(&wc);
			registered = true;
		}

		HWND AsHwnd(void* p) { return reinterpret_cast<HWND>(p); }
		HFONT AsHfont(void* p) { return reinterpret_cast<HFONT>(p); }
	}

	bool Window::Create(const CreateDesc& desc)
	{
		EnsureClassRegistered();

		const std::wstring titleW = ToWide(desc.title);
		const DWORD style = WS_OVERLAPPEDWINDOW;

		RECT rc{ 0, 0, desc.width, desc.height };
		AdjustWindowRect(&rc, style, FALSE);

		HWND hwnd = CreateWindowExW(
			0,
			kClassName,
			titleW.c_str(),
			style,
			CW_USEDEFAULT, CW_USEDEFAULT,
			rc.right - rc.left, rc.bottom - rc.top,
			nullptr,
			nullptr,
			GetModuleHandleW(nullptr),
			nullptr);

		if (!hwnd)
		{
			LOG_ERROR(Platform, "[Window] Create FAILED: CreateWindowExW");
			return false;
		}

		m_hwnd = hwnd;
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

		HWND overlay = CreateWindowExW(
			WS_EX_TOPMOST,
			L"STATIC",
			L"",
			WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_EDITCONTROL,
			16, 16, 520, 320,
			hwnd,
			nullptr,
			GetModuleHandleW(nullptr),
			nullptr);
		if (!overlay)
		{
			LOG_WARN(Platform, "[Window] Overlay create FAILED");
		}
		else
		{
			HFONT font = CreateFontW(
				21, 0, 0, 0,
				FW_SEMIBOLD,
				FALSE, FALSE, FALSE,
				DEFAULT_CHARSET,
				OUT_DEFAULT_PRECIS,
				CLIP_DEFAULT_PRECIS,
				CLEARTYPE_QUALITY,
				DEFAULT_PITCH | FF_DONTCARE,
				L"Segoe UI");
			m_overlayHwnd = overlay;
			m_overlayFont = font;
			if (font)
			{
				SendMessageW(overlay, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
			}
			ShowWindow(overlay, SW_HIDE);
			UpdateOverlayLayout();
		}

		ShowWindow(hwnd, SW_SHOW);
		UpdateWindow(hwnd);

		m_shouldClose = false;
		LOG_INFO(Platform, "[Window] Create OK (title={}, size={}x{})", desc.title, desc.width, desc.height);
		return true;
	}

	void Window::Destroy()
	{
		if (m_hwnd)
		{
			DestroyWindow(AsHwnd(m_hwnd));
			m_hwnd = nullptr;
		}
		if (m_overlayFont)
		{
			DeleteObject(AsHfont(m_overlayFont));
			m_overlayFont = nullptr;
		}
		m_overlayHwnd = nullptr;
		LOG_INFO(Platform, "[Window] Destroyed");
	}

	void Window::PollEvents()
	{
		MSG msg{};
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	bool Window::ShouldClose() const
	{
		return m_shouldClose;
	}

	void Window::RequestClose()
	{
		m_shouldClose = true;
		if (m_hwnd)
		{
			PostMessageW(AsHwnd(m_hwnd), WM_CLOSE, 0, 0);
		}
	}

	void Window::GetClientSize(int& outWidth, int& outHeight) const
	{
		outWidth = 0;
		outHeight = 0;
		if (!m_hwnd)
		{
			return;
		}

		RECT rc{};
		GetClientRect(AsHwnd(m_hwnd), &rc);
		outWidth = static_cast<int>(rc.right - rc.left);
		outHeight = static_cast<int>(rc.bottom - rc.top);
	}

	void Window::SetTitle(std::string_view title)
	{
		if (!m_hwnd)
		{
			return;
		}

		const std::wstring titleW = ToWide(title);
		SetWindowTextW(AsHwnd(m_hwnd), titleW.c_str());
	}

	void Window::SetOverlayText(std::string_view text)
	{
		if (!m_overlayHwnd)
		{
			return;
		}

		HWND overlay = AsHwnd(m_overlayHwnd);
		if (text.empty())
		{
			SetWindowTextW(overlay, L"");
			ShowWindow(overlay, SW_HIDE);
			return;
		}

		const std::wstring textW = ToWide(text);
		SetWindowTextW(overlay, textW.c_str());
		UpdateOverlayLayout();
		ShowWindow(overlay, SW_SHOW);
		SetWindowPos(overlay, HWND_TOPMOST, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
	}

	void Window::UpdateOverlayLayout()
	{
		if (!m_hwnd || !m_overlayHwnd)
		{
			return;
		}

		RECT rc{};
		GetClientRect(AsHwnd(m_hwnd), &rc);
		const int clientW = static_cast<int>(rc.right - rc.left);
		const int clientH = static_cast<int>(rc.bottom - rc.top);
		const int panelW = max(520, min((clientW * 40) / 100, 760));
		const int panelH = max(360, min((clientH * 60) / 100, 620));
		const int panelX = (clientW - panelW) / 2;
		const int panelY = (clientH - panelH) / 2;
		const int artW = max(150, min(panelW / 3, 240));
		const int width = max(220, panelW - artW - 74);
		const int height = max(140, panelH - 108);
		SetWindowPos(AsHwnd(m_overlayHwnd), HWND_TOPMOST, panelX + artW + 48, panelY + 34, width, height,
			SWP_NOACTIVATE | SWP_SHOWWINDOW);
	}

	void Window::ToggleFullscreen()
	{
		if (!m_hwnd)
		{
			return;
		}

		HWND hwnd = AsHwnd(m_hwnd);

		if (!m_fullscreen)
		{
			m_windowedStyle = static_cast<uint64_t>(GetWindowLongPtrW(hwnd, GWL_STYLE));

			RECT wr{};
			GetWindowRect(hwnd, &wr);
			m_windowedX = wr.left;
			m_windowedY = wr.top;
			m_windowedW = wr.right - wr.left;
			m_windowedH = wr.bottom - wr.top;

			MONITORINFO mi{ sizeof(mi) };
			GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);

			SetWindowLongPtrW(hwnd, GWL_STYLE, static_cast<LONG_PTR>(m_windowedStyle & ~WS_OVERLAPPEDWINDOW));
			SetWindowPos(hwnd, HWND_TOP,
				mi.rcMonitor.left, mi.rcMonitor.top,
				mi.rcMonitor.right - mi.rcMonitor.left,
				mi.rcMonitor.bottom - mi.rcMonitor.top,
				SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
			m_fullscreen = true;
		}
		else
		{
			SetWindowLongPtrW(hwnd, GWL_STYLE, static_cast<LONG_PTR>(m_windowedStyle));
			SetWindowPos(hwnd, nullptr,
				m_windowedX, m_windowedY, m_windowedW, m_windowedH,
				SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
			m_fullscreen = false;
		}
	}

	void Window::SetOnResize(std::function<void(int, int)> cb)
	{
		m_onResize = std::move(cb);
	}

	void Window::SetOnClose(std::function<void()> cb)
	{
		m_onClose = std::move(cb);
	}

	void Window::SetMessageHook(std::function<void(uint32_t, uint64_t, int64_t)> hook)
	{
		m_msgHook = std::move(hook);
	}

	intptr_t Window::HandleMessage(uint32_t msg, uint64_t wparam, int64_t lparam)
	{
		if (m_msgHook)
		{
			m_msgHook(msg, wparam, lparam);
		}

		switch (msg)
		{
		case WM_CLOSE:
			m_shouldClose = true;
			if (m_onClose)
			{
				m_onClose();
			}
			return 0;
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		case WM_SIZE:
LOG_DEBUG(Platform, "[WINDOW] WM_SIZE wparam={} w={} h={}", (unsigned long long)wparam, LOWORD((LPARAM)lparam), HIWORD((LPARAM)lparam));
			UpdateOverlayLayout();
   			if (wparam != SIZE_MINIMIZED && m_onResize)
			{
				const int w = LOWORD(static_cast<LPARAM>(lparam));
				const int h = HIWORD(static_cast<LPARAM>(lparam));
				if (w > 0 && h > 0)
					m_onResize(w, h);
			}
			break;
		case WM_CTLCOLORSTATIC:
			if (reinterpret_cast<HWND>(lparam) == AsHwnd(m_overlayHwnd))
			{
				HDC hdc = reinterpret_cast<HDC>(wparam);
				SetTextColor(hdc, RGB(232, 237, 243));
				SetBkColor(hdc, RGB(27, 40, 54));
				static HBRUSH brush = CreateSolidBrush(RGB(27, 40, 54));
				return reinterpret_cast<intptr_t>(brush);
			}
			break;
		default:
			break;
		}

		return static_cast<intptr_t>(DefWindowProcW(AsHwnd(m_hwnd),
			static_cast<UINT>(msg),
			static_cast<WPARAM>(wparam),
			static_cast<LPARAM>(lparam)));
	}
}

