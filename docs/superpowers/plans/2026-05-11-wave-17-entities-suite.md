# Wave 17 — Entities suite (Unit / Player / WorldObject / Creature) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Étendre la base `Object`/`ObjectGuid`/`UpdateField`/`UpdateMask` (Wave 7 #575) avec la hiérarchie complète : `Object → WorldObject → Unit → {Player, Creature}`. Ajouter les enums d'indices `UpdateField` par classe. Aucun travail réseau dans cette Wave (un Wave 17b adressera l'opcode `UPDATE_OBJECT` si nécessaire).

**Architecture:** 4 nouvelles classes server-side héritant de `Object`. Chaque classe expose ses propres `UpdateField<T>` via un enum d'indices stables (ObjectField → UnitField → PlayerField / CreatureField), pour permettre la sérialisation delta. Pattern aligné WoW/cmangos. Header-only à la base, `.cpp` minimaliste pour translation unit nommée (cohérence avec `Object.cpp`).

**Tech Stack:** C++20, namespace `engine::server::entities`, tests via `lcdlln_add_simple_test` (asserts + printf, pas de framework). Commentaires en français (convention repo).

**Spec:** [`docs/superpowers/specs/2026-05-11-waves-17-38-server-foundations-design.md`](../specs/2026-05-11-waves-17-38-server-foundations-design.md) §4 Wave 17.

---

## File Structure

### Création (nouveaux fichiers)

| Fichier | Responsabilité |
|---------|----------------|
| `src/shardd/entities/UpdateFieldIndices.h` | Enums `ObjectFieldIdx`, `UnitFieldIdx`, `PlayerFieldIdx`, `CreatureFieldIdx`. Valeurs **stables** (wire dépend). Constants `kFieldCount` par classe. |
| `src/shardd/entities/WorldObject.h` | Classe `WorldObject` : extends `Object`, ajoute position 3D (x, y, z, orientation), zoneId, mapId. `AddToWorld()`/`RemoveFromWorld()` stubs. |
| `src/shardd/entities/WorldObject.cpp` | Translation unit (constructeur, destructeur out-of-line). |
| `src/shardd/entities/WorldObjectTests.cpp` | Tests : construction, position get/set, AddToWorld flag. |
| `src/shardd/entities/Unit.h` | Classe `Unit` : extends `WorldObject`, ajoute HP/MaxHP/Level/Faction/Mana/MaxMana. Tous via `UpdateField<T>`. |
| `src/shardd/entities/Unit.cpp` | Translation unit. |
| `src/shardd/entities/UnitTests.cpp` | Tests : HP set marque dirty, faction transitions, IsAlive. |
| `src/shardd/entities/Player.h` | Classe `Player` : extends `Unit`, ajoute accountId, characterId, name (read-only après construction). |
| `src/shardd/entities/Player.cpp` | Translation unit. |
| `src/shardd/entities/PlayerTests.cpp` | Tests : construction, name immutable, level set delta. |
| `src/shardd/entities/Creature.h` | Classe `Creature` : extends `Unit`, ajoute creatureTemplateEntry, spawnId. |
| `src/shardd/entities/Creature.cpp` | Translation unit. |
| `src/shardd/entities/CreatureTests.cpp` | Tests : creatureTemplateEntry stable, OnDeath flag, spawn pool reference. |
| `src/shardd/entities/UpdateMaskDeltaTests.cpp` | Tests cross-classe : modifier HP sur Unit → seul HP est dirty, mask compact. |

### Modification

| Fichier | Modification |
|---------|--------------|
| `src/CMakeLists.txt` | Ajouter 4 `.cpp` au target `server_app` ; ajouter 4 nouveaux `lcdlln_add_simple_test` (`worldobject_tests`, `unit_tests`, `player_tests`, `creature_tests`) ; étendre `entities_foundation_tests` avec `UpdateMaskDeltaTests.cpp`. |
| `CODEBASE_MAP.md` | Section 22 "Entities foundation" : mettre à jour "Manque (Wave 17 roadmap)" → "Livré Wave 17 PR #XXX". |

---

## Task 1 : `UpdateFieldIndices.h` — énum stables par classe

**Files:**
- Create: `src/shardd/entities/UpdateFieldIndices.h`

- [ ] **Step 1: Créer le header avec les enums d'indices**

