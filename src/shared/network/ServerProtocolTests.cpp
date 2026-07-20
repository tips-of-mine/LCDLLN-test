// TE.1 — round-trip du protocole UDP gameplay : EncodeInput/DecodeInput (incl. Y + yaw,
// ajoutés en TC.1) + EncodeHello/DecodeHello + rejet d'un paquet tronqué.
// Pattern repo : int main() + assert + std::puts, sans framework. Cible CTest :
// server_protocol_tests (branche UNIX de src/CMakeLists.txt).
//
// Le preset CI Linux est Release (-DNDEBUG) → on neutralise NDEBUG pour que les
// assert mordent réellement.
#ifdef NDEBUG
#	undef NDEBUG
#endif

#include "src/shared/network/ServerProtocol.h"

#include <cassert>
#include <cstdio>
#include <vector>

using engine::server::DecodeHello;
using engine::server::DecodeInput;
using engine::server::DecodeSnapshot;
using engine::server::DecodeGoodbye;
using engine::server::EncodeGoodbye;
using engine::server::GoodbyeMessage;
using engine::server::EncodeHello;
using engine::server::EncodeInput;
using engine::server::EncodeSnapshot;
using engine::server::HelloMessage;
using engine::server::InputMessage;
using engine::server::SnapshotEntity;
using engine::server::SnapshotMessage;

namespace
{
	/// TC.1 : un Input encodé puis décodé restitue exactement clientId, sequence et la
	/// position 3D + yaw (encodage bit-exact via bit_cast → égalité flottante exacte).
	void TestInputRoundTrip()
	{
		InputMessage in{};
		in.clientId = 7u;
		in.inputSequence = 12345u;
		in.positionMetersX = 12.5f;
		in.positionMetersY = 100.25f;
		in.positionMetersZ = -8.75f;
		in.yawRadians = 1.5708f;
		in.animationState = 9u; // TD.8 : Emote (valeur d'AvatarAnimState)

		const std::vector<std::byte> packet = EncodeInput(in);
		assert(!packet.empty());

		InputMessage out{};
		assert(DecodeInput(packet, out));
		assert(out.clientId == in.clientId);
		assert(out.inputSequence == in.inputSequence);
		assert(out.positionMetersX == in.positionMetersX);
		assert(out.positionMetersY == in.positionMetersY);
		assert(out.positionMetersZ == in.positionMetersZ);
		assert(out.yawRadians == in.yawRadians);
		assert(out.animationState == in.animationState); // TD.8
		std::puts("[OK] TestInputRoundTrip");
	}

	/// Un Input tronqué (payload < 24 octets) est rejeté par DecodeInput.
	void TestInputRejectsTruncated()
	{
		InputMessage in{};
		in.clientId = 1u;
		std::vector<std::byte> packet = EncodeInput(in);
		assert(packet.size() > 4u);
		packet.resize(packet.size() - 1u); // ampute un octet du payload

		InputMessage out{};
		assert(!DecodeInput(packet, out));
		std::puts("[OK] TestInputRejectsTruncated");
	}

	/// Round-trip Hello (clientNonce uint64 = character_id complet, cf. protocole v2+).
	void TestHelloRoundTrip()
	{
		HelloMessage in{};
		in.requestedTickHz = 20u;
		in.requestedSnapshotHz = 10u;
		in.clientNonce = 0x1122334455667788ull;

		const std::vector<std::byte> packet = EncodeHello(in);
		assert(!packet.empty());

		HelloMessage out{};
		assert(DecodeHello(packet, out));
		assert(out.requestedTickHz == in.requestedTickHz);
		assert(out.requestedSnapshotHz == in.requestedSnapshotHz);
		assert(out.clientNonce == in.clientNonce);
		std::puts("[OK] TestHelloRoundTrip");
	}

