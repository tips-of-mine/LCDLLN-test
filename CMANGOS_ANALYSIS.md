# Analyse cmangos pour LCDLLN

Analyse de l'arborescence `cmangos/mangos-tbc/src` (commit `master` au
2026-05-07) pour identifier les patterns, abstractions et idées
réutilisables ou adaptables au projet LCDLLN. **Objectif** : piocher des
idées, pas porter à l'identique. Aucun code cmangos n'est copié.

> Couverture : `shared/` (9 dossiers) + `game/` (40 dossiers) = **49 dossiers**.
> Format par dossier : rôle (1-2 phrases) → 3-5 idées concrètes → 1 reco.

---

## Sommaire

- [Table récapitulative — 49 modules](#table-récapitulative--49-modules)
- [Résumé exécutif (top reco par priorité)](#résumé-exécutif)
- [shared/](#shared)
- [game/](#game)

---

## Table récapitulative — 49 modules

Triés par priorité puis alphabétique. **Aucun module n'est écarté** : même
ceux marqués « déjà couvert » ou « MVP non prioritaire » ont une idée
exploitable listée pour servir de point de départ le jour où on touche le
sujet.

Légende :
- **Source** : `s/` = `shared/`, `g/` = `game/`
- **Cible** : `master` (lobby/auth/relay), `shard` (world sim), `client`,
  `cross` (master + shard), `outil` (binaire à part)
- **Priorité** :
  - **P1** = squelette ou déblocant immédiat
  - **P2** = feature gameplay essentielle (à implémenter dès la première
    feature dépendante)
  - **P3** = ajout à valeur ajoutée, non critique
  - **P4** = déjà couvert chez nous, ou cas très spécifique / externe

### P1 — Squelette & déblocants (5 modules)

| Module | Source | Cible | Idée principale |
|---|---|---|---|
| Chat | g/ | cross | Déblocant MVP : `CanSpeak()` centralisé + `CheckEscapeSequences` + dispatch table-driven + split master(relay) / shard(proximité) |
| Entities | g/ | shard | `UpdateFields` + `UpdateMask` : delta réseau, pré-requis AoI |
| Grids | g/ | shard | Partitionnement spatial 2D + Visitor pattern templated + GridStates ; base AoI / spawn / broadcast |
| Movement | g/ | cross | Spline server-authoritative + interpolation client (réduit drastiquement la bande passante) |
| vmap | g/ | shard | LOS + height + tile streaming refcount + BIH/BVH + DynamicTree (portes) |

### P2 — Gameplay essentiel (23 modules)

À implémenter dès qu'une feature concrète en dépend. Ordre alphabétique.

| Module | Source | Cible | Idée principale |
|---|---|---|---|
| Accounts | g/ | master | Enum `AccountTypes` 5 niveaux + `HasLowerSecurity()` systématique avant action sur autrui |
| AI | g/ | shard | Registry + Selector pour CreatureAI + couche `EventAI` data-driven quand contenu PvE riche |
| Arena | g/ | shard | `ArenaTeam` persistant + MMR glicko + `WeeklyMaintenance` (colisée LCDLLN dans une ville, sans cross-realm) |
| AuctionHouse | g/ | master | Tick global d'expirations + livraison via mail + index RAM secondaire (multi-maisons par faction/région) |
| BattleGround | g/ | shard | Framework instancié hiérarchique (`BattleGround` virtuelle + 1 classe par variante) + score sous-classé + reconnexion |
| Combat | g/ | shard | Décomposition `CombatManager` / `ThreatManager` / `HostileRefManager` bidirectionnel + state machine in/leaving/out |
| Database | s/ | cross | `SQLStorage` (cache RAM read-only) + `SqlDelayThread` (queue async) + prepared statements |
| DBScripts | g/ | shard | DSL minimaliste ~30 commandes (TALK/MOVE_TO/CAST_SPELL…) + délais + targeting polymorphe + hot-reload |
| Globals | g/ | shard | Mini-DSL `Conditions` / `UnitCondition` data-driven, réutilisé par quêtes / loot / scripts |
| Groups | g/ | master | `GroupReference` intrusive (auto-invalidation) + loot rules pluggables + persistance partielle (5-man volatile, raid DB) |
| Guilds | g/ | master | Schéma DB (guild/member/rank/bank/eventlog) + permissions bitmask par rank + banque multi-onglets |
| Loot | g/ | shard | Templates DB génériques (`creature_loottemplate`, `go_loottemplate`…) + loot groups (somme = 100%) + reference loot |
| Mails | g/ | master | Schéma `mail` + `mail_items` + COD + expiration auto + `MassMailMgr` batched |
| Maps | g/ | shard | « Map = un thread logique » + sous-classes (Dungeon/BG/World) + `SpawnGroup` + `MapPersistentState` |
| MotionGenerators | g/ | shard | Stack de générateurs (`std::stack<MovementGenerator*>`) + pathfinder Detour navmesh par tile |
| Pools | g/ | shard | `pool_template` + `pool_creature` + nested pools + sélection pondérée non biaisée |
| Quests | g/ | cross | Split `QuestTemplate` (DB statique) / `QuestStatusData` (per-player) + objectifs hétérogènes via `std::variant` (plus C++20 idiomatique que arrays parallèles cmangos) + flags bitmask Daily/Weekly/Repeatable |
| Reputation | g/ | shard | Faction template matrix (faction_a × faction_b → relation) + bitmask flags + paliers calculés + spillover parent |
| Server | g/ | cross | `PacketLog` rejouable pour debug protocole + table d'opcodes typée + DBC stores équivalents |
| Social | g/ | master | `FriendsManager` + broadcast présence + ignored filtré côté expéditeur (économie réseau) + notes privées |
| Spells | g/ | shard | Split `Spell` (instance) / `SpellTemplate` (def) / `SpellAura` (effet appliqué) / `ProcEvent` + state machine cast + `SpellFamily` mask |
| Trade | g/ | master | 2-phase commit (both-must-accept) + revérification serveur atomique + timer anti-scam 6s post-modif |
| World | g/ | shard | `WorldStateExpression` mini-DSL conditions data-driven (game-changer pour éditeur monde) + tick multi-niveaux + shutdown gracieux |

### P3 — Ajouts à valeur ajoutée (14 modules)

Non critiques pour le MVP, mais chacun apporte une vraie qualité de vie ou
de prod.

| Module | Source | Cible | Idée principale |
|---|---|---|---|
| Anticheat | g/ | shard | Réserver interface `IServerSideValidator` + validation deltas de position (vitesse réelle vs max théorique) |
| Cinematics | g/ | client | Trigger via opcode + asset client-side + parsing M2 serveur pour anticheat (téléport pendant cinématique) |
| GameEvents | g/ | shard | Activation par `event_id` sur spawns + scheduler central + events en cascade (parent/child) |
| GMTickets | g/ | master | Ticket = row DB simple + handler/Mgr séparés + notification asymétrique joueur/GM |
| LFG | g/ | master | `LFGQueue` séparée du `LFGMgr` + matching par rôles requis + state machine joueur (Idle/Queued/Proposal/Boot) |
| Metric | s/ | cross | InfluxDB line-protocol + `Measurement` RAII (`{ Measurement m("X"); ... }` profile à la sortie de scope) + flush async batché |
| Multithreading | s/ | cross | Pattern `Messager` : file de `std::function` cross-thread, swap-and-execute (élimine contention prolongé) |
| OutdoorPvP | g/ | shard | `OutdoorPvPMgr` + plugin polymorphe par zone + state machine par objectif (capture de tour) |
| Platform | s/ | client | `WheatyExceptionReport` adapté pour crash dump joueur Windows (zéro dépendance externe vs Crashpad) |
| PlayerBot | g/ | shard | `WorldSession` headless réutilisant les vrais handlers — load testing du shard sans client réel (10x à 100x plus de bots) |
| Skills (craft) | g/ | shard | Hooks data-driven post-action (`skill_discovery_template`, `skill_extra_item_template`) |
| Tools | g/ | outil | `PlayerDump` format texte pour migration cross-shard + cleaner DB orphelins périodique + `Formulas.h` centralisé |
| Util | s/ | cross | `ByteBuffer` typé (`<<` / `>>`) + `ProducerConsumerQueue` + `UniqueTrackablePtr` (faible coût atomique) |
| Weather | g/ | shard | Markov chain par zone (table `game_weather` pondérée) + server-authoritative + transition douce (grade 0→1) |

### P4 — Déjà couvert ou cas spécifique (7 modules)

Listé pour référence — l'idée principale reste utile à connaître.

| Module | Source | Cible | Idée à connaître |
|---|---|---|---|
| Addons | g/ | client | Handshake addons à l'AUTH (CRC liste) + `BannedAddonList` côté serveur. **Déjà couvert** par `kProtocolVersion`. À reconsidérer si ImGui devient scriptable côté joueur. |
| AuctionHouseBot | g/ | master | Bot HV via opcodes internes (pas DB direct) — pertinent uniquement si la pop tombe trop bas pour avoir un HV organique. |
| Auth | s/ | master | SRP6 + `BigNumber` + `CryptoHash`. Pertinent uniquement si on quitte le mot-de-passe-hashé-en-base actuel pour un protocole zero-knowledge. |
| Config | s/ | cross | API `GetXxxDefault(key, fallback)`. **Déjà couvert** — notre stack JSON+CLI hiérarchique est strictement plus expressive. |
| Log | s/ | cross | Niveaux + multi-sinks + filtres. **Déjà couvert** par PR #468. |
| Network | s/ | cross | Boost.Asio + template `AsyncSocket<T>`. **Déjà couvert** — notre NetServer epoll est mieux adapté aux opcodes binaires fixes Linux. |
| VoiceChat | g/ | client | Stub TBC vide. À intégrer via service externe (LiveKit, Mumble) le jour venu, pas via master/shard. |

> **Total** : 5 (P1) + 23 (P2) + 14 (P3) + 7 (P4) = **49 modules** (couvre l'intégralité de `shared/` et `game/` cmangos-tbc).

---

## Résumé exécutif

### Priorité 1 — à intégrer rapidement (déblocants ou structurants)

| Module | Idée clé | Pourquoi maintenant |
|---|---|---|
| **Chat** | `CanSpeak()` centralisé + `CheckEscapeSequences` + dispatch table-driven + split master(relay)/shard(proximité) | C'est ton point de blocage actuel ; sans ces 4 patterns tout chat MVP s'effondre dès que la pop dépasse quelques joueurs. |
| **Grids** | Partitionnement spatial 2D + Visitor pattern templated + GridStates | Squelette de toute simulation MMO scalable côté shard ; aucun système d'AoI/spawn correct sans ça. |
| **vmap** | Collisions LOS + tile streaming + BIH/BVH + DynamicTree | Sans LOS serveur-authoritative, pas de combat distance crédible ni de portes. Le module cmangos avec la plus grosse valeur réutilisable directe. |
| **Movement** | Spline server-authoritative + interpolation client | Économie réseau majeure vs streaming de positions ; obligatoire à toute échelle. |
| **Entities** | UpdateFields + UpdateMask | Diff snapshot = pré-requis AoI, sans ça la bande passante explose à 50+ joueurs co-localisés. |

### Priorité 2 — gains forts dès qu'on touche le sujet

| Module | Idée clé |
|---|---|
| **Maps** | « Map = un thread logique » + sous-classes (DungeonMap, BattleGroundMap) + SpawnGroup + MapPersistentState |
| **MotionGenerators** | Stack de générateurs + pathfinder Detour partagé par tile |
| **Combat** | Décomposition `CombatManager` / `ThreatManager` / `HostileRefManager` (graphe bidirectionnel) |
| **Spells** | Split `Spell` / `SpellTemplate` / `SpellAura` / `ProcEvent` |
| **Loot** | Schéma DB `*_loottemplate` générique + loot groups + reference loot |
| **Guilds** | Schéma DB + permissions bitmask par rank + banque multi-onglets |
| **World** | `WorldStateExpression` (mini-DSL conditions) — leverage éditorial massif pour ton éditeur monde |
| **Globals** | Conditions data-driven (réutilisé par quêtes, loots, scripts) |
| **Database** | `SQLStorage` (cache RAM read-only) + `SqlDelayThread` (queue async) + prepared statements |

### Priorité 3 — utile, à programmer plus tard

| Module | Idée clé |
|---|---|
| **Util** | `ByteBuffer` (sérialisation paquet ergonomique) + `ProducerConsumerQueue` |
| **Server** | `PacketLog` rejouable pour debug protocole |
| **Tools** | `PlayerDump` pour migration cross-shard |
| **Metric** | InfluxDB line-protocol + `Measurement` RAII (observabilité prod) |
| **Multithreading** | Pattern `Messager` (file de fonctions cross-thread, swap-and-execute) |
| **Platform** | `WheatyExceptionReport` adapté au client Windows pour crash dumps joueurs |
| **Anticheat** | Réserver une interface `IServerSideValidator` shard-side |
| **Pools** | Schéma `pool_template` + `pool_creature` + nested pools |
| **Reputation** | Faction template (matrice `faction × faction → relation`) |
| **Weather** | Markov chain par zone + server-authoritative |

### À ignorer ou déjà couvert

- **Log** : déjà couvert par PR #468.
- **Config** : ta stack JSON+CLI hiérarchique est strictement plus expressive que celle de cmangos.
- **Auth** : SRP6 utile uniquement si tu veux quitter le hash-en-base actuel.
- **Network** : ton NetServer epoll est déjà mieux adapté que Boost.Asio pour le cas LCDLLN.
- **VoiceChat** : stub TBC vide, à intégrer via service externe le jour venu (LiveKit/Mumble).
- **OutdoorPvP**, **AuctionHouseBot**, **Cinematics**, **Skills (craft)** : reporter, non MVP.

---

## shared/

### Auth

**Rôle** : Couche cryptographique du flux de login WoW (handshake SRP6, hashage, chiffrement de session RC4) entre realmd/authserver et client. Wrappers maison (`BigNumber` autour de `BIGNUM` OpenSSL).

**Idées à piocher** :
- `BigNumber.h` : wrapper RAII autour de `BIGNUM` avec opérateurs surchargés. Utile si on ajoute un protocole de login custom — sinon overkill.
- SRP6 complet, éprouvé en prod sur 20 ans. Référence directe si on veut un login zero-knowledge (jamais de mot de passe en clair côté wire).
- `CryptoHash.h` : interface unifiée SHA1/SHA256 avec API streaming `UpdateData()/Finalize()`. Plus propre que des appels OpenSSL nus.
- `SARC4` : RC4 obsolète, **à ignorer** (on a OpenSSL + AES-GCM/ChaCha20).
- `base32` : utile si on introduit du TOTP/2FA un jour.

**Reco LCDLLN** : À ignorer pour l'instant — SRP6 ne se justifie que si on quitte le mot-de-passe-hashé-en-base actuel. Garder `BigNumber` en tête si besoin futur.

### Config

**Rôle** : Charge un fichier `.conf` style INI/key-value avec lecture typée (`GetIntDefault`, `GetStringDefault`). Mono-fichier, pas de hiérarchie native, pas de hot-reload.

**Idées à piocher** :
- API `GetXxxDefault(key, fallback)` avec fallback inline. **Déjà couvert** chez nous (config JSON+CLI hiérarchique, plus puissant).
- Rien d'autre à piocher.

**Reco LCDLLN** : Déjà couvert.

### Database

**Rôle** : Couche d'accès SQL multi-backend (MySQL/PostgreSQL/SQLite) avec pool, queue asynchrone (`SqlDelayThread`), prepared statements, et caches RAM typés (`SQLStorage`).

**Idées à piocher** :
- **`SqlDelayThread`** : worker dédié qui consomme une file de `SqlOperation` (callback + paramètres). Le caller reçoit un `QueryResultFuture`. Pattern intéressant si notre pool MySQL actuel est synchrone bloquant.
- **`SQLStorage` / `SQLStorageImpl`** : cache typé read-only chargé une fois au boot, indexé par PK, accédé en O(1) sans relock. Idéal pour tables statiques (loot, items, spells).
- **`SqlPreparedStatement`** : abstraction binding-typed avec cache des statements préparés par connexion.
- `Field.h` : extraction typée d'une cellule (`GetUInt32()`, `GetString()`) plus propre que les API MySQL brutes.
- Multi-backend : **à ignorer**, on est mono-MySQL.

**Reco LCDLLN** : À piocher — porter `SQLStorage` pour les tables statiques shard-side, vérifier si notre pool a déjà un équivalent `SqlDelayThread`.

### Log

**Rôle** : Système de log centralisé avec niveaux, multi-sinks fichier+console, formattage et timestamps.

**Idées à piocher** : aucune nouvelle.

**Reco LCDLLN** : Déjà couvert (PR #468).

### Metric

**Rôle** : Collecteur de métriques temps-série (points avec tags+fields à la InfluxDB) + `Measurement` RAII pour mesurer des durées de scope. Flush async via Boost.Asio.

**Idées à piocher** :
- **`Measurement` RAII** : `{ Measurement m("LoginHandler"); ... }` → durée poussée à la sortie de scope. Pattern ergonomique pour profiler du code chaud.
- **Modèle InfluxDB line-protocol** (tags + fields + timestamp) : standard de facto, lisible par Grafana/Telegraf out-of-the-box.
- **Flush asynchrone batché** : queue protégée + scheduled timer, évite N petits packets HTTP.
- Connexion config-driven, désactivation totale en dev par config vide.

**Reco LCDLLN** : À piocher — neuf chez nous, observabilité prod (latence handlers, taille queues, ticks/sec) critique pour un MMO. Cibler InfluxDB+Grafana.

### Multithreading

**Rôle** : Deux primitives — `Threading` (wrappers thread/lock) et `Messager` (file de messages cross-thread où un producteur push des `std::function` qu'un consommateur exécute en batch).

**Idées à piocher** :
- **`Messager`** : au lieu de protéger un état partagé par mutex, le thread propriétaire vide périodiquement une queue de fonctions qui modifient son état localement. **Swap-and-execute** : on swap le vector entier sous lock, exécution hors lock. Élimine le contention prolongé.
- Cas d'usage LCDLLN : master → shard "exécute ceci dans ton tick", IO thread → game thread "voici un paquet".
- `Threading.h` : wrappers `std::thread` classiques, **rien à piocher** vu C++20.

**Reco LCDLLN** : À piocher — pattern `Messager` élégant et probablement neuf chez nous.

### Network

**Rôle** : Couche socket asynchrone basée sur Boost.Asio, templates `AsyncSocket<T>`/`AsyncListener<T>` avec `Read()`, `ReadUntil()`, `Write()` à callbacks.

**Idées à piocher** :
- Pattern **CRTP/template + callback** sur `AsyncSocket<T>` : socket paramétrée par type de session, pas d'`std::function` indirect au runtime.
- `ReadUntil(delimiter)` : utile pour parser des protocoles texte (debug console).
- **À ignorer** : Boost.Asio est lourd, on a NetServer epoll TCP custom + OpenSSL plus léger.
- Architecture multi-thread Asio (un `io_context` par thread) — référence si on scale.

**Reco LCDLLN** : À ignorer — notre NetServer epoll est mieux adapté. Garder en tête le template `AsyncSocket<Session>` pour éviter des virtual calls hot path.

### Platform

**Rôle** : Couche d'abstraction OS — daemonization Posix, Windows Service API, byte order, et un crash reporter Windows (`WheatyExceptionReport`) qui dump une stack trace avec valeurs des variables locales.

**Idées à piocher** :
- **`WheatyExceptionReport`** : crash dump auto-généré sur exception non catchée Windows, avec types et valeurs des locales via DbgHelp. **Très précieux** pour le client Windows LCDLLN.
- **`PosixDaemon`** : `daemon(3)` + écriture pidfile + redirect stdout/stderr. Utile pour master/shard si on veut un mode `--daemon`.
- `ServiceWin32` : **à ignorer**, pas de cas d'usage Windows server.
- `CompilerDefs.h` : macros `MANGOS_INLINE`, `MANGOS_LIKELY` — **déjà couvert** par C++20 (`[[likely]]`/`[[unlikely]]`).

**Reco LCDLLN** : À piocher — adapter `WheatyExceptionReport` au client Windows pour collecter les crashes joueur sans dépendance externe (zéro coût vs Crashpad).

### Util

**Rôle** : Sac à outils — sérialisation binaire (`ByteBuffer`), endianness, timer, queue producer-consumer, smart pointer trackable, asserts, micro-benchmarks.

**Idées à piocher** :
- **`ByteBuffer`** : sérialisation packet avec `operator<<`/`operator>>` typés, position read/write séparées, gestion bit-level (`WriteBit`, `FlushBits`). Si notre `NetServer` utilise du `memcpy` brut, c'est un upgrade ergonomique majeur.
- **`ProducerConsumerQueue<T>`** : queue thread-safe générique avec `WaitAndPop` (cv). Réutilisable pour file de logs async, file de tasks DB.
- **`UniqueTrackablePtr`** : smart pointer avec compteur de références faibles (sans coût atomique d'un `shared_ptr`). Utile pour tracker des entités sans cycle.
- `CodeBench` : micro-bench inline `BENCH_START("name") ... BENCH_END()`. Souvent supplanté par `Measurement`.
- `ProgressBar`, `Timer`, `Errors` : déjà couvert ou trivial.

**Reco LCDLLN** : À piocher — `ByteBuffer` (gain ergonomique sur sérialisation) et `ProducerConsumerQueue` (générique).

---

## game/

### Accounts

**Rôle** : Cycle de vie des comptes côté serveur de jeu (création, validation credentials, niveau de privilège GM, lien compte→personnages). Centré sur `AccountMgr.cpp/h`.

**Idées à piocher** :
- **Enum `AccountTypes` à 5 niveaux** (PLAYER, MODERATOR, GAMEMASTER, ADMINISTRATOR, CONSOLE) plutôt qu'un simple booléen `is_gm`.
- **Fonction `HasLowerSecurity(target, source)`** systématique avant toute action affectant un autre compte (ban, kick, whisper privé) — empêche les escalades de privilèges même quand un GM a un bug logique.
- **Singleton `AccountMgr` chargé une fois au boot** avec cache mémoire username→accountId.
- Séparation nette `AccountMgr` (logique métier) / `AccountStore` (persistance).

**Reco LCDLLN** : À piocher — implémenter un enum `AccountRole` à plusieurs niveaux dans `AccountStore` côté master avant que des commandes GM/admin n'apparaissent.

### Addons

**Rôle** : Reçoit la liste des addons annoncée par le client à la connexion, valide leur CRC, renvoie la signature publique des addons officiels.

**Idées à piocher** :
- **Handshake addons à l'AUTH** : blob compressé (zlib) listant addons + CRC en début de session.
- **CRC public/privé** : addons officiels signés serveur, addons tiers passent en "non-validé".
- **`BannedAddonList`** côté serveur : blacklister à chaud sans patch client.

**Reco LCDLLN** : À ignorer — pas de système d'addons prévu, l'inspiration "version handshake" déjà couverte par `kProtocolVersion`. À reconsidérer si ImGui devient scriptable côté joueur.

### AI

**Rôle** : Framework d'IA pour PNJ : registre central (`CreatureAIRegistry`), sélecteur dynamique (`CreatureAISelector`), 4 sous-systèmes (BaseAI hard-codé, EventAI piloté par DB, PlayerAI, ScriptDevAI codé en C++).

**Idées à piocher** :
- **Registry + Selector pattern** : chaque AI s'auto-enregistre par nom au boot, sélecteur instancie via factory string→ctor. Ajout d'AI sans toucher au cœur.
- **EventAI piloté par DB** (table `creature_ai_scripts` avec triggers `OnAggro`/`OnHealthBelow`/`OnSpawn` + actions) — pas de recompilation pour un boss simple.
- **PlayerAI** = même interface que CreatureAI mais sur joueur (mind control, fear, charm) — abstraction propre.
- **ScriptDevAI** comme couche séparée pour les boss complexes en C++ : isole le code "contenu" du code "moteur".

**Reco LCDLLN** : À adapter — adopter le pattern Registry+Selector côté shard dès le premier PNJ scripté ; reporter EventAI tant que le contenu PvE est artisanal.

### Anticheat

**Rôle** : Détection speed/teleport/fly hack, mouvements impossibles, paquets malformés. Architecturé en plugin (`Anticheat.hpp` interface + `module/`).

**Idées à piocher** :
- **Architecture plugin** (interface abstraite + impl chargeable) — permet une nouvelle heuristique sans rebuild du shard.
- **Validation côté serveur des deltas de position** : recalcul de la vitesse réelle entre deux paquets et comparaison au max théorique.
- **Hook sur les commandes chat** (`AnticheatChatCommands.h`) — intercepter pour logger/bloquer.
- Désactivation par défaut + activation via config.

**Reco LCDLLN** : À adapter — pas prioritaire MVP, mais réserver dès maintenant une interface `IServerSideValidator` côté shard pour ne pas avoir à refactor plus tard.

### Arena

**Rôle** : Équipes d'arène (2v2, 3v3, 5v5) : création, roster, MMR/rating, distribution hebdo des points, persistance DB. Couplé à `BattleGroundQueue`.

**Idées à piocher** :
- **Séparation `ArenaTeam` (entité persistante) / `BattleGround` (instance de match)** : applicable au "colisée local" LCDLLN (équipe persiste, instance éphémère).
- **MMR + rating glicko-like** par équipe stocké en DB, recalculé après chaque match — réutilisable tel quel pour le ladder du colisée.
- **`ArenaTeamHandler`** : opcodes spécifiques (invite, kick, disband, query) séparés de la logique métier.
- **Distribution hebdo via `WeeklyMaintenance`** : points calculés en cron serveur, pas à chaque match.

**Reco LCDLLN** : À adapter — garder `ArenaTeam` + MMR + distribution périodique. **Ignorer la file cross-realm** (colisée LCDLLN = entrée physique dans une ville, pas de queue) ; matchmaking devient "qui est dans l'antichambre".

### AuctionHouse

**Rôle** : Listing, enchères, expirations, livraison par mail, frais de mise.

**Idées à piocher** :
- **3 maisons séparées** (Alliance/Horde/Neutre) avec taux de commission différents — pattern "instance par faction/zone" intéressant si LCDLLN veut des HV régionaux.
- **Update tick globale** qui balaie les enchères expirées (pas un timer par enchère) — scalable à 100k items.
- **Livraison par système mail** : découple le HV de la session du joueur (acheteur peut être offline).
- **Index secondaire en RAM** (par item entry, par owner, par expiration) reconstruit au boot — recherches O(1) sans hit DB.

**Reco LCDLLN** : À piocher — HV naturellement master-side (cross-shard) ; reprendre "tick global + livraison mail + index RAM" tel quel le jour où on l'implémente.

### AuctionHouseBot

**Rôle** : Bot peuplant le HV avec des objets aléatoires achetés/vendus pour simuler une économie active sur serveur low-pop.

**Idées à piocher** :
- **Configuration externe** (`.conf.dist.in` template versionné, `.conf` réel non-versionné).
- **Bot = client interne réutilisant les mêmes opcodes** que les vrais joueurs au lieu d'écrire en DB directement : exerce le code de production en permanence.
- **Tirage pondéré par rareté/qualité** + bornes prix min/max par catégorie.

**Reco LCDLLN** : À ignorer pour l'instant — pertinent uniquement si la pop tombe trop bas. Pattern "bot interne via opcodes" à mémoriser pour les tests d'intégration.

### BattleGround

**Rôle** : Framework PvP instancié : 1 base `BattleGround` + 1 classe par BG (WSG, AB, EotS, AV, arènes), `BattleGroundMgr` qui crée/détruit, `BattleGroundQueue` qui matchmake.

**Idées à piocher** :
- **Hiérarchie `BattleGround` virtuelle + `BattleGroundWS` concret** override `EventPlayerCapturedFlag`, `EndBattleGround`, `Reset` — pattern "moteur générique + contenu spécifique" hyper réutilisable pour le colisée LCDLLN.
- **`BattleGroundQueue` séparé du `BattleGroundMgr`** : la file vit en dehors de l'instance — applicable même sans cross-realm pour gérer "antichambre".
- **Score per-player via `BattleGroundScore`** sous-classé par BG (kills, flags, captures).
- **`EventPlayerLoggedIn`/`EventPlayerLoggedOut`** sur l'instance : reconnexion en plein match — critique.
- Reset automatique des instances vides plutôt que persistance.

**Reco LCDLLN** : À adapter — reprendre la hiérarchie pour le colisée, **ignorer le matchmaking cross-realm**, garder "score sous-classé + reconnexion supportée".

### Chat

**Rôle** : Système complet de communication : routage par canal (say/yell/whisper/party/guild/channel/world), canaux dynamiques avec ownership/modération, hiérarchie de commandes GM par niveau d'accès, anti-spam mute. **Module le plus mature à piocher** vu que ton chat MVP (opcodes 45/46) est le point de blocage actuel.

**Idées à piocher** :
- **Dispatch table-driven hiérarchique pour commandes GM** : tableau statique `ChatCommand[]` avec `{name, security_level, handler, sub_table}` — `FindCommand()` navigue récursivement (`.ban account`, `.debug send`). Plus maintenable que le `if/else` géant qu'on aura inévitablement, et permet d'ajouter une commande sans toucher le routeur.
- **Validation `HasLowerSecurity` avant toute action chat affectant autrui** (whisper bypass mute, kick channel, tell d'un GM caché) : empêche un modérateur de kick un admin par bug — à pré-câbler dès l'ajout des premiers rôles.
- **`Player::CanSpeak()` centralisé** : un seul endroit qui répond "ce joueur a-t-il le droit de parler maintenant ?" en agrégeant mute global, mute canal, anti-flood, restrictions niveau ; chaque type de message (say/whisper/channel) appelle la même fonction. **À porter direct dans `ChatRelayHandler`.**
- **Truncation UTF-8-safe à 255 octets** + **`CheckEscapeSequences()` pour bloquer liens malicieux** (item links forgés, hyperlinks fake) + **strip caractères invisibles** (`CONFIG_BOOL_CHAT_FAKE_MESSAGE_PREVENTING`) : trois protections orthogonales que LCDLLN devra avoir avant l'ouverture publique du chat — pas optionnel, les premiers exploits chat exploitent toujours ça.
- **Channels dynamiques avec lifecycle ownership** : premier joueur = owner, transfert auto au départ avec priorité aux modérateurs, ban-list mémoire, password optionnel, conversion custom→static au-delà d'un seuil. Pattern entier réutilisable pour des canaux thématiques LCDLLN ("/join recrutement-guilde").
- **GM commands segmentées en `Level0/1/2/3.cpp`** : un fichier = un palier de privilège, lecture du code donne immédiatement le périmètre d'un rôle. Mieux qu'un fichier monolithique annoté.
- **Chat relay pattern naturellement master-side** : whisper/guild/world transitent par un agrégateur central qui ne dépend d'aucun shard particulier — l'architecture LCDLLN actuelle (`ChatRelayHandler` côté master) est alignée ; en revanche le **say/yell/emote local** doit rester shard-side (proximité 3D), à séparer clairement dans le code pour ne pas charger le master de paquets de proximité.

**Reco LCDLLN** : À piocher massivement — porter immédiatement (1) `CanSpeak()` centralisé, (2) `CheckEscapeSequences` + truncation UTF-8, (3) dispatch table-driven pour les futures slash commands. Garder le **split master/shard** : relay (whisper/guild/world/channel) sur master, proximité (say/yell/emote) sur shard — débloque le chat MVP sans tout refondre.

### Cinematics

**Rôle** : Joue les cinématiques d'intro/quête côté client en envoyant l'ID, et parse les modèles M2 pour extraire les positions de caméra serveur-side (anticheat).

**Idées à piocher** :
- **Trigger cinématique = simple opcode `SMSG_TRIGGER_CINEMATIC` avec un ID** : le serveur ne stream rien, le client a déjà l'asset — pattern "le serveur nomme, le client joue" applicable à tout asset lourd.
- **Parsing M2 côté serveur** uniquement pour les chemins de caméra (anticheat : empêcher un téléport pendant la cinématique).
- **`M2Stores` cache global lazy-loaded** : on parse à la première utilisation, on cache ensuite.

**Reco LCDLLN** : À ignorer pour l'instant. Mémoriser le pattern "opcode trigger + asset client-side" pour tout futur effet visuel scripté.

### Combat

**Rôle** : Cœur du combat : `CombatManager` (entrée/sortie de combat), `ThreatManager` (aggro PNJ), `HostileRefManager` (graphe bidirectionnel "qui menace qui"), `DuelHandler`, `CombatHandler`.

**Idées à piocher** :
- **Graphe `HostileRefManager` bidirectionnel** : chaque référence "A menace B" est doublée en "B est menacé par A" pour O(1) cleanup quand un acteur meurt — évite les "fantômes d'aggro" sur PNJ orphelins.
- **`ThreatManager` séparé du `CombatManager`** : la menace (ordering des cibles) est distincte du fait d'être en combat.
- **State machine "in combat / leaving combat / out of combat"** avec délai de sortie (5s sans dégât) — évite "PvE skip" out-of-combat instantané.
- **`DuelHandler` totalement séparé** du PvP général : duel = mode opt-in avec ses propres règles (zone bornée, timer, win-condition à 1 HP) — bon précédent pour "mode arène 1v1 amical" dans une ville.

**Reco LCDLLN** : À piocher — décomposition Combat/Threat/HostileRef canonique, à reprendre telle quelle côté shard dès le premier PNJ hostile.

### DBScripts

**Rôle** : Moteur d'événements scriptés piloté par DB : tables `*_scripts` (quest_start, quest_end, gameobject_use, event, spell) avec colonnes `(id, delay, command, data1, data2, target_type)`.

**Idées à piocher** :
- **DSL minimaliste à ~30 commandes** (TALK, MOVE_TO, KILL_CREDIT, CAST_SPELL, RESPAWN_GO, OPEN_DOOR…) : suffisamment expressif pour 95% du contenu PvE narratif sans embarquer Lua ou JS.
- **Délais entre commandes** (colonne `delay`) : permet des séquences chronométrées sans coroutines.
- **Targeting polymorphe** (`target_type` : self, nearest_creature, player, source) résolu au runtime — un même script réutilisable.
- **`ScriptMgrDefines.h` enum centralisée des commandes** : single source of truth.
- **Hot-reload via `.reload all_scripts`** GM command : itération sans restart serveur — feature critique pour les game designers.

**Reco LCDLLN** : À adapter — adopter "script DB + DSL court + hot-reload" si LCDLLN veut du contenu narratif ; commencer par 5-6 commandes et étendre. **Ignorer** si le contenu reste artisanal en C++ pendant le MVP.

### Entities

**Rôle** : Définit toutes les entités du monde simulé (Object → Unit → Player/Creature/Pet, plus GameObject, Item, Corpse, Transports) avec sérialisation réseau via UpdateFields/UpdateMask.

**Idées à piocher** :
- **UpdateFields + UpdateMask** : chaque entité possède un tableau de champs typés et un bitmask des champs dirty ; à chaque tick on n'envoie au client que le delta — pattern fondamental pour réduire la bande passante AoI.
- **Hiérarchie Object → WorldObject → Unit** avec ObjectGuid (type encodé dans les bits hauts) : permet un dispatch polymorphe propre et un GUID universel routeable entre master/shard.
- **TemporarySpawn** comme classe dédiée pour les entités à durée de vie limitée (invocations, projectiles persistants, summons) — évite de polluer le spawn pool persistant.
- **CreatureLinkingMgr** : système de liens "si A meurt alors B respawn / B aggro" décrit en data, pas en code.
- **Camera** comme entité de premier ordre dissociée du Player : permet possessions, vues à distance, replays.

**Reco LCDLLN** : À piocher — adopter UpdateFields/UpdateMask pour les snapshots AoI shard→client, gain réseau majeur.

### GameEvents

**Rôle** : Événements de monde planifiés (Hallow's End, Darkmoon Faire, phases lunaires) qui activent/désactivent spawns, quêtes et PNJ selon un calendrier.

**Idées à piocher** :
- **Activation par event_id** sur les spawns : un même monde DB porte plusieurs variantes saisonnières filtrées au load time.
- **Scheduler central** avec start/end timestamps et récurrence — un seul tick global décide ce qui est actif.
- **`moon.cpp` dédié** : phases lunaires calculées (pas tablées) et exposées comme condition réutilisable par les scripts.
- **Events en cascade** : un event peut en déclencher d'autres (parent/child).

**Reco LCDLLN** : À adapter — un GameEventMgr léger côté shard pour les events saisonniers, alimenté par une table master, suffit largement.

### GMTickets

**Rôle** : Tickets de support en jeu (joueur ouvre, GM voit la file, répond, ferme).

**Idées à piocher** :
- **Persistence simple** : un ticket = une row DB avec état (open/in_progress/closed), pas de file in-memory à reconstruire.
- **Handler dédié séparé du Mgr** : le réseau ne touche que le manager, le manager ne touche que la DB.
- **Notification asymétrique** : le joueur ne voit que son ticket, le GM voit la file globale — gating par flag de compte.

**Reco LCDLLN** : À ignorer pour l'instant — pas prioritaire avant le mid-game ; quand utile, mettre côté master.

### Globals

**Rôle** : Couche d'accès et de cache pour les données de référence (ObjectMgr = templates créatures/items/quêtes, ObjectAccessor = lookup runtime, Conditions, Locales, Graveyards).

**Idées à piocher** :
- **Conditions / UnitCondition / CombatCondition** : un mini-DSL data-driven pour exprimer des prédicats ("a tel buff", "est en groupe", "niveau ≥ X") réutilisé par quêtes, loots, scripts — évite mille `if` C++.
- **GraveyardManager** : table de points de respawn liée aux zones + faction, requêtable par "plus proche graveyard valide".
- **EnumFlag / FlagConvertibleEnum** : helpers C++ type-safe pour bitwise flags.
- **Locales** : strings localisées chargées en RAM par locale_id, fallback sur la locale par défaut.
- **ObjectAccessor** : façade thread-safe pour `GetPlayer(guid)`, évite que chaque système tienne sa propre map.

**Reco LCDLLN** : À piocher — système Conditions data-driven est un game-changer pour quêtes/loot/triggers.

### Grids

**Rôle** : Partitionnement spatial 2D du monde en cellules de taille fixe, base de l'AoI et du chargement dynamique. Implémente Visitor Pattern + Searchers + Workers + Notifiers.

**Idées à piocher** :
- **Visitor pattern templated** sur les types contenus (`Visit(GridRefManager<Creature>&)`, `Visit(GridRefManager<Player>&)`) : zéro virtual call dans le hot path, dispatch statique.
- **Searchers/Workers/Checks composables** : `WorldObjectSearcher<Check>` paramétré par un foncteur — on écrit `NearestAttackableUnitInObjectRangeCheck` une fois et on le passe à n'importe quel searcher.
- **Chargement/déchargement par grille** quand un joueur entre/sort d'une zone tampon — le monde n'est jamais entièrement résident.
- **GridStates (state machine)** : Loaded / Active / Idle / Removal — chaque cellule passe d'un état à l'autre selon la présence de joueurs, on n'update que les cellules "Active".
- **GridNotifiers pour broadcast spatial** : `MessageDistDeliverer` parcourt seulement les cellules dans le rayon — base de l'AoI réseau efficace.

**Reco LCDLLN** : À piocher massivement — squelette de toute simulation de monde MMO scalable, à adopter tel quel sur le shard.

### Groups

**Rôle** : Groupes/raids de joueurs (5 ou 40 membres), invitations, loot rules, ready check, marqueurs cibles.

**Idées à piocher** :
- **GroupReference / GroupRefManager** : un Player détient une `GroupReference` qui s'invalide automatiquement quand le groupe se dissout — pattern intrusive list évitant les dangling pointers.
- **Loot rules par groupe** (FFA, Round Robin, Master Looter, Need Before Greed) encodées comme enum + politique — ajouter une nouvelle règle = ajouter une stratégie.
- **Persistance partielle** : seuls les raids permanents sont en DB, les groupes 5-man sont in-memory volatiles.
- **Cross-shard ready** : un groupe est un objet master-side (logique sociale), les positions des membres viennent du shard — séparation naturelle pour LCDLLN.

**Reco LCDLLN** : À piocher — Group côté master (relayé via chat-relay), GroupReference côté shard pour le cache local.

### Guilds

**Rôle** : Guildes persistantes (membres, ranks, banque, MOTD, taxe, logs d'événements).

**Idées à piocher** :
- **Schéma DB** : `guild`, `guild_member`, `guild_rank`, `guild_bank_item`, `guild_bank_tab`, `guild_eventlog` — séparation propre rank/membre/banque, à reprendre presque tel quel.
- **Permissions par rank en bitmask** : chaque rank a un bitfield (kick, invite, withdraw_money, edit_motd…) ; ajouter une perm = ajouter un bit, pas une colonne.
- **Event log circulaire en DB** : table avec rotation par taille max, sert d'audit + d'UI client.
- **GuildMgr singleton** charge toutes les guildes au boot — acceptable car peu nombreuses.
- **Banque multi-onglets** avec permissions par onglet par rank — flexibilité forte pour peu de complexité.

**Reco LCDLLN** : À piocher — Guild typiquement master-side ; copier le schéma DB et le modèle de permissions bitmask.

### LFG

**Rôle** : Looking-For-Group / matchmaking : joueurs s'inscrivent à une file, le système forme des groupes équilibrés tank/heal/dps.

**Idées à piocher** :
- **LFGQueue séparée du LFGMgr** : la file (algo de matching) est isolée du manager (état joueur, téléport, récompenses) — testable à part.
- **Matching par rôles requis** : chaque slot d'un dungeon demande un set de rôles, l'algo cherche la plus petite combinaison qui remplit.
- **State machine joueur** : Idle / Queued / Proposal / Boot / InDungeon — transitions explicites évitent les bugs de "joueur fantôme".
- **Timeout proposals** : si un joueur ne confirme pas en N secondes, sa place est rendue à la file.

**Reco LCDLLN** : À adapter — LFG master-side (transverse aux shards), reprendre la séparation Queue/Mgr et la state machine joueur.

### Loot

**Rôle** : Génération et distribution de butin (créature tuée, coffre, pêche, skinning) à partir de tables pondérées + règles de groupe.

**Idées à piocher** :
- **Loot templates en DB** : `creature_loottemplate`, `gameobject_loottemplate`, `fishing_loottemplate`, `pickpocketing_loottemplate` — même schéma générique (entry, item, chance, group, mincount, maxcount) réutilisé partout.
- **Loot groups** : à l'intérieur d'une table, des items partagent un "group" qui garantit qu'exactement un drop (somme des chances = 100%) — sépare loot garanti et loot bonus.
- **Reference loot** : un template peut référencer un autre par convention (`item < 0` = ref) — DRY pour les loot tables partagées.
- **Conditions par drop** : un item ne tombe que si le looter remplit une Condition (quête active, classe, faction).
- **Round-robin / need-greed runtime** : la résolution dépend de la GroupLootMethod, pas du template — orthogonalité données/règles.

**Reco LCDLLN** : À piocher — schéma DB loot directement réutilisable, éprouvé sur des milliers de serveurs.

### Mails

**Rôle** : Messagerie asynchrone in-game (lettre, pièces jointes, COD, expiration, retour).

**Idées à piocher** :
- **MassMailMgr** : envoi en masse (events, GM broadcasts, retours auction house) avec batching DB pour éviter N inserts.
- **Schéma DB** : `mail` + `mail_items` (jointure) + expiration timestamps ; le client poll au login + poll périodique du nombre non lu.
- **Workflow COD (Cash On Delivery)** : pièce jointe libérée seulement après paiement, sinon retour à l'expéditeur — pattern transactionnel utile pour AH/trade asynchrone.
- **Expiration auto** avec tâche périodique qui retourne ou supprime les mails périmés.

**Reco LCDLLN** : À adapter — master-side (cross-shard naturel), schéma à reprendre ; reporter MassMail tant qu'on n'a pas d'AH/events.

### Maps

**Rôle** : Cœur de la simulation : une `Map` = une instance de continent/donjon, tick monothread, possède sa grille 2D, son MapUpdater (tâches parallèles légères), son SpawnManager, son InstanceData scriptable.

**Idées à piocher** :
- **Une Map = un thread logique** : tout le gameplay d'une instance tourne mono-thread, supprimant 90% des locks ; le parallélisme se fait entre maps via MapUpdater/MapWorkers.
- **DungeonMap / BattleGroundMap / WorldMap** : sous-classes spécialisées avec policies différentes (lifecycle, persistance, joueur cap) — éviter un Map monolithique avec 50 flags.
- **MapPersistentState** : les instances sauvegardées (raids loot lockés) ont un état DB séparé du runtime — recharge un raid après reboot avec progression intacte.
- **SpawnGroup** : groupes de spawns coordonnés (par ex. "patrouille de 3", "pop l'un OU l'autre") — data-driven, pas scripté.
- **ObjectPosSelector** : trouve une position libre autour d'un point sans collision — utile pour summons, loot piles, téléports de groupe.
- **TransportSystem** : un transport est une mini-Map mobile avec ses propres coordonnées locales — pattern pour véhicules/bateaux scriptables.

**Reco LCDLLN** : À piocher — modèle "Map = thread" est exactement ce qu'il faut côté shard ; SpawnGroup et MapPersistentState sont des wins faciles.

### MotionGenerators

**Rôle** : Pile de générateurs de mouvement (idle/random/waypoint/chase/follow/path) gérée par MotionMaster sur chaque Unit, avec pathfinding Detour (MoveMap = navmeshes).

**Idées à piocher** :
- **MotionMaster = `std::stack<MovementGenerator*>`** : pousser un Chase pendant un combat, pop quand il finit, le Random idle reprend automatiquement — état comportemental sans state machine explicite.
- **Cleanup différé via flags** (`MMCF_UPDATE`) : si on Clear() pendant un Update(), on marque pour cleanup au prochain tick — évite les use-after-free classiques.
- **MoveMap (navmeshes Detour)** chargés par tile à la demande, partagés entre toutes les créatures de la map.
- **MoveSplineInit (builder pattern)** : on configure une spline (vélocité, flags vol/marche, points), puis Launch() — API fluente.
- **WaypointManager + DB** : patrouilles décrites en data (creature_movement, creature_movement_template), pas en C++.
- **TargetedMovementGenerator** templated par cible : Chase et Follow partagent 95% du code via template, divergent sur l'offset/comportement.

**Reco LCDLLN** : À piocher en priorité — la stack de générateurs + pathfinder Detour est le modèle d'IA de mouvement le plus solide.

### Movement

**Rôle** : Couche bas-niveau des splines de mouvement réseau : interpolation Catmull-Rom/Bézier, sérialisation des paquets de mouvement client/serveur, flags (run/walk/fly/swim).

**Idées à piocher** :
- **Spline catmull-rom server-authoritative** : le serveur calcule la trajectoire complète et envoie les control points + durée ; le client interpole localement et reste synchrone — réduit drastiquement la bande passante vs streaming de positions.
- **MoveSplineFlag** bitmask (Walk, Flying, NoSpline, Falling, Cyclic, EnterCycle, Frozen…) : un seul type de paquet sert tous les modes de déplacement.
- **packet_builder isolé** : la sérialisation est une couche pure (in: MoveSpline, out: bytes), testable sans serveur, et changeable si on bump le protocole.
- **spline.impl.h templated** : math des splines en header-only template, instanciable pour 2D/3D/4D selon besoin.
- **typedefs.h centralisé** : tous les `using Vector3 = ...` dans un seul fichier — facilite un swap futur.

**Reco LCDLLN** : À piocher — modèle "serveur envoie spline, client interpole" exactement ce qu'il faut pour un MMORPG Vulkan moderne.

### OutdoorPvP

**Rôle** : PvP en zones ouvertes (capture de tours, contrôle de territoires) avec un manager central et une implémentation par zone.

**Idées à piocher** :
- **Pattern Manager + plugin par zone** : `OutdoorPvPMgr` enregistre des `OutdoorPvP*` polymorphes au démarrage et dispatch les events `OnPlayerEnterZone`/`OnGameObjectCreate`. Réutilisable pour tout système événementiel à scopes géographiques.
- **State machine par objectif** : chaque tour/banner = mini-FSM (neutre → capture en cours → capturé) avec timer et progression.
- **WorldState broadcast** : compteurs de score diffusés via variables nommées à tous les clients de la zone — "shared scoreboard" sans payload custom.
- **Découplage triggers/règles** : déclencheurs (entrée AreaTrigger géographique) séparés de la logique de scoring — data-driven via DB.

**Reco LCDLLN** : À ignorer pour le MVP, à adapter plus tard — garder le pattern Manager+ZonePlugin pour des futurs events scriptés.

### PlayerBot

**Rôle** : Bots IA contrôlant des persos joueurs avec sous-modules `AI/` (logique décisionnelle) et `Base/` (intégration WorldSession factice).

**Idées à piocher** :
- **WorldSession headless** : un bot = vraie `WorldSession` sans socket, branchée directement sur les handlers serveur. Permet de tester les opcodes sans client réel — idéal pour load testing du shard.
- **Behavior tree / strategy pattern** : `AI/` utilise des stratégies composables (combat, follow, loot, quest) activables/désactivables runtime.
- **Config par bot via fichier** : `playerbot.conf.dist.in`, sans recompiler.
- **Réinjection des paquets côté serveur** : le bot évite tout sérialisation réseau, ce qui réduit énormément le coût CPU/RAM par client simulé (10x à 100x plus de bots qu'un vrai client).

**Reco LCDLLN** : À adapter — implémenter un mode `--headless-bot` sur le shard (pas un binaire séparé) qui spawn N `PlayerSession` factices pour stress test ; ne pas porter l'IA TBC.

### Pools

**Rôle** : Spawns aléatoires groupés (un pool de 10 mobs/objets, dont seuls X sont actifs simultanément, sélectionnés par poids).

**Idées à piocher** :
- **Spawn pool pondéré** : table `pool_template` (max_limit) + `pool_creature`/`pool_gameobject` avec `chance`. Permet rareté contrôlée (rare spawns) sans logique custom par mob.
- **Pools imbriqués** : un pool peut contenir d'autres pools (via `pool_pool`), hiérarchies "zone → sous-zone → mob".
- **Sélection cryptographiquement non biaisée** : alias method ou roulette wheel selon poids.
- **Persistance par instance** : pool_state en mémoire par map instance, reset au respawn timer.

**Reco LCDLLN** : À piocher — schéma DB directement réutilisable ; ~500 lignes côté shard suffisent.

### Quests

**Rôle** : Définition statique des quêtes (`QuestDef`) et handler des opcodes (accept, abandon, complete, reward).

**Idées à piocher** :
- **Séparation static def vs dynamic state** : `Quest` (immuable, chargé DB une fois) vs `QuestStatusData` (par joueur, en RAM/DB). Pattern essentiel pour économiser la RAM.
- **Objectifs polymorphes encodés en arrays parallèles** : `RequiredItemId[4]`, `RequiredCreatureId[4]` (négatif = GO), `RequiredSpellCast[4]`. Un peu old-school mais ultra-data-driven.
- **Flags bitmask pour variantes** : `DAILY`, `WEEKLY`, `REPEATABLE`, `AUTOCOMPLETE` cohabitent sur un même `Quest` → un seul type au lieu de classes dérivées.
- **Reward avec choix client** : 6 "choice items" + 4 "given items". Le client choisit ; le serveur valide.
- **Chaînes via PrevQuestId/NextQuestId** : graphe orienté résolu au load, pas de récursion runtime.

**Reco LCDLLN** : À adapter — split `QuestTemplate`/`QuestStatus`, mais simplifier à 1 seul tableau d'objectifs hétérogènes (`std::variant<KillObjective, CollectObjective, …>`) — plus C++20 idiomatique que des arrays parallèles.

### Reputation

**Rôle** : Réputation joueur ↔ factions (`ReputationMgr` par player), niveaux (Hostile → Exalted), modificateurs raciaux/quête, faction templates.

**Idées à piocher** :
- **Faction template DB** : matrice (faction_a × faction_b) → at_war/can_attack/can_assist, lookup O(1). Élégant pour gérer hostilités sans coder par paire.
- **Bitmask flags par faction** : `AT_WAR`, `VISIBLE`, `INACTIVE`, `HIDDEN` cohabitent sur un seul `uint8`.
- **Paliers calculés, pas stockés** : seul le "rep total" est persisté ; le rang est dérivé via `ReputationToRank()`. Évite désync DB.
- **Spillover via faction parent** : gain dans une faction enfant remonte au parent via `ParentFactionId` + ratio.
- **Réplication delta** : seules les factions modifiées sont envoyées au client (`SendStateChanged`).

**Reco LCDLLN** : À piocher si factions PvE/PvP prévues — concept de faction template (matrice de relations) et "stocke le total, dérive le rang" directement applicables.

### Server

**Rôle** : Plomberie réseau et accès aux données : `WorldSocket` (TCP+chiffrement), `WorldSession` (état par client), `Opcodes` (table d'opcodes), DBC stores, `WorldPacket` (sérialisation).

**Idées à piocher** :
- **Table d'opcodes typée** : `Opcodes.cpp` = un gros tableau `{opcode, name, status, processing, handler}`. Permet logging unifié, throttling par opcode, et stats. Modèle direct pour notre `OpcodeRegistry`.
- **WorldPacket = ByteBuffer wrapper** : `<<` pour write, `>>` pour read, validation auto de la taille.
- **AuthCrypt par session** : RC4 dérivé de la session key. Pour LCDLLN sur TLS/Noise — pattern "clé symétrique unique par session, dérivée du handshake" est le standard.
- **PacketLog pour replay** : capture binaire de toutes les frames, rejouable offline. Inestimable pour debug protocole et reproduire des bugs joueurs.
- **DBCStores** : préchargement RAM-mapped des fichiers de données statiques au boot, lookup O(1) via id.

**Reco LCDLLN** : À piocher — `PacketLog` rejouable est un must-have pour debug le protocole UDP gameplay (master+shard), implémenter un dump binaire toggle via config.

### Skills

**Rôle** : Sous-systèmes liés au craft : découverte aléatoire de recettes (SkillDiscovery) et items bonus au craft (SkillExtraItems).

**Idées à piocher** :
- **Discovery table data-driven** : `skill_discovery_template` (spell_required, spell_discovered, chance) → unlock probabiliste de recettes via crafting.
- **Extra item proc** : `skill_extra_item_template` (chance, items_per_craft) appliqué post-craft.
- Tout en SQL : zéro hardcoded.

**Reco LCDLLN** : À ignorer pour MVP — pas de craft prévu court terme ; garder le pattern "hooks data-driven post-action" en tête.

### Social

**Rôle** : Listes amis/ignorés/notes par joueur (`SocialMgr`), notifications de connexion, broadcast de status.

**Idées à piocher** :
- **Manager singleton + objet par joueur** : `PlayerSocial` chargé à login, déchargé à logout, persisté en DB delta.
- **Broadcast de présence** : login/logout d'un joueur déclenche `BroadcastToFriends()` qui pousse à tous les amis online. Pour LCDLLN typiquement master-side.
- **Liste ignorés appliquée côté chat-relay** : filtrage à l'expéditeur, pas au récepteur — économie réseau quand un joueur ignore beaucoup de monde.
- **Notes privées par contact** : champ texte attaché au mapping (ami → note).

**Reco LCDLLN** : À piocher côté master — `FriendsManager` à ajouter à AccountStore/SessionManager, intégré au chat-relay existant. ~300 lignes.

### Spells

**Rôle** : Cœur du gameplay magique : cast (`Spell`), effets (`SpellEffects`), auras/buffs (`SpellAuras`), proc events, stacking, ciblage.

**Idées à piocher** :
- **Spell = state machine** : `SPELL_STATE_PREPARING` → `CASTING` → `FINISHED` avec timers (cast time, GCD, channel). Modèle pour toute action joueur à durée non-instantanée (récolte, build).
- **Aura proc system** : `SpellProcEventEntry` (procFlags bitmask, ppmRate, cooldown) déclenche effects sur events (melee hit, spell crit). Pattern "réactivité data-driven" applicable à tout système de buffs/talents.
- **SpellFamily + ClassFamilyMask** : permet "ce talent buffe tous les sorts feu du mage" sans énumérer chaque sort.
- **Targeting matrix** : `SpellTargetInfoTable` mappe chaque target type → filtres. Centralise la logique au lieu de switch géants.
- **Stacking rules séparées** : `SpellStacking.cpp` isolé permet de raisonner sur l'algèbre des buffs (replace, refresh, sum) indépendamment des effects.
- **Inline + early-exit** : checks chauds (`IsPassive`, `IsPositive`) inlinés.

**Reco LCDLLN** : À piocher fortement — pour le futur système d'abilities/sorts, copier la séparation `Spell` (instance) / `SpellTemplate` (def DB) / `SpellAura` (effet appliqué) / `ProcEvent` (réactivité). Ne pas porter de code, mais le découpage est canonique.

### Tools

**Rôle** : Utilitaires d'administration : nettoyage DB des persos supprimés, dump/restore (transfert serveur), formules game (XP/level), table de strings localisées.

**Idées à piocher** :
- **PlayerDump format texte** : sérialisation human-readable d'un perso entier (perso + items + quêtes + skills + …) → restorable sur un autre shard. Pattern de migration inter-shard très propre.
- **CharacterDatabaseCleaner périodique** : scan des orphelins (item sans owner, mail sans expéditeur) en tâche async.
- **Formulas.h centralisé** : XP per kill, money loot, level penalty… une seule source de vérité, facile à équilibrer.
- **Language.h** = table de strings : i18n côté serveur (broadcasts, commandes GM).

**Reco LCDLLN** : À piocher — `PlayerDump` est exactement ce qu'il faut pour migrer des persos entre shards (futur split shard EU/NA). Implémenter un opcode admin master-side qui dump → JSON → restore.

### Trade

**Rôle** : Handler du trade fenêtre-à-fenêtre entre 2 joueurs (proposer/retirer items, gold, accepter, valider).

**Idées à piocher** :
- **State machine 2-phase commit** : both-must-accept avant exécution, un changement quelconque reset les checkboxes.
- **TradeData attachée au Player** : chaque joueur a un `TradeData*` (null si pas en trade), évite gros maps globales.
- **Validation atomique côté serveur** : le client envoie "j'accepte", le serveur revérifie inventaire + gold + bag space pour les deux côtés en une transaction DB, rollback si échec.
- **Anti-scam** : fenêtre de 6s avant validation finale après dernier changement, prévient le swap d'items en dernière seconde.

**Reco LCDLLN** : À adapter — garder 2-phase commit + revérification serveur, faire le trade via le master (déjà gère les inventaires).

### VoiceChat

**Rôle** : Module quasi-vide — squelette client TBC pour voice (jamais réellement implémenté en open-source).

**Idées à piocher** :
- **Stub minimaliste** : montre comment câbler un opcode officiel sans backend réel.
- **Séparation voice ↔ chat texte** : Blizzard a toujours gardé voice hors du protocole gameplay (relais externes type Vivox).

**Reco LCDLLN** : À ignorer — pas de voice prévu ; si un jour besoin, intégrer un service externe (LiveKit, Mumble) plutôt que sur master/shard.

### Weather

**Rôle** : Système météo par zone (pluie, neige, brouillard) avec transitions probabilistes. Diffusé en SMSG_WEATHER aux clients de la zone.

**Idées à piocher** :
- **Markov chain par zone** : table `game_weather` (zone, season, type, chance) → chaque tick rotation aléatoire pondérée. Très peu de code, beaucoup d'ambiance.
- **Server-authoritative météo** : le serveur décide, broadcast à tous les clients de la zone → cohérence multi-joueurs ("regarde la pluie là-bas"). Crucial vs météo client-locale.
- **Update lazy** : pas de tick haute fréquence, juste un timer 10min par zone occupée, désactivé si zone vide.
- **Transition douce** : `grade` de 0.0 à 1.0 envoyé au client pour fade in/out, pas de cuts brutaux.

**Reco LCDLLN** : À piocher si météo prévue — implémentation directe sur shard, ~200 lignes, broadcast via l'AoI existant.

### World

**Rôle** : `World` = singleton orchestrateur du tick serveur global (boucle principale, broadcast système, timers, shutdown). `WorldState` = machine à variables nommées partagées. `WorldStateExpression` = mini-DSL pour conditions data-driven.

**Idées à piocher** :
- **Mini-DSL d'expressions** : `WorldStateExpression` parse des formules ("worldstate_42 > 100 AND worldstate_43 == 1") évaluables runtime depuis DB. Énorme leverage éditorial : conditions de quêtes/events sans recompiler.
- **WorldStateVariableManager** : registre clé→valeur typé, observable (subscribe par zone/joueur), persistable. Pattern "shared blackboard" excellent pour events serveur-wide.
- **Tick global multi-niveaux** : World tick → Map tick → Object tick, chacun à fréquence propre (50ms / 100ms / 1s). Modèle hiérarchique pour maîtriser le coût CPU.
- **Shutdown gracieux** : broadcast countdown 5min/1min/30s, kick safe, flush DB.

**Reco LCDLLN** : À piocher fort — le `WorldStateExpression` (mini-DSL conditions) est un game-changer pour un éditeur monde data-driven (cf. `lcdlln_world_editor.exe`) : permet de scripter des triggers sans C++.

### vmap

**Rôle** : Collision et raycast 3D contre la géométrie statique du monde (WMO buildings, doodads). Charge des tiles `.vmap` extraits du client, BSP/BIH par tile, reference-counted.

**Idées à piocher** :
- **BIH (Bounding Interval Hierarchy)** : structure d'accélération raycast plus compacte qu'un BVH classique, idéale pour scènes statiques. Bonne alternative à un kd-tree.
- **Tile streaming + refcount** : `ManagedModel` charge/décharge les `.vmap` selon présence joueurs dans la zone (acquireModelInstance/release). Évite tout charger en RAM, scale à des mondes massifs.
- **Trois requêtes canoniques** : `isInLineOfSight()`, `getHeight(maxSearchDist)`, `getObjectHitPos()`. Couvrent 95% des besoins gameplay (LOS spell, pathing terrain, projectile).
- **DynamicTree séparé** : la géométrie dynamique (portes, GO transformables) vit dans un BSP séparé, fusionné aux requêtes statiques. Permet d'ouvrir/fermer une porte sans rebuild du tile entier.
- **TileAssembler offline** : conversion lourde (.wmo → .vmap optimisé) faite à l'extraction, pas au runtime. Pattern "pré-bake tout ce qui peut l'être".

**Reco LCDLLN** : À piocher fortement — le shard a besoin d'un système de collision LOS+height ; copier l'architecture (tile streaming + BIH/BVH + DynamicTree pour doors). Probablement le module de cmangos avec la plus grosse valeur réutilisable directement pour LCDLLN.

---

**Déploiement** : ✅ analyse documentaire pure, aucun code modifié — pas de redéploiement serveur.
