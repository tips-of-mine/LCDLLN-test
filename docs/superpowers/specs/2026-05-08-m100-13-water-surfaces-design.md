# M100.13 — Water Surfaces (Lakes & Rivers) — Design

> **Phase 4a — partie 1/4.** Première PR de la Phase 4 (Hydrologie & Hazards).
> M100.14 (Water Render Pass), M100.15 (Water Surface Hook), M100.16 (Hazard
> Volume System) viendront en PRs séparées après celle-ci.

## Goal

Livrer la chaîne complète d'authoring des plans d'eau (lacs polygonaux +
rivières polyline) :

1. Structures `LakeInstance` / `RiverInstance` / `WaterScene`.
2. Format binaire `instances/water.bin` versionné, round-trip byte-exact.
3. `WaterMeshBuilder` : ear clipping (lacs) + ribbon polyline (rivières).
4. `WaterDocument` + `LakeTool` + `RiverTool` éditeur avec mini-canvas 2D top-down.
5. Helpers `StreamCache::LoadWater` (consommation client) + `WriteWater` (CLI batch).

## Architecture

### Périmètre M100.13 vs ticket original

| Élément ticket | Statut M100.13 |
|---|---|
| `LakeInstance` + `RiverInstance` + `WaterScene` structs | ✅ Inclus |
| Format binaire `instances/water.bin` versionné | ✅ Inclus (`OutputVersionHeader` + xxhash64) |
| Triangulation lac | ✅ Ear clipping (placeholder pour vrai Delaunay si nécessaire) |
| Ribbon mesh rivière | ✅ Polyline droite N nodes → 2(N-1) quads |
| `LakeTool` + `RiverTool` + `WaterDocument` | ✅ Inclus avec mini-canvas top-down 2D |
| `ToolPropertiesPanel::RenderLakeParams` + `RenderRiverParams` | ✅ Inclus |
| `StreamCache::LoadWater(zone)` | ✅ Inclus |
| `tools/zone_builder/lib WriteWater` | ✅ Inclus (cohérent M100.5/M100.9) |
| Inspector profondeur moyenne / surface totale | ✅ Affiché dans le panel (read-only computed) |
| Calcul flow_direction depuis pente terrain | ✅ Pure function `ComputeFlowDirections` |
| Click viewport pour ajouter point | ⏭ Remplacé par mini top-down canvas (ScenePanel placeholder) |

### File structure

```
engine/world/water/                                [nouveau]
├── WaterSurfaces.h                                struct LakeInstance/RiverInstance/RiverNode/WaterScene + magic/version
├── WaterSurfaces.cpp                              SaveWaterBin / LoadWaterBin via OutputVersionHeader
├── WaterMeshBuilder.h                             BuildLakeMesh / BuildRiverMesh / ComputeFlowDirections
├── WaterMeshBuilder.cpp                           ear clipping + ribbon polyline
└── tests/
    ├── WaterSurfacesTests.cpp                     6 cas : roundtrip x3, bad magic, bad hash, flow direction
    └── WaterMeshBuilderTests.cpp                  5 cas : convexe, pentagone, L-shape concave, river ribbon, error

engine/editor/world/                               [nouveaux fichiers]
├── WaterDocument.h                                WaterScene + dirty flag
├── WaterDocument.cpp                              SaveToDisk(cfg) + LoadFromDisk(cfg)
├── LakeTool.h                                     polygon en cours + commit via undo
├── LakeTool.cpp                                   AddPoint(xz), ClosePolygon(), Cancel()
├── RiverTool.h                                    nodes en cours + commit via undo
├── RiverTool.cpp                                  AddNode(xz, terrain), EndSpline(), Cancel()
├── AddLakeCommand.h/.cpp                          ICommand : push_back/pop_back lake
└── AddRiverCommand.h/.cpp                         ICommand : push_back/pop_back river

engine/editor/world/panels/ToolPropertiesPanel.cpp [modif]
                                                   + RenderLakeParams() / RenderRiverParams()
                                                   + helper RenderTopDownCanvas (mini-canvas 2D)

engine/editor/world/WorldEditorShell.h/.cpp        [modif]
                                                   + ActiveTool::Lake (4) / ActiveTool::River (5)
                                                   + raccourcis L (lake) / R (river)
                                                   + m_lakeTool, m_riverTool, m_waterDoc
                                                   + Init wiring + LoadFromDisk au boot

engine/world/StreamCache.h/.cpp                    [modif]
                                                   + LoadWater(cfg, zone) → shared_ptr<WaterScene>

tools/zone_builder/lib/                            [modif]
├── Public/zone_builder/ChunkPackageWriter.h       + WriteWater signature
└── ChunkPackageWriter.cpp                         + WriteWater impl (réutilise SaveWaterBin)

CMakeLists.txt                                     [modif] sources + 2 test executables
```