	/// TD.4 — round-trip Snapshot avec `playerClientId` exposé pour les joueurs et
	/// laissé à 0 pour les mobs/lootbags. Format wire bump v3→v4 (52 octets / entité).
	void TestSnapshotRoundTripWithPlayerClientId()
	{
		SnapshotMessage inMsg{};
		inMsg.clientId = 42u;
		inMsg.serverTick = 1234u;
		inMsg.connectedClients = 2u;
		inMsg.entityCount = 3u;
		inMsg.receivedPackets = 99u;
		inMsg.sentPackets = 77u;

		std::vector<SnapshotEntity> in;
		// 2 joueurs avec playerClientId ≠ 0, characterName et gender non vides ;
		// 1 mob avec playerClientId = 0, characterName et gender vides.
		SnapshotEntity ePlayerA{};
		ePlayerA.entityId = 0x200000001ull;
		ePlayerA.state.positionX = 10.5f;
		ePlayerA.state.positionY = 1.25f;
		ePlayerA.state.positionZ = -3.75f;
		ePlayerA.state.yawRadians = 0.42f;
		ePlayerA.playerClientId = 7u;
		ePlayerA.characterName = "homme"; // TD.5
		ePlayerA.gender = "male";          // TD.6
		ePlayerA.animationState = engine::server::AvatarAnimState::Emote; // TD.8
		in.push_back(ePlayerA);

		SnapshotEntity ePlayerB{};
		ePlayerB.entityId = 0x200000002ull;
		ePlayerB.state.positionX = 100.0f;
		ePlayerB.state.positionZ = 50.0f;
		ePlayerB.playerClientId = 12u;
		ePlayerB.characterName = "femme"; // TD.5
		ePlayerB.gender = "female";        // TD.6
		ePlayerB.animationState = engine::server::AvatarAnimState::Roll; // TD.8
		in.push_back(ePlayerB);

		SnapshotEntity eMob{};
		eMob.entityId = 0x300000005ull;
		eMob.state.currentHealth = 80u;
		eMob.state.maxHealth = 100u;
		eMob.playerClientId = 0u; // mob => pas de nameplate
		// characterName et gender restent vides pour les mobs / lootbags (TD.5/TD.6).
		eMob.archetypeId = 100u; // Combat SP1 (wire v9) : archétype résolu côté client
		in.push_back(eMob);

		const std::vector<std::byte> packet = EncodeSnapshot(inMsg, in);
		assert(!packet.empty());

		SnapshotMessage outMsg{};
		std::vector<SnapshotEntity> out;
		assert(DecodeSnapshot(packet, outMsg, out));
		assert(outMsg.clientId == inMsg.clientId);
		assert(outMsg.serverTick == inMsg.serverTick);
		assert(outMsg.entityCount == inMsg.entityCount);
		assert(out.size() == 3u);

		assert(out[0].entityId == ePlayerA.entityId);
		assert(out[0].state.positionX == ePlayerA.state.positionX);
		assert(out[0].state.positionY == ePlayerA.state.positionY);
		assert(out[0].state.positionZ == ePlayerA.state.positionZ);
		assert(out[0].state.yawRadians == ePlayerA.state.yawRadians);
		assert(out[0].playerClientId == 7u);
		assert(out[0].characterName == "homme"); // TD.5
		assert(out[0].gender == "male");          // TD.6
		assert(out[0].animationState == engine::server::AvatarAnimState::Emote); // TD.8
		assert(out[0].archetypeId == 0u); // Combat SP1 : joueur => pas d'archétype

		assert(out[1].entityId == ePlayerB.entityId);
		assert(out[1].state.positionX == ePlayerB.state.positionX);
		assert(out[1].playerClientId == 12u);
		assert(out[1].characterName == "femme"); // TD.5
		assert(out[1].gender == "female");        // TD.6
		assert(out[1].animationState == engine::server::AvatarAnimState::Roll); // TD.8

		assert(out[2].entityId == eMob.entityId);
		assert(out[2].state.currentHealth == 80u);
		assert(out[2].state.maxHealth == 100u);
		assert(out[2].playerClientId == 0u);
		assert(out[2].characterName.empty()); // TD.5 : mob => pas de nom
		assert(out[2].gender.empty());        // TD.6 : mob => pas de genre
		assert(out[2].animationState == engine::server::AvatarAnimState::Idle); // TD.8 : défaut
		assert(out[2].archetypeId == 100u); // Combat SP1 (wire v9) : archétype du mob
		std::puts("[OK] TestSnapshotRoundTripWithPlayerClientId");
	}

	/// TD.6 — un Snapshot dont la taille de payload est inferieure au minimum attendu
	/// (61 octets/entité = 8 entityId + 40 EntityState + 4 playerClientId + 2 nameLen=0
	/// + 2 genderLen=0 + 1 animationState + 4 archetypeId, au-delà de l'entête 24 octets)
	/// doit être rejeté. Defense en profondeur contre un pair qui parlerait une version
	/// antérieure du wire. Le bump kProtocolVersion à v9 filtre déjà la plupart des cas
	/// dans DecodeHeader, ce test couvre une corruption après header valide.
	void TestSnapshotRejectsTruncatedPayload()
	{
		SnapshotMessage inMsg{};
		inMsg.entityCount = 1u;
		std::vector<SnapshotEntity> in;
		SnapshotEntity e{};
		e.entityId = 0x200000001ull;
		e.playerClientId = 5u;
		// characterName vide → 2 octets de nameLen seulement.
		in.push_back(e);

		std::vector<std::byte> packet = EncodeSnapshot(inMsg, in);
		// Ampute 4 octets : simule un pair qui n'aurait pas écrit nameLen+name (4 < 2 mais on
		// retire 4 pour passer aussi sous le playerClientId terminé, soit un pair pré-v6).
		assert(packet.size() >= 4u);
		packet.resize(packet.size() - 4u);

		SnapshotMessage outMsg{};
		std::vector<SnapshotEntity> out;
		assert(!DecodeSnapshot(packet, outMsg, out));
		std::puts("[OK] TestSnapshotRejectsTruncatedPayload");
	}

