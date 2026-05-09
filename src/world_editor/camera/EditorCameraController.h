#pragma once

#include "src/shared/math/Math.h"
#include "src/client/render/Camera.h"

#include <cstdint>

namespace engine::platform { class Input; }

namespace engine::editor::world
{
	/// Mode de navigation actif dans le panneau Scene de l'éditeur monde
	/// (M100.4). Trois modes mutuellement exclusifs :
	///   - FPS          : caméra perspective, déplacement WASD + mouselook.
	///   - Orbital      : caméra perspective, rotation Maya-style autour
	///                    d'un focusPoint, molette = dolly.
	///   - TopDownOrtho : caméra orthographique, vue de dessus (axes XZ),
	///                    pan avec flèches/drag milieu, molette = zoom (extent).
	///
	/// Le numérique de l'enum est figé (sérialisé dans `editor.world.camera.lastMode`).
	enum class EditorCameraMode : uint8_t
	{
		FPS = 0,
		Orbital = 1,
		TopDownOrtho = 2
	};

	/// Bornes et vitesses des trois modes caméra. Initialisée depuis
	/// `config.json` (clés `editor.world.camera.*`) via `EditorCameraController::Configure`.
	struct EditorCameraConfig
	{
		float fpsSpeedMps = 8.0f;          ///< Vitesse de déplacement FPS (m/s).
		float orbitalDistanceMin = 0.5f;   ///< Distance minimale caméra<->focus en mode Orbital (m).
		float orbitalDistanceMax = 5000.0f;///< Distance maximale caméra<->focus en mode Orbital (m).
		float topDownExtentMin = 5.0f;     ///< Extent ortho minimal en mode TopDown (m).
		float topDownExtentMax = 5000.0f;  ///< Extent ortho maximal en mode TopDown (m).
	};

	/// Contrôleur caméra du panneau Scene de l'éditeur monde (M100.4).
	///
	/// Détient l'état caméra (position, yaw/pitch, focus, distance orbitale,
	/// extent ortho) et offre :
	///   - `BuildCamera(w, h)` qui produit une `engine::render::Camera`
	///     consommable par le pipeline de rendu offscreen (M100.34).
	///   - `Update(input, dt)` qui consomme les inputs (souris/clavier)
	///     et fait évoluer l'état selon le mode actif.
	///   - `SetMode` qui commute le mode SANS détruire le focusPoint
	///     (préservation du POI lors d'un Numpad 1/3/7).
	///
	/// Contraintes thread/timing : main thread uniquement. `BuildCamera`
	/// peut être appelée plusieurs fois par frame (idempotente sur l'état).
	/// `Update` doit être appelée une seule fois par frame (consomme le
	/// delta souris).
	class EditorCameraController
	{
	public:
		/// Applique la config (vitesses, bornes). Appelée typiquement au
		/// boot de WorldEditorShell après lecture de `config.json`.
		void Configure(const EditorCameraConfig& cfg);

		/// Bascule le mode caméra. Le focusPoint est préservé : un
		/// passage FPS → Orbital ne réinitialise pas la cible.
		void SetMode(EditorCameraMode mode);

		/// Mode courant.
		EditorCameraMode GetMode() const { return m_mode; }

		/// Construit la `engine::render::Camera` pour la frame courante en
		/// fonction du mode. Aucun effet de bord (const). Doit être appelée
		/// une fois par frame depuis `ScenePanel::Render`. \p viewportWidth
		/// et \p viewportHeight servent à calculer l'aspect ratio (fallback
		/// 16/9 si height invalide).
		engine::render::Camera BuildCamera(int viewportWidth, int viewportHeight) const;

		/// Consomme les inputs souris/clavier pour la frame et fait évoluer
		/// l'état (position, yaw/pitch, distance, extent) selon le mode actif.
		/// Effet de bord : mute les membres de l'état caméra. Doit être appelée
		/// une fois par frame, après `engine::platform::Input::BeginFrame`.
		/// \param dtSeconds Delta temps depuis la frame précédente (s).
		void Update(engine::platform::Input& input, double dtSeconds);

		/// Recentre la caméra sur \p target. Le mode actif n'est pas changé.
		/// Effet de bord : `m_focusPoint` ← target. En mode Orbital la caméra
		/// orbitera autour du nouveau point dès la prochaine `BuildCamera`.
		void FocusOn(engine::math::Vec3 target);

		/// Point d'intérêt courant (utilisé en Orbital comme pivot, en
		/// TopDown comme centre de la vue).
		engine::math::Vec3 GetFocusPoint() const { return m_focusPoint; }

	private:
		/// Update spécifique mode FPS. Stub M100.4 : sera étoffé en fonction
		/// de la spec §"Spécification fonctionnelle" (WASD, QE, Shift/Ctrl,
		/// souris droite = look). Aucun effet pour l'instant.
		void UpdateFPS(engine::platform::Input& input, double dt);

		/// Update spécifique mode Orbital. Stub M100.4 : sera étoffé pour
		/// gérer Alt+drag (Maya-style), molette = dolly, F = focus sélection.
		void UpdateOrbital(engine::platform::Input& input, double dt);

		/// Update spécifique mode TopDown ortho. Stub M100.4 : sera étoffé
		/// pour gérer flèches/drag milieu = pan XZ, molette = zoom extent.
		void UpdateTopDownOrtho(engine::platform::Input& input, double dt);

		EditorCameraMode m_mode = EditorCameraMode::FPS;
		engine::math::Vec3 m_position{ 0.0f, 5.0f, 10.0f };
		engine::math::Vec3 m_focusPoint{ 0.0f, 0.0f, 0.0f };
		float m_yawDeg = 0.0f;
		float m_pitchDeg = -15.0f;
		float m_orbitalDistance = 10.0f;
		float m_topDownExtent = 50.0f;
		EditorCameraConfig m_cfg;
	};
}
