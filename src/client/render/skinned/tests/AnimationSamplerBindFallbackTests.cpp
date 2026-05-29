// src/client/render/skinned/tests/AnimationSamplerBindFallbackTests.cpp
//
// Garde anti-régression du bug « pied coupé à l'arrêt » : un os qu'un clip
// n'anime pas (pistes vides, ex. l'orteil dans le clip UE5 Idle_Loop) doit
// conserver sa BIND POSE complète (rotation + scale de bind compris), et non
// retomber sur une rotation identité qui écrasait la pose et effondrait la
// pointe de la botte. Vérifie aussi qu'un os réellement animé suit sa piste.

#include "src/client/render/skinned/AnimationSampler.h"
#include "src/client/render/skinned/Skeleton.h"
#include "src/client/render/skinned/AnimationClip.h"
#include "src/shared/math/Quat.h"

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

	using engine::render::skinned::AnimationClip;
	using engine::render::skinned::AnimationSampler;
	using engine::render::skinned::Skeleton;
	using engine::math::Mat4;
	using engine::math::Quat;
	using engine::math::Vec3;

	bool MatApproxEq(const Mat4& a, const Mat4& b, float eps = 1e-4f)
	{
		for (int i = 0; i < 16; ++i)
		{
			if (std::fabs(a.m[i] - b.m[i]) > eps) return false;
		}
		return true;
	}

	// Squelette d'un seul os, avec une bind pose à rotation NON identité (90° autour
	// de Z) + translation : reproduit un os comme l'orteil dont la rotation de bind
	// est significative.
	Skeleton MakeOneBoneSkeleton(const Mat4& bindLocal)
	{
		Skeleton skel;
		skel.bones.resize(1);
		skel.bones[0].name = "toe";
		skel.bones[0].parentIndex = -1;
		skel.bones[0].bindLocal = bindLocal;
		skel.bones[0].inverseBindGlobal = Mat4::Identity();
		return skel;
	}

	void Test_UnanimatedBone_KeepsFullBindPose()
	{
		const Mat4 bind = AnimationSampler::ComposeTRS(
			Vec3{1.0f, 2.0f, 3.0f},
			Quat::FromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 1.5707963f), // 90° Z
			Vec3{1.0f, 1.0f, 1.0f});
		const Skeleton skel = MakeOneBoneSkeleton(bind);

		// Clip qui n'anime pas l'os : une piste présente mais entièrement vide.
		AnimationClip clipEmptyTrack;
		clipEmptyTrack.name = "Idle";
		clipEmptyTrack.duration = 1.0f;
		clipEmptyTrack.tracks.resize(1); // BoneTracks par défaut = 3 vecteurs vides

		const auto locals1 = AnimationSampler::SamplePose(skel, clipEmptyTrack, 0.0f);
		REQUIRE(locals1.size() == 1u);
		// Doit être EXACTEMENT la bind pose (rotation 90° Z conservée), pas une identité.
		REQUIRE(MatApproxEq(locals1[0], bind));

		// Clip sans aucune piste (tracks.size() < bones.size()) : même résultat.
		AnimationClip clipNoTracks;
		clipNoTracks.name = "Idle";
		clipNoTracks.duration = 1.0f;
		const auto locals2 = AnimationSampler::SamplePose(skel, clipNoTracks, 0.0f);
		REQUIRE(locals2.size() == 1u);
		REQUIRE(MatApproxEq(locals2[0], bind));

		std::puts("[OK] Test_UnanimatedBone_KeepsFullBindPose");
	}

	void Test_AnimatedBone_FollowsTrack()
	{
		// Bind = rotation 90° Z. Le clip keye explicitement une rotation IDENTITÉ.
		const Mat4 bind = AnimationSampler::ComposeTRS(
			Vec3{0.0f, 0.0f, 0.0f},
			Quat::FromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 1.5707963f),
			Vec3{1.0f, 1.0f, 1.0f});
		const Skeleton skel = MakeOneBoneSkeleton(bind);

		AnimationClip clip;
		clip.name = "Walk";
		clip.duration = 1.0f;
		clip.tracks.resize(1);
		clip.tracks[0].rotation.push_back({0.0f, Quat::Identity()}); // os animé : rotation identité

		const auto locals = AnimationSampler::SamplePose(skel, clip, 0.0f);
		REQUIRE(locals.size() == 1u);
		// L'os est animé → on suit la piste (rotation identité), donc PAS la bind (90° Z).
		const Mat4 expected = AnimationSampler::ComposeTRS(
			Vec3{0.0f, 0.0f, 0.0f}, Quat::Identity(), Vec3{1.0f, 1.0f, 1.0f});
		REQUIRE(MatApproxEq(locals[0], expected));
		REQUIRE(!MatApproxEq(locals[0], bind)); // différent de la bind pose

		std::puts("[OK] Test_AnimatedBone_FollowsTrack");
	}
}

int main()
{
	Test_UnanimatedBone_KeepsFullBindPose();
	Test_AnimatedBone_FollowsTrack();
	return g_failed;
}
