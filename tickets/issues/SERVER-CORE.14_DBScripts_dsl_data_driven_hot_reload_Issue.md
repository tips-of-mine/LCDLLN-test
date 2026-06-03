# Issue: SERVER-CORE.14

**Status:** Closed

_Re-verifie DONE le 2026-06-03 (correction d'un faux-negatif du au decalage de chemins engine/ -> src/)._

## Preuves d'implementation
- src/shardd/dbscripts/DBScript.h
- src/shardd/dbscripts/DBScriptRuntime.h

## Note
DBScript VM + runtime + tests sous src/shardd/dbscripts (faux-negatif corrige)

---

## Contenu du ticket (SERVER-CORE.14)

# SERVER-CORE.14_DBScripts_dsl_data_driven_hot_reload

## Objectif

Mettre en place un **moteur d'événements scriptés piloté par DB** côté
shard LCDLLN, inspiré de `src/game/DBScripts` server-core. Quatre piliers :

1. **Tables `*_scripts` génériques** : `quest_start_scripts`,
   `quest_end_scripts`, `gameobject_use_scripts`, `event_scripts`,
   `spell_scripts` avec colonnes communes
   `(id, delay, command, data1, data2, data3, target_type)`.
2. **DSL minimaliste à ~30 commandes** : TALK, MOVE_TO, KILL_CREDIT,
   CAST_SPELL, RESPAWN_GO, OPEN_DOOR, DESPAWN, SUMMON, TELEPORT, …
   suffisamment expressif pour 95% du contenu PvE narratif **sans
   embarquer Lua/JS**.
3. **Délais entre commandes** (`delay` en secondes) : permet des
   séquences chronométrées (PNJ parle, attend 3s, marche, attend 2s,
   despawn) sans coroutines ni state machine ad hoc.
4. **Hot-reload via `.reload all_scripts`** GM command : itération
   contenu sans restart serveur — feature critique pour les game
   designers.

C'est un **P2 shard** quand le contenu PvE narratif arrive.

## Dépendances

- M00.1 (build base)
- SERVER-CORE.13 (Database — SQLStorage pour cache des scripts)
- SERVER-CORE.07 (AI — script peut être déclenché par event AI)
- SERVER-CORE.16 (Globals/Conditions — `condition_id` filtre l'exécution)
- SERVER-CORE.01 (Chat — commande `.reload`)

## Livrables

### Côté shard (`engine/server/shard/scripts/`)

- `ScriptCommand.h` — enum centralisée des commandes (`SCRIPT_TALK`,
  `SCRIPT_MOVE_TO`, `SCRIPT_CAST_SPELL`, …).
- `ScriptTargetType.h` — enum des targets (`TARGET_SELF`,
  `TARGET_NEAREST_CREATURE`, `TARGET_PLAYER_SOURCE`, …).
- `ScriptEntry.h` — struct row de la table : id, delay, command, params,
  target_type, condition_id.
- `ScriptMgr.{h,cpp}` — singleton :
  - `Load(ConnectionPool&)` — charge toutes les tables de scripts.
  - `Run(scriptType, scriptId, source, target)` — démarre l'exécution
    asynchrone d'un script.
  - `Tick(int32 dtMs)` — ticke les scripts en cours, exécute les
    commandes dont le delay est écoulé.
  - `ReloadAll()` — re-load DB.
- `ScriptInstance.{h,cpp}` — instance d'un script en cours : référence
  vers le ScriptEntry, source/target résolus, position courante (idx
  de la commande), timer.

### Migration DB

```sql
CREATE TABLE event_scripts (
  id            INT UNSIGNED NOT NULL,
  delay_sec     INT UNSIGNED NOT NULL DEFAULT 0,
  command       TINYINT UNSIGNED NOT NULL,
  data1         INT NOT NULL DEFAULT 0,
  data2         INT NOT NULL DEFAULT 0,
  data3         INT NOT NULL DEFAULT 0,
  target_type   TINYINT UNSIGNED NOT NULL DEFAULT 0,
  condition_id  INT UNSIGNED NOT NULL DEFAULT 0,
  comment       VARCHAR(255),
  PRIMARY KEY (id, delay_sec, command, data1)  -- composite, plusieurs commandes par script
);

-- 4 autres tables avec même schéma : quest_start_scripts, quest_end_scripts,
-- gameobject_use_scripts, spell_scripts.
```

### Configuration (`config.json`)

```json
"scripts": {
  "max_concurrent_instances": 1000,
  "tick_interval_ms": 100,
  "log_command_execution": false
}
```

### Tests

- `ScriptMgrTests.cpp` — load/reload, run avec délai 3s exécute la 2ᵉ commande après ticks correspondants.
- `ScriptCommandsTests.cpp` — chaque commande supportée a un test minimal.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. Set initial de commandes (15+)

| Command | data1 | data2 | data3 | Effet |
|---|---|---|---|---|
| TALK | text_id | chat_type | — | PNJ parle |
| EMOTE | emote_id | — | — | PNJ joue émote |
| FIELD_SET | field_idx | value | — | Modifie UpdateField |
| MOVE_TO | x | y | z | PNJ va à un point |
| FLAG_SET | field_idx | flag | — | Set bit flag |
| FLAG_REMOVE | field_idx | flag | — | Clear bit flag |
| TELEPORT_TO | map_id | x_y_packed | z | TP joueur target |
| KILL_CREDIT | creature_entry | — | — | Donne kill credit pour quête |
| RESPAWN_GO | go_guid | despawn_delay | — | Respawn un GameObject |
| OPEN_DOOR | go_guid | reset_delay | — | Ouvre une porte |
| CLOSE_DOOR | go_guid | reset_delay | — | Ferme |
| DESPAWN | delay | — | — | Despawn target |
| SUMMON_CREATURE | entry | duration | — | Spawn créature |
| CAST_SPELL | spell_id | flags | — | Cast un sort |
| QUEST_COMPLETE | quest_id | — | — | Marque quête comme complète |
| ACTIVATE_OBJECT | — | — | — | Active GO trigger |
| RUN_SCRIPT | other_script_id | — | — | Chaîne sur un autre script |

À étendre selon besoins.

### 2. Targeting

```cpp
enum class ScriptTargetType : uint8 {
  Self,
  Source,
  NearestCreature,    // data2 = entry, data3 = max_radius
  PlayerSource,
  GameObject,         // data3 = go_guid
};
```

Résolution au runtime quand la commande s'exécute (pas au load), pour
gérer les entités spawned dynamiquement.

### 3. Hot-reload

Commande GM `.reload event_scripts` (ou `all_scripts`) :
1. Stoppe les ScriptInstance en cours (ou les laisse finir avec l'ancien template, choisir).
2. Vide le cache `ScriptMgr::m_scripts`.
3. Recharge la DB.
4. Log : `[ScriptMgr] reloaded N scripts in M ms`.

## Étapes d'implémentation

1. Migration DB des 5 tables.
2. Implémenter `ScriptMgr::Load`.
3. Implémenter `ScriptInstance::Tick` avec dispatch sur commande.
4. Implémenter 5 commandes minimales (TALK, MOVE_TO, CAST_SPELL, DESPAWN, RUN_SCRIPT) — étendre ensuite.
5. Câbler `Quest::OnAccept` → `ScriptMgr::Run("quest_start_scripts", quest.start_script_id, player, npc)`.
6. Implémenter hot-reload + commande GM `.reload event_scripts`.
7. Tests : 2 fichiers.
8. Doc : section « DBScripts shard » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard)
- [ ] Tests passent
- [ ] Smoke test : script "TALK 'Hello' → MOVE_TO (10,20,5) → DESPAWN" exécute les 3 commandes dans l'ordre avec les delays
- [ ] `.reload event_scripts` recharge sans restart shard
- [ ] Migrations idempotentes
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Cas d'échec** : si `MOVE_TO` ne peut pas atteindre la position (path bloqué), continuer ? Abort ? Choisir : par défaut, continuer après un timeout (5s) avec un log warning. Sinon un script bug bloque la séquence indéfiniment.
- **Cycles** : un script qui RUN_SCRIPT vers lui-même → boucle. Ajouter un `max_depth = 8` dans `ScriptInstance` pour break.
- **Threading** : `ScriptMgr::Tick` est appelé par la Map. Pas de lock à prendre — chaque ScriptInstance vit dans la Map de sa source.
- **Localization** : `TALK` avec `text_id` doit pointer vers une table `localized_text` (i18n) ; pas de string littérale.
- **Hot-reload sans race** : pendant le reload, un script en cours peut référencer un `ScriptEntry` dont l'adresse mémoire change. Soit (a) attendre que les instances en cours finissent avant de purger, soit (b) faire un swap atomique des pointeurs et garder l'ancien jusqu'au prochain tick.
- **Pas de Lua / pas de JS** : volontairement minimaliste. Si un designer demande "je veux du if/else complexe", c'est probablement le moment d'écrire un C++ ScriptDevAI (SERVER-CORE.07) — pas d'étendre le DSL en VM.
- **Logging** : `log_command_execution` actif uniquement en dev (très verbeux). Filtrer via `LogFilter::DbScriptsDev` (déjà disponible PR #468).

## Références

- `SERVER-CORE_ANALYSIS.md` § DBScripts (P2 shard)
- server-core `src/game/DBScripts/ScriptMgr.cpp`, `ScriptMgrDefines.h`
