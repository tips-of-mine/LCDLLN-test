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
			LOG_ERROR(Platform, "CreateWindowExW failed");
			return false;
		}

		m_hwnd = hwnd;
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

		ShowWindow(hwnd, SW_SHOW);
		UpdateWindow(hwnd);

		m_shouldClose = false;
		return true;
	}

	void Window::Destroy()
	{
		if (m_hwnd)
		{
			DestroyWindow(AsHwnd(m_hwnd));
			m_hwnd = nullptr;
		}
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
			if (m_onResize)
			{
				const int w = LOWORD(static_cast<LPARAM>(lparam));
				const int h = HIWORD(static_cast<LPARAM>(lparam));
				m_onResize(w, h);
			}
			break;
		default:
			break;
		}

		return reinterpret_cast<intptr_t>(DefWindowProcW(AsHwnd(m_hwnd),
			static_cast<UINT>(msg),
			static_cast<WPARAM>(wparam),
			static_cast<LPARAM>(lparam)));
	}
}

