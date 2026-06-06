# Horloge monde serveur-autoritaire — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rendre l'heure de jeu (jour/nuit + phase de lune) serveur-autoritaire : le master détient une horloge monde unique, les clients la synchronisent et la calculent localement, avec contrôle de dérive et commandes admin RBAC.

**Architecture:** Une formule déterministe **partagée** (header-only `WorldClock`) calcule `gameSec` à partir de `{epoch, timeScale, offset, paused}` ; le master détient l'état mutable + le diffuse (opcodes 200-202, calque lunaire) ; le client le reçoit, calcule soleil **et** lune localement, et resynchronise périodiquement. Les modifications passent par les commandes admin existantes (opcode 195, RBAC `administrator`). Le système lunaire est rebranché sur la même horloge.

**Tech Stack:** C++17, Vulkan (client), sérialisation wire little-endian maison, RBAC slash commands, ctest (CI Linux).

**Spec de référence:** `docs/superpowers/specs/2026-06-06-horloge-monde-serveur-autoritaire-design.md`

**Règles repo critiques:**
- Nouveau `.cpp` partagé compilé côté serveur → l'ajouter AUSSI à `server_app` (et `shard_app` si utilisé shard) dans `src/CMakeLists.txt`.
- Tests : pattern cross-platform `add_executable + target_link_libraries + add_test` (PAS `lcdlln_add_simple_test`, incompatible MSVC).
- Commandes admin : déclarées en JSON AVANT impl, RBAC + log serveur obligatoires.
- PascalCase pour nouveaux fichiers/classes ; commentaires en français.
- Chaque sous-chantier = 1 PR. **Redéploiement serveur master requis** (lock-step client+serveur).

---

## Structure des fichiers

**Créés:**
- `src/shared/world/WorldClock.h` — formule déterministe partagée (header-only).
- `src/shared/world/tests/WorldClockTests.cpp` — tests unitaires de la formule.
- `src/shared/network/WorldClockPayloads.h` / `.cpp` — wire (structs + Build/Parse).
- `src/shared/network/tests/WorldClockPayloadsTests.cpp` — round-trip wire.
- `src/masterd/handlers/worldclock/WorldClockHandler.h` / `.cpp` — état mutable + handler + broadcast.

**Modifiés:**
- `src/shared/network/ProtocolV1Constants.h` — opcodes 200-202.
- `src/masterd/main_linux.cpp` — instanciation/wiring/dispatch/config.
- `src/masterd/handlers/admin/AdminCommandHandler.{h,cpp}` — 3 commandes + wiring vers WorldClockHandler.
- `game/data/config/slash_commands.json` — 3 commandes admin.
- `src/masterd/handlers/lunar/LunarHandler.cpp` — phase dérivée de l'horloge monde.
- `src/client/render/DayNightCycle.{h,cpp}` — mode piloté par horloge synchronisée.
- `src/client/app/Engine.cpp` — sync (req 200, handle 201/202) + contrôle dérive.
- `config.json` — bloc `game.worldclock` (epoch, timeScale, lunarPeriodGameDays, driftCheckSec, driftThresholdGameSec).
- `src/CMakeLists.txt` — sources partagées dans `server_app`/`shard_app` + cibles de test.

---

# Sous-chantier 1 — Horloge partagée (header-only) + tests  [PR 1]

### Task 1.1 : Formule déterministe `WorldClock`

**Files:**
- Create: `src/shared/world/WorldClock.h`
- Test: `src/shared/world/tests/WorldClockTests.cpp`
- Modify: `src/CMakeLists.txt` (cible test)

- [ ] **Step 1 : Écrire le test (échoue à la compilation)**

