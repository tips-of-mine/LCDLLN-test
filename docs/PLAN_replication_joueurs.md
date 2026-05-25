# Plan — Visibilité joueur↔joueur (réplication live)

> Backlog technique pour permettre à deux joueurs connectés de **se voir et se voir
> bouger** dans le monde. Rédigé suite au constat du 2026-05-25 (deux comptes
> `thedjinhn` / `thedjinhn2` connectés simultanément, invisibles l'un à l'autre).

## Constat / diagnostic

La visibilité joueur↔joueur repose sur un système de **réplication d'entités** :
chaque client envoie sa position via un **canal UDP gameplay** au shard ; le shard
maintient une **grille spatiale (AoI / interest management)** et diffuse des
*snapshots* (spawn/despawn + positions) des entités proches à chaque client.

Ce système existe dans le code (`ServerApp`, `GridState`, `GridNotifier`,
`SpatialPartition`, `UdpTransport`, messages `Hello`/`Welcome`/`Snapshot`/`Spawn`)
mais **n'est pas opérationnel en production** :

- **Serveur** : `ServerApp` (toute la réplication : `HandleInput`, grille AoI,
  snapshots, spawn/despawn) n'est compilé que dans la cible **Windows**
  `server_app` (`src/CMakeLists.txt`, bloc `if(WIN32)`). La cible Linux
  **`shard_app`** (`src/shardd/main_linux.cpp`, c'est le conteneur
  `lcdlln-shard`) ne l'inclut pas : elle ne fait que le handshake de ticket TCP +
  ticks AI/Threat/DBScript/AntiCheat. `UdpTransport.cpp` est lui-même **Windows
  only** (`WSAStartup`).
