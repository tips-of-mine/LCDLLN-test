// src/client/render/tests/WaterMeshGpuTests.cpp
#include "src/client/render/WaterMeshGpu.h"

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

	using engine::world::water::LakeInstance;
	using engine::world::water::RiverInstance;
	using engine::world::water::RiverNode;
	using engine::world::water::WaterScene;
	using engine::math::Vec3;
	using engine::render::BuildDrawInfos;
	using engine::render::WaterInstanceDrawInfo;

	void Test_BuildDrawInfos_EmptyScene_ZeroInstances()
	{
		WaterScene scene;
		std::vector<float> verts;
		std::vector<uint32_t> idx;
		std::vector<WaterInstanceDrawInfo> infos;
		BuildDrawInfos(scene, verts, idx, infos);
		REQUIRE(verts.empty());
		REQUIRE(idx.empty());
		REQUIRE(infos.empty());
	}

	void Test_BuildDrawInfos_OneLake_OneRiver_ProducesTwoInfos()
	{
		WaterScene scene;
		// Lac triangle simple (CCW vu du dessus)
		LakeInstance lake;
		lake.name = "lake0";
		lake.polygon = { Vec3{0,0,0}, Vec3{10,0,0}, Vec3{5,0,10} };
		lake.bottomColor = Vec3{ 0.1f, 0.2f, 0.3f };
		lake.turbidity = 0.5f;
		lake.waterLevelY = 0.0f;
		scene.lakes.push_back(std::move(lake));

		// Riviere 2 noeuds
		RiverInstance river;
		river.name = "river0";
		RiverNode n0; n0.position = Vec3{0,0,20}; n0.widthMeters = 1.0f;
		RiverNode n1; n1.position = Vec3{20,0,20}; n1.widthMeters = 1.0f;
		river.nodes = { n0, n1 };
		scene.rivers.push_back(std::move(river));

		std::vector<float> verts;
		std::vector<uint32_t> idx;
		std::vector<WaterInstanceDrawInfo> infos;
		BuildDrawInfos(scene, verts, idx, infos);

		REQUIRE(infos.size() == 2);
		REQUIRE(infos[0].paramsIndex == 0);  // Lake [0]
		REQUIRE(infos[1].paramsIndex == 1);  // River [0] : index unifie N_lakes + 0 = 1
		// Lac triangle : 3 indices, 3 vertices
		REQUIRE(infos[0].indexCount == 3);
		REQUIRE(infos[0].vertexOffset == 0);
		// Riviere 2 noeuds : 1 segment = 2 triangles = 6 indices, 4 vertices
		REQUIRE(infos[1].indexCount == 6);
		REQUIRE(infos[1].vertexOffset == 3);
		// Verts : (3 lake + 4 river) * 7 floats / vertex = 49 floats
		REQUIRE(verts.size() == 49u);
		// Idx : 3 lake + 6 river = 9 indices
		REQUIRE(idx.size() == 9u);
	}

	void Test_BuildDrawInfos_ParamsIndexOrdering_LakesFirst()
	{
		WaterScene scene;
		LakeInstance lake1, lake2;
		lake1.name = "lake1";
		lake1.polygon = { Vec3{0,0,0}, Vec3{1,0,0}, Vec3{0,0,1} };
		lake1.waterLevelY = 0.0f;
		lake2.name = "lake2";
		lake2.polygon = { Vec3{2,0,0}, Vec3{3,0,0}, Vec3{2,0,1} };
		lake2.waterLevelY = 0.0f;
		scene.lakes = { lake1, lake2 };

		RiverInstance r1;
		r1.name = "r1";
		RiverNode rn0; rn0.position = Vec3{0,0,5}; rn0.widthMeters = 1.0f;
		RiverNode rn1; rn1.position = Vec3{5,0,5}; rn1.widthMeters = 1.0f;
		r1.nodes = { rn0, rn1 };
		scene.rivers = { r1 };

		std::vector<float> verts;
		std::vector<uint32_t> idx;
		std::vector<WaterInstanceDrawInfo> infos;
		BuildDrawInfos(scene, verts, idx, infos);

		REQUIRE(infos.size() == 3);
		// Index unifie : lakes en tete (0..1), rivers ensuite (2).
		REQUIRE(infos[0].paramsIndex == 0);
		REQUIRE(infos[1].paramsIndex == 1);
		REQUIRE(infos[2].paramsIndex == 2);
		// Vertex offsets monotones croissants (concatenation lake1 | lake2 | river0).
		REQUIRE(infos[0].vertexOffset == 0);
		REQUIRE(infos[1].vertexOffset > infos[0].vertexOffset);
		REQUIRE(infos[2].vertexOffset > infos[1].vertexOffset);
	}

	void Test_BuildDrawInfos_FailedLake_ParamsIndexGap()
	{
		WaterScene scene;
		// Lake invalide (2 vertices < 3 requis par BuildLakeMesh) - mesh failed.
		LakeInstance badLake;
		badLake.name = "bad";
		badLake.polygon = { Vec3{0,0,0}, Vec3{1,0,0} };  // < 3 vertices = echec BuildLakeMesh
		badLake.waterLevelY = 0.0f;
		// Lake valide qui suit.
		LakeInstance goodLake;
		goodLake.name = "good";
		goodLake.polygon = { Vec3{2,0,0}, Vec3{3,0,0}, Vec3{2,0,1} };
		goodLake.waterLevelY = 0.0f;
		scene.lakes = { badLake, goodLake };

		std::vector<float> verts;
		std::vector<uint32_t> idx;
		std::vector<WaterInstanceDrawInfo> infos;
		BuildDrawInfos(scene, verts, idx, infos);

		// Seul le bon lake produit un drawInfo, mais paramsIndex doit etre 1
		// (le mauvais lake a consomme l'index 0).
		REQUIRE(infos.size() == 1);
		REQUIRE(infos[0].paramsIndex == 1);
	}
}

int main()
{
	Test_BuildDrawInfos_EmptyScene_ZeroInstances();
	Test_BuildDrawInfos_OneLake_OneRiver_ProducesTwoInfos();
	Test_BuildDrawInfos_ParamsIndexOrdering_LakesFirst();
	Test_BuildDrawInfos_FailedLake_ParamsIndexGap();

	if (g_failed == 0)
	{
		std::printf("All WaterMeshGpu CPU tests passed.\n");
		return 0;
	}
	std::fprintf(stderr, "%d test(s) failed.\n", g_failed);
	return 1;
}
