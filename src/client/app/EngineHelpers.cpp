// Engine — définitions des fonctions utilitaires internes (cf. EngineHelpers.h).
#include "src/client/app/EngineHelpers.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <string_view>

namespace engine::helpers
{
	namespace
	{
		// Touches remappables (nom config <-> enum platform::Key). Limitee aux
		// Key reellement definies dans l'enum. Sert au panneau Options (affichage
		// + capture du rebind) et a la lecture des binds gameplay depuis
		// controls.keybind.* (sprint / crouch / cast / grimoire / skilltree).
		struct RebindableKey { engine::platform::Key key; const char* name; };
		const RebindableKey kRebindableKeys[] = {
			{engine::platform::Key::A,"A"},{engine::platform::Key::B,"B"},{engine::platform::Key::C,"C"},
			{engine::platform::Key::D,"D"},{engine::platform::Key::E,"E"},{engine::platform::Key::F,"F"},
			{engine::platform::Key::G,"G"},{engine::platform::Key::H,"H"},{engine::platform::Key::I,"I"},
			{engine::platform::Key::L,"L"},
			{engine::platform::Key::M,"M"},{engine::platform::Key::N,"N"},{engine::platform::Key::O,"O"},
			{engine::platform::Key::P,"P"},{engine::platform::Key::Q,"Q"},{engine::platform::Key::R,"R"},
			{engine::platform::Key::S,"S"},{engine::platform::Key::U,"U"},{engine::platform::Key::V,"V"},
			{engine::platform::Key::W,"W"},{engine::platform::Key::X,"X"},{engine::platform::Key::Y,"Y"},
			{engine::platform::Key::Z,"Z"},
			{engine::platform::Key::Digit0,"0"},{engine::platform::Key::Digit1,"1"},{engine::platform::Key::Digit2,"2"},
			{engine::platform::Key::Digit3,"3"},{engine::platform::Key::Digit4,"4"},{engine::platform::Key::Digit5,"5"},
			{engine::platform::Key::Digit6,"6"},{engine::platform::Key::Digit7,"7"},{engine::platform::Key::Digit8,"8"},
			{engine::platform::Key::Digit9,"9"},
			{engine::platform::Key::Control,"Ctrl"},{engine::platform::Key::Alt,"Alt"},
			{engine::platform::Key::Shift,"Shift"},{engine::platform::Key::Space,"Espace"},
			{engine::platform::Key::Tab,"Tab"},
			{engine::platform::Key::F_1,"F1"},{engine::platform::Key::F_2,"F2"},{engine::platform::Key::F_3,"F3"},
			{engine::platform::Key::F_4,"F4"},{engine::platform::Key::F_5,"F5"},{engine::platform::Key::F_6,"F6"},
			{engine::platform::Key::F_7,"F7"},{engine::platform::Key::F_8,"F8"},{engine::platform::Key::F_9,"F9"},
			{engine::platform::Key::F_10,"F10"},{engine::platform::Key::F_11,"F11"},{engine::platform::Key::F_12,"F12"},
		};
	}

	float EngineNowSec()
	{
		static const auto kStart = std::chrono::steady_clock::now();
		return std::chrono::duration<float>(std::chrono::steady_clock::now() - kStart).count();
	}

	const char* KeyName(engine::platform::Key k)
	{
		for (const auto& e : kRebindableKeys)
			if (e.key == k) return e.name;
		return "?";
	}

	engine::platform::Key KeyFromName(const std::string& name, engine::platform::Key fallback)
	{
		for (const auto& e : kRebindableKeys)
			if (name == e.name) return e.key;
		return fallback;
	}

