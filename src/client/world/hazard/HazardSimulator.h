#pragma once

// M100.16 — Simulateur de hazard (machine à états PURE, client only).
//
// Aucune dépendance rendu/réseau : prend des entrées (dt, appui action,
// déplacement latéral, possession d'objet) et produit un état (enfoncement,
// évasion, mort) + des sorties consommables par le CharacterController et la
// caméra (offset sol, ralentissement). Testable headless.

#include <string>

#include "src/client/world/hazard/HazardVolumes.h"

namespace engine::world::hazard
{
	enum class HazardState : uint8_t { Idle = 0, Sinking = 1, Escaped = 2, Dead = 3 };

	struct HazardInput
	{
		float dtSeconds = 0.0f;
		bool  actionPressed = false;     ///< Un appui "Action" cette frame.
		float lateralDeltaMeters = 0.0f; ///< Déplacement horizontal cette frame.
		bool  hasRequiredItem = false;   ///< Possède l'objet requis (Tar).
	};

	struct HazardOutput
	{
		HazardState state = HazardState::Idle;
		float currentDepth = 0.0f;       ///< Profondeur d'enfoncement (m).
		float groundOffsetMeters = 0.0f; ///< Compensation tête (= currentDepth).
		float slowdownMul = 1.0f;        ///< Multiplicateur de vitesse appliqué.
		bool  justEscaped = false;       ///< Vrai la frame de l'évasion.
		std::string deathReason;         ///< Renseigné si state == Dead.
	};

	/// Simulateur d'un joueur dans un hazard. Un seul hazard actif à la fois.
	class HazardSimulator
	{
	public:
		/// Le joueur entre dans le volume : démarre l'enfoncement (ou le timer
		/// lave). Réinitialise l'état interne.
		void Enter(const HazardVolume& volume);

		/// Le joueur quitte le volume avant d'être bloqué (sortie volontaire).
		/// No-op si déjà Dead/Escaped.
		void Exit();

		/// Avance la simulation d'une frame et renvoie l'état/sorties.
		HazardOutput Update(const HazardInput& in);

		HazardState GetState() const { return m_state; }
		float GetCurrentDepth() const { return m_depth; }

		static constexpr float kMashWindowSeconds = 5.0f;
		static constexpr int   kMashRequiredPresses = 10;
		static constexpr float kLateralEscapeMeters = 2.0f;
		static constexpr float kLavaDeathSeconds = 3.0f;

	private:
		HazardVolume m_volume{};
		HazardState  m_state = HazardState::Idle;
		float m_depth = 0.0f;
		int   m_mashCount = 0;
		float m_mashWindowElapsed = 0.0f;
		float m_lateralAccum = 0.0f;
		float m_lavaTimer = 0.0f;
	};
}
