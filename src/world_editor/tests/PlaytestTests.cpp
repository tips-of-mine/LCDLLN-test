// M100.33 — Tests footstep audio + machine d'état playtest. Headless.

#include "src/client/audio/FootstepAudioSurfaceHook.h"
#include "src/world_editor/PlaytestMode.h"

#include <cmath>
#include <cstdio>

using namespace engine::editor::world;
using engine::math::Vec3;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	void Test_FootstepHook_GrassPlaysStepGrass()
	{
		engine::audio::FootstepSurface s; s.surfaceName = "grass"; s.wet = false;
		auto ev = engine::audio::ResolveFootstep(s, Vec3(1, 0, 2));
		REQUIRE(ev.id == "step_grass");
		REQUIRE(std::fabs(ev.pitch - 1.0f) < 1e-4f);
	}

	void Test_FootstepHook_SnowWetCrunchPitch()
	{
		engine::audio::FootstepSurface s; s.surfaceName = "snow"; s.wet = true;
		auto ev = engine::audio::ResolveFootstep(s, Vec3(0, 0, 0));
		REQUIRE(ev.id == "step_snow_crunch");
		REQUIRE(ev.pitch > 1.0f); // +5 % modificateur mouillé
	}

	void Test_PlaytestMode_TogglePreservesEditorState()
	{
		PlaytestMode pt;
		EditorCameraPose pose; pose.position = Vec3(12, 4, -3); pose.yawDeg = 45.0f; pose.pitchDeg = -10.0f;
		REQUIRE(!pt.IsActive());

		const bool nowActive = pt.Toggle(pose, Vec3(50, 0, 50));
		REQUIRE(nowActive);
		REQUIRE(pt.IsActive());
		REQUIRE(std::fabs(pt.PlayerStart().x - 50.0f) < 1e-4f);

		// Modifie une "fausse" pose éditeur entre-temps n'affecte pas la sauvegarde.
		const bool stillActive = pt.Toggle(EditorCameraPose{}, Vec3(0, 0, 0));
		REQUIRE(!stillActive);
		REQUIRE(!pt.IsActive());
		// La pose restaurée est celle sauvegardée à l'entrée.
		PlaytestMode pt2;
		pt2.Enter(pose, Vec3(1, 0, 1));
		const EditorCameraPose restored = pt2.Exit();
		REQUIRE(std::fabs(restored.position.x - 12.0f) < 1e-4f);
		REQUIRE(std::fabs(restored.yawDeg - 45.0f) < 1e-4f);
	}
}

int main()
{
	Test_FootstepHook_GrassPlaysStepGrass();
	Test_FootstepHook_SnowWetCrunchPitch();
	Test_PlaytestMode_TogglePreservesEditorState();

	if (g_failed == 0)
		std::fprintf(stderr, "[OK] PlaytestTests: tous les tests passent\n");
	else
		std::fprintf(stderr, "[FAIL] PlaytestTests: %d échec(s)\n", g_failed);
	return g_failed;
}
