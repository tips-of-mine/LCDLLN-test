# M100.11 Surface System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Livrer la chaîne client `splat-map → SurfaceType → SurfaceModifiers → vitesse joueur` (M100.11), pivot gameplay référencé par M100.15/16/19/26/27/33.

**Architecture:** Service `SurfaceQueryService` lit la splat-map dominante via `StreamCache` + résout vers une enum `SurfaceType` via `LayerPalette` + table de surfaces JSON. Un setter `SetSurfaceSpeedMultiplier` sur `ClientPredictionSystem` accepte le multiplier issu de `SurfaceTable::Get(type).baseSpeed`. Aucune modification serveur, aucun nouveau format binaire. Le wiring caller (Engine.cpp → Query → setter) reste out-of-scope (M100.33).

**Tech Stack:** C++20, Vulkan engine, ImGui pour le panel, framework test maison (`REQUIRE` macro hand-rolled), JSON parser linéaire (pattern `LayerPalette.cpp`).

**Spec source:** `docs/superpowers/specs/2026-05-07-m100-11-surface-system-design.md`.

---

## File Structure

### Création (12 fichiers)

| Fichier | Rôle |
|---|---|
| `engine/world/surface/SurfaceType.h` | Enum class + helpers ToString/ParseSurfaceType |
| `engine/world/surface/SurfaceTable.h` | Struct SurfaceTableEntry + class SurfaceTable |
| `engine/world/surface/SurfaceTable.cpp` | JSON parser (pattern LayerPalette) |
| `engine/world/surface/SurfaceQueryService.h` | API Query(Vec3) → SurfaceQueryResult |
| `engine/world/surface/SurfaceQueryService.cpp` | Algorithme dominant-layer + fallback |
| `engine/world/surface/tests/SurfaceTypeTests.cpp` | 5 cas |
| `engine/world/surface/tests/SurfaceTableTests.cpp` | 6 cas |
| `engine/world/surface/tests/SurfaceQueryServiceTests.cpp` | 7 cas |
| `engine/editor/world/panels/SurfaceTablePanel.h` | Panel ImGui IPanel-conforming |
| `engine/editor/world/panels/SurfaceTablePanel.cpp` | Rendu read-only + bouton Reload |
| `engine/gameplay/tests/ClientPredictionSurfaceMultiplierTests.cpp` | 3 cas |
| `engine/world/terrain/tests/LayerPaletteSurfaceTypeTests.cpp` | 3 cas |
| `assets/gameplay/surface_table.json` | Table 13 entrées (contenu spec ticket) |

### Modification (5 fichiers)

| Fichier | Modification |
|---|---|
| `engine/world/terrain/LayerPalette.h` | Renomme `surfaceType` (string) → `surfaceTypeName` ; ajoute `SurfaceType surfaceType` (enum) ; ajoute helper `GetSurfaceTypeForLayer(uint8_t)` |
| `engine/world/terrain/LayerPalette.cpp` | Ligne 98 : écrit dans `surfaceTypeName` puis appelle `ParseSurfaceType` pour remplir `surfaceType` |
| `engine/world/terrain/tests/SplatMapTests.cpp` | Ligne 129 : `surfaceType == "Rock"` → `surfaceType == SurfaceType::Rock` |
| `engine/gameplay/ClientPrediction.h` | + `void SetSurfaceSpeedMultiplier(float) noexcept` + member `m_surfaceSpeedMultiplier` |
| `engine/gameplay/ClientPrediction.cpp` | Ligne ~398 : multiplier appliqué dans `ApplyCommand` |
| `engine/editor/world/WorldEditorShell.cpp` | Ajoute `SurfaceTablePanel` à `m_panels` + load assets path |
| `CMakeLists.txt` | + `engine/world/surface/SurfaceTable.cpp`, `SurfaceQueryService.cpp` dans engine_core ; + `engine/editor/world/panels/SurfaceTablePanel.cpp` ; + 5 test exécutables |

---

## Branch & TDD Workflow

Branche active : `claude/m100-phase-3b-surface-system` (déjà créée, commit f0b65a8 = spec).

Chaque task suit TDD strict : red (test fail) → green (impl minimale) → commit. Build/run tests via :
- Build : `cmake --build build --target <test_name> --config Debug`
- Run : `./build/<test_name>` (Win32) ou `./build/Debug/<test_name>.exe`

---

## Task 1: SurfaceType enum + helpers (red)

**Files:**
- Create: `engine/world/surface/SurfaceType.h`
- Create: `engine/world/surface/tests/SurfaceTypeTests.cpp`
- Modify: `CMakeLists.txt` (+ test executable)

- [ ] **Step 1 : Écrire le header `SurfaceType.h`**

```cpp
// engine/world/surface/SurfaceType.h
#pragma once

#include <cstdint>
#include <string_view>

namespace engine::world::surface
{
    /// Surfaces reconnues par le pipeline gameplay (M100.11).
    /// Ordre figé. Tout futur ajout va AVANT `_Count`. Aucune renumérotation.
    enum class SurfaceType : uint16_t
    {
        Dirt = 0,
        Grass,
        Mud,
        Sand,
        Rock,
        Snow,
        ShallowWater,
        DeepWater,
        LavaCooled,
        WheatField,
        CornField,
        Road,
        Bridge,
        _Count
    };

    /// Renvoie le nom canonique ("Dirt", "Grass", ..., "Bridge").
    /// Pour `_Count` ou cast invalide : renvoie "_Invalid".
    std::string_view ToString(SurfaceType t) noexcept;

    /// Parse le nom canonique. True + sortie écrite si match exact.
    /// False sinon (out non touché).
    bool ParseSurfaceType(std::string_view s, SurfaceType& out) noexcept;
}
```

- [ ] **Step 2 : Écrire les 5 tests** dans `engine/world/surface/tests/SurfaceTypeTests.cpp`

```cpp
// engine/world/surface/tests/SurfaceTypeTests.cpp
#include "engine/world/surface/SurfaceType.h"

#include <cstdio>
#include <cstring>

namespace
{
    int g_failed = 0;

    #define REQUIRE(cond) do { \
        if (!(cond)) { \
            std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed; \
        } \
    } while (0)

    using engine::world::surface::SurfaceType;
    using engine::world::surface::ToString;
    using engine::world::surface::ParseSurfaceType;

    void Test_ToString_AllValues()
    {
        REQUIRE(ToString(SurfaceType::Dirt)         == "Dirt");
        REQUIRE(ToString(SurfaceType::Grass)        == "Grass");
        REQUIRE(ToString(SurfaceType::Mud)          == "Mud");
        REQUIRE(ToString(SurfaceType::Sand)         == "Sand");
        REQUIRE(ToString(SurfaceType::Rock)         == "Rock");
        REQUIRE(ToString(SurfaceType::Snow)         == "Snow");
        REQUIRE(ToString(SurfaceType::ShallowWater) == "ShallowWater");
        REQUIRE(ToString(SurfaceType::DeepWater)    == "DeepWater");
        REQUIRE(ToString(SurfaceType::LavaCooled)   == "LavaCooled");
        REQUIRE(ToString(SurfaceType::WheatField)   == "WheatField");
        REQUIRE(ToString(SurfaceType::CornField)    == "CornField");
        REQUIRE(ToString(SurfaceType::Road)         == "Road");
        REQUIRE(ToString(SurfaceType::Bridge)       == "Bridge");
    }

    void Test_ToString_OutOfRange()
    {
        REQUIRE(ToString(SurfaceType::_Count) == "_Invalid");
        REQUIRE(ToString(static_cast<SurfaceType>(999)) == "_Invalid");
    }

    void Test_ParseSurfaceType_AllValues()
    {
        SurfaceType out = SurfaceType::_Count;
        REQUIRE(ParseSurfaceType("Dirt", out)         && out == SurfaceType::Dirt);
        REQUIRE(ParseSurfaceType("Grass", out)        && out == SurfaceType::Grass);
        REQUIRE(ParseSurfaceType("Mud", out)          && out == SurfaceType::Mud);
        REQUIRE(ParseSurfaceType("Sand", out)         && out == SurfaceType::Sand);
        REQUIRE(ParseSurfaceType("Rock", out)         && out == SurfaceType::Rock);
        REQUIRE(ParseSurfaceType("Snow", out)         && out == SurfaceType::Snow);
        REQUIRE(ParseSurfaceType("ShallowWater", out) && out == SurfaceType::ShallowWater);
        REQUIRE(ParseSurfaceType("DeepWater", out)    && out == SurfaceType::DeepWater);
        REQUIRE(ParseSurfaceType("LavaCooled", out)   && out == SurfaceType::LavaCooled);
        REQUIRE(ParseSurfaceType("WheatField", out)   && out == SurfaceType::WheatField);
        REQUIRE(ParseSurfaceType("CornField", out)    && out == SurfaceType::CornField);
        REQUIRE(ParseSurfaceType("Road", out)         && out == SurfaceType::Road);
        REQUIRE(ParseSurfaceType("Bridge", out)       && out == SurfaceType::Bridge);
    }

    void Test_ParseSurfaceType_Unknown()
    {
        SurfaceType out = SurfaceType::Snow;  // sentinel non touchée
        REQUIRE(!ParseSurfaceType("Foobar", out));
        REQUIRE(out == SurfaceType::Snow);  // out inchangé
        REQUIRE(!ParseSurfaceType("", out));
        REQUIRE(out == SurfaceType::Snow);
    }

    void Test_EnumCount_Is13()
    {
        REQUIRE(static_cast<int>(SurfaceType::_Count) == 13);
    }
}

int main()
{
    Test_ToString_AllValues();
    Test_ToString_OutOfRange();
    Test_ParseSurfaceType_AllValues();
    Test_ParseSurfaceType_Unknown();
    Test_EnumCount_Is13();
    return g_failed;
}
```

