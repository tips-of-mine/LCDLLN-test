#pragma once
// ETAPE4.1 (chantiers 2.2 et 2.3 du rapport City of Heroes) — sommeil et
// fréquence de traitement des mobs du shard.
//
// Avant : chaque mob vivant exécutait son IA à 10 Hz (server.mob_ai_hz), même
// sans aucun joueur à proximité et même au repos.
//
// Désormais :
//   - CLASSE DE FRÉQUENCE (MobSimClass) : un mob en combat (cible, menace, retour
//     au point d'attache) garde la fréquence de combat ; un mob hostile au repos
//     (attente, errance) et un PNJ d'ambiance passent à une fréquence plus basse.
//     Le pas de déplacement suit le temps réellement écoulé depuis la dernière
//     décision (CatchUpSeconds) : un mob traité 2 fois moins souvent fait des pas
//     2 fois plus longs, sa vitesse au sol ne change pas.
//   - SOMMEIL : un mob sans joueur à moins de wakeRadiusMeters depuis
//     sleepDelaySeconds, et qui n'est pas occupé (combat, menace, retour), s'endort :
//     plus aucune IA ni déplacement. Le voisinage est vérifié une fois par seconde
//     (étalé par identifiant d'entité). Un mob occupé se réveille sur-le-champ (un
//     sort tiré de loin lui donne de la menace). Au réveil, aucun rattrapage du
//     temps dormi : il reprend là où il s'était arrêté.
//
// Module PUR (aucune dépendance au réseau ni à ServerApp) : testé seul
// (mob_activity_tests). ServerApp applique les décisions.

#include <cstdint>

namespace engine::server::mob_activity
{
	/// Classe de fréquence de traitement d'un mob.
	enum class MobSimClass : uint8_t
	{
		Combat = 0,  ///< cible, menace ou retour au point d'attache : fréquence de combat
		Calm = 1,    ///< mob hostile au repos (attente, errance)
		Ambient = 2, ///< PNJ d'ambiance (routine, jamais d'aggro)
	};

	/// Réglages (config server.mob_ai.*), bornés par Sanitize.
	struct MobActivityConfig
	{
		uint32_t combatHz = 10u;          ///< server.mob_ai_hz (fréquence historique)
		uint32_t calmHz = 5u;             ///< server.mob_ai.calm_hz
		uint32_t ambientHz = 5u;          ///< server.mob_ai.ambient_hz
		float wakeRadiusMeters = 600.0f;  ///< server.mob_ai.wake_radius_m (0 = jamais de sommeil ; le shard le relève à la portée de l'intérêt)
		float sleepDelaySeconds = 3.0f;   ///< server.mob_ai.sleep_delay_s
		float maxCatchUpSeconds = 1.0f;   ///< pas de déplacement maximal après une longue attente
	};

	/// Borne les réglages : fréquences dans [1, tickHz] (calme et ambiance jamais
	/// au-dessus du combat), rayon dans [0, 2000] m, délai dans [0, 60] s.
	MobActivityConfig Sanitize(MobActivityConfig config, uint32_t tickHz);

	/// Classe d'un mob. \p busy = cible d'aggro, menace non vide, état Aggro ou
	/// Return (cf. IsBusy).
	MobSimClass Classify(bool ambient, bool busy);

	/// Vrai si le mob ne doit ni dormir ni ralentir : cible d'aggro, menace, ou
	/// retour au point d'attache en cours (un retour interrompu laisserait le mob
	/// loin de chez lui).
	bool IsBusy(bool hasAggroTarget, bool hasThreat, bool inAggroOrReturnState);

	/// Intervalle entre deux décisions d'IA, en ticks du shard (≥ 1).
	uint32_t IntervalTicks(MobSimClass simClass, const MobActivityConfig& config, uint32_t tickHz);

	/// Durée (s) que la décision courante doit couvrir : le temps écoulé depuis la
	/// précédente (\p lastAiTick → \p nowTick), au moins l'intervalle nominal, au
	/// plus maxCatchUpSeconds. \p lastAiTick == 0 (jamais traité) → intervalle.
	float CatchUpSeconds(uint32_t lastAiTick, uint32_t nowTick, uint32_t intervalTicks,
		uint32_t tickHz, const MobActivityConfig& config);

	/// État de sommeil d'un mob (mutable, par mob).
	struct SleepState
	{
		bool asleep = false;
		uint32_t lastPlayerNearTick = 0; ///< dernier tick où un joueur était dans le rayon (0 = jamais vu)
	};

	/// Transition de sommeil produite par UpdateSleep.
	enum class SleepTransition : uint8_t { None = 0, FellAsleep, WokeUp };

	/// Évaluation périodique (1 Hz) : \p playerNear = un joueur à moins du rayon.
	/// S'endort si personne depuis sleepDelaySeconds et pas \p busy ; se réveille
	/// dès qu'un joueur est proche ou que le mob est occupé. Rayon 0 : jamais de
	/// sommeil. \p spawnTick = tick d'apparition (point de départ du délai si aucun
	/// joueur n'a encore été vu).
	SleepTransition UpdateSleep(SleepState& state, bool playerNear, bool busy, uint32_t nowTick,
		uint32_t spawnTick, uint32_t tickHz, const MobActivityConfig& config);

	/// Réveil immédiat (mob devenu occupé entre deux évaluations : dégâts, menace,
	/// provocation). \return vrai si le mob dormait.
	bool WakeIfBusy(SleepState& state, bool busy);

	/// Vrai si l'évaluation de voisinage (1 Hz) de l'entité \p entityId tombe sur
	/// ce tick : étale la charge sur la seconde au lieu de tout vérifier d'un coup.
	bool IsWakeCheckTick(uint64_t entityId, uint32_t nowTick, uint32_t tickHz);
}
