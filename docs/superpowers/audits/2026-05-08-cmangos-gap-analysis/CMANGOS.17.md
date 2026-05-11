# CMANGOS.17 — Loot (templates / groups / reference)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.17_Loot_templates_groups_reference.md](../../../../tickets/CMANGOS/CMANGOS.17_Loot_templates_groups_reference.md)
> **Priorité** : P2 — gameplay essentiel
> **Cible** : shard

## 1. Statut implémentation

🟡 **Partiel** — `GatheringSystem` couvre le loot des **ressources** (mining,
herbalism — M36.1) avec `ResourceNodeLootEntry`. Mais le système de loot
**général** pour creatures/gameobjects/fishing/pickpocketing avec
templates DB, loot groups, reference loot est absent.

## 2. Preuves dans le code

**Existant :**
- [engine/server/GatheringSystem.h](../../../../engine/server/GatheringSystem.h) + `.cpp` — `ResourceNodeLootEntry`
  (`itemId`, `minQty`, `maxQty`) + framework de loot pour resource nodes
  (gathering)
- M14.3 — Loot bag pickup inventory updates (milestone existante,
  livraison à vérifier)
- M36.1 — Resource nodes (gathering) probablement source de
  GatheringSystem

**Manquant (vs spec ticket) :**
- ❌ `engine/server/shard/loot/` — dossier inexistant
- ❌ `LootEntry` struct générique (`itemId<0` = reference, `groupId`,
  `chance`, `min/maxCount`, `conditionId`)
- ❌ `LootTemplate` collection indexée par entry
- ❌ `LootMgr` singleton avec Load + Generate
- ❌ `Loot` pile active (items + qui a vu / qui a pris)
- ❌ `LootRoll` (Need/Greed/Pass/Disenchant côté groupe)
- ❌ Tables DB `creature_loottemplate`, `gameobject_loottemplate`,
  `fishing_loottemplate`, `pickpocketing_loottemplate`,
  `reference_loot_template`
- ❌ Loot groups (somme chances = 100% dans un group → exactement 1 drop)
- ❌ Reference loot (`itemId<0` → autre template)
- ❌ Conditions par drop (consomme CMANGOS.16)
- ❌ Round-robin / need-greed runtime (consomme CMANGOS.15)

## 3. Recouvrement milestones existantes

✅ **Couvert (partiellement, autre périmètre)** — M14.3 et M36.1 livrent
le **gathering** + **bag pickup**, mais pas le système templates général.

## 4. Écart par rapport à la spec CMANGOS

L'écart est **fonctionnel important** : pas de loot creature, pas de
loot gameobject (coffres), pas de fishing/pickpocketing. Les patterns
spécifiques cmangos :

1. **Schéma générique unique** réutilisé pour 4 types — DRY parfait
2. **Loot groups** — pattern subtil mais puissant (loot garanti vs bonus)
3. **Reference loot** — DRY pour boss similaires
4. **Orthogonalité données / règles** — template = données,
   GroupLootMethod = règles

Sans ce système, chaque type de loot demande son propre framework
(ce qui est déjà partiellement le cas avec GatheringSystem séparé).

## 5. Effort estimé

**L** (1 sprint) :
- Migration DB (4 tables loot + 1 reference)
- LootEntry + LootTemplate + LootMgr + tests
- Reference loot resolution (récursivité contrôlée)
- Loot groups resolution (somme chances)
- Conditions par drop (intègre CMANGOS.16)
- LootRoll Need/Greed/Pass (intègre CMANGOS.15 group rules)
- Smoke test : un mob a une template avec 3 items + 1 group
  garanti + 1 reference → généré conformément.

Pas de wire-breaking côté protocole **dans le ticket lui-même** (les
opcodes loot client viendront avec les rolls Need/Greed).

## 6. Valeur joueur/serveur

**Critique** — déblocant pour **tout drop PvE**. Un MMO sans loot, c'est
pas un MMO. Aussi déblocant pour quêtes (objectifs "drop X items"),
crafting (matériaux drop).

## 7. Dépendances bloquantes

Le ticket dépend explicitement de :
- **CMANGOS.13 Database** — `SQLStorage` cache pour les templates au
  boot
- **CMANGOS.16 Globals/Conditions** — `condition_id` par drop
- **CMANGOS.15 Groups** — loot rules par groupe (Need/Greed/MasterLooter)

→ **CMANGOS.17 dépend de 3 tickets P2 amont**.

## 8. Risque / piège ⚠️

- ⚠️ **Migration DB** — 5 nouvelles tables à schéma quasi-identique.
  Idempotent.
- ⚠️ **Reference loot cycles** — `LootTemplate A` reference → `B` reference
  → `A` = boucle infinie. Détection profondeur max + DFS au load.
- ⚠️ **Loot groups somme ≠ 100%** — politique : normaliser, rejeter,
  warner. À acter (cmangos rejette).
- ⚠️ **Conditions par drop** — un item dropdé visible mais pas
  pickupable si condition fail au pickup time (recheck). Ou pas drop
  du tout si condition fail à la génération (cmangos = recheck au
  pickup pour cohérence groupe).
- ⚠️ **Anti-fraude MasterLooter** — log de tous les drops + audit. Hors
  scope ticket mais à prévoir.
- ⚠️ **Fishing/pickpocketing** — variants délicats (pickpocket ne tue
  pas, drop est temporaire). Couvrir au design.
- ⚠️ **Loot persistence** — `Loot` pile sur entité morte = persisté DB
  ou volatile ? cmangos = volatile (despawn = perte). Politique LCDLLN
  à acter.
- Pas de wire-breaking côté protocole dans le ticket de base.

## 9. Recommandation finale

✅ **Faire en l'état**, **après** CMANGOS.13 + .16 + .15 :

1. **Étape 0** : audit M14.3 et M36.1 livraison pour mesurer ce qui
   existe déjà. Statuer si fusionner GatheringSystem dans le nouveau
   LootMgr (probablement bonne idée long-terme).
2. **Étape 1** : migration DB + LootEntry/LootTemplate/LootMgr Load
   + test smoke "creature meurt → loot généré".
3. **Étape 2** : reference loot + loot groups + tests.
4. **Étape 3** : conditions par drop (intégration CMANGOS.16).
5. **Étape 4** : LootRoll Need/Greed (intégration CMANGOS.15).
6. **Étape 5** : refactor `GatheringSystem` pour utiliser `LootMgr`
   (ou conserver séparé si simpler — décision design).

À planifier **après** CMANGOS.13 + .16 + .15 livrés. Effort raisonnable,
gros déblocant gameplay.

---

*Audit du 2026-05-08. Mises à jour : —*
