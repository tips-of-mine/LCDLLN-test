# Combat SP1 — Créatures visibles : Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rendre les mobs (déjà spawnés et répliqués par le serveur) visibles dans le client, avec des stats data-driven par archétype à la place des constantes codées en dur.

**Architecture:** Un catalogue JSON `game/data/creatures/archetypes.json` est chargé des deux côtés : par shardd (nouvelle `CreatureArchetypeLibrary`, pattern `SpawnerRuntime`) pour les stats/XP, et par le client (nouveau `CreatureCatalog`, pattern parseur local à la `SkillSystem`) pour nom/niveau/mesh/échelle. Le `SnapshotEntity` gagne un champ `archetypeId` (wire-bump v8→v9) pour que le client sache quel archétype rendre. Le rendu réutilise le pipeline avatars distants (`GetRaceMesh`) et la plaque de nom existante.

**Tech Stack:** C++20, CMake, protocole UDP gameplay maison (`ServerProtocol`), parseurs JSON locaux maison (pas de dépendance), tests CTest exécutés par la CI build-linux (pas de toolchain locale — la compilation et les tests se valident via CI).

**Découpage en 2 PRs stackées :**
- **PR-A (serveur)** : Tasks 1-6 — branche `combat-sp1-server`
- **PR-B (client)** : Tasks 7-10 — branche `combat-sp1-client` stackée sur PR-A

