# cmangos-style Reorganization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Réorganiser la racine du repo pour calquer la structure de cmangos-tbc/src : `src/{shared,client,masterd,shardd,world_editor}` avec un sous-dossier par domaine, lowercase. Préserver le CI/CD existant et les artifacts produits.

**Architecture:** 4 commits sur la branche `claude/reorg-cmangos-style`, 1 PR. Phase A = moves only (`git mv`), phase B = `#include` fixes + CMakeLists refactor, phase C = CI/scripts/CODEBASE_MAP, phase D = web-portal interne. Le repo doit recompiler après la phase B et passer toute la CI après la phase C.

**Tech Stack:** CMake 3.20+, vcpkg, C++20, GitHub Actions, Next.js 13 (web-portal).

---

## File Structure (mapping engine/ → src/)

### Mapping global

| Source | Destination |
|---|---|
| `engine/Engine.cpp` + `engine/Engine.h` | `src/client/app/` |
| `engine/app/main.cpp` | `src/client/main.cpp` |
| `engine/app/world_editor_main.cpp` | `src/world_editor/main.cpp` |
| `engine/assets/` | `src/client/assets/` |
| `engine/audio/` | `src/client/audio/` |
| `engine/auth/` | `src/shared/auth/` |
| `engine/client/` (55 files) | `src/client/` (regroupés par domaine — voir détail) |
| `engine/core/` | `src/shared/core/` |
| `engine/editor/` | `src/world_editor/` |
| `engine/gameplay/` | `src/client/gameplay/` |
| `engine/math/` | `src/shared/math/` |
| `engine/net/` | `src/shared/net/` |
| `engine/network/` | `src/shared/network/` |
| `engine/platform/` | `src/shared/platform/` |
| `engine/render/` (97 files) | `src/client/render/` |
| `engine/world/` | `src/client/world/` |

### Mapping `engine/server/` (181 fichiers, le plus complexe)

#### Sous-dossiers déjà rangés (Phase 3-5 step 1+2)

Vers `src/shared/` (utilitaires cross-app) :
| Source | Destination |
|---|---|
| `engine/server/db/` | `src/shared/db/` |
| `engine/server/util/` | `src/shared/util/` |
| `engine/server/metric/` | `src/shared/metric/` |
| `engine/server/messager/` | `src/shared/messager/` |
| `engine/server/packetlog/` | `src/shared/packetlog/` |
| `engine/server/formulas/` | `src/shared/formulas/` |
| `engine/server/platform/` | `src/shared/platform/` |
| `engine/server/shard/` (chat/entities/globals/movement/vmap) | **garder** dans `src/shardd/shard/` (gameplay shard internals) |

Vers `src/masterd/` (master-only) :
| Source | Destination |
|---|---|
| `engine/server/chat/` (sanitizer, gate, registry, command router) | `src/masterd/chat/` |
| `engine/server/mail/` | `src/masterd/mail/` |
| `engine/server/gmtickets/` | `src/masterd/gmtickets/` |
| `engine/server/social/` | `src/masterd/social/` |
| `engine/server/reputation/` | `src/masterd/reputation/` |
| `engine/server/quests/` | `src/masterd/quests/` |
| `engine/server/lfg/` | `src/masterd/lfg/` |
| `engine/server/events/` | `src/masterd/events/` |
| `engine/server/world/` (WorldStateRegistry, WorldClock) | `src/masterd/world/` |

Vers `src/shardd/` (shard-only) :
| Source | Destination |
|---|---|
| `engine/server/ai/` | `src/shardd/ai/` |
| `engine/server/anticheat/` | `src/shardd/anticheat/` |
| `engine/server/arena/` | `src/shardd/arena/` |
| `engine/server/auction/` | `src/shardd/auction/` |
| `engine/server/battleground/` | `src/shardd/battleground/` |
| `engine/server/cinematics/` | `src/shardd/cinematics/` |
| `engine/server/combat/` | `src/shardd/combat/` |
| `engine/server/dbscripts/` | `src/shardd/dbscripts/` |
| `engine/server/guild/` | `src/shardd/guild/` |
| `engine/server/loot/` | `src/shardd/loot/` |
| `engine/server/maps/` | `src/shardd/maps/` |
| `engine/server/outdoorpvp/` | `src/shardd/outdoorpvp/` |
| `engine/server/playerbot/` | `src/shardd/playerbot/` |
| `engine/server/pools/` | `src/shardd/pools/` |
| `engine/server/skills/` | `src/shardd/skills/` |
| `engine/server/spell/` | `src/shardd/spell/` |
| `engine/server/trade/` | `src/shardd/trade/` |
| `engine/server/weather/` | `src/shardd/weather/` |
| `engine/server/shard/` | `src/shardd/internals/` (chat/entities/globals/movement/vmap) |

#### Fichiers flat de `engine/server/` (145 fichiers)

| Catégorie | Files | Destination |
|---|---|---|
| **Entry points** | `main.cpp` | `src/shared/server_bootstrap/main.cpp` |
| **Entry points** | `main_server_linux.cpp` | `src/masterd/main_linux.cpp` |
| **Entry points** | `main_server.cpp` | `src/shardd/main_win.cpp` |
| **Entry points** | `main_shard_linux.cpp` | `src/shardd/main_linux.cpp` |
| **Account (master)** | `Account*.{cpp,h}`, `InMemoryAccountStore*`, `MysqlAccountStore*`, `AccountValidation*`, `AccountRecord.h`, `AccountStore.h` | `src/masterd/account/` |
| **Account tests** | `AccountRoleServiceTests.cpp` | `src/masterd/account/` |
| **AntiBot tests** | `AntiBotTests.cpp` | `src/masterd/security/` |
| **Auction (shard)** | `AuctionHouse.{cpp,h}` | `src/shardd/gameplay/` |
| **Handlers (master)** | `AuthRegisterHandler.{cpp,h}`, `Character{Create,Delete,EnterWorld,List,SavePosition}Handler.{cpp,h}`, `ChatRelayHandler.{cpp,h}`, `PasswordResetHandler.{cpp,h}`, `PasswordResetStore.{cpp,h}`, `ShardRegisterHandler.{cpp,h}`, `ShardTicketHandler.{cpp,h}`, `ShardTicketHandshakeHandler.{cpp,h}`, `ShardTicketCrypto.{cpp,h}`, `ShardTicketValidator.{cpp,h}`, `ShardTicketTests.cpp`, `ServerListHandler.{cpp,h}`, `ServerListPolicyTests.cpp`, `TermsHandler.{cpp,h}`, `TermsRepository.{cpp,h}` | `src/masterd/handlers/` |
| **Security (shared)** | `BotDetector*`, `CaptchaVerifier*`, `RateLimitAndBan*`, `SecurityAuditLog*`, `SecurityTests.cpp`, `UserRateLimiter*`, `ConnectionDDoSProtector*` | `src/shared/security/` |
| **Sessions (master)** | `SessionManager*`, `SessionCharacterMap*`, `ConnectionSessionMap.{cpp,h}` | `src/masterd/session/` |
| **Email (master)** | `LocalizedEmail.{cpp,h}`, `SmtpMailer.{cpp,h}` | `src/masterd/email/` |
| **Migration** | `MigrationRunner.{cpp,h}` | `src/masterd/migrations/` |
| **Net server (shared)** | `NetServer*`, `NetServerBandwidthThrottleTests.cpp` | `src/shared/network/` |
| **Server protocol (shared)** | `ServerProtocol.{cpp,h}` | `src/shared/network/` |
| **Server registry (master)** | `ServerRegistry*`, `ShardRegistry*`, `ShardDownDetectionTests.cpp` | `src/masterd/shards/` |
| **Server app shared** | `ServerApp.{cpp,h}` | `src/shared/server_bootstrap/` |
| **Health / metrics (master)** | `HealthEndpoint.{cpp,h}`, `PrometheusMetrics.{cpp,h}` | `src/masterd/metrics/` |
| **Gameplay (shard win/linux)** | `CraftingSystem*`, `CurrencyConfig*`, `EventRuntime*`, `FriendSystem*`, `GatheringSystem*`, `GuildBank*`, `GuildSystem*`, `GuildTabard*`, `PartySystem*`, `PlayerWalletService*`, `QuestRuntime*`, `SpawnerRuntime*`, `TradeSystem*`, `VendorCatalog*`, `CharacterPersistence*`, `ChatCommandParser*` | `src/shardd/gameplay/` |
| **Spatial (shard)** | `GridState*`, `GridStateTests.cpp`, `LagCompensation*`, `SpatialPartition*`, `TickScheduler*`, `UdpTransport*`, `ZoneTransitions*` | `src/shardd/world/` |
| **Replication shared** | `ReplicationTypes.h` | `src/shared/network/` |

### Top-level moves

| Source | Destination |
|---|---|
| `db/` | `sql/` |
| `Editeur d'univers/` | `legacy/world-builder-html/` |
| `Editeur de monde/` | `legacy/world-editor-html/` |

---

## Phase A — Préparation

### Task A1: Créer la branche et vérifier l'état initial

**Files:**
- Modify: working directory state

- [ ] **Step 1: Vérifier que main est à jour et propre**

```bash
git checkout main
git pull --ff-only origin main
git status
```
Expected: `nothing to commit, working tree clean`

- [ ] **Step 2: Créer la branche**

```bash
git checkout -b claude/reorg-cmangos-style
```
Expected: `Switched to a new branch 'claude/reorg-cmangos-style'`

- [ ] **Step 3: Snapshot de l'état initial pour la PR**

```bash
git ls-files | wc -l > /tmp/files_before.txt
ls engine/ | sort > /tmp/engine_dirs_before.txt
ls engine/server/ | sort > /tmp/server_files_before.txt
echo "Snapshot saved"
```

### Task A2: Créer la structure de répertoires cible (vide)

- [ ] **Step 1: Créer `src/` et ses sous-dossiers**