```cpp
// src/shared/world/tests/WorldClockTests.cpp
#include "src/shared/world/WorldClock.h"
#include <cstdio>
#include <cmath>

using engine::world::WorldClockParams;
using engine::world::GameSeconds;
using engine::world::TimeOfDayHours;
using engine::world::LunarPhase;

static int g_failed = 0;
#define CHECK(cond) do { if(!(cond)){ std::fprintf(stderr,"[FAIL] %s:%d %s\n",__FILE__,__LINE__,#cond); ++g_failed; } } while(0)
static bool nearly(double a, double b, double eps=1e-3){ return std::fabs(a-b) < eps; }

int main()
{
    WorldClockParams p;                  // epoch=2026-01-01, timeScale=60, offset=0, lunarPeriod=16 jours-jeu
    p.epochRefUnixMs = 1000ull;          // epoch arbitraire pour le test
    p.timeScaleRealMinPerDay = 60.0f;    // 60 min réelles = 1 jour jeu => 1 s réelle = 24 s jeu

    // À epoch, gameSec = offset = 0 -> 00:00.
    CHECK(nearly(GameSeconds(1000ull, p), 0.0));
    CHECK(nearly(TimeOfDayHours(0.0), 0.0));

    // 1 s réelle après epoch -> 24 s de jeu.
    CHECK(nearly(GameSeconds(2000ull, p), 24.0));

    // 3600 s réelles (1 h) -> 86400 s de jeu = 1 jour complet -> timeOfDay revient à 0.
    CHECK(nearly(GameSeconds(1000ull + 3600ull*1000ull, p), 86400.0, 1.0));
    CHECK(nearly(TimeOfDayHours(86400.0), 0.0));

    // offset : +12h de jeu (43200 s) décale midi.
    p.offsetGameSec = 43200.0;
    CHECK(nearly(TimeOfDayHours(GameSeconds(1000ull, p)), 12.0));

    // paused : fige sur pausedAtGameSec quelle que soit l'heure réelle.
    WorldClockParams pp; pp.paused = true; pp.pausedAtGameSec = 7200.0; // 02:00
    CHECK(nearly(GameSeconds(999999ull, pp), 7200.0));
    CHECK(nearly(TimeOfDayHours(7200.0), 2.0));

    // Lune : à gameSec=0 -> phase 0 ; à la moitié de la période -> phase 8.
    const double period = 16.0*86400.0;
    CHECK(LunarPhase(0.0, period) == 0);
    CHECK(LunarPhase(period*0.5 + 1.0, period) == 8);
    CHECK(LunarPhase(period - 1.0, period) == 15);

    if (g_failed == 0) std::printf("[OK] WorldClockTests\n");
    return g_failed;
}
```

- [ ] **Step 2 : Écrire `WorldClock.h`**

```cpp
// src/shared/world/WorldClock.h
#pragma once
#include <cstdint>
#include <cmath>

namespace engine::world
{
    /// Paramètres de l'horloge monde. Source de vérité côté master ; répliqués
    /// au client. La formule est PURE et déterministe (mêmes entrées -> même
    /// sortie sur master, shard, client) pour garantir la synchronisation.
    struct WorldClockParams
    {
        uint64_t epochRefUnixMs        = 1767225600000ull; ///< 2026-01-01 00:00 UTC.
        float    timeScaleRealMinPerDay = 60.0f;           ///< minutes RÉELLES par jour de jeu (60 = 1 jour/h).
        double   offsetGameSec          = 0.0;             ///< décalage runtime (/settime). Non persisté.
        bool     paused                 = false;           ///< /pausetime : fige l'horloge.
        double   pausedAtGameSec        = 0.0;             ///< valeur figée quand paused.
        double   lunarPeriodGameSec     = 16.0 * 86400.0;  ///< cycle lunaire en SECONDES DE JEU (16 jours de jeu).
    };

    /// Secondes de jeu écoulées depuis l'epoch, d'après l'horloge réelle.
    /// \param nowUnixMs horloge réelle (ms) au moment du calcul.
    inline double GameSeconds(uint64_t nowUnixMs, const WorldClockParams& p)
    {
        if (p.paused) return p.pausedAtGameSec;
        const double realSec = (nowUnixMs >= p.epochRefUnixMs)
            ? static_cast<double>(nowUnixMs - p.epochRefUnixMs) / 1000.0 : 0.0;
        // secondes de jeu par seconde réelle = 86400 / (timeScale * 60).
        const double gsPerRs = 86400.0 / (static_cast<double>(p.timeScaleRealMinPerDay) * 60.0);
        return realSec * gsPerRs + p.offsetGameSec;
    }

    /// Heure du jour [0,24) à partir des secondes de jeu.
    inline float TimeOfDayHours(double gameSec)
    {
        double h = std::fmod(gameSec / 3600.0, 24.0);
        if (h < 0.0) h += 24.0;
        return static_cast<float>(h);
    }

    /// Phase de lune 0..15 à partir des secondes de jeu et de la période (s de jeu).
    inline uint8_t LunarPhase(double gameSec, double lunarPeriodGameSec)
    {
        if (lunarPeriodGameSec <= 0.0) return 0u;
        double pos = std::fmod(gameSec, lunarPeriodGameSec);
        if (pos < 0.0) pos += lunarPeriodGameSec;
        int phase = static_cast<int>((pos / lunarPeriodGameSec) * 16.0);
        if (phase < 0) phase = 0;
        if (phase > 15) phase = 15;
        return static_cast<uint8_t>(phase);
    }
}
```

