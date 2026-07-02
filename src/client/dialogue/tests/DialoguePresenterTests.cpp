// src/client/dialogue/tests/DialoguePresenterTests.cpp
//
// Tests unitaires de la logique de dialogue PNJ (sans ImGui) :
// modèle d'arbre, presenter (navigation/choix/distance/auto-scroll),
// journal de conversation de quête. Framework maison (REQUIRE).

#include "src/client/dialogue/DialogueTree.h"
#include "src/client/dialogue/DialoguePresenter.h"
#include "src/client/dialogue/QuestConversationJournal.h"
#include "src/client/dialogue/DialogueConfigLoader.h"
#include "src/shared/core/Config.h"

#include <cstdio>
#include <string>
#include <vector>

using engine::client::DialogueAction;
using engine::client::DialogueChoice;
using engine::client::DialogueLine;
using engine::client::DialogueNode;
using engine::client::DialogueTree;
using engine::client::DialoguePresenter;
using engine::client::DialogueNpcRef;
using engine::client::DialogueCloseReason;
using engine::client::IQuestConversationSink;
using engine::client::QuestConversationEntry;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond)                                                              \
	do                                                                             \
	{                                                                              \
		if (!(cond))                                                               \
		{                                                                          \
			std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond);  \
			++g_failed;                                                            \
		}                                                                          \
	} while (0)

	// =========================================================================
	// Task 1 — modèle DialogueTree
	// =========================================================================

	void Test_LegacyLinesBecomeSingleNode()
	{
		const DialogueTree t = engine::client::BuildTreeFromLegacyLines({"Bonjour.", "Au plaisir."});
		REQUIRE(t.nodes.size() == 1);
		REQUIRE(t.startNodeId == t.nodes[0].id);
		REQUIRE(t.nodes[0].lines.size() == 2);
		REQUIRE(t.nodes[0].choices.size() == 1);
		REQUIRE(t.nodes[0].choices[0].action == DialogueAction::End);
	}

	void Test_FindNode()
	{
		DialogueTree t;
		t.nodes.push_back(DialogueNode{"a", {}, {}});
		t.nodes.push_back(DialogueNode{"b", {}, {}});
		REQUIRE(t.FindNode("b") != nullptr);
		REQUIRE(t.FindNode("b")->id == "b");
		REQUIRE(t.FindNode("zzz") == nullptr);
	}

	void Test_NormalizeDefaultsStartAndDetectsBadNext()
	{
		DialogueTree t;
		DialogueNode n;
		n.id = "intro";
		n.choices.push_back(DialogueChoice{"Aller voir", "absent", DialogueAction::Continue, -1, ""});
		t.nodes.push_back(n);
		const auto r = engine::client::NormalizeDialogueTree(t);
		REQUIRE(t.startNodeId == "intro");   // défaut = premier nœud
		REQUIRE(r.ok == false);              // "absent" n'existe pas
		REQUIRE(!r.errors.empty());
	}

	void Test_NormalizeRejectsTooManyChoices()
	{
		DialogueTree t;
		DialogueNode n;
		n.id = "intro";
		for (int i = 0; i < 6; ++i) // 6 > 5
			n.choices.push_back(DialogueChoice{"c", "", DialogueAction::End, -1, ""});
		t.nodes.push_back(n);
		const auto r = engine::client::NormalizeDialogueTree(t);
		REQUIRE(r.ok == false);
	}

	// =========================================================================
	// Task 2 — navigation / choix
	// =========================================================================

	DialogueTree MakeTwoNodeTree()
	{
		DialogueTree t;
		DialogueNode intro;
		intro.id = "intro";
		intro.lines.push_back(DialogueLine{"Bonjour voyageur.", false});
		intro.choices.push_back(DialogueChoice{"En savoir plus", "details", DialogueAction::Continue, -1, ""});
		intro.choices.push_back(DialogueChoice{"Au revoir", "", DialogueAction::End, -1, ""});
		DialogueNode details;
		details.id = "details";
		details.lines.push_back(DialogueLine{"Les routes sont dangereuses.", false});
		details.choices.push_back(DialogueChoice{"Merci", "", DialogueAction::End, -1, ""});
		t.startNodeId = "intro";
		t.nodes.push_back(intro);
		t.nodes.push_back(details);
		return t;
	}

	void Test_OpenStartsAtStartNode()
	{
		DialoguePresenter p;
		p.OpenDialogue(MakeTwoNodeTree(), DialogueNpcRef{"Aldric", "Garde", 0});
		REQUIRE(p.IsActive());
		REQUIRE(p.CurrentNode() != nullptr);
		REQUIRE(p.CurrentNode()->id == "intro");
	}

	void Test_ContinueNavigates()
	{
		DialoguePresenter p;
		p.OpenDialogue(MakeTwoNodeTree(), DialogueNpcRef{"Aldric", "Garde", 0});
		p.SelectChoice(0); // "En savoir plus" -> details
		REQUIRE(p.IsActive());
		REQUIRE(p.CurrentNode()->id == "details");
	}

	void Test_EndChoiceCloses()
	{
		DialoguePresenter p;
		p.OpenDialogue(MakeTwoNodeTree(), DialogueNpcRef{"Aldric", "Garde", 0});
		p.SelectChoice(1); // "Au revoir" -> End
		REQUIRE(!p.IsActive());
		REQUIRE(p.LastCloseReason() == DialogueCloseReason::EndNode);
	}

	// =========================================================================
	// Task 3 — journalisation des conversations de quête
	// =========================================================================

	class FakeJournalSink final : public IQuestConversationSink
	{
	public:
		std::vector<QuestConversationEntry> entries;
		void RecordConversation(const QuestConversationEntry& e) override { entries.push_back(e); }
	};

	DialogueTree MakeQuestTree()
	{
		DialogueTree t;
		DialogueNode intro;
		intro.id = "intro";
		intro.lines.push_back(DialogueLine{"Retrouve mon frère.", false});
		// choix 0 : accepte la quête 1012, action AcceptQuest, pas de next => fin
		intro.choices.push_back(DialogueChoice{"J'accepte", "", DialogueAction::AcceptQuest, 1012, "⚔️"});
		// choix 1 : neutre, pas lié à une quête
		intro.choices.push_back(DialogueChoice{"Au revoir", "", DialogueAction::End, -1, ""});
		t.startNodeId = "intro";
		t.nodes.push_back(intro);
		return t;
	}

	void Test_QuestChoiceRecordsJournal()
	{
		FakeJournalSink sink;
		DialogueAction firedAction = DialogueAction::Continue;
		int firedQuest = -999;

		DialoguePresenter p;
		p.SetJournalSink(&sink);
		p.SetQuestActionCallback([&](DialogueAction a, int q, const std::string& /*questKey*/) { firedAction = a; firedQuest = q; });
		p.OpenDialogue(MakeQuestTree(), DialogueNpcRef{"Aldric", "Garde", 0});

		p.SelectChoice(0); // accept quest 1012
		REQUIRE(sink.entries.size() == 1);
		REQUIRE(sink.entries[0].questId == 1012);
		REQUIRE(sink.entries[0].npcLabel == "Aldric");
		REQUIRE(sink.entries[0].choiceText == "J'accepte");
		REQUIRE(sink.entries[0].lines.size() == 1);
		REQUIRE(firedAction == DialogueAction::AcceptQuest);
		REQUIRE(firedQuest == 1012);
		REQUIRE(!p.IsActive()); // pas de next => fermeture EndNode
	}

	void Test_NeutralChoiceDoesNotRecord()
	{
		FakeJournalSink sink;
		DialoguePresenter p;
		p.SetJournalSink(&sink);
		p.OpenDialogue(MakeQuestTree(), DialogueNpcRef{"Aldric", "Garde", 0});
		p.SelectChoice(1); // "Au revoir" neutre
		REQUIRE(sink.entries.empty());
	}

	// =========================================================================
	// Task 4 — rupture de distance
	// =========================================================================

	void Test_StaysOpenWithinRange()
	{
		DialoguePresenter p;
		p.OpenDialogue(MakeTwoNodeTree(), DialogueNpcRef{"Aldric", "Garde", 0});
		p.Tick(0.016f, 1.50f); // pile au seuil nominal
		REQUIRE(p.IsActive());
		p.Tick(0.016f, 1.59f); // dans l'hystérésis (< 1.6)
		REQUIRE(p.IsActive());
	}

	void Test_ClosesWhenTooFar()
	{
		DialoguePresenter p;
		p.OpenDialogue(MakeTwoNodeTree(), DialogueNpcRef{"Aldric", "Garde", 0});
		p.Tick(0.016f, 1.65f); // > 1.5 + 0.1
		REQUIRE(!p.IsActive());
		REQUIRE(p.LastCloseReason() == DialogueCloseReason::TooFar);
	}

	// =========================================================================
	// Task 5 — auto-scroll
	// =========================================================================

	void Test_AutoScrollAdvancesAndClamps()
	{
		DialoguePresenter p;
		p.OpenDialogue(MakeTwoNodeTree(), DialogueNpcRef{"Aldric", "Garde", 0});
		p.SetViewMetrics(/*content*/ 100.0f, /*view*/ 40.0f); // max scroll = 60
		REQUIRE(p.ScrollOffset() == 0.0f);
		p.Tick(1.0f, 1.0f); // +20 px (kAutoScrollPixelsPerSecond)
		REQUIRE(p.ScrollOffset() > 19.0f);
		REQUIRE(p.ScrollOffset() < 21.0f);
		p.Tick(100.0f, 1.0f); // clamp au max (60)
		REQUIRE(p.ScrollOffset() <= 60.0f + 0.001f);
		REQUIRE(p.ScrollOffset() >= 59.999f);
	}

	void Test_UserScrollPausesAutoScroll()
	{
		DialoguePresenter p;
		p.OpenDialogue(MakeTwoNodeTree(), DialogueNpcRef{"Aldric", "Garde", 0});
		p.SetViewMetrics(100.0f, 40.0f);
		p.OnUserScroll(10.0f);          // remonte manuellement
		REQUIRE(p.AutoScrollEnabled() == false);
		REQUIRE(p.ScrollOffset() == 10.0f);
		p.Tick(1.0f, 1.0f);             // ne doit pas bouger (pause)
		REQUIRE(p.ScrollOffset() == 10.0f);
		p.OnUserScroll(60.0f);          // revient en bas => reprise
		REQUIRE(p.AutoScrollEnabled() == true);
	}

	// =========================================================================
	// Task 6 — sérialisation journal
	// =========================================================================

	void Test_JournalSerializeEntryLine()
	{
		QuestConversationEntry e;
		e.npcLabel   = "Aldric";
		e.questId    = 1012;
		e.choiceText = "J'accepte";
		e.lines.push_back("Retrouve mon frère.");
		e.lines.push_back("Il a un médaillon \"lune\"."); // contient un guillemet à échapper

		const std::string line = engine::client::QuestConversationJournal::SerializeEntryLine(e);
		REQUIRE(line.find("\"npc\":\"Aldric\"") != std::string::npos);
		REQUIRE(line.find("\"questId\":1012") != std::string::npos);
		REQUIRE(line.find("\\\"lune\\\"") != std::string::npos); // guillemet échappé
		REQUIRE(line.find('\n') == std::string::npos);            // une seule ligne
	}

	// Task 8 (relocalisation) : charge le VRAI fichier de dialogue dédié
	// game/data/dialogues/villageois.json via la référence dialogue_id.
	// CTest tourne avec WORKING_DIRECTORY = CMAKE_SOURCE_DIR, donc le chemin
	// "game/data/..." est résoluble tel quel.
	void Test_LoadDialogueFromDedicatedFile()
	{
		engine::core::Config world;
		world.SetDefault("paths.content", std::string("game/data"));
		world.SetDefault("npc.dialogue_id", std::string("villageois"));

		const DialogueTree t = engine::client::LoadDialogueTree(world, "npc.", {});
		REQUIRE(t.startNodeId == "intro");
		REQUIRE(t.nodes.size() == 2);
		REQUIRE(t.FindNode("intro") != nullptr);
		REQUIRE(t.FindNode("infos") != nullptr);
		REQUIRE(t.FindNode("intro")->choices.size() == 3);
		// Le 2e choix du nœud d'intro accepte une quête (questId illustratif >= 0).
		REQUIRE(t.FindNode("intro")->choices[1].action == DialogueAction::AcceptQuest);
		REQUIRE(t.FindNode("intro")->choices[1].questId >= 0);
		// SP2 : le choix accept_quest porte aussi la clé texte de quête (wire QuestAcceptRequest).
		REQUIRE(t.FindNode("intro")->choices[1].questKey == "kill_10_boars");
	}

	// SP2 — un choix sans "questKey" dans la config retombe sur une chaîne vide
	// (pas de clé wire disponible : Engine ne doit pas envoyer un QuestAcceptRequest bidon).
	void Test_QuestKeyDefaultsToEmptyWhenAbsent()
	{
		engine::core::Config world;
		world.SetDefault("paths.content", std::string("game/data"));
		world.SetDefault("npc.dialogue_id", std::string("villageois"));

		const DialogueTree t = engine::client::LoadDialogueTree(world, "npc.", {});
		// Le choix "Au revoir" du nœud intro (index 2) n'est pas lié à une quête.
		REQUIRE(t.FindNode("intro")->choices[2].action == DialogueAction::End);
		REQUIRE(t.FindNode("intro")->choices[2].questKey.empty());
	}

	// Si dialogue_id est absent, on retombe sur l'arbre legacy (un nœud + « Au revoir »).
	void Test_NoDialogueIdFallsBackToLegacy()
	{
		engine::core::Config world; // aucune clé dialogue_id
		const DialogueTree t = engine::client::LoadDialogueTree(world, "npc.", {"Bonjour."});
		REQUIRE(t.nodes.size() == 1);
		REQUIRE(t.nodes[0].choices.size() == 1);
		REQUIRE(t.nodes[0].choices[0].action == DialogueAction::End);
	}

} // namespace

