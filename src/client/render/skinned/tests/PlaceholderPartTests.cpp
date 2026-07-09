#include "src/client/render/skinned/PlaceholderPart.h"
#include "src/client/render/skinned/Skeleton.h"
#include "src/shared/math/Math.h"

#include <cmath>
#include <cstdio>

using engine::math::Mat4;
using engine::math::Vec3;
using engine::render::skinned::Bone;
using engine::render::skinned::MakePlaceholderBoxPart;
using engine::render::skinned::Skeleton;
using engine::render::skinned::SkinnedMeshCpuData;

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { \
		if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
	} while (0)

	bool Approx(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

	/// Squelette 2 os : root (identité) puis head translaté de (0, 1.5, 0).
	/// `inverseBindGlobal` = inverse de la bind-globale (c'est CETTE matrice que
	/// le skinning utilise, donc c'est elle qui pilote le placement de la boîte).
	Skeleton MakeSkel()
	{
		Skeleton s;
		s.bones.resize(2);
		s.bones[0].name = "root";
		s.bones[0].parentIndex = -1;
		s.bones[0].bindLocal = Mat4::Identity();
		s.bones[0].inverseBindGlobal = Mat4::Identity();
		s.bones[1].name = "head";
		s.bones[1].parentIndex = 0;
		s.bones[1].bindLocal = Mat4::Translate(Vec3{0.0f, 1.5f, 0.0f});
		// bind-globale head = Translate(0,1.5,0) -> inverse = Translate(0,-1.5,0).
		s.bones[1].inverseBindGlobal = Mat4::Translate(Vec3{0.0f, -1.5f, 0.0f});
		return s;
	}

	void Test_BoxCounts()
	{
		SkinnedMeshCpuData part = MakePlaceholderBoxPart(MakeSkel(), 1, 0.1f);
		REQUIRE(part.vertices.size() == 24);
		REQUIRE(part.indices.size() == 72); // double-face (recto + verso)
		REQUIRE(part.submeshes.size() == 1);
		REQUIRE(part.submeshes[0].firstIndex == 0u);
		REQUIRE(part.submeshes[0].indexCount == 72u);
		REQUIRE(part.skeleton.bones.size() == 2); // squelette copié
	}

	void Test_AllVertsWeightedToTargetBone()
	{
		SkinnedMeshCpuData part = MakePlaceholderBoxPart(MakeSkel(), 1, 0.1f);
		for (const auto& v : part.vertices)
		{
			REQUIRE(v.boneIndices[0] == 1);
			REQUIRE(Approx(v.weights[0], 1.0f));
			REQUIRE(Approx(v.weights[1], 0.0f));
			REQUIRE(Approx(v.weights[2], 0.0f));
			REQUIRE(Approx(v.weights[3], 0.0f));
		}
	}

	void Test_BoxCenteredOnBoneBindGlobal()
	{
		SkinnedMeshCpuData part = MakePlaceholderBoxPart(MakeSkel(), 1, 0.1f);
		// Boîte rigidement attachée à l'os : centre = moyenne des 24 sommets
		// (boîte symétrique) = position bind-globale de head = inverse(IBM) = (0, 1.5, 0).
		float sx = 0.0f, sy = 0.0f, sz = 0.0f;
		for (const auto& v : part.vertices) { sx += v.pos[0]; sy += v.pos[1]; sz += v.pos[2]; }
		const float n = static_cast<float>(part.vertices.size());
		REQUIRE(Approx(sx / n, 0.0f));
		REQUIRE(Approx(sy / n, 1.5f));
		REQUIRE(Approx(sz / n, 0.0f));
	}

	void Test_OutOfRangeBone_FallsBackToOrigin()
	{
		SkinnedMeshCpuData part = MakePlaceholderBoxPart(MakeSkel(), 99, 0.1f);
		REQUIRE(part.vertices.size() == 24);
		for (const auto& v : part.vertices)
			REQUIRE(v.boneIndices[0] == 0); // repli sur l'os 0
		// bg = identité (os hors bornes) -> boîte centrée sur l'origine.
		float sy = 0.0f;
		for (const auto& v : part.vertices) sy += v.pos[1];
		REQUIRE(Approx(sy / static_cast<float>(part.vertices.size()), 0.0f));
	}
}

int main()
{
	Test_BoxCounts();
	Test_AllVertsWeightedToTargetBone();
	Test_BoxCenteredOnBoneBindGlobal();
	Test_OutOfRangeBone_FallsBackToOrigin();
	return g_failed == 0 ? 0 : 1;
}
