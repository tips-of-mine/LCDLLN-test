#pragma once

// Monde vivant (chantier « Royaumes ») — routines des PNJ d'ambiance.
//
// Un PNJ d'ambiance (villageois, garde, marchand, voyageur…) est un mob du
// shard dont l'archétype est de comportement « ambient » (non attaquable, sans
// aggro) et dont le spawner porte une ROUTINE :
//   - tour   : boucle sur un chemin (porte → rues → porte), pauses aléatoires ;
//   - patrol : boucle (ou aller-retour si pingPong) sur un chemin, rondes ;
//   - post   : reste à son poste (petite errance), regarde un point ;
//   - wander : errance libre dans un rayon autour de son point d'attache.
// Un horaire (day / night / always) décide s'il est dehors ; hors de son
// horaire, il rentre à son point de départ (sa porte) puis disparaît.
//
// Module PUR (ni réseau, ni ServerApp) : logique de décision testable
// (ambient_routine_tests). ServerApp applique les déplacements.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::server::ambient
{
	enum class RoutineKind : uint8_t { None = 0, Tour, Patrol, Post, Wander };
	enum class RoutineSchedule : uint8_t { Always = 0, Day, Night };

	struct RoutinePoint { float x = 0.0f; float z = 0.0f; };

	/// Définition (données de spawner, immuables).
	struct RoutineDefinition
	{
		RoutineKind kind = RoutineKind::None;
		RoutineSchedule schedule = RoutineSchedule::Always;
		std::vector<RoutinePoint> path;
		float pauseMinSec = 2.0f;
		float pauseMaxSec = 8.0f;
		float pauseChance = 0.35f;   ///< probabilité de marquer une pause à un point
		float wanderRadiusMeters = 0.0f;
		bool pingPong = false;
		bool hasFacePoint = false;   ///< post : point regardé à l'arrêt
		RoutinePoint facePoint;
	};

	/// État courant d'un PNJ (mutable, par mob).
	struct RoutineState
	{
		uint32_t waypoint = 0;       ///< index de la cible courante dans `path`
		int8_t direction = 1;        ///< +1 / -1 (aller-retour)
		bool hasTarget = false;
		RoutinePoint target;
		uint32_t pauseUntilTick = 0; ///< tick serveur de fin de pause (0 = pas de pause)
		bool goingHome = false;      ///< hors horaire : rentre à son point de départ
	};

	/// Convertit les libellés JSON. Inconnu → None / Always (et false).
	bool ParseRoutineKind(std::string_view text, RoutineKind& out);
	bool ParseRoutineSchedule(std::string_view text, RoutineSchedule& out);

	/// Vrai si le PNJ doit être dehors à l'heure de jeu \p hours ∈ [0,24).
	/// Day : 6 h → 21 h ; Night : 20 h → 6 h (relève au crépuscule).
	bool IsScheduleOpen(RoutineSchedule schedule, float hours);

	/// Point de départ / de retour (porte) : premier point du chemin, sinon le
	/// point d'attache du spawner.
	RoutinePoint HomePoint(const RoutineDefinition& def, float homeX, float homeZ);

	/// Choisit la PROCHAINE cible après une arrivée (ou au démarrage si
	/// `state.hasTarget` est faux). \p roll01 renvoie un réel uniforme [0,1).
	/// Effet de bord : met à jour `state` (waypoint, direction, target).
	void ChooseNextTarget(const RoutineDefinition& def, RoutineState& state,
		float homeX, float homeZ, float (*roll01)(void* ctx), void* ctx);

	/// Durée de pause (s) à tirer à l'arrivée sur une cible, ou 0 si pas de pause.
	float RollPauseSeconds(const RoutineDefinition& def, float (*roll01)(void* ctx), void* ctx);
}