### Boundaries

- **`engine/world/water/`** : pure CPU, aucune dépendance Vulkan/ImGui.
  Réutilisable côté serveur si nécessaire (mais pas lié aujourd'hui).
- **`engine/editor/world/{LakeTool,RiverTool,WaterDocument}`** : dépend de
  `engine/world/water/` ; LakeTool n'a pas besoin de TerrainDocument
  (waterLevelY constant) ; RiverTool oui (SampleHeight pour fixer node.Y).
- **`engine/editor/world/panels/ToolPropertiesPanel.cpp`** : gagne deux nouvelles
  méthodes + helper canvas. Pas de nouveau panel.
- **Aucun fichier ajouté à `engine/server/CMakeLists.txt`**.
- **Aucune modification de FrameGraph ou GeometryPass** (rendu eau = M100.14).
- **Mesh d'eau** est CPU only en M100.13. Upload GPU + drawcall = M100.14.

### Lifecycle

- `WaterDocument` instancié dans `WorldEditorShell` (membre `m_waterDoc`).
- `LakeTool` / `RiverTool` membres `m_lakeTool` / `m_riverTool`.
- Init via `tool.Init(m_commandStack, m_waterDoc[, &m_terrainDoc])` dans
  `WorldEditorShell::Init` ; `m_waterDoc.LoadFromDisk(cfg, ignored)` au boot
  (no-op silencieux si fichier absent).
- `RenderLakeParams` / `RenderRiverParams` appelés depuis
  `ToolPropertiesPanel::Render` switch sur `GetActiveTool()`.
- `WaterDocument::SaveToDisk(cfg)` écrit
  `<paths.content>/instances/water.bin` (fichier global zone-level pour MVP).
- `StreamCache::LoadWater(cfg, zone)` lit le fichier au boot client (consommé
  par M100.14 et M100.15 plus tard ; en M100.13 livré + testé).

## Data structures & APIs

### `WaterSurfaces.h`

```cpp
namespace engine::world::water
{
    /// Magic du fichier `instances/water.bin` ("WATR" little-endian).
    constexpr uint32_t kWaterMagic   = 0x52544157u;
    constexpr uint32_t kWaterVersion = 1u;

    struct LakeInstance
    {
        std::string name;
        std::vector<engine::math::Vec3> polygon;          // CCW dans XZ ; Y = waterLevelY pour tous
        engine::math::Vec3 bottomColor{ 0.05f, 0.20f, 0.30f };
        float turbidity   = 0.4f;                          // 0..1
        float waterLevelY = 0.0f;
    };

    struct RiverNode
    {
        engine::math::Vec3 position;                       // Y = terrain height au moment du add
        float widthMeters = 4.0f;
        float depthMeters = 1.0f;
    };

    struct RiverInstance
    {
        std::string name;
        std::vector<RiverNode> nodes;                      // au moins 2 pour produire un mesh
    };

    struct WaterScene
    {
        std::vector<LakeInstance>  lakes;
        std::vector<RiverInstance> rivers;
    };

    /// Sérialise `scene` au format `water.bin` (M100.13). Header
    /// OutputVersionHeader (magic=kWaterMagic, version=1, contentHash=xxhash64
    /// du payload post-header) + payload structuré.
    bool SaveWaterBin(const WaterScene& scene,
                      std::vector<uint8_t>& outBytes,
                      std::string& outError);

    /// Désérialise un `water.bin`. Valide magic, version, contentHash.
    /// Reset `outScene.lakes` et `outScene.rivers` avant désérialisation.
    bool LoadWaterBin(std::span<const uint8_t> bytes,
                      WaterScene& outScene,
                      std::string& outError);
}
```

### Layout binaire `instances/water.bin`

```
Offset       Content
[0..23]      OutputVersionHeader (magic=WATR, version=1, builderVer=1, engineVer=1, hash)
[24..27]     uint32 lakeCount
[28..31]     uint32 riverCount

For each lake (lakeCount times):
  uint32 nameLen
  char[]  name
  uint32 vertexCount
  float[3] polygon[vertexCount]
  float[3] bottomColor
  float    turbidity
  float    waterLevelY

For each river (riverCount times):
  uint32 nameLen
  char[]  name
  uint32 nodeCount
  Per node (nodeCount times):
    float[3] position
    float    widthMeters
    float    depthMeters
```

### `WaterMeshBuilder.h`

```cpp
namespace engine::world::water
{
    struct WaterVertex
    {
        engine::math::Vec3 position;
    };

    struct WaterMeshCpu
    {
        std::vector<WaterVertex> vertices;
        std::vector<uint32_t>    indices;          // 3 par triangle
    };

    /// Triangulation du polygone d'un lac via ear clipping.
    /// Précondition : polygon a >= 3 vertices, simple (non auto-intersectant).
    /// Si CW, inverse l'ordre interne avant traitement (CCW imposé).
    /// Tous les vertices output ont Y = lake.waterLevelY (mesh plat).
    bool BuildLakeMesh(const LakeInstance& lake,
                       WaterMeshCpu& outMesh,
                       std::string& outError);

    /// Ribbon mesh d'une rivière. N nodes → N-1 segments → 2*(N-1) triangles
    /// (chaque segment = un quad = 2 triangles). 2*N vertices total (2 par node).
    /// Y de chaque vertex = node.position.y. Précondition : nodes.size() >= 2.
    bool BuildRiverMesh(const RiverInstance& river,
                        WaterMeshCpu& outMesh,
                        std::string& outError);

    /// Calcule les directions de flot par segment de rivière.
    /// flow[i] = normalize((node[i+1] - node[i]).xz), output dans XZ uniquement
    /// (Y du vecteur = 0). Précondition : river.nodes.size() >= 2.
    /// Output size = nodes.size() - 1.
    std::vector<engine::math::Vec3> ComputeFlowDirections(const RiverInstance& river);
}
```

### `WaterDocument.h`

```cpp
namespace engine::editor::world
{
    /// État des plans d'eau de la zone éditée (M100.13). Persiste dans
    /// `<paths.content>/instances/water.bin`. Un seul WaterScene par éditeur
    /// (chunk-level partitioning vient avec M100.34).
    class WaterDocument
    {
    public:
        engine::world::water::WaterScene&       Mutable()       { return m_scene; }
        const engine::world::water::WaterScene& Get()     const { return m_scene; }

        bool IsDirty() const noexcept { return m_dirty; }
        void MarkDirty() noexcept     { m_dirty = true; }

        /// Sauvegarde dans `<paths.content>/instances/water.bin`. Reset m_dirty.
        bool SaveToDisk(const engine::core::Config& cfg, std::string& outError);

        /// Charge depuis `<paths.content>/instances/water.bin`. Si fichier absent,
        /// retourne true avec scene vide (premier lancement). Reset m_dirty.
        bool LoadFromDisk(const engine::core::Config& cfg, std::string& outError);

    private:
        engine::world::water::WaterScene m_scene;
        bool m_dirty = false;
    };
}
```

### `LakeTool.h`

```cpp
namespace engine::editor::world
{
    class CommandStack;

    /// Outil d'édition d'un lac (M100.13). État : un polygone en cours de
    /// construction (pas encore committé) + référence au document partagé.
    /// Workflow : AddPoint(xz) répété → ClosePolygon() commit le lac dans
    /// le document via une commande undoable. Cancel() abandonne sans commit.
    class LakeTool
    {
    public:
        bool Init(CommandStack& stack, WaterDocument& waterDoc) noexcept;

        /// Ajoute un point au polygone en cours. Y = m_currentWaterLevelY.
        void AddPoint(float worldX, float worldZ);

        /// Ferme le polygone et commit comme nouveau lac via une commande
        /// sur la pile undo. No-op si < 3 points.
        void ClosePolygon();

        /// Abandonne le polygone en cours (vide la liste de points).
        void Cancel() noexcept;

        // Accesseurs UI
        bool   HasActivePolygon() const noexcept { return !m_currentPoints.empty(); }
        size_t GetPointCount()    const noexcept { return m_currentPoints.size(); }
        const std::vector<engine::math::Vec3>& GetCurrentPoints() const { return m_currentPoints; }

        float& MutableWaterLevelY() noexcept { return m_currentWaterLevelY; }
        engine::math::Vec3& MutableBottomColor() noexcept { return m_currentBottomColor; }
        float& MutableTurbidity() noexcept { return m_currentTurbidity; }

    private:
        CommandStack*  m_stack = nullptr;
        WaterDocument* m_doc   = nullptr;
        std::vector<engine::math::Vec3> m_currentPoints;
        float m_currentWaterLevelY = 0.0f;
        engine::math::Vec3 m_currentBottomColor{ 0.05f, 0.20f, 0.30f };
        float m_currentTurbidity = 0.4f;
    };
}
```

### `RiverTool.h`

```cpp
namespace engine::editor::world
{
    class TerrainDocument;

    /// Outil d'édition d'une rivière (M100.13). État : nodes en cours de
    /// construction. AddNode(xz) sample la heightmap via TerrainDocument
    /// pour fixer Y. EndSpline() commit comme nouveau river via undo.
    class RiverTool
    {
    public:
        bool Init(CommandStack& stack, WaterDocument& waterDoc, TerrainDocument& terrainDoc) noexcept;

        /// Ajoute un node à la rivière en cours. Y = TerrainDocument::SampleHeight(xz).
        /// Si chunk pas chargé → fallback Y=0.0.
        void AddNode(float worldX, float worldZ);

        /// Termine la spline et commit via undo. No-op si < 2 nodes.
        void EndSpline();

        void Cancel() noexcept;

        bool   HasActiveRiver() const noexcept { return !m_currentNodes.empty(); }
        size_t GetNodeCount()   const noexcept { return m_currentNodes.size(); }
        const std::vector<engine::world::water::RiverNode>& GetCurrentNodes() const { return m_currentNodes; }

        float& MutableDefaultWidth() noexcept { return m_defaultWidth; }
        float& MutableDefaultDepth() noexcept { return m_defaultDepth; }

    private:
        CommandStack*    m_stack       = nullptr;
        WaterDocument*   m_doc         = nullptr;
        TerrainDocument* m_terrainDoc  = nullptr;
        std::vector<engine::world::water::RiverNode> m_currentNodes;
        float m_defaultWidth = 4.0f;
        float m_defaultDepth = 1.0f;
    };
}
```

### Commandes undoable

```cpp
namespace engine::editor::world
{
    class AddLakeCommand : public ICommand
    {
    public:
        AddLakeCommand(WaterDocument& doc, engine::world::water::LakeInstance lake);
        void Apply()  override;  // doc.scene.lakes.push_back(m_lake) + doc.MarkDirty()
        void Revert() override;  // doc.scene.lakes.pop_back() + doc.MarkDirty()
        size_t SizeBytes() const override;
        const char* GetLabel() const override { return "Add Lake"; }
    private:
        WaterDocument* m_doc;
        engine::world::water::LakeInstance m_lake;
    };

    class AddRiverCommand : public ICommand
    {
    public:
        AddRiverCommand(WaterDocument& doc, engine::world::water::RiverInstance river);
        void Apply()  override;
        void Revert() override;
        size_t SizeBytes() const override;
        const char* GetLabel() const override { return "Add River"; }
    private:
        WaterDocument* m_doc;
        engine::world::water::RiverInstance m_river;
    };
}
```

### Algorithme ear clipping (résumé)

```cpp
bool BuildLakeMesh(const LakeInstance& lake, WaterMeshCpu& out, std::string& err)
{
    if (lake.polygon.size() < 3) { err = "polygon needs >= 3 vertices"; return false; }

    // 0. Forcer CCW (orientation positive). Si CW, copie inversée.
    std::vector<engine::math::Vec3> poly = lake.polygon;
    if (SignedArea2D(poly) < 0.0f)
        std::reverse(poly.begin(), poly.end());

    // 1. Indices restants à découper
    std::vector<uint32_t> remaining(poly.size());
    std::iota(remaining.begin(), remaining.end(), 0u);

    // 2. Tant que > 3 sommets restants, trouve un "ear" puis découpe
    while (remaining.size() > 3)
    {
        bool foundEar = false;
        for (size_t i = 0; i < remaining.size(); ++i)
        {
            const uint32_t prev = remaining[(i + remaining.size() - 1) % remaining.size()];
            const uint32_t curr = remaining[i];
            const uint32_t next = remaining[(i + 1) % remaining.size()];
            if (!IsConvexCorner(poly[prev], poly[curr], poly[next])) continue;
            if (TriangleContainsAnyOther(remaining, i, poly)) continue;
            out.indices.push_back(prev);
            out.indices.push_back(curr);
            out.indices.push_back(next);
            remaining.erase(remaining.begin() + i);
            foundEar = true;
            break;
        }
        if (!foundEar)
        {
            err = "polygon ear-clipping failed (auto-intersecting?)";
            return false;
        }
    }
    out.indices.push_back(remaining[0]);
    out.indices.push_back(remaining[1]);
    out.indices.push_back(remaining[2]);

    // Vertices : positions XZ du polygone, Y = waterLevelY pour tous
    out.vertices.reserve(poly.size());
    for (const auto& p : poly)
        out.vertices.push_back({ engine::math::Vec3{ p.x, lake.waterLevelY, p.z } });

    return true;
}
```

### Algorithme ribbon (résumé)

```cpp
bool BuildRiverMesh(const RiverInstance& river, WaterMeshCpu& out, std::string& err)
{
    if (river.nodes.size() < 2) { err = "river needs >= 2 nodes"; return false; }

    out.vertices.reserve(2 * river.nodes.size());
    out.indices.reserve(6 * (river.nodes.size() - 1));

    // Pour chaque node, calcule la perpendiculaire au segment local
    for (size_t i = 0; i < river.nodes.size(); ++i)
    {
        engine::math::Vec3 tangent;
        if (i == 0)
            tangent = (river.nodes[1].position - river.nodes[0].position);
        else if (i == river.nodes.size() - 1)
            tangent = (river.nodes[i].position - river.nodes[i - 1].position);
        else
            tangent = (river.nodes[i + 1].position - river.nodes[i - 1].position);

        // Perpendiculaire dans XZ : (-tz, 0, tx) normalisé
        const float tlen = std::sqrt(tangent.x * tangent.x + tangent.z * tangent.z);
        const float perpX = (tlen > 0.0f) ? (-tangent.z / tlen) : 1.0f;
        const float perpZ = (tlen > 0.0f) ? ( tangent.x / tlen) : 0.0f;
        const float halfW = river.nodes[i].widthMeters * 0.5f;

        const auto& n = river.nodes[i];
        out.vertices.push_back({ engine::math::Vec3{ n.position.x + perpX * halfW, n.position.y, n.position.z + perpZ * halfW } });
        out.vertices.push_back({ engine::math::Vec3{ n.position.x - perpX * halfW, n.position.y, n.position.z - perpZ * halfW } });
    }

    // Quads entre nodes consécutifs
    for (uint32_t i = 0; i + 1 < river.nodes.size(); ++i)
    {
        const uint32_t a = i * 2 + 0;        // current right
        const uint32_t b = i * 2 + 1;        // current left
        const uint32_t c = (i + 1) * 2 + 0;  // next right
        const uint32_t d = (i + 1) * 2 + 1;  // next left
        // Quad ABDC → 2 triangles
        out.indices.push_back(a); out.indices.push_back(c); out.indices.push_back(d);
        out.indices.push_back(a); out.indices.push_back(d); out.indices.push_back(b);
    }
    return true;
}
```

## ToolPropertiesPanel UI + mini canvas 2D

### Pattern existant (M100.6/7/10)

`ToolPropertiesPanel::Render` switch sur `WorldEditorShell::GetActiveTool()` :

```cpp
case ActiveTool::TerrainSculpt: RenderSculptParams(shell.MutableSculptTool()); break;
case ActiveTool::TerrainStamp:  RenderStampParams(shell.MutableStampTool());   break;
case ActiveTool::SplatPaint:    RenderSplatPaintParams(shell, shell.MutableSplatPaintTool()); break;
case ActiveTool::Lake:          RenderLakeParams(shell, shell.MutableLakeTool());   break;  // NEW
case ActiveTool::River:         RenderRiverParams(shell, shell.MutableRiverTool()); break;  // NEW
```

### Layout `RenderLakeParams`

```
Default values for next lake :
  Water Level Y  [0.000]            (slider -50 .. +50)
  Bottom Color   ■■■                (ColorEdit3)
  Turbidity      [0.40]             (slider 0..1)

Current polygon : 5 points
[Close polygon (commit lake)]   [Cancel]

── 2D Top-Down Canvas ──────────────────────────────
Bounds: ±[50.0] m                  [Recenter on origin]
┌──────────────────────────────────────┐
│  +Z                                  │
│       •──•                           │  Existing lakes :
│       │   \                          │   - lake_main (8 pts)
│       •    •                         │   - lake_pond (5 pts)
│            │                         │  Existing rivers :
│       •────•                         │   (none)
│  -X       +X                         │
│            -Z                        │
└──────────────────────────────────────┘
Click LMB → ajoute point au polygone en cours
Click RMB → cancel polygone en cours
Hover → tooltip "world: (X.X, Z.X)"

Existing lakes table :
┌────────────┬───────┬──────────┬────────┐
│ Name       │ Pts   │ Y-level  │ ×      │
│ lake_main  │ 8     │ 12.5     │ [Del]  │
│ lake_pond  │ 5     │ 5.0      │ [Del]  │
└────────────┴───────┴──────────┴────────┘
```

### Layout `RenderRiverParams`

Identique pattern mais avec :
- Default `widthMeters` (slider 0.5..30) + `depthMeters` (slider 0.1..10)
- Pas de `WaterLevelY` (Y vient de la heightmap automatique)
- Bouton `End spline (commit river)` au lieu de `Close polygon`
- Le canvas affiche les nodes en cours comme polyline droite cyan

### Mini canvas 2D — helper

```cpp
struct CanvasState
{
    float boundsHalfMeters = 50.0f;          // édité par slider du panel
    engine::math::Vec2 centerWorldXZ{ 0.0f, 0.0f };
};

struct CanvasInput
{
    bool   leftClicked  = false;
    bool   rightClicked = false;
    float  worldX = 0.0f;
    float  worldZ = 0.0f;
};

CanvasInput RenderTopDownCanvas(
    const CanvasState&            canvasState,
    const WaterScene&             existingScene,    // affiche existing en gris
    const std::vector<Vec3>*      currentPolygon,   // jaune (lake en cours)
    const std::vector<RiverNode>* currentNodes);    // cyan (river en cours)
```

### Mapping écran ↔ monde

Linear mapping :
- `worldX = centerWorldXZ.x + (px / W * 2 - 1) * boundsHalfMeters`
- `worldZ = centerWorldXZ.z - (py / H * 2 - 1) * boundsHalfMeters`

(Top-left écran = `-X` / `+Z`. Bottom-right écran = `+X` / `-Z`.)

### Couleurs via `ImDrawList::AddLine` / `AddCircleFilled`

- Existing lakes : gris `IM_COL32(180, 180, 180, 200)`, polygone fermé + cercles 3px
- Existing rivers : gris foncé `IM_COL32(140, 140, 180, 200)` polyline + nodes
- Current polygon (lake en cours) : jaune `IM_COL32(255, 220, 80, 255)`
- Current nodes (river en cours) : cyan `IM_COL32(80, 220, 255, 255)`
- Croix au centre (origine monde) : blanc `IM_COL32(255, 255, 255, 100)`

### `WorldEditorShell` modifs

```cpp
enum class ActiveTool : uint8_t {
    None          = 0,
    TerrainSculpt = 1,
    TerrainStamp  = 2,
    SplatPaint    = 3,
    Lake          = 4,  // NEW (raccourci L)
    River         = 5,  // NEW (raccourci R)
};
```

`HandleShortcut` ajoute :
- `'L'` → `SetActiveTool(ActiveTool::Lake)`
- `'R'` → `SetActiveTool(ActiveTool::River)`

`Init` ajoute :
```cpp
m_lakeTool.Init(m_commandStack, m_waterDoc);
m_riverTool.Init(m_commandStack, m_waterDoc, m_terrainDoc);
std::string ignored;
m_waterDoc.LoadFromDisk(cfg, ignored);
```

## Tests TDD (2 suites, 11 cas)

Framework REQUIRE maison, pattern M100.11/M100.12. Pure CPU.

### `water_surfaces_tests` (6 cas)

| Test | Vérifie |
|---|---|
| `Test_Roundtrip_LakeOnly` | Save 1 lake (5 vertices, name, color, etc.) ; Load ; champs identiques |
| `Test_Roundtrip_RiverOnly` | Save 1 river (4 nodes) ; Load ; nodeCount + node fields byte-exact |
| `Test_Roundtrip_LakeAndRiver` | Scene mixte (2 lakes + 1 river) ; round-trip identique |
| `Test_Load_BadMagic_Fails` | Magic 0xDEADBEEFu → false, err contient "magic" |
| `Test_Load_BadContentHash_Fails` | Save valide, flip 1 byte payload, reload → false, err contient "contentHash" |
| `Test_FlowDirection_AlignsWithSlope` | River 3 nodes (descendant) → ComputeFlowDirections retourne 2 vecteurs alignés avec amont→aval |

### `water_mesh_builder_tests` (5 cas)

| Test | Vérifie |
|---|---|
| `Test_BuildLakeMesh_ConvexQuad_Produces2Triangles` | Polygone carré CCW → indices.size() == 6, vertices.size() == 4, all Y == waterLevelY |
| `Test_BuildLakeMesh_ConvexPentagon_Produces3Triangles` | Pentagone régulier 5 vertices → indices.size() == 9 |
| `Test_BuildLakeMesh_ConcaveLShape_ProducesCorrectTris` | Polygone L 6 vertices (concave) → indices.size() == 12 (4 triangles), tous dans le polygone |
| `Test_BuildRiverMesh_4Nodes_Produces6Quads` | River 4 nodes → vertices.size() == 8, indices.size() == 18 (3 segments × 2 tris × 3) |
| `Test_BuildLakeMesh_TooFewVertices_Fails` | Polygone 2 vertices → false, err mentionne ">= 3" |

### CMake tests

```cmake
foreach(suite IN ITEMS water_surfaces_tests water_mesh_builder_tests)
  if(WIN32)
    add_executable(${suite} engine/world/water/tests/${suite}.cpp)
    target_include_directories(${suite} PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(${suite} PRIVATE engine_core)
    if(MSVC)
      target_compile_options(${suite} PRIVATE /W4 /permissive- /Zc:preprocessor)
    endif()
    add_test(NAME ${suite} COMMAND ${suite})
  endif()
endforeach()
```

### Pas de tests pour

- **WaterDocument** : wrapper trivial sur SaveWaterBin/LoadWaterBin (déjà testé).
- **LakeTool / RiverTool** : state container avec AddPoint/ClosePolygon trivial. Validation visuelle.
- **ToolPropertiesPanel UI** : ImGui code, validation visuelle.
- **Canvas 2D mapping** : math triviale, validation visuelle.
- **AddLakeCommand / AddRiverCommand** : pattern M100.6 déjà testé via TerrainSculptCommand.

## Risques techniques

| Risque | Mitigation |
|---|---|
| Ear clipping infinite loop sur polygone auto-intersectant | Détection : si pass complet sans trouver d'oreille → return false avec err "polygon ear-clipping failed (auto-intersecting?)" |
| Polygone CCW vs CW | Helper `EnsureCcw` : calcule l'aire signée 2D, inverse l'ordre si négative. Appelé en début de BuildLakeMesh |
| Mesh d'eau plat traverse le terrain | Acceptable : depth test du rendu (M100.14) gérera ça |
| River node Y vs terrain.SampleHeight | Si chunk pas chargé → fallback Y=0.0 dans RiverTool::AddNode |
| Canvas 2D bounds limits visibility | Bounds par défaut ±50m, slider jusqu'à ±500m, Recenter button |
| WaterScene global vs zone | Un seul `instances/water.bin` global pour MVP. Partitioning chunk-level avec M100.34 |
| TerrainDocument couplage RiverTool | Volontaire : la rivière "suit" le terrain. LakeTool ne dépend que de WaterDocument |
| AddLakeCommand stocke copie complète LakeInstance | ~600 bytes/lake max, sous le budget undo |

## Critères d'acceptation revus

| Critère ticket | Statut design |
|---|---|
| Lac fermé (polygone CCW) produit mesh triangulaire couvrant la zone | ✅ Tests `Test_BuildLakeMesh_*` (3 cas) + `EnsureCcw` |
| Rivière 4 nodes → ribbon 6 quads suivant la heightmap | ✅ Test `Test_BuildRiverMesh_4Nodes_Produces6Quads`. Y de chaque node fixé via TerrainDocument::SampleHeight au AddNode |
| Sérialisation `water.bin` round-trip parfaite | ✅ 3 tests `Test_Roundtrip_*` (memcmp + ApproxEq) |
| Client lit le format via `StreamCache` sans branche éditeur | ✅ `StreamCache::LoadWater(cfg, zone)` ajouté |
| Sens d'écoulement cohérent (amont → aval) | ✅ `Test_FlowDirection_AlignsWithSlope` |

## Hors scope explicite

- ❌ Vrai Delaunay 2D (placeholder ear clipping documenté)
- ❌ Spline Catmull-Rom rivière (polyline droite)
- ❌ Click dans le viewport 3D (mini canvas 2D top-down dans le panel)
- ❌ Chunk-level partitioning du water.bin (un seul fichier global)
- ❌ Pan/zoom souris du canvas 2D (slider bounds + Recenter)
- ❌ Édition par sélection d'un point existant (ajout + suppression seulement)
- ❌ Cascades / waterfalls (futur ticket)
- ❌ Rendu visuel des water surfaces (M100.14)
- ❌ Hook gameplay nage (M100.15)
- ❌ Hot reload du water.bin runtime
- ❌ Multi-zone water (un fichier par zone éditée seulement)

## Déploiement

✅ **Client/éditeur uniquement, pas de redéploiement serveur.** Le binaire serveur ne lie pas `engine_core`. Aucun nouveau opcode, aucune migration DB. Le format `instances/water.bin` est un asset client-side.