- [ ] **Step 3 : Ajouter la cible de test (cross-platform) dans `src/CMakeLists.txt`**

```cmake
add_executable(world_clock_tests src/shared/world/tests/WorldClockTests.cpp)
target_link_libraries(world_clock_tests PRIVATE engine_core)
add_test(NAME world_clock_tests COMMAND world_clock_tests)
```
(Header-only → pas de source à ajouter à `server_app`/`shard_app`. NE PAS utiliser `lcdlln_add_simple_test`.)

- [ ] **Step 4 : Commit**

```bash
git add src/shared/world/WorldClock.h src/shared/world/tests/WorldClockTests.cpp src/CMakeLists.txt
git commit -m "feat(worldclock): formule horloge monde deterministe partagee + tests"
```

---

# Sous-chantier 2 — Wire (payloads + opcodes) + tests  [PR 2]

### Task 2.1 : Opcodes 200-202

**Files:** Modify `src/shared/network/ProtocolV1Constants.h` (après la ligne 794, bloc lunaire)

- [ ] **Step 1 : Ajouter les opcodes**

```cpp
	// Phase 6 — Horloge monde serveur-autoritaire (jour/nuit + lune unifies).
	//   - State (200/201)                 : etat de l'horloge sur connexion / contrôle de derive.
	//   - ChangeNotification (202, push)   : un admin a modifie le temps.
	constexpr uint16_t kOpcodeWorldClockStateRequest        = 200u; ///< Client to Master : etat horloge (vide).
	constexpr uint16_t kOpcodeWorldClockStateResponse       = 201u; ///< Master to Client : epoch, timeScale, offset, paused, lunarPeriod + serverTimeUnixMs.
	constexpr uint16_t kOpcodeWorldClockChangeNotification  = 202u; ///< Master to Client (push, request_id=0) : changement admin.
```

- [ ] **Step 2 : Commit** `git add -A && git commit -m "feat(worldclock): opcodes 200-202"`

### Task 2.2 : Payloads (calque `LunarPayloads`)

**Files:**
- Create: `src/shared/network/WorldClockPayloads.h` / `.cpp`
- Test: `src/shared/network/tests/WorldClockPayloadsTests.cpp`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1 : Écrire le test round-trip (échoue)**

