#pragma once

#include <array>
#include <cstdint>
#include <cstddef>

namespace engine::platform
{
	/// Key codes used by the input system (matches Win32 VK_* codes for the subset we use).
	enum class Key : uint16_t
	{
		W = 'W',
		A = 'A',
		S = 'S',
		D = 'D',
		Escape = 0x1B,
		Space = 0x20,
		Shift = 0x10
	};

	/// Per-frame input state: key states and mouse delta.
	class Input final
	{
	public:
		Input() = default;

		/// Call once per frame to reset per-frame transitions and mouse delta.
		void BeginFrame();

		/// Process a Win32 message (used by Window hook) to update key/mouse states.
		void HandleMessage(uint32_t msg, uint64_t wparam, int64_t lparam);

		/// Returns true while the key is held.
		bool IsDown(Key k) const;

		/// Returns true only on the frame the key transitions up->down.
		bool WasPressed(Key k) const;

		/// Returns true only on the frame the key transitions down->up.
		bool WasReleased(Key k) const;

		/// Mouse delta since last `BeginFrame()` (pixels).
		int MouseDeltaX() const { return m_mouseDx; }
		int MouseDeltaY() const { return m_mouseDy; }

		/// Capture/release cursor for mouse-look style control.
		void SetCursorCaptured(bool captured);
		bool IsCursorCaptured() const { return m_cursorCaptured; }

	private:
		static constexpr size_t kKeyMax = 256;
		std::array<bool, kKeyMax> m_down{};
		std::array<bool, kKeyMax> m_pressed{};
		std::array<bool, kKeyMax> m_released{};

		bool m_cursorCaptured = false;
		bool m_haveLastMouse = false;
		int m_lastMouseX = 0;
		int m_lastMouseY = 0;
		int m_mouseDx = 0;
		int m_mouseDy = 0;
	};
}