int main()
{
	// Task 1
	Test_LegacyLinesBecomeSingleNode();
	Test_FindNode();
	Test_NormalizeDefaultsStartAndDetectsBadNext();
	Test_NormalizeRejectsTooManyChoices();

	// Task 2
	Test_OpenStartsAtStartNode();
	Test_ContinueNavigates();
	Test_EndChoiceCloses();

	// Task 3
	Test_QuestChoiceRecordsJournal();
	Test_NeutralChoiceDoesNotRecord();

	// Task 4
	Test_StaysOpenWithinRange();
	Test_ClosesWhenTooFar();

	// Task 5
	Test_AutoScrollAdvancesAndClamps();
	Test_UserScrollPausesAutoScroll();

	// Task 6
	Test_JournalSerializeEntryLine();

	// Task 8 (relocalisation : fichiers de dialogue dédiés)
	Test_LoadDialogueFromDedicatedFile();
	Test_NoDialogueIdFallsBackToLegacy();

	// SP2 — Task 4 : parsing du champ questKey
	Test_QuestKeyDefaultsToEmptyWhenAbsent();

	if (g_failed == 0)
		std::printf("[OK] DialoguePresenterTests: all assertions passed\n");
	else
		std::fprintf(stderr, "[FAILED] DialoguePresenterTests: %d assertion(s) failed\n", g_failed);
	return g_failed == 0 ? 0 : 1;
}