```bash
mkdir -p src/shared/{core,auth,network,db,math,platform,metric,messager,packetlog,formulas,util,net,security,server_bootstrap}
mkdir -p src/client/{app,assets,audio,auth,gameplay,net,render,world}
mkdir -p src/masterd/{account,chat,email,events,gmtickets,handlers,lfg,mail,metrics,migrations,quests,reputation,security,session,shards,social,terms,world}
mkdir -p src/shardd/{ai,anticheat,app,arena,auction,battleground,cinematics,combat,dbscripts,gameplay,guild,internals,loot,maps,net,outdoorpvp,playerbot,pools,skills,spell,trade,weather,world}
mkdir -p src/world_editor/{app,io,tests,tools,ui}
mkdir -p legacy
```

- [ ] **Step 2: Vérifier la structure**

```bash
find src -type d | sort | head -40
```
Expected: ~55 dossiers listés.

- [ ] **Step 3: Ajouter des fichiers `.gitkeep` pour que les dossiers vides soient tracés**

```bash
find src -type d -empty -exec touch {}/.gitkeep \;
git add src/
git status --short | head -10
```

---

## Phase B — Moves C++ (Commit 1)

### Task B1: Move top-level renames

- [ ] **Step 1: `db/` → `sql/`**

```bash
git mv db sql
```

- [ ] **Step 2: HTML legacy editors → `legacy/`**

```bash
git mv "Editeur d'univers" "legacy/world-builder-html"
git mv "Editeur de monde" "legacy/world-editor-html"
```

- [ ] **Step 3: Vérifier**

```bash
ls sql/ legacy/
git status --short | head -20
```
Expected: `legacy/world-builder-html/` et `legacy/world-editor-html/` existent.

### Task B2: Move `engine/core/`, `engine/auth/`, `engine/math/`, `engine/platform/`, `engine/net/`, `engine/network/`

- [ ] **Step 1: Moves vers `src/shared/`**

```bash
git rm -f src/shared/core/.gitkeep src/shared/auth/.gitkeep src/shared/math/.gitkeep src/shared/platform/.gitkeep src/shared/net/.gitkeep src/shared/network/.gitkeep 2>/dev/null
git mv engine/core/* src/shared/core/
git mv engine/auth/* src/shared/auth/
git mv engine/math/* src/shared/math/
git mv engine/platform/* src/shared/platform/
git mv engine/net/* src/shared/net/
git mv engine/network/* src/shared/network/
```

- [ ] **Step 2: Vérifier — engine/core,auth,math,platform,net,network n'existent plus**

```bash
ls engine/core engine/auth engine/math engine/platform engine/net engine/network 2>&1 | head -5
ls src/shared/core | head -5
```
Expected: les 6 dossiers `engine/...` sont vides ou supprimés ; `src/shared/core/` contient `Log.cpp`, `Log.h`, etc.

- [ ] **Step 3: Supprimer les dossiers vides dans engine/**

```bash
rmdir engine/core engine/auth engine/math engine/platform engine/net engine/network 2>/dev/null
ls engine/ | sort
```

### Task B3: Move `engine/audio/`, `engine/gameplay/`, `engine/world/`, `engine/assets/`

- [ ] **Step 1: Moves vers `src/client/`**

```bash
git rm -f src/client/audio/.gitkeep src/client/gameplay/.gitkeep src/client/world/.gitkeep src/client/assets/.gitkeep 2>/dev/null
git mv engine/audio/* src/client/audio/
git mv engine/gameplay/* src/client/gameplay/
git mv engine/world/* src/client/world/
git mv engine/assets/* src/client/assets/
rmdir engine/audio engine/gameplay engine/world engine/assets 2>/dev/null
```

- [ ] **Step 2: Vérifier**

```bash
ls src/client/audio | head -3
ls src/client/world | head -3
```

### Task B4: Move `engine/client/` → `src/client/` (avec sous-dossiers existants)

- [ ] **Step 1: Examiner la structure de engine/client/ pour préserver les sous-dossiers**

```bash
ls -d engine/client/*/ 2>/dev/null
ls engine/client/*.cpp engine/client/*.h 2>/dev/null | wc -l
```

- [ ] **Step 2: Move tous les fichiers et sous-dossiers**

```bash
git mv engine/client/* src/client/
rmdir engine/client 2>/dev/null
```

- [ ] **Step 3: Vérifier**

```bash
ls src/client/auth/ | head -5
ls src/client/*.cpp | head -5
```

### Task B5: Move `engine/render/` → `src/client/render/`

- [ ] **Step 1: Move tout le contenu de engine/render/**

```bash
git rm -f src/client/render/.gitkeep 2>/dev/null
git mv engine/render/* src/client/render/
rmdir engine/render 2>/dev/null
```

- [ ] **Step 2: Vérifier**

```bash
ls src/client/render/ | head -10
ls -d src/client/render/*/ | head -5
```

### Task B6: Move `engine/editor/` → `src/world_editor/`

- [ ] **Step 1: Move les flat files (panels, modes) → `src/world_editor/ui/`**

```bash
git rm -f src/world_editor/ui/.gitkeep src/world_editor/tests/.gitkeep 2>/dev/null
git mv engine/editor/EditorAudioPanel.cpp engine/editor/EditorAudioPanel.h \
       engine/editor/EditorMode.cpp engine/editor/EditorMode.h \
       engine/editor/TextureLibraryPanel.cpp src/world_editor/ui/
