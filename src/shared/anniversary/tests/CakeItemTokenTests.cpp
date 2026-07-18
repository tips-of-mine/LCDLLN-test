/// Tests unitaires CPU pour CakeItemToken (SP3 anniversaires, 2026-07-18) :
/// aller-retour MakeCakeToken/ParseCakeToken, bornes de plage, rejets
/// (spellIds, vide, ids hors plage, non-numérique). Pur CPU, ctest.

#include "src/shared/anniversary/CakeItemToken.h"

#include <cstdio>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using namespace engine::anniversary;

	void Test_RangeAndRoundTrip()
	{
		REQUIRE(IsCakeItemId(5101u));
		REQUIRE(IsCakeItemId(5110u));
		REQUIRE(!IsCakeItemId(5100u));
		REQUIRE(!IsCakeItemId(5111u));
		REQUIRE(!IsCakeItemId(0u));

		for (uint32_t id = kFirstCakeItemId; id < kFirstCakeItemId + kCakeVariantCount; ++id)
		{
			uint32_t parsed = 0u;
			REQUIRE(ParseCakeToken(MakeCakeToken(id), parsed));
			REQUIRE(parsed == id);
		}
		REQUIRE(MakeCakeToken(5101u) == "item:5101");
	}

	void Test_Rejects()
	{
		uint32_t out = 0u;
		REQUIRE(!ParseCakeToken("", out));
		REQUIRE(!ParseCakeToken("melee_frappe_brutale", out));
		REQUIRE(!ParseCakeToken("item:", out));
		REQUIRE(!ParseCakeToken("item:abc", out));
		REQUIRE(!ParseCakeToken("item:5100", out));  // hors plage
		REQUIRE(!ParseCakeToken("item:5111", out));  // hors plage
		REQUIRE(!ParseCakeToken("item:5101x", out)); // suffixe non numérique
		REQUIRE(!ParseCakeToken("Item:5101", out));  // casse stricte
	}
}

int main()
{
	Test_RangeAndRoundTrip();
	Test_Rejects();

	if (g_failed == 0)
	{
		std::printf("[PASS] CakeItemTokenTests\n");
		return 0;
	}
	std::printf("[FAIL] CakeItemTokenTests: %d failure(s)\n", g_failed);
	return 1;
}
