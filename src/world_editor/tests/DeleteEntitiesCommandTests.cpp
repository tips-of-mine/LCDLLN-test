/// Tests CPU pour DeleteEntitiesCommand (Execute/Undo/Redo via callbacks).
#include "src/world_editor/scene/DeleteEntitiesCommand.h"
#include "src/world_editor/scene/DeleteEntitiesOps.h"
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

	using namespace engine::editor::scene;

	void Test_ExecuteUndoRedo()
	{
		// Modèle de test : un vecteur de strings simulant layoutInstances.
		std::vector<std::string> model = {"a", "b", "c"};

		std::vector<EntityId> ids = {
			{EntityKind::LayoutInstance, 0}, {EntityKind::LayoutInstance, 2} };

		// Snapshot custom du test : on réutilise le champ générique via lambdas.
		std::vector<std::pair<uint32_t, std::string>> backup;

		auto remove = [&](const std::vector<EntityId>& sel) -> DeletedEntities
		{
			std::vector<uint32_t> idx;
			for (const EntityId& e : sel) idx.push_back(e.index);
			backup = RemoveByIndexDescending(model, idx);
			return DeletedEntities{}; // l'état réel est dans `backup` (capturé)
		};
		auto restore = [&](const DeletedEntities&)
		{
			RestoreByIndexAscending(model, backup);
		};

		DeleteEntitiesCommand cmd(ids, remove, restore);
		REQUIRE(std::string(cmd.GetLabel()) != "");

		cmd.Execute();
		REQUIRE(model.size() == 1);
		REQUIRE(model[0] == "b");

		cmd.Undo();
		REQUIRE(model.size() == 3);
		REQUIRE(model[0] == "a"); REQUIRE(model[1] == "b"); REQUIRE(model[2] == "c");

		// Redo = Execute à nouveau (indices valides car Undo a tout restitué).
		cmd.Execute();
		REQUIRE(model.size() == 1);
		REQUIRE(model[0] == "b");
	}
}

int main()
{
	Test_ExecuteUndoRedo();
	if (g_failed == 0) { std::printf("[PASS] DeleteEntitiesCommandTests\n"); return 0; }
	std::printf("[FAIL] DeleteEntitiesCommandTests: %d failure(s)\n", g_failed);
	return 1;
}