```cpp
#pragma once
// UpdateFieldIndices : enums centralisees des indices UpdateField par classe.
// Valeurs STABLES (wire format en depend). Ne JAMAIS reassigner un indice
// existant ; ajouter de nouveaux indices a la fin de chaque enum.
//
// Convention : chaque sous-classe demarre ses propres indices APRES la fin
// du parent. Object 0..N-1, Unit N..M-1, Player/Creature M..K-1.

#include <cstddef>

namespace engine::server::entities
{
    /// Indices pour la classe Object (base). Stable.
    enum ObjectFieldIdx : size_t
    {
        kObjectFieldGuid       = 0,  ///< ObjectGuid (8 bytes = 2 slots 32-bit)
        kObjectFieldGuidHigh   = 1,  ///< high part du guid
        kObjectFieldType       = 2,  ///< ObjectType (uint8)
        kObjectFieldEntry      = 3,  ///< template entry (creature/gameobject) ou 0
        kObjectFieldScaleX     = 4,  ///< scale visuel (float, slot 32-bit)

        kObjectFieldEnd        = 5
    };

    /// kObjectFieldCount : nombre total de champs Object.
    static constexpr size_t kObjectFieldCount = kObjectFieldEnd;

    /// Indices pour WorldObject (extends Object). Demarre apres Object end.
    enum WorldObjectFieldIdx : size_t
    {
        kWorldObjectFieldMapId       = kObjectFieldEnd,      // 5
        kWorldObjectFieldZoneId      = kObjectFieldEnd + 1,  // 6
        kWorldObjectFieldPosX        = kObjectFieldEnd + 2,  // 7
        kWorldObjectFieldPosY        = kObjectFieldEnd + 3,  // 8
        kWorldObjectFieldPosZ        = kObjectFieldEnd + 4,  // 9
        kWorldObjectFieldOrientation = kObjectFieldEnd + 5,  // 10

        kWorldObjectFieldEnd         = kObjectFieldEnd + 6   // 11
    };

    static constexpr size_t kWorldObjectFieldCount = kWorldObjectFieldEnd;

    /// Indices pour Unit (extends WorldObject). Demarre apres WorldObject end.
    enum UnitFieldIdx : size_t
    {
        kUnitFieldHealth      = kWorldObjectFieldEnd,      // 11
        kUnitFieldMaxHealth   = kWorldObjectFieldEnd + 1,  // 12
        kUnitFieldMana        = kWorldObjectFieldEnd + 2,  // 13
        kUnitFieldMaxMana     = kWorldObjectFieldEnd + 3,  // 14
        kUnitFieldLevel       = kWorldObjectFieldEnd + 4,  // 15
        kUnitFieldFaction     = kWorldObjectFieldEnd + 5,  // 16

        kUnitFieldEnd         = kWorldObjectFieldEnd + 6   // 17
    };

    static constexpr size_t kUnitFieldCount = kUnitFieldEnd;

    /// Indices pour Player (extends Unit). Demarre apres Unit end.
    enum PlayerFieldIdx : size_t
    {
        kPlayerFieldAccountId    = kUnitFieldEnd,      // 17
        kPlayerFieldCharacterId  = kUnitFieldEnd + 1,  // 18
        kPlayerFieldXp           = kUnitFieldEnd + 2,  // 19

        kPlayerFieldEnd          = kUnitFieldEnd + 3   // 20
    };

    static constexpr size_t kPlayerFieldCount = kPlayerFieldEnd;

    /// Indices pour Creature (extends Unit). Demarre apres Unit end (parallele a Player).
    enum CreatureFieldIdx : size_t
    {
        kCreatureFieldTemplateEntry = kUnitFieldEnd,      // 17
        kCreatureFieldSpawnId       = kUnitFieldEnd + 1,  // 18

        kCreatureFieldEnd           = kUnitFieldEnd + 2   // 19
    };

    static constexpr size_t kCreatureFieldCount = kCreatureFieldEnd;
}
```

- [ ] **Step 2: Build (sanity)**

Run: `cmake --build build/linux-x64 --target server_app 2>&1 | tail -20`
Expected: compile OK, header-only, pas d'erreur.

- [ ] **Step 3: Commit**

```bash
git add src/shardd/entities/UpdateFieldIndices.h
git commit -m "feat(entities): Wave 17a -- UpdateFieldIndices enums stables par classe"
```

---

## Task 2 : `WorldObject` — base position 3D

**Files:**
- Create: `src/shardd/entities/WorldObject.h`
- Create: `src/shardd/entities/WorldObject.cpp`
- Test: `src/shardd/entities/WorldObjectTests.cpp`

- [ ] **Step 1: Écrire le test d'abord**

```cpp
// WorldObjectTests.cpp
#include "src/shardd/entities/WorldObject.h"

#include <cassert>
#include <cstdio>

using namespace engine::server::entities;

namespace
{
    void TestWorldObjectConstruction()
    {
        ObjectGuid g(ObjectType::GameObject, 42);
        WorldObject obj(g, kWorldObjectFieldCount);
        assert(obj.Guid() == g);
        assert(obj.GetMapId() == 0);
        assert(obj.IsInWorld() == false);
        std::puts("[OK] TestWorldObjectConstruction");
    }

    void TestWorldObjectPositionSet()
    {
        ObjectGuid g(ObjectType::GameObject, 1);
        WorldObject obj(g, kWorldObjectFieldCount);
        obj.SetPosition(100.5f, 200.5f, 30.0f, 1.57f);
        assert(obj.GetPosX() == 100.5f);
        assert(obj.GetPosY() == 200.5f);
        assert(obj.GetPosZ() == 30.0f);
        assert(obj.GetOrientation() == 1.57f);
        // 4 champs ont change : pos x/y/z + orientation
        assert(obj.Mask().PopCount() == 4);
        std::puts("[OK] TestWorldObjectPositionSet");
    }

    void TestWorldObjectAddRemoveWorld()
    {
        ObjectGuid g(ObjectType::Creature, 99);
        WorldObject obj(g, kWorldObjectFieldCount);
        assert(obj.IsInWorld() == false);
        obj.AddToWorld();
        assert(obj.IsInWorld() == true);
        obj.RemoveFromWorld();
        assert(obj.IsInWorld() == false);
        std::puts("[OK] TestWorldObjectAddRemoveWorld");
    }

    void TestWorldObjectMapZone()
    {
        ObjectGuid g(ObjectType::Player, 7);
        WorldObject obj(g, kWorldObjectFieldCount);
        obj.SetMapId(1);
        obj.SetZoneId(42);
        assert(obj.GetMapId() == 1);
        assert(obj.GetZoneId() == 42);
        std::puts("[OK] TestWorldObjectMapZone");
    }
}

int main()
{
    TestWorldObjectConstruction();
    TestWorldObjectPositionSet();
    TestWorldObjectAddRemoveWorld();
    TestWorldObjectMapZone();
    std::puts("All WorldObject tests passed");
    return 0;
}
```

- [ ] **Step 2: Run test, expect FAIL (header not exist)**

Run: `cmake --build build/linux-x64 --target worldobject_tests 2>&1 | tail -20`
Expected: FAIL — `WorldObject.h` not found.

- [ ] **Step 3: Écrire `WorldObject.h`**

