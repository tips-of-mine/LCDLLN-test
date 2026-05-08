// engine/world/water/tests/WaterMeshBuilderTests.cpp
#include "engine/world/water/WaterMeshBuilder.h"

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
}

int main()
{
	Test_BuildLakeMesh_ConvexQuad_Produces2Triangles();
	Test_BuildLakeMesh_ConvexPentagon_Produces3Triangles();
	Test_BuildLakeMesh_ConcaveLShape_ProducesCorrectTris();
	Test_BuildRiverMesh_4Nodes_Produces6Quads();
	Test_BuildLakeMesh_TooFewVertices_Fails();
	return g_failed;
}