```cpp
// src/shared/network/tests/WorldClockPayloadsTests.cpp
#include "src/shared/network/WorldClockPayloads.h"
#include <cstdio>
#include <cmath>
using namespace engine::network::worldclock;
static int g_failed=0;
#define CHECK(c) do{ if(!(c)){ std::fprintf(stderr,"[FAIL] %s:%d %s\n",__FILE__,__LINE__,#c); ++g_failed; } }while(0)

int main()
{
    WorldClockStateResponse r;
    r.status = WorldClockStatus::Ok;
    r.serverTimeUnixMs = 123456789ull;
    r.epochRefUnixMs   = 1767225600000ull;
    r.timeScaleRealMinPerDay = 60.0f;
    r.offsetGameSec    = 43200.0;
    r.paused           = true;
    r.pausedAtGameSec  = 7200.0;
    r.lunarPeriodGameSec = 16.0*86400.0;

    std::vector<uint8_t> buf;
    BuildWorldClockStateResponsePayload(r, buf);
    WorldClockStateResponse out;
    CHECK(ParseWorldClockStateResponsePayload(buf.data(), buf.size(), out));
    CHECK(out.status == WorldClockStatus::Ok);
    CHECK(out.serverTimeUnixMs == r.serverTimeUnixMs);
    CHECK(out.epochRefUnixMs == r.epochRefUnixMs);
    CHECK(std::fabs(out.offsetGameSec - r.offsetGameSec) < 1e-6);
    CHECK(out.paused == true);
    CHECK(std::fabs(out.lunarPeriodGameSec - r.lunarPeriodGameSec) < 1e-3);

    // Rejet taille incorrecte.
    CHECK(!ParseWorldClockStateResponsePayload(buf.data(), buf.size()-1, out));

    // Notification = mêmes champs.
    std::vector<uint8_t> nbuf;
    BuildWorldClockChangeNotificationPayload(r, nbuf);
    WorldClockStateResponse n;
    CHECK(ParseWorldClockChangeNotificationPayload(nbuf.data(), nbuf.size(), n));
    CHECK(std::fabs(n.timeScaleRealMinPerDay - 60.0f) < 1e-3f);

    if(g_failed==0) std::printf("[OK] WorldClockPayloadsTests\n");
    return g_failed;
}
```

- [ ] **Step 2 : Écrire `WorldClockPayloads.h`**

```cpp
// src/shared/network/WorldClockPayloads.h
#pragma once
#include <cstdint>
#include <vector>

namespace engine::network::worldclock
{
    enum class WorldClockStatus : uint8_t { Ok = 0, Unauthorized = 1 };

    struct WorldClockStateRequest {}; // vide

    /// État complet de l'horloge (réponse 201 ET notification 202 : mêmes champs).
    struct WorldClockStateResponse
    {
        WorldClockStatus status = WorldClockStatus::Ok;
        uint64_t serverTimeUnixMs    = 0;   ///< horloge serveur (calage client).
        uint64_t epochRefUnixMs      = 0;
        float    timeScaleRealMinPerDay = 60.0f;
        double   offsetGameSec       = 0.0;
        uint8_t  paused              = 0;   ///< 0/1.
        double   pausedAtGameSec     = 0.0;
        double   lunarPeriodGameSec  = 0.0;
    };

    void BuildWorldClockStateRequestPayload(std::vector<uint8_t>& out);
    void BuildWorldClockStateResponsePayload(const WorldClockStateResponse& msg, std::vector<uint8_t>& out);
    void BuildWorldClockChangeNotificationPayload(const WorldClockStateResponse& msg, std::vector<uint8_t>& out);

    bool ParseWorldClockStateRequestPayload(const uint8_t* data, size_t size, WorldClockStateRequest& out);
    bool ParseWorldClockStateResponsePayload(const uint8_t* data, size_t size, WorldClockStateResponse& out);
    bool ParseWorldClockChangeNotificationPayload(const uint8_t* data, size_t size, WorldClockStateResponse& out);
}
```

