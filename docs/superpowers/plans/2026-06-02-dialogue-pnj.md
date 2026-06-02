# Plan d'implémentation — Cellule de dialogue PNJ dédiée

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Afficher les dialogues PNJ dans une cellule dédiée (fenêtre centrale parchemin) au lieu du canal chat, avec auto-scroll fluide, word-wrap, 2-5 choix, verrouillage du déplacement + rupture auto à 1,50 m, et journalisation locale des conversations de quête.

**Architecture:** Logique pure et testable (`engine::client` : `DialogueTree`, `DialoguePresenter`, `QuestConversationJournal`) séparée du rendu ImGui (`DialogueImGuiRenderer`, Windows, non testé en CI). Le dialogue est piloté par `config.json` (format à clés plates de `engine::core::Config`). Le journal est écrit en local par personnage via `engine::platform::FileSystem`.

**Tech Stack:** C++17, framework de test maison (`REQUIRE`/`Test_*`/`main`), `engine::core::Config`, `engine::platform::FileSystem`, ImGui (rendu Windows), CMake (`engine_core` racine + `lcdlln_add_simple_test`).

---

## Contraintes de vérification (LIRE EN PREMIER)

- **Pas de toolchain de build local** (cmake/MSVC/vcpkg absents). Les compilations et
  les tests **tournent en CI** : `build-linux.yml` exécute `ctest`. Pour vérifier un
  test, pousser la branche `feat/dialogue-pnj` et lire le résultat de `build-linux`.
  Les commandes `ctest` ci-dessous décrivent ce que la CI exécute — ne pas tenter de
  les lancer localement.
- **CTest tourne avec `WORKING_DIRECTORY = CMAKE_SOURCE_DIR`** : les chemins
  `game/data/...` sont résolubles tels quels dans les tests.
- **Commits fréquents**, un par tâche minimum. Branche : `feat/dialogue-pnj` (déjà créée,
  contient déjà le spec + la maquette).
- **Conventions repo** : commentaires en français, `///` Doxygen sur les déclarations,
  PascalCase pour le nouveau code/fichiers, pas du terme « CMANGOS » dans le nouveau code.

## Structure des fichiers

**Nouveaux (logique pure, dans `engine_core`) :**
- `src/client/dialogue/DialogueTree.h` — structs de données + helpers purs.
- `src/client/dialogue/DialogueTree.cpp` — impl. des helpers.
- `src/client/dialogue/DialoguePresenter.h` — presenter runtime + interfaces.
- `src/client/dialogue/DialoguePresenter.cpp` — impl. presenter.
- `src/client/dialogue/QuestConversationJournal.h` — journal local par personnage.
- `src/client/dialogue/QuestConversationJournal.cpp` — impl. (sérialisation + I/O).
- `src/client/dialogue/DialogueConfigLoader.h` — chargement `Config` → `DialogueTree`.
- `src/client/dialogue/DialogueConfigLoader.cpp` — impl.
- `src/client/dialogue/tests/DialoguePresenterTests.cpp` — tests unitaires (CI).

**Nouveau (rendu, cible ImGui Windows) :**
- `src/client/render/DialogueImGuiRenderer.h` / `.cpp`.

**Modifiés :**
- `CMakeLists.txt` (racine) — ajout des `.cpp` à `engine_core` + cible de test + renderer.
- `src/client/app/Engine.h` — extension `InteractableEntity`, membres dialogue.
- `src/client/app/Engine.cpp` — chargement config, ouverture sur E, suppression poussée
  chat, tick distance + verrouillage déplacement, fermeture, appel du renderer.
- `config.json` — bloc `dialogue_tree` + `radius` 1,5 sur PNJ dialoguants.

---

## Task 1: Modèle de données `DialogueTree` + helpers purs

**Files:**
- Create: `src/client/dialogue/DialogueTree.h`
- Create: `src/client/dialogue/DialogueTree.cpp`
- Test: `src/client/dialogue/tests/DialoguePresenterTests.cpp`

- [ ] **Step 1: Écrire l'en-tête du modèle**

Create `src/client/dialogue/DialogueTree.h` :

```cpp
#pragma once

#include <string>
#include <vector>

namespace engine::client
{
	/// Action déclenchée par un choix de dialogue.
	enum class DialogueAction
	{
		Continue,      ///< Navigue vers \c nextNodeId.
		AcceptQuest,   ///< Accepte la quête \c questId puis navigue/ferme.
		CompleteQuest, ///< Complète la quête \c questId puis navigue/ferme.
		End            ///< Termine le dialogue.
	};

	/// Une ligne prononcée par le PNJ (ou une didascalie).
	struct DialogueLine
	{
		std::string text;
		bool        isCue = false; ///< Affichée en italique atténué (ex. « Il sourit »).
	};

	/// Un choix de réponse proposé au joueur.
	struct DialogueChoice
	{
		std::string    text;                              ///< Libellé (word-wrap au rendu).
		std::string    nextNodeId;                        ///< Nœud suivant si action == Continue.
		DialogueAction action  = DialogueAction::Continue;
		int            questId = -1;                      ///< >=0 => lié à une quête (journalisé).
		std::string    icon;                              ///< Optionnel (ex. "⚔️").
	};

	/// Un nœud de dialogue : lignes du PNJ + 1..5 choix.
	struct DialogueNode
	{
		std::string                 id;
		std::vector<DialogueLine>   lines;
		std::vector<DialogueChoice> choices;
	};

	/// Arbre de dialogue complet d'un PNJ.
	struct DialogueTree
	{
		std::string               startNodeId;
		std::vector<DialogueNode> nodes;

		/// Retourne le nœud d'id donné, ou nullptr si absent.
		const DialogueNode* FindNode(const std::string& id) const;
	};

	/// Convertit l'ancien format (liste de lignes en boucle) en arbre à nœud unique
	/// terminé par un choix « Au revoir » (action End). Rétro-compatibilité config.
	DialogueTree BuildTreeFromLegacyLines(const std::vector<std::string>& lines);

	/// Résultat de validation/normalisation d'un arbre.
	struct DialogueValidationResult
	{
		bool                     ok = true;
		std::vector<std::string> errors;
	};

	/// Valide et normalise un arbre chargé depuis la config :
	///  - startNodeId par défaut = premier nœud si vide ;
	///  - chaque nœud doit avoir 1..5 choix (sinon erreur) ;
	///  - chaque choix Continue doit référencer un nœud existant (sinon erreur) ;
	///  - un choix sans action explicite et sans nextNodeId valide est ramené à End.
	/// En cas d'erreur, \c ok = false et \c errors décrit les problèmes ; l'appelant
	/// peut alors retomber sur un nœud unique de secours.
	DialogueValidationResult NormalizeDialogueTree(DialogueTree& tree);

} // namespace engine::client
```

- [ ] **Step 2: Écrire les tests qui échouent**

Create `src/client/dialogue/tests/DialoguePresenterTests.cpp` :

