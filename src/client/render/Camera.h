#pragma once

#include "src/shared/math/Math.h"
#include <cstdint>

namespace engine::platform { class Input; }

namespace engine::render
{
	/// Camera data: position, orientation (yaw/pitch), and projection parameters.
	///
	/// Les champs `lookAt`, `ortho` et `orthoExtent` ont été ajoutés en M100.4
	/// pour supporter les modes caméra de l'éditeur monde (Orbital +
	/// TopDownOrtho). Ils sont ignorés par les controllers FPS/Orbital
	/// gameplay existants : `lookAt` n'est consommé que si un controller le
	/// renseigne explicitement (ex. `EditorCameraController` en mode Orbital
	/// ou TopDown), et `ortho==false` par défaut conserve le comportement
	/// perspective historique.
	struct Camera
	{
		engine::math::Vec3 position{ 0.0f, 0.0f, 0.0f };
		float yaw = 0.0f;   // radians, rotation around world Y
		float pitch = 0.0f; // radians, rotation around local X (-pi/2 .. +pi/2)

		float fovYDeg = 70.0f;  // vertical FOV in degrees (e.g. 60-90)
		float aspect = 16.0f / 9.0f;
		float nearZ = 0.1f;
		float farZ = 1000.0f;

		/// Point regardé par la caméra (mode Orbital / TopDown éditeur).
		/// Ignoré quand `ortho==false` ET que le controller utilise yaw/pitch
		/// (mode FPS standard). Sert à `EditorCameraController::BuildCamera`
		/// qui le remplit en mode Orbital/TopDown ; le rendu dérive alors
		/// la matrice de vue via lookAt au lieu de yaw/pitch.
		engine::math::Vec3 lookAt{ 0.0f, 0.0f, 0.0f };

		/// True si la projection est orthographique (mode TopDown éditeur).
		/// Quand true, `fovYDeg` est ignoré et `orthoExtent` définit la
		/// demi-hauteur du frustum vertical en mètres.
		bool ortho = false;

		/// Demi-hauteur (en mètres) du frustum ortho. Largeur dérivée via
		/// `aspect`. Ignoré si `ortho==false`.
		float orthoExtent = 50.0f;

		/// Builds view matrix (world -> camera space, right-handed, camera looks along -Z).
		engine::math::Mat4 ComputeViewMatrix() const;

		/// Builds perspective projection for Vulkan (Y down NDC, Z in [0,1]).
		engine::math::Mat4 ComputeProjectionMatrix() const;
	};

	/// FPS-style camera controller: WASD movement and mouse look.
	enum class MovementLayout : uint8_t
	{
		WASD = 0,
		ZQSD = 1
	};

	class FpsCameraController
	{
	public:
		/// Walk speed in m/s.
		static constexpr float kWalkSpeed = 5.0f;
		/// Run speed in m/s (when shift held).
		static constexpr float kRunSpeed = 10.0f;
		/// Pitch clamp in radians (avoid gimbal at ±90°).
		static constexpr float kPitchMin = -89.0f * 3.14159265f / 180.0f;
		static constexpr float kPitchMax = +89.0f * 3.14159265f / 180.0f;

		/// Updates camera from input and delta time. Mouse sensitivity in rad/pixel from config.
		/// Si \p scrollWheelAdjustsFov : la molette modifie le FOV vertical (éditeur monde).
		/// \p applyMouseLook / \p applyKeyboardMove permettent de laisser la vue réagir à la souris
		/// tout en bloquant WASD quand ImGui capte le clavier (champs texte), etc.
		/// Si \p worldEditorTerrainWorldSizeM > 0 (éditeur monde), la vitesse de marche/course est multipliée
		/// selon la taille du terrain (grandes zones navigables plus vite, plafonné).
		/// \p extraSpeedMultiplier (défaut 1.0) : multiplicateur supplémentaire utilisateur (slider UI éditeur),
		/// appliqué APRÈS la mise à l'échelle terrain. Clamp [0.05, 50.0] pour éviter une vitesse nulle ou folle.
		void Update(engine::platform::Input& input, double dt, float mouseSensitivityRadPerPixel, bool invertY,
			MovementLayout layout, bool scrollWheelAdjustsFov, bool applyMouseLook, bool applyKeyboardMove,
			float worldEditorTerrainWorldSizeM, Camera& camera, float extraSpeedMultiplier = 1.0f);
	};

