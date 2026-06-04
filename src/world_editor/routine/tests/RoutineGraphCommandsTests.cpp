// M101.4 / M101.5 — Tests des commandes d'édition de graphe (apply/undo),
// headless (aucun ImGui). Lié à engine_core.

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/routine/RoutineGraphCommands.h"
#include "src/world_editor/routine/RoutineGraphDocument.h"

#include <cstdio>
#include <memory>

using namespace engine::editor::world;
using engine::routine::RoutineGraphKind;
using engine::routine::RoutineLink;
using engine::routine::RoutineNode;
using engine::routine::RoutineNodeType;
using engine::routine::RoutineProperty;
using engine::routine::RoutineDataType;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	RoutineNode MakeNode(uint32_t id, float x = 0.0f, float y = 0.0f)
	{
		RoutineNode n; n.id = id; n.type = RoutineNodeType::EventOnInteract;
		n.canvasX = x; n.canvasY = y;
		return n;
	}

	void Test_AddNode_Undo()
	{
		RoutineGraphDocument doc;
		CommandStack stack;
		stack.Push(std::make_unique<AddNodeCommand>(doc, MakeNode(1)));
		REQUIRE(doc.graph.nodes.size() == 1);
		REQUIRE(doc.selectedNodeId == 1);
		stack.Undo();
		REQUIRE(doc.graph.nodes.empty());
	}

	void Test_RemoveNode_WithLinks_Undo()
	{
		RoutineGraphDocument doc;
		doc.graph.nodes.push_back(MakeNode(1));
		doc.graph.nodes.push_back(MakeNode(2));
		doc.graph.links.push_back(RoutineLink{ 100, 1, 10, 2, 20 });

		CommandStack stack;
		stack.Push(std::make_unique<RemoveNodeCommand>(doc, 1));
		REQUIRE(doc.graph.nodes.size() == 1);
		REQUIRE(doc.graph.links.empty()); // lien incident retiré
		stack.Undo();
		REQUIRE(doc.graph.nodes.size() == 2);
		REQUIRE(doc.graph.links.size() == 1);
	}

	void Test_MoveNode_Undo_AndMerge()
	{
		RoutineGraphDocument doc;
		doc.graph.nodes.push_back(MakeNode(1, 0.0f, 0.0f));

		CommandStack stack;
		stack.Push(std::make_unique<MoveNodeCommand>(doc, 1, 0.0f, 0.0f, 10.0f, 5.0f));
		REQUIRE(doc.FindNode(1)->canvasX == 10.0f);
		// Deuxième move même nœud → coalescing (mergeKey = nodeId).
		stack.Push(std::make_unique<MoveNodeCommand>(doc, 1, 10.0f, 5.0f, 20.0f, 8.0f));
		REQUIRE(doc.FindNode(1)->canvasX == 20.0f);
		REQUIRE(stack.UndoSize() == 1); // les deux moves fusionnés
		stack.Undo();
		REQUIRE(doc.FindNode(1)->canvasX == 0.0f); // retour à l'origine
		REQUIRE(doc.FindNode(1)->canvasY == 0.0f);
	}

	void Test_AddRemoveLink_Undo()
	{
		RoutineGraphDocument doc;
		doc.graph.nodes.push_back(MakeNode(1));
		doc.graph.nodes.push_back(MakeNode(2));

		CommandStack stack;
		stack.Push(std::make_unique<AddLinkCommand>(doc, RoutineLink{ 100, 1, 10, 2, 20 }));
		REQUIRE(doc.graph.links.size() == 1);
		stack.Undo();
		REQUIRE(doc.graph.links.empty());

		doc.graph.links.push_back(RoutineLink{ 200, 1, 10, 2, 20 });
		stack.Push(std::make_unique<RemoveLinkCommand>(doc, 200));
		REQUIRE(doc.graph.links.empty());
		stack.Undo();
		REQUIRE(doc.graph.links.size() == 1);
	}

	void Test_SetNodeProperty_Undo()
	{
		RoutineGraphDocument doc;
		RoutineNode n = MakeNode(1);
		RoutineProperty existing; existing.key = "open"; existing.type = RoutineDataType::Bool; existing.bValue = false;
		n.properties.push_back(existing);
		doc.graph.nodes.push_back(n);

		CommandStack stack;
		RoutineProperty np; np.key = "open"; np.type = RoutineDataType::Bool; np.bValue = true;
		stack.Push(std::make_unique<SetNodePropertyCommand>(doc, 1, np));
		REQUIRE(doc.FindNode(1)->properties[0].bValue == true);
		stack.Undo();
		REQUIRE(doc.FindNode(1)->properties[0].bValue == false);

		// Propriété absente → ajout puis undo (retrait).
		RoutineProperty added; added.key = "newKey"; added.type = RoutineDataType::Int; added.iValue = 7;
		stack.Push(std::make_unique<SetNodePropertyCommand>(doc, 1, added));
		REQUIRE(doc.FindNode(1)->properties.size() == 2);
		stack.Undo();
		REQUIRE(doc.FindNode(1)->properties.size() == 1);
	}
}

int main()
{
	Test_AddNode_Undo();
	Test_RemoveNode_WithLinks_Undo();
	Test_MoveNode_Undo_AndMerge();
	Test_AddRemoveLink_Undo();
	Test_SetNodeProperty_Undo();

	if (g_failed == 0)
		std::fprintf(stderr, "[OK] RoutineGraphCommandsTests: tous les tests passent\n");
	else
		std::fprintf(stderr, "[FAIL] RoutineGraphCommandsTests: %d échec(s)\n", g_failed);
	return g_failed;
}