```cpp
// src/client/dialogue/tests/DialoguePresenterTests.cpp
//
// Tests unitaires de la logique de dialogue PNJ (sans ImGui) :
// modèle d'arbre, presenter (navigation/choix/distance/auto-scroll),
// journal de conversation de quête. Framework maison (REQUIRE).

#include "src/client/dialogue/DialogueTree.h"

#include <cstdio>
#include <string>
#include <vector>

using engine::client::DialogueAction;
using engine::client::DialogueChoice;
using engine::client::DialogueLine;
using engine::client::DialogueNode;
using engine::client::DialogueTree;

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
} // namespace

int main()
{
	Test_LegacyLinesBecomeSingleNode();
	Test_FindNode();
	Test_NormalizeDefaultsStartAndDetectsBadNext();
	Test_NormalizeRejectsTooManyChoices();

	if (g_failed == 0)
		std::printf("[OK] DialoguePresenterTests: all assertions passed\n");
	else
		std::fprintf(stderr, "[FAILED] DialoguePresenterTests: %d assertion(s) failed\n", g_failed);
	return g_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 3: Implémenter `DialogueTree.cpp`**

Create `src/client/dialogue/DialogueTree.cpp` :

```cpp
#include "src/client/dialogue/DialogueTree.h"

namespace engine::client
{
	const DialogueNode* DialogueTree::FindNode(const std::string& id) const
	{
		for (const DialogueNode& n : nodes)
			if (n.id == id)
				return &n;
		return nullptr;
	}

	DialogueTree BuildTreeFromLegacyLines(const std::vector<std::string>& lines)
	{
		DialogueTree tree;
		DialogueNode node;
		node.id = "legacy";
		for (const std::string& l : lines)
			node.lines.push_back(DialogueLine{l, false});

		DialogueChoice bye;
		bye.text   = "Au revoir.";
		bye.action = DialogueAction::End;
		node.choices.push_back(bye);

		tree.startNodeId = node.id;
		tree.nodes.push_back(std::move(node));
		return tree;
	}

	DialogueValidationResult NormalizeDialogueTree(DialogueTree& tree)
	{
		DialogueValidationResult res;

		if (tree.nodes.empty())
		{
			res.ok = false;
			res.errors.push_back("arbre de dialogue sans nœud");
			return res;
		}

		if (tree.startNodeId.empty())
			tree.startNodeId = tree.nodes.front().id;

		if (tree.FindNode(tree.startNodeId) == nullptr)
		{
			res.ok = false;
			res.errors.push_back("startNodeId introuvable: " + tree.startNodeId);
		}

		for (DialogueNode& n : tree.nodes)
		{
			if (n.choices.empty() || n.choices.size() > 5)
			{
				res.ok = false;
				res.errors.push_back("nœud '" + n.id + "': nombre de choix invalide (attendu 1..5)");
			}

			for (DialogueChoice& c : n.choices)
			{
				if (c.action == DialogueAction::Continue)
				{
					if (c.nextNodeId.empty() || tree.FindNode(c.nextNodeId) == nullptr)
					{
						res.ok = false;
						res.errors.push_back("nœud '" + n.id + "': choix Continue vers nœud absent '"
						                     + c.nextNodeId + "'");
					}
				}
			}
		}
		return res;
	}

} // namespace engine::client
```

- [ ] **Step 4: Vérifier (CI)** — la cible `dialogue_presenter_tests` est créée en Task 7. Tant qu'elle n'existe pas, ce code compile via `engine_core` (Task 7). Verdict attendu après Task 7 : `ctest -R dialogue_presenter_tests` → PASS pour les 4 tests ci-dessus.

- [ ] **Step 5: Commit**

```bash
git add src/client/dialogue/DialogueTree.h src/client/dialogue/DialogueTree.cpp src/client/dialogue/tests/DialoguePresenterTests.cpp
git commit -m "feat(dialogue): modèle DialogueTree + helpers (legacy, normalize)"
```

---

## Task 2: `DialoguePresenter` — ouverture, navigation, choix

**Files:**
- Create: `src/client/dialogue/DialoguePresenter.h`
- Create: `src/client/dialogue/DialoguePresenter.cpp`
- Test: `src/client/dialogue/tests/DialoguePresenterTests.cpp` (étendu)

- [ ] **Step 1: Écrire l'en-tête du presenter**

Create `src/client/dialogue/DialoguePresenter.h` :

```cpp
#pragma once

#include "src/client/dialogue/DialogueTree.h"

#include <functional>
#include <string>
#include <vector>

namespace engine::client
{
	/// Référence légère vers le PNJ avec qui on dialogue.
	struct DialogueNpcRef
	{
		std::string label; ///< Nom affiché.
		std::string role;  ///< Sous-titre (ex. "Garde du pont").
		int         entityIndex = -1; ///< Index dans m_interactables (pour distance).
	};

	/// Raison de fermeture du dialogue.
	enum class DialogueCloseReason
	{
		None,
		UserClose, ///< ✕ ou Échap.
		TooFar,    ///< Joueur > seuil de distance.
		EndNode    ///< Choix End ou nœud terminal.
	};

	/// Entrée de conversation à consigner dans le journal de quête.
	struct QuestConversationEntry
	{
		std::string              npcLabel;
		int                      questId = -1;
		std::vector<std::string> lines;      ///< Texte échangé (lignes du nœud déclencheur).
		std::string              choiceText; ///< Choix retenu.
	};

	/// Puits de journalisation (découplé du système de fichiers pour testabilité).
	class IQuestConversationSink
	{
	public:
		virtual ~IQuestConversationSink() = default;
		virtual void RecordConversation(const QuestConversationEntry& entry) = 0;
	};

	/// Presenter runtime du dialogue PNJ. Logique pure (aucune dépendance ImGui).
	class DialoguePresenter final
	{
	public:
		/// Distance nominale max joueur↔PNJ (mètres). Au-delà, rupture.
		static constexpr float kMaxDistanceMeters = 1.5f;
		/// Hystérésis : rupture effective à kMaxDistanceMeters + kHysteresisMeters.
		static constexpr float kHysteresisMeters = 0.1f;
		/// Vitesse d'auto-scroll (pixels/seconde) — « doucement ».
		static constexpr float kAutoScrollPixelsPerSecond = 20.0f;

		/// Ouvre un dialogue (positionne le nœud de départ, active le presenter).
		void OpenDialogue(const DialogueTree& tree, const DialogueNpcRef& npc);

		/// Sélectionne le choix d'indice donné dans le nœud courant.
		/// Applique l'action (journalise si lié à une quête, fire le callback quête),
		/// puis navigue ou ferme.
		void SelectChoice(size_t index);

		/// Avance l'auto-scroll et applique la rupture de distance.
		/// \param deltaSeconds temps écoulé.
		/// \param distanceMeters distance XZ courante joueur↔PNJ.
		void Tick(float deltaSeconds, float distanceMeters);

		/// Ferme le dialogue avec une raison.
		void Close(DialogueCloseReason reason);

		// --- Lecture (pour le renderer) ---
		bool                IsActive() const { return m_active; }
		DialogueCloseReason LastCloseReason() const { return m_lastCloseReason; }
		const DialogueNpcRef& Npc() const { return m_npc; }
		const DialogueNode*   CurrentNode() const { return m_current; }
		float                 ScrollOffset() const { return m_scrollOffset; }
		bool                  AutoScrollEnabled() const { return m_autoScroll; }

		// --- Auto-scroll piloté par le renderer ---
		/// Renseigne les métriques de la zone texte (hauteur contenu/vue, en pixels).
		void SetViewMetrics(float contentHeight, float viewHeight);
		/// L'utilisateur a fait défiler manuellement : met l'auto-scroll en pause.
		void OnUserScroll(float newOffset);

		// --- Callbacks ---
		/// Appelé quand un choix porte AcceptQuest/CompleteQuest (déclenche le système quête).
		void SetQuestActionCallback(std::function<void(DialogueAction, int /*questId*/)> cb)
		{
			m_questActionCb = std::move(cb);
		}
		/// Puits de journalisation (non possédé). Peut être nullptr (pas de journal).
		void SetJournalSink(IQuestConversationSink* sink) { m_journalSink = sink; }

