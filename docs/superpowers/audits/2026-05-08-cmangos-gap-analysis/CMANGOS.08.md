# CMANGOS.08 — Arena (team / MMR / weekly / colisée)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.08_Arena_team_mmr_weekly_colisee.md](../../../../tickets/CMANGOS/CMANGOS.08_Arena_team_mmr_weekly_colisee.md)
> **Priorité** : P2 — gameplay essentiel
> **Cible** : master + shard

## 1. Statut implémentation

❌ **Absent** — aucune trace d'`ArenaTeam`, `ColosseumArena`, MMR, ladder.
Pas de table `arena_team` ni `arena_team_member`. Concept de **colisée
local LCDLLN** (spécifique au projet) entièrement à construire.

## 2. Preuves dans le code

**Existant (proches) :**
- (Aucun fichier engine spécifiquement arène.)
- `M00.2_Memory_allocateur_arenas` est un faux-ami : il s'agit d'un
  allocateur mémoire, pas du système gameplay arène.

**Manquant (vs spec ticket) :**
- ❌ `engine/server/arena/` — dossier inexistant côté master
- ❌ `ArenaTeam`, `ArenaTeamManager`, `ArenaTeamHandler`
- ❌ `ArenaMmrEngine` (calcul Glicko-2 ou Elo simplifié)
- ❌ `WeeklyMaintenance` cron pour distribution points
- ❌ `engine/server/shard/colosseum/` — dossier inexistant côté shard
- ❌ `ColosseumArena` (sous-classe BattleGround)
- ❌ `ColosseumEntrance` (PNJ portier)
- ❌ Opcodes `kOpcodeArenaTeam*` (Create/Invite/QueryStats/Disband)
- ❌ Tables `arena_team`, `arena_team_member`
- ❌ Migration DB

## 3. Recouvrement milestones existantes

❌ **Non couvert** — aucune milestone PvP/arène/colisée parmi M00-M44.

## 4. Écart par rapport à la spec CMANGOS

100% absent. La spec CMANGOS.08 est **bien cadrée pour LCDLLN spécifiquement** :
ticket adapté pour "colisée dans une ville, pas de file d'attente
cross-realm". Donc ce n'est pas un port littéral cmangos mais une
réinterprétation pour le concept LCDLLN.

Les éléments réutilisables tels quels :
- Pattern `ArenaTeam` (entité persistante) / `BattleGround` (instance) —
  séparation propre
- MMR Glicko-2 par équipe — algorithme connu, code réutilisable
- `WeeklyMaintenance` cron — pattern propre pour récompenses différées
- Distribution via mail (CMANGOS.18)

## 5. Effort estimé

**XL** (multi-sprints) — système large avec persistance, cron, opcodes,
sous-classe BattleGround (qui n'existe pas), zone bornée, IA portier,
distribution mail. À splitter en au moins 4 PR :
1. ArenaTeam + ArenaTeamManager + tables DB + opcodes Create/Invite
2. ArenaMmrEngine + tests
3. ColosseumArena + ColosseumEntrance (dépend de CMANGOS.10 BattleGround)
4. WeeklyMaintenance cron + distribution mail (dépend de CMANGOS.18)

## 6. Valeur joueur/serveur

**Élevée** — feature endgame visible joueur. Objectif progression
compétitive. Différenciant pour LCDLLN si bien fait.

Mais **pas critique court-terme** : le contenu PvE de base et le squelette
shard doivent venir d'abord. Sans monde vivant ni combat solide,
introduire l'arène est prématuré.

## 7. Dépendances bloquantes

Le ticket dépend explicitement de :
- **CMANGOS.06 Accounts** — pour identifier les membres d'équipe
- **CMANGOS.10 BattleGround** — `ColosseumArena` est sous-classe
  (bloquant)
- **CMANGOS.13 Database** — `SQLStorage` pour cache RAM des teams
- **CMANGOS.18 Mails** — distribution récompenses

→ **CMANGOS.08 dépend de 4 tickets P2 amont**. À planifier après ces 4.

## 8. Risque / piège ⚠️

- ⚠️ **Migration DB** — nouvelles tables `arena_team` + `arena_team_member`
  + (potentiellement) `arena_match_history`. Migration idempotente.
- ⚠️ **Wire-breaking** — plusieurs nouveaux opcodes
  (`kOpcodeArenaTeamCreate`, `kOpcodeArenaTeamInvite`, `kOpcodeArenaTeamQueryStats`,
  `kOpcodeArenaTeamDisband`) → bump `kProtocolVersion` + redéploiement
  master + client.
- ⚠️ **Redéploiement** — nouveaux handlers master (ArenaTeamHandler) +
  shard (Colosseum). Lock-step master+shard.
- ⚠️ **Cron WeeklyMaintenance** — risque d'exécution en double si plusieurs
  instances master. Idempotence requise (lock distribué ou seul master
  authoritative). À détailler.
- ⚠️ **Anti-cheat** — coordination entre joueurs hors arène (5 joueurs
  qui s'arrangent pour gonfler le MMR). Patterns détection (matchs
  trop courts, ratios suspects) à prévoir, hors scope ticket.
- ⚠️ **Spécificités colisée LCDLLN** — ce n'est PAS une simple file
  d'attente cross-realm. C'est une zone physique dans une ville. Implique
  spawn de PNJ, signalétique, gestion concurrence (1 seul match en cours
  ou plusieurs ?). À cadrer en design.
- ⚠️ **MMR initial** — choix valeur de départ (1500 typique). Calibration
  peut prendre des semaines de play. À surveiller post-launch.

## 9. Recommandation finale

⏸ **Reporter** — pas avant que la chaîne P1 + Combat (.11) + BattleGround
(.10) + Mails (.18) + Database (.13) soient stables. C'est un système
endgame qui cumule beaucoup de dépendances.

**Ordre d'attaque** (quand la maturité est là) :

1. **Étape 0** : design cadrage du colisée LCDLLN (zone physique, PNJ
   portier, plusieurs matchs simultanés ou un à la fois, formats 2v2/3v3/5v5).
   → Spec spécifique LCDLLN à écrire avant le code.
2. **Étape 1** : `ArenaTeam` + `ArenaTeamManager` + DB + opcodes Create/Invite
3. **Étape 2** : `ArenaMmrEngine` (Glicko-2 ou Elo, à arbitrer)
4. **Étape 3** : `ColosseumArena` (après CMANGOS.10 BattleGround livré)
5. **Étape 4** : `ColosseumEntrance` PNJ portier
6. **Étape 5** : `WeeklyMaintenance` cron + distribution mail

À planifier en **phase endgame**, pas en fondation. Garder la fiche pour
mémoire et arbitrage prio long-terme.

---

*Audit du 2026-05-08. Mises à jour : —*
