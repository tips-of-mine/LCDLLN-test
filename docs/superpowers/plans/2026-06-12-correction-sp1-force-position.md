# Correction SP1 — canal ForcePosition serveur→client : Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Chantier n°4 de la liste validée, re-cadré avec l'utilisateur (option « canal de correction » choisie) : le mouvement reste client-autoritaire (décision T0.1), mais le serveur gagne le droit d'IMPOSER une position. Corrige deux trous réels : (1) **bug du respawn SP2** — le serveur téléporte au spawn mais le client (autoritaire) écrase la téléportation au tick suivant → « Réapparaître » soigne sur place ; (2) **désync silencieuse anti-triche** — un input rejeté (speed/teleport hack) laisse le client convaincu de sa position sans jamais le corriger.

**Architecture:** Nouveau kind `ForcePosition = 86` (shard→client, ADDITIF — un client qui ne le connaît pas l'ignore, pas de bump de version ; v12 n'est de toute façon pas déployé). Producteurs serveur : `HandleRespawnRequest` (reason=Respawn, + **reset de l'état anti-triche** — sinon le premier input depuis le spawn serait vu comme un TeleportHack) et le rejet anti-triche de `HandleInput` (reason=AntiCheat, dernière position valide, throttlé 500 ms). Consommateur client : `m_characterController.Init(Vec3)` (le téléport de facto, déjà utilisé au changement de zone, Engine.cpp:8458) + yaw. V1 = snap immédiat dans tous les cas (le rubber-band lissé anti-triche = amélioration future ; un tricheur n'a pas droit au confort).

**Décision archivée :** `ClientPredictionSystem` (M30.1/M30.2) reste NON câblé — il implémente la prédiction-réconciliation d'un mouvement serveur-autoritaire (inputs → simulation serveur), à l'opposé de T0.1. Il sera le socle d'un éventuel futur passage en serveur-autoritaire (anti-triche durci). Documenté ici et dans CODEBASE_MAP.

### Task 1 — Wire (additif)
`ServerProtocol.h/.cpp/Tests` : kind `ForcePosition = 86` ; constantes `kForcePositionReasonRespawn=0 / AntiCheat=1 / Teleport=2` ; `struct ForcePositionMessage { uint32_t clientId; float x, y, z, yawRadians; uint8_t reason; }` (payload fixe 21 o) ; Encode/Decode + roundtrip + tronqué + reason hors domaine rejeté.

### Task 2 — Serveur
- `ConnectedClient.lastForcePositionSentMs` (throttle anti-triche).
- `SendForcePosition(client, reason)` (position courante serveur).
- `HandleRespawnRequest` : `m_antiCheat.Reset(persistenceCharacterKey)` + `SendForcePosition(…, Respawn)` après le téléport.
- Rejet anti-triche dans `HandleInput` : avant le `return`, `SendForcePosition(…, AntiCheat)` si ≥ 500 ms depuis le dernier.

### Task 3 — Client
- UIModel : `UIForcedPosition { bool pending; float x, y, z, yaw; uint8_t reason; }` + routage + `ClearForcedPosition()`.
- Engine (`UpdateGameplayNet`, avant l'envoi d'Input) : si pending → `m_characterController.Init(Vec3{x,y,z})`, `m_avatarYaw = yaw`, log avec reason, `ClearForcedPosition()`. L'Input suivant porte la position imposée → plus d'écrasement.

### Task 4 — Docs + PR
CODEBASE_MAP (canal de correction + statut archivé de ClientPrediction), push `correction-sp1`, PR base main. **Déploiement** : ⚠️ shardd (nouveaux producteurs) — fenêtre lock-step v12 déjà requise ; le kind est additif.

## Self-review
- Le bug respawn est corrigé de bout en bout (téléport + reset anti-triche + position rejouée par le client).
- Aucun changement de version wire ; rétro-additif.
- Boucle infinie impossible : ForcePosition ne déclenche pas d'Input rejeté (le client adopte la position serveur, l'anti-triche est resetté au respawn et tolère le delta au rejet — la position imposée EST sa référence).