```cpp
#pragma once
// WorldObject : Object + position 3D dans une map. Toute entite ayant une
// presence physique (Player, Creature, GameObject, Corpse) en herite.
//
// Position : (x, y, z, orientation) en metres + radians, world-space.
// MapId : identifie la carte logique. ZoneId : sous-zone (Stormwind, etc.).
//
// AddToWorld / RemoveFromWorld : flag binaire pour l'instant. L'integration
// avec la grid spatiale (Wave 18 GridVisitor) viendra dans la PR suivante.

#include "src/shardd/entities/Object.h"
#include "src/shardd/entities/UpdateField.h"
#include "src/shardd/entities/UpdateFieldIndices.h"

#include <cstdint>

namespace engine::server::entities
{
    class WorldObject : public Object
    {
    public:
        /// \param guid identifiant immuable
        /// \param fieldCount nombre de champs total (sous-classe peut etendre)
        WorldObject(ObjectGuid guid, size_t fieldCount = kWorldObjectFieldCount)
            : Object(guid, fieldCount)
            , m_mapId(kWorldObjectFieldMapId, &Mask())
            , m_zoneId(kWorldObjectFieldZoneId, &Mask())
            , m_posX(kWorldObjectFieldPosX, &Mask())
            , m_posY(kWorldObjectFieldPosY, &Mask())
            , m_posZ(kWorldObjectFieldPosZ, &Mask())
            , m_orientation(kWorldObjectFieldOrientation, &Mask())
        {}

        ~WorldObject() override = default;

        /// Set position 3D + orientation. Marque les 4 champs dirty si change.
        void SetPosition(float x, float y, float z, float orientation)
        {
            m_posX.Set(x);
            m_posY.Set(y);
            m_posZ.Set(z);
            m_orientation.Set(orientation);
        }

        float GetPosX() const noexcept { return m_posX.Get(); }
        float GetPosY() const noexcept { return m_posY.Get(); }
        float GetPosZ() const noexcept { return m_posZ.Get(); }
        float GetOrientation() const noexcept { return m_orientation.Get(); }

        void SetMapId(uint32_t id) { m_mapId.Set(id); }
        uint32_t GetMapId() const noexcept { return m_mapId.Get(); }

        void SetZoneId(uint32_t id) { m_zoneId.Set(id); }
        uint32_t GetZoneId() const noexcept { return m_zoneId.Get(); }

        /// Stub : marque l'objet comme present dans le monde. L'integration
        /// grid viendra dans Wave 18 (GridVisitor + GridNotifier).
        void AddToWorld() noexcept { m_inWorld = true; }
        void RemoveFromWorld() noexcept { m_inWorld = false; }
        bool IsInWorld() const noexcept { return m_inWorld; }

    private:
        UpdateField<uint32_t> m_mapId;
        UpdateField<uint32_t> m_zoneId;
        UpdateField<float>    m_posX;
        UpdateField<float>    m_posY;
        UpdateField<float>    m_posZ;
        UpdateField<float>    m_orientation;
        bool                  m_inWorld = false;
    };
}
```

- [ ] **Step 4: Écrire `WorldObject.cpp` (translation unit minimaliste)**

```cpp
// WorldObject : translation unit. Toutes les methodes sont inline dans le
// header. Ce .cpp existe pour CMake (cible nommee + symbole faible si
// methodes deplacees out-of-line dans le futur).
#include "src/shardd/entities/WorldObject.h"

namespace engine::server::entities
{
    // Methodes out-of-line a ajouter au besoin.
}
```

- [ ] **Step 5: Wire CMakeLists.txt (target + test)**

Modifier `src/CMakeLists.txt` autour de la ligne 199 (server_app entities) :

```cmake
    # Wave 7 Entities foundation + Wave 17 hierarchie.
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Object.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/WorldObject.cpp
```

Et autour de la ligne 375 (tests) :

```cmake
  # Wave 17 — Entities suite tests.
  lcdlln_add_simple_test(worldobject_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/WorldObjectTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/WorldObject.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Object.cpp)
```

- [ ] **Step 6: Build + run test**

```bash
cmake --build build/linux-x64 --target worldobject_tests
./build/linux-x64/worldobject_tests
```

Expected: 4 lignes `[OK] Test...` puis `All WorldObject tests passed`.

- [ ] **Step 7: Commit**

```bash
git add src/shardd/entities/WorldObject.{h,cpp} \
        src/shardd/entities/WorldObjectTests.cpp \
        src/CMakeLists.txt
git commit -m "feat(entities): Wave 17b -- WorldObject (Object + position 3D + map/zone)"
```

---

## Task 3 : `Unit` — combat stats (HP, MP, level, faction)

**Files:**
- Create: `src/shardd/entities/Unit.h`
- Create: `src/shardd/entities/Unit.cpp`
- Test: `src/shardd/entities/UnitTests.cpp`

- [ ] **Step 1: Écrire le test d'abord**

```cpp
// UnitTests.cpp
#include "src/shardd/entities/Unit.h"

#include <cassert>
#include <cstdio>

using namespace engine::server::entities;

namespace
{
    void TestUnitConstruction()
    {
        ObjectGuid g(ObjectType::Creature, 1);
        Unit u(g, kUnitFieldCount);
        assert(u.GetHealth() == 0);
        assert(u.GetMaxHealth() == 0);
        assert(u.GetLevel() == 0);
        assert(u.GetFaction() == 0);
        assert(u.IsAlive() == false); // HP == 0 → dead
        std::puts("[OK] TestUnitConstruction");
    }

    void TestUnitHealthSet()
    {
        ObjectGuid g(ObjectType::Creature, 2);
        Unit u(g, kUnitFieldCount);
        u.SetMaxHealth(1000);
        u.SetHealth(500);
        assert(u.GetHealth() == 500);
        assert(u.GetMaxHealth() == 1000);
        assert(u.IsAlive() == true);
        // 2 champs dirty
        assert(u.Mask().TestBit(kUnitFieldHealth));
        assert(u.Mask().TestBit(kUnitFieldMaxHealth));
        std::puts("[OK] TestUnitHealthSet");
    }

    void TestUnitHealthClampOverflow()
    {
        ObjectGuid g(ObjectType::Creature, 3);
        Unit u(g, kUnitFieldCount);
        u.SetMaxHealth(100);
        u.SetHealth(200); // > max → clamp a max
        assert(u.GetHealth() == 100);
        std::puts("[OK] TestUnitHealthClampOverflow");
    }

    void TestUnitFactionLevel()
    {
        ObjectGuid g(ObjectType::Creature, 4);
        Unit u(g, kUnitFieldCount);
        u.SetLevel(60);
        u.SetFaction(35); // alliance
        assert(u.GetLevel() == 60);
        assert(u.GetFaction() == 35);
        std::puts("[OK] TestUnitFactionLevel");
    }

    void TestUnitOnReplicationSentClearsMask()
    {
        ObjectGuid g(ObjectType::Creature, 5);
        Unit u(g, kUnitFieldCount);
        u.SetHealth(100);
        assert(u.IsDirty());
        u.OnReplicationSent();
        assert(!u.IsDirty());
        std::puts("[OK] TestUnitOnReplicationSentClearsMask");
    }
}

int main()
{
    TestUnitConstruction();
    TestUnitHealthSet();
    TestUnitHealthClampOverflow();
    TestUnitFactionLevel();
    TestUnitOnReplicationSentClearsMask();
    std::puts("All Unit tests passed");
    return 0;
}
```

