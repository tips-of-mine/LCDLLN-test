#if !defined(_WIN32)

#include "engine/platform/Window.h"

#include "engine/core/Log.h"

namespace engine::platform
{
	bool Window::Create(const CreateDesc&)
	{
		// Stub: pretend the window is created successfully.
		m_hwnd = nullptr;
		m_shouldClose = false;
		return true;
	}

	void Window::Destroy()
	{
	}

	void Window::PollEvents()
	{
	}

	bool Window::ShouldClose() const
	{
		return m_shouldClose;
	}

	void Window::RequestClose()
	{
		m_shouldClose = true;
	}

	void Window::GetClientSize(int& outWidth, int& outHeight) const
	{
		outWidth = 1280;
		outHeight = 720;
	}

	void Window::SetTitle(std::string_view)
	{
	}

	bool Window::OpenExternalUrl(std::string_view url) const
	{
		LOG_WARN(Platform, "[Window] OpenExternalUrl unsupported on this platform (url={})", url);
		return false;
	}

	void Window::SetOverlayText(std::string_view)
	{
	}

	void Window::SetAuthScreenState(const AuthScreenState&)
	{
	}

	Window::AuthScreenCommand Window::ConsumeAuthScreenCommand()
	{
		return AuthScreenCommand::None;
	}

	std::string Window::GetAuthPrimaryValue() const
	{
		return {};
	}

	std::string Window::GetAuthPasswordValue() const
	{
		return {};
	}

	bool Window::GetAuthRememberChecked() const
	{
		return false;
	}

	void Window::ToggleFullscreen()
	{
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

	void Window::SetAuthImageBytesLoader(std::function<std::vector<uint8_t>(std::string_view)>)
	{
	}

	intptr_t Window::HandleMessage(uint32_t, uint64_t, int64_t)
	{
		// No native messages on stub backend.
		return 0;
	}
}

#endif