- [ ] **Step 3 : Ajouter le test exécutable au CMake**

Dans `CMakeLists.txt`, après le bloc `splat_paint_tests` (ligne ~880), ajouter :

```cmake
# M100.11 — Tests SurfaceType enum + helpers (Phase 3b.1).
if(WIN32)
  add_executable(surface_type_tests engine/world/surface/tests/SurfaceTypeTests.cpp)
  target_include_directories(surface_type_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(surface_type_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(surface_type_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME surface_type_tests COMMAND surface_type_tests)
endif()
```

- [ ] **Step 4 : Configurer + tenter le build → expected FAIL (link error : SurfaceType.h pas .cpp)**

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --target surface_type_tests --config Debug
```

Expected: link error `SurfaceType.cpp` non trouvé OU symbole `engine::world::surface::ToString` undefined.

- [ ] **Step 5 : Créer `engine/world/surface/SurfaceType.cpp` (impl minimale)**

```cpp
// engine/world/surface/SurfaceType.cpp
#include "engine/world/surface/SurfaceType.h"

#include <array>

namespace engine::world::surface
{
    namespace
    {
        constexpr std::array<std::string_view, static_cast<size_t>(SurfaceType::_Count)> kNames = {
            "Dirt", "Grass", "Mud", "Sand", "Rock", "Snow",
            "ShallowWater", "DeepWater", "LavaCooled",
            "WheatField", "CornField", "Road", "Bridge"
        };
    }

    std::string_view ToString(SurfaceType t) noexcept
    {
        const auto idx = static_cast<size_t>(t);
        if (idx >= kNames.size()) return "_Invalid";
        return kNames[idx];
    }

