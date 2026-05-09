#include "src/world_editor/world/EditorCameraController.h"

#include "src/shared/platform/Input.h"

#include <cmath>

namespace engine::editor::world
{
	namespace
	{
		/// Constante pi en float, suffisamment précise pour des conversions
		/// degrés→radians dans une caméra (la double précision n'apporte rien).
		constexpr float kPi = 3.14159265358979323846f;

		/// Convertit un angle exprimé en degrés vers les radians attendus
		/// par les champs `yaw`/`pitch` de `engine::render::Camera`.
		float DegToRad(float deg)
		{
			return deg * kPi / 180.0f;
		}
	}

	/// Applique la config (vitesses, bornes). Ne modifie pas l'état caméra
	/// (position, focus, etc.). Idempotente : peut être rappelée à chaud
	/// si l'utilisateur change les valeurs dans `config.json`.
	void EditorCameraController::Configure(const EditorCameraConfig& cfg)
	{
		m_cfg = cfg;
	}

	/// Bascule le mode actif. Préserve `m_focusPoint`, `m_position`,
	/// `m_yawDeg`, `m_pitchDeg`, `m_orbitalDistance`, `m_topDownExtent` :
	/// le passage Numpad 1/3/7 ne doit jamais "perdre" la cible courante
	/// (cf. spec M100.4 critère d'acceptation "Le focus point est conservé").
	void EditorCameraController::SetMode(EditorCameraMode mode)
	{
		m_mode = mode;
	}

	/// Recentre la caméra sur \p target. Le mode actif n'est pas changé.
	/// En mode Orbital, la caméra orbitera autour du nouveau point dès la
	/// prochaine `BuildCamera`. En mode TopDown, la vue se recentrera sur
	/// le nouveau (X, Z) — la composante Y est ignorée par la projection
	/// ortho mais conservée pour un éventuel switch retour FPS/Orbital.
	void EditorCameraController::FocusOn(engine::math::Vec3 target)
	{
		m_focusPoint = target;
	}

	/// Construit la caméra de la frame courante selon `m_mode`.
	///
	/// Aucun effet de bord. Les trois modes partagent : nearZ=0.1, farZ=4000,
	/// fovYDeg=60 (perspective), aspect dérivé de viewport (fallback 16/9
	/// si \p viewportHeight <= 0).
	///
	/// FPS          : `cam.position = m_position`, yaw/pitch en radians.
	/// Orbital      : caméra placée à `focusPoint + dir(yaw, pitch) * dist`,
	///                `cam.lookAt = focusPoint`. Le caller (rendu) doit
	///                consommer `lookAt` plutôt que yaw/pitch en mode Orbital.
	/// TopDownOrtho : caméra à 1000m au-dessus du focus, `cam.ortho = true`,
	///                `cam.orthoExtent = m_topDownExtent`.
	engine::render::Camera EditorCameraController::BuildCamera(int viewportWidth, int viewportHeight) const
	{
		engine::render::Camera cam;
		const float aspect = (viewportHeight > 0)
			? static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight)
			: 16.0f / 9.0f;

		switch (m_mode)
		{
			case EditorCameraMode::FPS:
			{
				cam.position = m_position;
				cam.yaw = DegToRad(m_yawDeg);
				cam.pitch = DegToRad(m_pitchDeg);
				cam.fovYDeg = 60.0f;
				cam.aspect = aspect;
				cam.nearZ = 0.1f;
				cam.farZ = 4000.0f;
				cam.ortho = false;
				break;
			}
			case EditorCameraMode::Orbital:
			{
				// Position calculée à partir du focusPoint + distance + yaw/pitch.
				// Convention : yaw=0 → caméra sur l'axe +X, pitch>0 → au-dessus.
				const float yawRad = DegToRad(m_yawDeg);
				const float pitchRad = DegToRad(m_pitchDeg);
				const float cy = std::cos(yawRad);
				const float sy = std::sin(yawRad);
				const float cp = std::cos(pitchRad);
				const float sp = std::sin(pitchRad);
				cam.position = engine::math::Vec3{
					m_focusPoint.x + m_orbitalDistance * cy * cp,
					m_focusPoint.y + m_orbitalDistance * sp,
					m_focusPoint.z + m_orbitalDistance * sy * cp
				};
				cam.lookAt = m_focusPoint;
				cam.yaw = yawRad;
				cam.pitch = pitchRad;
				cam.fovYDeg = 60.0f;
				cam.aspect = aspect;
				cam.nearZ = 0.1f;
				cam.farZ = 4000.0f;
				cam.ortho = false;
				break;
			}
			case EditorCameraMode::TopDownOrtho:
			{
				// Caméra à 1000 m au-dessus du focusPoint, regardant vers le bas.
				cam.position = engine::math::Vec3{
					m_focusPoint.x,
					m_focusPoint.y + 1000.0f,
					m_focusPoint.z
				};
				cam.lookAt = m_focusPoint;
				cam.aspect = aspect;
				cam.ortho = true;
				cam.orthoExtent = m_topDownExtent;
				cam.nearZ = 0.1f;
				cam.farZ = 4000.0f;
				break;
			}
		}
		return cam;
	}

	/// Dispatche vers le handler spécifique au mode courant.
	/// Effet de bord : peut muter `m_position`, `m_yawDeg`, `m_pitchDeg`,
	/// `m_orbitalDistance`, `m_focusPoint`, `m_topDownExtent` selon le mode.
	void EditorCameraController::Update(engine::platform::Input& input, double dtSeconds)
	{
		switch (m_mode)
		{
			case EditorCameraMode::FPS:          UpdateFPS(input, dtSeconds); break;
			case EditorCameraMode::Orbital:      UpdateOrbital(input, dtSeconds); break;
			case EditorCameraMode::TopDownOrtho: UpdateTopDownOrtho(input, dtSeconds); break;
		}
	}

	/// Stub M100.4 : la logique fine (WASD horizontal, QE vertical, Shift×3,
	/// Ctrl×0.25, souris droite = mouselook yaw/pitch) sera implémentée
	/// dans un ticket de finition lorsque le pipeline de rendu offscreen
	/// (M100.34) sera disponible et que l'on pourra observer la caméra
	/// en action. Voir spec M100.4 §"Spécification fonctionnelle" lignes 30-35.
	void EditorCameraController::UpdateFPS(engine::platform::Input& /*input*/, double /*dt*/)
	{
		// Volontairement vide : implémentation différée (cf. doc ci-dessus).
	}

	/// Stub M100.4 : Alt+gauche = rotate (delta yaw/pitch),
	/// Alt+milieu = pan (`m_focusPoint`), molette = dolly (`m_orbitalDistance`
	/// dans [m_cfg.orbitalDistanceMin, m_cfg.orbitalDistanceMax]),
	/// F = focus sur sélection (déjà couvert par `FocusOn` qui est public).
	void EditorCameraController::UpdateOrbital(engine::platform::Input& /*input*/, double /*dt*/)
	{
		// Volontairement vide : implémentation différée (cf. doc ci-dessus).
	}

	/// Stub M100.4 : flèches ou drag milieu = pan XZ (mute `m_focusPoint.x`
	/// et `m_focusPoint.z`), molette = zoom (mute `m_topDownExtent` dans
	/// [m_cfg.topDownExtentMin, m_cfg.topDownExtentMax]).
	void EditorCameraController::UpdateTopDownOrtho(engine::platform::Input& /*input*/, double /*dt*/)
	{
		// Volontairement vide : implémentation différée (cf. doc ci-dessus).
	}
}
