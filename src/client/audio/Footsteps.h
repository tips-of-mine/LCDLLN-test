#pragma once

// SND.2 — Pas du personnage : QUAND jouer un pas (cadence) et QUEL son (surface).
//
// Module PUR (aucune dépendance audio ni moteur) : testé par footsteps_tests.
// Le branchement (position, contrôleur de personnage, carte des sols) est dans
// Engine (EngineAudio.cpp).

#include <cstdint>
#include <string_view>

namespace engine::audio
{
	/// Évènement de pas produit par la cadence.
	enum class FootstepEvent : uint8_t
	{
		None = 0,
		Step,    ///< un pied touche le sol
		Land,    ///< réception après une chute (≥ kMinAirTime)
		Stroke,  ///< mouvement de nage
	};

	/// Cadence des pas : un pas chaque fois que le personnage a parcouru une
	/// foulée ; la foulée s'allonge avec la vitesse (marche ≈ 0,75 m, course
	/// ≈ 1,3 m, sprint ≈ 1,6 m). Une réception est signalée après au moins
	/// kMinAirTime secondes en l'air ; à la nage, une brasse toutes les
	/// kStrokeSeconds tant que le personnage avance.
	class FootstepCadence
	{
	public:
		static constexpr float kMinAirTime = 0.35f;     ///< s en l'air avant qu'une réception s'entende
		static constexpr float kMinSpeed = 0.4f;        ///< m/s sous lesquels on ne marche pas
		static constexpr float kStrokeSeconds = 1.1f;   ///< s entre deux brasses

		/// Longueur d'une foulée (m) à la vitesse horizontale `speed` (m/s).
		static float StrideLength(float speed);

		/// Avance d'une frame. `dt` (s), `speed` vitesse horizontale (m/s).
		/// \return l'évènement à jouer cette frame (au plus un).
		FootstepEvent Update(float dt, float speed, bool grounded, bool swimming);

		/// Remet à zéro (téléportation, changement de personnage).
		void Reset();

	private:
		float m_distance = 0.0f;  ///< distance parcourue depuis le dernier pas (m)
		float m_airTime = 0.0f;   ///< temps passé en l'air (s)
		float m_strokeTimer = 0.0f;
		bool m_wasGrounded = true;
	};

	/// Couche dominante de la carte des sols du terrain (splat R/G/B/A).
	enum class GroundLayer : int8_t
	{
		Unknown = -1,
		Grass = 0,
		Dirt = 1,
		Rock = 2,
		Snow = 3,
	};

	/// Contexte du pied au moment du pas.
	struct FootstepContext
	{
		GroundLayer layer = GroundLayer::Unknown;
		bool swimming = false;
		bool inShallowWater = false;   ///< pied dans l'eau sans nager
		float heightAboveTerrain = 0.0f; ///< m : > 0,3 = plancher (bâtiment, pont)
	};

	/// Identifiant du son de pas (ids de game/data/gameplay/surface_table.json :
	/// step_grass, step_dirt, step_rock, step_snow_crunch, step_water,
	/// swim_stroke, step_wood…). Sol inconnu : step_dirt.
	std::string_view FootstepSoundId(const FootstepContext& ctx);

	/// Volume relatif d'un pas selon la vitesse (marche plus discrète que la course).
	float FootstepVolume(float speed);
}
