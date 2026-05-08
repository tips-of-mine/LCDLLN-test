# CMANGOS.11 — Combat (CombatManager / ThreatManager / HostileRef)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.11_Combat_threat_hostile_decomposition.md](../../../../tickets/CMANGOS/CMANGOS.11_Combat_threat_hostile_decomposition.md)
> **Priorité** : P2 — gameplay essentiel
> **Cible** : shard

## 1. Statut implémentation

❌ **Absent** (pour la décomposition CombatManager/ThreatManager/HostileRefManager).
M14 milestones traitent **partiellement** la validation de combat et
l'aggro AI states, mais sans la décomposition canonique cmangos.

## 2. Preuves dans le code

**Existant (concepts liés, architecture différente) :**
- M14.1 — Combat serveur validation damage events
- M14.2 — Aggro AI states (Idle/Patrol/Aggro/Return)
- [engine/gameplay/SkillSystem.h](../../../../engine/gameplay/SkillSystem.h) — système de compétences (peut produire
  des dégâts)
- [engine/gameplay/StatusEffect.h](../../../../engine/gameplay/StatusEffect.h) — buffs/debuffs

(Verdict provisoire — vérification manuelle recommandée pour mesurer
exactement ce que M14.1 a livré : tests, handlers, validation
serveur des dégâts.)

**Manquant (vs spec ticket) :**
- ❌ `engine/server/shard/combat/` — dossier inexistant
- ❌ `CombatManager` per-Unit (state machine
  `OutOfCombat → Combat → LeavingCombat → OutOfCombat` avec timer 5s)
- ❌ `ThreatManager` per-Creature (table de threat ordonnée)
- ❌ `HostileRef` (référence bidirectionnelle intrusive)
- ❌ `HostileRefManager` (cleanup O(1) à la mort d'un acteur)
- ❌ `DuelHandler` (mode 1v1 opt-in zone bornée + timer + win 1HP)
- ❌ Opcodes duel (`SMSG_DUEL_REQUEST`, `SMSG_DUEL_COMPLETE`)
- ❌ Config `combat.leaving_combat_timeout_sec`,
  `combat.duel_default_duration_sec`, etc.

## 3. Recouvrement milestones existantes

✅ **Couvert (partiellement)** — M14.1 + M14.2 livrent une approche du
combat et de l'aggro IA, mais sans la décomposition fine cmangos
(CombatManager/ThreatManager séparés, HostileRefManager bidirectionnel).

À arbitrer : le pattern cmangos est-il plus adapté que ce qui existe
(M14) ? Le bénéfice principal de la décomposition cmangos est le cleanup
**O(1) bidirectionnel** des références aggro à la mort d'un acteur —
critique pour éviter les "fantômes d'aggro" sur PNJ orphelins.

## 4. Écart par rapport à la spec CMANGOS

L'écart **fonctionnel** dépend de ce que M14.1/14.2 ont livré exactement
(évaluation provisoire, vérification manuelle recommandée). L'écart
**architectural** est important :

1. **Décomposition CombatManager / ThreatManager / HostileRefManager** —
   séparation propre des responsabilités (être en combat ≠ ordonner les
   cibles ≠ référencer les hostilités).
2. **HostileRefManager bidirectionnel** — anti-fantôme via intrusive list,
   bénéfice O(1) cleanup. Pattern moins évident que la décomposition.
3. **DuelHandler opt-in** — bon précédent pour mode arène 1v1 amical
   ville LCDLLN.

## 5. Effort estimé

**M-L** (2-4 PR) selon ce qui existe déjà dans M14 :
- Si M14 a un système combat solide, on peut **enrichir** avec
  ThreatManager + HostileRefManager en M.
- Si M14 est minimal, refonte complète en L.

Pas de wire-breaking pour Combat/Threat/HostileRef (server-only). Duel
introduit 2 opcodes + bump `kProtocolVersion`.

## 6. Valeur joueur/serveur

**Élevée → Critique** — sans système combat solide, pas de PvE crédible.
Les "fantômes d'aggro" sont un bug visible joueur (mob qui ne lâche
pas, stuck en combat infini) — corrige la classe entière de bugs.

Duel = feature visible joueur attendue (ville, social, e-sport
informel).

## 7. Dépendances bloquantes

Le ticket dépend explicitement de :
- **CMANGOS.02 Entities** — `Unit` porte `CombatManager`, `Creature` aussi
  `ThreatManager`. Sans hiérarchie, point d'attache à inventer
  (peut être `unordered_map<EntityId, CombatState*>`).
- **CMANGOS.03 Grids** — `MessageDistDeliverer` pour broadcast "X est en
  combat".
- **CMANGOS.07 AI** — l'IA réagit aux events combat (relation symétrique
  : Combat fournit les events, AI les consomme).

## 8. Risque / piège ⚠️

- ⚠️ **Doublon avec M14.1/14.2** — risque de réimplémenter ce qui existe.
  Audit M14 obligatoire avant code, sinon dette + bugs de divergence.
- ⚠️ **Cleanup bidirectionnel à la mort** — implémentation intrusive list
  délicate. Bug subtil = leaks ou crash. Tests exhaustifs requis (A→B→C
  meurt en chaîne, A et B doivent voir leurs refs cleanées).
- ⚠️ **Threat decay** — config `threat_decay_per_sec=0` par défaut. Mais
  si activé, recalibration à chaque dégât = O(N log N). Surveillance
  perf si beaucoup de mobs en combat.
- ⚠️ **Wire-breaking** (duel uniquement) — 2 opcodes
  `SMSG_DUEL_REQUEST`/`COMPLETE` + bump `kProtocolVersion`. Lock-step
  shard+client (master pas concerné si duel shard-only).
- ⚠️ **Sortie de zone duel** — détection robuste (pas trop sensible
  sinon faux forfaits, pas trop lâche sinon exploit). Tests parametriques.
- ⚠️ **Concurrence combat / AI / status effects** — interactions multiples
  (`OnDamageTaken` peut déclencher proc qui déclenche `OnDamageDealt`...).
  Risque de ré-entrance / récursion. Patterns à cadrer.
- ⚠️ **Duel HP=1 (vs HP=0)** — la victoire est à 1HP pour ne pas tuer
  vraiment. Implique un check **avant** le dégât final. Bug subtil sur
  one-shot critique.

## 9. Recommandation finale

🔧 **Adapter et faire** :

1. **Étape 0 (audit)** : examiner M14.1/14.2 pour mesurer ce qui existe.
   - Si M14 a CombatManager équivalent → enrichir avec
     ThreatManager + HostileRefManager + DuelHandler.
   - Si M14 est minimal → refonte avec décomposition cmangos.
2. **Étape 1** : implémenter `CombatManager` state machine + tests
   (entrée combat, leaving timer, sortie OOC).
3. **Étape 2** : implémenter `HostileRef` + `HostileRefManager`
   bidirectionnel. **Tests exhaustifs cleanup** (mort en chaîne).
4. **Étape 3** : implémenter `ThreatManager` + ordering. Câblage
   `BaseAI::SelectTarget` (CMANGOS.07).
5. **Étape 4** : implémenter `DuelHandler` séparé + opcodes + tests.
6. **Étape 5** : smoke test "1 PNJ aggro 3 joueurs, threat ordering
   cohérent, mort PNJ → tous les flags combat des 3 joueurs nettoyés
   en O(1)".

À planifier après la chaîne P1 (Entities/Grids/Movement/vmap) et
**en parallèle** de CMANGOS.07 AI (l'AI consomme les events combat ;
itération parallèle est le pattern naturel).

---

*Audit du 2026-05-08. Mises à jour : —*