	std::string KeyGlyph(engine::platform::Key k)
	{
	#if defined(_WIN32)
		const UINT vk = static_cast<UINT>(k); // l'enum Key == codes VK_* Win32
		const UINT ch = ::MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR) & 0x7FFFu;
		if (ch != 0)
		{
			const wchar_t w = static_cast<wchar_t>(ch);
			char utf8[8] = {0};
			const int n = ::WideCharToMultiByte(CP_UTF8, 0, &w, 1, utf8, sizeof(utf8) - 1, nullptr, nullptr);
			if (n > 0)
			{
				return std::string(utf8, static_cast<size_t>(n));
			}
		}
	#endif
		return KeyName(k);
	}

	[[maybe_unused]] bool WorldToScreenPx(const float vp[16], float wx, float wy, float wz,
		int vw, int vh, float& sx, float& sy)
	{
		const float cx = vp[0]*wx + vp[4]*wy + vp[8]*wz + vp[12];
		const float cy = vp[1]*wx + vp[5]*wy + vp[9]*wz + vp[13];
		const float cz = vp[2]*wx + vp[6]*wy + vp[10]*wz + vp[14];
		const float cw = vp[3]*wx + vp[7]*wy + vp[11]*wz + vp[15];
		if (cw <= 1e-5f) return false;
		const float invW = 1.0f / cw;
		const float ndcX = cx * invW, ndcY = cy * invW, ndcZ = cz * invW;
		if (ndcZ < 0.0f || ndcZ > 1.0f) return false;
		sx = (ndcX * 0.5f + 0.5f) * static_cast<float>(vw);
		sy = (ndcY * 0.5f + 0.5f) * static_cast<float>(vh);
		return true;
	}

	engine::gameplay::MoveInput BuildMoveInput(
		const engine::platform::Input& input,
		const engine::render::OrbitalCameraController& camera,
		engine::render::MovementLayout layout,
		engine::platform::Key sprintKey,
		engine::platform::Key crouchKey)
	{
		engine::gameplay::MoveInput out{};

		const engine::math::Vec3 forward = camera.GetForwardXZ();
		const engine::math::Vec3 right   = camera.GetRightXZ();

		const engine::platform::Key forwardKey =
			(layout == engine::render::MovementLayout::ZQSD) ? engine::platform::Key::Z : engine::platform::Key::W;
		const engine::platform::Key leftKey =
			(layout == engine::render::MovementLayout::ZQSD) ? engine::platform::Key::Q : engine::platform::Key::A;
		const engine::platform::Key backKey  = engine::platform::Key::S;
		const engine::platform::Key rightKey = engine::platform::Key::D;

		engine::math::Vec3 dir{ 0.0f, 0.0f, 0.0f };
		if (input.IsDown(forwardKey))
		{
			dir.x += forward.x;
			dir.z += forward.z;
		}
		if (input.IsDown(backKey))
		{
			dir.x -= forward.x;
			dir.z -= forward.z;
		}
		if (input.IsDown(rightKey))
		{
			dir.x += right.x;
			dir.z += right.z;
		}
		if (input.IsDown(leftKey))
		{
			dir.x -= right.x;
			dir.z -= right.z;
		}

		const float lenSq = dir.x * dir.x + dir.z * dir.z;
		if (lenSq > 0.0f)
		{
			const float invLen = 1.0f / std::sqrt(lenSq);
			out.moveDirXZ = engine::math::Vec3{ dir.x * invLen, 0.0f, dir.z * invLen };
		}

		out.run         = input.IsDown(engine::platform::Key::Shift);
		out.sprint      = input.IsDown(sprintKey);
		out.crouch      = input.IsDown(crouchKey);
		out.jumpPressed = input.WasPressed(engine::platform::Key::Space);
		// Nage : contrôle vertical. Espace = remonter, touche Crouch = descendre.
		// Le CharacterController ne les consomme qu'en mode Water (cf. QueryWater) ;
		// hors eau, Espace reste le saut et Crouch l'accroupi.
		out.swimUpPressed   = input.IsDown(engine::platform::Key::Space);
		out.swimDownPressed = input.IsDown(crouchKey);
		return out;
	}

	bool IsPureBackInput(const engine::platform::Input& input)
	{
		const bool back = input.IsDown(engine::platform::Key::S);
		if (!back) return false;
		const bool anyForward = input.IsDown(engine::platform::Key::W) || input.IsDown(engine::platform::Key::Z);
		const bool anyLeft    = input.IsDown(engine::platform::Key::A) || input.IsDown(engine::platform::Key::Q);
		const bool anyRight   = input.IsDown(engine::platform::Key::D);
		return !anyForward && !anyLeft && !anyRight;
	}

	const char* StateToClipName(engine::Engine::AvatarLocomotionState s)
	{
		switch (s) {
			case engine::Engine::AvatarLocomotionState::Idle:         return "Idle";
			case engine::Engine::AvatarLocomotionState::StartWalking: return "StartWalking";
			case engine::Engine::AvatarLocomotionState::Walk:         return "Walk";
			case engine::Engine::AvatarLocomotionState::WalkBack:     return "WalkBack";
			case engine::Engine::AvatarLocomotionState::Run:          return "Run";
			case engine::Engine::AvatarLocomotionState::Sprint:       return "Sprint";
			case engine::Engine::AvatarLocomotionState::CrouchIdle:   return "CrouchIdle";
			case engine::Engine::AvatarLocomotionState::CrouchWalk:   return "CrouchWalk";
			case engine::Engine::AvatarLocomotionState::Roll:         return "Roll";
			case engine::Engine::AvatarLocomotionState::Emote:        return "Emote";
			case engine::Engine::AvatarLocomotionState::Attack:       return "Attack";
			case engine::Engine::AvatarLocomotionState::Cast:         return "Cast";
			case engine::Engine::AvatarLocomotionState::Interact:     return "Interact";
			case engine::Engine::AvatarLocomotionState::Punch:        return "Punch";
			case engine::Engine::AvatarLocomotionState::Jump:         return "Jump";
			case engine::Engine::AvatarLocomotionState::Fall:         return "Fall";
			case engine::Engine::AvatarLocomotionState::Land:         return "Land";
			case engine::Engine::AvatarLocomotionState::SwimIdle:     return "SwimIdle";
			case engine::Engine::AvatarLocomotionState::SwimForward:  return "SwimForward";
		}
		return "Idle";
	}

	bool ClipLoops(engine::Engine::AvatarLocomotionState s)
	{
		return s == engine::Engine::AvatarLocomotionState::Idle
			|| s == engine::Engine::AvatarLocomotionState::Walk
			|| s == engine::Engine::AvatarLocomotionState::WalkBack
			|| s == engine::Engine::AvatarLocomotionState::Run
			|| s == engine::Engine::AvatarLocomotionState::Sprint
			|| s == engine::Engine::AvatarLocomotionState::CrouchIdle
			|| s == engine::Engine::AvatarLocomotionState::CrouchWalk
			|| s == engine::Engine::AvatarLocomotionState::Emote
			|| s == engine::Engine::AvatarLocomotionState::Fall
			|| s == engine::Engine::AvatarLocomotionState::SwimIdle
			|| s == engine::Engine::AvatarLocomotionState::SwimForward;
	}

	bool HasCliFlag(int argc, char** argv, std::string_view flag)
	{
		for (int i = 1; i < argc; ++i)
		{
			if (argv[i] && std::string_view(argv[i]) == flag)
			{
				return true;
			}
		}
		return false;
	}
} // namespace engine::helpers
