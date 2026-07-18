#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace engine::platform
{
	/// Key codes used by the input system (matches Win32 VK_* codes for the subset we use).
	enum class Key : uint16_t
	{
		Digit1 = '1',
		Digit2 = '2',
		Digit3 = '3',
		Digit4 = '4',
		Digit5 = '5',
		Digit6 = '6',
		Digit7 = '7',
		Digit8 = '8',
		Digit9 = '9',
		Digit0 = '0',
		C = 'C',
		B = 'B',
		M = 'M',
		N = 'N',
		O = 'O',
		P = 'P',
		Q = 'Q',
		V = 'V',
		G = 'G',
		H = 'H',
		I = 'I',
		L = 'L',
		R = 'R',
		F = 'F',
		W = 'W',
		A = 'A',
		S = 'S',
		D = 'D',
		E = 'E',
		U = 'U',
		X = 'X',
		Y = 'Y',
		Z = 'Z',
		// Combat SP2 — touches d'attaque (T) et de panneau combat avancé (J).
		// Valeurs = codes ASCII majuscules (== VK_* Windows), comme le reste.
		T = 'T',
		J = 'J',
		// Métiers SP1 — panneau d'artisanat (K).
		K = 'K',
		Escape = 0x1B,
		Tab = 0x09,
		Control = 0x11,
		Alt = 0x12, ///< VK_MENU (gauche ou droite) — capturé via WM_SYSKEYDOWN. Sprint (CHAR-MODEL).
		Space = 0x20,
		Enter = 0x0D,
		Backspace = 0x08,
		/// VK_DELETE — éditeur monde lot 5 (2026-07-18) : supprimer la sélection.
		Delete = 0x2E,
		/// US keyboard OEM_2 ('/'); used as chat focus toggle (M29.1).
		Slash = 0xBF,
		Shift = 0x10,
		Left = 0x25,
		Up = 0x26,
		Right = 0x27,
		Down = 0x28,
		PageUp = 0x21,
		PageDown = 0x22,
		// Win32 VK_* codes for function keys. F11 = bascule plein écran ; F1-F10 et
		// F12 servent à ouvrir les panneaux in-game (remappables via controls.keybind.*).
		F_1  = 0x70u,
		F_2  = 0x71u,
		F_3  = 0x72u,
		F_4  = 0x73u,
		F_5  = 0x74u,
		F_6  = 0x75u,
		F_7  = 0x76u,
		F_8  = 0x77u,
		F_9  = 0x78u,
		F_10 = 0x79u,
		F_11 = 0x7Au,
		F_12 = 0x7Bu
	};

	/// Mouse buttons used by the editor/runtime input system.
	enum class MouseButton : uint8_t
	{
		Left = 0,
		Right = 1,
		Middle = 2
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

		/// Returns true while the mouse button is held.
		bool IsMouseDown(MouseButton button) const;

		/// Returns true only on the frame the mouse button transitions up->down.
		bool WasMousePressed(MouseButton button) const;

		/// Returns true only on the frame the mouse button transitions down->up.
		bool WasMouseReleased(MouseButton button) const;

		/// Mouse delta since last `BeginFrame()` (pixels).
		int MouseDeltaX() const { return m_mouseDx; }
		int MouseDeltaY() const { return m_mouseDy; }
		/// Current mouse X position in client coordinates.
		int MouseX() const { return m_mouseX; }
		/// Current mouse Y position in client coordinates.
		int MouseY() const { return m_mouseY; }
		/// Mouse wheel scroll delta since last `BeginFrame()` (positive = scroll up/zoom out).
		int MouseScrollDelta() const { return m_scrollDelta; }

		/// Move pending UTF-8 text (from WM_CHAR) into \p out and clear the internal buffer.
		void ConsumePendingTextUtf8(std::string& out);

		/// Capture/release cursor for mouse-look style control.
		void SetCursorCaptured(bool captured);
		bool IsCursorCaptured() const { return m_cursorCaptured; }

	private:
		static constexpr size_t kKeyMax = 256;
		std::array<bool, kKeyMax> m_down{};
		std::array<bool, kKeyMax> m_pressed{};
		std::array<bool, kKeyMax> m_released{};
		static constexpr size_t kMouseButtonCount = 3;
		std::array<bool, kMouseButtonCount> m_mouseDown{};
		std::array<bool, kMouseButtonCount> m_mousePressed{};
		std::array<bool, kMouseButtonCount> m_mouseReleased{};

		bool m_cursorCaptured = false;
		bool m_haveLastMouse = false;
		int m_lastMouseX = 0;
		int m_lastMouseY = 0;
		int m_mouseDx = 0;
		int m_mouseDy = 0;
		int m_mouseX = 0;
		int m_mouseY = 0;
		int m_scrollDelta = 0;

		/// Pending UTF-8 text collected between WM_CHAR events (Windows).
		std::string m_pendingTextUtf8;
	};
}

