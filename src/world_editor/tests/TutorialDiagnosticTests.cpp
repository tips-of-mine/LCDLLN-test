// M100.49 — Tests Tutorial & Overlay (cœur indépendant de M100.48).
//
// Couvre le moteur de séquence (OverlayGuidanceSystem) + le système de tutoriel
// (progression 8 étapes, validation d'action, skip, pause/resume, flags) +
// WidgetTargetRegistry. Le système de DIAGNOSTIC (10 règles) dépend du
// ValidationContext de M100.48 (#817) et sera livré en 2e passe une fois #817
// mergé — non couvert ici.

#include "src/world_editor/help/OverlayGuidanceSystem.h"
#include "src/world_editor/help/WidgetTargetRegistry.h"
#include "src/world_editor/tutorial/TutorialIo.h"
#include "src/world_editor/tutorial/TutorialSystem.h"

#include <cstdio>
#include <string>

using namespace engine::editor::world::help;
using namespace engine::editor::world::tutorial;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	OverlayInstruction Instr(const std::string& target, const std::string& validatesOn)
	{
		OverlayInstruction i;
		i.targetWidget = target;
		i.validatesOnAction = validatesOn;
		i.requiresAction = true;
		return i;
	}

	void Test_Overlay_SequenceAdvanceAndComplete()
	{
		OverlayGuidanceSystem ov;
		bool done = false;
		ov.StartSequence({ Instr("a", "a.done"), Instr("b", "b.done"), Instr("c", "c.done") },
			[&done]() { done = true; });
		REQUIRE(ov.IsActiveSequence());
		REQUIRE(ov.StepCount() == 3);
		REQUIRE(ov.CurrentIndex() == 0);

		// Mauvaise action : pas d'avancement.
		REQUIRE(!ov.NotifyAction("x.done"));
		REQUIRE(ov.CurrentIndex() == 0);

		// Bonnes actions successives.
		REQUIRE(ov.NotifyAction("a.done"));
		REQUIRE(ov.CurrentIndex() == 1);
		REQUIRE(ov.NotifyAction("b.done"));
		REQUIRE(ov.CurrentIndex() == 2);
		REQUIRE(!done);
		REQUIRE(ov.NotifyAction("c.done")); // dernière → complétion
		REQUIRE(done);
		REQUIRE(!ov.IsActiveSequence());
	}

	void Test_Overlay_AbortDoesNotComplete()
	{
		OverlayGuidanceSystem ov;
		bool done = false;
		ov.StartSequence({ Instr("a", "a.done") }, [&done]() { done = true; });
		ov.AbortSequence();
		REQUIRE(!ov.IsActiveSequence());
		REQUIRE(!done);
	}

	void Test_WidgetRegistry_RegisterAndGet()
	{
		WidgetTargetRegistry reg;
		reg.Register("toolbar.button.cave", { 10.0f, 20.0f, 50.0f, 60.0f });
		bool found = false;
		WidgetRect r = reg.Get("toolbar.button.cave", &found);
		REQUIRE(found);
		REQUIRE(r.Valid());
		reg.Get("inconnu", &found);
		REQUIRE(!found);
		reg.Clear();
		REQUIRE(reg.Count() == 0);
	}

	void Test_Tutorial_FirstLaunchEightSteps()
	{
		auto def = LoadTutorialById("first_launch");
		REQUIRE(def.has_value());
		if (!def) return;
		REQUIRE(def->steps.size() == 8);
		REQUIRE(def->id == "first_launch");

		TutorialSystem sys;
		sys.LoadTutorial(*def);
		OverlayGuidanceSystem ov;
		sys.Start(ov);
		REQUIRE(sys.State() == TutorialState::Running);
		REQUIRE(ov.StepCount() == 8);

		// Franchit les 8 étapes via leurs validatesOnAction (la dernière a un
		// validatesOnAction vide → on avance via son targetWidget).
		const char* validations[8] = {
			"zone_preset_dialog.opened",
			"zone_preset_dialog.selected.temperate_forest",
			"zone.created",
			"tool.cave.active",
			"catalog.cave_small_01.selected",
			"mesh_insert.cave.placed",
			"panel.validation.opened",
			"menubar.file.export", // step 8 : validatesOnAction vide → match sur targetWidget
		};
		for (int i = 0; i < 8; ++i)
			REQUIRE(ov.NotifyAction(validations[i]));

		REQUIRE(sys.State() == TutorialState::Completed);
		REQUIRE(sys.GetFlag(kFirstLaunchFlag) == "completed");
		REQUIRE(!ov.IsActiveSequence());
	}

	void Test_Tutorial_Skip()
	{
		TutorialSystem sys;
		sys.LoadTutorial(BuildFirstLaunchTutorial());
		sys.Skip();
		REQUIRE(sys.State() == TutorialState::Skipped);
		REQUIRE(sys.IsSkipped());
		REQUIRE(sys.GetFlag(kFirstLaunchFlag) == "skipped");
	}

	void Test_Tutorial_PauseAndResume()
	{
		TutorialSystem sys;
		sys.LoadTutorial(BuildFirstLaunchTutorial());
		OverlayGuidanceSystem ov;
		sys.Start(ov);

		// Avance de 2 étapes puis met en pause.
		REQUIRE(ov.NotifyAction("zone_preset_dialog.opened"));
		REQUIRE(ov.NotifyAction("zone_preset_dialog.selected.temperate_forest"));
		REQUIRE(ov.CurrentIndex() == 2);
		sys.PauseFrom(ov);
		REQUIRE(sys.State() == TutorialState::Paused);
		REQUIRE(sys.SavedProgress() == 2);
		REQUIRE(!ov.IsActiveSequence());

		// Reprend : la sous-séquence repart à l'étape 2 (sur 8).
		sys.Resume(ov);
		REQUIRE(sys.State() == TutorialState::Running);
		REQUIRE(ov.StepCount() == 6); // 8 - 2
		// Première action attendue à la reprise = step3 (zone.created).
		REQUIRE(ov.NotifyAction("zone.created"));
		REQUIRE(ov.CurrentIndex() == 1);
	}
}

int main()
{
	Test_Overlay_SequenceAdvanceAndComplete();
	Test_Overlay_AbortDoesNotComplete();
	Test_WidgetRegistry_RegisterAndGet();
	Test_Tutorial_FirstLaunchEightSteps();
	Test_Tutorial_Skip();
	Test_Tutorial_PauseAndResume();

	if (g_failed == 0)
		std::printf("[tutorial_diagnostic_tests] all tests passed\n");
	else
		std::fprintf(stderr, "[tutorial_diagnostic_tests] %d check(s) failed\n", g_failed);
	return g_failed;
}
