// engine/world/collision/tests/ProxyWireframeTests.cpp
#include "engine/world/collision/ProxyWireframe.h"

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

	using engine::world::collision::CollisionProxy;
	using engine::world::collision::ProxyType;
	using engine::world::collision::GenerateWireframeEdges;
	using engine::math::Vec3;

	void Test_Wireframe_Capsule_EdgeCount()
	{
		CollisionProxy p;
		p.type = ProxyType::Capsule;
		p.capsuleA = Vec3{ 0.0f, -1.0f, 0.0f };
		p.capsuleB = Vec3{ 0.0f,  1.0f, 0.0f };
		p.capsuleRadius = 0.3f;

		auto edges = GenerateWireframeEdges(p);
		// 2 cap rings × 16 segments + 4 longitudinal = 36
		REQUIRE(edges.size() == 36);
	}

	void Test_Wireframe_ConvexHull_BoundingBox_12Edges()
	{
		CollisionProxy p;
		p.type = ProxyType::ConvexHull;
		p.vertices = {
			{-1, -1, -1}, { 1, -1, -1}, {-1,  1, -1}, { 1,  1, -1},
			{-1, -1,  1}, { 1, -1,  1}, {-1,  1,  1}, { 1,  1,  1},
		};

		auto edges = GenerateWireframeEdges(p);
		REQUIRE(edges.size() == 12);
	}

	void Test_Wireframe_TriMesh_3EdgesPerTriangle()
	{
		CollisionProxy p;
		p.type = ProxyType::TriMesh;
		p.vertices = {
			{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0},
		};
		p.indices = { 0, 1, 2,  1, 3, 2,  0, 2, 3,  0, 3, 1 };

		auto edges = GenerateWireframeEdges(p);
		REQUIRE(edges.size() == 12);  // 4 tris × 3 edges
	}

	void Test_Wireframe_EdgesNotEmpty()
	{
		CollisionProxy capsule;
		capsule.type = ProxyType::Capsule;
		REQUIRE(!GenerateWireframeEdges(capsule).empty());

		CollisionProxy hull;
		hull.type = ProxyType::ConvexHull;
		hull.vertices = {
			{0,0,0}, {1,0,0}, {0,1,0}, {1,1,0},
			{0,0,1}, {1,0,1}, {0,1,1}, {1,1,1},
		};
		REQUIRE(!GenerateWireframeEdges(hull).empty());

		CollisionProxy trimesh;
		trimesh.type = ProxyType::TriMesh;
		trimesh.vertices = { {0,0,0}, {1,0,0}, {0,1,0} };
		trimesh.indices  = { 0, 1, 2 };
		REQUIRE(!GenerateWireframeEdges(trimesh).empty());
	}
}

int main()
{
	Test_Wireframe_Capsule_EdgeCount();
	Test_Wireframe_ConvexHull_BoundingBox_12Edges();
	Test_Wireframe_TriMesh_3EdgesPerTriangle();
	Test_Wireframe_EdgesNotEmpty();
	return g_failed;
}
