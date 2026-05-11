// Wave 12 — PacketLog tests : couvre Append, Drain, capacity wraparound,
// DrainForConn et le format de FormatEntries. Pas de framework de test,
// juste main() avec asserts et return EXIT_FAILURE en cas d'echec.
// Le binaire est enregistre dans src/CMakeLists.txt comme cible CTest
// `packet_log_tests`.

#include "src/shared/network/PacketLog.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using engine::server::netdebug::FormatEntries;
using engine::server::netdebug::kPreviewBytes;
using engine::server::netdebug::PacketDirection;
using engine::server::netdebug::PacketLog;
using engine::server::netdebug::PacketLogEntry;

namespace
{
	/// Helper : Append une entry minimaliste (pas de payload) en RX.
	void AppendSimple(PacketLog& pl, uint32_t connId, uint16_t opcode,
	                  uint64_t nowMs)
	{
		pl.Append(PacketDirection::RX, connId, opcode,
		          /*requestId=*/0, /*sessionId=*/0,
		          /*payload=*/nullptr, /*payloadSize=*/0, nowMs);
	}

	/// Test 1 : Drain sur log vide -> vector vide.
	void TestEmpty()
	{
		PacketLog pl(4);
		auto v = pl.Drain();
		assert(v.empty());
		assert(pl.Size() == 0);
		assert(pl.Capacity() == 4);
		std::puts("[OK] TestEmpty");
	}

	/// Test 2 : Append 3 entries dans capacite 4 -> Drain renvoie 3 en
	/// ordre chronologique (opcode = 10, 11, 12).
	void TestBelowCapacity()
	{
		PacketLog pl(4);
		AppendSimple(pl, /*connId=*/1, /*opcode=*/10, /*nowMs=*/1000);
		AppendSimple(pl, /*connId=*/1, /*opcode=*/11, /*nowMs=*/1001);
		AppendSimple(pl, /*connId=*/1, /*opcode=*/12, /*nowMs=*/1002);

		auto v = pl.Drain();
		assert(v.size() == 3);
		assert(v[0].opcode == 10);
		assert(v[1].opcode == 11);
		assert(v[2].opcode == 12);
		assert(pl.Size() == 3);
		std::puts("[OK] TestBelowCapacity");
	}

	/// Test 3 : Append 4 entries (= capacite) -> Drain renvoie 4 en ordre.
	void TestExactCapacity()
	{
		PacketLog pl(4);
		for (uint16_t i = 0; i < 4; ++i)
			AppendSimple(pl, /*connId=*/2, /*opcode=*/i, /*nowMs=*/2000 + i);

		auto v = pl.Drain();
		assert(v.size() == 4);
		for (size_t i = 0; i < 4; ++i)
			assert(v[i].opcode == static_cast<uint16_t>(i));
		assert(pl.Size() == 4);
		std::puts("[OK] TestExactCapacity");
	}

	/// Test 4 : Append 6 entries dans capacite 4 -> Drain renvoie les 4
	/// dernieres (opcodes 2, 3, 4, 5) en ordre chronologique. Verifie le
	/// comportement de wraparound.
	void TestWraparound()
	{
		PacketLog pl(4);
		for (uint16_t i = 0; i < 6; ++i)
			AppendSimple(pl, /*connId=*/3, /*opcode=*/i, /*nowMs=*/3000 + i);

		auto v = pl.Drain();
		assert(v.size() == 4);
		assert(v[0].opcode == 2);
		assert(v[1].opcode == 3);
		assert(v[2].opcode == 4);
		assert(v[3].opcode == 5);
		assert(pl.Size() == 4);
		std::puts("[OK] TestWraparound");
	}

