# CMANGOS.39 — Skills (craft discovery / extra item proc)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.39_Skills_craft_discovery_extra_item_proc.md](../../../../tickets/CMANGOS/CMANGOS.39_Skills_craft_discovery_extra_item_proc.md)
> **Priorité** : P3 — ajout à valeur (craft polish)
> **Cible** : shard

## 1. Statut implémentation

🟡 **Partiel** — `CraftingSystem` LCDLLN existe (probablement M19/M36
craft de base), mais discovery probabiliste et extra item proc data-
driven sont absents.

## 2. Preuves dans le code

**Existant :**
- [engine/server/CraftingSystem.h](../../../../engine/server/CraftingSystem.h) + `.cpp` — système crafting de base
- M19 (?) Crafting milestone

**Manquant :**
- ❌ `skill_discovery_template` table (`spell_required`, `spell_discovered`,
  `chance`)
- ❌ `skill_extra_item_template` table (`chance`, `items_per_craft`)
- ❌ Hook post-craft pour proc discovery + extra item

## 3. Recouvrement milestones existantes

✅ **Couvert (partiellement)** — craft de base livré. Hooks
discovery/extra item absents.

## 4. Écart par rapport à la spec CMANGOS

L'écart est **fonctionnel modéré** : craft existe, mais sans la
"profondeur" rare/découverte qui crée la rétention craft.

## 5. Effort estimé

**S-M** (1-2 PR) — 2 tables + hooks dans `CraftingSystem` post-success.
Pas wire-breaking.

## 6. Valeur joueur/serveur

**Moyenne** — feature polish craft. Discovery = engagement long terme
("encore 1 craft pour peut-être unlock").

## 7. Dépendances bloquantes

- **CMANGOS.13 Database** — SQLStorage cache hooks
- **CraftingSystem existant** — déjà OK

## 8. Risque / piège ⚠️

- ⚠️ Migration DB (2 tables) — idempotent
- ⚠️ Discovery anti-exploit : si `chance=0.001%`, 1000 crafts pour
  garantie. Patterns calibration selon économie.
- ⚠️ Extra item duplication exploit — une mauvaise config peut donner
  drop infini. Tests post-déploiement.

## 9. Recommandation finale

🔧 **Adapter et faire** **uniquement si** craft est central dans
LCDLLN. Sinon ⏸ Reporter. Effort minimal si craft mature.

À planifier après CMANGOS.13 Database.

---

*Audit du 2026-05-08. Mises à jour : —*
