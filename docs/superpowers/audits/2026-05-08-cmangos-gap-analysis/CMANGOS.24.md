# CMANGOS.24 — Reputation (faction template matrix)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.24_Reputation_faction_template_matrix.md](../../../../tickets/CMANGOS/CMANGOS.24_Reputation_faction_template_matrix.md)
> **Priorité** : P2 — gameplay essentiel
> **Cible** : shard

## 1. Statut implémentation

🟡 **Partiel** — schéma DB de réputation par scope existe (`character_reputation`,
`factions`), mais le pattern **faction template matrix** (relations
faction × faction → at_war/can_attack/can_assist) et **paliers calculés
on the fly** sont absents.

## 2. Preuves dans le code

**Existant :**
- [db/migrations/0011_character_reputation_history_legacy_bounties_prison.sql](../../../../db/migrations/0011_character_reputation_history_legacy_bounties_prison.sql) —
  table `character_reputation` avec scope (`global`/`faction`/`region`/
  `npc`) + colonne `global_reputation` sur `characters`
- [db/migrations/0040_factions.sql](../../../../db/migrations/0040_factions.sql) — table `factions` (Empire,
  Theocratie, Confrérie, etc.)
- (Aucun fichier `Reputation*.{h,cpp}` ni `FactionTemplate*.{h,cpp}`)

**Manquant (vs spec ticket) :**
- ❌ `engine/server/shard/reputation/` — dossier inexistant
- ❌ `Faction` struct (factionId, parentFactionId, parentSpilloverRatio,
  reputationCap, reputationFloor)
- ❌ `FactionTemplate` (matrice relations faction × faction avec
  at_war/can_attack/can_assist bitmasks)
- ❌ `ReputationManager` per-Player + `ReputationToRank()` (paliers
  calculés)
- ❌ Bitmask flags par faction (`AT_WAR`/`VISIBLE`/`INACTIVE`/`HIDDEN`)
- ❌ Spillover via faction parent (gain enfant remonte parent × ratio)
- ❌ Réplication delta (factions modifiées only)
- ❌ Tables DB `faction_template`, `character_reputation_flags`

## 3. Recouvrement milestones existantes

✅ **Couvert (partiellement)** — schéma de base livré (M40 factions ?
M11 ? livraison à clarifier). La logique cmangos (matrix + bitmasks +
spillover) reste à porter.

## 4. Écart par rapport à la spec CMANGOS

L'écart est **fonctionnel modéré** :

1. **Faction template matrix** — pattern élégant : un Unit a un
   `factionTemplateId`, lookup O(1) "puis-je attaquer Unit B ?" via
   matrix. Sans ça, le code de `CanAttack` est codé en dur par paire.
2. **Paliers calculés** — pas stockés en DB → pas de désync. Calcul
   simple (`if rep > 21000 → Revered`).
3. **Spillover parent** — élégant pour les "famille de factions"
   (gagner rep ville → gagner rep royaume).
4. **Bitmask flags** — `AT_WAR` toggle joueur (déclaration de guerre
   personnelle), `VISIBLE` (apparaît dans UI), etc.
5. **Réplication delta** — économise bande passante (pas envoyer 100
   factions à chaque update).

## 5. Effort estimé

**M-L** (3 PR) :
- PR 1 : `Faction` + `FactionTemplate` + migration DB matrix +
  intégration `CanAttack`/`CanAssist` (consommé par CMANGOS.11 Combat)
- PR 2 : `ReputationManager` per-Player + `ReputationToRank` + spillover
  parent + bitmask flags + tests
- PR 3 : réplication delta vers client + opcodes update

Wire-breaking probable (nouveaux opcodes update reputation client).
Migration DB.

## 6. Valeur joueur/serveur

**Élevée** — déblocant pour PvE faction-based (mobs hostiles selon
réputation joueur) et UX joueur (UI réputation, paliers,
récompenses par rank).

Critique pour content design (quêtes "atteindre Friendly avec faction
X" pour débloquer vendor).

## 7. Dépendances bloquantes

Le ticket dépend explicitement de :
- **CMANGOS.13 Database** — SQLStorage cache pour faction/template
- **CMANGOS.02 Entities** — `Unit` a `factionId`
- **CMANGOS.11 Combat** — `CanAttack(other)` consulte les flags

## 8. Risque / piège ⚠️

- ⚠️ **Migration DB** — table `faction_template` à créer +
  `character_reputation_flags`. Idempotent.
- ⚠️ **Wire-breaking** — opcodes update reputation client. Bump
  `kProtocolVersion` + lock-step.
- ⚠️ **Spillover cycles** — faction parent → enfant → parent =
  boucle. Détection au load (DFS).
- ⚠️ **Paliers calculés ≠ stockés** — éviter de cacher les paliers (ils
  changent dès que rep change). Recalculer à la demande.
- ⚠️ **Bitmask AT_WAR personnel** — toggle par joueur, distinct du
  state de la faction. Persistance per-character.
- ⚠️ **Doublon avec character_reputation existant** — schéma actuel a
  scope `faction`, à harmoniser avec template cmangos.
- ⚠️ **Replication delta** — bug : si client manque un update, désync.
  Ack obligatoire OU full-sync périodique.
- ⚠️ **`reputationCap`/`floor`** — clamping côté add. Tests pour
  débordement.

## 9. Recommandation finale

🔧 **Adapter et faire**, **après** CMANGOS.13 + .02 + .11 :

1. **Étape 0 (audit)** : auditer M11/M40 livraison + schéma actuel.
   Statuer fusion / extension du `character_reputation` actuel.
2. **Étape 1** : `Faction` + `FactionTemplate` + migration matrix.
3. **Étape 2** : intégration `CanAttack`/`CanAssist` (CMANGOS.11
   Combat consomme).
4. **Étape 3** : `ReputationManager` per-Player + paliers calculés +
   tests.
5. **Étape 4** : spillover parent + bitmask flags AT_WAR/etc.
6. **Étape 5** : réplication delta vers client + opcodes update.

À planifier dans la roadmap P2 après les fondamentaux. Effort moyen,
ROI joueur élevé.

---

*Audit du 2026-05-08. Mises à jour : —*