	private:
		void SetCurrentNode(const std::string& id);

		DialogueTree         m_tree;
		const DialogueNode*  m_current = nullptr;
		DialogueNpcRef       m_npc;
		bool                 m_active = false;
		DialogueCloseReason  m_lastCloseReason = DialogueCloseReason::None;

		// Auto-scroll
		float m_scrollOffset  = 0.0f;
		float m_contentHeight = 0.0f;
		float m_viewHeight    = 0.0f;
		bool  m_autoScroll    = true;

		std::function<void(DialogueAction, int)> m_questActionCb;
		IQuestConversationSink*                  m_journalSink = nullptr;
	};

} // namespace engine::client
```

- [ ] **Step 2: Ajouter les tests de navigation (échouent)**

Dans `DialoguePresenterTests.cpp`, ajouter l'include et les tests, et les appeler dans `main()` :

```cpp
// en haut, après l'include existant :
#include "src/client/dialogue/DialoguePresenter.h"
```

```cpp
// dans le namespace anonyme :
using engine::client::DialoguePresenter;
using engine::client::DialogueNpcRef;
using engine::client::DialogueCloseReason;

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
```

```cpp
// dans main(), ajouter :
	Test_OpenStartsAtStartNode();
	Test_ContinueNavigates();
	Test_EndChoiceCloses();
```

- [ ] **Step 3: Implémenter `DialoguePresenter.cpp` (navigation/choix)**

Create `src/client/dialogue/DialoguePresenter.cpp` :

```cpp
#include "src/client/dialogue/DialoguePresenter.h"

#include <algorithm>

namespace engine::client
{
	void DialoguePresenter::OpenDialogue(const DialogueTree& tree, const DialogueNpcRef& npc)
	{
		m_tree            = tree;
		m_npc             = npc;
		m_active          = true;
		m_lastCloseReason = DialogueCloseReason::None;
		m_scrollOffset    = 0.0f;
		m_contentHeight   = 0.0f;
		m_viewHeight      = 0.0f;
		m_autoScroll      = true;
		SetCurrentNode(m_tree.startNodeId);
	}

	void DialoguePresenter::SetCurrentNode(const std::string& id)
	{
		m_current      = m_tree.FindNode(id);
		m_scrollOffset = 0.0f; // nouveau texte : on repart du haut
		m_autoScroll   = true;
	}

	void DialoguePresenter::SelectChoice(size_t index)
	{
		if (!m_active || m_current == nullptr || index >= m_current->choices.size())
			return;

		const DialogueChoice& choice = m_current->choices[index];

		// Journalisation si lié à une quête/raid.
		const bool questLinked = (choice.action == DialogueAction::AcceptQuest)
		                       || (choice.action == DialogueAction::CompleteQuest)
		                       || (choice.questId >= 0);
		if (questLinked && m_journalSink != nullptr)
		{
			QuestConversationEntry e;
			e.npcLabel   = m_npc.label;
			e.questId    = choice.questId;
			e.choiceText = choice.text;
			for (const DialogueLine& l : m_current->lines)
				e.lines.push_back(l.text);
			m_journalSink->RecordConversation(e);
		}

		// Déclenche l'action quête côté système (accept/complete).
		if ((choice.action == DialogueAction::AcceptQuest
		     || choice.action == DialogueAction::CompleteQuest)
		    && m_questActionCb)
		{
			m_questActionCb(choice.action, choice.questId);
		}

		// Navigation / fermeture.
		if (choice.action == DialogueAction::End)
		{
			Close(DialogueCloseReason::EndNode);
			return;
		}

		if (!choice.nextNodeId.empty() && m_tree.FindNode(choice.nextNodeId) != nullptr)
		{
			SetCurrentNode(choice.nextNodeId);
		}
		else
		{
			// Pas de suite valide (ex. accept_quest sans next) => fin naturelle.
			Close(DialogueCloseReason::EndNode);
		}
	}

	void DialoguePresenter::Close(DialogueCloseReason reason)
	{
		m_active          = false;
		m_lastCloseReason = reason;
		m_current         = nullptr;
	}

	// Tick / auto-scroll / métriques : implémentés en Tasks 4 et 5.

} // namespace engine::client
```

- [ ] **Step 4: Vérifier (CI)** — après Task 7 : `ctest -R dialogue_presenter_tests` → PASS (7 tests).

- [ ] **Step 5: Commit**

```bash
git add src/client/dialogue/DialoguePresenter.h src/client/dialogue/DialoguePresenter.cpp src/client/dialogue/tests/DialoguePresenterTests.cpp
git commit -m "feat(dialogue): DialoguePresenter — ouverture, navigation, choix End"
```

---

## Task 3: `DialoguePresenter` — journalisation des conversations de quête

**Files:**
- Modify: `src/client/dialogue/tests/DialoguePresenterTests.cpp`
- (L'impl. de journalisation est déjà dans `SelectChoice` — cette tâche la **vérifie**.)

- [ ] **Step 1: Ajouter un sink de test + les tests (échouent si l'impl. régresse)**

Dans `DialoguePresenterTests.cpp`, dans le namespace anonyme :

```cpp
using engine::client::IQuestConversationSink;
using engine::client::QuestConversationEntry;

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
	p.SetQuestActionCallback([&](DialogueAction a, int q) { firedAction = a; firedQuest = q; });
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
```

```cpp
// dans main(), ajouter :
	Test_QuestChoiceRecordsJournal();
	Test_NeutralChoiceDoesNotRecord();
```

- [ ] **Step 2: Vérifier (CI)** — `ctest -R dialogue_presenter_tests` → PASS (9 tests). L'impl. de Task 2 couvre déjà ce comportement ; si un test échoue, corriger `SelectChoice`.

- [ ] **Step 3: Commit**

```bash
git add src/client/dialogue/tests/DialoguePresenterTests.cpp
git commit -m "test(dialogue): journalisation des choix liés à une quête"
```

---

## Task 4: `DialoguePresenter` — rupture de distance (1,50 m + hystérésis)

**Files:**
- Modify: `src/client/dialogue/DialoguePresenter.cpp`
- Modify: `src/client/dialogue/tests/DialoguePresenterTests.cpp`

- [ ] **Step 1: Ajouter les tests de distance (échouent)**

Dans `DialoguePresenterTests.cpp` :

```cpp
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
```

```cpp
// dans main(), ajouter :
	Test_StaysOpenWithinRange();
	Test_ClosesWhenTooFar();
```

- [ ] **Step 2: Implémenter `Tick` (partie distance) dans `DialoguePresenter.cpp`**

Remplacer le commentaire `// Tick / auto-scroll ...` par :

```cpp
	void DialoguePresenter::Tick(float deltaSeconds, float distanceMeters)
	{
		if (!m_active)
			return;

		// Rupture de distance (filet de sécurité), avec hystérésis anti-clignotement.
		if (distanceMeters > (kMaxDistanceMeters + kHysteresisMeters))
		{
			Close(DialogueCloseReason::TooFar);
			return;
		}

		// Auto-scroll : implémenté en Task 5.
		(void)deltaSeconds;
	}
```

- [ ] **Step 3: Vérifier (CI)** — `ctest -R dialogue_presenter_tests` → PASS (11 tests).

- [ ] **Step 4: Commit**