**Conventions repo à respecter** : commentaires en français, PascalCase pour les nouveaux fichiers/classes, ne PAS utiliser le terme banni (l'ancien nom de serveur MMO open-source) dans code/commits/PR, indiquer le déploiement en fin de PR (ici : ⚠️ lock-step shardd + client, wire-breaking v9).

---

### Task 1: Catalogue de données `archetypes.json`

**Files:**
- Create: `game/data/creatures/archetypes.json`

- [ ] **Step 1: Recenser les archetypeId réellement référencés**

Run: `Grep "archetypeId" game/data/zones/ -rn` (et `game/data/events/` si présent)
Expected: au moins `game/data/zones/zone_0/spawners.json` → `"archetypeId": 100`. Noter tout autre id (spawners d'autres zones, events dynamiques, loot tables `game/data/loot/loot_tables.txt` colonne 1 = sourceArchetypeId).

- [ ] **Step 2: Écrire le catalogue avec une entrée par id recensé**

```json
{
  "archetypes": [
    {
      "id": 100,
      "name": "Sanglier des collines",
      "level": 2,
      "stats": {
        "hp": 60,
        "damage": 5,
        "accuracy": 80.0,
        "rangeMeters": 2.5,
        "critRate": 2.0,
        "critMult": 1.5,
        "attackPeriodMs": 2000
      },
      "xpReward": 10,
      "model": { "mesh": "orcs", "scale": 0.9 }
    }
  ]
}
```

Valeurs `hp: 60`, `damage: 5`, `xpReward: 10` = iso-comportement avec les constantes actuelles (`kDefaultMobHealth = 60`, `kBaseXpPerMobKill = 10`, dégâts mob par défaut) pour ne pas changer l'équilibrage en SP1. `accuracy`/`critRate`/`critMult` sont portés par le schéma mais consommés en SP2 (documenté dans le JSON ? non — JSON sans commentaires ; documenté dans le header de la library, Task 2). Ajouter une entrée par id supplémentaire trouvé au Step 1 (mêmes stats par défaut, nom distinct).

- [ ] **Step 3: Commit**

```bash
git add game/data/creatures/archetypes.json
git commit -m "feat(data): catalogue d'archétypes de créatures (combat SP1)"
```

---

### Task 2: `CreatureArchetypeLibrary` (serveur) — tests d'abord

**Files:**
- Create: `src/shardd/gameplay/creature/CreatureArchetypeLibrary.h`
- Create: `src/shardd/gameplay/creature/CreatureArchetypeLibrary.cpp`
- Create: `src/shardd/gameplay/creature/CreatureArchetypeLibraryTests.cpp`
- Modify: `src/CMakeLists.txt` (3 endroits, voir Task 3)

- [ ] **Step 1: Écrire le header**

```cpp
#pragma once

#include "src/shared/core/Config.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace engine::server
{
	/// Un archétype de créature data-driven chargé depuis
	/// `game/data/creatures/archetypes.json`. Porte les stats de combat du mob
	/// (consommées par ServerApp au spawn), la récompense d'XP au kill, et les
	/// informations d'apparence (mesh/scale/nom/niveau) que le client résout de
	/// son côté via le même fichier (cf. CreatureCatalog client).
	/// `accuracy`/`critRate`/`critMult` sont portés par le schéma dès SP1 mais ne
	/// sont consommés qu'à partir de SP2 (jets de précision/critique).
	struct CreatureArchetype
	{
		uint32_t archetypeId = 0;
		std::string name;
		uint32_t level = 1;
		uint32_t hp = 1;
		uint32_t damage = 0;
		float accuracy = 100.0f;
		float rangeMeters = 2.0f;
		float critRate = 0.0f;
		float critMult = 1.5f;
		uint32_t attackPeriodMs = 2000;
		uint32_t xpReward = 0;
		std::string meshKey;
		float scale = 1.0f;
	};

	/// Catalogue serveur des archétypes de créatures, résolu depuis
	/// `paths.content` (même politique stricte que SpawnerRuntime : fichier
	/// absent/illisible ou entrée invalide = échec d'Init, le shard ne boote pas
	/// avec un catalogue corrompu).
	class CreatureArchetypeLibrary final
	{
	public:
		/// Capture la config utilisée pour résoudre le JSON du catalogue.
		explicit CreatureArchetypeLibrary(const engine::core::Config& config);

		/// Charge et valide `creatures/archetypes.json`. Idempotent (warn si déjà
		/// initialisé). Retourne false si le fichier manque ou si une entrée est
		/// invalide (id dupliqué, hp/attackPeriodMs nuls, champs manquants).
		bool Init();

		/// Retourne l'archétype demandé, ou nullptr s'il est inconnu.
		const CreatureArchetype* Find(uint32_t archetypeId) const;

		/// Nombre d'archétypes chargés (0 avant Init).
		size_t Count() const { return m_archetypes.size(); }

		/// Variante testable : parse depuis un texte JSON en mémoire (pas d'I/O).
		/// Utilisée par Init() (qui lit le fichier) et par les tests unitaires.
		bool LoadFromText(std::string_view jsonText, std::string& outError);

	private:
		engine::core::Config m_config;
		std::unordered_map<uint32_t, CreatureArchetype> m_archetypes;
		bool m_initialized = false;
	};
}
```

- [ ] **Step 2: Écrire les tests (CTest, mêmes conventions que les tests shardd existants — regarder `src/shardd/gameplay/spawner/` ou un `*Tests.cpp` voisin pour le harnais assert utilisé, et le reproduire)**

Cas à couvrir (un `assert`/check par cas, harnais du repo) :

```cpp
// CreatureArchetypeLibraryTests.cpp — résumé des cas (code complet au moment
// de l'implémentation, en copiant le harnais d'un *Tests.cpp shardd voisin) :
// 1. LoadFromText(JSON valide à 2 archétypes) → true, Count()==2,
//    Find(100)->hp == 60, Find(100)->meshKey == "orcs", Find(999) == nullptr.
// 2. LoadFromText(JSON sans "archetypes") → false, erreur non vide.
// 3. LoadFromText(id dupliqué) → false.
// 4. LoadFromText(hp == 0) → false ; attackPeriodMs == 0 → false.
// 5. LoadFromText(scale absent) → true, scale == 1.0f (défaut) ;
//    model absent → false (mesh obligatoire).
```

- [ ] **Step 3: Écrire l'implémentation**

Copier le parseur JSON local de `src/shardd/gameplay/spawner/SpawnerRuntime.cpp:21-403` (classe `JsonParser` + helpers `FindObjectMember`/`TryGetUint`/`TryGetFloat` dans le namespace anonyme — c'est la convention du repo : parseur local par module, pas de dépendance). `Init()` lit `creatures/archetypes.json` via `engine::platform::FileSystem::ReadAllTextContent(m_config, "creatures/archetypes.json")` puis délègue à `LoadFromText`. `LoadFromText` valide : racine objet avec tableau `archetypes` non vide ; par entrée : `id` uint > 0 unique, `name` string non vide, `level` uint ≥ 1, `stats.hp` > 0, `stats.damage` uint, `stats.accuracy`/`stats.rangeMeters`/`stats.critRate`/`stats.critMult` floats finis, `stats.attackPeriodMs` > 0, `xpReward` uint, `model.mesh` string non vide, `model.scale` float > 0 optionnel (défaut 1.0). Logs `[CreatureArchetypeLibrary]` même format que SpawnerRuntime. Toutes les fonctions documentées en `///` français.

- [ ] **Step 4: Commit**

```bash
git add src/shardd/gameplay/creature/
git commit -m "feat(shardd): CreatureArchetypeLibrary — catalogue d'archétypes data-driven + tests"
```

---

### Task 3: Câblage CMake

**Files:**
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Ajouter le .cpp aux deux listes de sources qui contiennent déjà SpawnerRuntime.cpp**

Aux deux occurrences de `${CMAKE_SOURCE_DIR}/src/shardd/gameplay/spawner/SpawnerRuntime.cpp` (src/CMakeLists.txt:71 et :1144), ajouter juste au-dessus :

```cmake
    # Combat SP1 — catalogue d'archétypes de créatures (stats data-driven).
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/creature/CreatureArchetypeLibrary.cpp
```

- [ ] **Step 2: Enregistrer le test**

Localiser la liste de tests contenant `${CMAKE_SOURCE_DIR}/src/shared/network/ServerProtocolTests.cpp` (src/CMakeLists.txt:527) et repérer comment les `*Tests.cpp` shardd voisins sont enregistrés (exécutable de test + `add_test`). Ajouter `CreatureArchetypeLibraryTests.cpp` selon le même pattern, avec `CreatureArchetypeLibrary.cpp` dans les sources du même exécutable si la liste est explicite. Vérifier les exclusions ctest de `.github/workflows/build-linux.yml` (option `-E`) : ne pas nommer le test d'une façon qui matcherait une exclusion existante.

- [ ] **Step 3: Commit**

```bash
git add src/CMakeLists.txt
git commit -m "build: câblage CreatureArchetypeLibrary (server_app + shard_app + tests)"
```

---

### Task 4: Stats d'archétype appliquées au spawn (ServerApp)

**Files:**
- Modify: `src/shared/server_bootstrap/ServerApp.h` (membre library + champ xpReward sur MobEntity, autour de :170-200)
- Modify: `src/shared/server_bootstrap/ServerApp.cpp` (InitSpawners :1582+, SpawnSpawnerMob :1972-1984, mob d'événement dynamique :2246-2255, HandleAttackRequest :2725+ pour l'XP)

- [ ] **Step 1: Ajouter le membre et le champ**

Dans `ServerApp.h` : `#include "src/shardd/gameplay/creature/CreatureArchetypeLibrary.h"` (même pattern d'include cross-dossier que SpawnerRuntime) ; dans la section des membres runtime (à côté de `m_spawnerRuntime`) :

```cpp
		/// Combat SP1 — catalogue d'archétypes de créatures (stats data-driven).
		CreatureArchetypeLibrary m_archetypeLibrary;
```

(initialisé dans la liste d'init du constructeur avec `m_config`, comme `m_spawnerRuntime`). Dans `struct MobEntity` (ServerApp.h:170-200), après `leashDistanceMeters` :

```cpp
		/// Combat SP1 — XP attribuée au tueur, copiée depuis l'archétype au spawn.
		uint32_t xpReward = 0;
```

- [ ] **Step 2: Init + validation croisée dans InitSpawners**

Au début de `ServerApp::InitSpawners()` (ServerApp.cpp:1582), avant la boucle sur les définitions :

```cpp
		if (!m_archetypeLibrary.Init())
		{
			LOG_ERROR(Net, "[ServerApp] Spawner init FAILED: archetype library load failed");
			return false;
		}
```

Dans la boucle, après lecture de `definition`, valider :

```cpp
			if (m_archetypeLibrary.Find(definition.archetypeId) == nullptr)
			{
				LOG_ERROR(Net, "[ServerApp] Spawner init FAILED: unknown archetype (spawner_id={}, archetype_id={})",
					definition.spawnerId,
					definition.archetypeId);
				m_spawners.clear();
				return false;
			}
```

- [ ] **Step 3: Appliquer les stats aux deux sites de spawn**

À `SpawnSpawnerMob` (ServerApp.cpp:1972-1984), remplacer le bloc stats/combat par :

```cpp
		// Combat SP1 — stats data-driven depuis l'archétype (validé à l'init,
		// Find ne peut pas échouer ici ; garde défensive quand même).
		const CreatureArchetype* archetype = m_archetypeLibrary.Find(spawner.definition.archetypeId);
		const uint32_t mobHealth = (archetype != nullptr) ? archetype->hp : kDefaultMobHealth;
		const uint32_t mobDamage = (archetype != nullptr) ? archetype->damage : kDefaultMobDamage;
		mob.stats.currentHealth = mobHealth;
		mob.stats.maxHealth = mobHealth;
		mob.combat = BuildDefaultCombatComponent(m_tickHz, mobDamage);
		if (archetype != nullptr)
		{
			mob.combat.attackRangeMeters = archetype->rangeMeters;
			// attackPeriodMs → ticks (arrondi au tick supérieur, minimum 1 tick).
			mob.combat.cooldownTicks = std::max<uint32_t>(1u,
				(archetype->attackPeriodMs * m_tickHz + 999u) / 1000u);
			mob.xpReward = archetype->xpReward;
		}
```

Appliquer le même bloc au spawn de mob d'événement dynamique (ServerApp.cpp:2246-2255, avec `spawnDefinition.archetypeId`). Vérifier la formule de `BuildDefaultCombatComponent` (ServerApp.cpp:166) pour garder `cooldownTicks` homogène (la formule existante sert de référence d'unité : si elle calcule déjà `tickHz * périodeSec`, réutiliser sa convention).

- [ ] **Step 4: XP par archétype au kill**

Dans `HandleAttackRequest` (ServerApp.cpp:~2740), remplacer :

```cpp
			DistributePartyXp(*client,
			    target->positionMetersX,
			    target->positionMetersZ,
			    kBaseXpPerMobKill);
```

par :

```cpp
			// Combat SP1 — XP data-driven par archétype (fallback constante legacy
			// pour un mob spawné avant l'introduction du catalogue).
			DistributePartyXp(*client,
			    target->positionMetersX,
			    target->positionMetersZ,
			    target->xpReward != 0u ? target->xpReward : kBaseXpPerMobKill);
```

- [ ] **Step 5: Commit**

```bash
git add src/shared/server_bootstrap/ServerApp.h src/shared/server_bootstrap/ServerApp.cpp
git commit -m "feat(shardd): stats de mobs data-driven par archétype + XP au kill par archétype"
```

---

### Task 5: `archetypeId` dans les snapshots (wire-bump v8→v9)

**Files:**
- Modify: `src/shared/network/ReplicationTypes.h` (struct SnapshotEntity :107-117)
- Modify: `src/shared/network/ServerProtocol.h` (:39 kProtocolVersion)
- Modify: `src/shared/network/ServerProtocol.cpp` (EncodeSnapshot :513-547, DecodeSnapshot :298-380)
- Modify: `src/shared/server_bootstrap/ServerApp.cpp` (TryBuildSnapshotEntity :3989-4028, commentaire chunking :4268-4272 + constante kMaxEntitiesPerChunk)
- Modify: `src/shared/network/ServerProtocolTests.cpp` (tests snapshot existants)

- [ ] **Step 1: Étendre les tests de roundtrip snapshot existants**

Dans `ServerProtocolTests.cpp`, localiser les tests Encode/DecodeSnapshot (chercher `EncodeSnapshot`) et : (a) ajouter `archetypeId` non nul sur une entité de test (ex. `entity.archetypeId = 100u;`) et l'assert correspondant après décodage ; (b) ajouter un cas « mob » : `playerClientId == 0`, `characterName` vide, `archetypeId == 100` → roundtrip intact.

- [ ] **Step 2: Étendre la struct**

Dans `ReplicationTypes.h`, après `animationState` :

```cpp
		/// Combat SP1 — archétype de créature (0 = joueur ou loot bag). Permet au
		/// client de résoudre nom/niveau/mesh/échelle dans son CreatureCatalog.
		/// Wire-bump v8→v9 : u32 ajouté après animationState dans chaque entité.
		uint32_t archetypeId = 0;
```

- [ ] **Step 3: Bump + encode/decode**

`ServerProtocol.h:39` : `inline constexpr uint16_t kProtocolVersion = 9;` (et adapter le commentaire au-dessus). Dans `EncodeSnapshot` (ServerProtocol.cpp:513) : après `WriteU8(packet, static_cast<uint8_t>(entity.animationState));` ajouter :

```cpp
			// Combat SP1 (wire v9) : archétype de créature (0 = joueur / loot bag).
			WriteU32(packet, entity.archetypeId);
```

Mettre à jour le commentaire de sizing : « 8 + 40 + 4 + 2 + 2 + 1 + 4 = 61 par entité (nom et genre vides) » et le `BeginPacket(..., 24 + (entities.size() * 61))`. Dans `DecodeSnapshot` (ServerProtocol.cpp:298) : `minimumPayloadSize = 24 + (entityCount * 61)` (adapter le commentaire « 57 » → « 61 ») ; après la lecture d'`animationState` :

```cpp
			// Combat SP1 (wire v9) : archétype de créature, u32 après animationState.
			if (offset + 4u > payload.size())
			{
				outEntities.clear();
				return false;
			}
			entity.archetypeId = ReadU32(payload, offset);
			offset += 4u;
```

- [ ] **Step 4: Producteur + budget MTU**

Dans `TryBuildSnapshotEntity` (ServerApp.cpp:4011-4017), branche MobEntity :

```cpp
		if (const MobEntity* mob = FindMobByEntityId(entityId))
		{
			outEntity.entityId = mob->entityId;
			outEntity.state = BuildEntityState(*mob);
			// playerClientId reste 0 (entite non-joueur, pas de plaque joueur).
			// Combat SP1 : l'archetypeId permet au client de rendre le mob
			// (mesh/nom/niveau resolus dans son CreatureCatalog).
			outEntity.archetypeId = mob->archetypeId;
			return true;
		}
```

Dans `SendSnapshot` (ServerApp.cpp:4268-4274) : adapter le commentaire (« Une entite = 61 o ») et la constante : `constexpr size_t kMaxEntitiesPerChunk = 19u;` (1160 / 61 = 19,0 — on garde la même marge de sécurité que le 20/22 précédent).

- [ ] **Step 5: Commit**

```bash
git add src/shared/network/ReplicationTypes.h src/shared/network/ServerProtocol.h src/shared/network/ServerProtocol.cpp src/shared/server_bootstrap/ServerApp.cpp src/shared/network/ServerProtocolTests.cpp
git commit -m "feat(net): archetypeId dans SnapshotEntity — wire-bump protocole v8->v9"
```

---

### Task 6: PR-A — docs, push, CI

**Files:**
- Modify: `CODEBASE_MAP.md` (nouvelle section courte « Combat SP1 » ou enrichissement de la section serveur : catalogue d'archétypes + wire v9)
- Create: PR GitHub

- [ ] **Step 1: Mettre à jour CODEBASE_MAP.md** — 5-10 lignes : `game/data/creatures/archetypes.json`, `CreatureArchetypeLibrary` (shardd), champ `archetypeId` snapshot (v9), spec/plan sous docs/superpowers/. La spec et le plan sont committés dans cette PR (`git add docs/superpowers/specs/2026-06-10-combat-system-design.md docs/superpowers/plans/2026-06-10-combat-sp1-creatures-visibles.md docs/audit/2026-06-10-audit-global-4-parties.md`).

- [ ] **Step 2: Push + PR**

```bash
git push -u origin combat-sp1-server
gh pr create --title "feat(shardd): Combat SP1-A — archétypes de créatures data-driven + archetypeId snapshot (wire v9)" --body "<résumé + section Déploiement : ⚠️ lock-step shardd + client requis (wire-bump v9) ; merger PR-A avant PR-B>"
```

- [ ] **Step 3: Surveiller la CI** (build-linux ~10 min, build-windows ~30 min — ScheduleWakeup ~1700s) et corriger les erreurs de compilation/tests jusqu'au vert.

---

### Task 7: `CreatureCatalog` client

**Files:**
- Create: `src/client/world/CreatureCatalog.h`
- Create: `src/client/world/CreatureCatalog.cpp`
- Modify: `src/CMakeLists.txt` (liste engine_core, à côté des entrées `src/client/world/` existantes, ex. PakReader.cpp src/CMakeLists.txt:~308)

- [ ] **Step 1: Header**

```cpp
#pragma once

#include "src/shared/core/Config.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace engine::client
{
	/// Apparence client d'un archétype de créature, résolue depuis le même
	/// `game/data/creatures/archetypes.json` que le serveur (le client n'utilise
	/// que name/level/model — les stats restent l'autorité du serveur).
	struct CreatureAppearance
	{
		std::string name;
		uint32_t level = 1;
		/// Clé de mesh de race existant ("orcs", "humains", …) réutilisé pour le
		/// rendu du mob en attendant des assets créatures dédiés (cf. spec SP1).
		std::string meshKey;
		float scale = 1.0f;
	};

	/// Catalogue client des archétypes (lecture seule, chargé une fois au boot).
	/// Politique tolérante côté client (contrairement au serveur) : fichier
	/// absent/invalide = catalogue vide + LOG_WARN, les mobs sont alors rendus
	/// avec le fallback (mesh humains, nom "Créature <id>").
	class CreatureCatalog final
	{
	public:
		/// Charge `creatures/archetypes.json` depuis `paths.content`. Retourne
		/// false (catalogue vide) si absent/invalide — non bloquant côté client.
		bool Init(const engine::core::Config& config);

		/// Retourne l'apparence de l'archétype, ou nullptr si inconnu.
		const CreatureAppearance* Find(uint32_t archetypeId) const;

		/// Variante testable sans I/O (parse un texte JSON en mémoire).
		bool LoadFromText(std::string_view jsonText, std::string& outError);

	private:
		std::unordered_map<uint32_t, CreatureAppearance> m_appearances;
	};
}
```

- [ ] **Step 2: Implémentation** — copier le parseur JSON local de `SkillSystem.cpp` (convention client) ; parsing des mêmes champs que Task 2 mais en ne retenant que name/level/model ; logs `[CreatureCatalog]`. Toutes fonctions en `///` français.

- [ ] **Step 3: CMake + instanciation Engine** — ajouter `CreatureCatalog.cpp` à la liste engine_core (à côté des autres `src/client/world/*.cpp`) ; dans `Engine.h`, membre `engine::client::CreatureCatalog m_creatureCatalog;` (section des systèmes gameplay client) ; dans `Engine.cpp`, à l'init (près du chargement SkillSystem / des configs gameplay — chercher `SkillSystem` dans Engine.cpp pour le point d'ancrage) : `m_creatureCatalog.Init(m_cfg);` (non bloquant).

- [ ] **Step 4: Commit**

```bash
git add src/client/world/CreatureCatalog.h src/client/world/CreatureCatalog.cpp src/CMakeLists.txt src/client/app/Engine.h src/client/app/Engine.cpp
git commit -m "feat(client): CreatureCatalog — apparences d'archétypes de créatures"
```

---

### Task 8: `archetypeId` propagé jusqu'au modèle UI

**Files:**
- Modify: `src/client/ui_common/UIModel.h` (struct UIRemoteEntity :308-334)
- Modify: `src/client/ui_common/UIModel.cpp` (ApplySnapshot, boucle de remplissage des remoteEntities, ~:700-740)

- [ ] **Step 1: Champ UIRemoteEntity**

Après `animationState` (UIModel.h:333) :

```cpp
		/// Combat SP1 : archétype de créature propagé via le SnapshotEntity (wire
		/// v9). 0 pour les joueurs et loot bags ; ≠ 0 pour les mobs — le rendu
		/// résout nom/niveau/mesh/échelle via Engine::m_creatureCatalog.
		uint32_t archetypeId = 0;
```

- [ ] **Step 2: Copie dans ApplySnapshot**

Dans la boucle de `UIModel.cpp` qui construit `m_model.remoteEntities` depuis `m_snapshotScratch` (après la copie de `animationState`), ajouter `remote.archetypeId = entity.archetypeId;` (adapter au nom de variable locale réel de la boucle).

- [ ] **Step 3: Commit**

```bash
git add src/client/ui_common/UIModel.h src/client/ui_common/UIModel.cpp
git commit -m "feat(client): archetypeId propagé du snapshot au UIRemoteEntity"
```

---

### Task 9: Rendu des mobs — mesh + plaque de nom

**Files:**
- Modify: `src/client/app/Engine.cpp` — boucle nameplates (~:10179-10215) et `RecordRemoteAvatars` (~:11855-11940)

- [ ] **Step 1: Plaque de nom des mobs**

Dans la boucle nameplate (Engine.cpp:10189), remplacer le `continue` inconditionnel :

```cpp
					if (re.playerClientId == 0u && re.archetypeId == 0u)
						continue; // loot bag : pas de nameplate
```

et construire le label mob après le label joueur existant :

```cpp
					// Combat SP1 — plaque mob : "Nom (niv. N)  PV/PVmax" résolue
					// via le CreatureCatalog ; fallback générique si l'archétype
					// est inconnu du catalogue client (catalogue absent/désynchro).
					std::string label;
					if (re.playerClientId != 0u)
					{
						label = !re.displayName.empty()
							? re.displayName
							: ("P" + std::to_string(re.playerClientId));
					}
					else
					{
						const engine::client::CreatureAppearance* appearance =
							m_creatureCatalog.Find(re.archetypeId);
						const std::string mobName = (appearance != nullptr)
							? appearance->name
							: ("Creature " + std::to_string(re.archetypeId));
						const uint32_t mobLevel = (appearance != nullptr) ? appearance->level : 1u;
						label = mobName + " (niv. " + std::to_string(mobLevel) + ")  "
							+ std::to_string(re.currentHealth) + "/" + std::to_string(re.maxHealth);
					}
```

(La couleur du texte mob peut rester celle des joueurs en SP1 ; ne pas afficher la plaque si `(re.stateFlags & 1u) != 0` — mob mort en attente de despawn.)

- [ ] **Step 2: Mesh des mobs dans RecordRemoteAvatars**

Au `continue` des `playerClientId == 0u` (Engine.cpp:11866), remplacer par une résolution d'apparence :

```cpp
			// Combat SP1 — les mobs (archetypeId != 0) réutilisent un mesh de race
			// existant déclaré par le catalogue ; les loot bags (archetypeId == 0)
			// gardent leur pipeline dédié (pas de mesh humanoïde).
			std::string meshRace;
			std::string meshGender;
			float meshScale = 1.0f;
			if (re.playerClientId != 0u)
			{
				meshRace = "humains";
				meshGender = (re.gender == "male" || re.gender == "female") ? re.gender : std::string{"male"};
			}
			else if (re.archetypeId != 0u)
			{
				const engine::client::CreatureAppearance* appearance = m_creatureCatalog.Find(re.archetypeId);
				meshRace = (appearance != nullptr && !appearance->meshKey.empty()) ? appearance->meshKey : std::string{"humains"};
				meshGender = "male";
				meshScale = (appearance != nullptr) ? appearance->scale : 1.0f;
			}
			else
			{
				continue; // loot bag
			}
			engine::render::skinned::SkinnedMesh* remoteMesh = GetRaceMesh(meshRace, meshGender);
			if (remoteMesh == nullptr && re.archetypeId != 0u)
				remoteMesh = GetRaceMesh("humains", "male"); // fallback mob : race inconnue du registre
			if (remoteMesh == nullptr)
				continue;
```

puis appliquer `meshScale` à la matrice modèle du draw (localiser dans la suite de la boucle la construction de la matrice modèle — translation/rotation à partir de `px/py/pz/yaw` — et multiplier par une matrice d'échelle uniforme `Mat4::Scale(meshScale)` si l'API Mat4 du repo l'offre, sinon multiplier les 3 colonnes de rotation par `meshScale` ; vérifier `src/shared/math/Math.h`). Si l'échelle complexifie (skinning), la différer avec un commentaire `// Combat SP1 : échelle différée (skinning), cf. plan` — l'échelle est cosmétique, pas bloquante.

Attention budget : les mobs entrent dans le ring SSBO partagé du SkinnedRenderer (kFrameSlots=32, constat d'audit : >16 draws skinnés/frame = risque de pose corrompue). La zone_0 n'a qu'1 mob ; ajouter un commentaire au site de draw mob rappelant la limite, sans la corriger ici.

- [ ] **Step 3: Animation des mobs** — les mobs arrivent avec `animationState = Idle` ; vérifier que le chemin d'animation des avatars distants tolère une entité sans `gender`/`characterName` (déjà le cas pour les joueurs sans nom). Pas d'animation de combat en SP1.

- [ ] **Step 4: Commit**

```bash
git add src/client/app/Engine.cpp
git commit -m "feat(client): rendu des mobs — mesh d'archétype + plaque nom/niveau/PV"
```

---

### Task 10: PR-B — docs, push, CI

- [ ] **Step 1: CODEBASE_MAP.md** — compléter la section Combat SP1 (CreatureCatalog client, rendu mobs).
- [ ] **Step 2: Push + PR stackée**

```bash
git push -u origin combat-sp1-client
gh pr create --base combat-sp1-server --title "feat(client): Combat SP1-B — rendu des créatures (mesh archétype + nameplates)" --body "<résumé + Déploiement : ⚠️ lock-step shardd + client (wire v9, porté par PR-A) ; ordre de merge : PR-A puis PR-B>"
```

- [ ] **Step 3: CI verte sur les deux PRs**, puis indiquer à l'utilisateur : merger PR-A puis PR-B, redéployer shardd ET redistribuer le client en lock-step (un client v8 ne décode plus les snapshots v9), valider en jeu : le sanglier de zone_0 visible avec sa plaque, attaquable… non — l'attaque client arrive en SP2 ; valider : mob visible, plaque correcte, pas de régression joueurs distants.

---

## Self-review (faite à l'écriture)

- **Couverture spec §4** : 4.1 données → Task 1 ; 4.2 stats serveur + xpReward → Task 4 ; 4.3 wire → Task 5 ; 4.4 rendu → Tasks 7-9 ; 4.5 livraison/déploiement → Tasks 6/10. §8 tests → Tasks 2/5. §9 déploiement → Tasks 6/10. Pas d'écart.
- **Placeholders** : les deux points volontairement délégués à l'exécution (harnais exact des tests shardd, API d'échelle de Mat4) sont bornés par des instructions de localisation précises et un fallback documenté — pas de « TBD » ouvert.
- **Cohérence de types** : `CreatureArchetype.meshKey`/`CreatureAppearance.meshKey` string ; `archetypeId` uint32 partout (wire u32) ; `xpReward` uint32 ; `kMaxEntitiesPerChunk` 20→19 cohérent avec l'entité passée de 57 à 61 octets min.