	/// Controleur camera 3eme personne orbital (post-EnterWorld) -- CAMERA PURE.
	///
	/// Maintient un point cible (la position du joueur) et calcule la position
	/// camera en orbite arriere derriere ce point :
	///     camera.position = target - forwardDir(yaw, pitch) * distance
	///
	/// Inputs traites en interne :
	///   * Clic droit + souris : rotation yaw/pitch autour de la cible.
	///   * Molette : zoom in/out (modifie la distance).
	///
	/// Le mouvement de la cible (WASD/ZQSD, saut, accroupi, collision sol) n'est
	/// plus gere ici : c'est la responsabilite de CharacterController, qui
	/// pousse la position resultante via \ref SetTargetPosition chaque frame.
	/// La camera expose GetForwardXZ/GetRightXZ pour que l'orchestrateur projette
	/// l'input clavier dans le repere camera courant.
	class OrbitalCameraController
	{
	public:
		static constexpr float kPitchMin        = -60.0f * 3.14159265f / 180.0f; ///< 3eme personne : pas de plongee verticale extreme.
		static constexpr float kPitchMax        = +75.0f * 3.14159265f / 180.0f;
		static constexpr float kDistanceMin     = 1.0f;
		static constexpr float kDistanceMax     = 20.0f;
		// 3 m par defaut : compromis entre vue rapprochee (perso domine 43% ecran)
		// et visibilite complete (corps entier de la tete aux pieds).
		// Reglable via molette de 1m a 20m.
		static constexpr float kDistanceDefault = 3.0f;
		static constexpr float kZoomStep        = 1.0f;   ///< Increment molette.

		/// Repositionne la cible orbitale (point regarde par la camera) en monde.
		/// Marque aussi le controller comme initialise (cf. m_initialized).
		void SetTargetPosition(const engine::math::Vec3& worldPos);
		/// Renvoie la position cible courante (point regarde par la camera).
		const engine::math::Vec3& GetTargetPosition() const { return m_target; }
		/// Distance camera-cible courante (modifiee par la molette de souris).
		float GetDistance() const { return m_distance; }

		/// Renvoie le vecteur "avant" camera projete sur le plan XZ (Y=0).
		/// Utilise par Engine pour transformer l'input clavier (W/S) en direction
		/// monde, dans le repere camera courant.
		engine::math::Vec3 GetForwardXZ() const;
		/// Renvoie le vecteur "droite" camera projete sur le plan XZ (Y=0).
		/// Utilise par Engine pour transformer l'input clavier (A/D) en direction
		/// monde, dans le repere camera courant.
		engine::math::Vec3 GetRightXZ() const;
		/// Yaw camera en radians (utile pour debug et orientation coherente entre
		/// camera et avatar/character controller).
		float GetYawRad() const;

		/// Update logique a appeler chaque frame in-game. Met uniquement a jour la
		/// camera (yaw/pitch via souris, distance via molette) et derive
		/// camera.position depuis m_target. Le mouvement de m_target est desormais
		/// pilote a l'exterieur par CharacterController (via SetTargetPosition).
		///
		/// \p applyMouseLook : false coupe yaw/pitch (ex. quand le clic droit
		/// est requis et absent, ou qu'ImGui capte la souris).
		void Update(engine::platform::Input& input, double dt, float mouseSensitivityRadPerPixel,
		            bool invertY, bool applyMouseLook, Camera& camera);

	private:
		engine::math::Vec3 m_target{ 0.0f, 0.0f, 0.0f };
		float              m_distance = kDistanceDefault;
		bool               m_initialized = false;
		/// Copie du dernier yaw camera, utilisee par les getters Forward/Right/Yaw.
		/// Mise a jour en fin de Update.
		float              m_lastYaw = 0.0f;
	};
}