```bash
git add src/client/dialogue/DialoguePresenter.cpp src/client/dialogue/tests/DialoguePresenterTests.cpp
git commit -m "feat(dialogue): rupture auto au-delà de 1,50 m (hystérésis 0,1 m)"
```

---

## Task 5: `DialoguePresenter` — auto-scroll fluide + pause/reprise

**Files:**
- Modify: `src/client/dialogue/DialoguePresenter.cpp`
- Modify: `src/client/dialogue/tests/DialoguePresenterTests.cpp`

- [ ] **Step 1: Ajouter les tests d'auto-scroll (échouent)**

Dans `DialoguePresenterTests.cpp` :

```cpp
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
```

```cpp
// dans main(), ajouter :
	Test_AutoScrollAdvancesAndClamps();
	Test_UserScrollPausesAutoScroll();
```

- [ ] **Step 2: Implémenter l'auto-scroll dans `DialoguePresenter.cpp`**

Compléter `Tick` (remplacer le `(void)deltaSeconds;`) :

```cpp
		// Auto-scroll fluide vers le bas.
		if (m_autoScroll)
		{
			const float maxOffset = std::max(0.0f, m_contentHeight - m_viewHeight);
			m_scrollOffset += kAutoScrollPixelsPerSecond * deltaSeconds;
			if (m_scrollOffset >= maxOffset)
				m_scrollOffset = maxOffset;
		}
```

Ajouter les deux méthodes :

```cpp
	void DialoguePresenter::SetViewMetrics(float contentHeight, float viewHeight)
	{
		m_contentHeight = contentHeight;
		m_viewHeight    = viewHeight;
		const float maxOffset = std::max(0.0f, m_contentHeight - m_viewHeight);
		if (m_scrollOffset > maxOffset)
			m_scrollOffset = maxOffset;
	}

	void DialoguePresenter::OnUserScroll(float newOffset)
	{
		const float maxOffset = std::max(0.0f, m_contentHeight - m_viewHeight);
		m_scrollOffset = std::max(0.0f, std::min(newOffset, maxOffset));
		// Pause si l'utilisateur n'est pas tout en bas ; reprise s'il y revient.
		m_autoScroll = (m_scrollOffset >= maxOffset - 0.5f);
	}
```

- [ ] **Step 3: Vérifier (CI)** — `ctest -R dialogue_presenter_tests` → PASS (13 tests).

- [ ] **Step 4: Commit**

```bash
git add src/client/dialogue/DialoguePresenter.cpp src/client/dialogue/tests/DialoguePresenterTests.cpp
git commit -m "feat(dialogue): auto-scroll fluide + pause/reprise au scroll manuel"
```

---

## Task 6: Journal de conversation local (`QuestConversationJournal`)

**Files:**
- Create: `src/client/dialogue/QuestConversationJournal.h`
- Create: `src/client/dialogue/QuestConversationJournal.cpp`
- Modify: `src/client/dialogue/tests/DialoguePresenterTests.cpp`

Format de stockage : **JSON Lines** (une entrée JSON par ligne) sous
`<paths.content>/quest_journal/character_<id>.jsonl`. L'ajout est un simple
append de ligne (pas de parsing nécessaire pour écrire).

- [ ] **Step 1: Écrire l'en-tête**

Create `src/client/dialogue/QuestConversationJournal.h` :

```cpp
#pragma once

#include "src/client/dialogue/DialoguePresenter.h" // IQuestConversationSink, QuestConversationEntry

#include <cstdint>
#include <string>

namespace engine::core { class Config; }

namespace engine::client
{
	/// Journal local des conversations de quête, une ligne JSON par entrée
	/// (fichier `<paths.content>/quest_journal/character_<id>.jsonl`).
	/// Implémente \ref IQuestConversationSink : branché sur le presenter, il
	/// persiste chaque conversation liée à une quête/raid pour le suivi.
	class QuestConversationJournal final : public IQuestConversationSink
	{
	public:
		/// \param config pour résoudre `paths.content` et l'écriture relative.
		/// \param characterId identifiant du personnage courant.
		QuestConversationJournal(const engine::core::Config& config, uint64_t characterId);

		/// Sérialise l'entrée en une ligne JSON et l'ajoute au fichier du personnage.
		/// Effet de bord : écriture disque (append). Logue en cas d'échec.
		void RecordConversation(const QuestConversationEntry& entry) override;

		/// Sérialise une entrée en une ligne JSON compacte (pur, testable, sans I/O).
		static std::string SerializeEntryLine(const QuestConversationEntry& entry);

		/// Chemin relatif (sous paths.content) du fichier journal d'un personnage.
		static std::string RelPathForCharacter(uint64_t characterId);

	private:
		const engine::core::Config& m_config;
		uint64_t                    m_characterId;
	};

} // namespace engine::client
```

- [ ] **Step 2: Ajouter le test de sérialisation (échoue)**

Dans `DialoguePresenterTests.cpp` :

```cpp
// en haut, avec les includes :
#include "src/client/dialogue/QuestConversationJournal.h"
```

```cpp
// dans le namespace anonyme :
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
```

```cpp
// dans main(), ajouter :
	Test_JournalSerializeEntryLine();
```

- [ ] **Step 3: Implémenter `QuestConversationJournal.cpp`**

> Aligner les `#include` du système de fichiers / config / log sur ceux de
> `src/client/hud/HudLayoutEditor.cpp` (mêmes en-têtes `engine::core::Config`,
> `engine::platform::FileSystem`, macros `LOG_*`).

Create `src/client/dialogue/QuestConversationJournal.cpp` :

```cpp
#include "src/client/dialogue/QuestConversationJournal.h"

#include "src/shared/core/Config.h"
#include "src/shared/platform/FileSystem.h"
#include "src/shared/core/Log.h" // mêmes macros LOG_* que HudLayoutEditor.cpp

#include <string>

namespace engine::client
{
	namespace
	{
		/// Échappe une chaîne pour insertion dans un littéral JSON.
		std::string JsonEscape(const std::string& s)
		{
			std::string out;
			out.reserve(s.size() + 8);
			for (char c : s)
			{
				switch (c)
				{
					case '"':  out += "\\\""; break;
					case '\\': out += "\\\\"; break;
					case '\n': out += "\\n";  break;
					case '\r': out += "\\r";  break;
					case '\t': out += "\\t";  break;
					default:   out += c;      break;
				}
			}
			return out;
		}
	} // namespace

	QuestConversationJournal::QuestConversationJournal(const engine::core::Config& config,
	                                                   uint64_t characterId)
		: m_config(config), m_characterId(characterId)
	{
	}

	std::string QuestConversationJournal::RelPathForCharacter(uint64_t characterId)
	{
		return "quest_journal/character_" + std::to_string(characterId) + ".jsonl";
	}

	std::string QuestConversationJournal::SerializeEntryLine(const QuestConversationEntry& entry)
	{
		std::string out = "{";
		out += "\"npc\":\"" + JsonEscape(entry.npcLabel) + "\",";
		out += "\"questId\":" + std::to_string(entry.questId) + ",";
		out += "\"choice\":\"" + JsonEscape(entry.choiceText) + "\",";
		out += "\"lines\":[";
		for (size_t i = 0; i < entry.lines.size(); ++i)
		{
			if (i != 0) out += ",";
			out += "\"" + JsonEscape(entry.lines[i]) + "\"";
		}
		out += "]}";
		return out;
	}

	void QuestConversationJournal::RecordConversation(const QuestConversationEntry& entry)
	{
		const std::string relPath = RelPathForCharacter(m_characterId);
		const std::string contentRoot = m_config.GetString("paths.content", "game/data");
		const std::string fullPath = contentRoot + "/" + relPath;

		// Append : on relit l'existant et on ajoute une ligne (JSONL).
		std::string content = engine::platform::FileSystem::ReadAllText(fullPath);
		if (!content.empty() && content.back() != '\n')
			content += "\n";
		content += SerializeEntryLine(entry);
		content += "\n";

		if (!engine::platform::FileSystem::WriteAllTextContent(m_config, relPath, content))
		{
			LOG_ERROR(Core, "[QuestConversationJournal] écriture impossible: '{}'", relPath);
			return;
		}
		LOG_INFO(Core, "[QuestConversationJournal] conversation consignée (quête {}, PNJ '{}')",
		         entry.questId, entry.npcLabel);
	}

} // namespace engine::client
```

