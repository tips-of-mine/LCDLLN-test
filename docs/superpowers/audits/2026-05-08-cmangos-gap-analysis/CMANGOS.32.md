# CMANGOS.32 — GMTickets (support in-game)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.32_GMTickets_support_in_game.md](../../../../tickets/CMANGOS/CMANGOS.32_GMTickets_support_in_game.md)
> **Priorité** : P3 — ajout à valeur
> **Cible** : master

## 1. Statut implémentation

❌ **Absent** — `ShardTicketHandler` existant est pour l'auth shard
(handshake), pas pour les tickets de support in-game.

## 2. Preuves dans le code

**Existant (orthogonal) :**
- [engine/server/ShardTicketHandler.h](../../../../engine/server/ShardTicketHandler.h) — auth ticket shard, sans
  rapport GM support

**Manquant :**
- ❌ `engine/server/gmtickets/` — dossier inexistant
- ❌ `GMTicket` struct + `GMTicketMgr` + `GMTicketHandler`
- ❌ Migration DB `gm_tickets` (status open/in_progress/closed)
- ❌ Opcodes Submit/Update/Close
- ❌ Notification asymétrique (joueur voit son ticket, GM voit la file)

## 3. Recouvrement milestones existantes

❌ **Non couvert** — pas de milestone GM tickets in-game.

## 4. Écart par rapport à la spec CMANGOS

100% absent. Pas de canal de support in-game pour les joueurs.

## 5. Effort estimé

**S-M** (1-2 PR) — système simple : 1 table + 3 classes + opcodes.

## 6. Valeur joueur/serveur

**Moyenne** — qualité de service. Critique post-launch (joueurs ont
besoin de signaler bugs/cheats). Pré-launch optionnel.

## 7. Dépendances bloquantes

- **CMANGOS.06 Accounts** — flag GM (rôle Moderator/GM)
- **CMANGOS.13 Database** — persistance

## 8. Risque / piège ⚠️

- ⚠️ Migration DB simple (1 table) — idempotent
- ⚠️ Wire-breaking — opcodes Submit/Update/Close. Bump.
- ⚠️ Anti-spam — cap par joueur (1 ticket actif à la fois ?)
- ⚠️ Notification GM online seulement — fallback log si aucun GM online
- ⚠️ Audit trail — qui a répondu / actions prises

## 9. Recommandation finale

⏸ **Reporter** — pas critique pré-launch. À faire dès que serveur
en open beta. Effort raisonnable, valeur ops élevée.

À planifier après CMANGOS.06 Accounts (rôles GM) livré.

---

*Audit du 2026-05-08. Mises à jour : —*
