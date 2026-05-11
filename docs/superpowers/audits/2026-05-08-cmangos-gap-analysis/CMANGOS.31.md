# CMANGOS.31 — GameEvents (seasonal / scheduler / cascading)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.31_GameEvents_seasonal_scheduler_cascading.md](../../../../tickets/CMANGOS/CMANGOS.31_GameEvents_seasonal_scheduler_cascading.md)
> **Priorité** : P3 — ajout à valeur
> **Cible** : shard

## 1. Statut implémentation

❌ **Absent** — pas de gestionnaire d'events saisonniers / scheduler /
events cascade. M100.25 (Season System and Time of Year) couvre **un
pan voisin** (saison/cycle annuel côté éditeur monde) mais distinct.

## 2. Preuves dans le code

**Existant (orthogonal) :**
- M100.25 — Season System and Time of Year (côté éditeur monde, cycle
  annuel pour textures/saisons)

**Manquant :**
- ❌ `engine/server/shard/events/`
- ❌ `GameEvent` struct + `GameEventMgr` (singleton scheduler)
- ❌ Filtrage spawns par `event_id` au load time
- ❌ Events cascade (parent/child déclenchement)
- ❌ Tables DB `game_event`, `game_event_creature`,
  `game_event_gameobject`

## 3. Recouvrement milestones existantes

❌ **Non couvert** — M100.25 est une saison **éditoriale visuelle**
(textures), pas un scheduler d'events gameplay.

## 4. Écart par rapport à la spec CMANGOS

100% absent. Sans GameEvents :
- Pas d'events saisonniers (Halloween, Noël)
- Pas de variantes spawn par saison (boss spécial)
- Pas de festivals à étapes (events cascade)

## 5. Effort estimé

**M** (2 PR) — scheduler + filtre spawns + migration DB. Pas wire-breaking.

## 6. Valeur joueur/serveur

**Moyenne** — feature live ops (engagement saisonnier). Pas critique
MVP.

## 7. Dépendances bloquantes

- **CMANGOS.13 Database** — SQLStorage scheduler
- **CMANGOS.19 Maps** — filtrage spawns par event_id

## 8. Risque / piège ⚠️

- ⚠️ Migration DB (3 tables) — idempotent
- ⚠️ Cascading cycles — détection au load
- ⚠️ Recurrence — RRULE-like format ou champs simples (start/end/period)
- ⚠️ Reload runtime — start/stop event live (GM command)
- ⚠️ Coordination M100.25 saison visuelle vs event gameplay

## 9. Recommandation finale

⏸ **Reporter** — feature live ops. À planifier quand le serveur
tourne en prod et qu'on veut animer la communauté avec events
saisonniers. Pas avant MVP.

---

*Audit du 2026-05-08. Mises à jour : —*