- [ ] **Step 3 : Écrire `WorldClockPayloads.cpp`** — calque les helpers Write/Read LE de `LunarPayloads.cpp` (copier `WriteU8/WriteU32LE/WriteU64LE/WriteFloatLE` + lecture). Ajouter un `WriteDoubleLE`/`ReadDoubleLE` (memcpy 8 octets). Sérialiser dans l'ordre des champs ; `Build*ChangeNotification` = même corps que `Build*StateResponse`. `Parse*` : vérifier la taille EXACTE (status 1 + serverTime 8 + epoch 8 + timeScale 4 + offset 8 + paused 1 + pausedAt 8 + lunarPeriod 8 = **46 octets** pour la réponse ; la notification omet `status` -> **45 octets**, OU réutilise la même structure avec status -> garder 46 pour simplicité). Rejeter short ET extra.

> Détail de sérialisation `BuildWorldClockStateResponsePayload` : `WriteU8(status)`, `WriteU64LE(serverTimeUnixMs)`, `WriteU64LE(epochRefUnixMs)`, `WriteFloatLE(timeScaleRealMinPerDay)`, `WriteDoubleLE(offsetGameSec)`, `WriteU8(paused)`, `WriteDoubleLE(pausedAtGameSec)`, `WriteDoubleLE(lunarPeriodGameSec)`. La notification appelle la même fonction (status=Ok).

