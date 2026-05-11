# CMANGOS.21 — Guilds (schema / permissions / bank)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.21_Guilds_schema_permissions_bank.md](../../../../tickets/CMANGOS/CMANGOS.21_Guilds_schema_permissions_bank.md)
> **Priorité** : P2 — gameplay essentiel (vie sociale)
> **Cible** : master

## 1. Statut implémentation

🟡 **Partiel** — schéma DB de base livré (M32.3), tables `guilds` +
`guild_members` + `guild_ranks` avec permissions bitfield. Mais le
système C++ (`Guild`/`GuildMgr`/`GuildHandler`), la banque multi-onglets,
et l'event log sont absents.

## 2. Preuves dans le code

**Existant :**
- [db/migrations/0003_guilds.sql](../../../../db/migrations/0003_guilds.sql) — `guilds`, `guild_members`,
  `guild_ranks` (permission bitfield, rank_id check ≤ 3)
- M32.3 milestone — Guild system creation + roster + ranks
- (Aucun fichier `Guild*.{h,cpp}` ni `GuildMgr` détecté dans `engine/`)

**Manquant (vs spec ticket) :**
- ❌ `engine/server/guilds/` — dossier inexistant
- ❌ `Guild` class C++ (AddMember/RemoveMember/ChangeRank/DepositBank/
  WithdrawBank/LogEvent/HasPermission)
- ❌ `GuildRank` struct + `GuildPermission` enum bitmask
- ❌ `GuildMgr` singleton (Load au boot)
- ❌ `GuildHandler` opcodes
- ❌ `GuildEventLog` circulaire (rotation par taille max)
- ❌ Tables `guild_bank_tab`, `guild_bank_item`, `guild_eventlog`
- ❌ Banque multi-onglets avec permissions par onglet par rank
- ❌ Migration DB pour banque + eventlog

## 3. Recouvrement milestones existantes

✅ **Couvert (partiellement)** — M32.3 a livré les tables de base. Le code
C++ et les features avancées (banque, eventlog) restent à livrer.

## 4. Écart par rapport à la spec CMANGOS

L'écart est modéré côté schéma (les tables core existent) mais important
côté **logique métier C++** et **features avancées** :

1. **Banque multi-onglets** — flexibilité forte (onglets dédiés par
   classe d'item, permissions par onglet par rank). Pattern propre,
   peu de complexité.
2. **Event log circulaire** — rotation par taille max en DB, audit +
   UI client. Pattern simple mais utile.
3. **Permissions bitfield** — déjà présent côté schéma, à exposer côté
   C++.
4. **GuildMgr load au boot** — acceptable pour ~100k guildes max.

## 5. Effort estimé

**M-L** (3 PR) :
- PR 1 : `Guild`/`GuildRank` C++ + `GuildMgr` Load + tests CRUD basique
- PR 2 : `GuildHandler` opcodes (Invite/Accept/Promote/Demote/MOTD/
  Charter)
- PR 3 : banque multi-onglets + event log circulaire + migrations
  associées

Wire-breaking probable (nouveaux opcodes guild). Migration DB pour
banque + eventlog.

## 6. Valeur joueur/serveur

**Élevée** — feature visible joueur. Critique pour la vie sociale
durable. Banque guilde = feature attendue (stockage commun, prêts).

## 7. Dépendances bloquantes

Le ticket dépend explicitement de :
- **CMANGOS.06 Accounts** — pour identifier les membres
- **CMANGOS.13 Database** — persistance + cache
- **CMANGOS.18 Mails** — invitations / messages broadcast guilde
- Économie (gold/items) — déjà en place LCDLLN

## 8. Risque / piège ⚠️

- ⚠️ **Migration DB** — tables `guild_bank_tab`, `guild_bank_item`,
  `guild_eventlog`. Idempotent.
- ⚠️ **Wire-breaking** — plusieurs nouveaux opcodes guild. Bump
  `kProtocolVersion` + lock-step master+client.
- ⚠️ **Banque transactions** — déposit/withdraw items = ops atomiques.
  Si crash entre déposer et débiter inventaire = perte item.
  Pattern transaction DB.
- ⚠️ **Event log volumétrie** — 1000 guilds × 100 events/jour = 100k/jour
  insert. Rotation indexée par `guild_id` + `event_ts` obligatoire.
- ⚠️ **Anti-fraude banque** — log de tous les withdraw + audit.
  Permissions par rank par onglet permet de limiter mais pas
  d'empêcher. Fraude entre membres possible.
- ⚠️ **Permissions par onglet** — ajout d'une dimension
  (rank × tab × permission). Calcul `HasPermission(rank, tab, perm)`
  testable.
- ⚠️ **Doublon avec M32.3 livraison** — auditer ce qui existe avant
  code.

## 9. Recommandation finale

🔧 **Adapter et faire**, **après** audit M32.3 livraison et **après**
CMANGOS.18 Mails (pré-requis pour invitations) :

1. **Étape 0 (audit)** : examiner M32.3 livraison. Si livré, mesurer
   écart vs spec cmangos. Probable : tables OK, logic C++ partielle.
2. **Étape 1** : `Guild` + `GuildMgr` C++ + Load au boot + tests.
3. **Étape 2** : `GuildHandler` opcodes (Invite/Accept/Decline/Kick/
   Promote/Demote/MOTD/Charter/Leave).
4. **Étape 3** : event log circulaire + migration DB.
5. **Étape 4** : banque multi-onglets + permissions par tab par rank +
   migration DB.
6. **Étape 5** : intégration mail (invitation par mail si offline).

À planifier en parallèle des autres P2 master (CMANGOS.18 Mails,
CMANGOS.25 Social). Effort moyen, ROI joueur élevé.

---

*Audit du 2026-05-08. Mises à jour : —*