	/// Test 5 : Append entries avec connIds mixtes -> DrainForConn filtre
	/// correctement.
	void TestDrainForConn()
	{
		PacketLog pl(8);
		// Intercalle connId=10 et connId=20.
		AppendSimple(pl, 10, /*opcode=*/100, 5000);
		AppendSimple(pl, 20, /*opcode=*/200, 5001);
		AppendSimple(pl, 10, /*opcode=*/101, 5002);
		AppendSimple(pl, 20, /*opcode=*/201, 5003);
		AppendSimple(pl, 10, /*opcode=*/102, 5004);

		auto all = pl.Drain();
		assert(all.size() == 5);

		auto conn10 = pl.DrainForConn(10);
		assert(conn10.size() == 3);
		assert(conn10[0].opcode == 100);
		assert(conn10[1].opcode == 101);
		assert(conn10[2].opcode == 102);

		auto conn20 = pl.DrainForConn(20);
		assert(conn20.size() == 2);
		assert(conn20[0].opcode == 200);
		assert(conn20[1].opcode == 201);

		auto connEmpty = pl.DrainForConn(99);
		assert(connEmpty.empty());

		std::puts("[OK] TestDrainForConn");
	}

	/// Test 6 : Append un payload de 100 octets -> entry.payloadSize == 100,
	/// payloadPreview rempli avec les 64 premiers octets.
	void TestPayloadPreview()
	{
		PacketLog pl(2);

		std::vector<uint8_t> payload(100);
		for (size_t i = 0; i < payload.size(); ++i)
			payload[i] = static_cast<uint8_t>(i & 0xFF);

		pl.Append(PacketDirection::TX, /*connId=*/42, /*opcode=*/0xBEEF,
		          /*requestId=*/7, /*sessionId=*/0xDEADBEEFCAFEull,
		          payload.data(), payload.size(), /*nowMs=*/6000);

		auto v = pl.Drain();
		assert(v.size() == 1);
		assert(v[0].payloadSize == 100);
		assert(v[0].connId == 42);
		assert(v[0].opcode == 0xBEEF);
		assert(v[0].requestId == 7);
		assert(v[0].sessionId == 0xDEADBEEFCAFEull);
		assert(v[0].direction == PacketDirection::TX);

		// Les 64 premiers octets doivent correspondre.
		for (size_t i = 0; i < kPreviewBytes; ++i)
			assert(v[0].payloadPreview[i] == static_cast<uint8_t>(i & 0xFF));

		std::puts("[OK] TestPayloadPreview");
	}

	/// Test 7 : FormatEntries renvoie un texte non vide contenant les
	/// champs attendus (RX/TX, opcode=, connId=).
	void TestFormatEntriesNonEmpty()
	{
		PacketLog pl(4);
		uint8_t payload[3] = { 0xAA, 0xBB, 0xCC };
		pl.Append(PacketDirection::RX, /*connId=*/7, /*opcode=*/123,
		          /*requestId=*/0, /*sessionId=*/0,
		          payload, sizeof(payload), /*nowMs=*/1700000000000ULL);

		auto entries = pl.Drain();
		assert(entries.size() == 1);
		std::string s = FormatEntries(entries);
		assert(!s.empty());

		// Doit contenir le libelle de direction.
		assert(s.find("RX") != std::string::npos);
		// Doit contenir l'opcode formatte.
		assert(s.find("opcode=123") != std::string::npos);
		// Doit contenir le connId.
		assert(s.find("connId=7") != std::string::npos);
		// Doit contenir un preview hex (au moins les premiers octets).
		assert(s.find("AA BB CC") != std::string::npos);

		// FormatEntries sur un vector vide -> string vide.
		std::vector<PacketLogEntry> empty;
		assert(FormatEntries(empty).empty());

		std::puts("[OK] TestFormatEntriesNonEmpty");
	}

	/// Test bonus : capacity == 0 -> log inerte, Append no-op, Drain vide.
	void TestZeroCapacity()
	{
		PacketLog pl(0);
		AppendSimple(pl, 1, 99, 100);
		assert(pl.Drain().empty());
		assert(pl.Size() == 0);
		assert(pl.Capacity() == 0);
		std::puts("[OK] TestZeroCapacity");
	}
}

int main()
{
	TestEmpty();
	TestBelowCapacity();
	TestExactCapacity();
	TestWraparound();
	TestDrainForConn();
	TestPayloadPreview();
	TestFormatEntriesNonEmpty();
	TestZeroCapacity();
	std::puts("[ALL OK] PacketLogTests");
	return EXIT_SUCCESS;
}
