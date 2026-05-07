# CMANGOS.28_World_state_expression_dsl

## Objectif

Mettre en place le **système de world state + mini-DSL d'expressions**
côté shard LCDLLN, inspiré de `src/game/World` cmangos. Trois piliers :

1. **`WorldStateVariableManager`** : registre clé→valeur typé,
   observable (subscribe par zone/joueur), persistable. Pattern
   « shared blackboard » excellent pour events serveur-wide
   (compteurs de captures BG, score arène, progression event saisonnier).
2. **`WorldStateExpression` mini-DSL** : parse des formules
   (`worldstate_42 > 100 AND worldstate_43 == 1`) évaluables runtime
   depuis DB. **Énorme leverage éditorial** : conditions de quêtes/
   events sans recompiler. Particulièrement utile pour LCDLLN avec son
   `lcdlln_world_editor.exe`.
3. **Tick global multi-niveaux** : World tick → Map tick → Object tick,
   chacun à fréquence propre (1s / 100ms / variable). Modèle
   hiérarchique pour maîtriser le coût CPU.
4. **Shutdown gracieux** : broadcast countdown 5min/1min/30s, kick safe,
   flush DB. Évite la perte de données.

C'est un **P2 shard** + intégration master pour broadcast.

## Dépendances

- M00.1 (build base)
- CMANGOS.13 (Database — persistance)
- CMANGOS.16 (Globals/Conditions — alternative ciblée pour les prédicats simples)
- CMANGOS.19 (Maps — tick hiérarchique)

## Livrables

### Côté shard (`engine/server/shard/world/`)

- `WorldState.{h,cpp}` — singleton global :
  - `Get(stateId) → int32`
  - `Set(stateId, value)` — broadcast aux subscribers
  - `Subscribe(stateId, callback)` — pour notifications
- `WorldStateExpression.{h,cpp}` — parseur + évaluateur du DSL :
  ```cpp
  ExpressionId Parse(std::string_view formula);
  bool Evaluate(ExpressionId, EvaluationContext const&) const;
  ```
- `WorldStateExpressionLexer.{h,cpp}` — tokenize.
- `WorldStateExpressionAst.h` — AST nodes.
- `World.{h,cpp}` — singleton orchestrateur :
  - `Tick(int64 nowMs)` — appelé par le main loop, dispatch aux Maps via MapManager.
  - `Shutdown(int countdownSec)` — broadcast + kick + flush DB.

### Migration DB

```sql
CREATE TABLE world_states (
  state_id      INT UNSIGNED NOT NULL,
  value         INT NOT NULL DEFAULT 0,
  description   VARCHAR(255),
  persistent    TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (state_id)
);

CREATE TABLE world_state_expressions (
  expression_id INT UNSIGNED NOT NULL,
  formula       TEXT NOT NULL,
  description   VARCHAR(255),
  PRIMARY KEY (expression_id)
);
```

### Configuration (`config.json`)

```json
"world": {
  "tick_interval_ms": 1000,
  "shutdown_default_countdown_sec": 300,
  "world_state_persist_interval_sec": 60
}
```

### Tests

- `WorldStateTests.cpp` — Get/Set ; subscribe + notification.
- `WorldStateExpressionTests.cpp` — parse `worldstate_42 > 100`,
  parse expression composite avec AND/OR/NOT/parenthèses, gestion des erreurs syntaxe.
- `WorldShutdownTests.cpp` — countdown produit broadcasts aux 5min/1min/30s.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. DSL grammaire (BNF simplifié)

```
expression  := term (('AND' | 'OR') term)*
term        := factor | 'NOT' factor | '(' expression ')'
factor      := worldstate cmp_op literal
worldstate  := 'worldstate_' INT
cmp_op      := '==' | '!=' | '<' | '<=' | '>' | '>='
literal     := INT
```

Exemples valides :

- `worldstate_42 == 1`
- `worldstate_10 > 100 AND worldstate_11 == 0`
- `(worldstate_5 > 0 OR worldstate_6 > 0) AND NOT worldstate_7 == 1`

### 2. Parsing

Lexer simple (régex/scan), parser récursif descendant (pas besoin de
Bison). AST en `std::variant` ou hiérarchie classes :