	/// TG.1 — round-trip Snapshot avec chunkIndex / chunkCount > 1 : le wire transporte
	/// bien les 2 champs et le client peut donc raisonner sur le chunking.
	void TestSnapshotRoundTripWithChunking()
	{
		SnapshotMessage inMsg{};
		inMsg.clientId = 5u;
		inMsg.serverTick = 999u;
		inMsg.connectedClients = 30u;
		inMsg.entityCount = 2u; // ce chunk porte 2 entités sur un total potentiel de N
		inMsg.chunkIndex = 1u;  // 2e chunk (0-indexed)
		inMsg.chunkCount = 3u;  // sur 3 chunks au total

		std::vector<SnapshotEntity> in;
		SnapshotEntity a{};
		a.entityId = 100u;
		a.playerClientId = 1u;
		in.push_back(a);
		SnapshotEntity b{};
		b.entityId = 200u;
		b.playerClientId = 2u;
		in.push_back(b);

		const std::vector<std::byte> packet = EncodeSnapshot(inMsg, in);
		assert(!packet.empty());

		SnapshotMessage outMsg{};
		std::vector<SnapshotEntity> out;
		assert(DecodeSnapshot(packet, outMsg, out));
		assert(outMsg.chunkIndex == 1u);
		assert(outMsg.chunkCount == 3u);
		assert(outMsg.entityCount == 2u);
		assert(out.size() == 2u);
		assert(out[0].playerClientId == 1u);
		assert(out[1].playerClientId == 2u);
		std::puts("[OK] TestSnapshotRoundTripWithChunking");
	}

