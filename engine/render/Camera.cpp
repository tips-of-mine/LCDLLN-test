#include "engine/render/Camera.h"
#include "engine/platform/Input.h"
#include <algorithm>
#include <cmath>

namespace engine::render
{
	namespace
	{
		constexpr float kPi = 3.14159265f;
	}

	engine::math::Mat4 Camera::ComputeViewMatrix() const
	{
		const float cy = std::cos(yaw);
		const float sy = std::sin(yaw);
		const float cp = std::cos(pitch);
		const float sp = std::sin(pitch);
		// Forward in world (camera looks along -Z in camera space, so forward = -view direction).
		const engine::math::Vec3 forward(-sy * cp, -sp, -cy * cp);
		// Right = cross(world up, forward); world up = (0, 1, 0).
		engine::math::Vec3 right(forward.z, 0.0f, -forward.x);
		float rlen = right.Length();
		if (rlen > 0.0f) right = right * (1.0f / rlen);
		else right = engine::math::Vec3(1.0f, 0.0f, 0.0f);
		// Up = cross(forward, right).
		engine::math::Vec3 up(forward.y * right.z - forward.z * right.y,
			forward.z * right.x - forward.x * right.z,
			forward.x * right.y - forward.y * right.x);
		float ulen = up.Length();
		if (ulen > 0.0f) up = up * (1.0f / ulen);
		else up = engine::math::Vec3(0.0f, 1.0f, 0.0f);

		engine::math::Mat4 V;
		V.m[0] = right.x;  V.m[1] = right.y;  V.m[2] = right.z;  V.m[3] = 0.0f;
		V.m[4] = up.x;     V.m[5] = up.y;     V.m[6] = up.z;     V.m[7] = 0.0f;
		V.m[8] = -forward.x; V.m[9] = -forward.y; V.m[10] = -forward.z; V.m[11] = 0.0f;
		V.m[12] = -(right.x * position.x + right.y * position.y + right.z * position.z);
		V.m[13] = -(up.x * position.x + up.y * position.y + up.z * position.z);
		V.m[14] = (forward.x * position.x + forward.y * position.y + forward.z * position.z);
		V.m[15] = 1.0f;
		return V;
	}

	engine::math::Mat4 Camera::ComputeProjectionMatrix() const
	{
		const float fovYRad = fovYDeg * kPi / 180.0f;
		return engine::math::Mat4::PerspectiveVulkan(fovYRad, aspect, nearZ, farZ);
	}

	void FpsCameraController::Update(engine::platform::Input& input, double dt, float mouseSensitivityRadPerPixel, bool invertY,
		MovementLayout layout, bool scrollWheelAdjustsFov, bool applyMouseLook, bool applyKeyboardMove,
		float worldEditorTerrainWorldSizeM, Camera& camera, float extraSpeedMultiplier)
	{
		extraSpeedMultiplier = std::clamp(extraSpeedMultiplier, 0.05f, 50.0f);
		if (applyMouseLook)
		{
			const float sens = static_cast<float>(mouseSensitivityRadPerPixel);
			camera.yaw += static_cast<float>(input.MouseDeltaX()) * sens;
			const float pitchSign = invertY ? -1.0f : 1.0f;
			camera.pitch += static_cast<float>(input.MouseDeltaY()) * sens * pitchSign;
			if (camera.pitch < kPitchMin) camera.pitch = kPitchMin;
			if (camera.pitch > kPitchMax) camera.pitch = kPitchMax;
		}

		if (applyKeyboardMove)
		{
			float speed = input.IsDown(engine::platform::Key::Shift) ? kRunSpeed : kWalkSpeed;
			if (worldEditorTerrainWorldSizeM > 1.f)
			{
				constexpr float kRefM = 1024.f;
				const float scale = std::sqrt(worldEditorTerrainWorldSizeM / kRefM);
				speed *= std::clamp(scale, 1.f, 12.f);
			}
			speed *= extraSpeedMultiplier;
			const float dist = static_cast<float>(dt) * speed;
			const float cy = std::cos(camera.yaw);
			const float sy = std::sin(camera.yaw);
			const float cp = std::cos(camera.pitch);
			// Move in camera horizontal plane (yaw only for forward/back/left/right).
			const float forwardX = -sy * cp;
			const float forwardZ = -cy * cp;
			const float rightX = cy;
			const float rightZ = -sy;
			const engine::platform::Key forwardKey =
				(layout == MovementLayout::ZQSD) ? engine::platform::Key::Z : engine::platform::Key::W;
			const engine::platform::Key backwardKey = engine::platform::Key::S;
			const engine::platform::Key rightKey = engine::platform::Key::D;
			const engine::platform::Key leftKey =
				(layout == MovementLayout::ZQSD) ? engine::platform::Key::Q : engine::platform::Key::A;
			const bool forwardDown = input.IsDown(forwardKey);
			const bool backDown = input.IsDown(backwardKey);
			const bool rightDown = input.IsDown(rightKey);
			const bool leftDown = input.IsDown(leftKey);
			if (forwardDown)
			{
				camera.position.x += forwardX * dist;
				camera.position.z += forwardZ * dist;
			}
			if (backDown)
			{
				camera.position.x -= forwardX * dist;
				camera.position.z -= forwardZ * dist;
			}
			if (rightDown)
			{
				camera.position.x += rightX * dist;
				camera.position.z += rightZ * dist;
			}
			if (leftDown)
			{
				camera.position.x -= rightX * dist;
				camera.position.z -= rightZ * dist;
			}
		}

		if (scrollWheelAdjustsFov && applyMouseLook)
		{
			const int scroll = input.MouseScrollDelta();
			if (scroll != 0)
			{
				camera.fovYDeg -= static_cast<float>(scroll) * 2.0f;
				if (camera.fovYDeg < 25.0f)
				{
					camera.fovYDeg = 25.0f;
				}
				if (camera.fovYDeg > 110.0f)
				{
					camera.fovYDeg = 110.0f;
				}
			}
		}
	}