- [ ] **Step 4: Vérifier (CI)** — `ctest -R dialogue_presenter_tests` → PASS (14 tests). Note : `SerializeEntryLine` est pur (pas d'I/O) ; `RecordConversation` (disque) est vérifié en jeu, pas en CI.

- [ ] **Step 5: Commit**

```bash
git add src/client/dialogue/QuestConversationJournal.h src/client/dialogue/QuestConversationJournal.cpp src/client/dialogue/tests/DialoguePresenterTests.cpp
git commit -m "feat(dialogue): journal local de conversation de quête (JSONL par perso)"
```

---

## Task 7: Câblage CMake (sources `engine_core` + cible de test)

**Files:**
- Modify: `CMakeLists.txt` (racine) — liste des sources `engine_core` (~ligne 247-256) + bloc tests.

- [ ] **Step 1: Ajouter les `.cpp` purs à `engine_core`**

Dans `CMakeLists.txt` (racine), dans la liste des sources `engine_core`, juste après
`src/client/quest/QuestUi.cpp` (ligne ~253), ajouter :

```cmake
  src/client/dialogue/DialogueTree.cpp
  src/client/dialogue/DialoguePresenter.cpp
  src/client/dialogue/DialogueConfigLoader.cpp
  src/client/dialogue/QuestConversationJournal.cpp
```

> `DialogueConfigLoader.cpp` est créé en Task 8 ; l'ajouter ici dès maintenant pour
> éviter un second passage CMake. Si l'exécution de Task 8 est différée, créer un
> `DialogueConfigLoader.cpp` minimal (juste l'`#include` de son header) pour ne pas
> casser le build, puis l'implémenter en Task 8.

- [ ] **Step 2: Enregistrer la cible de test**

Repérer dans `CMakeLists.txt` la macro `lcdlln_add_simple_test` (ex. autour de
`character_customization_tests`). Ajouter, à la suite des autres tests :

```cmake
  # Dialogue PNJ : modèle d'arbre, presenter (navigation/choix/distance/auto-scroll),
  # journal de conversation. DialoguePresenter.cpp & co sont déjà linkés via engine_core.
  lcdlln_add_simple_test(dialogue_presenter_tests
    ${CMAKE_SOURCE_DIR}/src/client/dialogue/tests/DialoguePresenterTests.cpp)
```

- [ ] **Step 3: Vérifier (CI)** — pousser la branche. `build-linux` doit compiler et
`ctest -R dialogue_presenter_tests` doit afficher **PASS (14 tests)**.

```bash
ctest -R dialogue_presenter_tests --output-on-failure
```
Expected: `[OK] DialoguePresenterTests: all assertions passed` puis `100% tests passed`.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "build(dialogue): ajoute sources engine_core + cible dialogue_presenter_tests"
```

---

## Task 8: Chargement config → arbre (`DialogueConfigLoader`)

**Files:**
- Create: `src/client/dialogue/DialogueConfigLoader.h`
- Create/replace: `src/client/dialogue/DialogueConfigLoader.cpp`

`engine::core::Config` expose des clés **plates** (`GetInt/GetDouble/GetBool/GetString`),
les tableaux étant représentés par `<base>.count` + `<base>.<i>`. Le loader assemble
les clés et remplit un `DialogueTree`, puis appelle `NormalizeDialogueTree`.

- [ ] **Step 1: Écrire l'en-tête**

Create `src/client/dialogue/DialogueConfigLoader.h` :

```cpp
#pragma once

#include "src/client/dialogue/DialogueTree.h"

#include <string>
#include <vector>

namespace engine::core { class Config; }

namespace engine::client
{
	/// Charge l'arbre de dialogue d'un interactable depuis la config.
	/// \param cfg config du jeu.
	/// \param base préfixe de clé de l'interactable, ex. "world.interactables.0."
	///        (avec le point final).
	/// \param legacyLines lignes de l'ancien champ `dialogue` (fallback si pas d'arbre).
	/// \return un DialogueTree normalisé. Si le bloc `dialogue_tree` est absent OU
	///         invalide, retombe sur BuildTreeFromLegacyLines(legacyLines) (qui, si
	///         vide, produit un nœud unique avec un seul « Au revoir »).
	/// Effet de bord : logue les erreurs de validation.
	DialogueTree LoadDialogueTree(const engine::core::Config& cfg,
	                              const std::string& base,
	                              const std::vector<std::string>& legacyLines);

	/// Convertit une chaîne d'action config en enum (défaut : Continue).
	DialogueAction ParseDialogueAction(const std::string& s);

} // namespace engine::client
```

- [ ] **Step 2: Implémenter `DialogueConfigLoader.cpp`**

Create `src/client/dialogue/DialogueConfigLoader.cpp` (remplace le stub éventuel de Task 7) :

```cpp
#include "src/client/dialogue/DialogueConfigLoader.h"

#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"

namespace engine::client
{
	DialogueAction ParseDialogueAction(const std::string& s)
	{
		if (s == "accept_quest")   return DialogueAction::AcceptQuest;
		if (s == "complete_quest") return DialogueAction::CompleteQuest;
		if (s == "end")            return DialogueAction::End;
		return DialogueAction::Continue;
	}

	DialogueTree LoadDialogueTree(const engine::core::Config& cfg,
	                              const std::string& base,
	                              const std::vector<std::string>& legacyLines)
	{
		const std::string treeBase = base + "dialogue_tree.";
		const int nodeCount = static_cast<int>(cfg.GetInt(treeBase + "nodes.count", 0));

		if (nodeCount <= 0)
			return BuildTreeFromLegacyLines(legacyLines); // pas d'arbre => legacy

		DialogueTree tree;
		tree.startNodeId = cfg.GetString(treeBase + "start", "");

		for (int n = 0; n < nodeCount; ++n)
		{
			const std::string nb = treeBase + "nodes." + std::to_string(n) + ".";
			DialogueNode node;
			node.id = cfg.GetString(nb + "id", "");

			const int lineCount = static_cast<int>(cfg.GetInt(nb + "lines.count", 0));
			for (int l = 0; l < lineCount; ++l)
			{
				const std::string lb = nb + "lines." + std::to_string(l) + ".";
				DialogueLine line;
				line.text  = cfg.GetString(lb + "text", "");
				line.isCue = cfg.GetBool(lb + "cue", false);
				node.lines.push_back(std::move(line));
			}

			const int choiceCount = static_cast<int>(cfg.GetInt(nb + "choices.count", 0));
			for (int c = 0; c < choiceCount; ++c)
			{
				const std::string cb = nb + "choices." + std::to_string(c) + ".";
				DialogueChoice choice;
				choice.text       = cfg.GetString(cb + "text", "");
				choice.nextNodeId = cfg.GetString(cb + "next", "");
				choice.action     = ParseDialogueAction(cfg.GetString(cb + "action", "continue"));
				choice.questId    = static_cast<int>(cfg.GetInt(cb + "questId", -1));
				choice.icon       = cfg.GetString(cb + "icon", "");
				node.choices.push_back(std::move(choice));
			}

			tree.nodes.push_back(std::move(node));
		}

		const DialogueValidationResult vr = NormalizeDialogueTree(tree);
		if (!vr.ok)
		{
			for (const std::string& err : vr.errors)
				LOG_WARN(Core, "[DialogueConfigLoader] {}{} : {}", base, "dialogue_tree", err);
			LOG_WARN(Core, "[DialogueConfigLoader] arbre invalide pour '{}' — fallback legacy", base);
			return BuildTreeFromLegacyLines(legacyLines);
		}
		return tree;
	}

} // namespace engine::client
```

- [ ] **Step 3: Vérifier (CI)** — pousser ; `build-linux` doit compiler `engine_core`
(le loader n'a pas de test unitaire dédié : sa logique de validation est couverte par
les tests de `NormalizeDialogueTree`, Task 1).

- [ ] **Step 4: Commit**

```bash
git add src/client/dialogue/DialogueConfigLoader.h src/client/dialogue/DialogueConfigLoader.cpp
git commit -m "feat(dialogue): chargement config.json -> DialogueTree (clés plates + fallback legacy)"
```

---

## Task 9: `Engine.h` — extension `InteractableEntity` + membres dialogue

**Files:**
- Modify: `src/client/app/Engine.h`

- [ ] **Step 1: Inclure les en-têtes dialogue**

En haut de `src/client/app/Engine.h`, avec les autres `#include "src/client/..."`,
ajouter :

```cpp
#include "src/client/dialogue/DialogueTree.h"
#include "src/client/dialogue/DialoguePresenter.h"
#include "src/client/dialogue/QuestConversationJournal.h"
```

- [ ] **Step 2: Étendre `InteractableEntity`**

Dans `struct InteractableEntity` (~ligne 726), ajouter le champ arbre (après le champ
`std::vector<std::string> dialogue;` existant) :

```cpp
		/// Arbre de dialogue (format moderne). Si vide, le client le construit à partir
		/// de \ref dialogue (legacy) au chargement. \see DialogueConfigLoader.
		engine::client::DialogueTree dialogueTree;
```

- [ ] **Step 3: Ajouter les membres dialogue à `Engine`**

Avec les autres presenters/renderers membres (zone des `std::unique_ptr<...ImGuiRenderer>`),
ajouter :

```cpp
	// --- Dialogue PNJ (cellule dédiée) ---
	engine::client::DialoguePresenter m_dialogue;                       ///< Logique runtime du dialogue.
	std::unique_ptr<engine::client::DialogueImGuiRenderer> m_dialogueImGui; ///< Rendu (Windows).
	std::unique_ptr<engine::client::QuestConversationJournal> m_dialogueJournal; ///< Journal local (créé au login).
	bool m_dialogueActive = false;                                      ///< Vrai pendant un dialogue (verrouille le déplacement).
```

> `DialogueImGuiRenderer` est une déclaration anticipée suffisante via `unique_ptr` ;
> ajouter `namespace engine::client { class DialogueImGuiRenderer; }` en forward-declare
> en haut de `Engine.h` si l'en-tête du renderer (Task 11) n'est pas encore inclus, pour
> éviter une dépendance d'include lourde.

- [ ] **Step 4: Vérifier (CI)** — pousser ; `build-linux` compile (Engine.h fait partie
du client ; pas de test unitaire ici).

- [ ] **Step 5: Commit**

```bash
git add src/client/app/Engine.h
git commit -m "feat(dialogue): Engine.h — InteractableEntity.dialogueTree + membres presenter/journal"
```

---

## Task 10: `Engine.cpp` — chargement, ouverture sur E, verrouillage, tick, fermeture

**Files:**
- Modify: `src/client/app/Engine.cpp`

- [ ] **Step 1: Charger l'arbre à la construction des interactables**

Dans le chargement des interactables (~ligne 4898-4915), après le remplissage de
`e.dialogue` (legacy) et **avant** `m_interactables.push_back(e);`, ajouter :

```cpp
		// Construit l'arbre de dialogue moderne (ou fallback legacy depuis e.dialogue).
		e.dialogueTree = engine::client::LoadDialogueTree(m_cfg, base, e.dialogue);
```

Ajouter l'include en haut de `Engine.cpp` :

```cpp
#include "src/client/dialogue/DialogueConfigLoader.h"
#include "src/client/render/DialogueImGuiRenderer.h"
```

- [ ] **Step 2: Créer le journal au login + brancher le sink/callback**

Là où `m_questUi` / la session du personnage sont initialisés (après obtention du
`characterId`), ajouter :

```cpp
	// Journal de conversation local pour ce personnage + branchement du presenter.
	m_dialogueJournal = std::make_unique<engine::client::QuestConversationJournal>(m_cfg, characterId);
	m_dialogue.SetJournalSink(m_dialogueJournal.get());
	m_dialogue.SetQuestActionCallback(
		[this](engine::client::DialogueAction action, int questId)
		{
			if (questId < 0) return;
			if (action == engine::client::DialogueAction::AcceptQuest)
				m_questUi.AcceptQuest(questId);
			else if (action == engine::client::DialogueAction::CompleteQuest)
				m_questUi.CompleteQuest(questId);
		});
```

> Vérifier les signatures réelles `m_questUi.AcceptQuest(int)` / `CompleteQuest(int)`
> dans `src/client/quest/QuestUi.h` et ajuster si besoin (le presenter quête expose
> ces callbacks vers le réseau ; cf. spec section 2).

- [ ] **Step 3: Ouvrir le dialogue sur E (et retirer la poussée chat)**

Dans le bloc d'interaction sur la touche E (~ligne 8730-8786), pour le cas PNJ avec
dialogue : **remplacer** la poussée actuelle dans le chat (`[PNJ] label: text` /
`[Interaction] ...` et l'incrément de `dialogueCursor`) par l'ouverture de la cellule :

```cpp
		if (e.isNpc && !e.dialogueTree.nodes.empty())
		{
			engine::client::DialogueNpcRef npc;
			npc.label       = e.label;
			npc.role        = m_cfg.GetString(base + "role", ""); // si chargé ; sinon e.role
			npc.entityIndex = nearestI;
			m_dialogue.OpenDialogue(e.dialogueTree, npc);
			m_dialogueActive = true;
		}
```

> Supprimer aussi le message de proximité poussé dans le chat
> (`[Interaction] Près de XXX - appuyez sur E`) **uniquement** s'il était lié au
> dialogue ; garder l'invite « appuyez sur E » sous forme de prompt HUD/overlay si elle
> existe ailleurs. Ne pas casser les objets non-PNJ (coffres, etc.) qui gardent leur
> `e.message`.

- [ ] **Step 4: Verrouiller le déplacement pendant le dialogue**

Dans la construction du `MoveInput` / mapping d'input de déplacement, neutraliser le
déplacement quand `m_dialogueActive` (laisser la caméra libre) :

```cpp
	if (m_dialogueActive)
	{
		moveInput.moveDirXZ = engine::math::Vec3{0.0f, 0.0f, 0.0f};
		moveInput.run = moveInput.sprint = moveInput.crouch = false;
		moveInput.jumpPressed = false;
	}
```

> Placer ce gardien juste **après** la lecture des touches de déplacement et **avant**
> `m_characterController.Update(...)`. Ne pas toucher la caméra (regard libre).

- [ ] **Step 5: Tick distance + fermeture par Échap, et appel du renderer**

Par frame, après le calcul de la distance au PNJ courant (réutiliser la boucle XZ
~ligne 8722-8728 ; si le dialogue est actif, calculer la distance vers
`m_dialogue.Npc().entityIndex`) :

```cpp
	if (m_dialogueActive)
	{
		float dist = 999.0f;
		const int idx = m_dialogue.Npc().entityIndex;
		if (idx >= 0 && idx < static_cast<int>(m_interactables.size()))
		{
			const engine::math::Vec3 pp = m_characterController.GetPosition();
			const engine::math::Vec3 np = m_interactables[idx].position;
			const float dx = np.x - pp.x, dz = np.z - pp.z;
			dist = std::sqrt(dx * dx + dz * dz);
		}
		m_dialogue.Tick(deltaSeconds, dist);

		// Échap ferme le dialogue.
		if (input.WasKeyPressed(engine::platform::Key::Escape))
			m_dialogue.Close(engine::client::DialogueCloseReason::UserClose);

		// Synchronise le flag : toute fermeture (distance, Échap, choix End) libère le déplacement.
		if (!m_dialogue.IsActive())
			m_dialogueActive = false;
	}
```

> Adapter `input.WasKeyPressed(...)` / `engine::platform::Key::Escape` au véritable API
> d'input (cf. usage existant de la touche Échap ailleurs dans `Engine.cpp`).

Dans `Engine::Render()`, avec les autres renderers ImGui :

```cpp
	if (m_dialogueImGui && m_dialogue.IsActive())
		m_dialogueImGui->Render(m_dialogue, viewportWidth, viewportHeight);
```

- [ ] **Step 6: Vérifier (CI + manuel)** — pousser ; `build-linux` compile. Vérification
fonctionnelle = en jeu (Task 13).

- [ ] **Step 7: Commit**

```bash
git add src/client/app/Engine.cpp
git commit -m "feat(dialogue): Engine — ouverture sur E (hors chat), verrouillage déplacement, rupture 1,50 m"
```

---

## Task 11: `DialogueImGuiRenderer` — fenêtre centrale (disposition B)

**Files:**
- Create: `src/client/render/DialogueImGuiRenderer.h`
- Create: `src/client/render/DialogueImGuiRenderer.cpp`
- Modify: `CMakeLists.txt` (racine) — ajouter le `.cpp` à la cible qui contient
  `src/client/render/ChatImGuiRenderer.cpp` (rechercher `ChatImGuiRenderer.cpp`).

> Ce renderer n'est **pas** testé en CI (ImGui/Windows). Vérification en jeu (Task 13).
> Documenter chaque fonction (`///`), conformément aux conventions de rendu du repo.

- [ ] **Step 1: Écrire l'en-tête**

Create `src/client/render/DialogueImGuiRenderer.h` :

```cpp
#pragma once

#include <cstdint>

namespace engine::client
{
	class DialoguePresenter;

	/// Rendu ImGui de la cellule de dialogue PNJ (fenêtre centrale parchemin,
	/// disposition B). Lit l'état du presenter ; émet la sélection de choix et la
	/// fermeture via le presenter. À appeler en main thread, pendant la passe ImGui.
	class DialogueImGuiRenderer final
	{
	public:
		/// Dessine la fenêtre si le dialogue est actif.
		/// \param presenter source de vérité (lecture) + cible des actions joueur.
		/// \param viewportWidth/Height dimensions écran (px) pour centrer la fenêtre.
		void Render(DialoguePresenter& presenter, uint32_t viewportWidth, uint32_t viewportHeight);
	};

} // namespace engine::client
```

- [ ] **Step 2: Implémenter le rendu**

Create `src/client/render/DialogueImGuiRenderer.cpp` :

```cpp
#include "src/client/render/DialogueImGuiRenderer.h"

#include "src/client/dialogue/DialoguePresenter.h"

#include "imgui.h" // même chemin d'include qu'utilisé par ChatImGuiRenderer.cpp

#include <cstdio>

namespace engine::client
{
	void DialogueImGuiRenderer::Render(DialoguePresenter& presenter,
	                                   uint32_t viewportWidth, uint32_t viewportHeight)
	{
		if (!presenter.IsActive() || presenter.CurrentNode() == nullptr)
			return;

		const DialogueNode& node = *presenter.CurrentNode();

		// --- Fenêtre centrale (disposition B) ---
		const ImVec2 winSize(560.0f, 460.0f);
		ImGui::SetNextWindowSize(winSize, ImGuiCond_Always);
		ImGui::SetNextWindowPos(
			ImVec2(viewportWidth * 0.5f, viewportHeight * 0.5f),
			ImGuiCond_Always, ImVec2(0.5f, 0.5f));

		// Skin parchemin + cadre or (couleurs alignées sur la maquette).
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.91f, 0.86f, 0.75f, 0.98f));
		ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.78f, 0.64f, 0.29f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Text,     ImVec4(0.17f, 0.13f, 0.07f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
		                             | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
		bool open = true;
		if (ImGui::Begin("##dialogue_pnj", &open, flags))
		{
			// Barre de titre custom : nom + rôle.
			ImGui::TextUnformatted(presenter.Npc().label.c_str());
			if (!presenter.Npc().role.empty())
			{
				ImGui::SameLine();
				ImGui::TextDisabled("· %s", presenter.Npc().role.c_str());
			}
			ImGui::Separator();

			// --- Zone texte (auto-scroll + scrollbar droite + word-wrap) ---
			const float choicesHeight = 40.0f * node.choices.size() + 28.0f;
			const float textHeight = winSize.y - 110.0f - choicesHeight;
			ImGui::BeginChild("##dlg_text", ImVec2(0.0f, textHeight), true,
			                  ImGuiWindowFlags_AlwaysVerticalScrollbar);
			{
				const float wrapWidth = ImGui::GetContentRegionAvail().x;
				for (const DialogueLine& line : node.lines)
				{
					ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapWidth); // word-wrap
					if (line.isCue)
						ImGui::TextDisabled("%s", line.text.c_str()); // didascalie atténuée
					else
						ImGui::TextUnformatted(line.text.c_str());
					ImGui::PopTextWrapPos();
					ImGui::Spacing();
				}

				// Métriques pour l'auto-scroll du presenter.
				const float contentH = ImGui::GetScrollMaxY() + ImGui::GetWindowHeight();
				presenter.SetViewMetrics(contentH, ImGui::GetWindowHeight());

				// Détecte un scroll manuel (souris/molette) vs l'auto-scroll programmé.
				const float current = ImGui::GetScrollY();
				if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
					presenter.OnUserScroll(current);

				// Applique l'offset auto-scroll calculé par le presenter.
				if (presenter.AutoScrollEnabled())
					ImGui::SetScrollY(presenter.ScrollOffset());
			}
			ImGui::EndChild();

			// --- Zone réponses (2..5 choix, word-wrap, raccourcis 1..5) ---
			ImGui::Spacing();
			ImGui::TextDisabled("Vos réponses :");
			const float wrapW = ImGui::GetContentRegionAvail().x;
			for (size_t i = 0; i < node.choices.size(); ++i)
			{
				char label[512];
				std::snprintf(label, sizeof(label), "%zu. %s %s##choice%zu",
				              i + 1,
				              node.choices[i].icon.c_str(),
				              node.choices[i].text.c_str(), i);

				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapW);
				const bool clicked = ImGui::Button(label, ImVec2(wrapW, 0.0f));
				ImGui::PopTextWrapPos();

				// Raccourcis clavier 1..5.
				const bool keyed = (i < 5) &&
					ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + static_cast<int>(i)));
				if (clicked || keyed)
				{
					presenter.SelectChoice(i);
					break; // l'état a changé (navigation/fermeture)
				}
			}
		}
		ImGui::End();

		ImGui::PopStyleVar();
		ImGui::PopStyleColor(3);

		// Fermeture via la croix de la fenêtre.
		if (!open)
			presenter.Close(DialogueCloseReason::UserClose);
	}

} // namespace engine::client
```

> Adapter le chemin d'include ImGui (`"imgui.h"` vs `<imgui.h>`) et les enums de touche
> (`ImGuiKey_1`) à la version d'ImGui du projet — calquer sur `ChatImGuiRenderer.cpp`.
> La police décorative (Windlass) est la police par défaut côté jeu ; aucun chargement
> spécifique requis ici (à l'inverse de l'éditeur monde qui force Arial).

- [ ] **Step 3: Câbler dans CMake**

Rechercher `ChatImGuiRenderer.cpp` dans `CMakeLists.txt` et ajouter, dans la même
cible/liste :

```cmake
  src/client/render/DialogueImGuiRenderer.cpp
```

- [ ] **Step 4: Vérifier (CI)** — pousser ; `build-windows` doit compiler. Vérification
visuelle en jeu (Task 13).

- [ ] **Step 5: Commit**

```bash
git add src/client/render/DialogueImGuiRenderer.h src/client/render/DialogueImGuiRenderer.cpp CMakeLists.txt
git commit -m "feat(dialogue): DialogueImGuiRenderer — fenêtre centrale, auto-scroll, choix 1-5"
```

---

## Task 12: Contenu `config.json` — PNJ dialoguant

**Files:**
- Modify: `config.json` (entrée `world.interactables.<i>` d'un PNJ existant).

- [ ] **Step 1: Ajouter un arbre de dialogue à un PNJ + resserrer la portée**

Repérer un PNJ (`"npc": true`) dans `world.interactables` (ex. l'index `0`,
« Villageois »). Mettre son `radius` à `1.5`, ajouter `role` et `dialogue_tree` :

```json
"0": {
  "x": 4.0, "z": 0.0, "radius": 1.5, "npc": true,
  "label": "Aldric le Veilleur",
  "role": "Garde du pont",
  "dialogue_tree": {
    "start": "intro",
    "nodes": {
      "count": 2,
      "0": {
        "id": "intro",
        "lines": {
          "count": 2,
          "0": { "text": "Halte, voyageur. Peu de gens empruntent encore ce pont." },
          "1": { "text": "(Il pose la main sur le pommeau de son épée.)", "cue": true }
        },
        "choices": {
          "count": 3,
          "0": { "text": "Parle-moi de ces ruines.", "next": "ruines", "icon": "❓" },
          "1": { "text": "J'accepte ta quête.", "action": "accept_quest", "questId": 1012, "icon": "⚔️" },
          "2": { "text": "Au revoir.", "action": "end", "icon": "👋" }
        }
      },
      "1": {
        "id": "ruines",
        "lines": {
          "count": 1,
          "0": { "text": "Les ruines en aval grouillent de pillards depuis trois jours." }
        },
        "choices": {
          "count": 2,
          "0": { "text": "J'accepte ta quête.", "action": "accept_quest", "questId": 1012, "icon": "⚔️" },
          "1": { "text": "Je reviendrai plus tard.", "action": "end", "icon": "👋" }
        }
      }
    }
  }
}
```

> Le tableau JSON est représenté à la mode `engine::core::Config` : un objet avec une
> clé `count` + des clés numériques `"0"`, `"1"`, … (comme l'ancien `dialogue`). Garder
> l'ancien champ `dialogue` d'un autre PNJ pour valider la rétro-compatibilité legacy.

- [ ] **Step 2: Vérifier (manuel)** — au lancement, le client doit logger le chargement
sans erreur de validation (`[DialogueConfigLoader] ...` ne doit pas apparaître en WARN).

- [ ] **Step 3: Commit**

```bash
git add config.json
git commit -m "feat(dialogue): PNJ Aldric — arbre de dialogue de démo + portée 1,5 m"
```

---

## Task 13: Vérification finale + note de déploiement

**Files:** aucun (vérification).

- [ ] **Step 1: CI verte** — sur `feat/dialogue-pnj` : `build-linux` (dont
`dialogue_presenter_tests` PASS, 14 tests) **et** `build-windows` verts.

- [ ] **Step 2: Vérification en jeu (Windows)** — checklist manuelle :
  - [ ] S'approcher d'Aldric (< 1,5 m), presser **E** → la **cellule centrale** s'ouvre,
        **rien dans le chat**.
  - [ ] Le texte **revient à la ligne** et **défile lentement** vers le bas ; la
        **scrollbar droite** permet de remonter ; l'auto-scroll **reprend** en bas.
  - [ ] Les **choix** (2 puis 3) s'affichent, reviennent à la ligne, réagissent au
        **clic** et aux touches **1-3**.
  - [ ] Pendant le dialogue, le **déplacement est bloqué** (caméra libre).
  - [ ] En forçant un éloignement > 1,5 m (ex. téléport/knockback), le dialogue **se ferme**.
  - [ ] **Échap** et la **croix** ferment ; le déplacement reprend.
  - [ ] Choisir « J'accepte » → quête 1012 acceptée **et** une ligne ajoutée à
        `<paths.content>/quest_journal/character_<id>.jsonl`.

- [ ] **Step 3: Note de déploiement (dans la description de PR)**

```
**Déploiement** : ✅ client uniquement, pas de redéploiement serveur.
(Dialogue piloté par config.json côté client ; journal de conversation écrit en
local ; aucun nouvel opcode, handler ou migration DB. AcceptQuest/CompleteQuest
réutilisent les opcodes quête existants, déjà déployés.)
```

- [ ] **Step 4: Ouvrir la PR** depuis `feat/dialogue-pnj` vers `main`, CI verte, en
incluant la note de déploiement ci-dessus.

---

## Notes de revue (self-review)

- **Couverture spec** : cellule dédiée hors chat (T10) ; auto-scroll fluide + scrollbar
  droite (T5, T11) ; word-wrap texte & réponses (T11) ; 2-5 choix (T11, validation 1..5
  en T1) ; immobilité (T10 step 4) ; rupture 1,50 m (T4) ; journal local quête (T6) ;
  source config.json (T8, T12) ; disposition B (T11). ✅ Tous couverts.
- **Pas de placeholder** : tout le code testable est fourni ; les points « adapter à
  l'API réelle » concernent uniquement des intégrations non testées en CI (input ImGui,
  signatures `m_questUi`) et renvoient à un fichier existant précis à calquer.
- **Cohérence des types** : `DialogueAction`, `DialogueChoice`, `DialogueNode`,
  `DialogueTree`, `DialoguePresenter`, `QuestConversationEntry`, `IQuestConversationSink`
  utilisés de façon identique entre tâches.
