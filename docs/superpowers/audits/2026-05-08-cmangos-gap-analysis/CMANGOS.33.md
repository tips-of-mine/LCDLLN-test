# CMANGOS.33 — LFG (queue / role / matchmaking)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.33_LFG_queue_role_matchmaking.md](../../../../tickets/CMANGOS/CMANGOS.33_LFG_queue_role_matchmaking.md)
> **Priorité** : P3 — ajout à valeur
> **Cible** : master

## 1. Statut implémentation

❌ **Absent** — pas de système Looking-For-Group, pas de matchmaking
par rôles.

## 2. Preuves dans le code

**Manquant :**
- ❌ `engine/server/lfg/`
- ❌ `LFGQueue` + `LFGMgr`
- ❌ State machine joueur (Idle/Queued/Proposal/Boot/InDungeon)
- ❌ Matching par rôles requis
- ❌ Timeout proposals
- ❌ Migration DB

## 3. Recouvrement milestones existantes

❌ **Non couvert**.

## 4. Écart par rapport à la spec CMANGOS

100% absent.

## 5. Effort estimé

**M-L** (2-3 PR) — Queue + Mgr + state machine + matching algo +
timeouts + opcodes. Wire-breaking probable.

## 6. Valeur joueur/serveur

**Moyenne** — feature endgame (quand donjons matures). Réduit friction
de groupe.

## 7. Dépendances bloquantes

- **CMANGOS.13 Database**
- **CMANGOS.15 Groups** — création groupe au match formé
- **CMANGOS.10 BattleGround** + **.19 Maps** (DungeonMap) si LFG dungeon
- **CMANGOS.07 AI** + **.11 Combat** matures (sinon rien à matcher pour)

## 8. Risque / piège ⚠️

- ⚠️ Wire-breaking opcodes LFG. Bump.
- ⚠️ Matching algo optimisation : 1000 joueurs en queue × 50 dungeons =
  cycle complet rapide requis. O(N×M) inacceptable, indexer.
- ⚠️ Timeout proposals — race conditions. Locks fins.
- ⚠️ State machine bugs = joueurs fantômes. Tests exhaustifs transitions.
- ⚠️ Migration DB optionnelle (queue volatile suffit pour V1).

## 9. Recommandation finale

⏸ **Reporter** — pas avant que les donjons existent (CMANGOS.19
DungeonMap + CMANGOS.07 AI mature). Feature endgame.

À planifier vers fin de la roadmap P3, après .15 Groups + .19 Maps.

---

*Audit du 2026-05-08. Mises à jour : —*
