# SP1 — Cycle de vie des quêtes piloté par le joueur — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Transformer le système de quêtes shard « tout automatique » en un cycle piloté par le joueur (accept explicite au PNJ → progression → turn-in explicite au PNJ, récompenses versées au turn-in).

**Architecture:** Le shard (`QuestRuntime`) reste la source de vérité data-driven. On étend l'enum d'états (`Offered`, `ReadyToTurnIn`), on ajoute les champs `giver`/`turnIn` au JSON de contenu, trois nouveaux messages wire (giver-list, accept, turn-in), les handlers `ServerApp` correspondants, et une table de compat au chargement des personnages. Le versement des récompenses migre de la complétion auto vers le handler de turn-in.

**Tech Stack:** C++20, `src/shardd/gameplay/quest/QuestRuntime`, `src/shared/network/ServerProtocol`, `src/shardd/gameplay/character/CharacterPersistence`, `src/shared/server_bootstrap/ServerApp`. Tests via CTest (`lcdlln_add_simple_test`).

## Global Constraints

- **Commentaires en français**, clarté > brièveté (convention repo).
- **PascalCase** pour tout nouveau symbole/fichier ; les `m_camelCase` / `kPascalCase` existants sont conservés.
- **Ne jamais employer le terme « CMANGOS »** dans le code, commits, docs ou messages.
- **Pas de build local** (cmake/MSVC/vcpkg absents des shells) : la vérification des tests se fait **via la CI** — `build-linux.yml` exécute `ctest`. Les étapes « run test » décrivent la commande `ctest` attendue en CI ; l'agent ne compile pas en local, il pousse et lit la CI.
- **Pas de `assert()` nu dans les tests** : la CI `build-linux` compile en `NDEBUG` → les `assert` sont retirés. Utiliser le style d'assertion des tests existants (ex. `CreatureArchetypeLibraryTests.cpp` : conditions + `std::cerr` + `return 1`).
- **Wire-breaking** : ce SP bumpe `kProtocolVersion` de **13 → 14** (`src/shared/network/ServerProtocol.h:62`). ⚠️ **REDÉPLOIEMENT SHARD requis**, en lock-step avec le client SP2.
- **`questId` = `std::string`** partout côté système B (ne pas réintroduire d'`uint32`).
- Aucun nouveau `.cpp` partagé n'est créé (on modifie des fichiers existants) → pas d'ajout à la liste `server_app` du `CMakeLists`. Seuls des fichiers de **test** sont créés (registrés via `lcdlln_add_simple_test`).

---

## Structure de fichiers

**Modifiés :**
- `src/shardd/gameplay/quest/QuestRuntime.h` — enum `QuestStatus` étendu ; champs `giverId`/`turnInId` ; nouvelles méthodes pures.
- `src/shardd/gameplay/quest/QuestRuntime.cpp` — parse `giver`/`turnIn` ; `SyncQuestStates`→`Offered` ; `ApplyEvent`→`ReadyToTurnIn` sans reward ; helpers `CanAccept`/`CanTurnIn`/`TakeRewardOnTurnIn`.
- `game/data/quests/quest_definitions.json` — migration : ajouter `giver`/`turnIn`.
- `src/shared/network/ServerProtocol.h` — bump version ; 3 `MessageKind` ; 3 structs ; déclarations encode/decode.
- `src/shared/network/ServerProtocol.cpp` — encode/decode des 3 messages.
- `src/shardd/gameplay/character/CharacterPersistence.cpp` — map de compat au load ; save enum étendu.
- `src/shared/server_bootstrap/ServerApp.h` — déclarations `HandleAcceptQuest`/`HandleTurnInQuest` + helper giver-list.
- `src/shared/server_bootstrap/ServerApp.cpp` — dispatch + handlers ; retrait du reward de `ApplyQuestEvent` ; émission giver-list au `Talk`.

**Créés (tests) :**
- `src/shardd/gameplay/quest/QuestRuntimeTests.cpp`
- `src/shardd/gameplay/character/CharacterPersistenceQuestCompatTests.cpp`
- Ajouts wire dans le test existant `src/shared/network/ServerProtocolTests.cpp`.
- Enregistrements CTest dans `src/CMakeLists.txt`.

---

## Task 1 : Enum d'états étendu + champs giver/turnIn + parse JSON

**Files:**
- Modify: `src/shardd/gameplay/quest/QuestRuntime.h` (enum `QuestStatus` ~L23-28 ; struct `QuestDefinition` ~L47-53)
- Modify: `src/shardd/gameplay/quest/QuestRuntime.cpp` (`GetQuestStatusName` ~L486-497 ; `LoadDefinitions` boucle quête ~L845-932)
- Modify: `game/data/quests/quest_definitions.json`
- Create: `src/shardd/gameplay/quest/QuestRuntimeTests.cpp`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `enum class QuestStatus : uint8_t { Locked=0, Offered=1, Active=2, ReadyToTurnIn=3, Completed=4 }`
  - `QuestDefinition` gagne `std::string giverId;` et `std::string turnInId;`
  - `const char* GetQuestStatusName(QuestStatus)` gère les 5 valeurs.

- [ ] **Step 1 : Écrire le test de chargement (échec attendu)**

Créer `src/shardd/gameplay/quest/QuestRuntimeTests.cpp`. S'inspirer du style de `src/shardd/gameplay/creature/CreatureArchetypeLibraryTests.cpp` pour charger un JSON de fixture (écrire un fichier temporaire + `Config` pointant `server.quest_definitions_path` dessus, puis `Init()`).

```cpp
// Tests du QuestRuntime — cycle de vie piloté par le joueur (SP1).
#include "src/shardd/gameplay/quest/QuestRuntime.h"
#include "src/shared/core/Config.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

using engine::server::QuestRuntime;
using engine::server::QuestStatus;
using engine::server::QuestState;
using engine::server::QuestStepType;
using engine::server::QuestProgressDelta;

namespace
{
    int g_failures = 0;

    void Check(bool condition, const char* label)
    {
        if (!condition)
        {
            std::cerr << "[FAIL] " << label << "\n";
            ++g_failures;
        }
    }

    // Écrit un JSON de définitions dans un fichier temporaire relatif au content root
    // de test, et construit un QuestRuntime initialisé dessus.
    // Retourne le runtime prêt (Init() déjà appelé). Le fichier reste sur disque
    // pour la durée du test (nettoyé par l'OS / non critique).
    std::string WriteFixture(const std::string& jsonBody)
    {
        const std::string relPath = "quests/quest_definitions_test.json";
        // NB : suivre CreatureArchetypeLibraryTests pour la résolution du content root.
        std::ofstream out("game/data/" + relPath, std::ios::binary | std::ios::trunc);
        out << jsonBody;
        out.close();
        return relPath;
    }
}

int main()
{
    // Une définition minimale avec giver/turnIn.
    const std::string json = R"JSON({
      "quests": [
        { "id": "q1", "giver": "npc:marn", "turnIn": "npc:marn",
          "prereqs": [], "steps": [ { "type": "kill", "target": "mob:1", "requiredCount": 2 } ],
          "rewards": { "xp": 10, "gold": 5, "items": [] } }
      ]
    })JSON";

    engine::core::Config config;
    const std::string relPath = WriteFixture(json);
    config.SetString("server.quest_definitions_path", relPath);

    QuestRuntime runtime(config);
    Check(runtime.Init(), "Init charge une définition avec giver/turnIn");

    const auto* def = runtime.FindQuestDefinition("q1");
    Check(def != nullptr, "q1 trouvée");
    if (def != nullptr)
    {
        Check(def->giverId == "npc:marn", "giver parsé");
        Check(def->turnInId == "npc:marn", "turnIn parsé");
    }

    if (g_failures != 0)
    {
        std::cerr << g_failures << " assertion(s) échouée(s)\n";
        return 1;
    }
    std::cout << "OK\n";
    return 0;
}
```

- [ ] **Step 2 : Enregistrer le test dans CMake**

Dans `src/CMakeLists.txt`, après le bloc `quest_state_tests` (~L584-586), ajouter :

```cmake
  # SP1 : QuestRuntime — cycle de vie piloté par le joueur (parse giver/turnIn, transitions).
  lcdlln_add_simple_test(quest_runtime_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/quest/QuestRuntimeTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/quest/QuestRuntime.cpp)
```

Et l'`add_test` correspondant à côté des autres (le helper `lcdlln_add_simple_test` s'en charge s'il génère l'`add_test` ; sinon suivre le pattern des voisins).

