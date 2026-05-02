#pragma once

#include "engine/math/Math.h"
#include <cstdint>

namespace engine::platform { class Input; }

namespace engine::render
{
	/// Camera data: position, orientation (yaw/pitch), and projection parameters.
	struct Camera
	{
		engine::math::Vec3 position{ 0.0f, 0.0f, 0.0f };
		float yaw = 0.0f;   // radians, rotation around world Y
		float pitch = 0.0f; // radians, rotation around local X (-pi/2 .. +pi/2)

		float fovYDeg = 70.0f;  // vertical FOV in degrees (e.g. 60-90)
		float aspect = 16.0f / 9.0f;
		float nearZ = 0.1f;
		float farZ = 1000.0f;

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

	/// Controleur camera 3eme personne orbital (post-EnterWorld).
	///
	/// Maintient un point cible (la position du joueur, future position d'un avatar
	/// 3D) et calcule la position camera en orbite arriere derriere ce point :
	///     camera.position = target - forwardDir(yaw, pitch) * distance
	///
	/// Inputs :
	///   * Clic droit + souris : rotation yaw/pitch autour de la cible.
	///   * Molette : zoom in/out (modifie la distance).
	///   * WASD/ZQSD : deplace le point cible dans le plan XZ selon l'orientation
	///     yaw courante. La camera suit automatiquement.
	///
	/// La position cible est conservee entre les frames (membre m_target). Engine
	/// peut la (re)setter explicitement via \ref SetTargetPosition (utilise au
	/// spawn EnterWorld pour aligner sur la position du personnage).
	class OrbitalCameraController
	{
	public:
		static constexpr float kWalkSpeed       = 5.0f;
		static constexpr float kRunSpeed        = 10.0f;
		static constexpr float kPitchMin        = -60.0f * 3.14159265f / 180.0f; ///< 3eme personne : pas de plongee verticale extreme.
		static constexpr float kPitchMax        = +75.0f * 3.14159265f / 180.0f;
		static constexpr float kDistanceMin     = 1.5f;   ///< Zoom le plus proche : juste derriere la nuque.
		static constexpr float kDistanceMax     = 20.0f;  ///< Zoom le plus eloigne.
		// 8 m par defaut : la valeur 5m precedente, combinee aux offsets epaule + hauteur,
		// rendait le perso difficile a voir (trop pres de la camera, parfois hors frustum).
		// 8m donne un cadrage MMORPG classique (le perso fait ~1/4 de hauteur ecran).
		static constexpr float kDistanceDefault = 8.0f;
		static constexpr float kZoomStep        = 1.0f;   ///< Increment molette.
		/// Hauteur d'epaule par rapport au sol (1.7 m ~ taille humaine adulte).
		static constexpr float kTargetEyeHeight = 1.7f;

		void SetTargetPosition(const engine::math::Vec3& worldPos);
		const engine::math::Vec3& GetTargetPosition() const { return m_target; }
		float GetDistance() const { return m_distance; }

		/// Etat de locomotion derive du dernier Update : 0=idle, 1=walk, 2=run.
		enum class LocomotionState : uint8_t { Idle = 0, Walk = 1, Run = 2 };
		LocomotionState GetLocomotionState() const { return m_locomotion; }
		/// Phase d'animation de marche en radians, monotone croissante. Permet a
		/// l'avatar visuel d'osciller verticalement (placeholder bob) pour suggerer
		/// un mouvement avant de cabler de vraies animations.
		float GetWalkBobPhaseRad() const { return m_walkBobPhase; }

		/// Update logique a appeler chaque frame in-game. Met a jour m_target / yaw /
		/// pitch / m_distance selon l'input, puis ecrit camera.position et
		/// camera.yaw/pitch pour le rendu.
		///
		/// \p groundYAtTarget : hauteur du sol (en metres monde) sous la position
		/// cible courante, calculee par le caller via TerrainRenderer::SampleHeightAtWorldXZ.
		/// Sert a clamper la camera au-dessus du terrain quand l'utilisateur
		/// regarde fortement vers le bas. Passer 0 pour un sol plat (sans terrain).
		///
		/// \p speedMultiplier : facteur multiplicatif applique a la vitesse de
		/// marche/course/accroupi. Permet au caller de combiner :
		///   * un modificateur de race (ex. elfes 1.10x, nains 0.85x)
		///   * un modificateur de terrain (ex. sable 0.65x, neige 0.70x, herbe 1.0x)
		///   * un modificateur d'etat (buff sprint, debuff slow...)
		/// 1.0f = vitesse de base nominale (kWalkSpeed/kRunSpeed). Borne [0.05, 5.0].
		void Update(engine::platform::Input& input, double dt, float mouseSensitivityRadPerPixel, bool invertY,
			MovementLayout layout, bool applyMouseLook, bool applyKeyboardMove, Camera& camera,
			float groundYAtTarget = 0.0f, float speedMultiplier = 1.0f);

	private:
		engine::math::Vec3 m_target{ 0.0f, kTargetEyeHeight, 0.0f };
		float              m_distance = kDistanceDefault;
		bool               m_initialized = false;
		LocomotionState    m_locomotion = LocomotionState::Idle;
		float              m_walkBobPhase = 0.0f;
		// Saut : vitesse verticale (m/s) et offset Y (m) au-dessus du sol. A 0
		// quand le perso est au sol. Space declenche un saut quand grounded.
		float              m_verticalVelocityY = 0.0f;
		float              m_verticalOffsetY   = 0.0f;
		// Accroupi : maintenir Control. Reduit la hauteur cible et la vitesse
		// horizontale.
		bool               m_isCrouching = false;
	};
}
