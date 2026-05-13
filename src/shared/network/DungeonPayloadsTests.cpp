/// Tests round-trip pour M100.43 EnterDungeon{Request,Response} payloads.

#include "src/shared/network/DungeonPayloads.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using namespace engine::network;

	void Test_RequestRoundTrip()
	{
		const auto bytes = BuildEnterDungeonRequestPayload(
			1234567890ULL, "dungeon_starter_keep", 2u);
		REQUIRE(!bytes.empty());

		const auto parsed = ParseEnterDungeonRequestPayload(bytes.data(), bytes.size());
		REQUIRE(parsed.has_value());
		REQUIRE(parsed->characterId == 1234567890ULL);
		REQUIRE(parsed->dungeonTemplateId == "dungeon_starter_keep");
		REQUIRE(parsed->difficulty == 2u);
	}

	void Test_ResponseRoundTripSuccess()
	{
		const auto bytes = BuildEnterDungeonResponsePayload(
			true, 42ULL, "shard-eu-1.lune-noire.fr:7777", kEnterDungeonErrorNone);
		const auto parsed = ParseEnterDungeonResponsePayload(bytes.data(), bytes.size());
		REQUIRE(parsed.has_value());
		REQUIRE(parsed->success == true);
		REQUIRE(parsed->instanceId == 42ULL);
		REQUIRE(parsed->shardEndpoint == "shard-eu-1.lune-noire.fr:7777");
		REQUIRE(parsed->errorCode == kEnterDungeonErrorNone);
	}

	void Test_ResponseRoundTripError()
	{
		const auto bytes = BuildEnterDungeonResponsePayload(
			false, 0ULL, "", kEnterDungeonErrorTemplateNotFound);
		const auto parsed = ParseEnterDungeonResponsePayload(bytes.data(), bytes.size());
		REQUIRE(parsed.has_value());
		REQUIRE(parsed->success == false);
		REQUIRE(parsed->instanceId == 0ULL);
		REQUIRE(parsed->shardEndpoint.empty());
		REQUIRE(parsed->errorCode == kEnterDungeonErrorTemplateNotFound);
	}

	void Test_RequestRejectsDifficultyZero()
	{
		const auto bytes = BuildEnterDungeonRequestPayload(1ULL, "tid", 0u);
		// Builder clamp à kMaxDungeonDifficulty MAX mais conserve 0 ; parser rejette.
		const auto parsed = ParseEnterDungeonRequestPayload(bytes.data(), bytes.size());
		REQUIRE(!parsed.has_value());
	}

	void Test_RequestRejectsDifficultyTooHigh()
	{
		// Builder clamp à kMaxDungeonDifficulty = 5 ; donc on construit la trame à la main.
		std::vector<uint8_t> buf;
		// characterId
		for (int i = 0; i < 8; ++i) buf.push_back(0u);
		// string "tid" (len=3)
		buf.push_back(3u); buf.push_back(0u);
		buf.push_back('t'); buf.push_back('i'); buf.push_back('d');
		// difficulty = 99 (hors range)
		buf.push_back(99u);
		const auto parsed = ParseEnterDungeonRequestPayload(buf.data(), buf.size());
		REQUIRE(!parsed.has_value());
	}

	void Test_RequestRejectsTruncated()
	{
		std::vector<uint8_t> buf(5u, 0u);
		const auto parsed = ParseEnterDungeonRequestPayload(buf.data(), buf.size());
		REQUIRE(!parsed.has_value());
	}
}

int main()
{
	Test_RequestRoundTrip();
	Test_ResponseRoundTripSuccess();
	Test_ResponseRoundTripError();
	Test_RequestRejectsDifficultyZero();
	Test_RequestRejectsDifficultyTooHigh();
	Test_RequestRejectsTruncated();

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[DungeonPayloadsTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[DungeonPayloadsTests] all tests passed\n");
	return 0;
}