- [ ] **Step 3 : Vérifier l'échec en CI**

Run (CI build-linux) : `ctest -R quest_runtime_tests --output-on-failure`
Expected : **FAIL** — `QuestDefinition` n'a pas encore `giverId`/`turnInId` → erreur de compilation.

- [ ] **Step 4 : Étendre l'enum et la struct**

Dans `src/shardd/gameplay/quest/QuestRuntime.h`, remplacer l'enum `QuestStatus` :

```cpp
	/// Statut runtime d'une quête pour un joueur. L'ordre est significatif
	/// (persisté et transmis wire) : ne pas réordonner sans migration.
	enum class QuestStatus : uint8_t
	{
		Locked        = 0,   ///< pré-requis non remplis (interne, non affiché)
		Offered       = 1,   ///< proposée au PNJ giver, pas encore acceptée
		Active        = 2,   ///< acceptée, en cours
		ReadyToTurnIn = 3,   ///< étapes remplies, à rendre (pas encore récompensée)
		Completed     = 4,   ///< rendue + récompensée (terminal)
	};
```

Dans `struct QuestDefinition`, ajouter après `questId` :

```cpp
		std::string giverId;    ///< PNJ qui propose la quête (même espace que targetId du Talk)
		std::string turnInId;   ///< PNJ où rendre la quête (souvent = giverId)
```

- [ ] **Step 5 : Mettre à jour `GetQuestStatusName`**

Dans `QuestRuntime.cpp` (~L486), couvrir les 5 valeurs :

```cpp
	const char* GetQuestStatusName(QuestStatus status)
	{
		switch (status)
		{
		case QuestStatus::Locked:        return "locked";
		case QuestStatus::Offered:       return "offered";
		case QuestStatus::Active:        return "active";
		case QuestStatus::ReadyToTurnIn: return "ready_to_turn_in";
		case QuestStatus::Completed:     return "completed";
		}
		return "unknown";
	}
```

- [ ] **Step 6 : Parser `giver`/`turnIn` (obligatoires) dans `LoadDefinitions`**

Dans `QuestRuntime.cpp`, juste après `definition.questId = idValue->stringValue;` (~L846), ajouter la validation :

```cpp
			const JsonValue* giverValue  = FindObjectMember(questValue, "giver");
			const JsonValue* turnInValue = FindObjectMember(questValue, "turnIn");
			if (giverValue == nullptr || giverValue->type != JsonType::String || giverValue->stringValue.empty())
			{
				LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: quest '{}'.giver must be a non-empty string", definition.questId);
				m_definitions.clear();
				return false;
			}
			if (turnInValue == nullptr || turnInValue->type != JsonType::String || turnInValue->stringValue.empty())
			{
				LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: quest '{}'.turnIn must be a non-empty string", definition.questId);
				m_definitions.clear();
				return false;
			}
			definition.giverId  = giverValue->stringValue;
			definition.turnInId = turnInValue->stringValue;
```

- [ ] **Step 7 : Migrer le JSON de contenu**

Remplacer `game/data/quests/quest_definitions.json` pour ajouter `giver`/`turnIn` à chaque quête :

```json
{
  "quests": [
    {
      "id": "hunt_collect_enter",
      "giver": "npc:elder_marn",
      "turnIn": "npc:elder_marn",
      "prereqs": [],
      "steps": [
        { "type": "kill", "target": "mob:100", "requiredCount": 1 },
        { "type": "collect", "target": "item:2001", "requiredCount": 1 },
        { "type": "enter", "target": "zone:2", "requiredCount": 1 }
      ],
      "rewards": { "xp": 100, "gold": 25, "items": [ { "itemId": 3001, "quantity": 1 } ] }
    },
    {
      "id": "report_to_guard",
      "giver": "npc:guard_captain",
      "turnIn": "npc:guard_captain",
      "prereqs": ["hunt_collect_enter"],
      "steps": [ { "type": "talk", "target": "npc:guard_captain", "requiredCount": 1 } ],
      "rewards": { "xp": 25, "gold": 5, "items": [] }
    }
  ]
}
```

- [ ] **Step 8 : Ajouter un test de rejet (giver manquant)**

Dans `QuestRuntimeTests.cpp` `main()`, avant le bloc final, ajouter un cas :

```cpp
    {
        const std::string bad = R"JSON({ "quests": [
          { "id": "nogiver", "turnIn": "npc:x", "prereqs": [],
            "steps": [ { "type": "talk", "target": "npc:x", "requiredCount": 1 } ],
            "rewards": { "xp": 1, "gold": 0, "items": [] } } ] })JSON";
        engine::core::Config badCfg;
        const std::string p = WriteFixture(bad);
        badCfg.SetString("server.quest_definitions_path", p);
        QuestRuntime badRuntime(badCfg);
        Check(!badRuntime.Init(), "Init rejette une quête sans giver");
    }
```