	void OrbitalCameraController::SetTargetPosition(const engine::math::Vec3& worldPos)
	{
		m_target = worldPos;
		m_initialized = true;
	}

	void OrbitalCameraController::Update(engine::platform::Input& input, double dt, float mouseSensitivityRadPerPixel,
		bool invertY, MovementLayout layout, bool applyMouseLook, bool applyKeyboardMove, Camera& camera)
	{
		// Clic droit maintenu + souris -> rotation orbite (yaw/pitch).
		if (applyMouseLook)
		{
			const float sens = static_cast<float>(mouseSensitivityRadPerPixel);
			camera.yaw += static_cast<float>(input.MouseDeltaX()) * sens;
			const float pitchSign = invertY ? -1.0f : 1.0f;
			camera.pitch += static_cast<float>(input.MouseDeltaY()) * sens * pitchSign;
			if (camera.pitch < kPitchMin) camera.pitch = kPitchMin;
			if (camera.pitch > kPitchMax) camera.pitch = kPitchMax;
		}

		// Molette -> zoom in/out (modifie distance camera-cible).
		const int scroll = input.MouseScrollDelta();
		if (scroll != 0)
		{
			m_distance -= static_cast<float>(scroll) * kZoomStep;
			if (m_distance < kDistanceMin) m_distance = kDistanceMin;
			if (m_distance > kDistanceMax) m_distance = kDistanceMax;
		}

		// WASD/ZQSD -> deplace le point cible dans le plan XZ selon yaw courant.
		// Etape 5 : derive aussi un etat de locomotion (Idle/Walk/Run) et fait
		// avancer une phase de bob pour le placeholder anim de l'avatar.
		bool moving = false;
		bool running = false;
		if (applyKeyboardMove)
		{
			running = input.IsDown(engine::platform::Key::Shift);
			const float speed = running ? kRunSpeed : kWalkSpeed;
			const float dist = static_cast<float>(dt) * speed;
			const float cy = std::cos(camera.yaw);
			const float sy = std::sin(camera.yaw);
			// Direction avant horizontale (yaw seul, pitch ignore pour le mouvement
			// au sol -- l'avatar marche sur l'axe horizontal).
			const float forwardX = -sy;
			const float forwardZ = -cy;
			const float rightX = cy;
			const float rightZ = -sy;
			const engine::platform::Key forwardKey =
				(layout == MovementLayout::ZQSD) ? engine::platform::Key::Z : engine::platform::Key::W;
			const engine::platform::Key backwardKey = engine::platform::Key::S;
			const engine::platform::Key rightKey = engine::platform::Key::D;
			const engine::platform::Key leftKey =
				(layout == MovementLayout::ZQSD) ? engine::platform::Key::Q : engine::platform::Key::A;
			if (input.IsDown(forwardKey))
			{
				m_target.x += forwardX * dist;
				m_target.z += forwardZ * dist;
				moving = true;
			}
			if (input.IsDown(backwardKey))
			{
				m_target.x -= forwardX * dist;
				m_target.z -= forwardZ * dist;
				moving = true;
			}
			if (input.IsDown(rightKey))
			{
				m_target.x += rightX * dist;
				m_target.z += rightZ * dist;
				moving = true;
			}
			if (input.IsDown(leftKey))
			{
				m_target.x -= rightX * dist;
				m_target.z -= rightZ * dist;
				moving = true;
			}
		}
		// Etat de locomotion : Idle quand pas de touche, Walk normal, Run avec Shift.
		if (!moving)
			m_locomotion = LocomotionState::Idle;
		else
			m_locomotion = running ? LocomotionState::Run : LocomotionState::Walk;
		// Phase d'oscillation : avance proportionnellement a la vitesse pour que
		// le bob aille plus vite en Run qu'en Walk. 8 cycles/seconde en run, 5 en walk.
		if (moving)
		{
			constexpr float kPi2 = 6.2831853f;
			const float bobFreqHz = running ? 8.0f : 5.0f;
			m_walkBobPhase += static_cast<float>(dt) * bobFreqHz * kPi2;
			if (m_walkBobPhase > kPi2 * 1024.f) m_walkBobPhase = std::fmod(m_walkBobPhase, kPi2);
		}

		// Position camera = cible - dir(yaw, pitch) * distance. La camera regarde
		// le point cible (la "tete" du joueur a kTargetEyeHeight au-dessus du sol).
		const float cy = std::cos(camera.yaw);
		const float sy = std::sin(camera.yaw);
		const float cp = std::cos(camera.pitch);
		const float sp = std::sin(camera.pitch);
		// Forward du regard de la camera (cf. ComputeViewMatrix : forward = -view dir).
		const float forwardX = -sy * cp;
		const float forwardY = -sp;
		const float forwardZ = -cy * cp;

		// Etape 3 collision camera-decor : si la camera calculee va passer SOUS le
		// sol (Y < kGroundY + kGroundPadding), on reduit la distance effective
		// pour que la camera reste au-dessus du sol au lieu de la teleporter
		// verticalement (ce qui donnerait un saut visuel desagreable). On laisse
		// kDistanceMin comme plancher (la camera ne peut pas etre plus pres que
		// kDistanceMin de la cible).
		// TODO : remplacer kGroundY (constante = 0) par une vraie query de hauteur
		// terrain quand TerrainRenderer exposera SampleHeightAtWorldXZ.
		constexpr float kGroundY = 0.0f;
		constexpr float kGroundPadding = 0.5f;
		// Point de visee de la camera : on regarde la mi-corps de l'avatar plutot
		// que ses pieds. Sans cet offset, l'avatar (1.8 m de haut, pivot pieds)
		// occupait la moitie superieure de l'ecran et passait au-dessus du cadrage
		// au moindre zoom -> le perso disparaissait visuellement.
		const float lookAtY = m_target.y + kCameraLookAtUpOffsetM;
		float effectiveDistance = m_distance;
		if (forwardY < -0.001f)
		{
			// camera_y = lookAtY - forwardY * distance ; on veut camera_y >= floor.
			// distance_max = (lookAtY - floor) / -forwardY.
			const float floorY = kGroundY + kGroundPadding;
			const float distMaxBelowFloor = (lookAtY - floorY) / -forwardY;
			if (distMaxBelowFloor > 0.f && distMaxBelowFloor < effectiveDistance)
			{
				if (distMaxBelowFloor >= kDistanceMin)
				{
					effectiveDistance = distMaxBelowFloor;
				}
				else
				{
					effectiveDistance = kDistanceMin;
				}
			}
		}

		// Position camera : recule dans la direction OPPOSEE du forward, depuis le
		// point de visee (mi-corps de l'avatar).
		camera.position.x = m_target.x - forwardX * effectiveDistance;
		camera.position.y = lookAtY      - forwardY * effectiveDistance;
		camera.position.z = m_target.z - forwardZ * effectiveDistance;
		// Clamp final en Y : meme avec effectiveDistance reduit, on s'assure que
		// la camera ne descende pas sous le seuil (cas ou m_target.y < floor par ex.).
		if (camera.position.y < kGroundY + kGroundPadding)
		{
			camera.position.y = kGroundY + kGroundPadding;
		}
	}
}
