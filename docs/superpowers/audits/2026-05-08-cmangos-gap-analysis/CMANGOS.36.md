# CMANGOS.36 — OutdoorPvP (zone plugins / objectives)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.36_OutdoorPvP_zone_plugins_objectives.md](../../../../tickets/CMANGOS/CMANGOS.36_OutdoorPvP_zone_plugins_objectives.md)
> **Priorité** : P3 — ajout à valeur
> **Cible** : shard

## 1. Statut implémentation

❌ **Absent** — pas de framework PvP en zones ouvertes. M100.28
(Gameplay Zones and Weather Zones) couvre la **délimitation de zones
gameplay** côté éditeur, mais pas les objectifs PvP.

## 2. Preuves dans le code

**Existant (orthogonal) :**
- M100.28 — Gameplay Zones (délimitation, pas objectifs PvP)

**Manquant :**
- ❌ `engine/server/shard/outdoorpvp/`
- ❌ `OutdoorPvPMgr` + plugins polymorphes
- ❌ State machine par objectif (neutre/capture/capturé)
- ❌ Migration DB triggers + scoring rules

## 3. Recouvrement milestones existantes

❌ **Non couvert** — pas de framework outdoor PvP.

## 4. Écart par rapport à la spec CMANGOS

100% absent. Pattern transposable pour :
- PvP zones ouvertes (capture de tour, défense de pont)
- Events scriptés saisonniers (invasions, festivals avec objectifs)

## 5. Effort estimé

**M-L** (2-3 PR) — framework + 1 plugin exemple. Wire-breaking pour
opcodes broadcast scoring (sauf si réutilise CMANGOS.28 WorldState).

## 6. Valeur joueur/serveur

**Moyenne** — feature polish endgame. Pas critique.

## 7. Dépendances bloquantes

- **CMANGOS.28 World** — WorldState broadcast (pré-requis explicite)
- **CMANGOS.19 Maps** — instances de zones

## 8. Risque / piège ⚠️

- ⚠️ Plugin loader (statique compile-time = simple, dynamic = risque)
- ⚠️ State machine bugs sur transitions = scoring désync
- ⚠️ Migration DB optionnelle (config statique acceptable v1)
- ⚠️ Triggers AreaTrigger géographiques — précision spatiale

## 9. Recommandation finale

⏸ **Reporter** — pas avant que .28 World et .19 Maps soient livrés.
Feature polish, à activer quand contenu PvP zone-based émerge dans la
roadmap.

---

*Audit du 2026-05-08. Mises à jour : —*