- [ ] **Step 2: Run test, expect FAIL**

Run: `cmake --build build/linux-x64 --target unit_tests 2>&1 | tail -10`
Expected: FAIL — `Unit.h` not found.

- [ ] **Step 3: Écrire `Unit.h`**

```cpp
#pragma once
// Unit : WorldObject + stats combat (HP, MP, level, faction). Base pour
// Player et Creature. N'expose pas encore les hooks combat (Wave 19
// HostileRefManager) ni les Auras (Wave 23 SpellMgr).
//
// IsAlive : HP > 0. Pas de notion de spirit-of-redemption ou d'invuln a ce
// stade — c'est le travail des Auras (Wave 23).

#include "src/shardd/entities/WorldObject.h"
#include "src/shardd/entities/UpdateField.h"
#include "src/shardd/entities/UpdateFieldIndices.h"

#include <algorithm>
#include <cstdint>

namespace engine::server::entities
{
    class Unit : public WorldObject
    {
    public:
        Unit(ObjectGuid guid, size_t fieldCount = kUnitFieldCount)
            : WorldObject(guid, fieldCount)
            , m_health(kUnitFieldHealth, &Mask())
            , m_maxHealth(kUnitFieldMaxHealth, &Mask())
            , m_mana(kUnitFieldMana, &Mask())
            , m_maxMana(kUnitFieldMaxMana, &Mask())
            , m_level(kUnitFieldLevel, &Mask())
            , m_faction(kUnitFieldFaction, &Mask())
        {}

        ~Unit() override = default;

        /// Set HP. Clamp [0, maxHealth].
        void SetHealth(uint32_t hp)
        {
            const uint32_t maxHp = m_maxHealth.Get();
            m_health.Set(std::min(hp, maxHp));
        }
        uint32_t GetHealth() const noexcept { return m_health.Get(); }

        void SetMaxHealth(uint32_t maxHp) { m_maxHealth.Set(maxHp); }
        uint32_t GetMaxHealth() const noexcept { return m_maxHealth.Get(); }

        void SetMana(uint32_t mp)
        {
            const uint32_t maxMp = m_maxMana.Get();
            m_mana.Set(std::min(mp, maxMp));
        }
        uint32_t GetMana() const noexcept { return m_mana.Get(); }

        void SetMaxMana(uint32_t maxMp) { m_maxMana.Set(maxMp); }
        uint32_t GetMaxMana() const noexcept { return m_maxMana.Get(); }

        void SetLevel(uint32_t lvl) { m_level.Set(lvl); }
        uint32_t GetLevel() const noexcept { return m_level.Get(); }

        void SetFaction(uint32_t f) { m_faction.Set(f); }
        uint32_t GetFaction() const noexcept { return m_faction.Get(); }

        bool IsAlive() const noexcept { return m_health.Get() > 0; }

    private:
        UpdateField<uint32_t> m_health;
        UpdateField<uint32_t> m_maxHealth;
        UpdateField<uint32_t> m_mana;
        UpdateField<uint32_t> m_maxMana;
        UpdateField<uint32_t> m_level;
        UpdateField<uint32_t> m_faction;
    };
}
```

- [ ] **Step 4: Écrire `Unit.cpp`**

```cpp
#include "src/shardd/entities/Unit.h"

namespace engine::server::entities
{
    // Methodes out-of-line a ajouter au besoin.
}
```

- [ ] **Step 5: Wire CMakeLists.txt**

Ajouter sous server_app entities :
```cmake
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Unit.cpp
```

Ajouter test :
```cmake
  lcdlln_add_simple_test(unit_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/UnitTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Unit.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/WorldObject.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Object.cpp)
```

- [ ] **Step 6: Build + run**

```bash
cmake --build build/linux-x64 --target unit_tests
./build/linux-x64/unit_tests
```

Expected: 5 lignes `[OK]` puis `All Unit tests passed`.

- [ ] **Step 7: Commit**

```bash
git add src/shardd/entities/Unit.{h,cpp} \
        src/shardd/entities/UnitTests.cpp \
        src/CMakeLists.txt
git commit -m "feat(entities): Wave 17c -- Unit (WorldObject + HP/MP/level/faction)"
```

---

## Task 4 : `Player` — extends Unit avec identite compte

**Files:**
- Create: `src/shardd/entities/Player.h`
- Create: `src/shardd/entities/Player.cpp`
- Test: `src/shardd/entities/PlayerTests.cpp`

- [ ] **Step 1: Écrire le test d'abord**

