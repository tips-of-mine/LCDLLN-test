// src/client/world/water/tests/WaterSurfacesTests.cpp
#include "src/client/world/water/WaterSurfaces.h"
#include "src/client/world/water/WaterMeshBuilder.h"

#include <cmath>
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

	using engine::world::water::LakeInstance;
	using engine::world::water::RiverInstance;
	using engine::world::water::RiverNode;
	using engine::world::water::WaterScene;
	using engine::world::water::SaveWaterBin;
	using engine::world::water::LoadWaterBin;
	using engine::world::water::ComputeFlowDirections;
	using engine::math::Vec3;

	bool ApproxEq(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }
	bool VecEq(const Vec3& a, const Vec3& b, float eps = 1e-5f)
	{
		return ApproxEq(a.x, b.x, eps) && ApproxEq(a.y, b.y, eps) && ApproxEq(a.z, b.z, eps);
	}

	WaterScene MakeLakeScene()
	{
		WaterScene s;
		LakeInstance lake;
		lake.name = "lake_test";
		lake.polygon = { {0,10,0}, {5,10,0}, {5,10,5}, {2,10,7}, {0,10,5} };
		lake.bottomColor = Vec3{ 0.1f, 0.2f, 0.3f };
		lake.turbidity = 0.5f;
		lake.waterLevelY = 10.0f;
		s.lakes.push_back(std::move(lake));
		return s;
	}

	WaterScene MakeRiverScene()
	{
		WaterScene s;
		RiverInstance r;
		r.name = "river_test";
		r.nodes = {
			RiverNode{ Vec3{ 0, 0, 0}, 3.0f, 1.0f },
			RiverNode{ Vec3{10, 0, 0}, 4.0f, 1.5f },
			RiverNode{ Vec3{20,-1, 5}, 5.0f, 1.5f },
			RiverNode{ Vec3{30,-2,10}, 4.0f, 1.0f },
		};
		s.rivers.push_back(std::move(r));
		return s;
	}

	void Test_Roundtrip_LakeOnly()
	{
		WaterScene src = MakeLakeScene();
		std::vector<uint8_t> bytes; std::string err;
		REQUIRE(SaveWaterBin(src, bytes, err));
		REQUIRE(err.empty());

		WaterScene dst;
		REQUIRE(LoadWaterBin(std::span<const uint8_t>(bytes), dst, err));
		REQUIRE(dst.lakes.size() == 1);
		REQUIRE(dst.rivers.size() == 0);
		REQUIRE(dst.lakes[0].name == src.lakes[0].name);
		REQUIRE(dst.lakes[0].polygon.size() == src.lakes[0].polygon.size());
		REQUIRE(std::memcmp(dst.lakes[0].polygon.data(), src.lakes[0].polygon.data(),
			src.lakes[0].polygon.size() * sizeof(Vec3)) == 0);
		REQUIRE(VecEq(dst.lakes[0].bottomColor, src.lakes[0].bottomColor));
		REQUIRE(ApproxEq(dst.lakes[0].turbidity, src.lakes[0].turbidity));
		REQUIRE(ApproxEq(dst.lakes[0].waterLevelY, src.lakes[0].waterLevelY));
	}

	void Test_Roundtrip_RiverOnly()
	{
		WaterScene src = MakeRiverScene();
		std::vector<uint8_t> bytes; std::string err;
		REQUIRE(SaveWaterBin(src, bytes, err));

		WaterScene dst;
		REQUIRE(LoadWaterBin(std::span<const uint8_t>(bytes), dst, err));
		REQUIRE(dst.lakes.size() == 0);
		REQUIRE(dst.rivers.size() == 1);
		REQUIRE(dst.rivers[0].name == src.rivers[0].name);
		REQUIRE(dst.rivers[0].nodes.size() == 4);
		for (size_t i = 0; i < 4; ++i)
		{
			REQUIRE(VecEq(dst.rivers[0].nodes[i].position, src.rivers[0].nodes[i].position));
			REQUIRE(ApproxEq(dst.rivers[0].nodes[i].widthMeters, src.rivers[0].nodes[i].widthMeters));
			REQUIRE(ApproxEq(dst.rivers[0].nodes[i].depthMeters, src.rivers[0].nodes[i].depthMeters));
		}
	}

	void Test_Roundtrip_LakeAndRiver()
	{
		WaterScene src;
		src.lakes = MakeLakeScene().lakes;
		LakeInstance lake2;
		lake2.name = "lake_pond";
		lake2.polygon = { {-5,2,-5}, {0,2,-5}, {0,2,0}, {-5,2,0} };
		lake2.waterLevelY = 2.0f;
		src.lakes.push_back(std::move(lake2));
		src.rivers = MakeRiverScene().rivers;

		std::vector<uint8_t> bytes; std::string err;
		REQUIRE(SaveWaterBin(src, bytes, err));

		WaterScene dst;
		REQUIRE(LoadWaterBin(std::span<const uint8_t>(bytes), dst, err));
		REQUIRE(dst.lakes.size() == 2);
		REQUIRE(dst.rivers.size() == 1);
		REQUIRE(dst.lakes[0].name == "lake_test");
		REQUIRE(dst.lakes[1].name == "lake_pond");
		REQUIRE(dst.rivers[0].name == "river_test");
	}

	void Test_Load_BadMagic_Fails()
	{
		// Construit un buffer avec header magic invalide
		std::vector<uint8_t> bytes(24 + 8, 0u);
		const uint32_t badMagic = 0xDEADBEEFu;
		std::memcpy(bytes.data(), &badMagic, 4);
		// version, builderVer, engineVer, hash : laissés à 0
		// Counts 0/0
		WaterScene dst; std::string err;
		REQUIRE(!LoadWaterBin(std::span<const uint8_t>(bytes), dst, err));
		REQUIRE(err.find("magic") != std::string::npos);
	}

	void Test_Load_BadContentHash_Fails()
	{
		WaterScene src = MakeLakeScene();
		std::vector<uint8_t> bytes; std::string err;
		REQUIRE(SaveWaterBin(src, bytes, err));
		// Flip un byte du payload (offset 24 + 8 = lakeCount/riverCount frontière)
		bytes[32] ^= 0xFFu;
		WaterScene dst;
		REQUIRE(!LoadWaterBin(std::span<const uint8_t>(bytes), dst, err));
		REQUIRE(err.find("contentHash") != std::string::npos);
	}

	void Test_FlowDirection_AlignsWithSlope()
	{
		// Rivière 3 nodes descendant en +X
		RiverInstance r;
		r.nodes = {
			RiverNode{ Vec3{ 0, 10, 0}, 4.0f, 1.0f },
			RiverNode{ Vec3{10,  5, 0}, 4.0f, 1.0f },
			RiverNode{ Vec3{20,  0, 0}, 4.0f, 1.0f },
		};
		auto flows = ComputeFlowDirections(r);
		REQUIRE(flows.size() == 2);
		// flow[0] et flow[1] devraient pointer en +X (1, 0, 0) — flow XZ uniquement
		REQUIRE(ApproxEq(flows[0].x, 1.0f, 1e-3f));
		REQUIRE(ApproxEq(flows[0].z, 0.0f, 1e-3f));
		REQUIRE(ApproxEq(flows[1].x, 1.0f, 1e-3f));
		REQUIRE(ApproxEq(flows[1].z, 0.0f, 1e-3f));
	}
}

int main()
{
	Test_Roundtrip_LakeOnly();
	Test_Roundtrip_RiverOnly();
	Test_Roundtrip_LakeAndRiver();
	Test_Load_BadMagic_Fails();
	Test_Load_BadContentHash_Fails();
	Test_FlowDirection_AlignsWithSlope();
	return g_failed;
}
