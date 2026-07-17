/// Tests unitaires CPU pour le registre d'actions de l'éditeur monde
/// (réorganisation UI 2026-07-17, PR 1).
///
/// Pas d'ImGui ni de GPU — la suite tourne sous ctest Linux. On vérifie :
///   - Register accepte une action valide et préserve l'ordre d'insertion.
///   - Register refuse un id vide et un id dupliqué (unicité garantie).
///   - Find retrouve une action par id, nullptr sinon.
///   - IsEnabled : nul => true ; sinon le prédicat est évalué.
///   - Les toggles (checked) et l'exécution (execute) sont câblés.
///   - CommandStack::Serial() : monotone sur Push/Undo/Redo/Clear (le
///     dirty-tracking « non sauvegardé » de la barre de statut et de la
///     modale Quitter repose dessus).
///
/// Framework : REQUIRE maison + main monolithique (pattern identique aux
/// autres suites de tests world_editor).

#include "src/world_editor/actions/EditorActionRegistry.h"
#include "src/world_editor/core/CommandStack.h"

#include <cstdio>
#include <memory>
#include <string>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::editor::world::actions::ActionCategory;
	using engine::editor::world::actions::EditorAction;
	using engine::editor::world::actions::EditorActionRegistry;
	using engine::editor::world::CommandStack;
	using engine::editor::world::ICommand;

	/// Fabrique une action minimale valide pour les tests.
	EditorAction MakeAction(const char* id, const char* label)
	{
		EditorAction a;
		a.id = id;
		a.label = label;
		a.execute = [] {};
		return a;
	}

	/// Test : enregistrement nominal + ordre d'insertion préservé + Size.
	void Test_Register_PreservesInsertionOrder()
	{
		EditorActionRegistry reg;
		REQUIRE(reg.Size() == 0);
		REQUIRE(reg.Register(MakeAction("file.save", "Sauvegarder")));
		REQUIRE(reg.Register(MakeAction("edit.undo", "Annuler")));
		REQUIRE(reg.Register(MakeAction("edit.redo", "Retablir")));
		REQUIRE(reg.Size() == 3);
		REQUIRE(reg.Actions()[0].id == "file.save");
		REQUIRE(reg.Actions()[1].id == "edit.undo");
		REQUIRE(reg.Actions()[2].id == "edit.redo");
	}

	/// Test : id vide refusé, id dupliqué refusé (le registre reste intact).
	void Test_Register_RejectsEmptyAndDuplicateIds()
	{
		EditorActionRegistry reg;
		REQUIRE(!reg.Register(MakeAction("", "Sans id")));
		REQUIRE(reg.Size() == 0);

		REQUIRE(reg.Register(MakeAction("file.save", "Sauvegarder")));
		REQUIRE(!reg.Register(MakeAction("file.save", "Doublon")));
		REQUIRE(reg.Size() == 1);
		REQUIRE(reg.Find("file.save")->label == "Sauvegarder");
	}

	/// Test : Find retrouve par id, nullptr si absent.
	void Test_Find_ReturnsActionOrNull()
	{
		EditorActionRegistry reg;
		REQUIRE(reg.Register(MakeAction("view.grid", "Grille")));
		const EditorAction* found = reg.Find("view.grid");
		REQUIRE(found != nullptr);
		REQUIRE(found->label == "Grille");
		REQUIRE(reg.Find("absent.id") == nullptr);
	}

	/// Test : IsEnabled — prédicat nul => true ; sinon évalué à l'appel.
	void Test_IsEnabled_EvaluatesPredicate()
	{
		EditorAction always = MakeAction("a.always", "Toujours");
		REQUIRE(EditorActionRegistry::IsEnabled(always));

		bool gate = false;
		EditorAction gated = MakeAction("a.gated", "Conditionnelle");
		gated.enabled = [&gate] { return gate; };
		REQUIRE(!EditorActionRegistry::IsEnabled(gated));
		gate = true;
		REQUIRE(EditorActionRegistry::IsEnabled(gated));
	}

	/// Test : toggle (checked) + execute câblés — le pattern utilisé par les
	/// items de menu à coche (visibilité de panneau, grille).
	void Test_ToggleAction_CheckedAndExecute()
	{
		bool visible = false;
		EditorAction toggle = MakeAction("window.panel.test", "Panneau test");
		toggle.category = ActionCategory::Fenetre;
		toggle.checked = [&visible] { return visible; };
		toggle.execute = [&visible] { visible = !visible; };

		EditorActionRegistry reg;
		REQUIRE(reg.Register(std::move(toggle)));
		const EditorAction* a = reg.Find("window.panel.test");
		REQUIRE(a != nullptr);
		REQUIRE(a->checked && a->checked() == false);
		a->execute();
		REQUIRE(a->checked() == true);
		a->execute();
		REQUIRE(a->checked() == false);
	}

	/// Commande stub : aucun effet, empreinte fixe — sert uniquement à
	/// exercer Push/Undo/Redo de la pile pour le test de Serial().
	class StubCommand final : public ICommand
	{
	public:
		const char* GetLabel() const override { return "stub"; }
		size_t GetMemoryFootprint() const override { return 16; }
		void Execute() override {}
		void Undo() override {}
	};

	/// Test : Serial() est monotone croissant à chaque mutation de la pile
	/// (Push, Undo, Redo, Clear) et stable sinon. Les no-op (Undo sur pile
	/// vide) ne l'incrémentent pas.
	void Test_CommandStackSerial_MonotonicOnMutations()
	{
		CommandStack stack;
		const uint64_t s0 = stack.Serial();
		REQUIRE(s0 == 0);

		// No-op : Undo/Redo sur piles vides ne mutent rien.
		stack.Undo();
		stack.Redo();
		REQUIRE(stack.Serial() == s0);

		stack.Push(std::make_unique<StubCommand>());
		const uint64_t s1 = stack.Serial();
		REQUIRE(s1 > s0);

		stack.Undo();
		const uint64_t s2 = stack.Serial();
		REQUIRE(s2 > s1);

		stack.Redo();
		const uint64_t s3 = stack.Serial();
		REQUIRE(s3 > s2);

		stack.Clear();
		const uint64_t s4 = stack.Serial();
		REQUIRE(s4 > s3);

		// Clear sur pile déjà vide : toujours compté comme mutation ? Non —
		// on fige le contrat : Clear n'incrémente que s'il a vidé quelque
		// chose est trop subtil ; le contrat simple retenu est « Clear
		// incrémente toujours » (coût nul, aucune surprise consommateur).
		stack.Clear();
		REQUIRE(stack.Serial() > s4);
	}
}

int main()
{
	Test_Register_PreservesInsertionOrder();
	Test_Register_RejectsEmptyAndDuplicateIds();
	Test_Find_ReturnsActionOrNull();
	Test_IsEnabled_EvaluatesPredicate();
	Test_ToggleAction_CheckedAndExecute();
	Test_CommandStackSerial_MonotonicOnMutations();

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[EditorActionRegistryTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[EditorActionRegistryTests] all tests passed\n");
	return 0;
}
