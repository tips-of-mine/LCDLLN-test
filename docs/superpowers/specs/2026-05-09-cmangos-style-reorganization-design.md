# Réorganisation cmangos-style — Design

**Date :** 2026-05-09
**Branche cible :** `claude/reorg-cmangos-style`
**Scope :** réorganiser la racine du repo pour calquer la structure de [cmangos/mangos-tbc/src](https://github.com/cmangos/mangos-tbc/tree/master/src), en respectant la convention sous-dossier-par-domaine. Préserver le CI/CD existant et les artifacts produits.

## Objectif

Aujourd'hui, `engine/` mélange ~600 fichiers C++ pour 5 binaires distincts : client (`engine_app`), world editor (`world_editor_app`), master server Linux, shard server Linux, sandbox Windows. La séparation entre code client et code serveur est faible (sous-dossiers `engine/client/`, `engine/server/`, `engine/render/`, `engine/network/`, `engine/auth/`, `engine/core/`, etc. à plat sous `engine/`).

L'objectif : adopter la structure cmangos `src/{shared, client, masterd, shardd, world_editor}` avec un sous-dossier par domaine (ex. `src/shared/log/`, `src/shared/metric/`, `src/masterd/handlers/`, `src/shardd/combat/`). Casing **lowercase** pour minimiser les renommages d'`#include`. CI/CD préservé.

## Périmètre exclu

- Pas de refactor de code C++ (pas de changement de signatures, pas de split de fichiers gros). Mouvement de fichiers + ajustement `#include` + CMake uniquement.
- Pas de migration de `web-portal/` (Next.js, séparé du C++).
- Pas de delete de fichiers — tout ce qui est legacy va dans `legacy/`.

## Architecture cible

### 1. Top-level (racine du repo)

```
src/                   ← code C++
sql/                   ← RENOMMÉ depuis db/
external/              ← inchangé (deps vendored)
assets/                ← inchangé
game/                  ← inchangé (data runtime + shaders)
deploy/                ← inchangé
docs/                  ← inchangé
design/                ← inchangé
scripts/               ← inchangé
tools/                 ← inchangé
tickets/               ← inchangé
web-portal/            ← réorganisé en interne (cf. section 7)
legacy/                ← NOUVEAU : "Editeur d'univers/" + "Editeur de monde/" déplacés ici
.github/               ← workflows ajustés (paths)
CMakeLists.txt         ← refactoré pour utiliser src/
```

### 2. `src/shared/` (sources d'`engine_core`, lib statique)

> **Note** : `Engine.h/cpp` n'est PAS dans `shared/` car il porte la boucle client/world_editor avec un flag `m_worldEditorExe`. Il vit dans `src/client/app/Engine.cpp` et est sourcé par les 2 targets (`engine_app`, `world_editor_app`).

```
src/shared/
  core/         Log, LogConfig, Config, asserts
  auth/         Argon2, password hashing, SRP, TLS helpers
  network/      NetClient, NetServer, ByteReader/Writer, PacketBuilder, payloads
  db/           ConnectionPool, DbHelpers, MigrationRunner
  math/         vector, matrix
  platform/     FileSystem, OS abstraction
  metric/       MetricRegistry
  messager/     Messager MPSC queue
  packetlog/    PacketLog ring buffer
  formulas/     XP, aggro, drop, HP formulas
  util/         ServerUtil RNG, parse, format helpers
  net/          chat/ChatSystem.cpp, chat/ChatEmotes.cpp (legacy "net" dir)
```

### 3. `src/client/` (Windows client = `engine_app`)

```
src/client/
  main.cpp                  entry point (ex-engine/main.cpp)
  app/                      Engine.cpp main loop, AuthUi.h
  auth/                     screens/ (login, register, character_select, etc.) presenters
  render/                   Vulkan passes, AuthImGuiRenderer, ChatImGuiRenderer
  audio/
  gameplay/                 HUD, inventory, chat, combat UI
  world/                    terrain client, props, lighting
  net/                      gameplay UDP client, master/shard client
```

### 4. `src/masterd/` (master server Linux)

```
src/masterd/
  main_linux.cpp            entry (ex-main_server_linux.cpp)
  app/                      ServerApp lifecycle
  handlers/                 AuthRegister, Character*, ChatRelay, Terms, PasswordReset, ShardRegister, ShardTicket, ServerList
  account/                  InMemoryAccountStore, MysqlAccountStore, AccountValidation, AccountRole, AccountRoleService
  session/                  SessionManager, ConnectionSessionMap, SessionCharacterMap
  chat/                     ChatSanitizer, ChatGate, ChatChannelRegistry, ChatCommandRouter
  mail/                     MailManager, MysqlMailStore
  gmtickets/                GmTicketSystem, MysqlGmTicketStore
  social/                   IgnoreList, MysqlIgnoreStore
  reputation/               ReputationManager, MysqlReputationStore
  quests/                   QuestStateTracker, MysqlQuestStateStore
  lfg/                      LfgQueue
  events/                   GameEventManager
  shards/                   ShardRegistry, ServerRegistry, ShardTicketCrypto, ShardTicketValidator
  terms/                    TermsRepository
  email/                    SmtpMailer, LocalizedEmail
  security/                 RateLimitAndBan, SecurityAuditLog, UserRateLimiter, CaptchaVerifier, BotDetector, ConnectionDDoSProtector
  world/                    WorldStateRegistry, WorldClock
  metrics/                  HealthEndpoint, PrometheusMetrics
```

### 5. `src/shardd/` (shard Linux + sandbox WIN32)

```
src/shardd/
  main_linux.cpp            entry shard Linux (ex-main_shard_linux.cpp)
  main_win.cpp              entry sandbox Windows (ex-main_server.cpp)
  app/                      ServerApp shard
  combat/                   ThreatList, AntiCheatGameplay
  ai/                       EventAI, MotionGeneratorStack
  spell/                    SpellFamilyMask
  loot/                     LootTable
  pools/                    PoolManager
  maps/                     InstanceManager
  dbscripts/                DBScript VM
  weather/                  WeatherManager
  outdoorpvp/               OutdoorPvPManager
  arena/                    ArenaTeam
  battleground/             BattleGroundQueue
  trade/                    TradeSession, TradeSystem
  cinematics/               CinematicSequence
  gameplay/                 QuestRuntime, SpawnerRuntime, EventRuntime, GatheringSystem, CraftingSystem, AuctionHouse, GuildSystem*, FriendSystem, PartySystem, CharacterPersistence, VendorCatalog, CurrencyConfig, PlayerWalletService
  net/                      UdpTransport, TickScheduler, ServerProtocol shard side
  world/                    GridState, SpatialPartition, ZoneTransitions, LagCompensation
  guild/                    GuildPermissionMatrix
  skills/                   SkillBook
  playerbot/                PlayerBotProfile
  auction/                  AuctionHouseBot
```

### 6. `src/world_editor/` (Windows world editor = `world_editor_app`)

```
src/world_editor/
  main.cpp                  entry (--world-editor)
  app/                      WorldEditorShell
  ui/                       ImGui panels (ToolProperties, Layers, etc.)
  tools/                    terrain editing, prop placement, lake/river tools
  io/                       map serialization
  tests/                    TerrainChunkTests, SplatMapTests, etc.
```

### 7. `web-portal/` (Next.js 13+ portal — 4e app)

**Placement** : reste top-level (séparé de `src/` qui est C++). Next.js attend une structure projet à la racine (`package.json`, `next.config.mjs`, etc.). Le placer sous `src/` confondrait CMake.

**Réorganisation interne** par domaine (cmangos-style) :

```
web-portal/
  app/                            Next.js app router (kept structure)
    admin/                        admin pages (acceptances, bugs, cgu, faq, players, roadmap)
    api/                          API routes par domaine
      auth/                       login, logout, session
      admin/                      admin endpoints
      bugs/                       bug reports
      health/                     healthcheck
      password-recovery/
      player/
    player/                       player pages (account, cgu, chronicles, exploits, parental, privacy, recovery-profile, security)
    bugs/, cgu/, contact/, login/, password-recovery/, roadmap/, support/
    layout.tsx, page.tsx, globals.css

  components/                     SPLIT BY DOMAIN
    auth/                         AccountForm, PasswordChangeForm, PasswordRecoveryRequestForm, ResetPasswordForm
    bugs/                         BugReportForm
    cgu/                          CguAcceptButton
    character/                    CharacterDeleteButton
    exploits/                     ExploitsProfile
    player/                       PrivacyForm, RecoveryProfileForm
    layout/                       SiteHeader, HeaderActions
    admin/                        BugAdmin, CguManager, FaqAdmin, PlayerActions (déjà groupés)

  lib/                            SPLIT BY DOMAIN
    db/                           db.ts → db/connection.ts (MySQL pool)
    email/                        email.ts → email/sender.ts (intégration Nodemailer)
    auth/                         gamePasswordHash.ts, passwordRecovery.ts, portalLogin.ts, session.ts
    exploits/                     exploitTier.ts, exploitsData.ts

  email-templates/                kept (référencé par lib/email)
  nginx/                          kept (config reverse proxy déploiement)
  middleware.ts                   kept (Next.js middleware racine)
  Dockerfile, package.json, tsconfig.json, etc. kept
```

**Pourquoi `app/api/admin/` ET `components/admin/` séparés** : convention Next.js 13+ qui attend les API handlers sous `app/api/<domain>/route.ts`, et les composants React sous `components/<domain>/<Name>.tsx`. On respecte la convention Next.js et on calque la même organisation par domaine des deux côtés.

**Imports TypeScript** : Next.js utilise des paths absolus via `tsconfig.json` (`@/lib/auth/session`, `@/components/auth/AccountForm`). À mettre à jour dans `tsconfig.json` après le déplacement.

## CMake

- Top-level `CMakeLists.txt` reste à la racine, mais délégue : `add_subdirectory(src)`.
- `src/CMakeLists.txt` (NOUVEAU) appelle `add_subdirectory(shared)`, `add_subdirectory(client)`, `add_subdirectory(masterd)`, `add_subdirectory(shardd)`, `add_subdirectory(world_editor)`.
- `src/shared/CMakeLists.txt` produit la lib statique `engine_core` (linkée par tous).
- Chaque `src/<app>/CMakeLists.txt` déclare son `add_executable`.
- Le helper `lcdlln_add_simple_test()` reste défini dans `src/shared/CMakeLists.txt` et est utilisable depuis n'importe quel sous-CMakeLists.

### Targets exe (clarification)

Le repo actuel a **un seul** `add_executable(server_app)` avec une branche `if (WIN32)` / `elseif(UNIX)` (sources différentes par plateforme). C'est ambigu : `server_app` est le master sur Linux, mais le shard-sandbox sur Windows. La nouvelle organisation rend la dualité explicite via des targets distincts :

| Plateforme | Cible CMake | Entry point | Rôle |
|---|---|---|---|
| Linux | `master_app` | `src/masterd/main_linux.cpp` | Master Linux |
| Linux | `shard_app` | `src/shardd/main_linux.cpp` | Shard Linux |
| Windows | `shard_sandbox_app` | `src/shardd/main_win.cpp` | Sandbox Windows (gameplay test) |
| Windows | `engine_app` | `src/client/main.cpp` | Client de jeu |
| Windows | `world_editor_app` | `src/world_editor/main.cpp` | Éditeur monde |

**Important pour le CI** : le target `engine_app` reste nommé `engine_app` (artifact `windows-release-<sha>` cherche `engine_app.exe`). Le target `master_app` (Linux) remplace l'ancien `server_app` Linux ; le bundle Docker doit être ajusté pour copier `master_app` au lieu de `server_app`.

## CI/CD (préservation des artifacts)

### `.github/workflows/build-linux.yml`

- `cmake --preset linux-x64-release` : preset top-level inchangé, fonctionne tel quel.
- Step "Assembler deploy/docker" : path `deploy/docker` inchangé.
- Step "Pack Docker bundle" : utilise `pack-linux-docker-bundle.sh` qui doit être mis à jour pour pointer vers `sql/migrations/` au lieu de `db/migrations/`.
- Artifact uploadé : `lcdlln-docker-linux-<sha>` (nom inchangé).

### `.github/workflows/build-windows.yml`

- `cmake --preset vs2022-x64` : preset top-level inchangé.
- `add_executable(engine_app)` et `add_executable(world_editor_app)` : continuent à exister sous les mêmes noms cibles, leurs sources viennent désormais de `src/client/` et `src/world_editor/`.
- Step "Collect artifacts" : copie depuis `build/.../bin/engine_app.exe` et `world_editor_app.exe` — paths CMake output inchangés.
- Step "Compile GLSL to SPIR-V" : path `game/data/shaders/` inchangé.
- Artifact uploadé : `windows-release-<sha>` (nom inchangé).

### Scripts à mettre à jour

- `scripts/sync-db-to-docker-deploy.sh` : `db/` → `sql/`.
- `scripts/pack-linux-docker-bundle.sh` : `db/` → `sql/`.
- `scripts/sync-web-portal-to-docker-deploy.sh` : path `$SRC` reste `web-portal/` (pas de move). Aucun changement requis tant que le top-level reste `web-portal/`.
- `MigrationRunner` (config default `db.migrations_path` reste `db/migrations` mais le code lit la config depuis `config.json` ; on peut soit changer le default à `sql/migrations`, soit garder le default et changer le `config.json`). Choix : changer le default à `sql/migrations`, simpler.

### Web-portal CI/CD

Le portail Next.js n'a **pas de workflow GitHub Actions dédié**. Il est packagé dans le bundle Docker Linux via `scripts/sync-web-portal-to-docker-deploy.sh` (copie `web-portal/` → `deploy/docker/web-portal/`), puis le `docker compose` build l'image au déploiement. La réorganisation interne de `web-portal/` n'affecte pas le workflow CI tant que la racine `web-portal/` (et ses fichiers `Dockerfile`, `package.json`, `next.config.mjs`) reste à la même place.

Tests post-réorg :
- `npm run build` doit passer dans `web-portal/` (vérifie les imports TypeScript après split de `lib/` et `components/`).
- L'image Docker doit builder via `docker compose build web-portal` (locally testable).

## Stratégie de migration (3 commits)

### Commit 1 — moves only (`git mv` massif)

- Tous les fichiers déplacés via `git mv` (préserve l'historique git, le diff montre des renommages).
- Aucun `#include` modifié, aucun `CMakeLists.txt` modifié.
- **Le repo ne compile pas** après ce commit (intentionnel). Permet une review du diff move-only.
- ~600 fichiers déplacés.

### Commit 2 — `#include` + CMakeLists

- `sed` sur tous les fichiers C++/headers : `#include "engine/<old>/X.h"` → `#include "src/<app>/<new>/X.h"` ou `#include "src/shared/<new>/X.h"`.
- Refactor `CMakeLists.txt` racine : remplace les listes de sources `engine/...` par `add_subdirectory(src)`.
- Crée `src/CMakeLists.txt` + 5 `src/<app>/CMakeLists.txt`.
- **Le repo recompile** Linux + Windows à la fin de ce commit.
- Tests passent.

### Commit 3 — CI + scripts + CODEBASE_MAP

- Update `.github/workflows/build-linux.yml` : `db/` → `sql/` dans les steps.
- Update `.github/workflows/build-windows.yml` : paths Vulkan SDK pour shaders, paths artifacts (devraient être inchangés mais à verifier).
- Update `scripts/sync-db-to-docker-deploy.sh`, `scripts/pack-linux-docker-bundle.sh`.
- Update `CODEBASE_MAP.md` : sections 1, 5, 6, 12 (web-portal), et "Aide-mémoire" pour refléter la nouvelle structure. Préserver les sections 2/3 (flux d'auth) et 13-17 (UX iterations).

### Commit 4 — réorganisation interne web-portal

- `git mv` des composants vers `components/<domain>/`.
- `git mv` des helpers `lib/` vers `lib/<domain>/`.
- Update tous les imports TypeScript (`@/lib/db` → `@/lib/db/connection`, `@/components/AccountForm` → `@/components/auth/AccountForm`).
- `npm run build` doit passer.
- Test : `docker compose build web-portal` build sans erreur.

## Risques identifiés

1. **Volume de moves** : ~600 fichiers, risque de perdre des fichiers ou de mal classer. **Mitigation** : script bash qui liste chaque move avant exécution, dry-run avant `git mv` réel.
2. **`#include`** : ~3000 occurrences à modifier. **Mitigation** : `sed` automatisé avec mapping path-old → path-new explicite, validé par compilation Linux + Windows en CI.
3. **`engine/Engine.cpp`** est compilé par 2 targets (`engine_app` et `world_editor_app`) avec un flag `m_worldEditorExe`. **Mitigation** : `Engine.cpp` vit dans `src/client/app/`, sourcé par les 2 targets.
4. **`ServerProtocol.cpp`** partagé master + shard + client. **Mitigation** : vit dans `src/shared/network/ServerProtocol.cpp`.
5. **CI Windows step "Collect artifacts"** copie depuis paths spécifiques au build dir CMake. **Mitigation** : CMake output dirs inchangés (toujours `build/<preset>/`), donc le step continue à fonctionner sans modification.
6. **`db/` → `sql/`** : tout binaire en production qui lit `db/migrations` casse au prochain boot si le rename n'est pas synchronisé avec le déploiement. **Mitigation** : commit 3 update default + CI ; le déploiement Linux doit ré-installer le binaire ET le dossier `sql/` en lock-step.

## Tests

- **Build Linux + Windows** doivent passer sur le commit 2 (avant CI workflow update) et commit 3.
- **Tous les tests existants** (~30 test executables CMake) doivent continuer à passer sans modification de leur logique.
- **Smoke test deploy** : le bundle Docker Linux produit par CI doit démarrer le master et appliquer les migrations sql/.

## CODEBASE_MAP.md update

Update les sections suivantes :
- Section 1 (Vue d'ensemble architecturale) : nouvelle structure `src/{shared,client,masterd,shardd,world_editor}`.
- Section 5 (Couche serveur — fichiers clés) : paths `src/masterd/...` et `src/shardd/...`.
- Section 6 (Couche rendu Vulkan) : paths `src/client/render/...`.
- Section 8 (Configuration et build) : noter `add_subdirectory(src)`.
- Section 9 (Base de données) : path `sql/migrations/`.
- Section 10 (Outils et CI) : workflow paths mis à jour.
- "Aide-mémoire : comment trouver un écran" : paths `src/client/auth/screens/` au lieu de `engine/client/auth/screens/`.

Sections **préservées telles quelles** (contenu inchangé, seuls les paths) :
- Sections 2, 3 (flux d'auth, règles de lecture).
- Sections 7 (Localisation), 11 (Tickets), 12 (Web portal).
- Sections 13-17 (Tweaks login, 3rd person, menu pause, race select, world editor).

## Branche & PR

- 1 branche : `claude/reorg-cmangos-style`.
- 1 PR avec 4 commits (moves C++, includes+cmake, ci+map, web-portal interne).
- Déploiement : ⚠️ **REDÉPLOIEMENT SERVEUR REQUIS** — `db/` → `sql/` synchronisé avec l'image Docker (master + shard + web-portal en lock-step via le bundle `lcdlln-docker-linux-<sha>`).