- [ ] **Step 9 : Vérifier le succès en CI**

Run : `ctest -R quest_runtime_tests --output-on-failure`
Expected : **PASS**.

- [ ] **Step 10 : Commit**

```bash
git add src/shardd/gameplay/quest/QuestRuntime.h src/shardd/gameplay/quest/QuestRuntime.cpp \
        src/shardd/gameplay/quest/QuestRuntimeTests.cpp src/CMakeLists.txt \
        game/data/quests/quest_definitions.json
git commit -m "feat(quests): enum d'états étendu (Offered/ReadyToTurnIn) + giver/turnIn au JSON"
```

---

## Task 2 : Transitions pilotées par le joueur (Sync→Offered, ApplyEvent→ReadyToTurnIn) + helpers purs

**Files:**
- Modify: `src/shardd/gameplay/quest/QuestRuntime.h` (déclarations `CanAccept`/`CanTurnIn`/`TakeRewardOnTurnIn`)
- Modify: `src/shardd/gameplay/quest/QuestRuntime.cpp` (`SyncQuestStates` ~L610-613 ; `ApplyEvent` complétion ~L733-746 ; définitions des helpers)
- Modify: `src/shardd/gameplay/quest/QuestRuntimeTests.cpp`

**Interfaces:**
- Consumes: `QuestStatus`, `QuestDefinition`, `QuestState` (Task 1).
- Produces (méthodes membres `const` de `QuestRuntime`) :
  - `bool CanAccept(const QuestState& state, const QuestDefinition& def, std::string_view giverTargetId) const;`
  - `bool CanTurnIn(const QuestState& state, const QuestDefinition& def, std::string_view npcTargetId) const;`
  - `const QuestReward* TakeRewardOnTurnIn(const QuestDefinition& def) const;` (pointeur vers `def.rewards`, jamais nul)

- [ ] **Step 1 : Écrire le test des transitions (échec attendu)**

Ajouter dans `QuestRuntimeTests.cpp` `main()` (fixture `q1`, kill mob:1 x2) :

```cpp
    {
        std::vector<QuestState> states;
        std::vector<QuestProgressDelta> deltas;
        Check(runtime.SyncQuestStates(states, deltas), "sync OK");
        // q1 sans prérequis → Offered (PAS Active).
        Check(states.size() == 1 && states[0].status == QuestStatus::Offered,
              "prérequis remplis → Offered");

        const auto* def = runtime.FindQuestDefinition("q1");
        Check(def != nullptr, "def q1");
        Check(runtime.CanAccept(states[0], *def, "npc:marn"), "accept au bon giver");
        Check(!runtime.CanAccept(states[0], *def, "npc:autre"), "refus mauvais giver");

        // Simuler l'accept : Offered → Active.
        states[0].status = QuestStatus::Active;

        // Progresser : 1 kill (pas assez) reste Active.
        deltas.clear();
        runtime.ApplyEvent(states, QuestStepType::Kill, "mob:1", 1, deltas);
        Check(states[0].status == QuestStatus::Active, "1/2 kill → reste Active");

        // 2e kill → ReadyToTurnIn, SANS reward dans le delta.
        deltas.clear();
        runtime.ApplyEvent(states, QuestStepType::Kill, "mob:1", 1, deltas);
        Check(states[0].status == QuestStatus::ReadyToTurnIn, "2/2 kill → ReadyToTurnIn");
        bool anyReward = false;
        for (const auto& d : deltas)
            if (d.rewardExperience != 0 || d.rewardGold != 0 || !d.rewardItems.empty()) anyReward = true;
        Check(!anyReward, "aucune récompense au passage ReadyToTurnIn");

        Check(runtime.CanTurnIn(states[0], *def, "npc:marn"), "turn-in au bon PNJ");
        Check(!runtime.CanTurnIn(states[0], *def, "npc:autre"), "refus turn-in mauvais PNJ");

        const auto* reward = runtime.TakeRewardOnTurnIn(*def);
        Check(reward != nullptr && reward->experience == 10, "reward exposé au turn-in");
    }
```

- [ ] **Step 2 : Vérifier l'échec en CI**

Run : `ctest -R quest_runtime_tests --output-on-failure`
Expected : **FAIL** (méthodes inexistantes + `SyncQuestStates` produit encore `Active`).

- [ ] **Step 3 : `SyncQuestStates` → `Offered`**

Dans `QuestRuntime.cpp` (~L610-613), remplacer :

```cpp
			if (prerequisitesComplete)
			{
				desiredStatus = QuestStatus::Offered;
			}
```

Et protéger les états déjà avancés : au-dessus, la garde `if (state.status == QuestStatus::Completed) continue;` (~L593) doit aussi ignorer `Active` et `ReadyToTurnIn` pour ne pas rétrograder une quête acceptée :

```cpp
			if (state.status == QuestStatus::Completed
				|| state.status == QuestStatus::Active
				|| state.status == QuestStatus::ReadyToTurnIn)
			{
				continue;
			}
```

- [ ] **Step 4 : `ApplyEvent` → `ReadyToTurnIn` sans reward**

Dans `QuestRuntime.cpp` (~L733-746), remplacer le bloc de complétion :

```cpp
				if (AreAllStepsComplete(definition, state))
				{
					state.status = QuestStatus::ReadyToTurnIn;
					delta.status = QuestStatus::ReadyToTurnIn;
					LOG_INFO(Net,
						"[QuestRuntime] Quest ready to turn in (quest_id={})",
						definition.questId);
				}
```

(Les champs `delta.rewardExperience/Gold/Items` ne sont plus renseignés ici.)

- [ ] **Step 5 : Ajouter les helpers purs**

Déclarer dans `QuestRuntime.h` (section publique) :

```cpp
		/// Vrai si \p state peut être accepté au PNJ \p giverTargetId : quête Offered
		/// et \p giverTargetId == def.giverId. Pur, sans effet de bord.
		bool CanAccept(const QuestState& state, const QuestDefinition& def, std::string_view giverTargetId) const;

		/// Vrai si \p state peut être rendu au PNJ \p npcTargetId : quête ReadyToTurnIn
		/// et \p npcTargetId == def.turnInId. Pur, sans effet de bord.
		bool CanTurnIn(const QuestState& state, const QuestDefinition& def, std::string_view npcTargetId) const;

		/// Retourne le bundle de récompense à verser au turn-in (jamais nul).
		const QuestReward* TakeRewardOnTurnIn(const QuestDefinition& def) const;
```