```cpp
// PlayerTests.cpp
#include "src/shardd/entities/Player.h"

#include <cassert>
#include <cstdio>
#include <string>

using namespace engine::server::entities;

namespace
{
    void TestPlayerConstruction()
    {
        ObjectGuid g(ObjectType::Player, 42);
        Player p(g, /*accountId*/1001, /*characterId*/42, /*name*/"TestHero");
        assert(p.Guid() == g);
        assert(p.GetAccountId() == 1001);
        assert(p.GetCharacterId() == 42);
        assert(p.GetName() == "TestHero");
        std::puts("[OK] TestPlayerConstruction");
    }

    void TestPlayerNameImmutable()
    {
        // name est read-only apres construction (par design).
        ObjectGuid g(ObjectType::Player, 1);
        Player p(g, 1, 1, "Alpha");
        const std::string& n = p.GetName();
        assert(n == "Alpha");
        // Pas d'API SetName : verification compile-time via absence du setter.
        std::puts("[OK] TestPlayerNameImmutable");
    }

    void TestPlayerXp()
    {
        ObjectGuid g(ObjectType::Player, 2);
        Player p(g, 1, 2, "Beta");
        p.SetXp(12345);
        assert(p.GetXp() == 12345);
        assert(p.Mask().TestBit(kPlayerFieldXp));
        std::puts("[OK] TestPlayerXp");
    }

    void TestPlayerHpHerite()
    {
        // Verifier que Player herite bien des stats Unit.
        ObjectGuid g(ObjectType::Player, 3);
        Player p(g, 1, 3, "Gamma");
        p.SetMaxHealth(5000);
        p.SetHealth(3000);
        p.SetLevel(60);
        assert(p.GetHealth() == 3000);
        assert(p.GetLevel() == 60);
        std::puts("[OK] TestPlayerHpHerite");
    }
}

int main()
{
    TestPlayerConstruction();
    TestPlayerNameImmutable();
    TestPlayerXp();
    TestPlayerHpHerite();
    std::puts("All Player tests passed");
    return 0;
}
```

- [ ] **Step 2: Run test, expect FAIL**

Run: `cmake --build build/linux-x64 --target player_tests 2>&1 | tail -10`
Expected: FAIL.

- [ ] **Step 3: Écrire `Player.h`**

```cpp
#pragma once
// Player : Unit + identite compte (accountId, characterId, name).
// Le name est immuable apres construction (le renaming passe par une
// suppression+creation cote masterd, pas par mutation in-place).
//
// XP : UpdateField pour la replication delta (UI client peut afficher
// progression sans poll DB).

#include "src/shardd/entities/Unit.h"
#include "src/shardd/entities/UpdateField.h"
#include "src/shardd/entities/UpdateFieldIndices.h"

#include <cstdint>
#include <string>
#include <utility>

namespace engine::server::entities
{
    class Player : public Unit
    {
    public:
        Player(ObjectGuid guid, uint64_t accountId, uint64_t characterId, std::string name)
            : Unit(guid, kPlayerFieldCount)
            , m_accountId(kPlayerFieldAccountId, &Mask(), accountId)
            , m_characterId(kPlayerFieldCharacterId, &Mask(), characterId)
            , m_xp(kPlayerFieldXp, &Mask())
            , m_name(std::move(name))
        {
            // Mark account + character ids dirty pour la premiere replication.
            m_accountId.MarkDirty();
            m_characterId.MarkDirty();
        }

        ~Player() override = default;

        uint64_t GetAccountId() const noexcept { return m_accountId.Get(); }
        uint64_t GetCharacterId() const noexcept { return m_characterId.Get(); }
        const std::string& GetName() const noexcept { return m_name; }

        void SetXp(uint32_t xp) { m_xp.Set(xp); }
        uint32_t GetXp() const noexcept { return m_xp.Get(); }

    private:
        UpdateField<uint64_t> m_accountId;
        UpdateField<uint64_t> m_characterId;
        UpdateField<uint32_t> m_xp;
        const std::string     m_name;
    };
}
```

- [ ] **Step 4: Écrire `Player.cpp`**

```cpp
#include "src/shardd/entities/Player.h"

namespace engine::server::entities
{
    // Methodes out-of-line a ajouter au besoin.
}
```

- [ ] **Step 5: Wire CMakeLists.txt**

Ajouter server_app :
```cmake
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Player.cpp
```

Ajouter test :
```cmake
  lcdlln_add_simple_test(player_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/PlayerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Player.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Unit.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/WorldObject.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Object.cpp)
```

- [ ] **Step 6: Build + run**

```bash
cmake --build build/linux-x64 --target player_tests
./build/linux-x64/player_tests
```

Expected: 4 lignes `[OK]` puis `All Player tests passed`.

- [ ] **Step 7: Commit**

```bash
git add src/shardd/entities/Player.{h,cpp} \
        src/shardd/entities/PlayerTests.cpp \
        src/CMakeLists.txt
git commit -m "feat(entities): Wave 17d -- Player (Unit + accountId/characterId/name/xp)"
```

---

## Task 5 : `Creature` — extends Unit avec template entry

**Files:**
- Create: `src/shardd/entities/Creature.h`
- Create: `src/shardd/entities/Creature.cpp`
- Test: `src/shardd/entities/CreatureTests.cpp`

- [ ] **Step 1: Écrire le test d'abord**

