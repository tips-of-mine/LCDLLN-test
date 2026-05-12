// src/client/world/hazard/tests/HazardVolumesTests.cpp
#include "src/client/world/hazard/HazardVolumes.h"

#include <cstdio>
#include <cstring>

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::math::Vec3;
	using engine::world::hazard::EscapeMode;
	using engine::world::hazard::HazardInstance;
	using engine::world::hazard::HazardScene;
	using engine::world::hazard::HazardShape;
	using engine::world::hazard::HazardType;
	using engine::world::hazard::LoadHazardsBin;
	using engine::world::hazard::PointInHazard;
	using engine::world::hazard::SaveHazardsBin;

	HazardScene MakeFullScene()
	{
		HazardScene s;
		HazardInstance h1{ HazardType::Quicksand, HazardShape::Cylinder,
			Vec3{10, 0, 20}, Vec3{2, 1, 2}, 4.0f, 2.0f,
			0.15f, 1.8f, 0.10f, EscapeMode::MashButton, 0 };
		HazardInstance h2{ HazardType::Bog, HazardShape::Box,
			Vec3{30, 0, 40}, Vec3{3, 1.5f, 3}, 0.0f, 0.0f,
			0.08f, 1.2f, 0.20f, EscapeMode::LateralMove, 0 };
		HazardInstance h3{ HazardType::Tar, HazardShape::Cylinder,
			Vec3{50, 0, 60}, Vec3{2, 1, 2}, 3.0f, 1.5f,
			0.05f, 0.8f, 0.05f, EscapeMode::MashButtonItem, 42 };
		HazardInstance h4{ HazardType::LavaSurface, HazardShape::Box,
			Vec3{70, 0, 80}, Vec3{5, 0.5f, 5}, 0.0f, 0.0f,
			0.0f, 0.0f, 0.0f, EscapeMode::None, 0 };
		s.hazards = { h1, h2, h3, h4 };
		return s;
	}

	void Test_Hazards_RoundtripBin()
	{
		HazardScene src = MakeFullScene();
		std::vector<uint8_t> bytes;
		std::string err;
		REQUIRE(SaveHazardsBin(src, bytes, err));
		REQUIRE(err.empty());

		HazardScene dst;
		REQUIRE(LoadHazardsBin(std::span<const uint8_t>(bytes), dst, err));
		REQUIRE(err.empty());
		REQUIRE(dst.hazards.size() == src.hazards.size());

		for (size_t i = 0; i < src.hazards.size(); ++i)
		{
			const auto& a = src.hazards[i];
			const auto& b = dst.hazards[i];
			REQUIRE(a.type == b.type);
			REQUIRE(a.shape == b.shape);
			REQUIRE(std::memcmp(&a.position, &b.position, sizeof(Vec3)) == 0);
			REQUIRE(std::memcmp(&a.boxHalfExtents, &b.boxHalfExtents, sizeof(Vec3)) == 0);
			REQUIRE(a.cylRadius == b.cylRadius);
			REQUIRE(a.cylHeight == b.cylHeight);
			REQUIRE(a.sinkRateMps == b.sinkRateMps);
			REQUIRE(a.maxDepthMeters == b.maxDepthMeters);
			REQUIRE(a.slowdownMul == b.slowdownMul);
			REQUIRE(a.escapeMode == b.escapeMode);
			REQUIRE(a.requiredItemId == b.requiredItemId);
		}
	}

	void Test_PointInHazard_Cylinder()
	{
		HazardInstance hz{};
		hz.shape = HazardShape::Cylinder;
		hz.position = Vec3{0, 0, 0};
		hz.cylRadius = 3.0f;
		hz.cylHeight = 2.0f;
		REQUIRE(PointInHazard(hz, Vec3{0, 1, 0}));
		REQUIRE(!PointInHazard(hz, Vec3{0, -0.1f, 0}));
		REQUIRE(!PointInHazard(hz, Vec3{0, 2.1f, 0}));
		REQUIRE(!PointInHazard(hz, Vec3{3.1f, 1, 0}));
		REQUIRE(PointInHazard(hz, Vec3{2.9f, 1, 0}));
	}

	void Test_PointInHazard_Box()
	{
		HazardInstance hz{};
		hz.shape = HazardShape::Box;
		hz.position = Vec3{0, 0, 0};
		hz.boxHalfExtents = Vec3{2, 1, 3};
		REQUIRE(PointInHazard(hz, Vec3{0, 0, 0}));
		REQUIRE(PointInHazard(hz, Vec3{1.9f, 0.9f, 2.9f}));
		REQUIRE(!PointInHazard(hz, Vec3{2.1f, 0, 0}));
		REQUIRE(!PointInHazard(hz, Vec3{0, 1.1f, 0}));
		REQUIRE(!PointInHazard(hz, Vec3{0, 0, 3.1f}));
	}
}

int main()
{
	Test_Hazards_RoundtripBin();
	Test_PointInHazard_Cylinder();
	Test_PointInHazard_Box();
	if (g_failed == 0) std::printf("[OK] 3 tests passed\n");
	return g_failed == 0 ? 0 : 1;
}