Définir dans `QuestRuntime.cpp` (près de `FindQuestDefinition`) :

```cpp
	bool QuestRuntime::CanAccept(const QuestState& state, const QuestDefinition& def, std::string_view giverTargetId) const
	{
		return state.questId == def.questId
			&& state.status == QuestStatus::Offered
			&& giverTargetId == def.giverId;
	}

	bool QuestRuntime::CanTurnIn(const QuestState& state, const QuestDefinition& def, std::string_view npcTargetId) const
	{
		return state.questId == def.questId
			&& state.status == QuestStatus::ReadyToTurnIn
			&& npcTargetId == def.turnInId;
	}

	const engine::server::QuestReward* QuestRuntime::TakeRewardOnTurnIn(const QuestDefinition& def) const
	{
		return &def.rewards;
	}
```

- [ ] **Step 6 : Vérifier le succès en CI**

Run : `ctest -R quest_runtime_tests --output-on-failure`
Expected : **PASS**.

- [ ] **Step 7 : Commit**

```bash
git add src/shardd/gameplay/quest/QuestRuntime.h src/shardd/gameplay/quest/QuestRuntime.cpp \
        src/shardd/gameplay/quest/QuestRuntimeTests.cpp
git commit -m "feat(quests): Sync→Offered, ApplyEvent→ReadyToTurnIn (reward différé) + helpers accept/turn-in"
```

---

## Task 3 : Messages wire (giver-list, accept, turn-in) + bump version

**Files:**
- Modify: `src/shared/network/ServerProtocol.h` (`kProtocolVersion` L62 ; enum `MessageKind` ; structs ; déclarations)
- Modify: `src/shared/network/ServerProtocol.cpp` (encode/decode)
- Modify: `src/shared/network/ServerProtocolTests.cpp`

**Interfaces:**
- Produces :
  - `struct QuestGiverEntry { std::string questId; uint8_t role = 0; };` (role : 0=offer, 1=turnin)
  - `struct QuestGiverListMessage { uint32_t clientId = 0; std::string npcTargetId; std::vector<QuestGiverEntry> entries; };`
  - `struct QuestAcceptRequestMessage { uint32_t clientId = 0; std::string questId; std::string giverTargetId; };`
  - `struct QuestTurnInRequestMessage { uint32_t clientId = 0; std::string questId; std::string npcTargetId; };`
  - Fonctions : `Encode*` / `Decode*` pour les trois, mêmes conventions que `EncodeTalkRequest`/`DecodeTalkRequest`.
  - `kProtocolVersion == 14`.

- [ ] **Step 1 : Écrire les tests round-trip (échec attendu)**

Dans `src/shared/network/ServerProtocolTests.cpp`, ajouter (suivre le style des tests round-trip existants du fichier) :

```cpp
    {
        engine::server::QuestAcceptRequestMessage msg{};
        msg.clientId = 7; msg.questId = "q1"; msg.giverTargetId = "npc:marn";
        auto packet = engine::server::EncodeQuestAcceptRequest(msg);
        engine::server::QuestAcceptRequestMessage out{};
        Check(engine::server::DecodeQuestAcceptRequest(packet, out), "decode accept");
        Check(out.clientId == 7 && out.questId == "q1" && out.giverTargetId == "npc:marn", "accept round-trip");
    }
    {
        engine::server::QuestTurnInRequestMessage msg{};
        msg.clientId = 9; msg.questId = "q2"; msg.npcTargetId = "npc:guard";
        auto packet = engine::server::EncodeQuestTurnInRequest(msg);
        engine::server::QuestTurnInRequestMessage out{};
        Check(engine::server::DecodeQuestTurnInRequest(packet, out), "decode turnin");
        Check(out.clientId == 9 && out.questId == "q2" && out.npcTargetId == "npc:guard", "turnin round-trip");
    }
    {
        engine::server::QuestGiverListMessage msg{};
        msg.clientId = 3; msg.npcTargetId = "npc:marn";
        msg.entries.push_back({ "q1", 0 });
        msg.entries.push_back({ "q2", 1 });
        auto packet = engine::server::EncodeQuestGiverList(msg);
        engine::server::QuestGiverListMessage out{};
        Check(engine::server::DecodeQuestGiverList(packet, out), "decode giverlist");
        Check(out.entries.size() == 2 && out.entries[0].questId == "q1" && out.entries[1].role == 1,
              "giverlist round-trip");
    }
```

- [ ] **Step 2 : Vérifier l'échec en CI**

Run : `ctest -R server_protocol_tests --output-on-failure`
Expected : **FAIL** (symboles inexistants).

- [ ] **Step 3 : Bumper la version + ajouter les `MessageKind`**

Dans `ServerProtocol.h`, L62 : `inline constexpr uint16_t kProtocolVersion = 14;`

Dans l'enum `MessageKind`, **repérer la plus grande valeur actuellement définie** (les kinds M32/M33… vont au-delà de 24) et ajouter les trois nouveaux comme les **trois entiers séquentiels suivants** (remplacer `<N>` par la valeur trouvée +1/+2/+3) :

```cpp
		/// SP1 quêtes — liste des quêtes offertes/rendables d'un PNJ (serveur→client).
		QuestGiverList = <N>,
		/// SP1 quêtes — le joueur accepte une quête au PNJ giver (client→serveur).
		QuestAcceptRequest = <N+1>,
		/// SP1 quêtes — le joueur rend une quête au PNJ turn-in (client→serveur).
		QuestTurnInRequest = <N+2>,
```

- [ ] **Step 4 : Ajouter les structs**

Dans `ServerProtocol.h`, après `QuestDeltaMessage` (~L578) :

```cpp
	/// Une entrée de la liste de quêtes d'un PNJ (offer ou turn-in).
	struct QuestGiverEntry
	{
		std::string questId;
		uint8_t role = 0;   ///< 0 = offer (Offered), 1 = turnin (ReadyToTurnIn)
	};

	/// Réponse au Talk : quêtes qu'un PNJ propose / que le joueur peut y rendre.
	struct QuestGiverListMessage
	{
		uint32_t clientId = 0;
		std::string npcTargetId;
		std::vector<QuestGiverEntry> entries;
	};

	/// Le joueur accepte une quête au PNJ giver.
	struct QuestAcceptRequestMessage
	{
		uint32_t clientId = 0;
		std::string questId;
		std::string giverTargetId;
	};

	/// Le joueur rend une quête au PNJ turn-in.
	struct QuestTurnInRequestMessage
	{
		uint32_t clientId = 0;
		std::string questId;
		std::string npcTargetId;
	};
```

Et les déclarations près des autres (~L748-757) :

