#pragma once
// ETAPE3.3 (chantier 4.1) — règles de mouvement partagées client / shard.
//
// Le client simule toujours son personnage (CharacterController : collisions, terrain,
// eau) et envoie à 20 Hz des commandes numérotées : son horloge de simulation, l'instant
// de son dernier saut et de sa dernière esquive, la position obtenue. Le shard n'a pas
// la géométrie du monde : il ne rejoue pas les collisions, mais il TRANCHE sur ce qui
// est physiquement possible, avec ces règles communes aux deux côtés :
//   - budget de temps : l'horloge de simulation du client n'avance jamais plus vite que
//     l'horloge du shard (2 % et 300 ms de marge ; le temps non utilisé ne se garde pas
//     plus d'une seconde) — un client accéléré ne gagne rien ;
//   - distance horizontale : au plus la vitesse maximale AUTORITAIRE du personnage
//     (stats du shard : classe, équipement, auras) × durée, plus l'esquive (11 m/s
//     pendant 0,45 s, au plus une par seconde) ;
//   - montée : pente de 45°, 0,1 m de marge, nage, et 0,65 m par saut (au plus un
//     saut toutes les 0,4 s) ; descente : chute bornée ;
//   - au-delà, à l'horizontale : le shard ramène la position au point atteignable le
//     plus proche et renvoie la position officielle avec le numéro de commande
//     (MoveCorrection) ; le client s'y recale et rejoue les commandes qui ont suivi ;
//   - au-delà, à la verticale : la hauteur officielle (celle que voient les autres
//     joueurs) suit le client à la vitesse permise, SANS recaler le client. Le shard ne
//     connaît pas le terrain : imposer une hauteur pourrait mettre le client sous le
//     sol, qui remonterait aussitôt (boucle de corrections). Un client qui vole ne
//     trompe donc que son propre écran.
// Les commandes portent des instants (pas des durées ni des drapeaux) : une commande
// perdue ne fausse pas la suivante.
//
// Aucune dépendance au réseau, au rendu ni à engine_core (testable seul, cf.
// movement_rules_tests).

#include <cstdint>

namespace engine::movement
{
	/// Constantes physiques communes (alignées sur CharacterController::Config).
	struct MovementLimits
	{
		float maxSlopeRise = 1.0f;              ///< montée par mètre horizontal (45°).
		float stepHeightMeters = 0.1f;          ///< montée tolérée sans avancer (marches, arrondis) ; les marches en avançant passent par la pente.
		float jumpApexMeters = 0.65f;           ///< hauteur d'un saut (jumpSpeed² / 2|g| ≈ 0,60 m).
		uint32_t jumpCooldownMs = 400u;         ///< deux sauts acceptés sont séparés d'au moins ça.
		float swimRiseMetersPerSecond = 2.0f;   ///< remontée à la nage (le shard ne connaît pas l'eau).
		float maxFallMetersPerSecond = 60.0f;   ///< chute bornée.
		float fallSlackMeters = 2.0f;           ///< marge de descente par commande.
		float dodgeSpeedMetersPerSecond = 11.0f;
		uint32_t dodgeDurationMs = 450u;
		uint32_t dodgeCooldownMs = 1000u;
		float speedTolerance = 1.10f;           ///< marge sur la vitesse (accélération, arrondis).
		float distanceSlackMeters = 0.05f;      ///< marge fixe par commande (1 m/s à 20 Hz).
		uint32_t slackPeriodMs = 50u;           ///< marges fixes (distance, marche, chute) accordées en entier pour au moins ce temps couvert, au prorata en dessous (anti-rafale).
		uint32_t maxCommandGapMs = 1000u;       ///< temps simulé maximal couvert par une commande.
		float clockRateTolerance = 1.02f;       ///< dérive d'horloge client tolérée.
		uint32_t timeBudgetSlackMs = 300u;      ///< avance tolérée sur l'horloge du shard.
		uint32_t maxBankedTimeMs = 1000u;       ///< temps non simulé gardé au plus.
		uint32_t firstCommandDurationMs = 100u; ///< durée prêtée à la première commande.
		float maxCoordinateMeters = 1.0e7f;     ///< au-delà (ou NaN) : refus.
	};

	/// Une commande de mouvement reçue du client.
	struct MoveCommand
	{
		uint32_t sequence = 0;      ///< numéro croissant (> 0).
		uint32_t clientTimeMs = 0;  ///< horloge de simulation du client à la fin de la période.
		uint32_t lastJumpMs = 0;    ///< instant (horloge client) du dernier saut, 0 = aucun.
		uint32_t lastDodgeMs = 0;   ///< instant (horloge client) de la dernière esquive, 0 = aucune.
		float x = 0.0f;             ///< position obtenue par le client (m, globale).
		float y = 0.0f;
		float z = 0.0f;
	};

	/// État de mouvement d'un personnage tenu par le shard.
	struct MovementState
	{
		float x = 0.0f;                  ///< position officielle.
		float y = 0.0f;
		float z = 0.0f;
		uint32_t lastSequence = 0;       ///< dernière commande appliquée.
		bool started = false;            ///< une commande a déjà été appliquée depuis le dernier Reset.
		uint32_t lastClientTimeMs = 0;   ///< horloge client de la dernière commande appliquée.
		uint64_t budgetStartWallMs = 0;  ///< origine du budget (horloge shard).
		uint64_t budgetStartClientMs = 0;///< origine du budget (horloge client).
		uint32_t lastJumpClientMs = 0;   ///< dernier saut accepté.
		float jumpRiseLeft = 0.0f;       ///< montée de saut encore disponible.
		uint32_t dodgeStartClientMs = 0; ///< dernière esquive acceptée (0 = aucune).
	};

	/// Verdict d'une commande.
	enum class MoveVerdict : uint8_t
	{
		Accepted,  ///< position horizontale du client acceptée (hauteur éventuellement freinée).
		Corrected, ///< position horizontale ramenée dans les limites : renvoyer la position officielle.
		Stale,     ///< numéro ou horloge déjà vus (paquet en retard ou rejoué) : ignorée.
	};

	/// Résultat d'une commande.
	struct MoveResult
	{
		MoveVerdict verdict = MoveVerdict::Stale;
		uint32_t acceptedDurationMs = 0; ///< durée prise en compte après le budget de temps.
		float maxDistanceMeters = 0.0f;  ///< distance horizontale autorisée (diagnostic).
		bool verticalClamped = false;    ///< hauteur officielle freinée (montée ou chute hors règles).
		float velocityX = 0.0f;          ///< vitesse officielle sur la période (m/s).
		float velocityY = 0.0f;
		float velocityZ = 0.0f;
	};

	/// (Re)pose la position officielle : connexion, téléport, réapparition, changement
	/// de zone. Le budget de temps repart à la commande suivante ; les numéros de
	/// commande déjà vus restent refusés.
	void ResetMovement(MovementState& state, float x, float y, float z);

	/// Applique une commande. \p maxSpeed = vitesse horizontale maximale autoritaire
	/// (m/s, la plus grande des allures du personnage). \p nowWallMs = horloge monotone
	/// du shard. Met à jour \p state (position officielle, budget, saut, esquive).
	MoveResult ApplyMoveCommand(MovementState& state, const MoveCommand& command, float maxSpeed,
		uint64_t nowWallMs, const MovementLimits& limits = MovementLimits{});
}
