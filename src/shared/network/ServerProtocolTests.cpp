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
		// 2 joueurs avec playerClientId ≠ 0, 1 mob avec playerClientId = 0.
		SnapshotEntity ePlayerA{};
		ePlayerA.entityId = 0x200000001ull;
		ePlayerA.state.positionX = 10.5f;
		ePlayerA.state.positionY = 1.25f;
		ePlayerA.state.positionZ = -3.75f;
		ePlayerA.state.yawRadians = 0.42f;
		ePlayerA.playerClientId = 7u;
		in.push_back(ePlayerA);

		SnapshotEntity ePlayerB{};
		ePlayerB.entityId = 0x200000002ull;
		ePlayerB.state.positionX = 100.0f;
		ePlayerB.state.positionZ = 50.0f;
		ePlayerB.playerClientId = 12u;
		in.push_back(ePlayerB);

		SnapshotEntity eMob{};
		eMob.entityId = 0x300000005ull;
		eMob.state.currentHealth = 80u;
		eMob.state.maxHealth = 100u;
		eMob.playerClientId = 0u; // mob => pas de nameplate
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

		assert(out[1].entityId == ePlayerB.entityId);
		assert(out[1].state.positionX == ePlayerB.state.positionX);
		assert(out[1].playerClientId == 12u);

		assert(out[2].entityId == eMob.entityId);
		assert(out[2].state.currentHealth == 80u);
		assert(out[2].state.maxHealth == 100u);
		assert(out[2].playerClientId == 0u);
		std::puts("[OK] TestSnapshotRoundTripWithPlayerClientId");
	}

	/// TD.4 — un Snapshot dont la taille de payload ne suit pas la nouvelle convention
	/// 52 octets/entité doit être rejeté (defense contre un client/serveur de version
	/// antérieure parlant v3 = 48 octets/entité).
	void TestSnapshotRejectsLegacyV3PayloadSize()
	{
		SnapshotMessage inMsg{};
		inMsg.entityCount = 1u;
		std::vector<SnapshotEntity> in;
		SnapshotEntity e{};
		e.entityId = 0x200000001ull;
		e.playerClientId = 5u;
		in.push_back(e);

		std::vector<std::byte> packet = EncodeSnapshot(inMsg, in);
		// Ampute exactement 4 octets : simule l'absence du playerClientId (taille v3).
		assert(packet.size() >= 4u);
		packet.resize(packet.size() - 4u);

		SnapshotMessage outMsg{};
		std::vector<SnapshotEntity> out;
		assert(!DecodeSnapshot(packet, outMsg, out));
		std::puts("[OK] TestSnapshotRejectsLegacyV3PayloadSize");
	}
}

int main()
{
	TestInputRoundTrip();
	TestInputRejectsTruncated();
	TestHelloRoundTrip();
	TestSnapshotRoundTripWithPlayerClientId();
	TestSnapshotRejectsLegacyV3PayloadSize();
	std::puts("All ServerProtocol tests passed");
	return 0;
}
