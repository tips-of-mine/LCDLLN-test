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
		std::fill(m_mousePressed.begin(), m_mousePressed.end(), false);
		std::fill(m_mouseReleased.begin(), m_mouseReleased.end(), false);
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
			m_mouseX = x;
			m_mouseY = y;
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
		case WM_LBUTTONDOWN:
			if (!m_mouseDown[static_cast<size_t>(MouseButton::Left)])
			{
				m_mousePressed[static_cast<size_t>(MouseButton::Left)] = true;
			}
			m_mouseDown[static_cast<size_t>(MouseButton::Left)] = true;
			break;
		case WM_LBUTTONUP:
			if (m_mouseDown[static_cast<size_t>(MouseButton::Left)])
			{
				m_mouseReleased[static_cast<size_t>(MouseButton::Left)] = true;
			}
			m_mouseDown[static_cast<size_t>(MouseButton::Left)] = false;
			break;
		case WM_RBUTTONDOWN:
			if (!m_mouseDown[static_cast<size_t>(MouseButton::Right)])
			{
				m_mousePressed[static_cast<size_t>(MouseButton::Right)] = true;
			}
			m_mouseDown[static_cast<size_t>(MouseButton::Right)] = true;
			break;
		case WM_RBUTTONUP:
			if (m_mouseDown[static_cast<size_t>(MouseButton::Right)])
			{
				m_mouseReleased[static_cast<size_t>(MouseButton::Right)] = true;
			}
			m_mouseDown[static_cast<size_t>(MouseButton::Right)] = false;
			break;
		case WM_MBUTTONDOWN:
			if (!m_mouseDown[static_cast<size_t>(MouseButton::Middle)])
			{
				m_mousePressed[static_cast<size_t>(MouseButton::Middle)] = true;
			}
			m_mouseDown[static_cast<size_t>(MouseButton::Middle)] = true;
			break;
		case WM_MBUTTONUP:
			if (m_mouseDown[static_cast<size_t>(MouseButton::Middle)])
			{
				m_mouseReleased[static_cast<size_t>(MouseButton::Middle)] = true;
			}
			m_mouseDown[static_cast<size_t>(MouseButton::Middle)] = false;
			break;
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

	bool Input::IsMouseDown(MouseButton button) const
	{
		return m_mouseDown[static_cast<size_t>(button)];
	}

	bool Input::WasMousePressed(MouseButton button) const
	{
		return m_mousePressed[static_cast<size_t>(button)];
	}

	bool Input::WasMouseReleased(MouseButton button) const
	{
		return m_mouseReleased[static_cast<size_t>(button)];
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