- **Client** : le chemin UDP gameplay a été construit comme une **tranche
  verticale « commerce »** (boutique/vendeur/HV/troc — M35.x), pas pour le
  mouvement. `GameplayUdpClient` n'a **aucune** méthode d'envoi de position
  (`SendHello`/`SendTalk`/`SendShop*`/`SendAuction*` seulement) et **n'affiche
  aucun avatar distant** (`UIModelBinding::ApplySnapshot` décode le snapshot mais
  n'extrait que les stats du joueur **local** ; les entités distantes ne servent
  qu'à `m_chatWorld.SyncEntityPositions`).
- **Découverte d'endpoint** : le client vise `client.gameplay_udp.host=127.0.0.1`
  port `27015` par défaut (`Engine.cpp:9633`), **pas** l'adresse du shard annoncée
  par `SERVER_LIST`. Et `client.gameplay_udp.enabled` vaut `false` par défaut.

Conséquence visible dans les logs : la connexion shard n'est qu'un handshake de
ticket TCP (`alive_s≈0.15`, `rx_bytes=86`, `peer_closed`) ; tout le gameplay passe
par le **master** en TCP ; `CharacterSavePositionHandler` ne fait qu'un `UPDATE`
DB de la position **sans jamais la rediffuser** aux autres joueurs.

---

## Phase 0 — Décisions d'architecture (à trancher avant de coder)

- **T0.1 — Modèle d'autorité de position.** Aujourd'hui le **master** est autorité
  (`CharacterSavePositionHandler` → `UPDATE characters SET spawn_x/y/z`, TCP). Si le
  **shard** devient autorité via UDP, choisir :
  - *(recommandé)* le shard devient autorité runtime ; le master ne sert que de
    persistance périodique (le shard écrit la position en DB à l'autosave) ; le
    client cesse d'envoyer `CharacterSavePosition` au master pendant le jeu ;
  - sinon double-écriture → conflits de position.
- **T0.2 — Persistance côté shard Linux.** `ServerApp` Windows tourne **sans
  MySQL** (`CharacterPersistenceStore` en mode fichier). Sur Linux, décider si le
  shard lit/écrit la table `characters` MySQL (même base que le master) pour
  spawner chaque perso à sa vraie position → impacte le link de `shard_app`
  (`libmysqlclient` + `ENGINE_HAS_MYSQL`).
- **T0.3 — Port UDP & exposition Docker.** Choisir le port UDP gameplay du shard
  (ex. 3845) et l'exposer dans `deploy/docker/`.

---

## Phase A — Serveur : rendre la réplication exécutable sous Linux

- **TA.1 — Porter `UdpTransport` sur sockets POSIX.**
  `src/shardd/world/UdpTransport.{h,cpp}`. API publique inchangée ; entourer
  l'implémentation de `#if defined(_WIN32) … #else (BSD sockets) … #endif`
  (`socket/bind/recvfrom/sendto`, `fcntl(O_NONBLOCK)`, `EWOULDBLOCK/EAGAIN`,
  `close`, pas de `WSAStartup`). Test unitaire loopback `udp_transport_tests`.
  *Brique isolée, sans risque — bon point de départ.*
- **TA.2 — Ajouter `ServerApp` + monde à la cible `shard_app` (Linux).**
  `src/CMakeLists.txt` (cible `shard_app`, ~ligne 845) : ajouter `ServerApp.cpp`,
  `UdpTransport.cpp`, `SpatialPartition.cpp`, `GridState.cpp`, `GridNotifier.cpp`,
  `ZoneTransitions.cpp`, `TickScheduler.cpp`, `ServerProtocol.cpp` + systèmes
  gameplay (la liste du bloc WIN32, lignes ~20-60) + link `${MYSQL_LIBRARY}` selon
  T0.2. *Premier point de friction CI attendu (dépendances de compilation).*
- **TA.3 — Câbler la boucle gameplay dans `shardd/main_linux.cpp`.**
  Intégrer `ServerApp` (instanciation, `Init()`, tick) dans la boucle 100 ms
  existante (extraire un `TickOnce()` public ou lancer `ServerApp::Run()` sur un
  thread). Réconcilier les deux stacks réseau (TCP ticket + UDP gameplay). Le
  `helloNonce` UDP doit être validé contre un ticket/session émis (anti-usurpation
  de `character_id`).
- **TA.4 — Bridge persistance position.** Au `HandleHello`, charger la position
  réelle du perso (table `characters` via T0.2, ou la passer dans le ticket).
  Sinon tous les joueurs spawnent à l'origine.

---

## Phase B — Découverte de l'endpoint UDP

- **TB.1 — Le master annonce l'endpoint UDP du shard.** Champ `udp_endpoint` dans
  `ServerListPayloads` (ou la réponse `ShardTicket`), alimenté par
  `shard.register.udp_endpoint`, déclaré via
  `ShardToMasterClient::SetShardIdentity`.
- **TB.2 — Le client résout l'UDP depuis le shard choisi.** `Engine.cpp:9633` :
  prendre l'endpoint annoncé par `SERVER_LIST` au lieu du défaut `127.0.0.1`.
  ⚠️ **wire-breaking** (nouveau champ payload) → lock-step client/serveur.

---

## Phase C — Client : envoyer le mouvement

- **TC.1 — Ajouter l'envoi de position à `GameplayUdpClient`.** Méthode
  `SendInput(clientId, sequence, posX, posY, posZ, yaw)`. `ServerApp::HandleInput`
  ne prend aujourd'hui que `posX/posZ` → étendre le message `Input` (posY + yaw)
  → **bump `kProtocolVersion`**.
- **TC.2 — Appeler `SendInput` dans la boucle de jeu.** Dans
  `Engine::UpdateGameplayNet`, à la cadence `request_tick_hz`, envoyer la position
  de l'avatar local (le code n'y gère aujourd'hui que vendeur/HV/troc).
- **TC.3 — Activer l'UDP gameplay à l'entrée en monde** (pas seulement à
  l'ouverture d'une boutique) ; statuer sur le défaut de
  `client.gameplay_udp.enabled`.

---

## Phase D — Client : afficher les autres joueurs

- **TD.1 — Table d'entités distantes.** Consommer `Spawn`/`Snapshot`/`Despawn`
  dans `UIModelBinding` (aujourd'hui `ApplySnapshot` ignore les entités ≠ joueur
  local) → maintenir `id → {pos, yaw, vel}` avec buffer d'interpolation.
- **TD.2 — Rendu des avatars distants.** Réutiliser le pipeline avatar
  (`GeometryPass`). ⚠️ rappel CLAUDE.md : le pipeline avatar utilise
  `frontFace = CLOCKWISE` (winding fichier) — ne pas le casser.
- **TD.3 — Interpolation** entre snapshots (snapshot_hz≈10) ; extrapolation courte
  sur perte de paquet.
- **TD.4 — Plaques de nom (optionnel).** `m_chatWorld.SyncEntityPositions` fournit
  déjà les positions distantes — réutilisable pour nameplates / bulles de
  proximité.

---

## Phase E — Tests & déploiement

- **TE.1 — Tests unitaires** : `udp_transport_tests` (Linux loopback),
  encode/decode `Input` étendu, AoI grid (déjà couvert par `grid_*_tests`).
- **TE.2 — Test d'intégration 2 clients** en loopback : deux `Hello` → chacun
  reçoit le `Spawn`/`Snapshot` de l'autre.
- **TE.3 — Docker** : exposer le port UDP shard + clés de config
  (`shard.register.udp_endpoint`, `client.gameplay_udp.*`).
- **TE.4 — Anti-triche** : brancher `HandleInput` sur l'`AntiCheatGameplayRuntime`
  déjà présent (vitesse max, téléportation).

---

## Risques principaux

1. **Double autorité de position** (master DB vs shard UDP) — à trancher en T0.1,
   sinon désync / triche.
2. **Sécurité du `helloNonce`** : le shard doit lier la session UDP au ticket
   validé (TA.3), sinon usurpation de `character_id`.
3. **Deux stacks réseau sur le shard** (TCP ticket + UDP gameplay) à faire
   cohabiter proprement.
4. **Doublon chat** : `ServerApp` a son propre chat UDP, alors que le chat MVP
   passe par le master (`ChatRelayHandler`, opcodes 45/46) — clarifier lequel fait
   foi.

---

## Chemin critique minimal pour « se voir bouger »

TA.1 → TA.2 → TA.3 → TA.4 → TB.1/TB.2 → TC.1/TC.2/TC.3 → TD.1/TD.2/TD.3.

Les phases « commerce » UDP existantes sont déjà fonctionnelles et prouvent que le
pipeline `Hello`/`Welcome` marche.

---

## Déploiement

⚠️ **Redéploiement serveur (shard Linux) requis** + **lock-step client/serveur**
(nouveau champ endpoint UDP en Phase B, bump `kProtocolVersion` en Phase C) — un
client neuf parlerait dans le vide à un serveur ancien, et inversement.