```cpp
// CreatureTests.cpp
#include "src/shardd/entities/Creature.h"

#include <cassert>
#include <cstdio>

using namespace engine::server::entities;

namespace
{
    void TestCreatureConstruction()
    {
        ObjectGuid g(ObjectType::Creature, 100);
        Creature c(g, /*templateEntry*/666, /*spawnId*/12345);
        assert(c.Guid() == g);
        assert(c.GetTemplateEntry() == 666);
        assert(c.GetSpawnId() == 12345);
        std::puts("[OK] TestCreatureConstruction");
    }

    void TestCreatureTemplateImmutable()
    {
        // templateEntry est immuable apres construction (par design).
        ObjectGuid g(ObjectType::Creature, 101);
        Creature c(g, 999, 1);
        assert(c.GetTemplateEntry() == 999);
        // Pas d'API SetTemplateEntry.
        std::puts("[OK] TestCreatureTemplateImmutable");
    }

    void TestCreatureUnitHerite()
    {
        // Verifier que Creature herite bien des stats Unit.
        ObjectGuid g(ObjectType::Creature, 102);
        Creature c(g, 500, 1);
        c.SetMaxHealth(2000);
        c.SetHealth(1500);
        c.SetFaction(85); // monster faction
        assert(c.GetHealth() == 1500);
        assert(c.GetFaction() == 85);
        assert(c.IsAlive());
        std::puts("[OK] TestCreatureUnitHerite");
    }

    void TestCreatureDeath()
    {
        ObjectGuid g(ObjectType::Creature, 103);
        Creature c(g, 500, 1);
        c.SetMaxHealth(100);
        c.SetHealth(100);
        assert(c.IsAlive());
        c.SetHealth(0);
        assert(!c.IsAlive());
        std::puts("[OK] TestCreatureDeath");
    }
}

int main()
{
    TestCreatureConstruction();
    TestCreatureTemplateImmutable();
    TestCreatureUnitHerite();
    TestCreatureDeath();
    std::puts("All Creature tests passed");
    return 0;
}
```

- [ ] **Step 2: Run test, expect FAIL**

Run: `cmake --build build/linux-x64 --target creature_tests 2>&1 | tail -10`
Expected: FAIL.

- [ ] **Step 3: Écrire `Creature.h`**

```cpp
#pragma once
// Creature : Unit + ancrage spawn pool. templateEntry pointe vers
// creature_template (donnees statiques), spawnId vers creature_spawn
// (instance dans le pool). Les deux sont immuables apres construction.
//
// L'IA + le motion stack viendront via composition (Wave 19+ pour
// HostileRefManager, Wave 24 pour Navmesh).

#include "src/shardd/entities/Unit.h"
#include "src/shardd/entities/UpdateField.h"
#include "src/shardd/entities/UpdateFieldIndices.h"

#include <cstdint>

namespace engine::server::entities
{
    class Creature : public Unit
    {
    public:
        Creature(ObjectGuid guid, uint32_t templateEntry, uint32_t spawnId)
            : Unit(guid, kCreatureFieldCount)
            , m_templateEntry(kCreatureFieldTemplateEntry, &Mask(), templateEntry)
            , m_spawnId(kCreatureFieldSpawnId, &Mask(), spawnId)
        {
            m_templateEntry.MarkDirty();
            m_spawnId.MarkDirty();
        }

        ~Creature() override = default;

        uint32_t GetTemplateEntry() const noexcept { return m_templateEntry.Get(); }
        uint32_t GetSpawnId() const noexcept { return m_spawnId.Get(); }

    private:
        UpdateField<uint32_t> m_templateEntry;
        UpdateField<uint32_t> m_spawnId;
    };
}
```

- [ ] **Step 4: Écrire `Creature.cpp`**

```cpp
#include "src/shardd/entities/Creature.h"

namespace engine::server::entities
{
    // Methodes out-of-line a ajouter au besoin.
}
```

- [ ] **Step 5: Wire CMakeLists.txt**

Ajouter server_app :
```cmake
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Creature.cpp
```

Ajouter test :
```cmake
  lcdlln_add_simple_test(creature_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/CreatureTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Creature.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Unit.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/WorldObject.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Object.cpp)
```

- [ ] **Step 6: Build + run**

```bash
cmake --build build/linux-x64 --target creature_tests
./build/linux-x64/creature_tests
```

Expected: 4 lignes `[OK]` puis `All Creature tests passed`.

- [ ] **Step 7: Commit**

```bash
git add src/shardd/entities/Creature.{h,cpp} \
        src/shardd/entities/CreatureTests.cpp \
        src/CMakeLists.txt
git commit -m "feat(entities): Wave 17e -- Creature (Unit + templateEntry/spawnId)"
```

---

## Task 6 : Tests delta cross-classe — UpdateMaskDeltaTests

**Files:**
- Create: `src/shardd/entities/UpdateMaskDeltaTests.cpp`

- [ ] **Step 1: Écrire les tests cross-classe**

