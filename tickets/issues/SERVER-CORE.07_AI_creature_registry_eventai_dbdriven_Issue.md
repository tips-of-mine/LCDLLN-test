# Issue: SERVER-CORE.07

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- src/shardd/ai/EventAI.h
- src/shardd/ai/EventAITests.cpp

## Note
EventAI db-driven + tests

---

## Contenu du ticket (SERVER-CORE.07)

# SERVER-CORE.07_AI_creature_registry_eventai_dbdriven

## Objectif

Mettre en place un **framework d'IA pour PNJ** côté shard LCDLLN inspiré
de `src/game/AI` server-core. Trois piliers :

1. **Registry + Selector pattern** : chaque IA s'auto-enregistre par nom
   au boot, le sélecteur instancie via factory `string→ctor` selon
   `creature_template.AIName` ou un flag scripts. Ajout d'une nouvelle
   IA = ajout dans le registry, pas de modification du cœur.
2. **`EventAI` piloté par DB** : table `creature_ai_scripts`
   `(entry, event_type, event_param, action_type, action_param,
   chance, repeat_min, repeat_max)` interprétée par un moteur
   générique. Permet de scripter un boss simple sans recompiler.
3. **`PlayerAI`** comme contrepartie : même interface que CreatureAI mais
   sur Player (utilisé pour mind control, fear, charm).
4. **`ScriptDevAI`** comme couche séparée pour les boss complexes en C++,
   isolant le code "contenu" du code "moteur" — bon découpage si le
   contenu PvE devient riche.

C'est un **P2 shard**, pré-requis dès le premier PNJ avec comportement
non trivial.

## Dépendances

- M00.1 (build base)
- SERVER-CORE.02 (Entities) — `Creature` porte un `CreatureAI*`
- SERVER-CORE.04 (Movement) — l'IA pousse des `MoveSplineInit` au mouvement
- SERVER-CORE.11 (Combat) — l'IA réagit aux events `OnAggro`, `OnDamageDealt`,
  `OnDamageTaken`
- SERVER-CORE.20 (MotionGenerators) — l'IA pousse/pop des générateurs sur
  le `MotionMaster`

## Livrables

### Côté shard (`engine/server/shard/ai/`)

- `CreatureAI.{h,cpp}` — interface abstraite. Méthodes virtuelles vides
  par défaut :
  - `OnAggro(Unit& enemy)`
  - `OnDamageTaken(Unit& attacker, uint32 damage)`
  - `OnDamageDealt(Unit& victim, uint32 damage)`
  - `OnEvade()` — sortie de combat anormale
  - `OnDeath(Unit* killer)`
  - `OnSpellHit(Unit& caster, SpellTemplate const&)`
  - `Update(int32 deltaMs)` — appelé chaque tick par la Map
- `BaseAI.{h,cpp}` — implémentation no-op (aggressive sur tout joueur dans
  le rayon, melee uniquement). IA par défaut.
- `EventAI.{h,cpp}` — interpréteur de scripts DB.
- `PlayerAI.{h,cpp}` — sous-classe pour Player charmé/fear (à activer/
  désactiver à la volée).
- `CreatureAIRegistry.{h,cpp}` — map `aiName → factory`. API
  `Register(name, factory)`, `Create(name, creature)`.
- `CreatureAISelector.{h,cpp}` — sélectionne l'IA pour une Creature
  spawnée :
  1. Si `creature.AIName` dans la DB est non-vide → instancier cette IA.
  2. Sinon, si `creature.HasEventAIScript()` → `EventAI`.
  3. Sinon → `BaseAI`.

### Migration DB

- `engine/server/migrations/00xx_creature_ai.sql` :
  ```sql
  ALTER TABLE creature_template
    ADD COLUMN ai_name VARCHAR(64) NOT NULL DEFAULT '' AFTER faction;

  CREATE TABLE creature_ai_scripts (
    creature_entry  INT UNSIGNED NOT NULL,
    event_id        INT UNSIGNED NOT NULL,
    event_type      TINYINT UNSIGNED NOT NULL,    -- enum EventAIEventType
    event_param1    INT NOT NULL DEFAULT 0,
    event_param2    INT NOT NULL DEFAULT 0,
    chance          TINYINT UNSIGNED NOT NULL DEFAULT 100,
    repeat_min_ms   INT UNSIGNED NOT NULL DEFAULT 0,
    repeat_max_ms   INT UNSIGNED NOT NULL DEFAULT 0,
    action_type     TINYINT UNSIGNED NOT NULL,    -- enum EventAIActionType
    action_param1   INT NOT NULL DEFAULT 0,
    action_param2   INT NOT NULL DEFAULT 0,
    action_param3   INT NOT NULL DEFAULT 0,
    PRIMARY KEY (creature_entry, event_id)
  );
  ```

### Tests

- `engine/server/shard/ai/CreatureAIRegistryTests.cpp` — register +
  retrieve.
