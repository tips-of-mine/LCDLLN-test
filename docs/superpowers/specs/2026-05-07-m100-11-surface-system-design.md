# M100.11 — Surface Material System & SurfaceQueryService — Design

> **Phase 3b — partie 1/2.** Pivot gameplay référencé par M100.15, M100.16,
> M100.19, M100.26, M100.27, M100.33. Ce design couvre **uniquement M100.11** ;
> M100.12 (Collision Proxy) sera designé séparément après merge.

## Goal

Livrer la chaîne client `splat-map → SurfaceType → SurfaceModifiers → vitesse
joueur` :

1. Enum forte `SurfaceType` (13 entrées + `_Count`).
2. Table de surfaces `assets/gameplay/surface_table.json` chargée par
   `SurfaceTable`.
3. Service `SurfaceQueryService::Query(worldPos)` qui combine splat dominante
   + palette de layers + table.
4. Hook `ClientPrediction::SetSurfaceSpeedMultiplier(float)` qui module la
   vitesse de marche/course.
5. Panneau éditeur read-only `SurfaceTablePanel` (Tools → Surface Table).

## Architecture

```
engine/world/surface/                    [nouveau, client-only]
├── SurfaceType.h                        enum class SurfaceType : uint16_t
├── SurfaceTable.{h,cpp}                 LoadFromJson + Get(SurfaceType)
├── SurfaceQueryService.{h,cpp}          Query(Vec3) → SurfaceQueryResult
└── tests/
    ├── SurfaceTypeTests.cpp
    ├── SurfaceTableTests.cpp
    └── SurfaceQueryServiceTests.cpp

engine/editor/world/panels/
└── SurfaceTablePanel.{h,cpp}            ImGui read-only table

engine/world/terrain/LayerPalette.{h,cpp}    [modif]
  + SurfaceType GetSurfaceTypeForLayer(uint8_t) const
  + parse-time string→enum

engine/gameplay/ClientPrediction.{h,cpp}     [modif]
  + void SetSurfaceSpeedMultiplier(float) noexcept
  + private float m_surfaceSpeedMultiplier = 1.0f
  + multiplication ligne 398-400

engine/gameplay/tests/
└── ClientPredictionSurfaceMultiplierTests.cpp

assets/gameplay/surface_table.json       [nouveau, contenu spec ticket]

CMakeLists.txt                          [modif : sources + 5 add_executable test]
```

### Boundaries

- `SurfaceQueryService` dépend de `SurfaceTable` + `StreamCache` + `Config` +
  `LayerPalette`. **Pas** de dépendance Vulkan, ImGui, ou réseau.
- `ClientPrediction` ne `#include` **pas** `SurfaceQueryService.h`. Découplage
  strict : un futur caller (Engine.cpp) appellera `Query()` puis
  `SetSurfaceSpeedMultiplier()`. Cette indirection garde gameplay agnostique
  du système world/surface.
- `SurfaceTablePanel` ne fait que **lire** `SurfaceTable&`. Aucune écriture
  runtime (édition par texte JSON externe).
- **Aucun fichier ajouté à `engine/server/CMakeLists.txt`.** `server_app` ne
  link déjà pas `engine_core` (vérifié : il compile une liste explicite de
  fichiers individuels). La séparation demandée par le ticket est factuelle.

### Lifecycle

- `SurfaceTable` chargée une fois via `Engine::Init()` depuis
  `assets/gameplay/surface_table.json`. Membre `Engine::m_surfaceTable`.
- `SurfaceQueryService` init via `Engine::Init()` avec `&m_surfaceTable +
  &m_streamCache + &m_config + &m_layerPalette`. Membre `Engine::m_surfaceQuery`.
