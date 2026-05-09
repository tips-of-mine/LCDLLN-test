// src/client/world/collision/tests/AutoFitProxyTests.cpp
#include "src/client/world/collision/AutoFitProxy.h"

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

	using engine::world::collision::AutoFit;
	using engine::world::collision::CollisionMeshCpu;
	using engine::world::collision::CollisionProxy;
	using engine::world::collision::ProxyType;
	using engine::math::Vec3;

	bool ApproxEq(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

	/// height = 4.0, widthMax = 0.6 → ratio 6.7 → Capsule.
	CollisionMeshCpu MakeTallCylinderMesh()
	{
		CollisionMeshCpu m;
		m.vertices = {
			{ -0.3f, -2.0f, -0.3f }, {  0.3f, -2.0f, -0.3f },
			{ -0.3f,  2.0f, -0.3f }, {  0.3f,  2.0f, -0.3f },
			{ -0.3f, -2.0f,  0.3f }, {  0.3f, -2.0f,  0.3f },
			{ -0.3f,  2.0f,  0.3f }, {  0.3f,  2.0f,  0.3f },
		};
		return m;
	}

	/// height = widthMax = 1.0 → ratio 1.0 → ConvexHull.
	CollisionMeshCpu MakeCompactCubeMesh()
	{
		CollisionMeshCpu m;
		m.vertices = {
			{ -0.5f, -0.5f, -0.5f }, {  0.5f, -0.5f, -0.5f },
			{ -0.5f,  0.5f, -0.5f }, {  0.5f,  0.5f, -0.5f },
			{ -0.5f, -0.5f,  0.5f }, {  0.5f, -0.5f,  0.5f },
			{ -0.5f,  0.5f,  0.5f }, {  0.5f,  0.5f,  0.5f },
		};
		return m;
	}

	/// 800 vertices + isStatic=true → TriMesh.
	CollisionMeshCpu MakeStaticBuildingMesh()
	{
		CollisionMeshCpu m;
		m.isStatic = true;
		m.vertices.reserve(800);
		for (int i = 0; i < 800; ++i)
		{
			const float fi = static_cast<float>(i);
			m.vertices.push_back({ fi * 0.01f, std::sin(fi), fi * 0.005f });
		}
		for (uint32_t i = 0; i + 2 < 800; ++i)
		{
			m.indices.push_back(i);
			m.indices.push_back(i + 1);
			m.indices.push_back(i + 2);
		}
		return m;
	}

	/// height = 0.01, widthMax = 10 → ratio 0.001 → ConvexHull (pas Capsule).
	CollisionMeshCpu MakeFlatPlaneMesh()
	{
		CollisionMeshCpu m;
		m.vertices = {
			{ -5.0f, 0.0f, -5.0f }, {  5.0f, 0.0f, -5.0f },
			{ -5.0f, 0.01f,  5.0f }, {  5.0f, 0.01f,  5.0f },
		};
		return m;
	}

	void Test_AutoFit_TallSlim_PicksCapsule()
	{
		CollisionProxy p = AutoFit(MakeTallCylinderMesh());
		REQUIRE(p.type == ProxyType::Capsule);
		REQUIRE(p.capsuleA.y < p.capsuleB.y);
		REQUIRE(ApproxEq(p.capsuleA.x, p.capsuleB.x, 1e-3f));
		REQUIRE(ApproxEq(p.capsuleA.z, p.capsuleB.z, 1e-3f));
		REQUIRE(ApproxEq(p.capsuleRadius, 0.3f, 1e-3f));
	}

	void Test_AutoFit_Compact_PicksConvexHull()
	{
		CollisionProxy p = AutoFit(MakeCompactCubeMesh());
		REQUIRE(p.type == ProxyType::ConvexHull);
		REQUIRE(p.vertices.size() == 8);
	}

	void Test_AutoFit_StaticComplex_PicksTriMesh()
	{
		CollisionProxy p = AutoFit(MakeStaticBuildingMesh());
		REQUIRE(p.type == ProxyType::TriMesh);
		REQUIRE(p.vertices.size() == 800);
		REQUIRE(p.indices.size() > 0);
	}

	void Test_AutoFit_FlatPlane_PicksConvexHull()
	{
		CollisionProxy p = AutoFit(MakeFlatPlaneMesh());
		REQUIRE(p.type == ProxyType::ConvexHull);
		REQUIRE(p.vertices.size() == 8);
	}

	void Test_AutoFit_EmptyMesh_ReturnsCapsuleDefault()
	{
		CollisionMeshCpu empty;
		CollisionProxy p = AutoFit(empty);
		// Fallback documenté : default-constructed CollisionProxy = Capsule.
		REQUIRE(p.type == ProxyType::Capsule);
		REQUIRE(ApproxEq(p.capsuleRadius, 0.5f));
	}
}

int main()
{
	Test_AutoFit_TallSlim_PicksCapsule();
	Test_AutoFit_Compact_PicksConvexHull();
	Test_AutoFit_StaticComplex_PicksTriMesh();
	Test_AutoFit_FlatPlane_PicksConvexHull();
	Test_AutoFit_EmptyMesh_ReturnsCapsuleDefault();
	return g_failed;
}