```cpp
	std::vector<std::byte> EncodeQuestGiverList(const QuestGiverListMessage& message);
	bool DecodeQuestGiverList(std::span<const std::byte> packet, QuestGiverListMessage& outMessage);
	std::vector<std::byte> EncodeQuestAcceptRequest(const QuestAcceptRequestMessage& message);
	bool DecodeQuestAcceptRequest(std::span<const std::byte> packet, QuestAcceptRequestMessage& outMessage);
	std::vector<std::byte> EncodeQuestTurnInRequest(const QuestTurnInRequestMessage& message);
	bool DecodeQuestTurnInRequest(std::span<const std::byte> packet, QuestTurnInRequestMessage& outMessage);
```

- [ ] **Step 5 : Implémenter encode/decode**

Dans `ServerProtocol.cpp`, après `DecodeQuestDelta` (~L1200), suivre le modèle `EncodeTalkRequest`/`DecodeTalkRequest` (`BeginPacket`, `WriteU32`, `WriteSizedString`, `DecodeHeader`, lecteurs symétriques). Les deux requêtes (accept/turn-in) ont la forme `u32 clientId + string + string` :

```cpp
	std::vector<std::byte> EncodeQuestAcceptRequest(const QuestAcceptRequestMessage& message)
	{
		const size_t payloadSize = 4u + 2u + message.questId.size() + 2u + message.giverTargetId.size();
		std::vector<std::byte> packet = BeginPacket(MessageKind::QuestAcceptRequest, payloadSize);
		WriteU32(packet, message.clientId);
		WriteSizedString(packet, message.questId);
		WriteSizedString(packet, message.giverTargetId);
		return packet;
	}

	bool DecodeQuestAcceptRequest(std::span<const std::byte> packet, QuestAcceptRequestMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::QuestAcceptRequest, payload) || payload.size() < 8)
		{
			return false;
		}
		size_t offset = 0;
		outMessage.clientId = ReadU32(payload, offset);
		if (!ReadSizedString(payload, offset, outMessage.questId)) return false;
		if (!ReadSizedString(payload, offset, outMessage.giverTargetId)) return false;
		return true;
	}
```

Répliquer `QuestTurnInRequest` à l'identique (champ `npcTargetId` au lieu de `giverTargetId`, `MessageKind::QuestTurnInRequest`). Pour `QuestGiverList`, encoder `u32 clientId`, `string npcTargetId`, `u16 count`, puis pour chaque entrée `string questId` + `u8 role` :

```cpp
	std::vector<std::byte> EncodeQuestGiverList(const QuestGiverListMessage& message)
	{
		size_t payloadSize = 4u + 2u + message.npcTargetId.size() + 2u;
		for (const QuestGiverEntry& e : message.entries)
			payloadSize += 2u + e.questId.size() + 1u;
		std::vector<std::byte> packet = BeginPacket(MessageKind::QuestGiverList, payloadSize);
		WriteU32(packet, message.clientId);
		WriteSizedString(packet, message.npcTargetId);
		WriteU16(packet, static_cast<uint16_t>(message.entries.size()));
		for (const QuestGiverEntry& e : message.entries)
		{
			WriteSizedString(packet, e.questId);
			packet.push_back(static_cast<std::byte>(e.role));
		}
		return packet;
	}

	bool DecodeQuestGiverList(std::span<const std::byte> packet, QuestGiverListMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::QuestGiverList, payload) || payload.size() < 8)
		{
			return false;
		}
		size_t offset = 0;
		outMessage.clientId = ReadU32(payload, offset);
		if (!ReadSizedString(payload, offset, outMessage.npcTargetId)) return false;
		uint16_t count = ReadU16(payload, offset);
		outMessage.entries.clear();
		for (uint16_t i = 0; i < count; ++i)
		{
			QuestGiverEntry e{};
			if (!ReadSizedString(payload, offset, e.questId)) return false;
			if (offset >= payload.size()) return false;
			e.role = static_cast<uint8_t>(payload[offset++]);
			outMessage.entries.push_back(std::move(e));
		}
		return true;
	}
```

> ⚠️ Vérifier les noms exacts des helpers (`ReadU32`, `ReadU16`, `WriteU16`, `ReadSizedString`) dans `ServerProtocol.cpp` et les employer tels quels (ne pas en inventer). S'ils diffèrent, aligner sur ceux utilisés par `DecodeQuestDelta`.

- [ ] **Step 6 : Vérifier le succès en CI**

Run : `ctest -R server_protocol_tests --output-on-failure`
Expected : **PASS**.

- [ ] **Step 7 : Commit**

```bash
git add src/shared/network/ServerProtocol.h src/shared/network/ServerProtocol.cpp \
        src/shared/network/ServerProtocolTests.cpp
git commit -m "feat(quests): messages wire accept/turn-in/giver-list + bump kProtocolVersion 13->14"
```

---

## Task 4 : Compat persistance (ancien enum 0/1/2 → nouveau) + save étendu

**Files:**
- Modify: `src/shardd/gameplay/character/CharacterPersistence.cpp` (load quête ~L126-138 ; save quête ~L268)
- Create: `src/shardd/gameplay/character/CharacterPersistenceQuestCompatTests.cpp`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `QuestStatus` (Task 1).
- Produces: fonction libre `engine::server::QuestStatus MapPersistedQuestStatus(int64_t persistedValue, uint32_t formatVersion)` déclarée/définie en tête de `CharacterPersistence.cpp` (anonymous namespace) et **exposée pour test** via un petit header `CharacterPersistenceQuestCompat.h` (déclaration seule).

- [ ] **Step 1 : Écrire le test de compat (échec attendu)**

Créer `src/shardd/gameplay/character/CharacterPersistenceQuestCompatTests.cpp` :

```cpp
// Test de la table de compat des statuts de quête persistés (SP1).
#include "src/shardd/gameplay/character/CharacterPersistenceQuestCompat.h"

#include <iostream>

using engine::server::QuestStatus;
using engine::server::MapPersistedQuestStatus;

int main()
{
    int failures = 0;
    auto expect = [&](QuestStatus got, QuestStatus want, const char* label) {
        if (got != want) { std::cerr << "[FAIL] " << label << "\n"; ++failures; }
    };

    // Ancien format (version 0) : 0=Locked, 1=Active, 2=Completed.
    expect(MapPersistedQuestStatus(0, 0), QuestStatus::Locked,    "old 0 -> Locked");
    expect(MapPersistedQuestStatus(1, 0), QuestStatus::Active,    "old 1 -> Active");
    expect(MapPersistedQuestStatus(2, 0), QuestStatus::Completed, "old 2 -> Completed");

    // Nouveau format (version 1) : valeurs 0..4 = enum direct.
    expect(MapPersistedQuestStatus(1, 1), QuestStatus::Offered,       "new 1 -> Offered");
    expect(MapPersistedQuestStatus(3, 1), QuestStatus::ReadyToTurnIn, "new 3 -> ReadyToTurnIn");
    expect(MapPersistedQuestStatus(4, 1), QuestStatus::Completed,     "new 4 -> Completed");

    // Valeur hors plage → Locked (dégradation sûre).
    expect(MapPersistedQuestStatus(99, 1), QuestStatus::Locked, "out of range -> Locked");

    if (failures) { std::cerr << failures << " échec(s)\n"; return 1; }
    std::cout << "OK\n";
    return 0;
}
```