- Wiring per-frame `Engine.cpp` Query → SetSurfaceSpeedMultiplier :
  **out-of-scope M100.11**. Raison : `ClientPrediction::Tick()` n'est
  actuellement appelée nulle part en production (mover dormant, le client
  utilise `m_orbitalCameraController` free-fly aujourd'hui). Le wiring final
  viendra avec M100.33 (Playtest Mode F5). Pour M100.11 la chaîne entière est
  testée par tests unitaires.

## Data structures & APIs

### `SurfaceType.h`

```cpp
namespace engine::world::surface
{
    enum class SurfaceType : uint16_t
    {
        Dirt = 0, Grass, Mud, Sand, Rock, Snow,
        ShallowWater, DeepWater, LavaCooled,
        WheatField, CornField, Road, Bridge,
        _Count
    };

    /// "Dirt".."Bridge". Renvoie "_Invalid" pour les valeurs hors enum.
    std::string_view ToString(SurfaceType t) noexcept;

    /// True + sortie écrite si `s` matche un nom valide. False sinon (out non touché).
    bool ParseSurfaceType(std::string_view s, SurfaceType& out) noexcept;
}
```

Ordre **figé**. Tout futur ajout va AVANT `_Count` ; aucun renumérotage.

### `SurfaceTable.h`

```cpp
namespace engine::world::surface
{
    struct SurfaceTableEntry
    {
        SurfaceType type;
        float baseSpeed;       // multiplicateur vs Dirt baseline 1.0
        std::string audioStep;
        std::string visualTag;
    };

    class SurfaceTable
    {
    public:
        bool LoadFromJson(const std::filesystem::path& path, std::string& outError);
        const SurfaceTableEntry& Get(SurfaceType t) const noexcept;
        bool IsLoaded() const noexcept { return m_loaded; }

    private:
        std::array<SurfaceTableEntry, static_cast<size_t>(SurfaceType::_Count)> m_entries{};
        bool m_loaded = false;
    };
}
```

JSON parser : pattern `LayerPalette` (linear minimal hand-rolled).
Validation au load :
- exactement 13 entrées dans le tableau `surfaces`
- chaque `type` parse via `ParseSurfaceType` ; doublons interdits
- `baseSpeed >= 0.0f`
- `audioStep` non vide (warning sinon, accepté)

Format `assets/gameplay/surface_table.json` (cf. ticket M100.11 lignes 84-115,
13 entrées Dirt..Bridge).

### `SurfaceQueryService.h`

```cpp
namespace engine::world::surface
{
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
        // Optional : `mutable std::unordered_set<int64_t> m_warnedChunks` pour throttle warn
    };
}
```

#### Algorithme `Query`

1. `worldPos.xz` → `(GlobalChunkCoord coord, int localCellX, int localCellZ)`
   via `engine::world::WorldToGlobalChunkCoord` (existant).
2. `auto splat = m_cache->LoadSplatMap(*m_cfg, coord.x, coord.z)`. Si nullptr
   → `LOG_WARN` une fois par `coord` puis retourne `{Dirt, {}}`.
3. Clamp `localCellX/Z` aux bornes splat-map (cas limite bord chunk).
4. Lire les **8 poids** à `(localCellX, localCellZ)`. Choisir
   `argmax(weights[0..7])`. Tie-break : plus petit index.
5. `surfaceType = m_palette->GetSurfaceTypeForLayer(layerIdx)`.
6. `modifiers = {}` (M100.11 ne calcule rien ; M100.26 modifiera).
7. Retourne `{surfaceType, {}}`.

Le `speedMultiplier` final est calculé par le **caller** :
`table.Get(result.base).baseSpeed * result.modifiers.speedMultiplier`.

### `LayerPalette` modif

```cpp
struct LayerEntry {
    uint32_t index = 0;
    std::string name;
    std::string albedoPath;
    std::string normalPath;
    std::string armPath;
    float tilingMeters = 4.0f;
    std::string surfaceTypeName;                   // <- renommé depuis surfaceType (string) ; conservé pour debug/serialize
    SurfaceType surfaceType = SurfaceType::Dirt;   // <- nouveau (enum canonique)
};
```

**Migration des 2 callers existants** :
- `engine/world/terrain/LayerPalette.cpp:98` : remplacer
  `e.surfaceType = ExtractStringField(obj, "surfaceType", "Dirt");` par
  `e.surfaceTypeName = ExtractStringField(...)` + appel `ParseSurfaceType(e.surfaceTypeName, e.surfaceType)`.
- `engine/world/terrain/tests/SplatMapTests.cpp:129` : remplacer
  `REQUIRE(palette.layers[5].surfaceType == "Rock")` par
  `REQUIRE(palette.layers[5].surfaceType == SurfaceType::Rock)` (compare enum, plus robuste).

Au `LoadLayerPalette()` :
- parse le champ JSON `"surfaceType"` dans `surfaceTypeName` (comme avant)
- appelle `ParseSurfaceType(surfaceTypeName, layer.surfaceType)`
- si parse échoue : `layer.surfaceType = SurfaceType::Dirt`, `LOG_WARN` une fois

```cpp
// Helper accessor sur LayerPalette
SurfaceType GetSurfaceTypeForLayer(uint8_t layer) const noexcept;
// Précondition : layer < 8. Ne fait pas de bounds check (debug assert).
```

### `ClientPrediction` modif

```cpp
class ClientPrediction {
public:
    // ... existing API ...

    /// Multiplier appliqué aux walkSpeed/runSpeed du Config. Clampé [0.1, 5.0].
    /// Default = 1.0 (pas d'effet).
    void SetSurfaceSpeedMultiplier(float m) noexcept;

private:
    // ... existing members ...
    float m_surfaceSpeedMultiplier = 1.0f;
};
```

Modification dans `Tick()` (lignes 397-400 de ClientPrediction.cpp) :

```cpp
const float baseSpeed = HasFlag(cmd.keys, MovementKeyFlags::Run)
                      ? m_cfg.runSpeed : m_cfg.walkSpeed;
const float speed = baseSpeed * m_surfaceSpeedMultiplier;
m_state.velocity.x = wishX * speed;
m_state.velocity.z = wishZ * speed;
```

Pas de #include de SurfaceQueryService. Découplage strict.

## SurfaceTablePanel UI

### Placement menu

Sous **Tools → Surface Table** dans la menubar du `WorldEditorShell` (cohérent
avec `Tools → Texture Library`, `Tools → Stamp Library` existants). Toggle
on/off via menu, état persisté dans `editor_world_layout.ini`.

### Layout

```
┌─ Surface Table ─────────────────────────────────────────────────────────┐
│ Source: assets/gameplay/surface_table.json   [Reload]                   │
│ Status: Loaded ✓ (13 entries)                                           │
│                                                                          │
│ ┌──────────────┬───────┬──────────────────────┬───────────────────────┐ │
│ │ SurfaceType  │ Speed │ Audio step           │ Visual tag            │ │
│ ├──────────────┼───────┼──────────────────────┼───────────────────────┤ │
│ │ Dirt         │ 1.00  │ step_dirt            │ dust_sprint           │ │
│ │ Grass        │ 0.95  │ step_grass           │ grass_bend            │ │
│ │ ... 11 autres lignes ...                                             │ │
│ │ Bridge       │ 1.10  │ step_wood            │ —                     │ │
│ └──────────────┴───────┴──────────────────────┴───────────────────────┘ │
└──────────────────────────────────────────────────────────────────────────┘
```

- `ImGui::BeginTable("##surfaces", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)`.
- 13 lignes dans l'ordre de l'enum.
- Champs vides → `—` (em dash).
- **Aucune** édition (pas de `InputText`, pas de `InputFloat`).

### Bouton `[Reload]`

Re-parse `assets/gameplay/surface_table.json` from disk dans **la copie panel**.
Status devient `Reloaded ✓ (13 entries)` ou `Parse error: <msg>` (rouge).
Ne touche **pas** au `SurfaceQueryService` runtime — hot-reload runtime
out-of-scope M100.11.

### API panel

```cpp
namespace engine::editor::world::panels
{
    class SurfaceTablePanel
    {
    public:
        void Init(const std::filesystem::path& contentRoot);
        void Render();   // ImGui draw - no-op si !m_open
        bool IsOpen() const noexcept { return m_open; }
        void SetOpen(bool o) noexcept { m_open = o; }

    private:
        bool m_open = false;
        engine::world::surface::SurfaceTable m_table;
        std::string m_status;
        std::filesystem::path m_jsonPath;
    };
}
```

`WorldEditorShell` instancie `m_surfaceTablePanel`, expose toggle dans
menubar, appelle `Render()` chaque frame si `IsOpen()`.

## Tests TDD (5 suites, ≈ 23 cas)

Framework du repo : macros maison `REQUIRE` / `REQUIRE_FALSE` / `REQUIRE_EQ`.
Pattern `add_executable() + add_test()` par suite (cohérent avec
`terrain_chunk_tests`, `splat_map_tests`, `chunk_runtime_tests`).

### `surface_type_tests` (5 cas)
- `ToString_AllValues` : 13 chaînes attendues
- `ToString_OutOfRange` : `_Count` ou cast invalide → `"_Invalid"`
- `ParseSurfaceType_AllValues` : 13 chaînes valides → bonne enum
- `ParseSurfaceType_Unknown` : "Foobar" → false, out inchangé
- `EnumCount_Is13` : `static_cast<int>(_Count) == 13`

### `surface_table_tests` (6 cas)
- `LoadFromJson_FixtureHas13Entries` : table loaded, 13 entries
- `LoadFromJson_DirtBaseSpeedIs1p0` : `Get(Dirt).baseSpeed == 1.00f`
- `LoadFromJson_SnowBaseSpeedIs0p5` : ratio 2× du critère acceptance
- `LoadFromJson_AudioStepNonEmpty` : tous audioStep non vides
- `LoadFromJson_MalformedJson_Fails` : JSON tronqué → false + message
- `LoadFromJson_MissingEntry_Fails` : 12 entrées → fail

Fixtures écrites en dur (`std::ofstream` temp file → load → verify → cleanup).

### `surface_query_service_tests` (7 cas — **le cœur**)

Mock `StreamCache` minimal : sous-classe avec `SetMockSplat(coord, splatMap)`
+ override de `LoadSplatMap`. Pattern utilisé par `chunk_runtime_tests` pour
les `IGpuBufferAllocator`.

- `Query_SplatAbsent_FallbackDirt` : nullptr LoadSplatMap → `{Dirt, {}}`
- `Query_DominantLayerDirt_ReturnsDirt` : 100% layer 0 (Dirt) → `Dirt`
- `Query_DominantLayerRock_ReturnsRock` : 100% layer 4 (Rock) → `Rock`
- `Query_DominantLayerSnow_ReturnsSnow` : 100% layer 5 (Snow) — critère acceptance
- `Query_TieBreaker_LowestIndex` : 50/50 layers → plus petit index
- `Query_OutOfBoundsCell_FallbackDirt` : worldPos hors splat → fallback
- `Query_ModifiersNeutralByDefault` : `result.modifiers.speedMultiplier == 1.0f`

### `client_prediction_surface_multiplier_tests` (3 cas)
- `SetSurfaceSpeedMultiplier_Default_Is1p0` : sans appel → walkSpeed standard
- `SetSurfaceSpeedMultiplier_0p5_HalvesSpeed` : multiplier 0.5 + walkSpeed 5.0
  → vélocité 2.5 sur axe move
- `SetSurfaceSpeedMultiplier_Clamp` : Set(-1.0)→0.1 ; Set(99.0)→5.0

Vérification via `ClientPrediction::Tick()` + lecture vélocité post-tick.

### `layer_palette_surface_type_tests` (3 cas, nouvelle suite)
- `LoadLayerPalette_ParsesSurfaceTypeString` : `"surfaceType":"Snow"` →
  `palette.layers[i].surfaceType == SurfaceType::Snow`
- `LoadLayerPalette_UnknownSurfaceType_FallsBackDirt` : `"Foobar"` → `Dirt`
  + parse réussit (warn ailleurs)
- `GetSurfaceTypeForLayer_ValidIndex` : retourne enum stockée pour layer 0..7

### CMake tests

Ajout au pattern existant des suites M100 (cf. terrain_chunk_tests block) :

```cmake
foreach(suite IN ITEMS
    surface_type_tests
    surface_table_tests
    surface_query_service_tests
    client_prediction_surface_multiplier_tests
    layer_palette_surface_type_tests)
  if(NOT UNIX)  # même garde que les autres tests M100 (pas sur Linux server)
    add_executable(${suite} engine/world/surface/tests/${suite}.cpp)
    target_link_libraries(${suite} PRIVATE engine_core)
    add_test(NAME ${suite} COMMAND ${suite})
  endif()
endforeach()
```

(`client_prediction_surface_multiplier_tests` vit dans
`engine/gameplay/tests/`, pas `surface/tests/` — chemin ajusté dans
l'add_executable.)

## Risques techniques

| Risque | Mitigation |
|---|---|
| **splat.bin absents au runtime** (cas actuel : aucun fichier produit, M100.34 pas Done) | Fallback `Dirt` neutre. Throttle warn par `(coord)` dans SurfaceQueryService : `mutable std::unordered_set<int64_t>` ; clé = `(coord.x << 32) \| coord.z`. 1 warn par chunk par session. |
| **Race chargement palette ↔ table** | Init ordonné single-thread : `SurfaceTable::Load` → `LoadLayerPalette` → `SurfaceQueryService::Init`. |
| **Surface modifiers vs serveur autoritatif** | Hors scope M100.11. Note : si autorité serveur revient sur le mouvement, les modifiers devront soit être prédits côté client + accept snap-back, soit propagés serveur. Aujourd'hui `gameplay_udp.enabled = false`. |
| **Hot-reload SurfaceTable runtime** | Hors scope. Bouton Reload du panel ne touche que la copie panel. |
| **Engine ne wire pas Query→SetSurfaceSpeedMultiplier en prod** | Volontaire — ClientPrediction dormante. Tests unitaires couvrent toute la chaîne. Wiring final = M100.33. Critère "vitesse÷2 sur Snow" vérifié unit, pas en playtest. |
| **Tie-breaker layers ex-aequo** | Spec : plus petit `layerIdx`. Déterministe + testé. |
| **localCellX/Z hors splat-map (bord chunk)** | Clamp aux bornes [0, splatRes-1]. Test couvre. |

## Critères d'acceptation

| Critère | Validation |
|---|---|
| Enum `SurfaceType` avec 13 entrées + `_Count` | `surface_type_tests::EnumCount_Is13` |
| `SurfaceTable::LoadFromJson` parse `surface_table.json` sans erreur | `surface_table_tests::LoadFromJson_FixtureHas13Entries` |
| `Query` → `Dirt` sur 100% layer 0 | `Query_DominantLayerDirt_ReturnsDirt` |
| `Query` → `Rock` sur 100% layer 4 (mappé Rock dans LayerPalette) | `Query_DominantLayerRock_ReturnsRock` |
| `engine_core_server` ne contient pas `SurfaceQueryService.cpp` | **Automatique** : `server_app` ne link pas `engine_core` (CMake déjà séparé). Test grep gardien : `grep -rn "SurfaceQueryService" engine/server/ \| wc -l` doit valoir 0. |
| Panneau Surface Table affiche les 13 entrées | Visuel WorldEditor (manuel, capture screenshot) |
| Vitesse ÷2 sur Snow vs Dirt | `client_prediction_surface_multiplier_tests::SetSurfaceSpeedMultiplier_0p5_HalvesSpeed` (validation unit ; playtest viendra avec M100.33) |

## Hors scope explicite

- ❌ Modifiers météo / saison (M100.26)
- ❌ Audio de pas (M100.33)
- ❌ Overrides hazards (M100.16)
- ❌ Calcul serveur (jamais)
- ❌ Refactoring engine_core/engine_core_server CMake (déjà séparé de fait)
- ❌ Wire ClientPrediction → SurfaceQueryService dans Engine.cpp (mover dormant ; viendra avec M100.33)
- ❌ Hot-reload du JSON en runtime
- ❌ Édition de la table via UI (édition par texte JSON externe)

## Déploiement

✅ **Client/éditeur uniquement, pas de redéploiement serveur.**
Aucun nouveau opcode, aucune migration DB, aucun fichier serveur modifié.
