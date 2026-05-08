# CMANGOS.15 — Groups (GroupRef / loot rules / persistence)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.15_Groups_groupref_loot_rules_master.md](../../../../tickets/CMANGOS/CMANGOS.15_Groups_groupref_loot_rules_master.md)
> **Priorité** : P2 — gameplay essentiel
> **Cible** : master

## 1. Statut implémentation

❌ **Absent** — pas de classe `Group`/`Raid` côté master, pas de
`GroupReference` intrusive, pas de `GroupManager`. M32.2 milestone
existe pour ça mais semble non livré (à vérifier).

## 2. Preuves dans le code

**Existant :**
- M32.2 — Party system formation + loot rules (milestone existante,
  livraison à vérifier)
- M32.3 — Guild system (potentiellement déjà livré ou en cours)
- [db/migrations/0003_guilds.sql](../../../../db/migrations/0003_guilds.sql) — schéma guilds (≠ party/raid)
- (Aucun fichier `Group*.{h,cpp}` détecté dans `engine/server/`)

**Manquant (vs spec ticket) :**
- ❌ `engine/server/groups/` — dossier inexistant
- ❌ `Group` class (Type=Party/Raid, LootMethod, members, leader)
- ❌ `GroupReference<T>` templated intrusive list
- ❌ `GroupRefManager` (cleanup auto à la dissolution)
- ❌ `GroupManager` singleton (CreateGroup/DisbandGroup/GetForAccount)
- ❌ `GroupHandler` opcodes (Invite/Accept/Decline/Kick/Leave/SetLeader/
  ConvertToRaid/SetLootMethod)
- ❌ Tables `persistent_group` + `persistent_group_member`
- ❌ Loot rules pluggables (FreeForAll, RoundRobin, MasterLooter,
  NeedBeforeGreed)
- ❌ Migration DB

## 3. Recouvrement milestones existantes

✅ **Couvert (en théorie)** — M32.2 (Party system formation + loot rules)
est explicitement le ticket LCDLLN qui couvre le sujet. À auditer si
livré ou pending.

## 4. Écart par rapport à la spec CMANGOS

L'écart **fonctionnel** dépend de l'état de M32.2 (vérification manuelle
recommandée). Les patterns spécifiques cmangos :

1. **`GroupReference` intrusive list** — pattern intéressant : un Player
   porte une `GroupReference<Group>` qui s'invalide auto à la dissolution,
   pas de dangling pointer. Distinct de `shared_ptr` (pas d'atomic
   refcount).
2. **Persistance partielle** — raid permanent en DB, party 5-man
   in-memory. Pattern simple, économise des écritures.
3. **Cross-shard ready** — Group master-side, position members shard.
   Bon pattern pour le futur si LCDLLN devient multi-shard avec partage
   de groupe.
4. **Loot rules pluggables** — stratégie pattern, ajouter une règle =
   ajouter une classe.

## 5. Effort estimé

**M-L** (3 PR) :
- PR 1 : `Group` + `GroupReference` + `GroupManager` + tests
- PR 2 : opcodes `GroupHandler` + tests roundtrip + persistence DB
- PR 3 : loot rules pluggables (stratégies + tests par règle)

Pas de wire-breaking si on alloue les opcodes proprement (pas de
modification d'opcodes existants).

## 6. Valeur joueur/serveur

**Élevée** — déblocant pour PvE de groupe (donjons, quêtes coop) et raid.
Valeur joueur visible et attendue (formation, loot, leadership).

Critique pour CMANGOS.17 Loot (rules de groupe) et CMANGOS.10
BattleGround (formation par groupe).

## 7. Dépendances bloquantes

Le ticket dépend explicitement de :
- **CMANGOS.06 Accounts** — pour identifier les membres
- **CMANGOS.13 Database** — pour persistance raid permanent
- **CMANGOS.17 Loot** — pour appliquer les rules à un drop

## 8. Risque / piège ⚠️

- ⚠️ **Migration DB** — tables `persistent_group` +
  `persistent_group_member`. Idempotent.
- ⚠️ **Wire-breaking** — plusieurs nouveaux opcodes (Invite/Accept/...).
  Bump `kProtocolVersion` + lock-step master+client.
- ⚠️ **GroupReference cleanup** — bug intrusive list = leaks ou crash.
  Tests exhaustifs (membre déconnecte plein milieu, leader meurt,
  groupe dissout pendant que membre est en zone, etc.).
- ⚠️ **MasterLooter exploit** — un MasterLooter peut tout garder. Anti-
  scam : log de tous les drops + revue post-raid possible. Hors scope
  ticket mais à prévoir.
- ⚠️ **NeedBeforeGreed timer** — fenêtre de roll. Si trop court, joueur
  AFK rate. Si trop long, gameplay lent.
- ⚠️ **ConvertToRaid one-way** — un raid ne devient pas Party. Décision
  délibérée cmangos. À documenter pour les joueurs.
- ⚠️ **Cross-shard groupe** — si membres sur shards différents, position
  + chat de groupe à coordonner. Hors scope court-terme mais à designer
  pour ne pas se piéger.
- ⚠️ **Doublon avec M32.2** — risque réimplémentation. Audit M32.2
  obligatoire.

## 9. Recommandation finale

🔧 **Adapter et faire**, **après** audit de M32.2 :

1. **Étape 0 (audit)** : examiner M32.2 livraison/branche/PR. Si livré,
   mesurer écart vs spec cmangos. Si pending, cadrer la suite.
2. **Étape 1** : `Group` + `GroupReference` + `GroupManager` minimal
   (Party only, in-memory, sans persistence).
3. **Étape 2** : opcodes Invite/Accept/Decline/Kick/Leave + tests E2E.
4. **Étape 3** : ConvertToRaid + persistence raid + migration DB.
5. **Étape 4** : loot rules (FreeForAll par défaut, autres comme
   stratégies pluggables).
6. **Étape 5** : intégration CMANGOS.17 Loot quand livré.

À planifier après CMANGOS.06 Accounts (pré-requis) et **avant**
CMANGOS.17 Loot (qui consomme les rules de groupe).

---

*Audit du 2026-05-08. Mises à jour : —*