- [ ] **Step 2 : Enregistrer le test**

Dans `src/CMakeLists.txt`, après `quest_runtime_tests` :

```cmake
  # SP1 : compat des statuts de quête persistés (ancien enum 0/1/2 -> nouveau).
  lcdlln_add_simple_test(character_persistence_quest_compat_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterPersistenceQuestCompatTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterPersistence.cpp)
```

- [ ] **Step 3 : Vérifier l'échec en CI**

Run : `ctest -R character_persistence_quest_compat_tests --output-on-failure`
Expected : **FAIL** (header/fonction inexistants).

- [ ] **Step 4 : Créer le header de compat**

Créer `src/shardd/gameplay/character/CharacterPersistenceQuestCompat.h` :

```cpp
#pragma once
// SP1 — Table de compat des statuts de quête persistés. L'ancien format
// (formatVersion 0) sérialisait 0=Locked/1=Active/2=Completed ; le nouveau
// (formatVersion >= 1) sérialise directement l'enum QuestStatus (0..4).

#include "src/shardd/gameplay/quest/QuestRuntime.h"  // engine::server::QuestStatus

#include <cstdint>

namespace engine::server
{
	/// Convertit une valeur de statut persistée en QuestStatus.
	/// \param persistedValue valeur brute lue du fichier de personnage.
	/// \param formatVersion 0 = ancien schéma (0/1/2), >=1 = enum direct.
	/// \return QuestStatus mappé ; Locked pour toute valeur hors plage.
	QuestStatus MapPersistedQuestStatus(int64_t persistedValue, uint32_t formatVersion);
}
```

- [ ] **Step 5 : Implémenter la fonction + brancher le load/save**

En haut de `CharacterPersistence.cpp` (après les includes), ajouter :

```cpp
#include "src/shardd/gameplay/character/CharacterPersistenceQuestCompat.h"

namespace engine::server
{
	QuestStatus MapPersistedQuestStatus(int64_t persistedValue, uint32_t formatVersion)
	{
		if (formatVersion == 0u)
		{
			switch (persistedValue)
			{
			case 0: return QuestStatus::Locked;
			case 1: return QuestStatus::Active;
			case 2: return QuestStatus::Completed;
			default: return QuestStatus::Locked;
			}
		}
		if (persistedValue >= 0 && persistedValue <= 4)
			return static_cast<QuestStatus>(static_cast<uint8_t>(persistedValue));
		return QuestStatus::Locked;
	}
}
```

Dans le **load** (~L126-138), lire un champ de version de format (clé `quests.format_version`, défaut `0` pour les persos existants) et remplacer le bloc `if/else if/else` de conversion par :

```cpp
			const uint32_t questFormatVersion =
				static_cast<uint32_t>(persisted.GetInt("quests.format_version", 0));
			const int64_t persistedStatus =
				persisted.GetInt("quests." + std::to_string(questIndex) + ".status", 0);
			questState.status = MapPersistedQuestStatus(persistedStatus, questFormatVersion);
```

Dans le **save** (~L264, juste avant `quests.count`), écrire la version de format courante :

```cpp
		output << "quests.format_version=1\n";
```

(Le reste du save — `.status=` avec `static_cast<uint32_t>(status)` — reste correct : il écrit désormais 0..4.)

- [ ] **Step 6 : Vérifier le succès en CI**

Run : `ctest -R character_persistence_quest_compat_tests --output-on-failure`
Expected : **PASS**.

- [ ] **Step 7 : Commit**

```bash
git add src/shardd/gameplay/character/CharacterPersistenceQuestCompat.h \
        src/shardd/gameplay/character/CharacterPersistence.cpp \
        src/shardd/gameplay/character/CharacterPersistenceQuestCompatTests.cpp src/CMakeLists.txt
git commit -m "feat(quests): compat persistance des statuts (ancien 0/1/2 -> nouvel enum) + format_version"
```

---

## Task 5 : Handlers ServerApp (accept, turn-in) + reward au turn-in + giver-list au Talk

**Files:**
- Modify: `src/shared/server_bootstrap/ServerApp.h` (déclarations privées)
- Modify: `src/shared/server_bootstrap/ServerApp.cpp` (dispatch ~L860 ; `HandleTalkRequest` ~L3367-3423 ; `ApplyQuestEvent` ~L5208-5270 ; nouveaux handlers)

**Interfaces:**
- Consumes: `QuestRuntime::CanAccept/CanTurnIn/TakeRewardOnTurnIn` (Task 2) ; `Decode*` / `EncodeQuestGiverList` (Task 3) ; le bloc de grant XP/or/items (`ApplyLevelUpsAfterXp` / `m_playerWallet.AddCurrency` / `AddItemToInventory`).
- Produces (méthodes privées de `ServerApp`) :
  - `void HandleAcceptQuest(const Endpoint& endpoint, const QuestAcceptRequestMessage& msg);`
  - `void HandleTurnInQuest(const Endpoint& endpoint, const QuestTurnInRequestMessage& msg);`
  - `void SendQuestGiverList(const ConnectedClient& receiver, std::string_view npcTargetId);`

> **Note test** : `ServerApp` est une classe d'intégration non couverte par des tests unitaires directs dans ce repo (comme les autres handlers UDP). La logique testable a été isolée dans `QuestRuntime` (Task 2) et le wire (Task 3). Cette tâche est validée par la **compilation CI** + la revue ; pas de nouveau test unitaire. Un smoke test manuel figure dans la checklist finale.

- [ ] **Step 1 : Retirer le versement de récompense de `ApplyQuestEvent`**

Dans `ServerApp.cpp` (~L5221-5251), supprimer le bloc `if (delta.rewardExperience != 0 || …) { … }` (grant XP/or/items) et le `rewardedItems` associé. `ApplyQuestEvent` ne fait plus que propager les deltas au client :

