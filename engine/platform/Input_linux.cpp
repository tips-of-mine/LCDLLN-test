#if !defined(_WIN32)

#include "engine/platform/Input.h"

#include <algorithm>

namespace engine::platform
{
	void Input::BeginFrame()
	{
		std::fill(m_pressed.begin(), m_pressed.end(), false);
		std::fill(m_released.begin(), m_released.end(), false);
		m_mouseDx = 0;
		m_mouseDy = 0;
		m_scrollDelta = 0;
	}

	void Input::HandleMessage(uint32_t, uint64_t, int64_t)
	{
		// No native messages on stub backend.
	}

	bool Input::IsDown(Key) const
	{
		return false;
	}

	bool Input::WasPressed(Key) const
	{
		return false;
	}

	bool Input::WasReleased(Key) const
	{
		return false;
	}

	bool Input::IsMouseDown(MouseButton) const
	{
		return false;
	}

	bool Input::WasMousePressed(MouseButton) const
	{
		return false;
	}

	bool Input::WasMouseReleased(MouseButton) const
	{
		return false;
	}

	void Input::SetCursorCaptured(bool)
	{
		m_cursorCaptured = false;
		m_haveLastMouse = false;
		m_mouseDx = 0;
		m_mouseDy = 0;
	}
}

#endif

