# CMANGOS.07 — AI (creature registry / EventAI DB-driven)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.07_AI_creature_registry_eventai_dbdriven.md](../../../../tickets/CMANGOS/CMANGOS.07_AI_creature_registry_eventai_dbdriven.md)
> **Priorité** : P2 — gameplay essentiel
> **Cible** : shard

## 1. Statut implémentation

❌ **Absent** — aucune trace de framework IA pour créatures (`CreatureAI`,
`EventAI`, `CreatureAIRegistry`, `CreatureAISelector`). Pas de table
`creature_ai_scripts`, pas de colonne `ai_name`.

## 2. Preuves dans le code

**Existant (architecture alternative LCDLLN) :**
- [engine/server/SpawnerRuntime.h](../../../../engine/server/SpawnerRuntime.h) — spawn data-driven JSON, mais
  uniquement définitions `archetypeId`/position/leash, pas de comportement
  IA
- M15.2 (`Spawners respawn + leash + activation par présence joueurs`) couvre
  le cycle de vie spawn/respawn mais pas l'IA en combat

**Manquant (vs spec ticket) :**
- ❌ `engine/server/shard/ai/` — dossier inexistant
- ❌ `CreatureAI` interface abstraite (méthodes virtuelles : `OnAggro`,
  `OnDamageTaken`, `OnDamageDealt`, `OnEvade`, `OnDeath`, `OnSpellHit`,
  `Update`)
- ❌ `BaseAI` (no-op default IA agressive melee)
- ❌ `EventAI` interpréteur de scripts DB
- ❌ `PlayerAI` (charme/fear)
- ❌ `CreatureAIRegistry` (factory `aiName → ctor`)
- ❌ `CreatureAISelector` (sélection par AIName / scripts / fallback)
- ❌ `ScriptDevAI` (couche C++ pour boss complexes)
- ❌ Migration DB : colonne `ai_name` sur `creature_template`,
  table `creature_ai_scripts`
- ❌ Config `ai.default_aggro_range_m`, `ai.evade_distance_m`,
  `ai.evade_full_heal`, `ai.event_ai_enabled`
- ❌ Aucune `creature_template` ni `creature_ai_scripts` dans `db/migrations/`

## 3. Recouvrement milestones existantes

❌ **Non couvert** — pas de milestone IA. M15.2 spawners couvre uniquement
le respawn et le leash, pas la sémantique d'un combat. Il existe un vide
fonctionnel total pour le comportement créature.

## 4. Écart par rapport à la spec CMANGOS

100% absent. Le ticket couvre 4 piliers :
1. Registry/Selector pattern — pattern d'extensibilité
2. EventAI DB-driven — moteur générique pour scripts simples
3. PlayerAI — pour charme/fear
4. ScriptDevAI séparé — isolation contenu/moteur

Sans ce framework, **toute Creature spawned reste passive** (pas d'aggro,
pas de combat, pas d'esquive). C'est un déblocant absolu pour CMANGOS.11
Combat (rien à attaquer si rien ne réagit).

## 5. Effort estimé

**L** (1 sprint complet) :
- Interface + BaseAI + Registry + Selector + tests
- EventAI interpréteur (events + actions + cooldowns + chance)
- Migration DB `creature_template.ai_name` + `creature_ai_scripts`
- Câblage avec MotionMaster (CMANGOS.20) et Combat (CMANGOS.11)
- Premier set d'IA stockée (BaseAI + 1-2 EventAI scripts d'exemple)

Pas de wire-breaking côté protocole (l'IA est server-only), mais
nouvelles tables DB → migration. Redéploiement shard.

## 6. Valeur joueur/serveur

**Élevée → Critique** — déblocant pour le PvE. Sans IA, le monde est mort.
Premier PNJ avec comportement = première démo "monde vivant" jouable.

Critique aussi pour la **production de contenu** : sans `EventAI`
DB-driven, chaque boss demande du C++ + recompil + redéploiement.
Avec EventAI, un designer peut scripter un boss en SQL en 10 minutes.

## 7. Dépendances bloquantes

Le ticket dépend explicitement de :
- **CMANGOS.02 Entities** — `Creature` porte un `CreatureAI*`. Sans hiérarchie
  ou équivalent, point d'attache à inventer (peut être une `unordered_map<EntityId, AI*>`).
- **CMANGOS.04 Movement** — l'IA pousse `MoveSplineInit` au mouvement.
  Sans splines, l'IA ne peut pas faire bouger les Creatures de façon crédible.
- **CMANGOS.11 Combat** — l'IA réagit aux events `OnAggro`, `OnDamage*`.
- **CMANGOS.20 MotionGenerators** — l'IA pousse/pop des générateurs sur
  `MotionMaster`.

→ **CMANGOS.07 dépend de 4 tickets P1/P2 amont**. À planifier après ces 4.

## 8. Risque / piège ⚠️

- ⚠️ **Migration DB** — nouvelle colonne `ai_name VARCHAR(64)` + nouvelle
  table `creature_ai_scripts`. Migration idempotente requise.
- ⚠️ **Redéploiement** — nouveau handler shard (lecture des scripts au
  boot, dispatch IA au tick).
- ⚠️ **Boucles infinies dans EventAI** — un script qui déclenche un event
  qui re-déclenche le script peut créer une boucle. Mécanisme de
  cooldown obligatoire (`repeat_min_ms`/`repeat_max_ms`).
- ⚠️ **Évade infini** — si `evade_distance_m` est mal calibré, mob revient
  en boucle. À profiler.
- ⚠️ **Hot-reload des scripts** — souhaitable pour productivité designer
  (cf. CMANGOS.14 DBScripts). Si non livré, redémarrage shard à chaque
  modif.
- ⚠️ **Coordination ScriptDevAI / EventAI** — risque de doublon
  (un boss scripté en C++ ET en EventAI). Convention claire :
  `creature.AIName != "" XOR HasEventAIScript()`.
- ⚠️ **Charme/fear via PlayerAI** — switch dynamique CreatureAI ↔ PlayerAI
  sur un Player. Risque de fuites si pas géré proprement (ownership).
- Pas de wire-breaking opcode côté protocole.

## 9. Recommandation finale

🔧 **Adapter et faire**, **après** la chaîne P1 (CMANGOS.02 Entities,
.04 Movement, .03 Grids, .05 vmap) et **avec/après** CMANGOS.11 Combat
et CMANGOS.20 MotionGenerators.

**Plan d'attaque** :

1. **Étape 0** : valider que les pré-requis sont livrés (Movement +
   MotionMaster + Combat events).
2. **Étape 1** : implémenter `CreatureAI` interface + `BaseAI` no-op
   agressive. Premier mob réagit au pathing joueur dans rayon.
3. **Étape 2** : implémenter `CreatureAIRegistry` + `CreatureAISelector`.
4. **Étape 3** : migration DB + `EventAI` interpréteur (les 7 events de
   référence + 5-10 actions de base : `CastSpell`, `Say`, `MoveRandom`,
   etc.).
5. **Étape 4** : tests + premier boss scripté en EventAI (smoke test
   "boss à 50% HP enrage").
6. **Étape 5** (différé) : `PlayerAI` pour charme/fear (livrable séparé,
   après que les sorts charme/fear existent côté CMANGOS.26 Spells).
7. **Étape 6** (différé) : structure `ScriptDevAI` pour boss C++ complexes
   (livrable séparé, quand le 1er boss complexe est demandé).

**À ne pas attaquer avant** que la chaîne P1 + Combat soit stable, sinon
on développe contre une cible mouvante.

---

*Audit du 2026-05-08. Mises à jour : —*
