#pragma once
// CMANGOS.39 (Phase 4.39 step 3+4) — Wire payloads pour les opcodes Skills
// (113-119).
//
// Trois paires Request/Response + 1 push :
//   - List              (113/114)
//   - Learn             (115/116)
//   - Use               (117/118)
//   - UpgradeNotification (119 push) — Master to Client pour annoncer un gain
//     de skill (suite a un Use ou a un crafting hook futur).
//
// Le SkillBook est read-only cote client cible : le serveur seul decide quand
// gain ; les requests Learn/Use sont des intentions, le serveur valide.
//
// Format wire : ByteReader/ByteWriter little-endian. Les valeurs uint16
// (skillId, value, cap, bonus) sont serialisees via WriteU32 (le ByteWriter
// n'expose pas WriteU16 generique mais WriteU32 ; on caste donc et on
// re-tronque au Parse). Note : ByteWriter EXPOSE bien WriteU16 — on l'utilise
// systematiquement pour les champs uint16 afin de garder le wire compact.
// Le delta du push UpgradeNotification est int16 ; on serialise comme uint16
// (cast bitwise two's complement) puis on re-cast en int16 au Parse.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace engine::network
{
	// =========================================================================
	// Codes d'erreur — wire-level pour Skills.
	// =========================================================================

	/// Code d'erreur generique pour les opcodes Skills.
	/// 0 = OK ; sinon le payload secondaire est zero (entries vide / value=0).
	enum class SkillErrorCode : uint8_t
	{
		Ok               = 0,
		Unauthorized     = 6,  ///< Pas de session valide cote master.
		UnknownSkill     = 7,  ///< skillId hors du catalog V1 (1..5).
		AlreadyLearned   = 8,  ///< Learn d'un skill deja appris.
		SkillNotLearned  = 9,  ///< Use sur un skill jamais appris.
		SkillFailed      = 10, ///< Use sans gain (RNG failed) — soft failure.
	};

	/// Result code retourne par SkillUseResponse.
	/// Different de l'error code : Use peut reussir sans erreur mais sans gain.
	enum class SkillUseResult : uint8_t
	{
		Success         = 0,
		Fail            = 1,
		CriticalSuccess = 2,
	};

	// =========================================================================
	// SKILLS_LIST — Client to Master : liste les skills de l'account.
	// =========================================================================

	/// Wire format : (vide). L'account est derive de la session cote master.
	struct SkillsListRequestPayload
	{
		// (vide)
	};

	/// Une entree du SkillBook (skillId, value, cap, bonus).
	/// Mirror direct de skills::SkillEntry du shardd avec skillId additionnel.
	struct SkillBookEntry
	{
		uint16_t skillId = 0;
		uint16_t value   = 0; ///< niveau actuel.
		uint16_t cap     = 0; ///< cap dur (max attribuable a ce moment).
		uint16_t bonus   = 0; ///< bonus temporaire (potion, equipement).
	};

	/// Wire format :
	///   uint8  error            (cf. SkillErrorCode)
	///   uint16 count             (si error == 0)
	///   <count> entries          (2 + 2 + 2 + 2 = 8 octets chacune)
	struct SkillsListResponsePayload
	{
		uint8_t                       error = 0;
		std::vector<SkillBookEntry>   skills;
	};

	// =========================================================================
	// SKILL_LEARN — Client to Master : demande apprendre un skill.
	// =========================================================================

	/// Wire format : uint16 skillId.
	struct SkillLearnRequestPayload
	{
		uint16_t skillId = 0;
	};

	/// Wire format :
	///   uint8  error
	///   uint16 initialCap   (cap initial du skill, 0 si error != Ok)
	struct SkillLearnResponsePayload
	{
		uint8_t  error      = 0;
		uint16_t initialCap = 0;
	};

	// =========================================================================
	// SKILL_USE — Client to Master : utilise un skill non-combat.
	// =========================================================================

	/// Wire format :
	///   uint16 skillId
	///   uint64 targetEntityId (V1 : opaque, log seulement)
	struct SkillUseRequestPayload
	{
		uint16_t skillId        = 0;
		uint64_t targetEntityId = 0;
	};

	/// Wire format :
	///   uint8  error
	///   uint8  result        (cf. SkillUseResult ; 0=Success, 1=Fail, 2=Crit)
	///   uint16 deltaValue    (gain effectif applique au skill)
	struct SkillUseResponsePayload
	{
		uint8_t  error      = 0;
		uint8_t  result     = 0;
		uint16_t deltaValue = 0;
	};

	// =========================================================================
	// SKILL_UPGRADE_NOTIFICATION — Master to Client (push, request_id=0).
	// Annonce qu'un gain de skill a eu lieu cote serveur.
	// =========================================================================

	/// Wire format :
	///   uint16 skillId      (2)
	///   uint16 newValue     (2)
	///   uint16 newCap       (2)
	///   int16  delta        (2, cast via uint16 ; positif = gain, 0 = bonus/cap update)
	struct SkillUpgradeNotificationPayload
	{
		uint16_t skillId  = 0;
		uint16_t newValue = 0;
		uint16_t newCap   = 0;
		int16_t  delta    = 0;
	};

	// -------------------------------------------------------------------------
	// Parse / Build — Requests
	// -------------------------------------------------------------------------

	/// Parse le payload d'un SKILLS_LIST_REQUEST. Toujours OK (payload vide accepte).
	std::optional<SkillsListRequestPayload> ParseSkillsListRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Parse le payload d'un SKILL_LEARN_REQUEST. \return nullopt si payloadSize < 2.
	std::optional<SkillLearnRequestPayload> ParseSkillLearnRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Parse le payload d'un SKILL_USE_REQUEST. \return nullopt si payloadSize < 10.
	std::optional<SkillUseRequestPayload>   ParseSkillUseRequestPayload  (const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildSkillsListRequestPayload();
	std::vector<uint8_t> BuildSkillLearnRequestPayload(uint16_t skillId);
	std::vector<uint8_t> BuildSkillUseRequestPayload  (uint16_t skillId, uint64_t targetEntityId);

	// -------------------------------------------------------------------------
	// Parse / Build — Responses (payload-only)
	// -------------------------------------------------------------------------

	std::optional<SkillsListResponsePayload>          ParseSkillsListResponsePayload         (const uint8_t* payload, size_t payloadSize);
	std::optional<SkillLearnResponsePayload>          ParseSkillLearnResponsePayload         (const uint8_t* payload, size_t payloadSize);
	std::optional<SkillUseResponsePayload>            ParseSkillUseResponsePayload           (const uint8_t* payload, size_t payloadSize);
	std::optional<SkillUpgradeNotificationPayload>    ParseSkillUpgradeNotificationPayload   (const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildSkillsListResponsePayload         (uint8_t error, const std::vector<SkillBookEntry>& skills);
	std::vector<uint8_t> BuildSkillLearnResponsePayload         (uint8_t error, uint16_t initialCap);
	std::vector<uint8_t> BuildSkillUseResponsePayload           (uint8_t error, uint8_t result, uint16_t deltaValue);
	std::vector<uint8_t> BuildSkillUpgradeNotificationPayload   (uint16_t skillId, uint16_t newValue, uint16_t newCap, int16_t delta);

	// -------------------------------------------------------------------------
	// Build full packets (header + payload). Utilise cote handler serveur.
	// -------------------------------------------------------------------------

	std::vector<uint8_t> BuildSkillsListResponsePacket  (uint8_t error, const std::vector<SkillBookEntry>& skills,
	                                                     uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildSkillLearnResponsePacket  (uint8_t error, uint16_t initialCap,
	                                                     uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildSkillUseResponsePacket    (uint8_t error, uint8_t result, uint16_t deltaValue,
	                                                     uint32_t requestId, uint64_t sessionIdHeader);
	/// Push asynchrone (request_id=0). Aucun client request en correspondance.
	std::vector<uint8_t> BuildSkillUpgradeNotificationPacket(uint16_t skillId, uint16_t newValue, uint16_t newCap, int16_t delta,
	                                                          uint64_t sessionIdHeader);
}