```cpp
// UpdateMaskDeltaTests : vérifie que les hierarchies Object/WorldObject/
// Unit/Player/Creature ne marquent QUE les champs effectivement modifies,
// et que les indices sont coherents (pas de chevauchement).
//
// C'est le test critique de delta replication : si HP change sur un Player,
// SEUL kUnitFieldHealth doit etre dirty, pas le tableau entier.

#include "src/shardd/entities/Creature.h"
#include "src/shardd/entities/Player.h"

#include <cassert>
#include <cstdio>

using namespace engine::server::entities;

namespace
{
    /// Modifier seulement HP sur un Unit : seul kUnitFieldHealth est dirty.
    void TestDeltaUnitOnlyHealth()
    {
        ObjectGuid g(ObjectType::Creature, 1);
        Unit u(g, kUnitFieldCount);
        u.OnReplicationSent(); // partir d'un mask propre
        u.SetMaxHealth(100);
        u.SetHealth(50);
        // 2 bits dirty : maxHealth + health
        assert(u.Mask().PopCount() == 2);
        assert(u.Mask().TestBit(kUnitFieldHealth));
        assert(u.Mask().TestBit(kUnitFieldMaxHealth));
        assert(!u.Mask().TestBit(kUnitFieldLevel));
        assert(!u.Mask().TestBit(kWorldObjectFieldPosX));
        std::puts("[OK] TestDeltaUnitOnlyHealth");
    }

    /// Modifier name : impossible (immutable). Modifier XP seul.
    void TestDeltaPlayerOnlyXp()
    {
        ObjectGuid g(ObjectType::Player, 2);
        Player p(g, 1001, 2, "Test");
        p.OnReplicationSent(); // reset mask post-construction
        p.SetXp(500);
        // Seul kPlayerFieldXp doit etre dirty.
        assert(p.Mask().PopCount() == 1);
        assert(p.Mask().TestBit(kPlayerFieldXp));
        std::puts("[OK] TestDeltaPlayerOnlyXp");
    }

    /// Modifier position 3D sur un Creature : 4 bits dirty (x/y/z/orientation),
    /// rien d'autre (HP/MP/template intacts).
    void TestDeltaCreaturePosition()
    {
        ObjectGuid g(ObjectType::Creature, 3);
        Creature c(g, 100, 1);
        c.OnReplicationSent();
        c.SetPosition(1.0f, 2.0f, 3.0f, 0.5f);
        assert(c.Mask().PopCount() == 4);
        assert(c.Mask().TestBit(kWorldObjectFieldPosX));
        assert(c.Mask().TestBit(kWorldObjectFieldPosY));
        assert(c.Mask().TestBit(kWorldObjectFieldPosZ));
        assert(c.Mask().TestBit(kWorldObjectFieldOrientation));
        assert(!c.Mask().TestBit(kUnitFieldHealth));
        std::puts("[OK] TestDeltaCreaturePosition");
    }

    /// Verification que Player et Creature ne partagent pas leurs indices
    /// au-dela de Unit (kPlayerFieldXp != kCreatureFieldSpawnId).
    /// Note : Player et Creature DEMARRENT au meme index (kUnitFieldEnd),
    /// c'est OK car ce sont deux classes distinctes — chacune a son propre
    /// mask.
    void TestPlayerCreatureIndicesNoOverlap()
    {
        // Verification compile-time : kPlayerFieldEnd != kCreatureFieldEnd
        // (a confirmer si les deux divergent en taille).
        static_assert(kPlayerFieldEnd == kUnitFieldEnd + 3, "Player has 3 own fields");
        static_assert(kCreatureFieldEnd == kUnitFieldEnd + 2, "Creature has 2 own fields");
        std::puts("[OK] TestPlayerCreatureIndicesNoOverlap");
    }

    /// Reset mask post-replication clear tous les bits (cross-classe).
    void TestReplicationSentClearsAll()
    {
        ObjectGuid g(ObjectType::Player, 4);
        Player p(g, 1, 4, "ResetTest");
        p.SetPosition(10, 20, 30, 0);
        p.SetHealth(100);
        p.SetXp(999);
        assert(p.IsDirty());
        p.OnReplicationSent();
        assert(!p.IsDirty());
        assert(p.Mask().PopCount() == 0);
        std::puts("[OK] TestReplicationSentClearsAll");
    }
}

int main()
{
    TestDeltaUnitOnlyHealth();
    TestDeltaPlayerOnlyXp();
    TestDeltaCreaturePosition();
    TestPlayerCreatureIndicesNoOverlap();
    TestReplicationSentClearsAll();
    std::puts("All UpdateMask delta tests passed");
    return 0;
}
```

- [ ] **Step 2: Wire CMakeLists.txt — nouveau test target**

```cmake
  lcdlln_add_simple_test(update_mask_delta_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/UpdateMaskDeltaTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Creature.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Player.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Unit.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/WorldObject.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Object.cpp)
```

- [ ] **Step 3: Build + run**

```bash
cmake --build build/linux-x64 --target update_mask_delta_tests
./build/linux-x64/update_mask_delta_tests
```

Expected: 5 lignes `[OK]` puis `All UpdateMask delta tests passed`.

- [ ] **Step 4: Run tous les nouveaux tests + tests Wave 7 (non-regression)**

```bash
cmake --build build/linux-x64 --target worldobject_tests unit_tests player_tests creature_tests update_mask_delta_tests entities_foundation_tests
ctest --test-dir build/linux-x64 -R "worldobject_tests|unit_tests|player_tests|creature_tests|update_mask_delta_tests|entities_foundation_tests" -V
```

Expected: 6 tests verts.

- [ ] **Step 5: Commit**

```bash
git add src/shardd/entities/UpdateMaskDeltaTests.cpp src/CMakeLists.txt
git commit -m "test(entities): Wave 17f -- UpdateMask delta cross-classe (Unit/Player/Creature)"
```

---

## Task 7 : Mise à jour CODEBASE_MAP.md

**Files:**
- Modify: `CODEBASE_MAP.md`

- [ ] **Step 1: Mise à jour Section 22**

Trouver dans `CODEBASE_MAP.md` la Section 22 "Entities foundation (Wave 7, #575)" et la remplacer par :

```markdown
## 22. Entities foundation (Wave 7 → Wave 17, #575 + Wave 17 PR)

### Hiérarchie complète (post-Wave 17)

```
Object (Wave 7, base)
  └── WorldObject (Wave 17 — ajoute position 3D + mapId/zoneId + IsInWorld)
        └── Unit (Wave 17 — ajoute HP/MP/level/faction + IsAlive)
              ├── Player (Wave 17 — ajoute accountId/characterId/name/xp)
              └── Creature (Wave 17 — ajoute templateEntry/spawnId)
