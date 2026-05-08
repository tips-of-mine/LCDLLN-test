# CMANGOS.30 — Cinematics (trigger opcode / camera validation)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.30_Cinematics_trigger_opcode_camera_validation.md](../../../../tickets/CMANGOS/CMANGOS.30_Cinematics_trigger_opcode_camera_validation.md)
> **Priorité** : P3 — ajout à valeur
> **Cible** : cross (shard + client)

## 1. Statut implémentation

❌ **Absent** — pas de système cinématiques, pas d'opcode trigger,
pas de chemins caméra anticheat.

## 2. Preuves dans le code

**Manquant :**
- ❌ `engine/server/shard/cinematics/`
- ❌ Opcode `SMSG_TRIGGER_CINEMATIC`
- ❌ Asset cinematic client-side packagé
- ❌ Parsing M2/cinematic-data serveur (anticheat)
- ❌ Migration DB `cinematic_camera_paths`

## 3. Recouvrement milestones existantes

❌ **Non couvert** — pas de milestone cinématiques.

## 4. Écart par rapport à la spec CMANGOS

100% absent. Pas critique MVP.

## 5. Effort estimé

**M** (2 PR) — opcode + handler + asset packagé client + anticheat
position. Wire-breaking (nouvel opcode).

## 6. Valeur joueur/serveur

**Moyenne** — feature visible (transitions narratives quêtes/intro
zone) mais pas critique. Polish.

## 7. Dépendances bloquantes

- **CMANGOS.05 vmap** — anticheat position caméra

## 8. Risque / piège ⚠️

- ⚠️ Wire-breaking (opcode trigger)
- ⚠️ Asset client-only — version sync requis (mismatch = crash client)
- ⚠️ Anticheat caméra : si désync, joueur "bloqué" en cinematic
- ⚠️ Pas de migration DB obligatoire (asset client suffit pour MVP)

## 9. Recommandation finale

⏸ **Reporter** — feature polish, pas dans MVP. À planifier quand
contenu narratif (quêtes scriptées, intro zones) demande des
cinématiques.

---

*Audit du 2026-05-08. Mises à jour : —*
