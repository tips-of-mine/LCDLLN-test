# CMANGOS.28 — World (state expression DSL)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.28_World_state_expression_dsl.md](../../../../tickets/CMANGOS/CMANGOS.28_World_state_expression_dsl.md)
> **Priorité** : P2 — gameplay essentiel
> **Cible** : shard + intégration master

## 1. Statut implémentation

❌ **Absent** — pas de `WorldStateVariableManager`, pas de mini-DSL
d'expressions évaluable runtime, pas de tick global multi-niveaux,
pas de shutdown gracieux broadcast countdown.

## 2. Preuves dans le code

**Manquant (vs spec ticket) :**
- ❌ `engine/server/shard/world/` — dossier inexistant
- ❌ `WorldState` singleton (Get/Set/Subscribe sur stateId)
- ❌ `WorldStateExpression` parseur + évaluateur DSL
- ❌ `WorldStateExpressionLexer` + AST nodes
- ❌ `World` singleton orchestrateur (Tick + Shutdown gracieux)
- ❌ Tick global multi-niveaux (World 1s / Map 100ms / Object variable)
- ❌ Tables DB `world_states`, `world_state_expressions`
- ❌ Broadcast shutdown countdown (5min/1min/30s)
- ❌ Migration DB

## 3. Recouvrement milestones existantes

❌ **Non couvert** — aucune milestone "world state DSL" dans M00-M44.

## 4. Écart par rapport à la spec CMANGOS

100% absent. Le ticket apporte 3 patterns :

1. **WorldStateVariableManager** — blackboard partagé, observable.
   Excellent pour events serveur (compteurs BG capture, score arène,
   progression event saisonnier). Sans ça, chaque feature recrée son
   propre compteur global.
2. **WorldStateExpression DSL** — leverage éditorial **énorme** : un
   designer écrit `worldstate_42 > 100 AND worldstate_43 == 1` en SQL,
   ça devient une condition évaluable runtime. Particulièrement adapté
   à LCDLLN avec son `lcdlln_world_editor.exe`.
3. **Tick hiérarchique** — World 1s / Map 100ms / Object variable.
   Maîtrise du coût CPU.
4. **Shutdown gracieux** — broadcast + flush DB. Évite perte de
   données au reboot.

## 5. Effort estimé

**M-L** (3 PR) :
- PR 1 : `WorldState` singleton + `WorldStateVariableManager` +
  migration DB + opcodes update client
- PR 2 : `WorldStateExpression` lexer + parser + évaluateur + tests
  parsing + tests évaluation
- PR 3 : `World` orchestrateur + tick hiérarchique + shutdown gracieux

Wire-breaking probable (opcodes `WORLD_STATE_UPDATE` côté client). Migration DB.

## 6. Valeur joueur/serveur

**Élevée** — invisible joueur direct mais **gros leverage content**.
ExpressionDSL = designer indépendant pour conditions complexes.

WorldState observable = pattern réutilisé partout (compteurs, events
saisonniers, BG state, etc.).

Shutdown gracieux = visible joueur (countdown au lieu de kick brutal).

## 7. Dépendances bloquantes

Le ticket dépend explicitement de :
- **CMANGOS.13 Database** — persistance
- **CMANGOS.16 Globals/Conditions** — alternative pour prédicats
  simples (les deux peuvent coexister)
- **CMANGOS.19 Maps** — tick hiérarchique

## 8. Risque / piège ⚠️

- ⚠️ **Migration DB** — `world_states` + `world_state_expressions`.
  Idempotent.
- ⚠️ **Wire-breaking** — opcode `WORLD_STATE_UPDATE` pour push client.
  Bump.
- ⚠️ **Parser DSL** — bug parsing = silently wrong evaluations. Tests
  exhaustifs (tokens, précédence, parenthèses, opérateurs).
- ⚠️ **Doublon avec Conditions (CMANGOS.16)** — risque divergence.
  Convention : Conditions pour prédicats simples (HasItem, LevelGE),
  Expressions pour formules complexes (>= 2 variables + opérateurs).
- ⚠️ **Tick hiérarchique** — bug : un tick lent dans World ralentit
  Map. Mesurer + watchdog.
- ⚠️ **Shutdown gracieux** — race conditions (joueur déco pendant
  flush). Idempotence des operations DB.
- ⚠️ **WorldState volumétrie** — 1000+ stateIds × 100 maps × 5 maj/s =
  500k events/s. Optimisation `Subscribe` (pas broadcast à tous, juste
  les concernés).
- ⚠️ **Persistence selective** — `persistent=0` volatile, `=1` flushé
  DB. Politique claire pour éviter écritures inutiles.

## 9. Recommandation finale

🔧 **Adapter et faire**, **après** CMANGOS.13 + .16 + .19 :

1. **Étape 1** : `WorldState` singleton + Get/Set/Subscribe + migration
   DB + opcodes update + tests.
2. **Étape 2** : `WorldStateExpression` lexer/parser/évaluateur +
   tests exhaustifs DSL.
3. **Étape 3** : `World` orchestrateur + tick hiérarchique multi-niveaux.
4. **Étape 4** : shutdown gracieux broadcast countdown + flush DB.
5. **Étape 5** : intégration content (BG state, arène score, events
   saisonniers).

À planifier après la chaîne P2 fondamentale (.13/.16/.19). Effort
moyen, ROI éditorial élevé.

---

*Audit du 2026-05-08. Mises à jour : —*