	/// TG.1 — defauts SnapshotMessage : chunkCount=1 (mono-paquet) doit round-tripper
	/// proprement (compat « pas de chunking » = cas dominant a 2 joueurs).
	void TestSnapshotMonoChunkDefaultsRoundTrip()
	{
		SnapshotMessage inMsg{};
		inMsg.clientId = 1u;
		inMsg.entityCount = 0u;
		// chunkIndex defaut = 0, chunkCount defaut = 1 (cf. struct init in ServerProtocol.h).

		const std::vector<std::byte> packet = EncodeSnapshot(inMsg, {});
		assert(!packet.empty());

		SnapshotMessage outMsg{};
		std::vector<SnapshotEntity> out;
		assert(DecodeSnapshot(packet, outMsg, out));
		assert(outMsg.chunkIndex == 0u);
		assert(outMsg.chunkCount == 1u);
		assert(out.empty());
		std::puts("[OK] TestSnapshotMonoChunkDefaultsRoundTrip");
	}
}

	/// Départ propre : un Goodbye encodé puis décodé restitue le clientId, et un paquet
	/// tronqué (payload != 4 octets) est rejeté par DecodeGoodbye.
	void TestGoodbyeRoundTrip()
	{
		GoodbyeMessage in{};
		in.clientId = 4242u;
		const std::vector<std::byte> packet = EncodeGoodbye(in);
		assert(!packet.empty());

		GoodbyeMessage out{};
		assert(DecodeGoodbye(packet, out));
		assert(out.clientId == in.clientId);

		std::vector<std::byte> truncated = packet;
		truncated.resize(truncated.size() - 1u);
		GoodbyeMessage bad{};
		assert(!DecodeGoodbye(truncated, bad));
		std::puts("[OK] TestGoodbyeRoundTrip");
	}

	/// Combat SP2 (wire v10) — un CombatEvent encodé puis décodé restitue les flags
	/// critique/raté en plus des champs historiques.
	void TestCombatEventRoundTripWithFlags()
	{
		engine::server::CombatEventMessage in{};
		in.attackerEntityId = 0x200000001ull;
		in.targetEntityId = 0x300000005ull;
		in.damage = 75u;
		in.targetCurrentHealth = 25u;
		in.targetMaxHealth = 100u;
		in.targetStateFlags = 0u;
		in.flags = engine::server::kCombatEventFlagCrit;

		const std::vector<std::byte> packet = engine::server::EncodeCombatEvent(in);
		assert(!packet.empty());

		engine::server::CombatEventMessage out{};
		assert(engine::server::DecodeCombatEvent(packet, out));
		assert(out.attackerEntityId == in.attackerEntityId);
		assert(out.targetEntityId == in.targetEntityId);
		assert(out.damage == 75u);
		assert(out.targetCurrentHealth == 25u);
		assert(out.targetMaxHealth == 100u);
		assert(out.flags == engine::server::kCombatEventFlagCrit);

		// Raté : damage 0 + bit miss.
		in.damage = 0u;
		in.flags = engine::server::kCombatEventFlagMiss;
		const std::vector<std::byte> missPacket = engine::server::EncodeCombatEvent(in);
		assert(engine::server::DecodeCombatEvent(missPacket, out));
		assert(out.damage == 0u);
		assert((out.flags & engine::server::kCombatEventFlagMiss) != 0u);

		// Paquet tronqué (ancien format 32 octets) rejeté.
		std::vector<std::byte> truncated = missPacket;
		truncated.resize(truncated.size() - 4u);
		assert(!engine::server::DecodeCombatEvent(truncated, out));
		std::puts("[OK] TestCombatEventRoundTripWithFlags");
	}

	/// Combat SP2 — l'AttackRequest gagne son encodeur côté client : round-trip
	/// contre le décodeur serveur existant.
	void TestAttackRequestRoundTrip()
	{
		engine::server::AttackRequestMessage in{};
		in.clientId = 7u;
		in.targetEntityId = 0x300000005ull;

		const std::vector<std::byte> packet = engine::server::EncodeAttackRequest(in);
		assert(!packet.empty());

		engine::server::AttackRequestMessage out{};
		assert(engine::server::DecodeAttackRequest(packet, out));
		assert(out.clientId == 7u);
		assert(out.targetEntityId == 0x300000005ull);
		std::puts("[OK] TestAttackRequestRoundTrip");
	}

	/// Combat SP2 — round-trip RespawnRequest + rejet d'un payload tronqué.
	void TestRespawnRequestRoundTrip()
	{
		engine::server::RespawnRequestMessage in{};
		in.clientId = 42u;
		in.destination = engine::server::kRespawnDestinationInn; // wire v13

		const std::vector<std::byte> packet = engine::server::EncodeRespawnRequest(in);
		assert(!packet.empty());

		engine::server::RespawnRequestMessage out{};
		assert(engine::server::DecodeRespawnRequest(packet, out));
		assert(out.clientId == 42u);
		assert(out.destination == engine::server::kRespawnDestinationInn);

		std::vector<std::byte> truncated = packet;
		truncated.resize(truncated.size() - 2u);
		assert(!engine::server::DecodeRespawnRequest(truncated, out));

		// Destination hors domaine rejetée (octet 4 du payload, après header 8 o).
		std::vector<std::byte> badDestination = packet;
		badDestination[8 + 4] = static_cast<std::byte>(7);
		assert(!engine::server::DecodeRespawnRequest(badDestination, out));
		std::puts("[OK] TestRespawnRequestRoundTrip");
	}

	/// Combat SP3 (wire v11) — round-trips des messages sorts/auras + rejets tronqués.
	void TestCastRequestRoundTrip()
	{
		engine::server::CastRequestMessage in{};
		in.clientId = 7u;
		in.targetEntityId = 0x300000005ull;
		in.spellId = "melee_frappe_brutale";

		const std::vector<std::byte> packet = engine::server::EncodeCastRequest(in);
		assert(!packet.empty());

		engine::server::CastRequestMessage out{};
		assert(engine::server::DecodeCastRequest(packet, out));
		assert(out.clientId == 7u);
		assert(out.targetEntityId == 0x300000005ull);
		assert(out.spellId == "melee_frappe_brutale");

		std::vector<std::byte> truncated = packet;
		truncated.resize(truncated.size() - 3u);
		assert(!engine::server::DecodeCastRequest(truncated, out));
		std::puts("[OK] TestCastRequestRoundTrip");
	}

	void TestSetActionBarLayoutRoundTrip()
	{
		engine::server::SetActionBarLayoutMessage in{};
		in.clientId = 42u;
		in.slots[0] = "lanceur_trait_de_feu";
		in.slots[1] = "lanceur_nova";
		in.slots[9] = "lanceur_brulure";
		// slots 2..8 restent vides ("")

		const std::vector<std::byte> packet = engine::server::EncodeSetActionBarLayout(in);
		assert(!packet.empty());

		engine::server::SetActionBarLayoutMessage out{};
		assert(engine::server::DecodeSetActionBarLayout(packet, out));
		assert(out.clientId == 42u);
		assert(out.slots[0] == "lanceur_trait_de_feu");
		assert(out.slots[1] == "lanceur_nova");
		assert(out.slots[2].empty());
		assert(out.slots[9] == "lanceur_brulure");

		std::vector<std::byte> truncated = packet;
		truncated.resize(truncated.size() - 2u);
		assert(!engine::server::DecodeSetActionBarLayout(truncated, out));
		std::puts("[OK] TestSetActionBarLayoutRoundTrip");
	}

	void TestActionBarLayoutUpdateRoundTrip()
	{
		engine::server::ActionBarLayoutUpdateMessage in{};
		in.clientId = 7u;
		in.slots[0] = "melee_frappe_brutale";
		in.slots[3] = "melee_cri_de_guerre";

		const std::vector<std::byte> packet = engine::server::EncodeActionBarLayoutUpdate(in);
		assert(!packet.empty());

		engine::server::ActionBarLayoutUpdateMessage out{};
		assert(engine::server::DecodeActionBarLayoutUpdate(packet, out));
		assert(out.clientId == 7u);
		assert(out.slots[0] == "melee_frappe_brutale");
		assert(out.slots[3] == "melee_cri_de_guerre");
		assert(out.slots[1].empty());

		std::vector<std::byte> truncated = packet;
		truncated.resize(truncated.size() - 2u);
		assert(!engine::server::DecodeActionBarLayoutUpdate(truncated, out));
		std::puts("[OK] TestActionBarLayoutUpdateRoundTrip");
	}

	/// Ceinture v2 (2026-07-20) — kinds 99/100 count-prefixed : round-trip à
	/// taille variable (4 et 12 slots), rejet count > 12, rejet tronqué.
	void TestBeltLayoutRoundTripVariableSize()
	{
		// 4 slots (défaut sans ceinture équipée).
		engine::server::SetBeltLayoutMessage in{};
		in.clientId = 42u;
		in.slots = { "item:6002", "", "item:5101", "" };
		const std::vector<std::byte> packet = engine::server::EncodeSetBeltLayout(in);
		assert(!packet.empty());
		engine::server::SetBeltLayoutMessage out{};
		assert(engine::server::DecodeSetBeltLayout(packet, out));
		assert(out.clientId == 42u);
		assert(out.slots.size() == 4u);
		assert(out.slots[0] == "item:6002");
		assert(out.slots[1].empty());
		assert(out.slots[2] == "item:5101");

		// 12 slots (grande ceinture équipée) — kind 100.
		engine::server::BeltLayoutUpdateMessage big{};
		big.clientId = 7u;
		big.slots.assign(12u, std::string{});
		big.slots[11] = "item:6003";
		const std::vector<std::byte> bigPacket = engine::server::EncodeBeltLayoutUpdate(big);
		engine::server::BeltLayoutUpdateMessage bigOut{};
		assert(engine::server::DecodeBeltLayoutUpdate(bigPacket, bigOut));
		assert(bigOut.slots.size() == 12u);
		assert(bigOut.slots[11] == "item:6003");

		// count > 12 : encodé volontairement à 13 → rejeté au décodage.
		engine::server::SetBeltLayoutMessage tooBig{};
		tooBig.clientId = 1u;
		tooBig.slots.assign(13u, std::string{});
		const std::vector<std::byte> tooBigPacket = engine::server::EncodeSetBeltLayout(tooBig);
		engine::server::SetBeltLayoutMessage rejected{};
		assert(!engine::server::DecodeSetBeltLayout(tooBigPacket, rejected));

		// Tronqué : rejeté proprement.
		std::vector<std::byte> truncated = packet;
		truncated.resize(truncated.size() - 2u);
		assert(!engine::server::DecodeSetBeltLayout(truncated, out));
		std::puts("[OK] TestBeltLayoutRoundTripVariableSize");
	}

	void TestClassProgressionUpdateRoundTrip()
	{
		engine::server::ClassProgressionUpdateMessage in{};
		in.clientId = 55u;
		in.classId  = "guerrier";
		in.knownSkillIds = { "frappe_puissante", "cri_de_guerre", "bouclier_de_fer" };

		const std::vector<std::byte> packet = engine::server::EncodeClassProgressionUpdate(in);
		assert(!packet.empty());

		engine::server::ClassProgressionUpdateMessage out{};
		assert(engine::server::DecodeClassProgressionUpdate(packet, out));
		assert(out.clientId == 55u);
		assert(out.classId == "guerrier");
		assert(out.knownSkillIds.size() == 3u);
		assert(out.knownSkillIds[0] == "frappe_puissante");
		assert(out.knownSkillIds[1] == "cri_de_guerre");
		assert(out.knownSkillIds[2] == "bouclier_de_fer");

		// Paquet tronqué rejeté.
		std::vector<std::byte> truncated = packet;
		truncated.resize(truncated.size() - 3u);
		assert(!engine::server::DecodeClassProgressionUpdate(truncated, out));
		std::puts("[OK] TestClassProgressionUpdateRoundTrip");
	}

	void TestChooseClassSkillRequestRoundTrip()
	{
		engine::server::ChooseClassSkillRequestMessage in{};
		in.clientId = 12u;
		in.level    = 5u;
		in.skillId  = "frappe_puissante";

		const std::vector<std::byte> packet = engine::server::EncodeChooseClassSkillRequest(in);
		assert(!packet.empty());

		engine::server::ChooseClassSkillRequestMessage out{};
		assert(engine::server::DecodeChooseClassSkillRequest(packet, out));
		assert(out.clientId == 12u);
		assert(out.level    == 5u);
		assert(out.skillId  == "frappe_puissante");

		// Paquet tronqué rejeté.
		std::vector<std::byte> truncated = packet;
		truncated.resize(truncated.size() - 2u);
		assert(!engine::server::DecodeChooseClassSkillRequest(truncated, out));
		std::puts("[OK] TestChooseClassSkillRequestRoundTrip");
	}

	void TestResourceUpdateRoundTrip()
	{
		engine::server::ResourceUpdateMessage in{};
		in.clientId = 7u;
		in.currentResource = 120u;
		in.maxResource = 200u;

		const std::vector<std::byte> packet = engine::server::EncodeResourceUpdate(in);
		engine::server::ResourceUpdateMessage out{};
		assert(engine::server::DecodeResourceUpdate(packet, out));
		assert(out.clientId == 7u && out.currentResource == 120u && out.maxResource == 200u);

		std::vector<std::byte> truncated = packet;
		truncated.resize(truncated.size() - 2u);
		assert(!engine::server::DecodeResourceUpdate(truncated, out));
		std::puts("[OK] TestResourceUpdateRoundTrip");
	}

	void TestCastBarUpdateRoundTrip()
	{
		engine::server::CastBarUpdateMessage in{};
		in.clientId = 7u;
		in.status = engine::server::kCastBarStatusStart;
		in.durationMs = 1500u;
		in.spellId = "distance_tir_vise";

		const std::vector<std::byte> packet = engine::server::EncodeCastBarUpdate(in);
		engine::server::CastBarUpdateMessage out{};
		assert(engine::server::DecodeCastBarUpdate(packet, out));
		assert(out.status == engine::server::kCastBarStatusStart);
		assert(out.durationMs == 1500u);
		assert(out.spellId == "distance_tir_vise");

		// status hors domaine rejeté. Offset absolu = header 8 octets (magic u32
		// + version u16 + kind u16, cf. kHeaderSize) + 4 (clientId) = 12.
		std::vector<std::byte> badStatus = packet;
		badStatus[8 + 4] = static_cast<std::byte>(9);
		assert(!engine::server::DecodeCastBarUpdate(badStatus, out));
		std::puts("[OK] TestCastBarUpdateRoundTrip");
	}

	void TestAuraUpdateRoundTrip()
	{
		engine::server::AuraUpdateMessage in{};
		in.targetEntityId = 0x300000005ull;
		engine::server::AuraWireEntry dot{};
		dot.spellId = "melee_entaille";
		dot.effectType = 1u; // DamageOverTime
		dot.remainingMs = 6000u;
		dot.stacks = 1u;
		in.auras.push_back(dot);
		engine::server::AuraWireEntry slow{};
		slow.spellId = "tank_coup_de_bouclier";
		slow.effectType = 7u; // SlowMobPercent
		slow.remainingMs = 4000u;
		slow.stacks = 1u;
		in.auras.push_back(slow);

		const std::vector<std::byte> packet = engine::server::EncodeAuraUpdate(in);
		engine::server::AuraUpdateMessage out{};
		assert(engine::server::DecodeAuraUpdate(packet, out));
		assert(out.targetEntityId == in.targetEntityId);
		assert(out.auras.size() == 2u);
		assert(out.auras[0].spellId == "melee_entaille" && out.auras[0].remainingMs == 6000u);
		assert(out.auras[1].effectType == 7u);

		// Liste vide : valide (toutes les auras expirées → le client purge).
		engine::server::AuraUpdateMessage empty{};
		empty.targetEntityId = 42u;
		const std::vector<std::byte> emptyPacket = engine::server::EncodeAuraUpdate(empty);
		assert(engine::server::DecodeAuraUpdate(emptyPacket, out));
		assert(out.targetEntityId == 42u && out.auras.empty());

		std::vector<std::byte> truncated = packet;
		truncated.resize(truncated.size() - 1u);
		assert(!engine::server::DecodeAuraUpdate(truncated, out));
		std::puts("[OK] TestAuraUpdateRoundTrip");
	}

	/// Combat SP4 (wire v12) — round-trip de la table de menace + liste vide + tronqué.
	void TestThreatUpdateRoundTrip()
	{
		engine::server::ThreatUpdateMessage in{};
		in.mobEntityId = 0x300000005ull;
		in.entries.push_back(engine::server::ThreatWireEntry{ 0x200000001ull, 450u });
		in.entries.push_back(engine::server::ThreatWireEntry{ 0x200000002ull, 120u });

		const std::vector<std::byte> packet = engine::server::EncodeThreatUpdate(in);
		engine::server::ThreatUpdateMessage out{};
		assert(engine::server::DecodeThreatUpdate(packet, out));
		assert(out.mobEntityId == in.mobEntityId);
		assert(out.entries.size() == 2u);
		assert(out.entries[0].playerEntityId == 0x200000001ull && out.entries[0].threatValue == 450u);
		assert(out.entries[1].threatValue == 120u);

		// Liste vide = effacement côté client : valide.
		engine::server::ThreatUpdateMessage empty{};
		empty.mobEntityId = 42u;
		assert(engine::server::DecodeThreatUpdate(engine::server::EncodeThreatUpdate(empty), out));
		assert(out.mobEntityId == 42u && out.entries.empty());

		std::vector<std::byte> truncated = packet;
		truncated.resize(truncated.size() - 4u);
		assert(!engine::server::DecodeThreatUpdate(truncated, out));
		std::puts("[OK] TestThreatUpdateRoundTrip");
	}

	/// Combat SP3 (wire v11) — PlayerStats porte désormais profileId en queue.
	void TestPlayerStatsRoundTripWithProfile()
	{
		engine::server::PlayerStatsMessage in{};
		in.clientId = 7u;
		in.maxHealth = 250u;
		in.resource = 120u;
		in.damage = 22u;
		in.accuracy = 82.5f;
		in.resourceKey = "ferveur";
		in.profileId = "sacre";

		const std::vector<std::byte> packet = engine::server::EncodePlayerStats(in);
		engine::server::PlayerStatsMessage out{};
		assert(engine::server::DecodePlayerStats(packet, out));
		assert(out.clientId == 7u && out.maxHealth == 250u && out.resource == 120u);
		assert(out.resourceKey == "ferveur");
		assert(out.profileId == "sacre");
		std::puts("[OK] TestPlayerStatsRoundTripWithProfile");
	}

	/// Correction SP1 — round-trip ForcePosition + rejets (tronqué, reason hors domaine).
	void TestForcePositionRoundTrip()
	{
		engine::server::ForcePositionMessage in{};
		in.clientId = 7u;
		in.positionX = 96.0f;
		in.positionY = 1.5f;
		in.positionZ = 96.0f;
		in.yawRadians = 1.57f;
		in.reason = engine::server::kForcePositionReasonRespawn;

		const std::vector<std::byte> packet = engine::server::EncodeForcePosition(in);
		engine::server::ForcePositionMessage out{};
		assert(engine::server::DecodeForcePosition(packet, out));
		assert(out.clientId == 7u);
		assert(out.positionX == 96.0f && out.positionY == 1.5f && out.positionZ == 96.0f);
		assert(out.yawRadians == 1.57f);
		assert(out.reason == engine::server::kForcePositionReasonRespawn);

		// Paquet tronqué rejeté.
		std::vector<std::byte> truncated = packet;
		truncated.resize(truncated.size() - 2u);
		assert(!engine::server::DecodeForcePosition(truncated, out));

		// Reason hors domaine rejeté (octet 20 du payload, après le header 8 o).
		std::vector<std::byte> badReason = packet;
		badReason[8 + 20] = static_cast<std::byte>(9);
		assert(!engine::server::DecodeForcePosition(badReason, out));
		std::puts("[OK] TestForcePositionRoundTrip");
	}

	/// Validation v12 — round-trip LootNotify (butin auto) + rejet tronqué.
	void TestLootNotifyRoundTrip()
	{
		engine::server::LootNotifyMessage in{};
		in.clientId = 7u;
		in.items.push_back(engine::server::ItemStack{ 2001u, 1u });
		in.items.push_back(engine::server::ItemStack{ 2002u, 3u });

		const std::vector<std::byte> packet = engine::server::EncodeLootNotify(in);
		engine::server::LootNotifyMessage out{};
		assert(engine::server::DecodeLootNotify(packet, out));
		assert(out.clientId == 7u);
		assert(out.items.size() == 2u);
		assert(out.items[0].itemId == 2001u && out.items[0].quantity == 1u);
		assert(out.items[1].itemId == 2002u && out.items[1].quantity == 3u);

		// Paquet tronqué rejeté (count annonce 2 objets, payload amputé).
		std::vector<std::byte> truncated = packet;
		truncated.resize(truncated.size() - 3u);
		assert(!engine::server::DecodeLootNotify(truncated, out));
		std::puts("[OK] TestLootNotifyRoundTrip");
	}

	/// SP1 quêtes — round-trip QuestAcceptRequest (client→serveur) + rejet tronqué.
	void TestQuestAcceptRequestRoundTrip()
	{
		engine::server::QuestAcceptRequestMessage in{};
		in.clientId = 7u;
		in.questId = "q1";
		in.giverTargetId = "npc:marn";

		const std::vector<std::byte> packet = engine::server::EncodeQuestAcceptRequest(in);
		assert(!packet.empty());

		engine::server::QuestAcceptRequestMessage out{};
		assert(engine::server::DecodeQuestAcceptRequest(packet, out));
		assert(out.clientId == 7u);
		assert(out.questId == "q1");
		assert(out.giverTargetId == "npc:marn");

		std::vector<std::byte> truncated = packet;
		truncated.resize(truncated.size() - 1u);
		assert(!engine::server::DecodeQuestAcceptRequest(truncated, out));
		std::puts("[OK] TestQuestAcceptRequestRoundTrip");
	}

	/// SP1 quêtes — round-trip QuestTurnInRequest (client→serveur) + rejet tronqué.
	void TestQuestTurnInRequestRoundTrip()
	{
		engine::server::QuestTurnInRequestMessage in{};
		in.clientId = 9u;
		in.questId = "q2";
		in.npcTargetId = "npc:guard";

		const std::vector<std::byte> packet = engine::server::EncodeQuestTurnInRequest(in);
		assert(!packet.empty());

		engine::server::QuestTurnInRequestMessage out{};
		assert(engine::server::DecodeQuestTurnInRequest(packet, out));
		assert(out.clientId == 9u);
		assert(out.questId == "q2");
		assert(out.npcTargetId == "npc:guard");

		std::vector<std::byte> truncated = packet;
		truncated.resize(truncated.size() - 1u);
		assert(!engine::server::DecodeQuestTurnInRequest(truncated, out));
		std::puts("[OK] TestQuestTurnInRequestRoundTrip");
	}

	/// SP1 quêtes — round-trip QuestGiverList (serveur→client) : 2 entrées (offer + turnin),
	/// liste vide valide, et rejet d'un paquet tronqué.
	void TestQuestGiverListRoundTrip()
	{
		engine::server::QuestGiverListMessage in{};
		in.clientId = 3u;
		in.npcTargetId = "npc:marn";
		in.entries.push_back(engine::server::QuestGiverEntry{ "q1", 0u });
		in.entries.push_back(engine::server::QuestGiverEntry{ "q2", 1u });

		const std::vector<std::byte> packet = engine::server::EncodeQuestGiverList(in);
		assert(!packet.empty());

		engine::server::QuestGiverListMessage out{};
		assert(engine::server::DecodeQuestGiverList(packet, out));
		assert(out.clientId == 3u);
		assert(out.npcTargetId == "npc:marn");
		assert(out.entries.size() == 2u);
		assert(out.entries[0].questId == "q1" && out.entries[0].role == 0u);
		assert(out.entries[1].questId == "q2" && out.entries[1].role == 1u);

		// Liste vide : valide (PNJ sans quête à offrir/rendre pour ce joueur).
		engine::server::QuestGiverListMessage empty{};
		empty.clientId = 3u;
		empty.npcTargetId = "npc:marn";
		assert(engine::server::DecodeQuestGiverList(engine::server::EncodeQuestGiverList(empty), out));
		assert(out.entries.empty());

		std::vector<std::byte> truncated = packet;
		truncated.resize(truncated.size() - 1u);
		assert(!engine::server::DecodeQuestGiverList(truncated, out));
		std::puts("[OK] TestQuestGiverListRoundTrip");
	}

	// Chantier 2 SP-A — équipement.
	void TestEquipRequestRoundTrip()
	{
		engine::server::EquipRequestMessage msg{};
		msg.clientId = 7u;
		msg.itemId = 2004u;
		std::vector<std::byte> packet = engine::server::EncodeEquipRequest(msg);

		engine::server::EquipRequestMessage out{};
		assert(engine::server::DecodeEquipRequest(packet, out));
		assert(out.clientId == 7u);
		assert(out.itemId == 2004u);

		std::vector<std::byte> truncated = packet;
		truncated.resize(truncated.size() - 1u);
		assert(!engine::server::DecodeEquipRequest(truncated, out));
		std::puts("[OK] TestEquipRequestRoundTrip");
	}

	void TestUnequipRequestRoundTrip()
	{
		engine::server::UnequipRequestMessage msg{};
		msg.clientId = 12u;
		msg.slot = 6u; // MainHand
		std::vector<std::byte> packet = engine::server::EncodeUnequipRequest(msg);

		engine::server::UnequipRequestMessage out{};
		assert(engine::server::DecodeUnequipRequest(packet, out));
		assert(out.clientId == 12u);
		assert(out.slot == 6u);

		std::vector<std::byte> truncated = packet;
		truncated.resize(truncated.size() - 1u);
		assert(!engine::server::DecodeUnequipRequest(truncated, out));
		std::puts("[OK] TestUnequipRequestRoundTrip");
	}

	void TestEquipmentUpdateRoundTrip()
	{
		engine::server::EquipmentUpdateMessage msg{};
		msg.clientId = 42u;
		std::vector<engine::server::EquipmentEntry> entries = {
			{1u, 2001u}, // Head
			{2u, 2002u}, // Chest
			{6u, 2004u}, // MainHand
		};
		std::vector<std::byte> packet = engine::server::EncodeEquipmentUpdate(msg, entries);

		engine::server::EquipmentUpdateMessage out{};
		std::vector<engine::server::EquipmentEntry> outEntries;
		assert(engine::server::DecodeEquipmentUpdate(packet, out, outEntries));
		assert(out.clientId == 42u);
		assert(outEntries.size() == 3u);
		assert(outEntries[0].slot == 1u && outEntries[0].itemId == 2001u);
		assert(outEntries[1].slot == 2u && outEntries[1].itemId == 2002u);
		assert(outEntries[2].slot == 6u && outEntries[2].itemId == 2004u);

		// Snapshot vide (tout déséquipé) : valide, 0 entrée.
		engine::server::EquipmentUpdateMessage empty{};
		empty.clientId = 5u;
		assert(engine::server::DecodeEquipmentUpdate(
			engine::server::EncodeEquipmentUpdate(empty, {}), out, outEntries));
		assert(outEntries.empty());

		std::vector<std::byte> truncated = packet;
		truncated.resize(truncated.size() - 1u);
		assert(!engine::server::DecodeEquipmentUpdate(truncated, out, outEntries));
		std::puts("[OK] TestEquipmentUpdateRoundTrip");
	}

