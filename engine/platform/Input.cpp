#include "engine/platform/Input.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cctype>

namespace engine::platform
{
	void Input::BeginFrame()
	{
		std::fill(m_pressed.begin(), m_pressed.end(), false);
		std::fill(m_released.begin(), m_released.end(), false);
		m_mouseDx = 0;
		m_mouseDy = 0;
	}

	void Input::HandleMessage(uint32_t msg, uint64_t wparam, int64_t lparam)
	{
		switch (msg)
		{
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		{
			const uint32_t vk = static_cast<uint32_t>(wparam) & 0xFFu;
			if (!m_down[vk])
			{
				m_pressed[vk] = true;
			}
			m_down[vk] = true;
			break;
		}
		case WM_KEYUP:
		case WM_SYSKEYUP:
		{
			const uint32_t vk = static_cast<uint32_t>(wparam) & 0xFFu;
			if (m_down[vk])
			{
				m_released[vk] = true;
			}
			m_down[vk] = false;
			break;
		}
		case WM_MOUSEMOVE:
		{
			const int x = GET_X_LPARAM(static_cast<LPARAM>(lparam));
			const int y = GET_Y_LPARAM(static_cast<LPARAM>(lparam));
			if (m_haveLastMouse)
			{
				m_mouseDx += (x - m_lastMouseX);
				m_mouseDy += (y - m_lastMouseY);
			}
			m_lastMouseX = x;
			m_lastMouseY = y;
			m_haveLastMouse = true;
			break;
		}
		default:
			break;
		}
	}

	bool Input::IsDown(Key k) const
	{
		return m_down[static_cast<size_t>(static_cast<uint16_t>(k) & 0xFFu)];
	}

	bool Input::WasPressed(Key k) const
	{
		return m_pressed[static_cast<size_t>(static_cast<uint16_t>(k) & 0xFFu)];
	}

	bool Input::WasReleased(Key k) const
	{
		return m_released[static_cast<size_t>(static_cast<uint16_t>(k) & 0xFFu)];
	}

	void Input::SetCursorCaptured(bool captured)
	{
		m_cursorCaptured = captured;

		ShowCursor(captured ? FALSE : TRUE);

		if (captured)
		{
			POINT p{};
			GetCursorPos(&p);
			m_lastMouseX = p.x;
			m_lastMouseY = p.y;
			m_haveLastMouse = true;
			SetCapture(GetForegroundWindow());
		}
		else
		{
			ReleaseCapture();
			m_haveLastMouse = false;
		}
	}
}

