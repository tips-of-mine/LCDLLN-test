// src/client/world/water/tests/WaterMeshBuilderTests.cpp
#include "src/client/world/water/WaterMeshBuilder.h"

#include <cmath>
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
	using engine::world::water::WaterMeshCpu;
	using engine::world::water::BuildLakeMesh;
	using engine::world::water::BuildRiverMesh;
	using engine::math::Vec3;

	bool ApproxEq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

	void Test_BuildLakeMesh_ConvexQuad_Produces2Triangles()
	{
		LakeInstance lake;
		lake.waterLevelY = 5.0f;
		lake.polygon = { {0,5,0}, {2,5,0}, {2,5,2}, {0,5,2} };  // CCW square
		WaterMeshCpu mesh; std::string err;
		REQUIRE(BuildLakeMesh(lake, mesh, err));
		REQUIRE(mesh.vertices.size() == 4);
		REQUIRE(mesh.indices.size() == 6);  // 2 triangles
		for (const auto& v : mesh.vertices)
			REQUIRE(ApproxEq(v.position.y, 5.0f));
	}

	void Test_BuildLakeMesh_ConvexPentagon_Produces3Triangles()
	{
		LakeInstance lake;
		lake.waterLevelY = 0.0f;
		// Pentagon CCW
		const float r = 1.0f;
		for (int i = 0; i < 5; ++i)
		{
			const float t = static_cast<float>(i) / 5.0f * 6.28318f;
			lake.polygon.push_back({ std::cos(t) * r, 0.0f, std::sin(t) * r });
		}
		WaterMeshCpu mesh; std::string err;
		REQUIRE(BuildLakeMesh(lake, mesh, err));
		REQUIRE(mesh.vertices.size() == 5);
		REQUIRE(mesh.indices.size() == 9);  // 3 triangles
	}

	void Test_BuildLakeMesh_ConcaveLShape_ProducesCorrectTris()
	{
		LakeInstance lake;
		lake.waterLevelY = 0.0f;
		// L-shape CCW : 6 vertices, concave at (2,1)
		lake.polygon = {
			{0,0,0}, {2,0,0}, {2,0,1},
			{1,0,1}, {1,0,2}, {0,0,2},
		};
		WaterMeshCpu mesh; std::string err;
		REQUIRE(BuildLakeMesh(lake, mesh, err));
		REQUIRE(mesh.vertices.size() == 6);
		REQUIRE(mesh.indices.size() == 12);  // 4 triangles
	}

	void Test_BuildRiverMesh_4Nodes_Produces6Quads()
	{
		RiverInstance river;
		river.nodes = {
			RiverNode{ Vec3{0,0,0}, 2.0f, 1.0f },
			RiverNode{ Vec3{2,0,0}, 2.0f, 1.0f },
			RiverNode{ Vec3{4,0,0}, 2.0f, 1.0f },
			RiverNode{ Vec3{6,0,0}, 2.0f, 1.0f },
		};
		WaterMeshCpu mesh; std::string err;
		REQUIRE(BuildRiverMesh(river, mesh, err));
		REQUIRE(mesh.vertices.size() == 8);   // 2 par node
		REQUIRE(mesh.indices.size() == 18);   // 3 segments × 2 triangles × 3 indices
		// Y de chaque vertex = node.y = 0
		for (const auto& v : mesh.vertices)
			REQUIRE(ApproxEq(v.position.y, 0.0f));
	}

	void Test_BuildLakeMesh_TooFewVertices_Fails()
	{
		LakeInstance lake;
		lake.polygon = { {0,0,0}, {1,0,0} };  // 2 vertices
		WaterMeshCpu mesh; std::string err;
		REQUIRE(!BuildLakeMesh(lake, mesh, err));
		REQUIRE(err.find(">= 3") != std::string::npos);
	}

	/// v1.0.133 — vasque de 16 sommets loin de l'origine de rendu (300 km) : l'aire
	/// calculée en float y changeait de signe et le lac n'était pas maillé.
	LakeInstance FarStarLake(bool clockwise, bool closingDuplicate)
	{
		LakeInstance lake;
		const float cx = 300000.0f, cz = -150000.0f;
		for (int k = 0; k < 16; ++k)
		{
			const int i = clockwise ? 15 - k : k;
			const double a = 6.283185307179586 * i / 16.0;
			const double r = 6.5 + 1.2 * std::sin(3.0 * a);
			lake.polygon.push_back(Vec3{ cx + static_cast<float>(r * std::cos(a)), 0.0f, cz + static_cast<float>(r * std::sin(a)) });
		}
		if (closingDuplicate) lake.polygon.push_back(lake.polygon.front());
		lake.waterLevelY = 12.0f;
		return lake;
	}

	void Test_BuildLakeMesh_FarFromOrigin_BothWindings()
	{
		for (int cw = 0; cw < 2; ++cw)
			for (int dup = 0; dup < 2; ++dup)
			{
				WaterMeshCpu mesh; std::string err;
				REQUIRE(BuildLakeMesh(FarStarLake(cw != 0, dup != 0), mesh, err));
				REQUIRE(err.empty());
				REQUIRE(mesh.indices.size() == 14 * 3); // 16 sommets -> 14 triangles
			}
	}

	void Test_BuildLakeMesh_CollinearPoints_Removed()
	{
		LakeInstance lake;
		lake.polygon = { {0,0,0}, {1,0,0}, {2,0,0}, {2,0,2}, {0,0,2} }; // (1,0,0) aligné
		WaterMeshCpu mesh; std::string err;
		REQUIRE(BuildLakeMesh(lake, mesh, err));
		REQUIRE(mesh.indices.size() == 2 * 3);
	}

	void Test_BuildRiverMesh_TooFewNodes_Fails()
	{
		RiverInstance river;
		river.nodes = { RiverNode{ Vec3{0,0,0}, 2.0f, 1.0f } };  // 1 node only
		WaterMeshCpu mesh; std::string err;
		REQUIRE(!BuildRiverMesh(river, mesh, err));
		REQUIRE(err.find(">= 2") != std::string::npos);
	}
}

int main()
{
	Test_BuildLakeMesh_ConvexQuad_Produces2Triangles();
	Test_BuildLakeMesh_ConvexPentagon_Produces3Triangles();
	Test_BuildLakeMesh_ConcaveLShape_ProducesCorrectTris();
	Test_BuildRiverMesh_4Nodes_Produces6Quads();
	Test_BuildLakeMesh_TooFewVertices_Fails();
	Test_BuildRiverMesh_TooFewNodes_Fails();
	Test_BuildLakeMesh_FarFromOrigin_BothWindings();
	Test_BuildLakeMesh_CollinearPoints_Removed();
	return g_failed;
}
