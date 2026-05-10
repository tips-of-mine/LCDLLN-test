#pragma once
// CMANGOS.30 (Phase 5.30 step 3+4) — Wire payloads pour les opcodes
// Cinematics (108-112). 1 push + 2 paires Request/Response :
//   - PlayNotification (108, push)        : master annonce une cinematic.
//   - Ack             (109/110)           : client signale completion.
//   - Skip            (111/112)           : client demande a skip.
//
// Les sequences cinematiques sont content-driven (fichiers data cote client) :
// le wire ne transporte que des metadata (sequenceId, reason, completionState).
//
// Format wire : ByteReader/ByteWriter little-endian. Les bool sont serialises
// sur 1 octet via WriteBytes/ReadBytes pour preserver la portabilite.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace engine::network
{
	// =========================================================================
	// Codes d'erreur — wire-level pour Cinematics.
	// =========================================================================

	/// Code d'erreur generique pour les opcodes Cinematics. 0 = OK.
	enum class CinematicErrorCode : uint8_t
	{
		Ok                = 0,
		UnknownSequence   = 1, ///< Le sequenceId n'existe pas dans le catalog cote master (V1 : never returned).
		SkipNotAllowed    = 2, ///< Le master refuse le skip (cinematique obligatoire ; V1 : never returned).
		NoActiveCinematic = 3, ///< L'ack/skip arrive sans cinematique active (V1 : log only).
		Unauthorized      = 6, ///< Pas de session valide cote master.
	};

	// =========================================================================
	// Constantes "reason" pour CinematicPlayNotification.
	// =========================================================================

	/// Raison du declenchement d'une cinematic. Mapping cote client uniquement
	/// (le master logge la valeur brute).
	enum class CinematicReason : uint8_t
	{
		ZoneEnter     = 0, ///< L'avatar a franchi un trigger de zone scenique.
		QuestComplete = 1, ///< La quete a ete completee, reward cutscene.
		Intro         = 2, ///< Introduction (premiere connexion personnage, zone tutoriel, etc.).
		Other         = 3, ///< Catch-all (event manuel, GM trigger, ...).
	};

	/// Etat de completion remonte par le client a la fin de la lecture.
	/// Mapping cote client (le master logge la valeur brute).
	enum class CinematicCompletionState : uint8_t
	{
		EndedNormally = 0, ///< La sequence est arrivee a la derniere keyframe.
		SkippedByUser = 1, ///< L'utilisateur a confirme un skip (Esc).
		Interrupted   = 2, ///< Interruption externe (deconnexion, mort, etc.).
	};

	// =========================================================================
	// CINEMATIC_PLAY_NOTIFICATION (108, push) — Master to Client.
	// Annonce qu'une cinematic doit etre jouee ; le client charge la sequence
	// depuis ses fichiers data locaux et lance la lecture.
	// =========================================================================

	/// Wire format :
	///   uint32 sequenceId   (id opaque, mappe vers game/data/cinematics/seq<id>.json)
	///   uint8  reason       (cf. CinematicReason)
	struct CinematicPlayNotificationPayload
	{
		uint32_t sequenceId = 0;
		uint8_t  reason     = 0;
	};

	// =========================================================================
	// CINEMATIC_ACK_REQUEST (109) — Client to Master.
	// Signale que la lecture d'une cinematic est terminee ou interrompue.
	// =========================================================================

	/// Wire format :
	///   uint32 sequenceId       (id de la cinematic terminee)
	///   uint8  completionState  (cf. CinematicCompletionState)
	struct CinematicAckRequestPayload
	{
		uint32_t sequenceId      = 0;
		uint8_t  completionState = 0;
	};

	// =========================================================================
	// CINEMATIC_ACK_RESPONSE (110) — Master to Client.
	// ACK minimal. V1 : toujours Ok (le master ne tracke pas l'active cinematic).
	// =========================================================================

	/// Wire format :
	///   uint8 error   (cf. CinematicErrorCode)
	struct CinematicAckResponsePayload
	{
		uint8_t error = 0;
	};

	// =========================================================================
	// CINEMATIC_SKIP_REQUEST (111) — Client to Master.
	// Demande a skipper la cinematic en cours. Le master peut autoriser ou
	// refuser (V1 : toujours autorise).
	// =========================================================================

	/// Wire format :
	///   uint32 sequenceId   (id de la cinematic active cote client)
	struct CinematicSkipRequestPayload
	{
		uint32_t sequenceId = 0;
	};

	// =========================================================================
	// CINEMATIC_SKIP_RESPONSE (112) — Master to Client.
	// Reponse au skip request. allowed == true => le client peut interrompre
	// la lecture et envoyer un ack avec completionState = SkippedByUser.
	// =========================================================================

	/// Wire format :
	///   uint8 error    (cf. CinematicErrorCode)
	///   uint8 allowed  (0 = refuse, 1 = autorise ; serialise via uint8)
	struct CinematicSkipResponsePayload
	{
		uint8_t error   = 0;
		bool    allowed = false;
	};

	// -------------------------------------------------------------------------
	// Parse / Build — Push notification (108)
	// -------------------------------------------------------------------------

	std::optional<CinematicPlayNotificationPayload> ParseCinematicPlayNotificationPayload(const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildCinematicPlayNotificationPayload(uint32_t sequenceId, uint8_t reason);

	/// Push asynchrone (request_id=0).
	std::vector<uint8_t> BuildCinematicPlayNotificationPacket(uint32_t sequenceId, uint8_t reason,
	                                                          uint64_t sessionIdHeader);

	// -------------------------------------------------------------------------
	// Parse / Build — Ack request/response (109/110)
	// -------------------------------------------------------------------------

	std::optional<CinematicAckRequestPayload>  ParseCinematicAckRequestPayload(const uint8_t* payload, size_t payloadSize);
	std::optional<CinematicAckResponsePayload> ParseCinematicAckResponsePayload(const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildCinematicAckRequestPayload(uint32_t sequenceId, uint8_t completionState);
	std::vector<uint8_t> BuildCinematicAckResponsePayload(uint8_t error);

	std::vector<uint8_t> BuildCinematicAckResponsePacket(uint8_t error,
	                                                     uint32_t requestId, uint64_t sessionIdHeader);

	// -------------------------------------------------------------------------
	// Parse / Build — Skip request/response (111/112)
	// -------------------------------------------------------------------------

	std::optional<CinematicSkipRequestPayload>  ParseCinematicSkipRequestPayload(const uint8_t* payload, size_t payloadSize);
	std::optional<CinematicSkipResponsePayload> ParseCinematicSkipResponsePayload(const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildCinematicSkipRequestPayload(uint32_t sequenceId);
	std::vector<uint8_t> BuildCinematicSkipResponsePayload(uint8_t error, bool allowed);

	std::vector<uint8_t> BuildCinematicSkipResponsePacket(uint8_t error, bool allowed,
	                                                      uint32_t requestId, uint64_t sessionIdHeader);
}