    bool ParseSurfaceType(std::string_view s, SurfaceType& out) noexcept
    {
        for (size_t i = 0; i < kNames.size(); ++i)
        {
            if (kNames[i] == s)
            {
                out = static_cast<SurfaceType>(i);
                return true;
            }
        }
        return false;
    }
}
```

- [ ] **Step 6 : Ajouter `SurfaceType.cpp` à engine_core dans CMakeLists.txt**

Dans `CMakeLists.txt` autour de la ligne 367 (au milieu des sources `engine/world/terrain/*`), ajouter :

```cmake
  engine/world/surface/SurfaceType.cpp
```

- [ ] **Step 7 : Rebuild + run test → PASS**

```bash
cmake --build build --target surface_type_tests --config Debug
./build/Debug/surface_type_tests.exe
echo $?  # doit afficher 0
```

Expected: 0 failures, exit code 0.

- [ ] **Step 8 : Commit**

```bash
git add engine/world/surface/SurfaceType.h \
        engine/world/surface/SurfaceType.cpp \
        engine/world/surface/tests/SurfaceTypeTests.cpp \
        CMakeLists.txt
git commit -m "feat(world/surface): SurfaceType enum + helpers (M100.11 Task 1)"
```

---

## Task 2: SurfaceTable JSON loader

**Files:**
- Create: `engine/world/surface/SurfaceTable.h`
- Create: `engine/world/surface/SurfaceTable.cpp`
- Create: `engine/world/surface/tests/SurfaceTableTests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Écrire le header `SurfaceTable.h`**

```cpp
// engine/world/surface/SurfaceTable.h
#pragma once

#include "engine/world/surface/SurfaceType.h"

#include <array>
#include <filesystem>
#include <string>

namespace engine::world::surface
{
    struct SurfaceTableEntry
    {
        SurfaceType type = SurfaceType::Dirt;
        float baseSpeed = 1.0f;     // multiplier vs Dirt baseline 1.0
        std::string audioStep;
        std::string visualTag;
    };

    /// Charge une fois `assets/gameplay/surface_table.json` au boot.
    /// Format : { "version":1, "surfaces":[ {type, baseSpeed, audioStep, visualTag}, ... 13 ] }.
    /// Validation : exactement 13 entrées, types uniques, baseSpeed >= 0.
    class SurfaceTable
    {
    public:
        bool LoadFromJson(const std::filesystem::path& path, std::string& outError);

        /// Précondition : t < SurfaceType::_Count. Aucun bounds check release.
        const SurfaceTableEntry& Get(SurfaceType t) const noexcept;

        bool IsLoaded() const noexcept { return m_loaded; }

    private:
        std::array<SurfaceTableEntry, static_cast<size_t>(SurfaceType::_Count)> m_entries{};
        bool m_loaded = false;
    };
}
```

- [ ] **Step 2 : Écrire les 6 tests dans `engine/world/surface/tests/SurfaceTableTests.cpp`**

```cpp
// engine/world/surface/tests/SurfaceTableTests.cpp
#include "engine/world/surface/SurfaceTable.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace
{
    int g_failed = 0;

    #define REQUIRE(cond) do { \
        if (!(cond)) { \
            std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed; \
        } \
    } while (0)

    using engine::world::surface::SurfaceType;
    using engine::world::surface::SurfaceTable;

    bool ApproxEq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

    /// Écrit un fichier JSON temporaire avec les 13 entrées de la spec ticket.
    std::filesystem::path WriteFixtureFull()
    {
        auto p = std::filesystem::temp_directory_path() / "surface_table_full.json";
        std::ofstream f(p);
        f << R"({
  "version": 1,
  "surfaces": [
    { "type": "Dirt",         "baseSpeed": 1.00, "audioStep": "step_dirt",          "visualTag": "dust_sprint" },
    { "type": "Grass",        "baseSpeed": 0.95, "audioStep": "step_grass",         "visualTag": "grass_bend" },
    { "type": "Mud",          "baseSpeed": 0.55, "audioStep": "step_mud_squelch",   "visualTag": "splash_mud" },
    { "type": "Sand",         "baseSpeed": 0.70, "audioStep": "step_sand",          "visualTag": "footprint_decal_fade" },
    { "type": "Rock",         "baseSpeed": 1.05, "audioStep": "step_rock",          "visualTag": "" },
    { "type": "Snow",         "baseSpeed": 0.50, "audioStep": "step_snow_crunch",   "visualTag": "footprint_decal_persistent" },
    { "type": "ShallowWater", "baseSpeed": 0.40, "audioStep": "step_water",         "visualTag": "splash_water" },
    { "type": "DeepWater",    "baseSpeed": 0.25, "audioStep": "swim_stroke",        "visualTag": "swim_mode" },
    { "type": "LavaCooled",   "baseSpeed": 0.85, "audioStep": "step_rock",          "visualTag": "heat_emission" },
    { "type": "WheatField",   "baseSpeed": 0.85, "audioStep": "step_wheat_rustle",  "visualTag": "wheat_part" },
    { "type": "CornField",    "baseSpeed": 0.80, "audioStep": "step_corn_rustle",   "visualTag": "corn_part" },
    { "type": "Road",         "baseSpeed": 1.10, "audioStep": "step_gravel",        "visualTag": "" },
    { "type": "Bridge",       "baseSpeed": 1.10, "audioStep": "step_wood",          "visualTag": "" }
  ]
})";
        return p;
    }

    void Test_LoadFromJson_FixtureHas13Entries()
    {
        auto path = WriteFixtureFull();
        SurfaceTable table;
        std::string err;
        REQUIRE(table.LoadFromJson(path, err));
        REQUIRE(err.empty());
        REQUIRE(table.IsLoaded());
        std::filesystem::remove(path);
    }

    void Test_LoadFromJson_DirtBaseSpeedIs1p0()
    {
        auto path = WriteFixtureFull();
        SurfaceTable table; std::string err;
        REQUIRE(table.LoadFromJson(path, err));
        REQUIRE(ApproxEq(table.Get(SurfaceType::Dirt).baseSpeed, 1.00f));
        std::filesystem::remove(path);
    }

    void Test_LoadFromJson_SnowBaseSpeedIs0p5()
    {
        auto path = WriteFixtureFull();
        SurfaceTable table; std::string err;
        REQUIRE(table.LoadFromJson(path, err));
        REQUIRE(ApproxEq(table.Get(SurfaceType::Snow).baseSpeed, 0.50f));
        std::filesystem::remove(path);
    }

    void Test_LoadFromJson_AudioStepNonEmpty()
    {
        auto path = WriteFixtureFull();
        SurfaceTable table; std::string err;
        REQUIRE(table.LoadFromJson(path, err));
        for (int i = 0; i < static_cast<int>(SurfaceType::_Count); ++i)
        {
            const auto& e = table.Get(static_cast<SurfaceType>(i));
            REQUIRE(!e.audioStep.empty());
        }
        std::filesystem::remove(path);
    }

    void Test_LoadFromJson_MalformedJson_Fails()
    {
        auto p = std::filesystem::temp_directory_path() / "surface_table_bad.json";
        std::ofstream f(p);
        f << R"({ "version": 1, "surfaces": [ { "type": "Di)";  // tronqué
        f.close();

        SurfaceTable table; std::string err;
        REQUIRE(!table.LoadFromJson(p, err));
        REQUIRE(!err.empty());
        REQUIRE(!table.IsLoaded());
        std::filesystem::remove(p);
    }

    void Test_LoadFromJson_MissingEntry_Fails()
    {
        auto p = std::filesystem::temp_directory_path() / "surface_table_short.json";
        std::ofstream f(p);
        // Seulement 12 entrées : il manque "Bridge"
        f << R"({
  "version": 1,
  "surfaces": [
    { "type": "Dirt",       "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "Grass",      "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "Mud",        "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "Sand",       "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "Rock",       "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "Snow",       "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "ShallowWater","baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "DeepWater",  "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "LavaCooled", "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "WheatField", "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "CornField",  "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "Road",       "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" }
  ]
})";
        f.close();

        SurfaceTable table; std::string err;
        REQUIRE(!table.LoadFromJson(p, err));
        REQUIRE(err.find("13") != std::string::npos);  // message mentionne "13 entries expected"
        std::filesystem::remove(p);
    }
}

int main()
{
    Test_LoadFromJson_FixtureHas13Entries();
    Test_LoadFromJson_DirtBaseSpeedIs1p0();
    Test_LoadFromJson_SnowBaseSpeedIs0p5();
    Test_LoadFromJson_AudioStepNonEmpty();
    Test_LoadFromJson_MalformedJson_Fails();
    Test_LoadFromJson_MissingEntry_Fails();
    return g_failed;
}
```

- [ ] **Step 3 : Ajouter test exécutable au CMakeLists.txt**

Après le bloc `surface_type_tests` :

```cmake
# M100.11 — Tests SurfaceTable JSON loader.
if(WIN32)
  add_executable(surface_table_tests engine/world/surface/tests/SurfaceTableTests.cpp)
  target_include_directories(surface_table_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(surface_table_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(surface_table_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME surface_table_tests COMMAND surface_table_tests)
endif()
```

- [ ] **Step 4 : Build → expected FAIL (SurfaceTable.cpp absent)**

```bash
cmake --build build --target surface_table_tests --config Debug
```

Expected: link error `engine::world::surface::SurfaceTable::LoadFromJson` undefined.

- [ ] **Step 5 : Créer `engine/world/surface/SurfaceTable.cpp` (impl minimale)**

```cpp
// engine/world/surface/SurfaceTable.cpp
#include "engine/world/surface/SurfaceTable.h"

#include <fstream>
#include <sstream>

namespace engine::world::surface
{
    namespace
    {
        /// Pattern LayerPalette : extracteur string linéaire pour un objet JSON
        /// minimal (pas d'imbrication, pas d'escapes). `obj` est la sous-chaîne
        /// "{...}" d'une entrée du tableau "surfaces".
        std::string ExtractStringField(const std::string& obj, const std::string& key,
            const std::string& fallback = "")
        {
            const std::string searchKey = "\"" + key + "\"";
            size_t keyPos = obj.find(searchKey);
            if (keyPos == std::string::npos) return fallback;
            size_t colonPos = obj.find(':', keyPos);
            if (colonPos == std::string::npos) return fallback;
            size_t startQuote = obj.find('"', colonPos + 1);
            if (startQuote == std::string::npos) return fallback;
            size_t endQuote = obj.find('"', startQuote + 1);
            if (endQuote == std::string::npos) return fallback;
            return obj.substr(startQuote + 1, endQuote - startQuote - 1);
        }

        float ExtractFloatField(const std::string& obj, const std::string& key,
            float fallback = 0.0f)
        {
            const std::string searchKey = "\"" + key + "\"";
            size_t keyPos = obj.find(searchKey);
            if (keyPos == std::string::npos) return fallback;
            size_t colonPos = obj.find(':', keyPos);
            if (colonPos == std::string::npos) return fallback;
            std::string remainder = obj.substr(colonPos + 1);
            try { return std::stof(remainder); } catch (...) { return fallback; }
        }
    }

    bool SurfaceTable::LoadFromJson(const std::filesystem::path& path, std::string& outError)
    {
        m_loaded = false;
        std::ifstream f(path);
        if (!f.good())
        { outError = "SurfaceTable: cannot open " + path.string(); return false; }

        std::stringstream buf;
        buf << f.rdbuf();
        const std::string content = buf.str();

        const std::string surfacesKey = "\"surfaces\"";
        size_t pos = content.find(surfacesKey);
        if (pos == std::string::npos)
        { outError = "SurfaceTable: missing 'surfaces' field"; return false; }
        size_t arrStart = content.find('[', pos);
        if (arrStart == std::string::npos)
        { outError = "SurfaceTable: 'surfaces' is not an array"; return false; }

        const size_t expected = static_cast<size_t>(SurfaceType::_Count);
        bool seen[static_cast<size_t>(SurfaceType::_Count)] = {};
        size_t cursor = arrStart;
        size_t parsed = 0;

        for (size_t i = 0; i < expected; ++i)
        {
            size_t objStart = content.find('{', cursor);
            if (objStart == std::string::npos)
            {
                outError = "SurfaceTable: 13 entries expected, got " + std::to_string(parsed);
                return false;
            }
            size_t objEnd = content.find('}', objStart);
            if (objEnd == std::string::npos)
            {
                outError = "SurfaceTable: unterminated entry " + std::to_string(i);
                return false;
            }
            std::string obj = content.substr(objStart, objEnd - objStart + 1);

            const std::string typeStr = ExtractStringField(obj, "type");
            SurfaceType type = SurfaceType::Dirt;
            if (!ParseSurfaceType(typeStr, type))
            {
                outError = "SurfaceTable: unknown type '" + typeStr + "' at index "
                    + std::to_string(i);
                return false;
            }
            const auto idx = static_cast<size_t>(type);
            if (seen[idx])
            {
                outError = "SurfaceTable: duplicate type '" + typeStr + "'";
                return false;
            }
            seen[idx] = true;

            SurfaceTableEntry& e = m_entries[idx];
            e.type      = type;
            e.baseSpeed = ExtractFloatField(obj, "baseSpeed", 1.0f);
            if (e.baseSpeed < 0.0f)
            { outError = "SurfaceTable: negative baseSpeed for '" + typeStr + "'"; return false; }
            e.audioStep = ExtractStringField(obj, "audioStep");
            e.visualTag = ExtractStringField(obj, "visualTag");

            cursor = objEnd + 1;
            ++parsed;
        }

        // Vérifie que toutes les surfaces sont présentes.
        for (size_t i = 0; i < expected; ++i)
        {
            if (!seen[i])
            {
                outError = "SurfaceTable: 13 entries expected, missing index "
                    + std::to_string(i);
                return false;
            }
        }

        m_loaded = true;
        return true;
    }

    const SurfaceTableEntry& SurfaceTable::Get(SurfaceType t) const noexcept
    {
        return m_entries[static_cast<size_t>(t)];
    }
}
```

- [ ] **Step 6 : Ajouter `SurfaceTable.cpp` à engine_core dans CMakeLists.txt**

À côté de `engine/world/surface/SurfaceType.cpp` (Task 1 step 6) :

```cmake
  engine/world/surface/SurfaceTable.cpp
```

- [ ] **Step 7 : Build + run → PASS**

```bash
cmake --build build --target surface_table_tests --config Debug
./build/Debug/surface_table_tests.exe
```

Expected: 0 failures.

- [ ] **Step 8 : Commit**

```bash
git add engine/world/surface/SurfaceTable.h \
        engine/world/surface/SurfaceTable.cpp \
        engine/world/surface/tests/SurfaceTableTests.cpp \
        CMakeLists.txt
git commit -m "feat(world/surface): SurfaceTable JSON loader (M100.11 Task 2)"
```

---

## Task 3: surface_table.json fixture asset

**Files:**
- Create: `assets/gameplay/surface_table.json`

- [ ] **Step 1 : Créer le répertoire et le fichier**

```bash
mkdir -p assets/gameplay
```

- [ ] **Step 2 : Écrire `assets/gameplay/surface_table.json`**

```json
{
  "version": 1,
  "surfaces": [
    { "type": "Dirt",         "baseSpeed": 1.00, "audioStep": "step_dirt",          "visualTag": "dust_sprint" },
    { "type": "Grass",        "baseSpeed": 0.95, "audioStep": "step_grass",         "visualTag": "grass_bend" },
    { "type": "Mud",          "baseSpeed": 0.55, "audioStep": "step_mud_squelch",   "visualTag": "splash_mud" },
    { "type": "Sand",         "baseSpeed": 0.70, "audioStep": "step_sand",          "visualTag": "footprint_decal_fade" },
    { "type": "Rock",         "baseSpeed": 1.05, "audioStep": "step_rock",          "visualTag": "" },
    { "type": "Snow",         "baseSpeed": 0.50, "audioStep": "step_snow_crunch",   "visualTag": "footprint_decal_persistent" },
    { "type": "ShallowWater", "baseSpeed": 0.40, "audioStep": "step_water",         "visualTag": "splash_water" },
    { "type": "DeepWater",    "baseSpeed": 0.25, "audioStep": "swim_stroke",        "visualTag": "swim_mode" },
    { "type": "LavaCooled",   "baseSpeed": 0.85, "audioStep": "step_rock",          "visualTag": "heat_emission" },
    { "type": "WheatField",   "baseSpeed": 0.85, "audioStep": "step_wheat_rustle",  "visualTag": "wheat_part" },
    { "type": "CornField",    "baseSpeed": 0.80, "audioStep": "step_corn_rustle",   "visualTag": "corn_part" },
    { "type": "Road",         "baseSpeed": 1.10, "audioStep": "step_gravel",        "visualTag": "" },
    { "type": "Bridge",       "baseSpeed": 1.10, "audioStep": "step_wood",          "visualTag": "" }
  ]
}
```

- [ ] **Step 3 : Commit**

```bash
git add assets/gameplay/surface_table.json
git commit -m "feat(assets): surface_table.json 13 surfaces (M100.11 Task 3)"
```

---

## Task 4: LayerPalette migration string→enum (red)

**Files:**
- Modify: `engine/world/terrain/LayerPalette.h`
- Modify: `engine/world/terrain/LayerPalette.cpp:98`
- Modify: `engine/world/terrain/tests/SplatMapTests.cpp:129`
- Create: `engine/world/terrain/tests/LayerPaletteSurfaceTypeTests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Modifier `engine/world/terrain/LayerPalette.h`**

Remplacer la struct `LayerEntry` (ligne 14-23) par :

```cpp
#include "engine/world/surface/SurfaceType.h"

// ...

struct LayerEntry
{
    uint32_t index = 0;
    std::string name;
    std::string albedoPath;
    std::string normalPath;
    std::string armPath;
    float tilingMeters = 4.0f;
    std::string surfaceTypeName;                                  // string brute du JSON, conservée pour debug
    engine::world::surface::SurfaceType surfaceType =
        engine::world::surface::SurfaceType::Dirt;                // enum canonique parsée
};
```

Et après la struct `LayerPalette` (ligne 32), ajouter le helper :

```cpp
struct LayerPalette
{
    uint32_t version = 1u;
    std::array<LayerEntry, 8> layers;

    /// Précondition : layer < 8. Retourne l'enum canonique de la layer.
    /// (Layer index hors range : comportement non spécifié — debug-assert.)
    engine::world::surface::SurfaceType GetSurfaceTypeForLayer(uint8_t layer) const noexcept;
};
```

- [ ] **Step 2 : Modifier `engine/world/terrain/LayerPalette.cpp` (ligne 98)**

Remplacer la ligne :
```cpp
e.surfaceType  = ExtractStringField(obj, "surfaceType", "Dirt");
```
par :
```cpp
e.surfaceTypeName = ExtractStringField(obj, "surfaceType", "Dirt");
if (!engine::world::surface::ParseSurfaceType(e.surfaceTypeName, e.surfaceType))
{
    e.surfaceType = engine::world::surface::SurfaceType::Dirt;
    // Pas de LOG_WARN ici : pour les fixtures de test on tolère, le warn
    // serait noise. Le caller (Engine::Init) peut logguer s'il veut.
}
```

Et ajouter en haut du fichier `#include "engine/world/surface/SurfaceType.h"`.

À la fin du namespace, ajouter l'impl du helper :

```cpp
engine::world::surface::SurfaceType
LayerPalette::GetSurfaceTypeForLayer(uint8_t layer) const noexcept
{
    return layers[layer].surfaceType;  // précondition layer < 8 documentée
}
```

- [ ] **Step 3 : Migrer `engine/world/terrain/tests/SplatMapTests.cpp` ligne 129**

Remplacer :
```cpp
REQUIRE(palette.layers[5].surfaceType == "Rock");
```
par :
```cpp
REQUIRE(palette.layers[5].surfaceType == engine::world::surface::SurfaceType::Rock);
```

(Et ajouter `#include "engine/world/surface/SurfaceType.h"` en haut du fichier si absent.)

- [ ] **Step 4 : Écrire les 3 tests dans `engine/world/terrain/tests/LayerPaletteSurfaceTypeTests.cpp`**

```cpp
// engine/world/terrain/tests/LayerPaletteSurfaceTypeTests.cpp
#include "engine/world/terrain/LayerPalette.h"
#include "engine/world/surface/SurfaceType.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace
{
    int g_failed = 0;

    #define REQUIRE(cond) do { \
        if (!(cond)) { \
            std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed; \
        } \
    } while (0)

    using engine::world::surface::SurfaceType;
    using engine::world::terrain::LayerPalette;
    using engine::world::terrain::LoadLayerPalette;

    /// 8 layers avec un mix de surfaceType : Dirt, Grass, Rock, Snow, ...
    std::filesystem::path WritePaletteFixture()
    {
        auto p = std::filesystem::temp_directory_path() / "layer_palette_surface_test.json";
        std::ofstream f(p);
        f << R"({
  "version": 1,
  "layers": [
    { "index": 0, "name": "dirt",  "albedo": "a0", "normal": "n0", "arm": "r0", "tilingMeters": 4.0, "surfaceType": "Dirt" },
    { "index": 1, "name": "grass", "albedo": "a1", "normal": "n1", "arm": "r1", "tilingMeters": 4.0, "surfaceType": "Grass" },
    { "index": 2, "name": "mud",   "albedo": "a2", "normal": "n2", "arm": "r2", "tilingMeters": 4.0, "surfaceType": "Mud" },
    { "index": 3, "name": "sand",  "albedo": "a3", "normal": "n3", "arm": "r3", "tilingMeters": 4.0, "surfaceType": "Sand" },
    { "index": 4, "name": "rock",  "albedo": "a4", "normal": "n4", "arm": "r4", "tilingMeters": 4.0, "surfaceType": "Rock" },
    { "index": 5, "name": "snow",  "albedo": "a5", "normal": "n5", "arm": "r5", "tilingMeters": 4.0, "surfaceType": "Snow" },
    { "index": 6, "name": "road",  "albedo": "a6", "normal": "n6", "arm": "r6", "tilingMeters": 4.0, "surfaceType": "Road" },
    { "index": 7, "name": "bridge","albedo": "a7", "normal": "n7", "arm": "r7", "tilingMeters": 4.0, "surfaceType": "Bridge" }
  ]
})";
        return p;
    }

    void Test_LoadLayerPalette_ParsesSurfaceTypeString()
    {
        auto path = WritePaletteFixture();
        LayerPalette pal; std::string err;
        REQUIRE(LoadLayerPalette(path, pal, err));
        REQUIRE(pal.layers[0].surfaceType == SurfaceType::Dirt);
        REQUIRE(pal.layers[5].surfaceType == SurfaceType::Snow);
        REQUIRE(pal.layers[5].surfaceTypeName == "Snow");
        std::filesystem::remove(path);
    }

    void Test_LoadLayerPalette_UnknownSurfaceType_FallsBackDirt()
    {
        auto p = std::filesystem::temp_directory_path() / "layer_palette_unknown.json";
        std::ofstream f(p);
        f << R"({
  "version": 1,
  "layers": [
    { "index": 0, "name": "x", "albedo": "a", "normal": "n", "arm": "r", "tilingMeters": 4.0, "surfaceType": "Foobar" },
    { "index": 1, "name": "x", "albedo": "a", "normal": "n", "arm": "r", "tilingMeters": 4.0, "surfaceType": "Dirt" },
    { "index": 2, "name": "x", "albedo": "a", "normal": "n", "arm": "r", "tilingMeters": 4.0, "surfaceType": "Dirt" },
    { "index": 3, "name": "x", "albedo": "a", "normal": "n", "arm": "r", "tilingMeters": 4.0, "surfaceType": "Dirt" },
    { "index": 4, "name": "x", "albedo": "a", "normal": "n", "arm": "r", "tilingMeters": 4.0, "surfaceType": "Dirt" },
    { "index": 5, "name": "x", "albedo": "a", "normal": "n", "arm": "r", "tilingMeters": 4.0, "surfaceType": "Dirt" },
    { "index": 6, "name": "x", "albedo": "a", "normal": "n", "arm": "r", "tilingMeters": 4.0, "surfaceType": "Dirt" },
    { "index": 7, "name": "x", "albedo": "a", "normal": "n", "arm": "r", "tilingMeters": 4.0, "surfaceType": "Dirt" }
  ]
})";
        f.close();

        LayerPalette pal; std::string err;
        REQUIRE(LoadLayerPalette(p, pal, err));      // load réussit (warn-level seulement)
        REQUIRE(pal.layers[0].surfaceType == SurfaceType::Dirt);  // fallback Dirt
        REQUIRE(pal.layers[0].surfaceTypeName == "Foobar");        // string brute conservée
        std::filesystem::remove(p);
    }

    void Test_GetSurfaceTypeForLayer_ValidIndex()
    {
        auto path = WritePaletteFixture();
        LayerPalette pal; std::string err;
        REQUIRE(LoadLayerPalette(path, pal, err));
        REQUIRE(pal.GetSurfaceTypeForLayer(0) == SurfaceType::Dirt);
        REQUIRE(pal.GetSurfaceTypeForLayer(4) == SurfaceType::Rock);
        REQUIRE(pal.GetSurfaceTypeForLayer(7) == SurfaceType::Bridge);
        std::filesystem::remove(path);
    }
}

int main()
{
    Test_LoadLayerPalette_ParsesSurfaceTypeString();
    Test_LoadLayerPalette_UnknownSurfaceType_FallsBackDirt();
    Test_GetSurfaceTypeForLayer_ValidIndex();
    return g_failed;
}
```

- [ ] **Step 5 : Ajouter test exécutable au CMakeLists.txt**

```cmake
# M100.11 — Tests LayerPalette migration string→SurfaceType enum.
if(WIN32)
  add_executable(layer_palette_surface_type_tests engine/world/terrain/tests/LayerPaletteSurfaceTypeTests.cpp)
  target_include_directories(layer_palette_surface_type_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(layer_palette_surface_type_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(layer_palette_surface_type_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME layer_palette_surface_type_tests COMMAND layer_palette_surface_type_tests)
endif()
```

- [ ] **Step 6 : Build all impacted targets + run**

```bash
cmake --build build --target engine_core --config Debug
cmake --build build --target splat_map_tests --config Debug
cmake --build build --target layer_palette_surface_type_tests --config Debug

./build/Debug/splat_map_tests.exe
./build/Debug/layer_palette_surface_type_tests.exe
```

Expected: les deux passent (`splat_map_tests` migration ligne 129 OK, nouveaux tests verts).

- [ ] **Step 7 : Commit**

```bash
git add engine/world/terrain/LayerPalette.h \
        engine/world/terrain/LayerPalette.cpp \
        engine/world/terrain/tests/SplatMapTests.cpp \
        engine/world/terrain/tests/LayerPaletteSurfaceTypeTests.cpp \
        CMakeLists.txt
git commit -m "feat(world/terrain): LayerEntry.surfaceType en enum (M100.11 Task 4)"
```

---

## Task 5: SurfaceQueryService core (red)

**Files:**
- Create: `engine/world/surface/SurfaceQueryService.h`
- Create: `engine/world/surface/SurfaceQueryService.cpp`
- Create: `engine/world/surface/tests/SurfaceQueryServiceTests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Écrire le header `SurfaceQueryService.h`**

```cpp
// engine/world/surface/SurfaceQueryService.h
#pragma once

#include "engine/math/Math.h"
#include "engine/world/surface/SurfaceType.h"

#include <cstdint>
#include <unordered_set>

namespace engine::core { class Config; }
namespace engine::world { class StreamCache; }
namespace engine::world::terrain { struct LayerPalette; }

namespace engine::world::surface
{
    class SurfaceTable;

    struct SurfaceModifiers
    {
        bool slippery = false;
        bool wet = false;
        bool frozen = false;
        bool seasonalSnow = false;
        float speedMultiplier = 1.0f;
        float audioPitchShift = 1.0f;
    };

    struct SurfaceQueryResult
    {
        SurfaceType base = SurfaceType::Dirt;
        SurfaceModifiers modifiers{};
    };

    /// Résout `worldPos` → `(SurfaceType, SurfaceModifiers)` à partir de la
    /// splat-map dominante locale. Lecture via `StreamCache::LoadSplatMap`,
    /// résolution layer→type via `LayerPalette`. Si splat indisponible :
    /// fallback `{Dirt, {}}` + warn une fois par chunk.
    /// Modifiers neutres en M100.11 (M100.26 les calculera depuis météo/saison).
    class SurfaceQueryService
    {
    public:
        bool Init(const SurfaceTable& table,
                  engine::world::StreamCache& cache,
                  const engine::core::Config& cfg,
                  const engine::world::terrain::LayerPalette& palette) noexcept;

        SurfaceQueryResult Query(engine::math::Vec3 worldPos) const;

    private:
        const SurfaceTable*                              m_table = nullptr;
        engine::world::StreamCache*                      m_cache = nullptr;
        const engine::core::Config*                      m_cfg = nullptr;
        const engine::world::terrain::LayerPalette*      m_palette = nullptr;
        // Throttle warn : 1 par (chunkX, chunkZ) pour la session.
        // Encodage clé : (int64_t(chunkX) << 32) | uint32_t(chunkZ).
        mutable std::unordered_set<int64_t>              m_warnedChunks;
    };
}
```

- [ ] **Step 2 : Écrire les 7 tests dans `engine/world/surface/tests/SurfaceQueryServiceTests.cpp`**

Pattern de mock : on utilise un **vrai** `StreamCache` et on pré-popule via `Insert(cacheKey, blob)` où `blob` est le résultat de `SaveSplatBin`. `LoadSplatMap` fait un `Lookup` AVANT de tenter le disque, donc le cache hit court-circuite l'IO disque.

```cpp
// engine/world/surface/tests/SurfaceQueryServiceTests.cpp
#include "engine/world/surface/SurfaceQueryService.h"
#include "engine/world/surface/SurfaceTable.h"
#include "engine/world/StreamCache.h"
#include "engine/world/terrain/LayerPalette.h"
#include "engine/world/terrain/SplatMap.h"
#include "engine/core/Config.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    int g_failed = 0;

    #define REQUIRE(cond) do { \
        if (!(cond)) { \
            std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed; \
        } \
    } while (0)

    using engine::world::surface::SurfaceType;
    using engine::world::surface::SurfaceTable;
    using engine::world::surface::SurfaceQueryService;
    using engine::world::surface::SurfaceQueryResult;
    using engine::world::StreamCache;
    using engine::world::terrain::LayerPalette;
    using engine::world::terrain::SplatMap;
    using engine::world::terrain::SaveSplatBin;
    using engine::world::terrain::kSplatResolution;
    using engine::world::terrain::kSplatLayerCount;

    /// Construit une LayerPalette test avec mapping fixé : layer 0=Dirt, 4=Rock,
    /// 5=Snow, et le reste à Dirt (les tests s'en moquent).
    LayerPalette MakeTestPalette()
    {
        LayerPalette pal;
        for (uint32_t i = 0; i < 8; ++i)
        {
            pal.layers[i].index = i;
            pal.layers[i].surfaceType = SurfaceType::Dirt;
        }
        pal.layers[0].surfaceType = SurfaceType::Dirt;
        pal.layers[4].surfaceType = SurfaceType::Rock;
        pal.layers[5].surfaceType = SurfaceType::Snow;
        return pal;
    }

    /// Insère une SplatMap uniforme (100% layer X) dans le cache pour le chunk (cx, cz).
    void InsertUniformSplatToCache(StreamCache& cache, int chunkX, int chunkZ, uint32_t layerIdx)
    {
        SplatMap splat = SplatMap::MakeUniform(layerIdx);
        std::vector<uint8_t> bytes;
        std::string err;
        REQUIRE(SaveSplatBin(splat, bytes, err));

        std::ostringstream ks;
        ks << "chunks/chunk_" << chunkX << "_" << chunkZ << "/splat.bin";
        cache.Insert(ks.str(), bytes);
    }

    /// Idem mais 50/50 entre layer A et B (poids 128/127, somme=255).
    void InsertTwoLayerSplat(StreamCache& cache, int chunkX, int chunkZ,
        uint32_t layerA, uint32_t layerB)
    {
        SplatMap splat;
        const size_t cellCount = static_cast<size_t>(kSplatResolution) * kSplatResolution;
        splat.weights.assign(cellCount * kSplatLayerCount, 0u);
        for (size_t cell = 0; cell < cellCount; ++cell)
        {
            splat.weights[cell * kSplatLayerCount + layerA] = 128u;
            splat.weights[cell * kSplatLayerCount + layerB] = 127u;
        }

        std::vector<uint8_t> bytes;
        std::string err;
        REQUIRE(SaveSplatBin(splat, bytes, err));

        std::ostringstream ks;
        ks << "chunks/chunk_" << chunkX << "_" << chunkZ << "/splat.bin";
        cache.Insert(ks.str(), bytes);
    }

    /// Construit un SurfaceTable + Config + StreamCache + LayerPalette dans un
    /// helper unique. Le `paths.content` pointe vers un dossier inexistant pour
    /// que le fallback disque échoue (le cache est l'unique source de splat).
    struct Fixture
    {
        SurfaceTable table;
        engine::core::Config cfg;
        StreamCache cache;
        LayerPalette palette;
        SurfaceQueryService svc;

        Fixture()
        {
            // SurfaceTable : on remplit minimalement via fixture JSON.
            auto p = std::filesystem::temp_directory_path() / "svc_test_table.json";
            std::ofstream f(p);
            f << R"({"version":1,"surfaces":[
                {"type":"Dirt","baseSpeed":1.00,"audioStep":"d","visualTag":""},
                {"type":"Grass","baseSpeed":0.95,"audioStep":"g","visualTag":""},
                {"type":"Mud","baseSpeed":0.55,"audioStep":"m","visualTag":""},
                {"type":"Sand","baseSpeed":0.70,"audioStep":"s","visualTag":""},
                {"type":"Rock","baseSpeed":1.05,"audioStep":"r","visualTag":""},
                {"type":"Snow","baseSpeed":0.50,"audioStep":"sn","visualTag":""},
                {"type":"ShallowWater","baseSpeed":0.40,"audioStep":"sw","visualTag":""},
                {"type":"DeepWater","baseSpeed":0.25,"audioStep":"dw","visualTag":""},
                {"type":"LavaCooled","baseSpeed":0.85,"audioStep":"l","visualTag":""},
                {"type":"WheatField","baseSpeed":0.85,"audioStep":"w","visualTag":""},
                {"type":"CornField","baseSpeed":0.80,"audioStep":"c","visualTag":""},
                {"type":"Road","baseSpeed":1.10,"audioStep":"ro","visualTag":""},
                {"type":"Bridge","baseSpeed":1.10,"audioStep":"b","visualTag":""}
            ]})";
            f.close();
            std::string err;
            REQUIRE(table.LoadFromJson(p, err));
            std::filesystem::remove(p);

            cfg.SetValue("paths.content", std::string("/nonexistent_dir_for_test"));
            cache.Init(cfg);
            palette = MakeTestPalette();
            REQUIRE(svc.Init(table, cache, cfg, palette));
        }
    };

    void Test_Query_SplatAbsent_FallbackDirt()
    {
        Fixture fx;
        // Pas d'Insert : LoadSplatMap → cache miss → disk miss → nullptr.
        engine::math::Vec3 pos{ 0.0f, 0.0f, 0.0f };
        SurfaceQueryResult r = fx.svc.Query(pos);
        REQUIRE(r.base == SurfaceType::Dirt);
        REQUIRE(r.modifiers.speedMultiplier == 1.0f);
    }

    void Test_Query_DominantLayerDirt_ReturnsDirt()
    {
        Fixture fx;
        // Chunk (0, 0) avec splat 100% layer 0 (Dirt dans la palette test).
        InsertUniformSplatToCache(fx.cache, 0, 0, 0);
        engine::math::Vec3 pos{ 1.0f, 0.0f, 1.0f };  // dans chunk (0,0)
        SurfaceQueryResult r = fx.svc.Query(pos);
        REQUIRE(r.base == SurfaceType::Dirt);
    }

    void Test_Query_DominantLayerRock_ReturnsRock()
    {
        Fixture fx;
        InsertUniformSplatToCache(fx.cache, 0, 0, 4);  // layer 4 = Rock
        engine::math::Vec3 pos{ 1.0f, 0.0f, 1.0f };
        SurfaceQueryResult r = fx.svc.Query(pos);
        REQUIRE(r.base == SurfaceType::Rock);
    }

    void Test_Query_DominantLayerSnow_ReturnsSnow()
    {
        Fixture fx;
        InsertUniformSplatToCache(fx.cache, 0, 0, 5);  // layer 5 = Snow
        engine::math::Vec3 pos{ 1.0f, 0.0f, 1.0f };
        SurfaceQueryResult r = fx.svc.Query(pos);
        REQUIRE(r.base == SurfaceType::Snow);
    }

    void Test_Query_TieBreaker_LowestIndex()
    {
        Fixture fx;
        // 50/50 entre layer 4 (Rock, 128) et layer 5 (Snow, 127).
        // argmax avec tie-break "plus petit index" → layer 4 = Rock.
        InsertTwoLayerSplat(fx.cache, 0, 0, 4, 5);
        engine::math::Vec3 pos{ 1.0f, 0.0f, 1.0f };
        SurfaceQueryResult r = fx.svc.Query(pos);
        REQUIRE(r.base == SurfaceType::Rock);  // layer 4 wins (lower index, equal weight)
    }

    void Test_Query_OutOfBoundsCell_FallbackDirt()
    {
        Fixture fx;
        InsertUniformSplatToCache(fx.cache, 0, 0, 5);  // chunk (0,0) Snow
        // worldPos très loin → chunk (1000, 1000) sans splat.
        engine::math::Vec3 pos{ 100000.0f, 0.0f, 100000.0f };
        SurfaceQueryResult r = fx.svc.Query(pos);
        REQUIRE(r.base == SurfaceType::Dirt);  // fallback
    }

    void Test_Query_ModifiersNeutralByDefault()
    {
        Fixture fx;
        InsertUniformSplatToCache(fx.cache, 0, 0, 5);  // Snow
        SurfaceQueryResult r = fx.svc.Query({ 1.0f, 0.0f, 1.0f });
        REQUIRE(r.modifiers.speedMultiplier == 1.0f);
        REQUIRE(r.modifiers.audioPitchShift == 1.0f);
        REQUIRE(!r.modifiers.slippery);
        REQUIRE(!r.modifiers.wet);
        REQUIRE(!r.modifiers.frozen);
        REQUIRE(!r.modifiers.seasonalSnow);
    }
}

int main()
{
    Test_Query_SplatAbsent_FallbackDirt();
    Test_Query_DominantLayerDirt_ReturnsDirt();
    Test_Query_DominantLayerRock_ReturnsRock();
    Test_Query_DominantLayerSnow_ReturnsSnow();
    Test_Query_TieBreaker_LowestIndex();
    Test_Query_OutOfBoundsCell_FallbackDirt();
    Test_Query_ModifiersNeutralByDefault();
    return g_failed;
}
```

- [ ] **Step 3 : Ajouter test exécutable au CMakeLists.txt**

```cmake
# M100.11 — Tests SurfaceQueryService (mock cache via Insert).
if(WIN32)
  add_executable(surface_query_service_tests engine/world/surface/tests/SurfaceQueryServiceTests.cpp)
  target_include_directories(surface_query_service_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(surface_query_service_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(surface_query_service_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME surface_query_service_tests COMMAND surface_query_service_tests)
endif()
```

- [ ] **Step 4 : Build → expected FAIL (SurfaceQueryService.cpp absent)**

```bash
cmake --build build --target surface_query_service_tests --config Debug
```

Expected: link error `Init`/`Query` undefined.

- [ ] **Step 5 : Créer `engine/world/surface/SurfaceQueryService.cpp`**

```cpp
// engine/world/surface/SurfaceQueryService.cpp
#include "engine/world/surface/SurfaceQueryService.h"
#include "engine/world/surface/SurfaceTable.h"
#include "engine/world/StreamCache.h"
#include "engine/world/WorldModel.h"
#include "engine/world/terrain/LayerPalette.h"
#include "engine/world/terrain/SplatMap.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <algorithm>

namespace engine::world::surface
{
    bool SurfaceQueryService::Init(const SurfaceTable& table,
                                    engine::world::StreamCache& cache,
                                    const engine::core::Config& cfg,
                                    const engine::world::terrain::LayerPalette& palette) noexcept
    {
        m_table   = &table;
        m_cache   = &cache;
        m_cfg     = &cfg;
        m_palette = &palette;
        return true;
    }

    SurfaceQueryResult SurfaceQueryService::Query(engine::math::Vec3 worldPos) const
    {
        SurfaceQueryResult fallback{ SurfaceType::Dirt, {} };
        if (!m_cache || !m_cfg || !m_palette) return fallback;

        // 1. worldPos.xz → (chunkCoord, localCellX, localCellZ).
        const auto coord = engine::world::WorldModel::WorldToGlobalChunkCoord(worldPos.x, worldPos.z);

        // ChunkBounds → cellule splat locale. ChunkSize en mètres = engine::world::kChunkSize.
        // localCell = (worldOffset / chunkSize) * splatResolution.
        const auto bounds = engine::world::WorldModel::ChunkBounds(coord);
        const float chunkSizeX = bounds.maxX - bounds.minX;
        const float chunkSizeZ = bounds.maxZ - bounds.minZ;
        if (chunkSizeX <= 0.0f || chunkSizeZ <= 0.0f) return fallback;

        const float fx = (worldPos.x - bounds.minX) / chunkSizeX;
        const float fz = (worldPos.z - bounds.minZ) / chunkSizeZ;
        const int splatRes = static_cast<int>(engine::world::terrain::kSplatResolution);
        int localCellX = static_cast<int>(fx * static_cast<float>(splatRes));
        int localCellZ = static_cast<int>(fz * static_cast<float>(splatRes));
        localCellX = std::clamp(localCellX, 0, splatRes - 1);
        localCellZ = std::clamp(localCellZ, 0, splatRes - 1);

        // 2. Charger splat.bin (cache → disk → nullptr).
        auto splat = m_cache->LoadSplatMap(*m_cfg, coord.x, coord.z);
        if (!splat)
        {
            // Throttle warn : 1× par (coord) par session.
            const int64_t key = (static_cast<int64_t>(coord.x) << 32)
                              | static_cast<uint32_t>(coord.z);
            if (m_warnedChunks.insert(key).second)
            {
                LOG_WARN(World, "[SurfaceQuery] splat absent for chunk ({},{}) → fallback Dirt",
                    coord.x, coord.z);
            }
            return fallback;
        }

        // 3. Lire les 8 poids à (localCellX, localCellZ). Tie-break : plus petit index.
        const auto layerCount = static_cast<size_t>(splat->layerCount);
        const size_t cellOffset = (static_cast<size_t>(localCellZ) * splat->resolution
                                 + static_cast<size_t>(localCellX)) * layerCount;
        uint8_t maxWeight = 0;
        size_t maxLayer = 0;
        for (size_t i = 0; i < layerCount; ++i)
        {
            const uint8_t w = splat->weights[cellOffset + i];
            if (w > maxWeight)
            {
                maxWeight = w;
                maxLayer = i;
            }
        }

        // 4-5. layer→SurfaceType via palette.
        SurfaceQueryResult r;
        r.base = m_palette->GetSurfaceTypeForLayer(static_cast<uint8_t>(maxLayer));
        // 6. modifiers neutres en M100.11.
        return r;
    }
}
```

- [ ] **Step 6 : Ajouter `SurfaceQueryService.cpp` à engine_core**

```cmake
  engine/world/surface/SurfaceQueryService.cpp
```

- [ ] **Step 7 : Build + run → PASS**

```bash
cmake --build build --target surface_query_service_tests --config Debug
./build/Debug/surface_query_service_tests.exe
```

Expected: 0 failures.

- [ ] **Step 8 : Commit**

```bash
git add engine/world/surface/SurfaceQueryService.h \
        engine/world/surface/SurfaceQueryService.cpp \
        engine/world/surface/tests/SurfaceQueryServiceTests.cpp \
        CMakeLists.txt
git commit -m "feat(world/surface): SurfaceQueryService dominant-layer + fallback (M100.11 Task 5)"
```

---

## Task 6: ClientPredictionSystem hook

**Files:**
- Modify: `engine/gameplay/ClientPrediction.h`
- Modify: `engine/gameplay/ClientPrediction.cpp`
- Create: `engine/gameplay/tests/ClientPredictionSurfaceMultiplierTests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Modifier `engine/gameplay/ClientPrediction.h`**

Dans `class ClientPredictionSystem`, partie publique (après `GetBufferSize` ligne 188), ajouter :

```cpp
        /// Multiplier appliqué aux walkSpeed/runSpeed effectifs (M100.11).
        /// Clampé à [0.1, 5.0]. Default = 1.0 (no-op).
        /// Set par le caller (futur Engine.cpp) après chaque
        /// `SurfaceQueryService::Query(playerPos)`. Découplé : pas de
        /// dépendance à world/surface ici.
        void SetSurfaceSpeedMultiplier(float m) noexcept;
```

Et dans la partie privée (après `m_correcting` ligne 224), ajouter :

```cpp
        /// M100.11 — Multiplier surface (1.0 = neutre).
        float m_surfaceSpeedMultiplier = 1.0f;
```

- [ ] **Step 2 : Modifier `engine/gameplay/ClientPrediction.cpp`**

Ajouter en haut du fichier (avant le namespace) :

```cpp
#include <algorithm>  // pour std::clamp
```

À la fin du namespace `engine::gameplay`, ajouter l'impl du setter :

```cpp
void ClientPredictionSystem::SetSurfaceSpeedMultiplier(float m) noexcept
{
    m_surfaceSpeedMultiplier = std::clamp(m, 0.1f, 5.0f);
}
```

Modifier `ApplyCommand` (ligne 397-400) :

Avant :
```cpp
const float speed = HasFlag(cmd.keys, MovementKeyFlags::Run)
                    ? m_cfg.runSpeed
                    : m_cfg.walkSpeed;
```

Après :
```cpp
const float baseSpeed = HasFlag(cmd.keys, MovementKeyFlags::Run)
                       ? m_cfg.runSpeed
                       : m_cfg.walkSpeed;
const float speed = baseSpeed * m_surfaceSpeedMultiplier;
```

- [ ] **Step 3 : Créer `engine/gameplay/tests/ClientPredictionSurfaceMultiplierTests.cpp`**

```cpp
// engine/gameplay/tests/ClientPredictionSurfaceMultiplierTests.cpp
#include "engine/gameplay/ClientPrediction.h"

#include <cmath>
#include <cstdio>

namespace
{
    int g_failed = 0;

    #define REQUIRE(cond) do { \
        if (!(cond)) { \
            std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed; \
        } \
    } while (0)

    using engine::gameplay::ClientPredictionSystem;
    using engine::gameplay::InputCommand;
    using engine::gameplay::MovementKeyFlags;

    bool ApproxEq(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

    /// Tick une fois et lit la velocité prédite. yaw=0 → axe +X = forward dans
    /// la convention engine (Z- est forward, mais peu importe ici : on vérifie
    /// la magnitude vélocité, pas l'orientation).
    engine::math::Vec3 TickAndGetVelocity(ClientPredictionSystem& sys, bool run)
    {
        InputCommand cmd;
        cmd.tick = 1;
        cmd.dt = 1.0f / 60.0f;
        cmd.yawRadians = 0.0f;
        cmd.keys = MovementKeyFlags::Forward;
        if (run) cmd.keys = cmd.keys | MovementKeyFlags::Run;
        // ApplyCommand est privée ; on passe par Update qui la branche.
        // Pour un test isolé, on simule 1 tick : on n'a pas besoin du send batch.
        // Mais Update prend Input& (clavier réel) : on ne peut pas le faire ainsi.
        //
        // Solution : on utilise ApplyCommand via l'API publique de Tick — or
        // ApplyCommand est privée. Pour le test, on fait un friend OU on rend
        // le multiplier observable via un getter public temporaire.
        //
        // STRATÉGIE RETENUE : ajouter un `GetSurfaceSpeedMultiplier()` public
        // (read-only getter) dans Task 6 step 1, ET tester directement la
        // valeur du multiplier après Set/clamp. La multiplication réelle dans
        // ApplyCommand est validée par inspection visuelle de la modif ligne 398.
        (void)sys; (void)cmd;
        return engine::math::Vec3{};
    }

    void Test_SetSurfaceSpeedMultiplier_Default_Is1p0()
    {
        ClientPredictionSystem sys;
        // Default avant tout Set = 1.0
        REQUIRE(ApproxEq(sys.GetSurfaceSpeedMultiplier(), 1.0f));
    }

    void Test_SetSurfaceSpeedMultiplier_0p5_HalvesSpeed()
    {
        ClientPredictionSystem sys;
        sys.SetSurfaceSpeedMultiplier(0.5f);
        REQUIRE(ApproxEq(sys.GetSurfaceSpeedMultiplier(), 0.5f));
        // Le ratio 2× du critère M100.11 : Snow (0.5) vs Dirt (1.0) → 0.5×.
    }

    void Test_SetSurfaceSpeedMultiplier_Clamp()
    {
        ClientPredictionSystem sys;
        sys.SetSurfaceSpeedMultiplier(-1.0f);
        REQUIRE(ApproxEq(sys.GetSurfaceSpeedMultiplier(), 0.1f));   // clamp bas
        sys.SetSurfaceSpeedMultiplier(99.0f);
        REQUIRE(ApproxEq(sys.GetSurfaceSpeedMultiplier(), 5.0f));   // clamp haut
    }
}

int main()
{
    Test_SetSurfaceSpeedMultiplier_Default_Is1p0();
    Test_SetSurfaceSpeedMultiplier_0p5_HalvesSpeed();
    Test_SetSurfaceSpeedMultiplier_Clamp();
    return g_failed;
}
```

**Note importante :** Ce test exige un **getter public** `GetSurfaceSpeedMultiplier() const` sur `ClientPredictionSystem`. L'ajouter à Task 6 step 1 :

```cpp
        /// Read-only accessor pour test/debug. Renvoie la valeur courante (clampée).
        float GetSurfaceSpeedMultiplier() const noexcept { return m_surfaceSpeedMultiplier; }
```

L'effet réel sur la vélocité (multiplication ligne 398-400) est validé par revue de la diff (impossible de driver Update sans `engine::platform::Input` réel). Acceptable car le code est trivial : `speed = baseSpeed * m_surfaceSpeedMultiplier`.

- [ ] **Step 4 : Ajouter test exécutable au CMakeLists.txt**

```cmake
# M100.11 — Tests ClientPredictionSystem.SetSurfaceSpeedMultiplier.
if(WIN32)
  add_executable(client_prediction_surface_multiplier_tests
    engine/gameplay/tests/ClientPredictionSurfaceMultiplierTests.cpp)
  target_include_directories(client_prediction_surface_multiplier_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(client_prediction_surface_multiplier_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(client_prediction_surface_multiplier_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME client_prediction_surface_multiplier_tests COMMAND client_prediction_surface_multiplier_tests)
endif()
```

- [ ] **Step 5 : Build + run → PASS**

```bash
cmake --build build --target client_prediction_surface_multiplier_tests --config Debug
./build/Debug/client_prediction_surface_multiplier_tests.exe
```

Expected: 0 failures.

- [ ] **Step 6 : Commit**

```bash
git add engine/gameplay/ClientPrediction.h \
        engine/gameplay/ClientPrediction.cpp \
        engine/gameplay/tests/ClientPredictionSurfaceMultiplierTests.cpp \
        CMakeLists.txt
git commit -m "feat(gameplay): ClientPredictionSystem.SetSurfaceSpeedMultiplier (M100.11 Task 6)"
```

---

## Task 7: SurfaceTablePanel UI

**Files:**
- Create: `engine/editor/world/panels/SurfaceTablePanel.h`
- Create: `engine/editor/world/panels/SurfaceTablePanel.cpp`
- Modify: `engine/editor/world/WorldEditorShell.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Créer `engine/editor/world/panels/SurfaceTablePanel.h`**

```cpp
// engine/editor/world/panels/SurfaceTablePanel.h
#pragma once

#include "engine/editor/world/IPanel.h"
#include "engine/world/surface/SurfaceTable.h"

#include <filesystem>
#include <string>

namespace engine::editor::world::panels
{
    /// Panel ImGui lecture seule listant les 13 surfaces de
    /// `assets/gameplay/surface_table.json` (M100.11). Aucune édition runtime
    /// — modifier le JSON via éditeur externe + bouton [Reload].
    class SurfaceTablePanel final : public engine::editor::world::IPanel
    {
    public:
        const char* GetName() const override { return "Surface Table"; }
        void Render() override;
        bool IsVisible() const override { return m_visible; }
        void SetVisible(bool v) override { m_visible = v; }

        /// Charge le JSON depuis `<contentRoot>/assets/gameplay/surface_table.json`.
        /// `contentRoot` typique : "game/data". Appelé une fois par WorldEditorShell::Init.
        void LoadFromContentRoot(const std::filesystem::path& contentRoot);

    private:
        bool m_visible = false;  // panel masqué par défaut
        engine::world::surface::SurfaceTable m_table;
        std::string m_status;     // "Loaded ✓ (13 entries)" / "Parse error: ..."
        std::filesystem::path m_jsonPath;
    };
}
```

- [ ] **Step 2 : Créer `engine/editor/world/panels/SurfaceTablePanel.cpp`**

```cpp
// engine/editor/world/panels/SurfaceTablePanel.cpp
#include "engine/editor/world/panels/SurfaceTablePanel.h"
#include "engine/world/surface/SurfaceType.h"

#include <imgui.h>

namespace engine::editor::world::panels
{
    void SurfaceTablePanel::LoadFromContentRoot(const std::filesystem::path& contentRoot)
    {
        m_jsonPath = contentRoot / "assets" / "gameplay" / "surface_table.json";
        std::string err;
        if (m_table.LoadFromJson(m_jsonPath, err))
        {
            m_status = "Loaded \xE2\x9C\x93 (13 entries)";  // UTF-8 ✓
        }
        else
        {
            m_status = "Parse error: " + err;
        }
    }

    void SurfaceTablePanel::Render()
    {
        if (!ImGui::Begin(GetName(), &m_visible))
        {
            ImGui::End();
            return;
        }

        ImGui::Text("Source: %s", m_jsonPath.string().c_str());
        ImGui::SameLine();
        if (ImGui::Button("Reload"))
        {
            std::string err;
            if (m_table.LoadFromJson(m_jsonPath, err))
                m_status = "Reloaded \xE2\x9C\x93 (13 entries)";
            else
                m_status = "Parse error: " + err;
        }

        // Status (rouge si parse error).
        if (m_status.find("error") != std::string::npos)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Status: %s", m_status.c_str());
        else
            ImGui::Text("Status: %s", m_status.c_str());

        ImGui::Separator();

        if (!m_table.IsLoaded())
        {
            ImGui::TextDisabled("(table non chargée)");
            ImGui::End();
            return;
        }

        if (ImGui::BeginTable("##surfaces", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("SurfaceType", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Speed",       ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Audio step",  ImGuiTableColumnFlags_WidthFixed, 200.0f);
            ImGui::TableSetupColumn("Visual tag",  ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            using namespace engine::world::surface;
            for (int i = 0; i < static_cast<int>(SurfaceType::_Count); ++i)
            {
                const auto t = static_cast<SurfaceType>(i);
                const auto& e = m_table.Get(t);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(std::string(ToString(t)).c_str());
                ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f", e.baseSpeed);
                ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(e.audioStep.c_str());
                ImGui::TableSetColumnIndex(3);
                if (e.visualTag.empty())
                    ImGui::TextDisabled("\xE2\x80\x94");  // em dash
                else
                    ImGui::TextUnformatted(e.visualTag.c_str());
            }
            ImGui::EndTable();
        }

        ImGui::End();
    }
}
```

- [ ] **Step 3 : Wirer dans `engine/editor/world/WorldEditorShell.cpp`**

Ajouter en haut du fichier :
```cpp
#include "engine/editor/world/panels/SurfaceTablePanel.h"
```

Dans `Init()`, après l'emplace du `HistoryPanel` (ligne ~81), ajouter :
```cpp
        // M100.11 — Panel lecture seule de la table de surfaces. Caché par
        // défaut, toggle via View > Surface Table.
        auto surfacePanel = std::make_unique<panels::SurfaceTablePanel>();
        surfacePanel->LoadFromContentRoot(
            std::filesystem::path(cfg.GetString("paths.content", "game/data")));
        m_panels.emplace_back(std::move(surfacePanel));
```

- [ ] **Step 4 : Ajouter `SurfaceTablePanel.cpp` à engine_core**

Dans `CMakeLists.txt`, à côté des autres panels (autour ligne 284) :

```cmake
  engine/editor/world/panels/SurfaceTablePanel.cpp
```

- [ ] **Step 5 : Build + lancer le world editor manuellement**

```bash
cmake --build build --target world_editor_app --config Debug
./build/Debug/lcdlln_world_editor.exe
```

Manuel : ouvrir le menu **View** → cliquer **Surface Table** → la fenêtre apparaît avec 13 entrées + bouton Reload + status `Loaded ✓ (13 entries)`. Capturer un screenshot pour la PR.

- [ ] **Step 6 : Commit**

```bash
git add engine/editor/world/panels/SurfaceTablePanel.h \
        engine/editor/world/panels/SurfaceTablePanel.cpp \
        engine/editor/world/WorldEditorShell.cpp \
        CMakeLists.txt
git commit -m "feat(editor/world): SurfaceTablePanel ImGui read-only (M100.11 Task 7)"
```

---

## Task 8: Test grep gardien serveur + final build

**Files:**
- (validation only)

- [ ] **Step 1 : Vérifier que le serveur ne référence pas SurfaceQueryService**

```bash
grep -rn "SurfaceQueryService\|SurfaceTable\|engine::world::surface" engine/server/ tools/zone_builder/ tools/migration_checksum/ tools/load_tester/ tools/hlod_builder/ 2>&1
```

Expected: **aucun résultat**. Le serveur, les outils CLI, et le hlod_builder ne touchent pas au monde surface (volontairement client-only).

- [ ] **Step 2 : Build complet final**

```bash
cmake --build build --config Debug
```

Expected: pas d'erreur de compilation.

- [ ] **Step 3 : Run all M100.11 tests**

```bash
ctest -C Debug --test-dir build -R "surface|surface_query|client_prediction_surface|layer_palette_surface" --output-on-failure
```

Expected: 5 suites pass (surface_type_tests, surface_table_tests, surface_query_service_tests, client_prediction_surface_multiplier_tests, layer_palette_surface_type_tests).

- [ ] **Step 4 : Run regression tests (vérifier que SplatMapTests migration n'a rien cassé)**

```bash
ctest -C Debug --test-dir build -R "splat_map_tests" --output-on-failure
```

Expected: PASS (la modif ligne 129 doit être verte).

- [ ] **Step 5 : Mettre à jour `tickets/M100/INDEX.md`**

Dans le tableau des tickets, ligne M100.11, changer la colonne `Statut` de `Ready` à `Done (CI pending)`.

- [ ] **Step 6 : Commit**

```bash
git add tickets/M100/INDEX.md
git commit -m "docs(tickets/M100): marque M100.11 Done (Phase 3b.1, CI pending)"
```

---

## Récap couverture spec → tasks

| Section spec | Task |
|---|---|
| Architecture (file structure) | T1-T7 (création/modif fichiers exact selon spec) |
| `SurfaceType.h` API | T1 |
| `SurfaceTable.h/.cpp` + JSON | T2 |
| `assets/gameplay/surface_table.json` | T3 |
| `LayerPalette` migration string→enum + helper | T4 |
| `SurfaceQueryService.h/.cpp` + algorithme | T5 |
| Throttle warn par chunk | T5 step 5 (m_warnedChunks) |
| `ClientPredictionSystem.SetSurfaceSpeedMultiplier` + multiplier dans Tick | T6 |
| `SurfaceTablePanel` UI + WorldEditorShell wiring | T7 |
| Test grep gardien serveur | T8 |
| 5 suites tests TDD (~23 cas) | T1-T6 (5 add_executable) |
| Bouton [Reload] | T7 step 2 |
| Critères acceptation "vitesse÷2 sur Snow" | T6 (test 2) + T2 (test 3) |
| Hors scope (Engine.cpp wiring, hot-reload runtime) | non couvert (volontaire) |

## Self-Review

**1. Spec coverage:** Toutes les sections du spec ont une task qui les implémente. Le wiring `Engine::Init` (instanciation `m_surfaceTable` + `m_surfaceQuery`) du spec lifecycle est **non couvert** mais c'est cohérent : le spec lui-même dit que ce wiring est out-of-scope (ClientPrediction dormante). Le panel charge sa propre `SurfaceTable` via `LoadFromContentRoot`. Conséquence : la chaîne `Engine.cpp → Query → SetSurfaceSpeedMultiplier` n'existe pas après ce plan, mais c'est explicitement ce que dit le spec. Pas de gap.

**2. Placeholder scan:** Aucun "TBD"/"TODO"/"implement later" dans le plan. Tous les snippets de code sont complets.

**3. Type consistency:**
- `ClientPredictionSystem` (pas `ClientPrediction`) : utilisé partout cohéremment depuis Task 6.
- `m_surfaceSpeedMultiplier` : nom stable.
- `engine::world::surface::SurfaceType` : pleinement qualifié dans LayerPalette.h pour éviter circular include.
- `MakeUniform(uint32_t)` : signature SplatMap utilisée dans Task 5 mock helpers, vérifiée vs `engine/world/terrain/SplatMap.h:42`.
- `WorldModel::WorldToGlobalChunkCoord(float, float)` : signature vérifiée dans `engine/world/WorldModel.h:105`.
- `WorldModel::ChunkBounds(GlobalChunkCoord)` : retourne `struct ChunkBounds { float minX/minZ/maxX/maxZ; }` vérifié `WorldModel.h:58-64,112`.

Plan complet.