```cpp
		for (const QuestProgressDelta& delta : deltas)
		{
			(void)SendQuestDelta(client, delta);
		}
		SaveConnectedClient(client, reason);
```

(Conserver le log final. Retirer l'appel `SendInventoryDelta(rewardedItems)` et `SendWalletUpdate` de ce chemin — ils reviennent dans le turn-in.)

- [ ] **Step 2 : Déclarer les handlers dans `ServerApp.h`**

Près des autres `Handle*` privés :

```cpp
		/// SP1 quêtes — le joueur accepte une quête au PNJ giver (Offered -> Active).
		void HandleAcceptQuest(const engine::server::Endpoint& endpoint, const engine::server::QuestAcceptRequestMessage& msg);
		/// SP1 quêtes — le joueur rend une quête (ReadyToTurnIn -> Completed) : récompenses versées ici.
		void HandleTurnInQuest(const engine::server::Endpoint& endpoint, const engine::server::QuestTurnInRequestMessage& msg);
		/// SP1 quêtes — envoie la liste des quêtes offertes/rendables d'un PNJ au client.
		void SendQuestGiverList(const engine::server::ConnectedClient& receiver, std::string_view npcTargetId);
```

- [ ] **Step 3 : Brancher le dispatch**

Dans la chaîne de décodage (~L860, après le bloc `TalkRequest`), ajouter :

```cpp
		QuestAcceptRequestMessage questAccept{};
		if (DecodeQuestAcceptRequest(packetBytes, questAccept))
		{
			HandleAcceptQuest(datagram.endpoint, questAccept);
			return;
		}

		QuestTurnInRequestMessage questTurnIn{};
		if (DecodeQuestTurnInRequest(packetBytes, questTurnIn))
		{
			HandleTurnInQuest(datagram.endpoint, questTurnIn);
			return;
		}
```

- [ ] **Step 4 : Émettre la giver-list au `Talk`**

Dans `HandleTalkRequest`, juste avant l'appel final `ApplyQuestEvent(*client, QuestStepType::Talk, …)` (~L3422), ajouter :

```cpp
		SendQuestGiverList(*client, targetId);
```

Puis définir `SendQuestGiverList` : parcourir `client.questStates`, pour chaque état, récupérer la définition (`m_questRuntime.FindQuestDefinition`) et ajouter une entrée si `status == Offered && def->giverId == npcTargetId` (role 0) ou `status == ReadyToTurnIn && def->turnInId == npcTargetId` (role 1). N'envoyer que si non vide :

```cpp
	void ServerApp::SendQuestGiverList(const ConnectedClient& receiver, std::string_view npcTargetId)
	{
		QuestGiverListMessage msg{};
		msg.clientId = receiver.clientId;
		msg.npcTargetId = std::string(npcTargetId);
		for (const QuestState& state : receiver.questStates)
		{
			const QuestDefinition* def = m_questRuntime.FindQuestDefinition(state.questId);
			if (def == nullptr) continue;
			if (state.status == QuestStatus::Offered && def->giverId == npcTargetId)
				msg.entries.push_back({ state.questId, 0 });
			else if (state.status == QuestStatus::ReadyToTurnIn && def->turnInId == npcTargetId)
				msg.entries.push_back({ state.questId, 1 });
		}
		if (msg.entries.empty()) return;
		const auto packet = EncodeQuestGiverList(msg);
		SendToClient(receiver, packet);   // suivre le helper d'envoi utilisé par SendQuestDelta
	}
```

> ⚠️ Utiliser le **même helper d'envoi** que `SendQuestDelta` (repérer son nom exact : `SendToClient` / `SendPacket` / `m_transport.Send…`). Ne pas inventer.

- [ ] **Step 5 : Implémenter `HandleAcceptQuest`**

Résoudre le client depuis l'endpoint (suivre `HandleTalkRequest` pour la résolution `endpoint → ConnectedClient*` + garde). Puis :

```cpp
	void ServerApp::HandleAcceptQuest(const Endpoint& endpoint, const QuestAcceptRequestMessage& msg)
	{
		ConnectedClient* client = /* résolution comme HandleTalkRequest */;
		if (client == nullptr) return;

		const QuestDefinition* def = m_questRuntime.FindQuestDefinition(msg.questId);
		if (def == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] AcceptQuest: quête inconnue (client_id={}, quest_id={})", client->clientId, msg.questId);
			return;
		}
		const size_t idx = FindClientQuestStateIndex(*client, msg.questId); // helper : cf. Step 6
		if (idx == kInvalidQuestIndex) return;
		QuestState& state = client->questStates[idx];

		if (!m_questRuntime.CanAccept(state, *def, msg.giverTargetId))
		{
			LOG_WARN(Net, "[ServerApp] AcceptQuest refusé (client_id={}, quest_id={}, giver={})",
				client->clientId, msg.questId, msg.giverTargetId);
			return;
		}
		if (!IsClientNearNpc(*client, msg.giverTargetId)) return;  // contrôle de portée (cf. Step 6)

		state.status = QuestStatus::Active;
		state.stepProgressCounts.assign(def->steps.size(), 0u);

		QuestProgressDelta delta{};
		delta.questId = state.questId;
		delta.status = state.status;
		delta.stepProgressCounts = state.stepProgressCounts;
		(void)SendQuestDelta(*client, delta);
		SaveConnectedClient(*client, "quest_accept");
		LOG_INFO(Net, "[ServerApp] Quest accepted (client_id={}, quest_id={})", client->clientId, msg.questId);
	}
```

- [ ] **Step 6 : Ajouter les helpers manquants**

Si `FindClientQuestStateIndex`, `kInvalidQuestIndex` et `IsClientNearNpc` n'existent pas :
- `FindClientQuestStateIndex(client, questId)` : boucle linéaire sur `client.questStates`, renvoie l'index ou une sentinelle.
- **Portée** : réutiliser le contrôle déjà appliqué aux interactions PNJ. Si le `Talk` actuel n'impose pas de portée serveur (il route par `targetId` sans distance — cf. `HandleTalkRequest`), alors `IsClientNearNpc` renvoie `true` pour V1 (la portée est déjà garantie par le client qui n'émet le Talk qu'à proximité) et un `// TODO SP-ultérieur` documente le durcissement. **Documenter ce choix** dans le commentaire de la fonction (ne pas prétendre valider une portée non vérifiée).

- [ ] **Step 7 : Implémenter `HandleTurnInQuest` (récompenses ici)**

```cpp
	void ServerApp::HandleTurnInQuest(const Endpoint& endpoint, const QuestTurnInRequestMessage& msg)
	{
		ConnectedClient* client = /* résolution comme HandleTalkRequest */;
		if (client == nullptr) return;

		const QuestDefinition* def = m_questRuntime.FindQuestDefinition(msg.questId);
		if (def == nullptr) return;
		const size_t idx = FindClientQuestStateIndex(*client, msg.questId);
		if (idx == kInvalidQuestIndex) return;
		QuestState& state = client->questStates[idx];

		if (!m_questRuntime.CanTurnIn(state, *def, msg.npcTargetId))
		{
			LOG_WARN(Net, "[ServerApp] TurnInQuest refusé (client_id={}, quest_id={}, npc={})",
				client->clientId, msg.questId, msg.npcTargetId);
			return;
		}
		if (!IsClientNearNpc(*client, msg.npcTargetId)) return;

		// Verser les récompenses (bloc déplacé depuis l'ancien ApplyQuestEvent).
		const QuestReward* reward = m_questRuntime.TakeRewardOnTurnIn(*def);
		std::vector<ItemStack> rewardedItems;
		if (reward != nullptr)
		{
			ApplyLevelUpsAfterXp(*client, reward->experience);
			if (reward->gold != 0u)
			{
				std::string walletErr;
				if (!m_playerWallet.AddCurrency(*client, kCurrencyGold, reward->gold, walletErr))
					LOG_WARN(Net, "[ServerApp] Quest gold grant blocked (client_id={}, err={})", client->clientId, walletErr);
			}
			for (const ItemStack& item : reward->items)
			{
				AddItemToInventory(*client, item);
				rewardedItems.push_back(item);
			}
		}

		state.status = QuestStatus::Completed;

		QuestProgressDelta delta{};
		delta.questId = state.questId;
		delta.status = state.status;
		delta.stepProgressCounts = state.stepProgressCounts;
		if (reward != nullptr)
		{
			delta.rewardExperience = reward->experience;
			delta.rewardGold = reward->gold;
			delta.rewardItems = reward->items;
		}
		(void)SendQuestDelta(*client, delta);
		if (!rewardedItems.empty()) (void)SendInventoryDelta(*client, rewardedItems);
		(void)SendWalletUpdate(*client);
		SaveConnectedClient(*client, "quest_turnin");
		LOG_INFO(Net, "[ServerApp] Quest turned in (client_id={}, quest_id={}, xp={}, gold={})",
			client->clientId, msg.questId, reward ? reward->experience : 0u, reward ? reward->gold : 0u);
	}
```

- [ ] **Step 8 : Vérifier la compilation en CI**

Run : `ctest --output-on-failure` (suite complète — pas de test unitaire ServerApp, mais la compilation CI doit passer et les tests Task 1-4 rester verts).
Expected : **PASS** (compilation + tests quêtes/wire/persistance).

- [ ] **Step 9 : Commit**

```bash
git add src/shared/server_bootstrap/ServerApp.h src/shared/server_bootstrap/ServerApp.cpp
git commit -m "feat(quests): handlers accept/turn-in (reward au turn-in) + giver-list au Talk"
```

---

## Task 6 : Documentation CODEBASE_MAP + rapport de déploiement

**Files:**
- Modify: `docs/CODEBASE_MAP.md` (ou l'équivalent existant) — section quêtes.

- [ ] **Step 1 : Documenter le cycle de vie**

Ajouter une courte section « Quêtes — cycle de vie piloté par le joueur (SP1) » décrivant la machine d'états (`Offered/Active/ReadyToTurnIn/Completed`), le rôle du `Talk` (event + giver-list), les 3 messages wire, et le fait que **les récompenses sont versées au turn-in**. Renvoyer aux specs `docs/superpowers/specs/2026-07-02-quest-*`.

- [ ] **Step 2 : Commit**

```bash
git add docs/CODEBASE_MAP.md
git commit -m "docs(quests): cycle de vie SP1 dans CODEBASE_MAP"
```

---

## Checklist finale (DoD SP1)

- [ ] Enum `QuestStatus` étendu + réordonné ; `GetQuestStatusName` couvre 5 valeurs.
- [ ] `giver`/`turnIn` parsés ; définition sans ces champs **rejetée** au load ; JSON exemple migré.
- [ ] `SyncQuestStates` → `Offered` ; ne rétrograde pas `Active`/`ReadyToTurnIn`/`Completed`.
- [ ] `ApplyEvent` → `ReadyToTurnIn` **sans** reward.
- [ ] 3 messages wire (giver-list / accept / turn-in) + round-trip verts ; `kProtocolVersion == 14`.
- [ ] `Talk` renvoie la giver-list en plus de l'event d'étape.
- [ ] Récompenses versées **au turn-in uniquement** (retirées de `ApplyQuestEvent`).
- [ ] Compat persistance : `MapPersistedQuestStatus` testée (ancien 0/1/2 + nouveau 0..4 + hors plage) ; `quests.format_version=1` écrit.
- [ ] Tous les tests CI verts (`quest_runtime_tests`, `character_persistence_quest_compat_tests`, `server_protocol_tests`).
- [ ] Fonctions nouvelles/modifiées documentées en français.
- [ ] **Smoke test manuel** (après déploiement shard) : accepter une quête au PNJ → progresser (kill) → état `ReadyToTurnIn` sans récompense → rendre au PNJ → récompenses reçues + `Completed` ; survit à un restart shard (persistance).
- [ ] Rapport final incluant : **⚠️ REDÉPLOIEMENT SHARD requis** (wire-breaking, `kProtocolVersion` 13→14), lock-step avec le client SP2.

---

## Self-review (effectuée)

- **Couverture spec** : §2 machine d'états → Tasks 1-2, 5 ; §3 JSON → Task 1 ; §4 wire+flux → Tasks 3, 5 ; §5 refonte QuestRuntime → Task 2 ; §6 persistance → Task 4 ; §7 tests → Tasks 1-4 ; §8 DoD → checklist finale.
- **Points d'attention laissés explicites** (à vérifier dans le code voisin, pas inventés) : valeurs numériques exactes des nouveaux `MessageKind` (Task 3 Step 3) ; noms exacts des helpers wire `ReadU16/WriteU16/ReadSizedString` ; nom exact du helper d'envoi paquet (`SendToClient` vs autre) ; résolution `endpoint → ConnectedClient*` (calquée sur `HandleTalkRequest`) ; existence de `IsClientNearNpc` (sinon V1 = `true` documenté).
- **Cohérence de types** : `questId` = `std::string` partout ; `QuestStatus` 5 valeurs ; helpers `CanAccept/CanTurnIn/TakeRewardOnTurnIn` signatures identiques entre Task 2 (déclaration) et Task 5 (consommation).
