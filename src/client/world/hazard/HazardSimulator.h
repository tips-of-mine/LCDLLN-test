// src/client/world/hazard/HazardSimulator.h
#pragma once

#include "src/client/world/hazard/HazardVolumes.h"
#include "src/shared/math/Math.h"

#include <functional>
#include <string_view>

namespace engine::world::hazard
{
	/// Callbacks injectés pour découpler le simulator des systèmes inventaire,
	/// animation, audio et mort scriptée. Tous nullables : un callback null
	/// est traité comme un no-op (`hasItem` null → `false`).
	struct HazardCallbacks
	{
		std::function<bool(uint32_t itemId)> hasItem;          // inventaire local
		std::function<void()> onEnter;                          // anim + audio enter
		std::function<void()> onExit;                           // anim + audio exit
		std::function<void(std::string_view reason)> die;       // mort scriptée
	};

	/// État courant du simulator (lecture seule pour debug HUD ou tests).
	struct HazardState
	{
		bool inHazard = false;
		const HazardInstance* activeHazard = nullptr;
		float currentDepth = 0.0f;        // mètres enfoncés sous la surface
		float lateralTraveled = 0.0f;     // mètres horizontal cumulés (LateralMove)
		int   mashCount = 0;              // appuis "Action" dans la fenêtre
		float mashWindowSec = 0.0f;       // âge de la fenêtre mash (s)
		float lavaTimer = 0.0f;           // secondes dans LavaSurface
	};

	/// Effet à appliquer au CharacterController chaque frame.
	/// `applySinkRate=true` → CC force `vel.y = -sinkRateMps` (override gravité).
	/// `slowdownMul` multiplie la vitesse horizontale.
	struct HazardEffect
	{
		bool applySinkRate = false;
		float sinkRateMps = 0.0f;
		float slowdownMul = 1.0f;
	};

	/// Simulator client : détection entrée volume, progression sinking, escape,
	/// mort scriptée. Doit être ticked chaque frame depuis l'Engine après le
	/// calcul de position du joueur. Pas de thread-safety (main thread uniquement).
	class HazardSimulator
	{
	public:
		/// Mémorise les références. La scène et les callbacks doivent survivre
		/// au simulator.
		void Init(const HazardScene& scene, const HazardCallbacks& cb) noexcept;

		/// Avance la simulation d'une frame.
		/// \param dt seconds écoulées depuis la dernière frame.
		/// \param playerPos position monde des pieds du joueur.
		/// \param actionPressed front montant du bouton Action (true exactement
		///        la frame où le joueur appuie).
		/// \return effet à appliquer au CharacterController ce frame.
		HazardEffect Update(float dt, engine::math::Vec3 playerPos,
			bool actionPressed) noexcept;

		const HazardState& State() const noexcept { return m_state; }

	private:
		const HazardScene* m_scene = nullptr;
		HazardCallbacks m_cb;
		HazardState m_state;
		engine::math::Vec3 m_lastPlayerPos{0, 0, 0};
		bool m_hasLastPos = false;
	};
}