int main()
{
	TestInputRoundTrip();
	TestInputRejectsTruncated();
	TestGoodbyeRoundTrip();
	TestHelloRoundTrip();
	TestSnapshotRoundTripWithPlayerClientId();
	TestSnapshotRejectsTruncatedPayload();
	TestSnapshotRoundTripWithChunking();
	TestSnapshotMonoChunkDefaultsRoundTrip();
	TestCombatEventRoundTripWithFlags();
	TestAttackRequestRoundTrip();
	TestRespawnRequestRoundTrip();
	TestCastRequestRoundTrip();
	TestSetActionBarLayoutRoundTrip();
	TestBeltLayoutRoundTripVariableSize();
	TestActionBarLayoutUpdateRoundTrip();
	TestClassProgressionUpdateRoundTrip();
	TestChooseClassSkillRequestRoundTrip();
	TestResourceUpdateRoundTrip();
	TestCastBarUpdateRoundTrip();
	TestAuraUpdateRoundTrip();
	TestThreatUpdateRoundTrip();
	TestPlayerStatsRoundTripWithProfile();
	TestForcePositionRoundTrip();
	TestLootNotifyRoundTrip();
	TestQuestAcceptRequestRoundTrip();
	TestQuestTurnInRequestRoundTrip();
	TestQuestGiverListRoundTrip();
	TestEquipRequestRoundTrip();
	TestUnequipRequestRoundTrip();
	TestEquipmentUpdateRoundTrip();
	std::puts("All ServerProtocol tests passed");
	return 0;
}