- `engine/server/shard/ai/EventAITests.cpp` — un script
  `OnAggro → CastSpell` produit l'action attendue ; cooldown respecté.
- `engine/server/shard/ai/CreatureAISelectorTests.cpp` — sélection
  correcte selon AIName / scripts / fallback.

### Configuration (`config.json`)

```json
"ai": {
  "default_aggro_range_m": 20.0,
  "evade_distance_m": 50.0,
  "evade_full_heal": true,
  "event_ai_enabled": true
}
```

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- Contenu : N/A
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. EventAI events de référence

| Event type | Event param | Trigger |
|---|---|---|
| OnAggro | — | Premier joueur aggro |
| OnDamageTaken_HpBelow | hp_pct | Quand HP descend sous X% |
| OnTargetCasting | spell_id | Cible commence un cast |
| OnSpawn | — | À l'apparition |
| OnTimer | period_ms | Périodique (utilise `repeat_min/max`) |
| OnDeath | — | Au moment de mourir |
| OnEnemyOOM | — | Cible cible plus de mana |

### 2. EventAI actions de référence

| Action type | Params | Effet |
|---|---|---|
| CastSpell | spell_id, target | Lance un sort |
| Say | text_id | Dit un message localisé |
| Yell | text_id | Cri (zone broadcast) |
| Despawn | delay_ms | Disparait |
| FleeForAssistance | — | Fuit chercher renfort |
| CallForHelp | radius_m | Appelle créatures alliées proches |
| ChangePhase | phase_id | Bascule la phase d'IA |

Set initial minimaliste — extensible par ticket ultérieur.

### 3. Registry + Selector

```cpp
using CreatureAIFactory = std::function<std::unique_ptr<CreatureAI>(Creature&)>;

class CreatureAIRegistry {
public:
  void Register(std::string name, CreatureAIFactory factory);
  std::unique_ptr<CreatureAI> Create(std::string_view name, Creature& c) const;
};

// au boot :
g_aiRegistry.Register("EventAI", [](Creature& c){ return std::make_unique<EventAI>(c); });
g_aiRegistry.Register("BaseAI",  [](Creature& c){ return std::make_unique<BaseAI>(c); });
// futur : "Boss_Ragnaros", etc., enregistrés par ScriptDevAI module
```

## Étapes d'implémentation

1. Créer `engine/server/shard/ai/` + interfaces.
2. Implémenter `CreatureAI` (interface) + `BaseAI` (impl basique).
3. Implémenter `CreatureAIRegistry` + `CreatureAISelector`.
4. Câbler `Creature` (SERVER-CORE.02) pour porter `m_ai` et l'appeler dans `Update()`.
5. Migration DB `00xx_creature_ai.sql`.
6. Implémenter `EventAI` : load scripts au boot, dispatch events, exécute actions.
7. Implémenter `PlayerAI` (charm/fear/possession).
8. Tests : 3 fichiers listés.
9. Doc : section « AI shard » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard)
- [ ] Tests `CreatureAI*Tests`, `EventAITests` passent
- [ ] Smoke test : un PNJ avec script `OnAggro → Yell + CastSpell` exécute les 2 actions à la prise d'aggro
- [ ] PlayerAI : charme un joueur, son input est ignoré, l'IA prend le contrôle ; fin du charme, contrôle restauré
- [ ] Migration `00xx_creature_ai.sql` appliquée et idempotente
- [ ] Aucun nouveau dossier racine non autorisé créé
- [ ] Rapport final : fichiers modifiés + commandes + résultats + DoD

## Notes / pièges à éviter

- **Cooldowns** : ne **pas** câbler les cooldowns des actions dans EventAI globalement ; chaque event a son `repeat_min_ms / repeat_max_ms`. Sinon une IA sophistiquée se bloque avec un cooldown global.
- **Threading** : l'IA tourne dans le tick Map (SERVER-CORE.13). `BaseAI::Update` ne doit **pas** prendre de lock global. Si l'IA accède à des données partagées (météo, world state), passer par un cache thread-local ou une queue.
- **PlayerAI lifecycle** : à l'activation (charme), sauvegarder l'AIController courant ; à la fin, restaurer. Ne **pas** détruire l'IA d'origine.
- **Cycles d'aggro** : si l'IA appelle `CallForHelp` et que les renforts appellent à leur tour → boucle. Ajouter un timestamp `lastCallForHelp` pour debounce.
- **Hot-reload** : pour itérer sur du contenu, prévoir une commande GM `.reload event_ai_scripts` qui purge le cache + recharge la table sans restart shard. À ajouter quand la commande router (SERVER-CORE.01) est en place.

## Références

- `SERVER-CORE_ANALYSIS.md` § AI (P2 shard)
- server-core `src/game/AI/CreatureAI.cpp`, `EventAI/`, `BaseAI.cpp`,
  `PlayerAI.cpp`, `CreatureAIRegistry.cpp`, `CreatureAISelector.cpp`
