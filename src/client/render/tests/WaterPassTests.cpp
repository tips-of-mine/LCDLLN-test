// src/client/render/tests/WaterPassTests.cpp
#include "src/client/render/WaterPass.h"

#include <cstddef>
#include <cstdio>
#if defined(_MSC_VER)
#pragma warning(disable : 4127) // REQUIRE() sur expressions constantes (sizeof/offsetof) dans les tests
#endif

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::render::WaterPassPushConstants;

	void Test_WaterPassPushConstants_Is128Bytes()
	{
		REQUIRE(sizeof(WaterPassPushConstants) == 128);
	}

	void Test_WaterPassPushConstants_FieldOffsets_MatchSpec()
	{
		using PC = WaterPassPushConstants;
		REQUIRE(offsetof(PC, viewProj)            ==   0);
		REQUIRE(offsetof(PC, cameraPos)           ==  64);
		REQUIRE(offsetof(PC, timeSeconds)         ==  76);
		REQUIRE(offsetof(PC, bottomColor)         ==  80);
		REQUIRE(offsetof(PC, turbidity)           ==  92);
		REQUIRE(offsetof(PC, flowDirection)       ==  96);
		REQUIRE(offsetof(PC, flowSpeed)           == 104);
		REQUIRE(offsetof(PC, refractionAmount)    == 108);
		REQUIRE(offsetof(PC, fresnelPower)        == 112);
		REQUIRE(offsetof(PC, reflectionStrength)  == 116);
		REQUIRE(offsetof(PC, screenSize)          == 120);
	}
}

int main()
{
	Test_WaterPassPushConstants_Is128Bytes();
	Test_WaterPassPushConstants_FieldOffsets_MatchSpec();

	if (g_failed == 0)
	{
		std::printf("All WaterPass layout tests passed.\n");
		return 0;
	}
	std::fprintf(stderr, "%d test(s) failed.\n", g_failed);
	return 1;
}
