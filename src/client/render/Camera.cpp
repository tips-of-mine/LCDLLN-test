#include "src/client/render/Camera.h"
#include "src/shared/platform/Input.h"
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
		// PR26.5 (M??.?) : right vector via convention standard cross(forward, world_up)
		// au lieu de cross(world_up, forward). L'ancien calcul donnait un right
		// inverse (-X au lieu de +X pour yaw=0), ce qui se traduisait par les
		// touches Q et D produisant un mouvement world inverse (D allait a
		// gauche, Q allait a droite). Confirme par l'utilisateur 2026-05-06.
		// Le up calcule via cross(right, forward) ci-dessous donne LE MEME
		// vecteur up qu'avant (cross(right_new, forward) = -cross(forward, right_new)
		// = -cross(forward, -right_old) = cross(forward, right_old) = up_old),
		// donc le rendu Y reste inchange. Seul le mouvement horizontal Q/D est
		// corrige.
		engine::math::Vec3 right(-forward.z, 0.0f, forward.x);
		float rlen = right.Length();
		if (rlen > 0.0f) right = right * (1.0f / rlen);
		else right = engine::math::Vec3(1.0f, 0.0f, 0.0f);
		// Up = cross(right, forward) (convention RH standard du triedre).
		engine::math::Vec3 up(right.y * forward.z - right.z * forward.y,
			right.z * forward.x - right.x * forward.z,
			right.x * forward.y - right.y * forward.x);
		float ulen = up.Length();
		if (ulen > 0.0f) up = up * (1.0f / ulen);
		else up = engine::math::Vec3(0.0f, 1.0f, 0.0f);

		engine::math::Mat4 V;
		// Vue Vulkan left-handed (+Z forward dans le view-space, ce qu'attend
		// `Mat4::PerspectiveVulkan` qui pose row3 = (0,0,1,0) -> clip.w = view.z).
		// Stockage column-major : m[col*4+row]. Lignes du view matrix :
		//   row 0 = (right.x, right.y, right.z, -dot(right, pos))
		//   row 1 = (up.x,    up.y,    up.z,    -dot(up,    pos))
		//   row 2 = (fwd.x,   fwd.y,   fwd.z,   -dot(fwd,   pos))
		//   row 3 = (0, 0, 0, 1)
		// Bug pre-existant (PR #427 diag) : l'ancien code stockait les basis
		// vectors comme COLONNES (matrice camera->world au lieu de world->camera),
		// avec en plus sign-flip sur m[10] et m[14]. Resultat : viewProj.row3
		// recevait des valeurs proches de viewProj.row2, donc clip.w sortait
		// negatif pour TOUS les points -> rasterizer Vulkan rejetait tout.
		V.m[0] = right.x;   V.m[1] = up.x;     V.m[2]  = forward.x; V.m[3]  = 0.0f;
		V.m[4] = right.y;   V.m[5] = up.y;     V.m[6]  = forward.y; V.m[7]  = 0.0f;
		V.m[8] = right.z;   V.m[9] = up.z;     V.m[10] = forward.z; V.m[11] = 0.0f;
		V.m[12] = -(right.x   * position.x + right.y   * position.y + right.z   * position.z);
		V.m[13] = -(up.x      * position.x + up.y      * position.y + up.z      * position.z);
		V.m[14] = -(forward.x * position.x + forward.y * position.y + forward.z * position.z);
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
			// PR25 (M??.?) : ajout d'un controle vertical world-space pour la
			// camera free-fly de l'editeur monde. R = monter, F = descendre.
			// Le pas vertical est strictement aligne sur l'axe Y world (pas de
			// composante pitch), pour que l'utilisateur garde la meme cible
			// horizontale en prenant de l'alttitude (cas typique : repositionner
			// la camera au-dessus du terrain pour vue d'ensemble). Touches
			// choisies pour rester en main gauche cote ZQSD/WASD : R/F adjacents
			// sur AZERTY et QWERTY, et n'entrent pas en conflit avec d'eventuels
			// raccourcis editeur Ctrl-X. Les touches s'appliquent uniquement
			// quand applyKeyboardMove est vrai (i.e. mode editeur, pas pendant
			// que la souris est capturee par un panneau ImGui).
			if (input.IsDown(engine::platform::Key::R))
			{
				camera.position.y += dist;
			}
			if (input.IsDown(engine::platform::Key::F))
			{
				camera.position.y -= dist;
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
		bool invertY, bool applyMouseLook, Camera& camera)
	{
		(void)dt; // Plus de logique d'integration temporelle ici (mouvement delegue a CharacterController).

		// Premier passage : on s'assure que m_initialized passe a true, meme si
		// SetTargetPosition n'a pas encore ete appele (cas de bring-up / tests).
		// La logique de spawn EnterWorld appelle SetTargetPosition explicitement.
		if (!m_initialized)
		{
			m_initialized = true;
		}

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

		// Position camera = cible - dir(yaw, pitch) * distance. La camera regarde
		// le point cible (ex. la "tete" du joueur, place par CharacterController
		// a 1.7 m au-dessus du sol). On garde la meme convention de forward que
		// ComputeViewMatrix (forward = -view dir).
		const float cy = std::cos(camera.yaw);
		const float sy = std::sin(camera.yaw);
		const float cp = std::cos(camera.pitch);
		const float sp = std::sin(camera.pitch);
		const float forwardX = -sy * cp;
		const float forwardY = -sp;
		const float forwardZ = -cy * cp;

		// Offset vertical "over the head" : la camera se place ~1 m au-dessus du
		// point cible apres recul, pour que l'avatar n'occupe pas tout l'ecran.
		// La collision camera-decor (clamp au sol) n'est plus geree ici : elle
		// reviendra au CharacterController / un futur module collision camera.
		constexpr float kHeightOffsetM = 1.0f;
		camera.position.x = m_target.x - forwardX * m_distance;
		camera.position.y = m_target.y - forwardY * m_distance + kHeightOffsetM;
		camera.position.z = m_target.z - forwardZ * m_distance;

		// Memorise le yaw courant pour que GetForwardXZ/GetRightXZ/GetYawRad
		// renvoient des valeurs coherentes avec le rendu de la frame.
		m_lastYaw = camera.yaw;
	}

	engine::math::Vec3 OrbitalCameraController::GetForwardXZ() const
	{
		// Convention identique au calcul de forward dans Update :
		// forward = (-sin(yaw)*cos(pitch), -sin(pitch), -cos(yaw)*cos(pitch)).
		// Projete sur XZ (pitch=0), on renvoie (-sin(yaw), 0, -cos(yaw)). C'est
		// exactement la direction utilisee par l'ancien WASD pour la touche W
		// (avant : forwardX = -sy ; forwardZ = -cy), donc CharacterController peut
		// reutiliser ce vecteur pour deplacer le joueur dans la meme direction.
		return engine::math::Vec3{ -std::sin(m_lastYaw), 0.0f, -std::cos(m_lastYaw) };
	}

	engine::math::Vec3 OrbitalCameraController::GetRightXZ() const
	{
		// Right XZ coherent avec l'ancien WASD (avant : rightX = cy ; rightZ = -sy).
		// Equivaut a cross(forward, world_up) projete sur XZ.
		return engine::math::Vec3{ std::cos(m_lastYaw), 0.0f, -std::sin(m_lastYaw) };
	}

	float OrbitalCameraController::GetYawRad() const
	{
		return m_lastYaw;
	}
}
