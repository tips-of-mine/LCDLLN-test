# CMANGOS.04 — Movement (server-authoritative splines)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.04_Movement_server_authoritative_splines.md](../../../../tickets/CMANGOS/CMANGOS.04_Movement_server_authoritative_splines.md)
> **Priorité** : P1 — squelette
> **Cible** : cross (master + shard + client)

## 1. Statut implémentation

❌ **Absent** — pas de système de splines server-authoritative. LCDLLN
utilise actuellement un modèle **client-prediction + reconciliation**
(M30) pour les Players, et il n'existe pas de système équivalent pour
les Creatures/NPCs.

## 2. Preuves dans le code

**Existant (architecture alternative LCDLLN, joueurs uniquement) :**
- [engine/gameplay/ClientPrediction.h](../../../../engine/gameplay/ClientPrediction.h) — prédiction côté client
  (`MovementKeyFlags`, `InputCommand` avec tick + clés + mouseDelta)
- [engine/gameplay/ClientPrediction.cpp](../../../../engine/gameplay/ClientPrediction.cpp) — implémentation
- [engine/gameplay/CharacterController.cpp](../../../../engine/gameplay/CharacterController.cpp) — contrôle local du perso
- M30 (3 tickets) : Client prediction + Server reconciliation + Lag compensation
- M26 (3 tickets) : Character controller movement + physics collision

**Manquant (vs spec ticket) :**
- ❌ `engine/server/shard/movement/` — dossier inexistant
- ❌ `engine/network/movement/` — dossier inexistant
- ❌ `engine/client/movement/` — dossier inexistant (le mouvement client passe
  par `gameplay/ClientPrediction`, pas un module dédié spline)
- ❌ `MoveSpline` (état runtime côté shard) — grep 0 résultat dans engine/
  hors world editor (RiverTool n'a aucun rapport)
- ❌ `MoveSplineInit` (builder fluide)
- ❌ `MoveSplineFlag` enum bitmask 32 bits (Walk/Flying/Swimming/Falling/Cyclic…)
- ❌ `Spline<T>` math header-only (Catmull-Rom + Bézier)
- ❌ `MoveSplinePacketBuilder` (sérialisation pure)
- ❌ `MoveSplineInterpolator` côté client
- ❌ Opcodes `kOpcodeMonsterMove`, `kOpcodeMonsterMoveStop`, `kOpcodeMoveTeleportAck`
- ❌ `MovementTypedefs.h` (centralisation `Vector3`/`Vector4`/`Quat`)

## 3. Recouvrement milestones existantes

✅ **Couvert (partiellement, mécanismes différents)** :
- M30.1/2/3 — Client prediction + reconciliation + lag compensation pour **les
  Players** (pas les NPCs).
- M26.1/3 — Character controller (Player) + swimming/flying.

Le pattern cmangos `MoveSpline` cible **les Creatures et l'IA** (broadcast
de patrouilles, déplacement scripté). C'est complémentaire de la prédiction
joueur, pas substituable.

## 4. Écart par rapport à la spec CMANGOS

100% des livrables sont absents pour les **Creatures/NPCs**. Pour les
**Players**, on a une approche radicalement différente (input streaming +
reconciliation) qui couvre ses propres cas.

Le besoin **réel non-couvert** : déplacement de masse de NPCs / mobs
(patrouilles, AI avance vers cible) sans saturer la bande passante. À ce
jour, si le shard veut bouger 100 mobs, soit il streame 100 positions par
tick (impossible à scaler), soit il n'envoie rien (NPCs immobiles côté
client).

## 5. Effort estimé

**L** (1 sprint complet) — 5 nouveaux modules + opcodes + tests round-trip
shard/client + bump `kProtocolVersion`. Ticket bien découpé en couches
isolées (math, runtime, builder, packet, interpolator) qui peuvent être
développées en parallèle puis intégrées.

## 6. Valeur joueur/serveur

**Élevée** — déblocant pour le PvE/contenu monde dynamique. Sans `MoveSpline`,
pas de mobs en mouvement crédibles à grande échelle. C'est un déblocant
**indirect** pour CMANGOS.07 (AI), CMANGOS.11 (Combat), CMANGOS.20
(MotionGenerators).

Critique pour le sentiment "monde vivant" du MMO. Moins critique court-terme
que CMANGOS.01/06 (qui sont nécessaires AVANT que les joueurs interagissent).

## 7. Dépendances bloquantes

- **CMANGOS.02 Entities** — `Unit` porte le `MoveSpline`. Sans hiérarchie ou
  équivalent, pas de point d'attache. Mais peut s'attacher à `EntityId` opaque.
- **CMANGOS.03 Grids** — `MessageDistDeliverer` pour broadcast spatial
  efficace de `SMSG_MONSTER_MOVE`. À défaut, broadcast shard global (acceptable
  pour la v1).

## 8. Risque / piège ⚠️

- ⚠️ **Wire-breaking** — 3 nouveaux opcodes (`kOpcodeMonsterMove`,
  `kOpcodeMonsterMoveStop`, `kOpcodeMoveTeleportAck`) + bump
  `kProtocolVersion` → **redéploiement serveur master + shard + client
  lock-step obligatoire**.
- ⚠️ **Redéploiement** — nouveau système de routing sur shard pour broadcast
  des splines.
- ⚠️ **Cohérence horloge client/serveur** — si le RTT varie, l'interpolation
  devient saccadée. Stratégie classique : timestamp serveur dans chaque
  paquet + offset lissé côté client (cf. ticket §Notes).
- ⚠️ **Spline ID monotone** — sur UDP, paquets out-of-order. Le client doit
  ignorer les paquets avec ID < à celui en cours. Bug subtil sinon.
- ⚠️ **Round-trip cross-platform** — le test wire round-trip doit passer
  côté shard Linux ET client Windows. Désync = bug critique.
- ⚠️ **Falling ≠ spline** — chute libre = paquet `Falling` avec vélocité
  initiale + gravité locale client + revérification périodique serveur.
- ⚠️ **Interaction MotionGenerators** (CMANGOS.20) — `MoveSpline` est la
  **couche transport**, `MotionGenerator` la **couche IA**. Ne pas mélanger.
- Pas de migration DB.

## 9. Recommandation finale

🔧 **Adapter et faire** :

1. **Étape 0 (cadrage)** : statuer la coexistence avec M30 (Players via
   prediction) et CMANGOS.04 (NPCs via splines). Valider que les deux
   systèmes peuvent cohabiter — c'est le cas dans cmangos, c'est cohérent.
2. **Étape 1** : implémenter `MovementTypedefs.h` + `Spline<T>` math
   (header-only, testable en isolation).
3. **Étape 2** : implémenter `MoveSplineFlag` enum + `MoveSpline` runtime
   shard + `MoveSplineInit` builder.
4. **Étape 3** : implémenter `MoveSplinePacketBuilder` + tests round-trip
   shard↔client.
5. **Étape 4** : allouer opcodes, bump `kProtocolVersion`, intégrer
   `MoveSplineInterpolator` côté client.
6. **Étape 5** : smoke test 1 Creature traversant 100m en 10s avec **1 seul
   paquet** envoyé (vs 100+ en streaming positions).

À planifier **après** CMANGOS.02 (au moins `ObjectGuid`) et **après**
CMANGOS.03 (au moins `MessageDistDeliverer`). Sinon le broadcast n'est
pas optimal et on perd une partie du bénéfice.

---

*Audit du 2026-05-08. Mises à jour : —*