ls engine/editor/*.{cpp,h} 2>/dev/null
```

- [ ] **Step 2: Move `engine/editor/world/` → `src/world_editor/`**

```bash
git mv engine/editor/world/* src/world_editor/
rmdir engine/editor/world 2>/dev/null
```

- [ ] **Step 3: Move `engine/editor/tests/` → `src/world_editor/tests/`**

```bash
git mv engine/editor/tests/* src/world_editor/tests/
rmdir engine/editor/tests engine/editor 2>/dev/null
```

- [ ] **Step 4: Vérifier**

```bash
ls src/world_editor/ui/
ls src/world_editor/tests/ | head -5
```

### Task B7: Move `engine/app/` → entry points

- [ ] **Step 1: Move main.cpp → client, world_editor_main.cpp → world_editor**

```bash
git mv engine/app/main.cpp src/client/main.cpp
git mv engine/app/world_editor_main.cpp src/world_editor/main.cpp
rmdir engine/app 2>/dev/null
```

- [ ] **Step 2: Vérifier**

```bash
ls src/client/main.cpp src/world_editor/main.cpp
```

### Task B8: Move `engine/Engine.cpp` + `engine/Engine.h` → `src/client/app/`

- [ ] **Step 1: Move**

```bash
git rm -f src/client/app/.gitkeep 2>/dev/null
git mv engine/Engine.cpp engine/Engine.h src/client/app/
```

- [ ] **Step 2: Vérifier qu'il ne reste rien dans engine/**

```bash
ls engine/ 2>&1
```
Expected: only `engine/server/` left.

### Task B9: Move `engine/server/` sous-dossiers vers `src/shared/`

- [ ] **Step 1: Move db, util, metric, messager, packetlog, formulas, platform**

```bash
git rm -f src/shared/db/.gitkeep src/shared/util/.gitkeep src/shared/metric/.gitkeep \
  src/shared/messager/.gitkeep src/shared/packetlog/.gitkeep src/shared/formulas/.gitkeep 2>/dev/null
git mv engine/server/db/* src/shared/db/
git mv engine/server/util/* src/shared/util/
git mv engine/server/metric/* src/shared/metric/
git mv engine/server/messager/* src/shared/messager/
git mv engine/server/packetlog/* src/shared/packetlog/
git mv engine/server/formulas/* src/shared/formulas/
# platform : merge avec src/shared/platform/ existant
git mv engine/server/platform/* src/shared/platform/
rmdir engine/server/{db,util,metric,messager,packetlog,formulas,platform} 2>/dev/null
```

- [ ] **Step 2: Vérifier**

```bash
ls src/shared/db/ src/shared/util/ src/shared/metric/ | head -10
```

### Task B10: Move `engine/server/` sous-dossiers vers `src/masterd/`

- [ ] **Step 1: Master-only sous-dossiers**

```bash
git rm -f src/masterd/chat/.gitkeep src/masterd/mail/.gitkeep src/masterd/gmtickets/.gitkeep \
  src/masterd/social/.gitkeep src/masterd/reputation/.gitkeep src/masterd/quests/.gitkeep \
  src/masterd/lfg/.gitkeep src/masterd/events/.gitkeep src/masterd/world/.gitkeep 2>/dev/null
git mv engine/server/chat/* src/masterd/chat/
git mv engine/server/mail/* src/masterd/mail/
git mv engine/server/gmtickets/* src/masterd/gmtickets/
git mv engine/server/social/* src/masterd/social/
git mv engine/server/reputation/* src/masterd/reputation/
git mv engine/server/quests/* src/masterd/quests/
git mv engine/server/lfg/* src/masterd/lfg/
git mv engine/server/events/* src/masterd/events/
git mv engine/server/world/* src/masterd/world/
rmdir engine/server/{chat,mail,gmtickets,social,reputation,quests,lfg,events,world} 2>/dev/null
```

- [ ] **Step 2: Vérifier**

```bash
ls src/masterd/chat/ | head -5
ls src/masterd/mail/ | head -5
```

### Task B11: Move `engine/server/` sous-dossiers vers `src/shardd/`

- [ ] **Step 1: Shard-only sous-dossiers**

```bash
git rm -f src/shardd/{ai,anticheat,arena,auction,battleground,cinematics,combat,dbscripts,guild,loot,maps,outdoorpvp,playerbot,pools,skills,spell,trade,weather}/.gitkeep src/shardd/internals/.gitkeep 2>/dev/null
for dir in ai anticheat arena auction battleground cinematics combat dbscripts guild loot maps outdoorpvp playerbot pools skills spell trade weather; do
  git mv "engine/server/$dir"/* "src/shardd/$dir/"
  rmdir "engine/server/$dir" 2>/dev/null
done
git mv engine/server/shard/* src/shardd/internals/
rmdir engine/server/shard 2>/dev/null
```

- [ ] **Step 2: Vérifier**

```bash
ls src/shardd/ai/
ls src/shardd/internals/
```

### Task B12: Move flat files de `engine/server/` (Account → masterd/account)

- [ ] **Step 1: Account files (master)**

```bash
git rm -f src/masterd/account/.gitkeep 2>/dev/null
git mv engine/server/AccountRecord.h \
       engine/server/AccountRole.cpp engine/server/AccountRole.h \
       engine/server/AccountRoleService.cpp engine/server/AccountRoleService.h \
       engine/server/AccountRoleServiceTests.cpp \
       engine/server/AccountStore.h \
       engine/server/AccountValidation.cpp engine/server/AccountValidation.h \
       engine/server/InMemoryAccountStore.cpp engine/server/InMemoryAccountStore.h \
       engine/server/MysqlAccountStore.cpp engine/server/MysqlAccountStore.h \
       src/masterd/account/
```

- [ ] **Step 2: Vérifier**

```bash
ls src/masterd/account/ | head -10
```

### Task B13: Move flat files (Handlers → masterd/handlers)

- [ ] **Step 1: Tous les *Handler*.{cpp,h}**

```bash
git rm -f src/masterd/handlers/.gitkeep 2>/dev/null
git mv engine/server/AuthRegisterHandler.cpp engine/server/AuthRegisterHandler.h \
       engine/server/CharacterCreateHandler.cpp engine/server/CharacterCreateHandler.h \
       engine/server/CharacterDeleteHandler.cpp engine/server/CharacterDeleteHandler.h \
       engine/server/CharacterEnterWorldHandler.cpp engine/server/CharacterEnterWorldHandler.h \
       engine/server/CharacterListHandler.cpp engine/server/CharacterListHandler.h \
       engine/server/CharacterSavePositionHandler.cpp engine/server/CharacterSavePositionHandler.h \
       engine/server/ChatRelayHandler.cpp engine/server/ChatRelayHandler.h \
       engine/server/PasswordResetHandler.cpp engine/server/PasswordResetHandler.h \
       engine/server/PasswordResetStore.cpp engine/server/PasswordResetStore.h \
       engine/server/ShardRegisterHandler.cpp engine/server/ShardRegisterHandler.h \
       engine/server/ShardTicketCrypto.cpp engine/server/ShardTicketCrypto.h \
       engine/server/ShardTicketHandler.cpp engine/server/ShardTicketHandler.h \
       engine/server/ShardTicketHandshakeHandler.cpp engine/server/ShardTicketHandshakeHandler.h \
       engine/server/ShardTicketTests.cpp \
       engine/server/ShardTicketValidator.cpp engine/server/ShardTicketValidator.h \
       engine/server/ServerListHandler.cpp engine/server/ServerListHandler.h \
       engine/server/ServerListPolicyTests.cpp \
       engine/server/TermsHandler.cpp engine/server/TermsHandler.h \
       engine/server/TermsRepository.cpp engine/server/TermsRepository.h \
       src/masterd/handlers/
```

- [ ] **Step 2: Vérifier**

```bash
ls src/masterd/handlers/ | wc -l
```
Expected: ~25-30 fichiers.

### Task B14: Move flat files (Sessions, Email, Migrations, Shards registry, Health → masterd)

- [ ] **Step 1: Sessions**

```bash
git rm -f src/masterd/session/.gitkeep src/masterd/email/.gitkeep src/masterd/migrations/.gitkeep \
  src/masterd/shards/.gitkeep src/masterd/metrics/.gitkeep 2>/dev/null
git mv engine/server/SessionManager.cpp engine/server/SessionManager.h engine/server/SessionManagerTests.cpp \
       engine/server/SessionCharacterMap.cpp engine/server/SessionCharacterMap.h engine/server/SessionCharacterMapTests.cpp \
       engine/server/ConnectionSessionMap.cpp engine/server/ConnectionSessionMap.h \
       src/masterd/session/
```

- [ ] **Step 2: Email**

```bash
git mv engine/server/LocalizedEmail.cpp engine/server/LocalizedEmail.h \
       engine/server/SmtpMailer.cpp engine/server/SmtpMailer.h \
       src/masterd/email/
```

- [ ] **Step 3: Migrations**

```bash
git mv engine/server/MigrationRunner.cpp engine/server/MigrationRunner.h src/masterd/migrations/
```

- [ ] **Step 4: Shards registry**

```bash
git mv engine/server/ServerRegistry.cpp engine/server/ServerRegistry.h \
       engine/server/ShardRegistry.cpp engine/server/ShardRegistry.h \
       engine/server/ShardDownDetectionTests.cpp \
       src/masterd/shards/
```

- [ ] **Step 5: Health/metrics**

```bash
git mv engine/server/HealthEndpoint.cpp engine/server/HealthEndpoint.h \
       engine/server/PrometheusMetrics.cpp engine/server/PrometheusMetrics.h \
       src/masterd/metrics/
```

### Task B15: Move flat files (Security partagé → src/shared/security)

- [ ] **Step 1: Security files (utilisés par master ET shard via target_sources)**

```bash
git rm -f src/shared/security/.gitkeep 2>/dev/null
git mv engine/server/BotDetector.cpp engine/server/BotDetector.h \
       engine/server/CaptchaVerifier.cpp engine/server/CaptchaVerifier.h \
       engine/server/RateLimitAndBan.cpp engine/server/RateLimitAndBan.h \
       engine/server/SecurityAuditLog.cpp engine/server/SecurityAuditLog.h \
       engine/server/SecurityTests.cpp \
       engine/server/UserRateLimiter.cpp engine/server/UserRateLimiter.h \
       engine/server/ConnectionDDoSProtector.h engine/server/ConnectionDDoSProtectorTests.cpp \
       engine/server/AntiBotTests.cpp \
       src/shared/security/
```

- [ ] **Step 2: Vérifier**

```bash
ls src/shared/security/ | wc -l
```
Expected: ~14 fichiers.

### Task B16: Move flat files (Network shared → src/shared/network)

- [ ] **Step 1: NetServer + ServerProtocol + ReplicationTypes**

```bash
git mv engine/server/NetServer.cpp engine/server/NetServer.h engine/server/NetServerBandwidthThrottleTests.cpp \
       engine/server/ServerProtocol.cpp engine/server/ServerProtocol.h \
       engine/server/ReplicationTypes.h \
       src/shared/network/
```

### Task B17: Move flat files (Server bootstrap shared)

- [ ] **Step 1: ServerApp + main.cpp partagé**

```bash
git rm -f src/shared/server_bootstrap/.gitkeep 2>/dev/null
git mv engine/server/ServerApp.cpp engine/server/ServerApp.h engine/server/main.cpp \
       src/shared/server_bootstrap/
```

### Task B18: Move flat files (Gameplay shard → src/shardd/gameplay)

- [ ] **Step 1: Tous les systèmes gameplay (utilisés par WIN32 sandbox + Linux shard)**

```bash
git rm -f src/shardd/gameplay/.gitkeep 2>/dev/null
git mv engine/server/AuctionHouse.cpp engine/server/AuctionHouse.h \
       engine/server/CharacterPersistence.cpp engine/server/CharacterPersistence.h \
       engine/server/ChatCommandParser.cpp engine/server/ChatCommandParser.h \
       engine/server/CraftingSystem.cpp engine/server/CraftingSystem.h \
       engine/server/CurrencyConfig.cpp engine/server/CurrencyConfig.h \
       engine/server/EventRuntime.cpp engine/server/EventRuntime.h \
       engine/server/FriendSystem.cpp engine/server/FriendSystem.h \
       engine/server/GatheringSystem.cpp engine/server/GatheringSystem.h \
       engine/server/GuildBank.cpp engine/server/GuildBank.h \
       engine/server/GuildSystem.cpp engine/server/GuildSystem.h \
       engine/server/GuildTabard.cpp engine/server/GuildTabard.h \
       engine/server/PartySystem.cpp engine/server/PartySystem.h \
       engine/server/PlayerWalletService.cpp engine/server/PlayerWalletService.h \
       engine/server/QuestRuntime.cpp engine/server/QuestRuntime.h \
       engine/server/SpawnerRuntime.cpp engine/server/SpawnerRuntime.h \
       engine/server/TradeSystem.cpp engine/server/TradeSystem.h \
       engine/server/VendorCatalog.cpp engine/server/VendorCatalog.h \
       src/shardd/gameplay/
```

### Task B19: Move flat files (World/spatial shard → src/shardd/world)

- [ ] **Step 1: Spatial systems**

```bash
git rm -f src/shardd/world/.gitkeep 2>/dev/null
git mv engine/server/GridState.cpp engine/server/GridState.h engine/server/GridStateTests.cpp \
       engine/server/LagCompensation.cpp engine/server/LagCompensation.h \
       engine/server/SpatialPartition.cpp engine/server/SpatialPartition.h \
       engine/server/TickScheduler.cpp engine/server/TickScheduler.h \
       engine/server/UdpTransport.cpp engine/server/UdpTransport.h \
       engine/server/ZoneTransitions.cpp engine/server/ZoneTransitions.h \
       src/shardd/world/
```

### Task B20: Move entry points server

- [ ] **Step 1: Master Linux entry → masterd**

```bash
git mv engine/server/main_server_linux.cpp src/masterd/main_linux.cpp
```

- [ ] **Step 2: Shard entries → shardd**

```bash
git mv engine/server/main_server.cpp src/shardd/main_win.cpp
git mv engine/server/main_shard_linux.cpp src/shardd/main_linux.cpp
```

- [ ] **Step 3: CMakeLists.txt de engine/server/ → src/CMakeLists.txt initial (placeholder, sera refait phase C)**

```bash
git mv engine/server/CMakeLists.txt src/CMakeLists.txt.old
```

- [ ] **Step 4: Vérifier qu'engine/server/ est vide**

```bash
ls engine/server/ 2>&1
```
Expected: dossier vide ou n'existe plus.

- [ ] **Step 5: Supprimer engine/server/ et engine/ vides**

```bash
rmdir engine/server engine 2>/dev/null
ls engine 2>&1
```
Expected: `ls: cannot access 'engine': No such file or directory`

### Task B21: Nettoyer les .gitkeep restants

- [ ] **Step 1: Lister les .gitkeep**

```bash
find src -name .gitkeep
```

- [ ] **Step 2: Supprimer ceux dans des dossiers non-vides**

```bash
for f in $(find src -name .gitkeep); do
  dir=$(dirname "$f")
  files=$(ls "$dir" | grep -v "^.gitkeep$" | wc -l)
  if [ "$files" -gt 0 ]; then
    git rm "$f"
  fi
done
```

- [ ] **Step 3: Garder .gitkeep dans les dossiers vraiment vides**

```bash
find src -name .gitkeep
```
Expected: liste courte de dossiers vraiment vides (ex: src/world_editor/io/ s'il n'a pas encore de fichiers).

### Task B22: Commit 1 — moves only

- [ ] **Step 1: Vérifier le diff statistique**

```bash
git status --short | wc -l
git diff --stat HEAD | tail -3
```
Expected: ~600 fichiers renommés, peu d'ajouts/suppressions de lignes.

- [ ] **Step 2: Commit**

```bash
git commit -m "$(cat <<'EOF'
refactor(repo): move engine/ to src/{shared,client,masterd,shardd,world_editor}

Phase A du re-rangement cmangos-style. Mouvements de fichiers uniquement
via git mv (préserve l'historique). Le repo NE COMPILE PAS après ce commit
— c'est intentionnel pour minimiser la review surface du diff move-only.
La compilation revient au commit 2 (#include + CMakeLists).

Top-level :
- db/ → sql/ (parité cmangos)
- "Editeur d'univers/" + "Editeur de monde/" → legacy/

src/shared/ : core, auth, network, db, math, platform, metric, messager,
packetlog, formulas, util, net, security, server_bootstrap.

src/client/ : Engine.cpp, main.cpp, app/, audio/, auth/, gameplay/, net/,
render/, world/, assets/.

src/masterd/ : main_linux.cpp, account/, chat/, email/, events/, gmtickets/,
handlers/, lfg/, mail/, metrics/, migrations/, quests/, reputation/,
session/, shards/, social/, world/.

src/shardd/ : main_linux.cpp, main_win.cpp, ai/, anticheat/, arena/,
auction/, battleground/, cinematics/, combat/, dbscripts/, gameplay/,
guild/, internals/, loot/, maps/, outdoorpvp/, playerbot/, pools/, skills/,
spell/, trade/, weather/, world/.

src/world_editor/ : main.cpp, ui/, tests/, + ex-engine/editor/world/.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 3: Vérifier le commit**

```bash
git log --oneline -1
git show --stat HEAD | tail -5
```

---

## Phase C — `#include` + CMakeLists (Commit 2)

### Task C1: Construire le mapping des `#include` à modifier

Le mapping `engine/<old-path>/` → `<new-path>/` est :

| Ancien préfixe | Nouveau préfixe |
|---|---|
| `engine/core/` | `src/shared/core/` |
| `engine/auth/` | `src/shared/auth/` |
| `engine/math/` | `src/shared/math/` |
| `engine/platform/` | `src/shared/platform/` |
| `engine/net/` | `src/shared/net/` |
| `engine/network/` | `src/shared/network/` |
| `engine/audio/` | `src/client/audio/` |
| `engine/gameplay/` | `src/client/gameplay/` |
| `engine/world/` | `src/client/world/` |
| `engine/render/` | `src/client/render/` |
| `engine/client/` | `src/client/` |
| `engine/server/db/` | `src/shared/db/` |
| `engine/server/util/` | `src/shared/util/` |
| `engine/server/metric/` | `src/shared/metric/` |
| `engine/server/messager/` | `src/shared/messager/` |
| `engine/server/packetlog/` | `src/shared/packetlog/` |
| `engine/server/formulas/` | `src/shared/formulas/` |
| `engine/server/platform/` | `src/shared/platform/` |
| `engine/server/chat/` | `src/masterd/chat/` |
| `engine/server/mail/` | `src/masterd/mail/` |
| `engine/server/gmtickets/` | `src/masterd/gmtickets/` |
| `engine/server/social/` | `src/masterd/social/` |
| `engine/server/reputation/` | `src/masterd/reputation/` |
| `engine/server/quests/` | `src/masterd/quests/` |
| `engine/server/lfg/` | `src/masterd/lfg/` |
| `engine/server/events/` | `src/masterd/events/` |
| `engine/server/world/` | `src/masterd/world/` |
| `engine/server/shard/` | `src/shardd/internals/` |
| `engine/server/ai/` | `src/shardd/ai/` |
| `engine/server/anticheat/` | `src/shardd/anticheat/` |
| `engine/server/arena/` | `src/shardd/arena/` |
| `engine/server/auction/` | `src/shardd/auction/` |
| `engine/server/battleground/` | `src/shardd/battleground/` |
| `engine/server/cinematics/` | `src/shardd/cinematics/` |
| `engine/server/combat/` | `src/shardd/combat/` |
| `engine/server/dbscripts/` | `src/shardd/dbscripts/` |
| `engine/server/guild/` | `src/shardd/guild/` |
| `engine/server/loot/` | `src/shardd/loot/` |
| `engine/server/maps/` | `src/shardd/maps/` |
| `engine/server/outdoorpvp/` | `src/shardd/outdoorpvp/` |
| `engine/server/playerbot/` | `src/shardd/playerbot/` |
| `engine/server/pools/` | `src/shardd/pools/` |
| `engine/server/skills/` | `src/shardd/skills/` |
| `engine/server/spell/` | `src/shardd/spell/` |
| `engine/server/trade/` | `src/shardd/trade/` |
| `engine/server/weather/` | `src/shardd/weather/` |
| `engine/server/` (flat headers — voir B12-B19) | `src/masterd/<subdir>/` ou `src/shardd/<subdir>/` ou `src/shared/<subdir>/` |
| `engine/Engine.h` | `src/client/app/Engine.h` |
| `engine/editor/` | `src/world_editor/` |

- [ ] **Step 1: Vérifier qu'on est sur la branche `claude/reorg-cmangos-style`**

```bash
git branch --show-current
```
Expected: `claude/reorg-cmangos-style`

- [ ] **Step 2: Compter les `#include` à modifier**

```bash
grep -rn '#include "engine/' src/ | wc -l
grep -rn '#include "engine/' CMakeLists.txt | wc -l
```
Expected: ~3000 occurrences à fixer.

### Task C2: Sed automatisé sur les sous-dossiers (groupe shared)

- [ ] **Step 1: Préparer le script sed**

```bash
cat > /tmp/fix-includes-shared.sh << 'BASH'
#!/usr/bin/env bash
set -e
find src -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -print0 | \
  xargs -0 sed -i \
    -e 's|engine/core/|src/shared/core/|g' \
    -e 's|engine/auth/|src/shared/auth/|g' \
    -e 's|engine/math/|src/shared/math/|g' \
    -e 's|engine/platform/|src/shared/platform/|g' \
    -e 's|engine/net/|src/shared/net/|g' \
    -e 's|engine/network/|src/shared/network/|g' \
    -e 's|engine/server/db/|src/shared/db/|g' \
    -e 's|engine/server/util/|src/shared/util/|g' \
    -e 's|engine/server/metric/|src/shared/metric/|g' \
    -e 's|engine/server/messager/|src/shared/messager/|g' \
    -e 's|engine/server/packetlog/|src/shared/packetlog/|g' \
    -e 's|engine/server/formulas/|src/shared/formulas/|g' \
    -e 's|engine/server/platform/|src/shared/platform/|g'
echo "DONE shared"
BASH
chmod +x /tmp/fix-includes-shared.sh
```

- [ ] **Step 2: Exécuter**

```bash
bash /tmp/fix-includes-shared.sh
```

- [ ] **Step 3: Vérifier**

```bash
grep -rn '#include "engine/core/' src/ | wc -l
```
Expected: 0

### Task C3: Sed sur les sous-dossiers (groupe masterd)

- [ ] **Step 1: Script sed**

```bash
cat > /tmp/fix-includes-masterd.sh << 'BASH'
#!/usr/bin/env bash
set -e
find src -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -print0 | \
  xargs -0 sed -i \
    -e 's|engine/server/chat/|src/masterd/chat/|g' \
    -e 's|engine/server/mail/|src/masterd/mail/|g' \
    -e 's|engine/server/gmtickets/|src/masterd/gmtickets/|g' \
    -e 's|engine/server/social/|src/masterd/social/|g' \
    -e 's|engine/server/reputation/|src/masterd/reputation/|g' \
    -e 's|engine/server/quests/|src/masterd/quests/|g' \
    -e 's|engine/server/lfg/|src/masterd/lfg/|g' \
    -e 's|engine/server/events/|src/masterd/events/|g' \
    -e 's|engine/server/world/|src/masterd/world/|g'
echo "DONE masterd"
BASH
bash /tmp/fix-includes-masterd.sh
```

### Task C4: Sed sur les sous-dossiers (groupe shardd)

- [ ] **Step 1: Script sed**

```bash
cat > /tmp/fix-includes-shardd.sh << 'BASH'
#!/usr/bin/env bash
set -e
find src -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -print0 | \
  xargs -0 sed -i \
    -e 's|engine/server/shard/|src/shardd/internals/|g' \
    -e 's|engine/server/ai/|src/shardd/ai/|g' \
    -e 's|engine/server/anticheat/|src/shardd/anticheat/|g' \
    -e 's|engine/server/arena/|src/shardd/arena/|g' \
    -e 's|engine/server/auction/|src/shardd/auction/|g' \
    -e 's|engine/server/battleground/|src/shardd/battleground/|g' \
    -e 's|engine/server/cinematics/|src/shardd/cinematics/|g' \
    -e 's|engine/server/combat/|src/shardd/combat/|g' \
    -e 's|engine/server/dbscripts/|src/shardd/dbscripts/|g' \
    -e 's|engine/server/guild/|src/shardd/guild/|g' \
    -e 's|engine/server/loot/|src/shardd/loot/|g' \
    -e 's|engine/server/maps/|src/shardd/maps/|g' \
    -e 's|engine/server/outdoorpvp/|src/shardd/outdoorpvp/|g' \
    -e 's|engine/server/playerbot/|src/shardd/playerbot/|g' \
    -e 's|engine/server/pools/|src/shardd/pools/|g' \
    -e 's|engine/server/skills/|src/shardd/skills/|g' \
    -e 's|engine/server/spell/|src/shardd/spell/|g' \
    -e 's|engine/server/trade/|src/shardd/trade/|g' \
    -e 's|engine/server/weather/|src/shardd/weather/|g'
echo "DONE shardd"
BASH
bash /tmp/fix-includes-shardd.sh
```

### Task C5: Sed sur les sous-dossiers client / world_editor

- [ ] **Step 1: Script sed**

```bash
cat > /tmp/fix-includes-client.sh << 'BASH'
#!/usr/bin/env bash
set -e
find src -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -print0 | \
  xargs -0 sed -i \
    -e 's|engine/audio/|src/client/audio/|g' \
    -e 's|engine/gameplay/|src/client/gameplay/|g' \
    -e 's|engine/world/|src/client/world/|g' \
    -e 's|engine/render/|src/client/render/|g' \
    -e 's|engine/client/|src/client/|g' \
    -e 's|engine/Engine.h|src/client/app/Engine.h|g' \
    -e 's|engine/editor/|src/world_editor/|g'
echo "DONE client/editor"
BASH
bash /tmp/fix-includes-client.sh
```

### Task C6: Sed pour flat files engine/server/* (Account, Handlers, etc.)

Les flat files sont les plus tricky : `#include "engine/server/AccountStore.h"` doit devenir `#include "src/masterd/account/AccountStore.h"`. Mais on a ~30 fichiers flat dispatchés dans 8 sous-dossiers cibles différents (account, handlers, session, email, migrations, shards, metrics, security, network, server_bootstrap, gameplay, world).

- [ ] **Step 1: Construire le script avec mapping explicite par fichier**

```bash
cat > /tmp/fix-includes-server-flat.sh << 'BASH'
#!/usr/bin/env bash
set -e
declare -A MAP=(
  # account/
  [Account]=src/masterd/account
  [InMemoryAccountStore]=src/masterd/account
  [MysqlAccountStore]=src/masterd/account
  # handlers/
  [AuthRegisterHandler]=src/masterd/handlers
  [CharacterCreateHandler]=src/masterd/handlers
  [CharacterDeleteHandler]=src/masterd/handlers
  [CharacterEnterWorldHandler]=src/masterd/handlers
  [CharacterListHandler]=src/masterd/handlers
  [CharacterSavePositionHandler]=src/masterd/handlers
  [ChatRelayHandler]=src/masterd/handlers
  [PasswordResetHandler]=src/masterd/handlers
  [PasswordResetStore]=src/masterd/handlers
  [ShardRegisterHandler]=src/masterd/handlers
  [ShardTicketCrypto]=src/masterd/handlers
  [ShardTicketHandler]=src/masterd/handlers
  [ShardTicketHandshakeHandler]=src/masterd/handlers
  [ShardTicketValidator]=src/masterd/handlers
  [ServerListHandler]=src/masterd/handlers
  [TermsHandler]=src/masterd/handlers
  [TermsRepository]=src/masterd/handlers
  # session/
  [SessionManager]=src/masterd/session
  [SessionCharacterMap]=src/masterd/session
  [ConnectionSessionMap]=src/masterd/session
  # email/
  [LocalizedEmail]=src/masterd/email
  [SmtpMailer]=src/masterd/email
  # migrations/
  [MigrationRunner]=src/masterd/migrations
  # shards registry/
  [ServerRegistry]=src/masterd/shards
  [ShardRegistry]=src/masterd/shards
  # metrics/
  [HealthEndpoint]=src/masterd/metrics
  [PrometheusMetrics]=src/masterd/metrics
  # shared/security/
  [BotDetector]=src/shared/security
  [CaptchaVerifier]=src/shared/security
  [RateLimitAndBan]=src/shared/security
  [SecurityAuditLog]=src/shared/security
  [UserRateLimiter]=src/shared/security
  [ConnectionDDoSProtector]=src/shared/security
  # shared/network/
  [NetServer]=src/shared/network
  [ServerProtocol]=src/shared/network
  [ReplicationTypes]=src/shared/network
  # shared/server_bootstrap/
  [ServerApp]=src/shared/server_bootstrap
  # shardd/gameplay/
  [AuctionHouse]=src/shardd/gameplay
  [CharacterPersistence]=src/shardd/gameplay
  [ChatCommandParser]=src/shardd/gameplay
  [CraftingSystem]=src/shardd/gameplay
  [CurrencyConfig]=src/shardd/gameplay
  [EventRuntime]=src/shardd/gameplay
  [FriendSystem]=src/shardd/gameplay
  [GatheringSystem]=src/shardd/gameplay
  [GuildBank]=src/shardd/gameplay
  [GuildSystem]=src/shardd/gameplay
  [GuildTabard]=src/shardd/gameplay
  [PartySystem]=src/shardd/gameplay
  [PlayerWalletService]=src/shardd/gameplay
  [QuestRuntime]=src/shardd/gameplay
  [SpawnerRuntime]=src/shardd/gameplay
  [TradeSystem]=src/shardd/gameplay
  [VendorCatalog]=src/shardd/gameplay
  # shardd/world/
  [GridState]=src/shardd/world
  [LagCompensation]=src/shardd/world
  [SpatialPartition]=src/shardd/world
  [TickScheduler]=src/shardd/world
  [UdpTransport]=src/shardd/world
  [ZoneTransitions]=src/shardd/world
)
for stem in "${!MAP[@]}"; do
  newdir="${MAP[$stem]}"
  find src -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -print0 | \
    xargs -0 sed -i \
      -e "s|engine/server/${stem}\.h|${newdir}/${stem}.h|g" \
      -e "s|engine/server/${stem}\.cpp|${newdir}/${stem}.cpp|g"
done
echo "DONE flat server"
BASH
bash /tmp/fix-includes-server-flat.sh
```

- [ ] **Step 2: Vérifier qu'il ne reste plus aucun `engine/server/...` dans les includes**

```bash
grep -rn '#include "engine/' src/ | head -20
```
Expected: liste vide ou très courte (à investiguer manuellement si non vide).

- [ ] **Step 3: Si reste, fix manuel**

Si la commande au step 2 retourne des lignes, examiner et fixer chaque cas individuellement avec sed ou Edit. Les cas typiques restants :
- `engine/server/main.cpp` → `src/shared/server_bootstrap/main.cpp` (déjà déplacé)
- includes de fichiers déplacés vers `src/shardd/internals/` (ex `engine/server/shard/...`).

### Task C7: Refactoriser `CMakeLists.txt` racine

- [ ] **Step 1: Lire le CMakeLists.txt actuel pour comprendre la structure**

```bash
wc -l CMakeLists.txt
head -50 CMakeLists.txt
```

- [ ] **Step 2: Backup**

```bash
cp CMakeLists.txt CMakeLists.txt.bak
```

- [ ] **Step 3: Modifier les paths `engine/...` → `src/...` via sed**

```bash
sed -i \
  -e 's|engine/core/|src/shared/core/|g' \
  -e 's|engine/auth/|src/shared/auth/|g' \
  -e 's|engine/math/|src/shared/math/|g' \
  -e 's|engine/platform/|src/shared/platform/|g' \
  -e 's|engine/net/|src/shared/net/|g' \
  -e 's|engine/network/|src/shared/network/|g' \
  -e 's|engine/audio/|src/client/audio/|g' \
  -e 's|engine/gameplay/|src/client/gameplay/|g' \
  -e 's|engine/world/|src/client/world/|g' \
  -e 's|engine/render/|src/client/render/|g' \
  -e 's|engine/client/|src/client/|g' \
  -e 's|engine/Engine\.cpp|src/client/app/Engine.cpp|g' \
  -e 's|engine/Engine\.h|src/client/app/Engine.h|g' \
  -e 's|engine/app/main\.cpp|src/client/main.cpp|g' \
  -e 's|engine/app/world_editor_main\.cpp|src/world_editor/main.cpp|g' \
  -e 's|engine/editor/|src/world_editor/|g' \
  CMakeLists.txt
```

- [ ] **Step 4: Modifier le `add_subdirectory(engine/server)` (ou ses sources)**

Le CMakeLists.txt référence l'ancien `engine/server/CMakeLists.txt`. À remplacer par les sous-CMakeLists par app.

```bash
sed -i 's|add_subdirectory(engine/server)|add_subdirectory(src)|g' CMakeLists.txt
grep -n "add_subdirectory" CMakeLists.txt | head -5
```

### Task C8: Créer `src/CMakeLists.txt`

- [ ] **Step 1: Création**

```bash
cat > src/CMakeLists.txt << 'CMAKE'
# src/CMakeLists.txt — agrège les apps cmangos-style.
# Chaque sous-dossier déclare ses targets via son propre CMakeLists.txt.

add_subdirectory(shared)

if(WIN32)
  add_subdirectory(shardd)        # WIN32 sandbox (target shard_sandbox_app)
  # Les apps client/world_editor sont définies dans le CMakeLists racine
  # (engine_app, world_editor_app) car elles dépendent de modules vcpkg
  # Vulkan/ImGui et de target_sources spécifiques.
elseif(UNIX)
  add_subdirectory(masterd)       # Linux master (target master_app)
  add_subdirectory(shardd)        # Linux shard (target shard_app)
endif()
CMAKE
```

- [ ] **Step 2: Vérifier**

```bash
cat src/CMakeLists.txt
```

### Task C9: Créer `src/shared/CMakeLists.txt`

- [ ] **Step 1: Récupérer les sources `engine_core` actuelles depuis CMakeLists.txt racine**

```bash
grep -n "engine_core" CMakeLists.txt | head -10
grep -A 30 "add_library(engine_core" CMakeLists.txt | head -40
```

- [ ] **Step 2: Créer `src/shared/CMakeLists.txt` avec définition de `engine_core`**

```bash
cat > src/shared/CMakeLists.txt << 'CMAKE'
# src/shared/CMakeLists.txt — engine_core (lib statique partagée).
# Les sources sont collectées via globs par sous-dossier domaine.

# core/ : Log, Config, etc.
file(GLOB_RECURSE SHARED_CORE_SRC core/*.cpp core/*.h)
file(GLOB_RECURSE SHARED_AUTH_SRC auth/*.cpp auth/*.h)
file(GLOB_RECURSE SHARED_NETWORK_SRC network/*.cpp network/*.h)
file(GLOB_RECURSE SHARED_DB_SRC db/*.cpp db/*.h)
file(GLOB_RECURSE SHARED_MATH_SRC math/*.cpp math/*.h)
file(GLOB_RECURSE SHARED_PLATFORM_SRC platform/*.cpp platform/*.h)
file(GLOB_RECURSE SHARED_NET_SRC net/*.cpp net/*.h)

target_sources(engine_core PRIVATE
  ${SHARED_CORE_SRC}
  ${SHARED_AUTH_SRC}
  ${SHARED_NETWORK_SRC}
  ${SHARED_DB_SRC}
  ${SHARED_MATH_SRC}
  ${SHARED_PLATFORM_SRC}
  ${SHARED_NET_SRC}
)

# Helper lcdlln_add_simple_test() reste défini dans le CMakeLists racine
# pour les binaires de test qui utilisent engine_core.

# Tests partagés (formulas, util, metric, etc.)
file(GLOB SHARED_TESTS_SRC formulas/*Tests.cpp util/*Tests.cpp
                            metric/*Tests.cpp messager/*Tests.cpp
                            packetlog/*Tests.cpp security/*Tests.cpp
                            db/*Tests.cpp)
# Les tests sont déclarés dans le CMakeLists racine via lcdlln_add_simple_test()
# parce qu'ils nécessitent l'inclusion explicite des sources non-engine_core.
CMAKE
```

- [ ] **Step 3: Migration de la déclaration `add_library(engine_core ...)` du CMakeLists racine vers `src/shared/CMakeLists.txt`**

Cette opération demande de la délicatesse — on ne peut pas seulement déplacer le bloc, il faut s'assurer que la lib est créée avant l'`add_subdirectory(src)` qui tente d'y ajouter des sources. **Stratégie** : garder `add_library(engine_core ...)` dans le CMakeLists racine (avant `add_subdirectory(src)`), et utiliser `target_sources(engine_core PRIVATE ...)` dans `src/shared/CMakeLists.txt`.

```bash
grep -B2 -A5 "add_library(engine_core" CMakeLists.txt | head -10
```

### Task C10: Créer `src/masterd/CMakeLists.txt` (Linux master)

- [ ] **Step 1: Création avec target `master_app`**

```bash
cat > src/masterd/CMakeLists.txt << 'CMAKE'
# src/masterd/CMakeLists.txt — Linux master server.

if(NOT UNIX)
  return()
endif()

find_package(OpenSSL REQUIRED)
find_path(MYSQL_INCLUDE_DIR NAMES mysql.h mysql/mysql.h PATH_SUFFIXES mysql)
find_library(MYSQL_LIBRARY NAMES mysqlclient)
if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_LIBRARY)
  message(FATAL_ERROR "MySQL client not found.")
endif()

file(GLOB_RECURSE MASTERD_SRC
  account/*.cpp account/*.h
  chat/*.cpp chat/*.h
  email/*.cpp email/*.h
  events/*.cpp events/*.h
  gmtickets/*.cpp gmtickets/*.h
  handlers/*.cpp handlers/*.h
  lfg/*.cpp lfg/*.h
  mail/*.cpp mail/*.h
  metrics/*.cpp metrics/*.h
  migrations/*.cpp migrations/*.h
  quests/*.cpp quests/*.h
  reputation/*.cpp reputation/*.h
  session/*.cpp session/*.h
  shards/*.cpp shards/*.h
  social/*.cpp social/*.h
  world/*.cpp world/*.h
)
# Filtrer les *Tests.cpp
list(FILTER MASTERD_SRC EXCLUDE REGEX ".*Tests\\.cpp$")

add_executable(master_app
  main_linux.cpp
  ${MASTERD_SRC}
  ${CMAKE_SOURCE_DIR}/src/shared/server_bootstrap/main.cpp  # bootstrap shared
  ${CMAKE_SOURCE_DIR}/src/shared/server_bootstrap/ServerApp.cpp
)

target_include_directories(master_app PRIVATE
  ${MYSQL_INCLUDE_DIR}
  ${CMAKE_SOURCE_DIR}
)
target_compile_definitions(master_app PRIVATE ENGINE_HAS_MYSQL=1)
target_link_libraries(master_app PRIVATE
  pthread OpenSSL::SSL engine_auth spdlog::spdlog ${MYSQL_LIBRARY} engine_core
)
CMAKE
```

### Task C11: Créer `src/shardd/CMakeLists.txt`

- [ ] **Step 1: Création avec branches WIN32 (sandbox) et UNIX (shard)**

```bash
cat > src/shardd/CMakeLists.txt << 'CMAKE'
# src/shardd/CMakeLists.txt — shard server (Linux + Windows sandbox).

file(GLOB_RECURSE SHARDD_GAMEPLAY_SRC
  ai/*.cpp ai/*.h anticheat/*.cpp anticheat/*.h
  arena/*.cpp arena/*.h auction/*.cpp auction/*.h
  battleground/*.cpp battleground/*.h cinematics/*.cpp cinematics/*.h
  combat/*.cpp combat/*.h dbscripts/*.cpp dbscripts/*.h
  gameplay/*.cpp gameplay/*.h guild/*.cpp guild/*.h
  internals/*.cpp internals/*.h loot/*.cpp loot/*.h
  maps/*.cpp maps/*.h outdoorpvp/*.cpp outdoorpvp/*.h
  playerbot/*.cpp playerbot/*.h pools/*.cpp pools/*.h
  skills/*.cpp skills/*.h spell/*.cpp spell/*.h
  trade/*.cpp trade/*.h weather/*.cpp weather/*.h
  world/*.cpp world/*.h
)
list(FILTER SHARDD_GAMEPLAY_SRC EXCLUDE REGEX ".*Tests\\.cpp$")

if(WIN32)
  # Sandbox Windows (gameplay test local, pas de MySQL)
  add_executable(shard_sandbox_app
    main_win.cpp
    ${SHARDD_GAMEPLAY_SRC}
  )
  target_include_directories(shard_sandbox_app PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(shard_sandbox_app PRIVATE
    ws2_32 engine_auth spdlog::spdlog engine_core
  )
elseif(UNIX)
  # Shard Linux
  find_package(OpenSSL REQUIRED)
  find_path(MYSQL_INCLUDE_DIR NAMES mysql.h mysql/mysql.h PATH_SUFFIXES mysql)
  find_library(MYSQL_LIBRARY NAMES mysqlclient)
  add_executable(shard_app
    main_linux.cpp
    ${SHARDD_GAMEPLAY_SRC}
  )
  target_include_directories(shard_app PRIVATE
    ${MYSQL_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}
  )
  target_compile_definitions(shard_app PRIVATE ENGINE_HAS_MYSQL=1)
  target_link_libraries(shard_app PRIVATE
    pthread OpenSSL::SSL engine_auth spdlog::spdlog ${MYSQL_LIBRARY} engine_core
  )
endif()
CMAKE
```

### Task C12: Build Linux test (sanity check)

- [ ] **Step 1: Configure**

```bash
cmake --preset linux-x64-release 2>&1 | tail -20
```
Expected: `Configuring done` sans erreur.

- [ ] **Step 2: Build**

```bash
cmake --build --preset linux-x64-release 2>&1 | tail -30
```
Expected: `Built target master_app`, `shard_app`, `engine_core`, `world_editor_app`, etc.

- [ ] **Step 3: Si erreurs `#include` non trouvés, fixer manuellement**

Les erreurs typiques :
- `fatal error: src/shared/X.h: No such file or directory` → soit le fichier est mal placé, soit le include path manque dans target_include_directories.
- Solution : ajouter `target_include_directories(<target> PRIVATE ${CMAKE_SOURCE_DIR})` (déjà fait dans templates ci-dessus).

### Task C13: Build Windows test (sanity check)

- [ ] **Step 1: Configure**

```bash
cmake --preset vs2022-x64 2>&1 | tail -20
```

- [ ] **Step 2: Build**

```bash
cmake --build --preset vs2022-x64-release 2>&1 | tail -30
```
Expected: `Built target engine_app`, `world_editor_app`, `shard_sandbox_app`.

### Task C14: Lancer les tests

- [ ] **Step 1: ctest**

```bash
cd build/linux-x64-release && ctest --output-on-failure 2>&1 | tail -30
cd ../..
```
Expected: tous les tests existants passent.

### Task C15: Commit 2 — includes + cmake

- [ ] **Step 1: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
refactor(repo): fix #include paths + refactor CMakeLists for src/ layout

Phase B du re-rangement cmangos-style. Le repo recompile à nouveau.

- ~3000 #include "engine/..." → "src/..." via sed (mapping global).
- CMakeLists.txt racine : add_subdirectory(src) au lieu de engine/server.
- Nouveaux CMakeLists par app : src/{CMakeLists.txt, shared/, masterd/,
  shardd/}/CMakeLists.txt avec file(GLOB_RECURSE) sur les sous-dossiers.
- Nouveaux targets CMake :
  - master_app (Linux master, ex-server_app UNIX)
  - shard_app (Linux shard)
  - shard_sandbox_app (Windows sandbox, ex-server_app WIN32)
- engine_app, world_editor_app : inchangés (CMakeLists racine).

Build Linux + Windows passent ; ctest passe.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase D — CI / scripts / CODEBASE_MAP (Commit 3)

### Task D1: Update `scripts/sync-db-to-docker-deploy.sh`

- [ ] **Step 1: Lire le script**

```bash
cat scripts/sync-db-to-docker-deploy.sh
```

- [ ] **Step 2: Sed `db/` → `sql/` dans le script**

```bash
sed -i 's|/db/|/sql/|g; s|"db"|"sql"|g; s| db/| sql/|g' scripts/sync-db-to-docker-deploy.sh
grep -n "db\|sql" scripts/sync-db-to-docker-deploy.sh
```

### Task D2: Update `scripts/pack-linux-docker-bundle.sh`

- [ ] **Step 1: Sed `db/` → `sql/`**

```bash
sed -i 's|/db/|/sql/|g; s|"db"|"sql"|g; s| db/| sql/|g; s|/db |/sql |g' scripts/pack-linux-docker-bundle.sh
grep -n "db\|sql" scripts/pack-linux-docker-bundle.sh | head -10
```

### Task D3: Update `MigrationRunner` default path

- [ ] **Step 1: Lire le fichier**

```bash
grep -n "db/migrations\|migrations_path" src/masterd/migrations/MigrationRunner.cpp
```

- [ ] **Step 2: Patch default**

```bash
sed -i 's|"db/migrations"|"sql/migrations"|g' src/masterd/migrations/MigrationRunner.cpp
grep -n "sql/migrations\|db/migrations" src/masterd/migrations/MigrationRunner.cpp
```

- [ ] **Step 3: Update config.json default**

```bash
grep -n "db.migrations_path\|migrations" config.json
sed -i 's|"db/migrations"|"sql/migrations"|g' config.json
```

### Task D4: Update `.github/workflows/build-linux.yml`

- [ ] **Step 1: Lire**

```bash
cat .github/workflows/build-linux.yml | head -100
```

- [ ] **Step 2: Sed**

```bash
sed -i 's| db/| sql/|g; s|/db/|/sql/|g; s|"db"|"sql"|g' .github/workflows/build-linux.yml
grep -nE "db|sql" .github/workflows/build-linux.yml | head -10
```

- [ ] **Step 3: Update path target binaire (server_app → master_app + shard_app)**

```bash
grep -n "server_app" .github/workflows/build-linux.yml
sed -i 's|server_app|master_app|g' .github/workflows/build-linux.yml
```

### Task D5: Update `.github/workflows/build-windows.yml`

- [ ] **Step 1: Lire**

```bash
cat .github/workflows/build-windows.yml | head -100
```

- [ ] **Step 2: Vérifier paths shaders**

```bash
grep -n "game/data/shaders\|engine/" .github/workflows/build-windows.yml
```

- [ ] **Step 3: Si `engine/` ou `engine_app` paths spécifiques, fixer**

```bash
# engine_app reste le nom de la cible Windows, pas de changement requis
# Juste vérifier qu'on n'a pas de chemins en dur engine/...
sed -i 's|engine_app\.exe|engine_app.exe|g' .github/workflows/build-windows.yml  # noop, juste pour vérifier
```

### Task D6: Update `CODEBASE_MAP.md`

- [ ] **Step 1: Lire la section 1 (Vue d'ensemble)**

```bash
sed -n '8,40p' CODEBASE_MAP.md
```

- [ ] **Step 2: Update section 1 — nouvelle structure**

Remplacer le bloc ASCII existant par :

```bash
cat > /tmp/section1.txt << 'TXT'
## 1. Vue d'ensemble architecturale

```
┌─────────────────────────────────────────────────────────────────┐
│                        CLIENT (Windows)                         │
│  src/client/auth/   ←→   src/client/render/auth/                │
│  Presenter (logique)         Renderer (affichage ImGui/Vulkan)  │
│         ↕ RenderModel (struct de données UI)                    │
│  src/client/        ←→   src/client/render/                     │
│  HUD, inventaire, chat       Passes Vulkan, terrain             │
└──────────────────────────────────┬──────────────────────────────┘
                                   │ UDP / TCP (src/shared/network/)
┌──────────────────────────────────▼──────────────────────────────┐
│                     SERVEUR MASTER (Linux)                      │
│  src/masterd/handlers/   →  auth, register, shards, terms       │
│  src/shared/db/          →  pool MySQL, migrations              │
└──────────────────────────────────┬──────────────────────────────┘
                                   │ shard tickets
┌──────────────────────────────────▼──────────────────────────────┐
│                     SERVEUR SHARD (Linux)                       │
│  src/shardd/   →  gameplay : quêtes, craft, guildes, combat     │
└─────────────────────────────────────────────────────────────────┘
```

**Technologies :** C++20, Vulkan, ImGui, MySQL, UDP maison, CMake + vcpkg.

**Targets CMake :**
- `engine_app` (Windows client)
- `world_editor_app` (Windows world editor)
- `master_app` (Linux master)
- `shard_app` (Linux shard)
- `shard_sandbox_app` (Windows sandbox dev)

**Lib partagée :** `engine_core` (sources dans `src/shared/`).
TXT
```

Manuel : remplacer dans `CODEBASE_MAP.md` les lignes 8-34 par le contenu de `/tmp/section1.txt`.

- [ ] **Step 3: Update section 5 (Couche serveur — fichiers clés)**

Trouver et remplacer toutes les références `engine/server/` par `src/masterd/` ou `src/shardd/` selon le contexte.

```bash
grep -n "engine/server" CODEBASE_MAP.md | head -20
```

Manuel : pour chaque ligne, remplacer par le bon path `src/masterd/...` ou `src/shardd/...` selon le mapping de la spec.

- [ ] **Step 4: Update section 6 (Couche rendu Vulkan)**

```bash
sed -i 's|engine/render/|src/client/render/|g; s|engine/client/auth/|src/client/auth/|g' CODEBASE_MAP.md
```

- [ ] **Step 5: Update section "Aide-mémoire"**

```bash
sed -i 's|engine/client/auth/screens/|src/client/auth/screens/|g; s|engine/render/auth/screens/|src/client/render/auth/screens/|g' CODEBASE_MAP.md
```

- [ ] **Step 6: Update section 9 (Base de données)**

```bash
sed -i 's|db/migrations/|sql/migrations/|g; s|db/schema|sql/schema|g; s|`db/`|`sql/`|g' CODEBASE_MAP.md
```

- [ ] **Step 7: Update section 10 (Outils et CI)**

```bash
sed -i 's|engine/|src/|g' CODEBASE_MAP.md
# Vérifier qu'on n'a pas créé de doubles src/src/
grep "src/src" CODEBASE_MAP.md | head -5
# Si ça arrive, fix manuel
```

- [ ] **Step 8: Update timestamp en haut**

```bash
sed -i 's|Dernière mise à jour : 2026-04-30|Dernière mise à jour : 2026-05-09 — Réorganisation cmangos-style (src/{shared,client,masterd,shardd,world_editor}, db/ → sql/)|' CODEBASE_MAP.md
```

- [ ] **Step 9: Vérifier — plus aucune référence à engine/**

```bash
grep -n "engine/" CODEBASE_MAP.md | head -5
```
Expected: liste vide (ou seulement les contextes historiques explicites comme "ex-engine/...").

### Task D7: Tester un boot serveur Docker (smoke test)

- [ ] **Step 1: Exécuter le sync script**

```bash
bash scripts/sync-db-to-docker-deploy.sh
ls deploy/docker/sql/migrations/ | head -5
```
Expected: les fichiers SQL sont copiés.

- [ ] **Step 2: Exécuter le pack script**

```bash
bash scripts/pack-linux-docker-bundle.sh 2>&1 | tail -10
ls deploy/docker/*.zip 2>/dev/null
```
Expected: bundle créé sans erreur.

### Task D8: Commit 3 — CI + scripts + CODEBASE_MAP

- [ ] **Step 1: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
refactor(repo): update CI workflows + scripts + CODEBASE_MAP for src/ layout

Phase C du re-rangement cmangos-style.

CI :
- .github/workflows/build-linux.yml : db/ → sql/, server_app → master_app
- .github/workflows/build-windows.yml : pas de changement (paths CMake
  output inchangés, engine_app + world_editor_app conservés).

Scripts :
- scripts/sync-db-to-docker-deploy.sh : sql/ au lieu de db/
- scripts/pack-linux-docker-bundle.sh : sql/ au lieu de db/

Code :
- MigrationRunner default db.migrations_path : "db/migrations" → "sql/migrations"
- config.json : alignement migrations_path

CODEBASE_MAP.md : update sections 1, 5, 6, 9, 10, "Aide-mémoire" pour
refléter src/{shared,client,masterd,shardd,world_editor}. Timestamp
2026-05-09 + note sur la réorg.

Artifacts CI préservés : lcdlln-docker-linux-<sha>, windows-release-<sha>.

Deploiement : REDEPLOIEMENT SERVEUR REQUIS — rename db/ → sql/ dans
l'image Docker.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase E — Web-portal interne (Commit 4)

### Task E1: Réorganiser `web-portal/components/`

- [ ] **Step 1: Créer les sous-dossiers**

```bash
mkdir -p web-portal/components/{auth,bugs,cgu,character,exploits,player,layout}
```

- [ ] **Step 2: Move les composants**

```bash
git mv web-portal/components/AccountForm.tsx web-portal/components/auth/
git mv web-portal/components/PasswordChangeForm.tsx web-portal/components/auth/
git mv web-portal/components/PasswordRecoveryRequestForm.tsx web-portal/components/auth/
git mv web-portal/components/ResetPasswordForm.tsx web-portal/components/auth/
git mv web-portal/components/BugReportForm.tsx web-portal/components/bugs/
git mv web-portal/components/CguAcceptButton.tsx web-portal/components/cgu/
git mv web-portal/components/CharacterDeleteButton.tsx web-portal/components/character/
git mv web-portal/components/ExploitsProfile.tsx web-portal/components/exploits/
git mv web-portal/components/PrivacyForm.tsx web-portal/components/player/
git mv web-portal/components/RecoveryProfileForm.tsx web-portal/components/player/
git mv web-portal/components/SiteHeader.tsx web-portal/components/layout/
git mv web-portal/components/HeaderActions.tsx web-portal/components/layout/
```

- [ ] **Step 3: Vérifier**

```bash
ls web-portal/components/
ls web-portal/components/auth/
```

### Task E2: Réorganiser `web-portal/lib/`

- [ ] **Step 1: Créer les sous-dossiers**

```bash
mkdir -p web-portal/lib/{db,email,auth,exploits}
```

- [ ] **Step 2: Move les helpers**

```bash
git mv web-portal/lib/db.ts web-portal/lib/db/connection.ts
git mv web-portal/lib/email.ts web-portal/lib/email/sender.ts
git mv web-portal/lib/gamePasswordHash.ts web-portal/lib/auth/
git mv web-portal/lib/passwordRecovery.ts web-portal/lib/auth/
git mv web-portal/lib/portalLogin.ts web-portal/lib/auth/
git mv web-portal/lib/session.ts web-portal/lib/auth/
git mv web-portal/lib/exploitTier.ts web-portal/lib/exploits/
git mv web-portal/lib/exploitsData.ts web-portal/lib/exploits/
```

- [ ] **Step 3: Vérifier**

```bash
ls web-portal/lib/
ls web-portal/lib/auth/
```

### Task E3: Update les imports TypeScript

- [ ] **Step 1: Vérifier `tsconfig.json` paths**

```bash
grep -A 5 '"paths"' web-portal/tsconfig.json
```

- [ ] **Step 2: Sed dans tous les .tsx et .ts pour mettre à jour les imports**

```bash
cd web-portal
find app components lib -type f \( -name "*.tsx" -o -name "*.ts" \) -print0 | \
  xargs -0 sed -i \
    -e "s|from '@/lib/db'|from '@/lib/db/connection'|g" \
    -e "s|from '@/lib/email'|from '@/lib/email/sender'|g" \
    -e "s|from '@/lib/gamePasswordHash'|from '@/lib/auth/gamePasswordHash'|g" \
    -e "s|from '@/lib/passwordRecovery'|from '@/lib/auth/passwordRecovery'|g" \
    -e "s|from '@/lib/portalLogin'|from '@/lib/auth/portalLogin'|g" \
    -e "s|from '@/lib/session'|from '@/lib/auth/session'|g" \
    -e "s|from '@/lib/exploitTier'|from '@/lib/exploits/exploitTier'|g" \
    -e "s|from '@/lib/exploitsData'|from '@/lib/exploits/exploitsData'|g" \
    -e "s|from '@/components/AccountForm'|from '@/components/auth/AccountForm'|g" \
    -e "s|from '@/components/PasswordChangeForm'|from '@/components/auth/PasswordChangeForm'|g" \
    -e "s|from '@/components/PasswordRecoveryRequestForm'|from '@/components/auth/PasswordRecoveryRequestForm'|g" \
    -e "s|from '@/components/ResetPasswordForm'|from '@/components/auth/ResetPasswordForm'|g" \
    -e "s|from '@/components/BugReportForm'|from '@/components/bugs/BugReportForm'|g" \
    -e "s|from '@/components/CguAcceptButton'|from '@/components/cgu/CguAcceptButton'|g" \
    -e "s|from '@/components/CharacterDeleteButton'|from '@/components/character/CharacterDeleteButton'|g" \
    -e "s|from '@/components/ExploitsProfile'|from '@/components/exploits/ExploitsProfile'|g" \
    -e "s|from '@/components/PrivacyForm'|from '@/components/player/PrivacyForm'|g" \
    -e "s|from '@/components/RecoveryProfileForm'|from '@/components/player/RecoveryProfileForm'|g" \
    -e "s|from '@/components/SiteHeader'|from '@/components/layout/SiteHeader'|g" \
    -e "s|from '@/components/HeaderActions'|from '@/components/layout/HeaderActions'|g"
cd ..
```

- [ ] **Step 3: Vérifier — pas de pattern `@/lib/X'` ou `@/components/X'` cassé**

```bash
cd web-portal
grep -rn "from '@/lib/" app/ components/ lib/ | grep -v "lib/db/\|lib/email/\|lib/auth/\|lib/exploits/" | head -10
grep -rn "from '@/components/" app/ components/ | grep -v "components/auth/\|components/bugs/\|components/cgu/\|components/character/\|components/exploits/\|components/player/\|components/layout/\|components/admin/" | head -10
cd ..
```
Expected: lignes vides (tout fixé) ou cas spécifiques à corriger manuellement.

### Task E4: Test `npm run build` web-portal

- [ ] **Step 1: Installer les deps**

```bash
cd web-portal
npm install 2>&1 | tail -3
cd ..
```

- [ ] **Step 2: Build**

```bash
cd web-portal
npm run build 2>&1 | tail -20
cd ..
```
Expected: `Compiled successfully` sans erreur.

### Task E5: Smoke test Docker build web-portal

- [ ] **Step 1: Sync vers deploy/docker**

```bash
bash scripts/sync-web-portal-to-docker-deploy.sh
ls deploy/docker/web-portal/components/auth/ 2>&1
```

- [ ] **Step 2: Optionnel — tester docker build localement**

```bash
cd deploy/docker
docker compose build web-portal 2>&1 | tail -10
cd ../..
```
Expected: image buildée sans erreur (ou skip si docker non installé localement).

### Task E6: Commit 4 — web-portal interne

- [ ] **Step 1: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
refactor(web-portal): organize components/ and lib/ by domain

Phase D du re-rangement cmangos-style.

components/ : split par domaine
- auth/ : AccountForm, Password*, ResetPasswordForm
- bugs/ : BugReportForm
- cgu/ : CguAcceptButton
- character/ : CharacterDeleteButton
- exploits/ : ExploitsProfile
- player/ : PrivacyForm, RecoveryProfileForm
- layout/ : SiteHeader, HeaderActions
- admin/ : (déjà groupé) BugAdmin, CguManager, FaqAdmin, PlayerActions

lib/ : split par domaine
- db/connection.ts (ex-db.ts)
- email/sender.ts (ex-email.ts)
- auth/ : gamePasswordHash, passwordRecovery, portalLogin, session
- exploits/ : exploitTier, exploitsData

Imports TypeScript mis à jour dans app/, components/, lib/.
npm run build passe.

Deploiement : pas de redéploiement serveur supplémentaire (déjà inclus
dans le bundle Docker du commit 3).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase F — Push + PR

### Task F1: Push la branche et créer la PR

- [ ] **Step 1: Push**

```bash
git push -u origin claude/reorg-cmangos-style 2>&1 | tail -3
```

- [ ] **Step 2: Créer la PR**

```bash
"/c/Program Files/GitHub CLI/gh.exe" pr create --title "refactor: cmangos-style reorganization (src/{shared,client,masterd,shardd,world_editor})" --body "$(cat <<'EOF'
## Réorganisation cmangos-style

Spec : [docs/superpowers/specs/2026-05-09-cmangos-style-reorganization-design.md](docs/superpowers/specs/2026-05-09-cmangos-style-reorganization-design.md)

### Résumé

Réorganise la racine du repo pour calquer la structure de [cmangos-tbc/src](https://github.com/cmangos/mangos-tbc/tree/master/src) avec un sous-dossier par domaine. Le code C++ passe de `engine/` à `src/{shared,client,masterd,shardd,world_editor}`. Le portail Next.js (`web-portal/`) est aussi réorganisé internement.

### 4 commits

1. **moves only** : `git mv` massif (~600 fichiers), repo ne compile pas (intentionnel, minimise la review surface).
2. **#include + CMakeLists** : ~3000 includes mis à jour via sed, nouveaux CMakeLists par app, repo recompile.
3. **CI + scripts + CODEBASE_MAP** : workflows mis à jour, `db/` → `sql/`, doc à jour.
4. **web-portal** : `components/<domain>/` et `lib/<domain>/` + imports TypeScript.

### Targets CMake

| Avant | Après |
|---|---|
| `server_app` (UNIX) | `master_app` |
| `server_app` (WIN32) | `shard_sandbox_app` |
| (n/a) | `shard_app` (UNIX, ex-main_shard_linux.cpp) |
| `engine_app` | `engine_app` (inchangé) |
| `world_editor_app` | `world_editor_app` (inchangé) |

### Test plan

- [x] Build Linux passe (`master_app`, `shard_app`, `engine_core`, `world_editor_app`).
- [x] Build Windows passe (`engine_app.exe`, `world_editor_app.exe`, `shard_sandbox_app.exe`).
- [x] ctest passe (~30 tests existants).
- [x] `npm run build` web-portal passe.
- [x] `docker compose build web-portal` passe localement.
- [ ] CI Linux artifact `lcdlln-docker-linux-<sha>` produit avec succès.
- [ ] CI Windows artifact `windows-release-<sha>` produit avec succès.

**Déploiement** : ⚠️ **REDÉPLOIEMENT SERVEUR REQUIS** — rename `db/` → `sql/` synchronisé avec l'image Docker (master + shard + web-portal en lock-step via le bundle).

Generated with Claude Code
EOF
)"
```

---

## Self-Review

**Spec coverage** :
- Section 1 (top-level) : couvert par tâches B1, A2.
- Section 2 (`src/shared/`) : couvert par B2, B9, B15, B16, B17.
- Section 3 (`src/client/`) : couvert par B3, B4, B5, B7, B8.
- Section 4 (`src/masterd/`) : couvert par B10, B12, B13, B14, B20.
- Section 5 (`src/shardd/`) : couvert par B11, B18, B19, B20.
- Section 6 (`src/world_editor/`) : couvert par B6, B7.
- Section 7 (web-portal) : couvert par phase E.
- CMake : couvert par C7-C11.
- CI/scripts : couvert par phase D.
- 3 commits + commit 4 web-portal : couvert par tâches B22, C15, D8, E6.

**Placeholder scan** : aucun TBD/TODO/placeholder dans les steps. Les commandes sont concrètes et exécutables.

**Type/path consistency** : les paths sources et destinations sont cohérents entre les phases B (move) et C (sed). Le mapping est explicite dans Task C1.

**Ambiguïtés résolues** :
- `engine/server/main.cpp` (générique, non spécifique master/shard) → `src/shared/server_bootstrap/main.cpp` (utilisé comme common bootstrap, à confirmer en lisant le contenu lors de l'exécution).
- `engine/server/ServerProtocol.cpp` (utilisé par master + shard + client) → `src/shared/network/ServerProtocol.cpp`.
- `engine/server/ServerApp.cpp` (utilisé en UNIX et WIN32 server_app) → `src/shared/server_bootstrap/ServerApp.cpp`.
- Le helper `lcdlln_add_simple_test()` reste dans le `CMakeLists.txt` racine (pas déplacé).
- `add_library(engine_core)` reste dans le `CMakeLists.txt` racine ; les sources sont ajoutées via `target_sources(engine_core PRIVATE ...)` dans `src/shared/CMakeLists.txt`.
