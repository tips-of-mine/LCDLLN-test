#include "engine/platform/Window.h"

#include "engine/core/Log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include <filesystem>
#include <string>

namespace engine::platform
{
	namespace
	{
		const wchar_t* kClassName = L"LCDLLN_Engine_Window";
		constexpr int kAuthPrimaryEditId = 1001;
		constexpr int kAuthRememberCheckboxId = 1002;
		constexpr int kAuthPasswordEditId = 1003;
		constexpr int kAuthForgotButtonId = 1004;
		constexpr int kAuthRegisterButtonId = 1005;
		constexpr int kAuthBackButtonId = 1006;
		constexpr int kAuthSubmitButtonId = 1007;
		constexpr int kAuthQuitButtonId = 1008;
		ULONG_PTR g_gdiplusToken = 0;

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

		std::string FromWide(const wchar_t* text)
		{
			if (!text || !*text)
			{
				return {};
			}
			const int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
			if (needed <= 1)
			{
				return {};
			}
			std::string out(static_cast<size_t>(needed - 1), '\0');
			WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), needed, nullptr, nullptr);
			return out;
		}

		void EnsureGdiplusStarted()
		{
			if (g_gdiplusToken != 0)
			{
				return;
			}
			Gdiplus::GdiplusStartupInput input;
			Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr);
		}

		std::filesystem::path GetExecutableDirectory()
		{
			wchar_t buffer[MAX_PATH]{};
			const DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
			if (len == 0)
			{
				return {};
			}
			return std::filesystem::path(buffer).parent_path();
		}

		std::filesystem::path ResolveUiImagePath(std::string_view relativePath)
		{
			if (relativePath.empty())
			{
				return {};
			}

			const std::filesystem::path raw(relativePath);
			if (raw.is_absolute() && std::filesystem::exists(raw))
			{
				return raw;
			}

			std::filesystem::path base = std::filesystem::current_path();
			for (int i = 0; i < 6; ++i)
			{
				const std::filesystem::path candidate = base / raw;
				if (std::filesystem::exists(candidate))
				{
					return candidate;
				}
				if (!base.has_parent_path())
				{
					break;
				}
				base = base.parent_path();
			}

			base = GetExecutableDirectory();
			for (int i = 0; i < 6; ++i)
			{
				const std::filesystem::path candidate = base / raw;
				if (std::filesystem::exists(candidate))
				{
					return candidate;
				}
				if (!base.has_parent_path())
				{
					break;
				}
				base = base.parent_path();
			}

			return raw;
		}

		HBITMAP LoadBitmapFromPng(std::string_view pathUtf8, int targetWidth, int targetHeight)
		{
			EnsureGdiplusStarted();
			const std::filesystem::path resolved = ResolveUiImagePath(pathUtf8);
			const std::wstring resolvedW = resolved.wstring();
			Gdiplus::Bitmap source(resolvedW.c_str());
			if (source.GetLastStatus() != Gdiplus::Ok)
			{
				return nullptr;
			}
			const int width = targetWidth > 0 ? targetWidth : static_cast<int>(source.GetWidth());
			const int height = targetHeight > 0 ? targetHeight : static_cast<int>(source.GetHeight());
			Gdiplus::Bitmap scaled(width, height, PixelFormat32bppARGB);
			Gdiplus::Graphics graphics(&scaled);
			graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
			graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
			graphics.DrawImage(&source, 0, 0, width, height);
			HBITMAP out = nullptr;
			scaled.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &out);
			return out;
		}

		bool ReplaceStaticBitmap(void* hwndHandle, void*& bitmapHandle, std::string& cachedPath, int& cachedWidth, int& cachedHeight,
			std::string_view newPath, int targetWidth, int targetHeight)
		{
			if (!hwndHandle)
			{
				return false;
			}
			if (cachedPath == newPath && cachedWidth == targetWidth && cachedHeight == targetHeight)
			{
				return bitmapHandle != nullptr;
			}
			if (bitmapHandle)
			{
				SendMessageW(AsHwnd(hwndHandle), STM_SETIMAGE, IMAGE_BITMAP, 0);
				DeleteObject(reinterpret_cast<HBITMAP>(bitmapHandle));
				bitmapHandle = nullptr;
			}
			cachedPath = std::string(newPath);
			cachedWidth = targetWidth;
			cachedHeight = targetHeight;
			if (newPath.empty())
			{
				return false;
			}
			bitmapHandle = LoadBitmapFromPng(newPath, targetWidth, targetHeight);
			if (bitmapHandle)
			{
				SendMessageW(AsHwnd(hwndHandle), STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(bitmapHandle));
				return true;
			}
			return false;
		}
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

		auto createAuthControl = [hwnd](DWORD exStyle, const wchar_t* className, const wchar_t* text, DWORD style, int id) -> HWND
		{
			return CreateWindowExW(
				exStyle,
				className,
				text,
				WS_CHILD | style,
				0, 0, 0, 0,
				hwnd,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
				GetModuleHandleW(nullptr),
				nullptr);
		};

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

		m_authTitleFont = CreateFontW(34, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
		m_authUiFont = CreateFontW(22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

		m_authBackgroundHwnd = createAuthControl(0, L"STATIC", L"", SS_BITMAP, 0);
		m_authLogoHwnd = createAuthControl(0, L"STATIC", L"", SS_BITMAP, 0);
		m_authInfoHwnd = createAuthControl(0, L"STATIC", L"", SS_BITMAP, 0);
		m_authTitleLine1Hwnd = createAuthControl(0, L"STATIC", L"", SS_CENTER | SS_NOPREFIX, 0);
		m_authTitleLine2Hwnd = createAuthControl(0, L"STATIC", L"", SS_CENTER | SS_NOPREFIX, 0);
		m_authSectionTitleHwnd = createAuthControl(0, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0);
		m_authPrimaryLabelHwnd = createAuthControl(0, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0);
		m_authPrimaryEditHwnd = createAuthControl(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, kAuthPrimaryEditId);
		m_authRememberCheckboxHwnd = createAuthControl(0, L"BUTTON", L"Se souvenir", BS_AUTOCHECKBOX | WS_TABSTOP, kAuthRememberCheckboxId);
		m_authPasswordLabelHwnd = createAuthControl(0, L"STATIC", L"Mot de passe", SS_LEFT | SS_NOPREFIX, 0);
		m_authPasswordEditHwnd = createAuthControl(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD, kAuthPasswordEditId);
		m_authForgotButtonHwnd = createAuthControl(0, L"BUTTON", L"Recuperation du mot de passe", BS_PUSHBUTTON | WS_TABSTOP, kAuthForgotButtonId);
		m_authRegisterButtonHwnd = createAuthControl(0, L"BUTTON", L"Inscription", BS_PUSHBUTTON | WS_TABSTOP, kAuthRegisterButtonId);
		m_authSubmitButtonHwnd = createAuthControl(0, L"BUTTON", L"Valider", BS_DEFPUSHBUTTON | WS_TABSTOP, kAuthSubmitButtonId);
		m_authQuitButtonHwnd = createAuthControl(0, L"BUTTON", L"Quitter", BS_PUSHBUTTON | WS_TABSTOP, kAuthQuitButtonId);

		void* authControls[] = {
			m_authBackgroundHwnd, m_authLogoHwnd, m_authInfoHwnd,
			m_authTitleLine1Hwnd, m_authTitleLine2Hwnd, m_authSectionTitleHwnd, m_authPrimaryLabelHwnd,
			m_authPrimaryEditHwnd, m_authRememberCheckboxHwnd, m_authPasswordLabelHwnd, m_authPasswordEditHwnd,
			m_authForgotButtonHwnd, m_authRegisterButtonHwnd, m_authSubmitButtonHwnd, m_authQuitButtonHwnd
		};
		for (void* ctrl : authControls)
		{
			if (!ctrl)
			{
				continue;
			}
			SendMessageW(AsHwnd(ctrl), WM_SETFONT, reinterpret_cast<WPARAM>(m_authUiFont), TRUE);
			ShowWindow(AsHwnd(ctrl), SW_HIDE);
		}
		if (m_authTitleFont)
		{
			SendMessageW(AsHwnd(m_authTitleLine1Hwnd), WM_SETFONT, reinterpret_cast<WPARAM>(m_authTitleFont), TRUE);
			SendMessageW(AsHwnd(m_authTitleLine2Hwnd), WM_SETFONT, reinterpret_cast<WPARAM>(m_authTitleFont), TRUE);
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
		if (m_authTitleFont)
		{
			DeleteObject(AsHfont(m_authTitleFont));
			m_authTitleFont = nullptr;
		}
		if (m_authUiFont)
		{
			DeleteObject(AsHfont(m_authUiFont));
			m_authUiFont = nullptr;
		}
		if (m_authBackgroundBitmap)
		{
			DeleteObject(reinterpret_cast<HBITMAP>(m_authBackgroundBitmap));
			m_authBackgroundBitmap = nullptr;
		}
		if (m_authLogoBitmap)
		{
			DeleteObject(reinterpret_cast<HBITMAP>(m_authLogoBitmap));
			m_authLogoBitmap = nullptr;
		}
		if (m_authInfoBitmap)
		{
			DeleteObject(reinterpret_cast<HBITMAP>(m_authInfoBitmap));
			m_authInfoBitmap = nullptr;
		}
		m_overlayHwnd = nullptr;
		m_authBackgroundHwnd = nullptr;
		m_authLogoHwnd = nullptr;
		m_authInfoHwnd = nullptr;
		m_authTitleLine1Hwnd = nullptr;
		m_authTitleLine2Hwnd = nullptr;
		m_authSectionTitleHwnd = nullptr;
		m_authPrimaryLabelHwnd = nullptr;
		m_authPrimaryEditHwnd = nullptr;
		m_authRememberCheckboxHwnd = nullptr;
		m_authPasswordLabelHwnd = nullptr;
		m_authPasswordEditHwnd = nullptr;
		m_authForgotButtonHwnd = nullptr;
		m_authRegisterButtonHwnd = nullptr;
		m_authBackButtonHwnd = nullptr;
		m_authSubmitButtonHwnd = nullptr;
		m_authQuitButtonHwnd = nullptr;
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

	void Window::SetAuthScreenState(const AuthScreenState& state)
	{
		m_authVisible = state.visible;
		if (!m_hwnd)
		{
			return;
		}

		auto setText = [](void* hwndHandle, std::string_view text)
		{
			if (!hwndHandle)
			{
				return;
			}
			const std::wstring textW = ToWide(text);
			SetWindowTextW(AsHwnd(hwndHandle), textW.c_str());
		};
		auto setVisible = [](void* hwndHandle, bool visible)
		{
			if (hwndHandle)
			{
				ShowWindow(AsHwnd(hwndHandle), visible ? SW_SHOW : SW_HIDE);
			}
		};

		if (!state.visible)
		{
			setVisible(m_authBackgroundHwnd, false);
			setVisible(m_authLogoHwnd, false);
			setVisible(m_authInfoHwnd, false);
			setVisible(m_authTitleLine1Hwnd, false);
			setVisible(m_authTitleLine2Hwnd, false);
			setVisible(m_authSectionTitleHwnd, false);
			setVisible(m_authPrimaryLabelHwnd, false);
			setVisible(m_authPrimaryEditHwnd, false);
			setVisible(m_authRememberCheckboxHwnd, false);
			setVisible(m_authPasswordLabelHwnd, false);
			setVisible(m_authPasswordEditHwnd, false);
			setVisible(m_authForgotButtonHwnd, false);
			setVisible(m_authRegisterButtonHwnd, false);
			setVisible(m_authBackButtonHwnd, false);
			setVisible(m_authSubmitButtonHwnd, false);
			setVisible(m_authQuitButtonHwnd, false);
			return;
		}

		m_authSyncInProgress = true;
		setText(m_authTitleLine1Hwnd, state.titleLine1);
		setText(m_authTitleLine2Hwnd, state.titleLine2);
		setText(m_authSectionTitleHwnd, state.sectionTitle);
		setText(m_authPrimaryLabelHwnd, state.primaryLabel);
		if (m_authPrimaryValue != state.primaryValue)
		{
			setText(m_authPrimaryEditHwnd, state.primaryValue);
			m_authPrimaryValue = state.primaryValue;
		}
		if (m_authPasswordValue != state.passwordValue)
		{
			setText(m_authPasswordEditHwnd, state.passwordValue);
			m_authPasswordValue = state.passwordValue;
		}
		setText(m_authSubmitButtonHwnd, state.submitLabel.empty() ? std::string_view("Valider") : std::string_view(state.submitLabel));
		SendMessageW(AsHwnd(m_authRememberCheckboxHwnd), BM_SETCHECK, state.rememberChecked ? BST_CHECKED : BST_UNCHECKED, 0);
		m_authRememberChecked = state.rememberChecked;
		m_authSyncInProgress = false;

		m_authBackgroundPath = state.backgroundImagePath;
		m_authLogoPath = state.logoImagePath;
		m_authInfoPath = state.infoImagePath;
		setVisible(m_authBackgroundHwnd, false);
		setVisible(m_authLogoHwnd, false);
		setVisible(m_authInfoHwnd, false);
		setVisible(m_authTitleLine1Hwnd, !state.titleLine1.empty());
		setVisible(m_authTitleLine2Hwnd, !state.titleLine2.empty());
		setVisible(m_authSectionTitleHwnd, !state.sectionTitle.empty());
		setVisible(m_authPrimaryLabelHwnd, !state.primaryLabel.empty());
		setVisible(m_authPrimaryEditHwnd, !state.primaryLabel.empty());
		setVisible(m_authRememberCheckboxHwnd, state.showRemember && !state.primaryLabel.empty());
		setVisible(m_authPasswordLabelHwnd, state.showPassword);
		setVisible(m_authPasswordEditHwnd, state.showPassword);
		setVisible(m_authForgotButtonHwnd, state.showForgot);
		setVisible(m_authRegisterButtonHwnd, state.showRegister);
		setVisible(m_authBackButtonHwnd, false);
		setVisible(m_authSubmitButtonHwnd, !state.submitLabel.empty());
		setVisible(m_authQuitButtonHwnd, state.showQuit);
		UpdateAuthScreenLayout();

		if (state.focusPrimary && (GetFocus() == nullptr || GetFocus() == AsHwnd(m_hwnd)))
		{
			SetFocus(AsHwnd(m_authPrimaryEditHwnd));
		}
		else if (state.focusPassword && state.showPassword && (GetFocus() == nullptr || GetFocus() == AsHwnd(m_hwnd)))
		{
			SetFocus(AsHwnd(m_authPasswordEditHwnd));
		}
	}

	Window::AuthScreenCommand Window::ConsumeAuthScreenCommand()
	{
		const AuthScreenCommand cmd = m_pendingAuthCommand;
		m_pendingAuthCommand = AuthScreenCommand::None;
		return cmd;
	}

	std::string Window::GetAuthPrimaryValue() const
	{
		return m_authPrimaryValue;
	}

	std::string Window::GetAuthPasswordValue() const
	{
		return m_authPasswordValue;
	}

	bool Window::GetAuthRememberChecked() const
	{
		return m_authRememberChecked;
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

	void Window::UpdateAuthScreenLayout()
	{
		if (!m_hwnd || !m_authVisible)
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
		const int contentX = panelX + artW + 48;
		const int contentW = max(220, panelW - artW - 74);
		const int logoSize = max(64, min(76, clientW / 20));
		const int infoSize = 72;

		const bool hasBackgroundBitmap = ReplaceStaticBitmap(m_authBackgroundHwnd, m_authBackgroundBitmap, m_authBackgroundPath, m_authBackgroundWidth, m_authBackgroundHeight,
			m_authBackgroundPath, clientW, clientH);
		const bool hasLogoBitmap = ReplaceStaticBitmap(m_authLogoHwnd, m_authLogoBitmap, m_authLogoPath, m_authLogoWidth, m_authLogoHeight,
			m_authLogoPath, logoSize, logoSize);
		const bool hasInfoBitmap = ReplaceStaticBitmap(m_authInfoHwnd, m_authInfoBitmap, m_authInfoPath, m_authInfoWidth, m_authInfoHeight,
			m_authInfoPath, infoSize, infoSize);
		SetWindowPos(AsHwnd(m_authBackgroundHwnd), HWND_BOTTOM, 0, 0, clientW, clientH,
			SWP_NOACTIVATE | (hasBackgroundBitmap ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
		SetWindowPos(AsHwnd(m_authLogoHwnd), HWND_TOP, 20, 20, logoSize, logoSize,
			SWP_NOACTIVATE | (hasLogoBitmap ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
		SetWindowPos(AsHwnd(m_authInfoHwnd), HWND_TOP, panelX + panelW - infoSize - 22, panelY + 110, infoSize, infoSize,
			SWP_NOACTIVATE | (hasInfoBitmap ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
		SetWindowPos(AsHwnd(m_authTitleLine1Hwnd), HWND_TOP, panelX, panelY + 24, panelW, 34, SWP_NOACTIVATE | SWP_SHOWWINDOW);
		SetWindowPos(AsHwnd(m_authTitleLine2Hwnd), HWND_TOP, panelX, panelY + 60, panelW, 38, SWP_NOACTIVATE | SWP_SHOWWINDOW);
		SetWindowPos(AsHwnd(m_authSectionTitleHwnd), HWND_TOP, contentX, panelY + 126, contentW, 30, SWP_NOACTIVATE | SWP_SHOWWINDOW);
		SetWindowPos(AsHwnd(m_authPrimaryLabelHwnd), HWND_TOP, contentX, panelY + 184, contentW / 2, 24, SWP_NOACTIVATE | SWP_SHOWWINDOW);
		SetWindowPos(AsHwnd(m_authPrimaryEditHwnd), HWND_TOP, contentX, panelY + 212, contentW - 140, 34, SWP_NOACTIVATE | SWP_SHOWWINDOW);
		SetWindowPos(AsHwnd(m_authRememberCheckboxHwnd), HWND_TOP, contentX + contentW - 128, panelY + 214, 128, 28, SWP_NOACTIVATE | SWP_SHOWWINDOW);
		SetWindowPos(AsHwnd(m_authPasswordLabelHwnd), HWND_TOP, contentX, panelY + 264, contentW, 24, SWP_NOACTIVATE | SWP_SHOWWINDOW);
		SetWindowPos(AsHwnd(m_authPasswordEditHwnd), HWND_TOP, contentX, panelY + 292, contentW, 34, SWP_NOACTIVATE | SWP_SHOWWINDOW);
		SetWindowPos(AsHwnd(m_authForgotButtonHwnd), HWND_TOP, contentX, panelY + 338, contentW, 28, SWP_NOACTIVATE | SWP_SHOWWINDOW);
		SetWindowPos(AsHwnd(m_authRegisterButtonHwnd), HWND_TOP, contentX, panelY + 374, 132, 34, SWP_NOACTIVATE | SWP_SHOWWINDOW);
		SetWindowPos(AsHwnd(m_authSubmitButtonHwnd), HWND_TOP, contentX, panelY + panelH - 76, 160, 38, SWP_NOACTIVATE | SWP_SHOWWINDOW);
		SetWindowPos(AsHwnd(m_authQuitButtonHwnd), HWND_TOP, contentX + 174, panelY + panelH - 76, 160, 38, SWP_NOACTIVATE | SWP_SHOWWINDOW);
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
			UpdateAuthScreenLayout();
   			if (wparam != SIZE_MINIMIZED && m_onResize)
			{
				const int w = LOWORD(static_cast<LPARAM>(lparam));
				const int h = HIWORD(static_cast<LPARAM>(lparam));
				if (w > 0 && h > 0)
					m_onResize(w, h);
			}
			break;
		case WM_COMMAND:
		{
			const int controlId = LOWORD(static_cast<DWORD>(wparam));
			const int notifyCode = HIWORD(static_cast<DWORD>(wparam));
			if (notifyCode == EN_CHANGE && !m_authSyncInProgress)
			{
				wchar_t buffer[512]{};
				if (controlId == kAuthPrimaryEditId && m_authPrimaryEditHwnd)
				{
					GetWindowTextW(AsHwnd(m_authPrimaryEditHwnd), buffer, 512);
					m_authPrimaryValue = FromWide(buffer);
				}
				else if (controlId == kAuthPasswordEditId && m_authPasswordEditHwnd)
				{
					GetWindowTextW(AsHwnd(m_authPasswordEditHwnd), buffer, 512);
					m_authPasswordValue = FromWide(buffer);
				}
			}
			if (notifyCode == BN_CLICKED)
			{
				switch (controlId)
				{
				case kAuthRememberCheckboxId:
					m_authRememberChecked = SendMessageW(AsHwnd(m_authRememberCheckboxHwnd), BM_GETCHECK, 0, 0) == BST_CHECKED;
					break;
				case kAuthForgotButtonId:
					LOG_INFO(Core, "[AuthUi] Click bouton recuperation du mot de passe");
					m_pendingAuthCommand = AuthScreenCommand::OpenForgotPassword;
					break;
				case kAuthRegisterButtonId:
					LOG_INFO(Core, "[AuthUi] Click bouton inscription");
					m_pendingAuthCommand = AuthScreenCommand::OpenRegister;
					break;
				case kAuthBackButtonId:
					m_pendingAuthCommand = AuthScreenCommand::BackToLogin;
					break;
				case kAuthSubmitButtonId:
					LOG_INFO(Core, "[AuthUi] Click bouton validation");
					m_pendingAuthCommand = AuthScreenCommand::Submit;
					break;
				case kAuthQuitButtonId:
					LOG_INFO(Core, "[AuthUi] Click bouton quitter");
					m_pendingAuthCommand = AuthScreenCommand::Quit;
					break;
				default:
					break;
				}
			}
			break;
		}
		case WM_CTLCOLORSTATIC:
			if (reinterpret_cast<HWND>(lparam) == AsHwnd(m_overlayHwnd))
			{
				HDC hdc = reinterpret_cast<HDC>(wparam);
				SetTextColor(hdc, RGB(232, 237, 243));
				SetBkColor(hdc, RGB(27, 40, 54));
				static HBRUSH brush = CreateSolidBrush(RGB(27, 40, 54));
				return reinterpret_cast<intptr_t>(brush);
			}
			if (reinterpret_cast<HWND>(lparam) == AsHwnd(m_authTitleLine1Hwnd)
				|| reinterpret_cast<HWND>(lparam) == AsHwnd(m_authTitleLine2Hwnd)
				|| reinterpret_cast<HWND>(lparam) == AsHwnd(m_authSectionTitleHwnd)
				|| reinterpret_cast<HWND>(lparam) == AsHwnd(m_authPrimaryLabelHwnd)
				|| reinterpret_cast<HWND>(lparam) == AsHwnd(m_authPasswordLabelHwnd))
			{
				HDC hdc = reinterpret_cast<HDC>(wparam);
				SetTextColor(hdc, RGB(232, 237, 243));
				SetBkMode(hdc, TRANSPARENT);
				static HBRUSH brush = CreateSolidBrush(RGB(27, 40, 54));
				return reinterpret_cast<intptr_t>(brush);
			}
			break;
		case WM_CTLCOLORBTN:
			if (reinterpret_cast<HWND>(lparam) == AsHwnd(m_authRememberCheckboxHwnd))
			{
				HDC hdc = reinterpret_cast<HDC>(wparam);
				SetTextColor(hdc, RGB(232, 237, 243));
				SetBkColor(hdc, RGB(27, 40, 54));
				static HBRUSH brush = CreateSolidBrush(RGB(27, 40, 54));
				return reinterpret_cast<intptr_t>(brush);
			}
			break;
		case WM_CTLCOLOREDIT:
			if (reinterpret_cast<HWND>(lparam) == AsHwnd(m_authPrimaryEditHwnd)
				|| reinterpret_cast<HWND>(lparam) == AsHwnd(m_authPasswordEditHwnd))
			{
				HDC hdc = reinterpret_cast<HDC>(wparam);
				SetTextColor(hdc, RGB(232, 237, 243));
				SetBkColor(hdc, RGB(18, 28, 39));
				static HBRUSH brush = CreateSolidBrush(RGB(18, 28, 39));
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

