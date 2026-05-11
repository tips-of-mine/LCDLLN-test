/// Tests unitaires CPU pour CommandStack (M100.2).
///
/// Vérifient les comportements clés de la pile undo/redo :
///   - Push exécute la commande et l'empile.
///   - Undo + Redo : roundtrip propre, état cohérent.
///   - Eviction par capacité : configure capacity=3, push 5 → undoSize == 3.
///   - Eviction par mémoire : configure maxBytes serré → la plus ancienne tombe.
///   - Coalescing par mergeKey : commandes consécutives même mergeKey fusionnent.
///   - Push purge la pile redo.
///   - RewindTo(i) : annule en cascade jusqu'à ce que UndoSize() == i.
///
/// Le mock `CounterCommand` est un `ICommand` minimal qui incrémente /
/// décrémente un `int*` à chaque Execute / Undo. Pour `Test_MergeKeyCoalescesConsecutive`,
/// `TryMerge` accumule simplement `delta` dans la commande au sommet (qui
/// absorbe les `delta` des suivantes).
///
/// Pas de dépendance ImGui ni Vulkan : la pile est pure CPU. Les tests se
/// lancent sur n'importe quelle plate-forme où engine_core compile.

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

	using engine::editor::world::CommandStack;
	using engine::editor::world::CommandStackConfig;
	using engine::editor::world::CommandMergeKey;
	using engine::editor::world::ICommand;

	/// Mock `ICommand` qui modifie un compteur partagé. `delta` peut s'accumuler
	/// lors d'un `TryMerge` (pour le test de coalescing) — la commande absorbe
	/// alors le `delta` de l'autre. Note : `Execute` rejoue l'effet du delta
	/// ABSORBÉ ; on incrémente par `delta - merged` pour ne pas double-compter.
	struct CounterCommand : ICommand
	{
		int* counter = nullptr;
		int delta = 0;
		CommandMergeKey mergeKey = 0;
		size_t footprint = sizeof(CounterCommand);

		CounterCommand(int* c, int d, CommandMergeKey k = 0, size_t fp = sizeof(CounterCommand))
			: counter(c), delta(d), mergeKey(k), footprint(fp) {}

		const char* GetLabel() const override { return "Counter"; }
		size_t GetMemoryFootprint() const override { return footprint; }
		CommandMergeKey GetMergeKey() const override { return mergeKey; }

		void Execute() override { *counter += delta; }
		void Undo() override    { *counter -= delta; }

		/// Accumule le `delta` de `other` dans `*this` et applique l'effet
		/// supplémentaire sur le compteur (la pile attend que `other` ait déjà
		/// été `Execute()` par `Push` ; sa contribution est donc déjà dans le
		/// compteur — on l'incorpore au delta absorbant pour que `Undo`
		/// reverse les deux d'un coup).
		bool TryMerge(const ICommand& other) override
		{
			const auto* o = dynamic_cast<const CounterCommand*>(&other);
			if (!o) return false;
			delta += o->delta;
			return true;
		}
	};

	/// Vérifie que `Push` exécute la commande (effet sur le compteur visible
	/// immédiatement) et qu'elle est bien empilée (UndoSize == 1).
	void Test_PushExecutesAndStores()
	{
		int counter = 0;
		CommandStack stack;
		stack.Push(std::make_unique<CounterCommand>(&counter, 7));
		REQUIRE(counter == 7);
		REQUIRE(stack.UndoSize() == 1u);
		REQUIRE(stack.RedoSize() == 0u);
		REQUIRE(stack.CanUndo());
		REQUIRE(!stack.CanRedo());
	}

	/// Roundtrip : Push → Undo (counter revient à 0, pile redo = 1) → Redo
	/// (counter revient à 5, pile undo = 1).
	void Test_UndoRedoRoundtrip()
	{
		int counter = 0;
		CommandStack stack;
		stack.Push(std::make_unique<CounterCommand>(&counter, 5));
		REQUIRE(counter == 5);
		stack.Undo();
		REQUIRE(counter == 0);
		REQUIRE(stack.UndoSize() == 0u);
		REQUIRE(stack.RedoSize() == 1u);
		stack.Redo();
		REQUIRE(counter == 5);
		REQUIRE(stack.UndoSize() == 1u);
		REQUIRE(stack.RedoSize() == 0u);
	}

	/// Eviction par capacité : configure capacity=3, push 5 commandes →
	/// UndoSize == 3 (les 2 plus anciennes sont jetées).
	void Test_CapacityEvictsOldest()
	{
		int counter = 0;
		CommandStack stack;
		CommandStackConfig cfg;
		cfg.capacity = 3;
		cfg.maxBytes = 1024ull * 1024ull * 1024ull; // énorme : on ne teste que la capacity
		stack.Configure(cfg);

		for (int i = 0; i < 5; ++i)
		{
			stack.Push(std::make_unique<CounterCommand>(&counter, 1));
		}
		REQUIRE(stack.UndoSize() == 3u);
		REQUIRE(counter == 5); // les 5 Execute ont eu lieu
	}

	/// Eviction par mémoire : chaque commande = 100 octets, maxBytes = 250 →
	/// après 5 push on ne garde que les 2 dernières (200 octets total).
	void Test_MaxBytesEvictsOldest()
	{
		int counter = 0;
		CommandStack stack;
		CommandStackConfig cfg;
		cfg.capacity = 1024;       // énorme : on ne teste que maxBytes
		cfg.maxBytes = 250;
		stack.Configure(cfg);

		for (int i = 0; i < 5; ++i)
		{
			stack.Push(std::make_unique<CounterCommand>(&counter, 1, /*mergeKey*/ 0, /*footprint*/ 100));
		}
		REQUIRE(stack.UndoSize() == 2u);
		REQUIRE(stack.TotalBytes() == 200u);
	}

	/// Coalescing : 3 push avec même mergeKey non-nul fusionnent en une seule
	/// commande au sommet (UndoSize == 1, mais le compteur a bien encaissé
	/// les 3 deltas).
	void Test_MergeKeyCoalescesConsecutive()
	{
		int counter = 0;
		CommandStack stack;
		const CommandMergeKey k = 42;
		stack.Push(std::make_unique<CounterCommand>(&counter, 1, k));
		stack.Push(std::make_unique<CounterCommand>(&counter, 2, k));
		stack.Push(std::make_unique<CounterCommand>(&counter, 3, k));
		REQUIRE(counter == 6);
		REQUIRE(stack.UndoSize() == 1u);

		// Undo doit reverser les 3 deltas d'un coup (la commande fusionnée
		// les a accumulés dans son delta).
		stack.Undo();
		REQUIRE(counter == 0);
	}

	/// Push purge la pile redo. Scénario : Push, Undo (redo=1), Push (redo
	/// doit être vidée).
	void Test_PushClearsRedoStack()
	{
		int counter = 0;
		CommandStack stack;
		stack.Push(std::make_unique<CounterCommand>(&counter, 1));
		stack.Undo();
		REQUIRE(stack.RedoSize() == 1u);
		stack.Push(std::make_unique<CounterCommand>(&counter, 10));
		REQUIRE(stack.RedoSize() == 0u);
		REQUIRE(stack.UndoSize() == 1u);
		REQUIRE(counter == 10);
	}

	/// RewindTo(0) annule toutes les commandes en cascade ; RewindTo(i) ramène
	/// la pile undo à i éléments.
	void Test_RewindToReplaysCascade()
	{
		int counter = 0;
		CommandStack stack;
		for (int i = 0; i < 5; ++i)
		{
			stack.Push(std::make_unique<CounterCommand>(&counter, 1));
		}
		REQUIRE(counter == 5);
		REQUIRE(stack.UndoSize() == 5u);

		// Rewind à 2 : il doit rester 2 commandes appliquées (counter == 2).
		stack.RewindTo(2);
		REQUIRE(stack.UndoSize() == 2u);
		REQUIRE(counter == 2);

		// Les 3 commandes annulées doivent être dans la pile redo.
		REQUIRE(stack.RedoSize() == 3u);

		// Rewind à 0 : tout doit être annulé.
		stack.RewindTo(0);
		REQUIRE(stack.UndoSize() == 0u);
		REQUIRE(counter == 0);
	}
}

int main()
{
	Test_PushExecutesAndStores();
	Test_UndoRedoRoundtrip();
	Test_CapacityEvictsOldest();
	Test_MaxBytesEvictsOldest();
	Test_MergeKeyCoalescesConsecutive();
	Test_PushClearsRedoStack();
	Test_RewindToReplaysCascade();

	if (g_failed == 0)
	{
		std::printf("[PASS] CommandStackTests (7/7)\n");
		return 0;
	}
	std::printf("[FAIL] CommandStackTests: %d failure(s)\n", g_failed);
	return 1;
}