```cpp
struct ExprBinaryOp { ExprPtr lhs; BinOp op; ExprPtr rhs; };
struct ExprUnaryOp  { UnaryOp op; ExprPtr operand; };
struct ExprWsCompare { uint32_t stateId; CmpOp op; int32_t value; };
using Expr = std::variant<ExprBinaryOp, ExprUnaryOp, ExprWsCompare>;
```

### 3. Évaluation

```cpp
bool Evaluate(Expr const& e, EvaluationContext const& ctx) {
  return std::visit(EvalVisitor{ctx}, e);
}
```

`EvaluationContext` contient `WorldState const&` (pour les `worldstate_X`)
et potentiellement le joueur courant pour des extensions (`player_level`, `player_class`).

### 4. Tick hiérarchique

```cpp
void World::Tick(int64 nowMs) {
  // 1s : world state save batched, broadcast cluster summary, expirations
  if (nowMs - m_lastWorldTick >= 1000) {
    PersistWorldStates();
    BroadcastClusterSummary();
    m_lastWorldTick = nowMs;
  }
  // 100ms : delegate to map tick (CMANGOS.19)
  g_mapManager.UpdateAllMaps(nowMs);
}
```

### 5. Shutdown gracieux

```cpp
void World::Shutdown(int countdownSec) {
  for (int t : {300, 60, 30, 10, 5, 1}) {
    if (t <= countdownSec) {
      ScheduleBroadcast(t, "Server shutdown in " + format_time(t));
    }
  }
  ScheduleAt(countdownSec, [] {
    KickAllPlayers();
    FlushAllDb();
    ExitProcess();
  });
}
```

## Étapes d'implémentation

1. Créer `engine/server/shard/world/`.
2. Migration DB des 2 tables.
3. Implémenter `WorldState` (singleton + subscribe).
4. Implémenter `WorldStateExpressionLexer` + parser récursif.
5. Implémenter `Evaluate(Expr, ctx)`.
6. Implémenter tick hiérarchique dans `World::Tick`.
7. Implémenter `Shutdown` gracieux.
8. Tests : 3 fichiers.
9. Doc : section « World state + DSL » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard)
- [ ] Tests passent
- [ ] Smoke test DSL : parse + eval `worldstate_5 > 0 AND worldstate_6 == 1` avec valeurs connues
- [ ] Smoke test shutdown : `Shutdown(60)` produit broadcasts à 60/30/10/5/1s
- [ ] WorldState persiste via `world_states.persistent = 1` après reboot
- [ ] Migrations idempotentes
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Erreurs DSL** : si une expression contient une syntaxe invalide, ne **pas** crash. Logger un warning et retourner `false` à l'évaluation. Le parsing se fait au load, idéalement on rejette la mauvaise expression dès là.
- **Performance évaluation** : si une expression est évaluée à chaque tick (ex. boss enrage timer), profiler. Cache l'AST (parse une fois, évalue N fois).
- **Subscribe/notify** : éviter callback chains à 10 niveaux. Si un callback `Set` un autre worldstate qui re-notify, on a un cycle. Détecter avec un compteur de récursion local.
- **Shutdown abuse** : la commande GM `.shutdown N` doit être réservée à `Administrator`. Sinon un GM peut crasher le shard.
- **Broadcasts pendant shutdown** : pendant le countdown, **désactiver** les broadcasts non critiques (météo, cluster summary). Seuls les "shutdown imminent" passent.
- **Compatibilité avec Conditions** : si `WorldStateExpression` est trop puissant et `Conditions` (CMANGOS.16) trop simple, on a deux DSL en parallèle. Choisir : `Conditions` pour les prédicats joueur (level, has_aura), `WorldStateExpression` pour les états globaux. Documenter clairement la séparation.
- **DSL extensibility** : démarrer **strict** (worldstate_X et int literal). Étendre seulement quand un cas concret le demande (`player_level`, `is_in_zone`). Sinon on réinvente Lua.

## Références

- `CMANGOS_ANALYSIS.md` § World (P2 shard)
- cmangos `src/game/World/World.cpp`, `WorldState.cpp`,
  `WorldStateExpression.cpp`, `WorldStateExpressionDefines.h`
