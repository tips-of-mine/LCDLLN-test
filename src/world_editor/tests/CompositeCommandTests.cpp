/// Roadmap-6 (2026-07-19) — Tests unitaires CPU de CompositeCommand :
/// Execute dans l'ordre d'ajout, Undo dans l'ordre inverse, empreinte
/// mémoire agrégée, intégration CommandStack (une seule étape d'historique).
/// Pur CPU (aucune dépendance ImGui/Vulkan), tourne sous ctest Linux.

#include "src/world_editor/core/CompositeCommand.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::editor::world::CommandStack;
	using engine::editor::world::CompositeCommand;
	using engine::editor::world::ICommand;

	/// Commande sonde : journalise ses Execute/Undo dans un log partagé pour
	/// vérifier l'ORDRE d'exécution du composite.
	class ProbeCommand final : public ICommand
	{
	public:
		ProbeCommand(std::vector<std::string>* log, std::string name)
			: m_log(log), m_name(std::move(name)) {}
		const char* GetLabel() const override { return m_name.c_str(); }
		size_t GetMemoryFootprint() const override { return 100u; }
		void Execute() override { m_log->push_back("exec:" + m_name); }
		void Undo() override { m_log->push_back("undo:" + m_name); }
	private:
		std::vector<std::string>* m_log;
		std::string m_name;
	};

	/// Execute en ordre d'ajout, Undo en ordre INVERSE (transaction).
	void Test_ExecuteOrder_UndoReverse()
	{
		std::vector<std::string> log;
		CompositeCommand c("geste");
		c.AddChild(std::make_unique<ProbeCommand>(&log, "a"));
		c.AddChild(std::make_unique<ProbeCommand>(&log, "b"));
		c.AddChild(std::make_unique<ProbeCommand>(&log, "c"));
		REQUIRE(!c.Empty());
		REQUIRE(c.ChildCount() == 3u);

		c.Execute();
		REQUIRE(log.size() == 3u);
		REQUIRE(log[0] == "exec:a" && log[1] == "exec:b" && log[2] == "exec:c");

		log.clear();
		c.Undo();
		REQUIRE(log.size() == 3u);
		REQUIRE(log[0] == "undo:c" && log[1] == "undo:b" && log[2] == "undo:a");
	}

	/// L'empreinte mémoire agrège celle des enfants (éviction par budget).
	void Test_MemoryFootprintAggregates()
	{
		std::vector<std::string> log;
		CompositeCommand c("geste");
		REQUIRE(c.Empty());
		const size_t base = c.GetMemoryFootprint();
		c.AddChild(std::make_unique<ProbeCommand>(&log, "a"));
		c.AddChild(std::make_unique<ProbeCommand>(&log, "b"));
		REQUIRE(c.GetMemoryFootprint() >= base + 200u);
	}

	/// Poussé sur un CommandStack, le composite est UNE étape : un seul Undo
	/// annule les 3 enfants, un seul Redo les rejoue.
	void Test_CommandStackSingleStep()
	{
		std::vector<std::string> log;
		auto c = std::make_unique<CompositeCommand>("geste");
		c->AddChild(std::make_unique<ProbeCommand>(&log, "a"));
		c->AddChild(std::make_unique<ProbeCommand>(&log, "b"));
		c->AddChild(std::make_unique<ProbeCommand>(&log, "c"));

		CommandStack stack;
		stack.Push(std::move(c)); // Execute immédiat
		REQUIRE(log.size() == 3u);
		REQUIRE(stack.UndoSize() == 1u);

		log.clear();
		stack.Undo();
		REQUIRE(log.size() == 3u);
		REQUIRE(log[0] == "undo:c");
		REQUIRE(stack.UndoSize() == 0u);
		REQUIRE(stack.RedoSize() == 1u);

		log.clear();
		stack.Redo();
		REQUIRE(log.size() == 3u);
		REQUIRE(log[0] == "exec:a");
	}

	/// Deux composites successifs ne fusionnent JAMAIS (mergeKey 0) : chaque
	/// geste reste une étape d'annulation distincte.
	void Test_NoCoalescingBetweenGestures()
	{
		std::vector<std::string> log;
		CommandStack stack;
		for (int i = 0; i < 2; ++i)
		{
			auto c = std::make_unique<CompositeCommand>("geste");
			c->AddChild(std::make_unique<ProbeCommand>(&log, "x"));
			stack.Push(std::move(c));
		}
		REQUIRE(stack.UndoSize() == 2u);
	}
}

int main()
{
	Test_ExecuteOrder_UndoReverse();
	Test_MemoryFootprintAggregates();
	Test_CommandStackSingleStep();
	Test_NoCoalescingBetweenGestures();

	if (g_failed == 0)
	{
		std::printf("[PASS] CompositeCommandTests\n");
		return 0;
	}
	std::printf("[FAIL] CompositeCommandTests: %d failure(s)\n", g_failed);
	return 1;
}