- [ ] **Step 4 : CMake** — ajouter `WorldClockPayloads.cpp` à `engine_core` ET à `server_app` (et `shard_app` si le shard l'utilise) dans `src/CMakeLists.txt`. Ajouter la cible test :
```cmake
add_executable(world_clock_payloads_tests src/shared/network/tests/WorldClockPayloadsTests.cpp)
target_link_libraries(world_clock_payloads_tests PRIVATE engine_core)
add_test(NAME world_clock_payloads_tests COMMAND world_clock_payloads_tests)
```

- [ ] **Step 5 : Commit** `git commit -m "feat(worldclock): payloads wire + round-trip tests"`

---

# Sous-chantier 3 — Handler master + état mutable  [PR 3]

### Task 3.1 : `WorldClockHandler` (calque `LunarHandler`)

**Files:**
- Create: `src/masterd/handlers/worldclock/WorldClockHandler.h` / `.cpp`
- Modify: `src/masterd/main_linux.cpp`, `src/CMakeLists.txt`

- [ ] **Step 1 : `WorldClockHandler.h`** — classe avec :
  - `SetServer/SetSessionManager/SetConnectionSessionMap` (comme LunarHandler).
  - `void Configure(const engine::world::WorldClockParams& p)` (params boot depuis config).
  - `void HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, size)` → si opcode 200, valide session (comme LunarHandler) → `BuildWorldClockStateResponsePayload` avec `serverTimeUnixMs = now()`, params courants → Send 201. Si session invalide → `status=Unauthorized`.
  - **Mutateurs (appelés par AdminCommandHandler), chacun broadcast** :
    - `bool SetTimeOfDay(float hours)` → calcule `offsetGameSec` pour que `TimeOfDayHours(GameSeconds(now)) == hours` (voir formule ci-dessous) → broadcast.
    - `bool SetPaused(bool paused)` → si on met paused, mémoriser `pausedAtGameSec = GameSeconds(now)` ; broadcast.
    - `bool SetTimeScale(float realMinPerDay)` → borne [1,1440] ; **conserver la continuité** : recalculer `offsetGameSec` pour que `GameSeconds(now)` soit inchangé juste après le changement de scale ; broadcast.
  - `engine::world::WorldClockParams GetParams() const` (mutex-protégé) — pour le shard/lunaire/queries.
  - `void BroadcastChange()` (privé) : `BuildWorldClockChangeNotificationPayload` + `BuildPushPacket(202, payload)` + `m_connMap->Snapshot()` + Send-all (exact calque `LunarHandler::PushPhaseChangeBroadcast`).
  - Membres : `WorldClockParams m_params; std::mutex m_mutex;` + pointeurs services.

  **Formule `SetTimeOfDay`** : `targetSec = hours*3600` ; `base = GameSeconds(now, paramsAvecOffset0) `; `m_params.offsetGameSec += targetSec - std::fmod(base + m_params.offsetGameSec, 86400.0)` (ramène la composante jour à `targetSec` sans toucher la composante long-terme). Documenter que cela décale aussi la phase lunaire (effet de bord assumé v1).

- [ ] **Step 2 : `WorldClockHandler.cpp`** — implémenter (calque `LunarHandler.cpp` pour auth/send/broadcast). Utiliser `engine::world::GameSeconds` etc. depuis `WorldClock.h`. `now()` = `std::chrono::system_clock` ms.

- [ ] **Step 3 : Wiring `main_linux.cpp`** (calque lunaire) :
```cpp
engine::server::WorldClockHandler worldClockHandler;
worldClockHandler.SetServer(&server);
worldClockHandler.SetSessionManager(&sessionManager);
worldClockHandler.SetConnectionSessionMap(&connSessionMap);
engine::world::WorldClockParams wcParams;
wcParams.epochRefUnixMs         = static_cast<uint64_t>(config.GetInt("game.worldclock.epoch_ms", 1767225600000ll));
wcParams.timeScaleRealMinPerDay = static_cast<float>(config.GetDouble("game.worldclock.timescale_real_min_per_day", 60.0));
wcParams.lunarPeriodGameSec     = config.GetDouble("game.worldclock.lunar_period_game_days", 16.0) * 86400.0;
worldClockHandler.Configure(wcParams);
// dispatch :
else if (opcode == kOpcodeWorldClockStateRequest)
    worldClockHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
```

- [ ] **Step 4 : CMake** — ajouter `WorldClockHandler.cpp` à `server_app` (`src/CMakeLists.txt`). (Pas client.)

- [ ] **Step 5 : Commit** `git commit -m "feat(worldclock): handler master + etat mutable + broadcast"`

---

# Sous-chantier 4 — Commandes admin RBAC  [PR 4]

### Task 4.1 : Déclarer les commandes en JSON (AVANT impl)

**Files:** Modify `game/data/config/slash_commands.json`

- [ ] **Step 1** : Ajouter 3 entrées (calque l'entrée `/sky moon`), `minRole: "administrator"`, `serverInteraction: true`, `serverLogged: true`, `status: "implemented"`, `implementation_file: "src/masterd/handlers/admin/AdminCommandHandler.cpp"` :
  - `/settime <HH:MM>` — « Fixe l'heure du jour (horloge monde serveur, broadcast à tous). »
  - `/pausetime <on|off>` — « Gèle ou reprend l'horloge monde. »
  - `/settimescale <minutes réelles par jour>` — « Vitesse du cycle jour/nuit (1..1440). »
- [ ] **Step 2** : Commit `git commit -m "feat(worldclock): declare slash commands admin (settime/pausetime/settimescale)"`

### Task 4.2 : Dispatch dans `AdminCommandHandler`

**Files:** Modify `src/masterd/handlers/admin/AdminCommandHandler.{h,cpp}`, `src/masterd/main_linux.cpp`

- [ ] **Step 1** : `AdminCommandHandler.h` — ajouter `void SetWorldClockHandler(WorldClockHandler* h){ m_worldClock=h; }` + membre `WorldClockHandler* m_worldClock=nullptr;` + déclarer `DispatchSetTime/DispatchPauseTime/DispatchSetTimeScale`.
- [ ] **Step 2** : `AdminCommandHandler.cpp` — implémenter les 3 dispatch (calque `DispatchSkyMoon`) : valider args (parser `HH:MM`, `on/off`, entier borné), appeler `m_worldClock->SetTimeOfDay/SetPaused/SetTimeScale`, remplir `resp.status=Ok`/`InvalidArgs`. Le RBAC `administrator` est déjà appliqué en amont par le flux AdminCommand (vérifier le rôle min via le registre/slash_commands.json). `LogAudit` est déjà appelé par le flux. Brancher la table de dispatch par nom de commande (là où `/sky moon` est routé).
- [ ] **Step 3** : `main_linux.cpp` — `adminCommandHandler.SetWorldClockHandler(&worldClockHandler);`.
- [ ] **Step 4** : Commit `git commit -m "feat(worldclock): commandes admin settime/pausetime/settimescale (RBAC+log)"`

> Test RBAC : un test handler master (si l'infra de test AdminCommand existe) vérifie qu'un rôle < administrator reçoit `Unauthorized`. Sinon, validation manuelle documentée dans la PR.

---

# Sous-chantier 5 — Unification lune (refactor prudent)  [PR 5]

### Task 5.1 : Lune dérivée de l'horloge monde

**Files:** Modify `src/masterd/handlers/lunar/LunarHandler.cpp`, adapter tests lunaires.

- [ ] **Step 1** : Injecter l'horloge dans le lunaire — donner à `LunarHandler` un pointeur vers `WorldClockHandler` (`SetWorldClock(WorldClockHandler*)`), wiré dans `main_linux.cpp`.
- [ ] **Step 2** : Remplacer le calcul de phase : au lieu de `LunarCalendar::Compute(realNowMs, m_cycleStartMs, m_cycleDurationMs)`, calculer `gameSec = engine::world::GameSeconds(now, m_worldClock->GetParams())` puis `phase = engine::world::LunarPhase(gameSec, params.lunarPeriodGameSec)` et `illumination = LunarCalendar::ComputeIllumination(phase)` (réutilise la courbe existante). Le `Tick` compare la phase issue de l'horloge monde.
- [ ] **Step 3** : `GameEventManager` — vérifier que rien ne casse : les événements lunaires lisent `LunarHandler::CurrentPhase()` qui retourne désormais la phase horloge-monde. Si un event doit rester sur le **calendrier réel**, il peut lire `nowUnixMs` directement (documenter). Aucun changement de comportement attendu hors cadence.
- [ ] **Step 4** : Adapter les tests lunaires existants (`LunarCalendar`/handler) pour la nouvelle source ; garder un test prouvant que phase=f(gameSec).
- [ ] **Step 5** : Commit `git commit -m "refactor(lunar): phase derivee de l'horloge monde unifiee"`

> ⚠️ Risque principal (cf. spec §8) : la cadence lunaire devient temps-de-jeu. Valider en revue que `GameEventManager` / festivals restent cohérents.

---

# Sous-chantier 6 — Intégration client (sync + dérive)  [PR 6]

### Task 6.1 : `DayNightCycle` piloté par horloge synchronisée

**Files:** Modify `src/client/render/DayNightCycle.{h,cpp}`

- [ ] **Step 1** : Ajouter un mode « piloté serveur » :
  - `void SetServerClock(const engine::world::WorldClockParams& p, uint64_t serverTimeUnixMs, uint64_t clientRecvUnixMs)` — stocke les params + le calage (`m_clockOffsetMs = serverTimeUnixMs - clientRecvUnixMs`), passe `m_driven=true`.
  - En mode `m_driven`, `Advance(dt)` ne fait plus avancer `m_timeOfDay` librement : il calcule `nowServerMs = clientNowMs + m_clockOffsetMs`, `gameSec = GameSeconds(nowServerMs, m_params)`, `m_timeOfDay = TimeOfDayHours(gameSec)`, `moonPhase = LunarPhase(gameSec, m_params.lunarPeriodGameSec)`, `moonIllumination = LunarCalendar::ComputeIllumination(moonPhase)` (ou courbe locale équivalente), puis `ComputeState()`.
  - **Contrôle de dérive** (le client redemande l'état périodiquement, voir Task 6.2) : à réception d'un nouvel état, si l'écart entre l'heure calculée et l'heure serveur > seuil → corriger en douceur (`m_timeOfDay = lerp(m_timeOfDay, serverTimeOfDay, k)` sur quelques frames) au lieu de sauter.
  - **Fallback** : si jamais `SetServerClock` n'est appelé (solo/serveur muet), conserver le comportement local actuel (`m_driven=false`). Pas de régression.
- [ ] **Step 2** : Commit `git commit -m "feat(client): DayNightCycle pilote par horloge serveur + fallback local"`

### Task 6.2 : Sync réseau dans `Engine`

**Files:** Modify `src/client/app/Engine.cpp`, `config.json`

- [ ] **Step 1** : À l'entrée monde (là où le client envoie déjà `LunarStateRequest`), envoyer aussi `WorldClockStateRequest` (opcode 200) via le même dispatcher.
- [ ] **Step 2** : Ajouter le handling réception (calque le bloc lunaire `case kOpcodeLunarStateResponse`) :
```cpp
case kOpcodeWorldClockStateResponse:
case kOpcodeWorldClockChangeNotification: {
    engine::network::worldclock::WorldClockStateResponse parsed;
    bool ok = (opcode == kOpcodeWorldClockStateResponse)
        ? engine::network::worldclock::ParseWorldClockStateResponsePayload(payload, payloadSize, parsed)
        : engine::network::worldclock::ParseWorldClockChangeNotificationPayload(payload, payloadSize, parsed);
    if (!ok || parsed.status != engine::network::worldclock::WorldClockStatus::Ok) return;
    engine::world::WorldClockParams p;
    p.epochRefUnixMs = parsed.epochRefUnixMs; p.timeScaleRealMinPerDay = parsed.timeScaleRealMinPerDay;
    p.offsetGameSec = parsed.offsetGameSec; p.paused = parsed.paused != 0;
    p.pausedAtGameSec = parsed.pausedAtGameSec; p.lunarPeriodGameSec = parsed.lunarPeriodGameSec;
    const uint64_t clientNow = /* now ms */;
    m_dayNight.SetServerClock(p, parsed.serverTimeUnixMs, clientNow);
    return;
}
```
- [ ] **Step 2b** : Contrôle de dérive — toutes les `game.worldclock.drift_check_sec` (config, défaut 300), renvoyer un `WorldClockStateRequest` léger (timer dans `Update`). La réception (ci-dessus) recale ; `DayNightCycle` corrige en douceur si l'écart > `drift_threshold_game_sec` (config, défaut 30).
- [ ] **Step 3** : `config.json` — bloc `game.worldclock` (epoch_ms, timescale_real_min_per_day, lunar_period_game_days, drift_check_sec, drift_threshold_game_sec). Documenter « CLIENT lit drift_* ; epoch/timescale/lunar_period sont AUTORITÉ master ».
- [ ] **Step 4** : Commit `git commit -m "feat(client): sync horloge monde (req 200, handle 201/202) + controle derive"`

---

## Ordre de merge & déploiement

PR 1 → PR 2 → PR 3 → PR 4 → PR 5 → PR 6 (dépendances séquentielles). Les PR 1-2 (formule + wire) sont sûres (tests CI). PR 3-5 = serveur. PR 6 = client.

⚠️ **REDÉPLOIEMENT SERVEUR MASTER REQUIS** dès PR 3 (nouveau handler/opcodes). **Lock-step** : déployer master neuf AVANT/AVEC le client neuf. Tant que PR 6 n'est pas livré, le client garde son horloge locale (fallback) — pas de casse. Tant que le master neuf n'est pas déployé, le client neuf retombe sur le fallback local (pas de réponse 201).

## Plan de test

- **CI (ctest, sans GPU)** : `world_clock_tests` (formule), `world_clock_payloads_tests` (round-trip), tests lunaires adaptés, test RBAC AdminCommand si infra dispo.
- **En jeu (manuel)** : 2 clients voient la même heure ; `/settime 22:00` (admin) → nuit chez tous ; non-admin → refusé ; `/pausetime on` → fige ; `/settimescale 5` → cycle rapide ; dérive corrigée en douceur ; lune cohérente avec le soleil.