```

### Fichiers livrés

| Fichier | Wave | Rôle |
|---------|------|------|
| `src/shardd/entities/Object.{h,cpp}` | 7 | Base abstraite : `ObjectGuid m_guid`, virtual `Update(diff)`, helpers UpdateMask. |
| `src/shardd/entities/ObjectGuid.h` | 7 | GUID 64 bits : `[type 8 bits | id 56 bits]`. |
| `src/shardd/entities/UpdateField.h` | 7 | Wrapper de champ avec auto-flag dirty sur Set(). |
| `src/shardd/entities/UpdateMask.h` | 7 | Bitmask delta replication. |
| `src/shardd/entities/UpdateFieldIndices.h` | 17 | Enums stables `ObjectFieldIdx`, `WorldObjectFieldIdx`, `UnitFieldIdx`, `PlayerFieldIdx`, `CreatureFieldIdx`. |
| `src/shardd/entities/WorldObject.{h,cpp}` | 17 | Object + position 3D + mapId/zoneId + AddToWorld/RemoveFromWorld stubs. |
| `src/shardd/entities/Unit.{h,cpp}` | 17 | WorldObject + HP/MaxHP/MP/MaxMP/level/faction + IsAlive. |
| `src/shardd/entities/Player.{h,cpp}` | 17 | Unit + accountId/characterId/name (immutable) + xp. |
| `src/shardd/entities/Creature.{h,cpp}` | 17 | Unit + templateEntry/spawnId (tous deux immutables après construction). |

### Tests CTest

| Cible | Couverture |
|-------|------------|
| `entities_foundation_tests` (Wave 7) | ObjectGuid encoding, UpdateMask bit ops, UpdateField auto-flag, Object dirty tracking. |
| `worldobject_tests` (Wave 17) | Construction, SetPosition, AddToWorld/RemoveFromWorld, MapId/ZoneId. |
| `unit_tests` (Wave 17) | HP/MaxHP/MP/level/faction, clamp overflow, IsAlive, OnReplicationSent. |
| `player_tests` (Wave 17) | accountId/characterId/name immutable, XP, héritage stats Unit. |
| `creature_tests` (Wave 17) | templateEntry/spawnId immutable, héritage Unit, death state. |
| `update_mask_delta_tests` (Wave 17) | Delta replication cross-classe : seul HP dirty si seul HP changé, etc. |

### Prochaines étapes (post-Wave 17)

- Wave 18 (GridVisitor/GridNotifier) : `WorldObject::AddToWorld()` insérera dans `SpatialPartition` au lieu d'un simple flag.
- Wave 23 (Spells) : `Unit` recevra `m_auras` (composition) pour persistance buffs/debuffs.
- Wave réseau dédiée : opcode `UPDATE_OBJECT` push qui sérialise `UpdateMask` + valeurs dirty.
```

- [ ] **Step 2: Commit**

```bash
git add CODEBASE_MAP.md
git commit -m "docs(map): CODEBASE_MAP -- Wave 17 hierarchie Entities (Unit/Player/Creature)"
```

---

## Task 8 : Verification finale + smoke check Wave 7 non-régression

**Files:** none (sanity check only)

- [ ] **Step 1: Build complet du shard**

```bash
cmake --build build/linux-x64 --target server_app 2>&1 | tail -10
```

Expected: build OK, pas de warning nouveau.

- [ ] **Step 2: Run tous les tests entities**

```bash
ctest --test-dir build/linux-x64 -R "entities_foundation_tests|worldobject_tests|unit_tests|player_tests|creature_tests|update_mask_delta_tests" -V 2>&1 | tail -30
```

Expected: 6 tests verts (0 failed).

- [ ] **Step 3: Smoke non-régression — tous les tests Wave 1-16 toujours verts**

```bash
ctest --test-dir build/linux-x64 -E "entities" --output-on-failure 2>&1 | tail -20
```

Expected: 0 failed. Aucun test pré-existant cassé par Wave 17.

- [ ] **Step 4: Final commit (s'il reste quelque chose)**

```bash
git status
# Si tout est commité, rien à faire. Sinon ajouter et commit.
```

- [ ] **Step 5: Push branche + open PR**

```bash
git push -u origin claude/mystifying-nightingale-1bc440
```

Puis `gh pr create` avec body incluant la mention déploiement :

> **Déploiement** : ⚠️ REDÉPLOIEMENT SHARD REQUIS — nouvelles classes Unit/Player/WorldObject/Creature côté shard. Pas de wire-breaking (les opcodes UPDATE_OBJECT seront ajoutés en Wave 17b dédiée si nécessaire). Pas de migration DB.

---

## Recap & critère "Done"

À l'issue des 8 Tasks :

- ✅ 5 commits TDD propres (1 par classe + 1 tests delta + 1 doc).
- ✅ Hierarchie complete `Object → WorldObject → Unit → {Player, Creature}`.
- ✅ 6 tests CTest verts (1 Wave 7 + 5 Wave 17).
- ✅ Zéro régression sur les tests Wave 1-16.
- ✅ `CODEBASE_MAP.md` à jour.
- ✅ Build Linux server_app OK.

Diff total estimé : ~600-700 lignes (sous le seuil 800 lignes — pas de split nécessaire).

Prochaine wave : **Wave 18 — GridVisitor + GridNotifier** sur la base `SpatialPartition` existante.

---

## Self-Review (effectuée 2026-05-11)

**Couverture spec §4 Wave 17** :
- ✅ Unit/Player/WorldObject/Creature livrés (Tasks 2-5).
- ✅ UpdateFields enums étendus (Task 1).
- ✅ UpdateMask génération delta sur Update() (utilise `UpdateField<T>::Set()` Wave 7).
- ✅ Tests `unit_tests`, `player_tests`, `creature_tests`, `update_mask_delta_tests` (Tasks 3-6).
- ✅ Splitting prévu si > 800 lignes (estimé sous le seuil).
- ⚠️ Wire opcode `UPDATE_OBJECT` : **différé en Wave 17b dédiée**. Décision documentée dans Task 5 + mention déploiement.
- ⚠️ `WorldObject::AddToWorld()` est un stub : intégration grid effective en Wave 18. Documenté dans le header.

**Placeholders** : aucun TBD/TODO trouvé. Tous les snippets de code sont complets.

**Type consistency** :
- `kPlayerFieldEnd == kUnitFieldEnd + 3` ✓ (3 champs Player propres : accountId, characterId, xp).
- `kCreatureFieldEnd == kUnitFieldEnd + 2` ✓ (2 champs Creature propres : templateEntry, spawnId).
- Les indices se chevauchent volontairement entre Player et Creature (chacun a son propre mask) → static_assert dans Task 6 documente l'invariant.

**Ambigüité** : aucune. Les setters/getters sont nommés cohéremment, le pattern UpdateField<T> est le même partout.
