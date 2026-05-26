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
using engine::server::EncodeHello;
using engine::server::EncodeInput;
using engine::server::HelloMessage;
using engine::server::InputMessage;

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
}

int main()
{
	TestInputRoundTrip();
	TestInputRejectsTruncated();
	TestHelloRoundTrip();
	std::puts("All ServerProtocol tests passed");
	return 0;
}
