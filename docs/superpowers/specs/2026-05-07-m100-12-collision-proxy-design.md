# M100.12 — Collision Proxy System — Design

> **Phase 3b — partie 2/2.** Suit M100.11 (mergé via PR #476). Couvre uniquement
> M100.12 ; PlacementOverlapInfo et hook auto-fit à l'import sont reportés
> (dépendances M100.17 / future asset import pipeline).

## Goal

Livrer la chaîne complète d'authoring de proxies de collision :

1. Struct `CollisionProxy` (3 niveaux : Capsule / ConvexHull / TriMesh).
2. Format binaire `*.collision.bin` versionné, round-trip byte-exact.
3. `AutoFit(CollisionMeshCpu) → CollisionProxy` heuristique single-pass.
4. `GenerateWireframeEdges(CollisionProxy) → vector<Edge3D>` pure function.
5. `CollisionEditorPanel` ImGui : open/save, switch type, sliders capsule, mini-preview 3D avec wireframe vert overlay.

## Architecture

### Périmètre M100.12 vs ticket original

| Élément ticket | Statut M100.12 |
|---|---|
| CollisionProxy 3 types + sérialisation `*.collision.bin` | ✅ Inclus (cœur livré) |
| AutoFitProxy heuristique | ✅ Inclus, fonction pure |
| CollisionEditorPanel ImGui | ✅ Inclus avec mini-preview 3D |
| Wireframe overlay vert | ✅ Inclus dans la preview du panel via ImGui DrawList |
| `View → Show Collision Proxies (V)` global toggle | ⏭ Stub no-op (ScenePanel placeholder ; viendra avec M100.34) |
| AssetBrowserPanel auto-fit à l'import | ❌ Out-of-scope (AssetBrowserPanel est un stub) |
| `PlacementOverlapInfo` compteur d'overlaps | ❌ Out-of-scope (M100.17 placement tool pas Done) |
| `EditorOverlayPass` Vulkan FrameGraph pass | ❌ Remplacé par ImGui DrawList in-panel |

### File structure

```
engine/world/collision/                            [nouveau]
├── CollisionProxy.h                               struct CollisionProxy + ProxyType enum + magic/version
├── CollisionProxy.cpp                             SaveToFile / LoadFromFile (binary serialize)
├── CollisionMeshCpu.h                             struct minimal { vertices, indices, isStatic }
├── AutoFitProxy.h                                 AutoFit(CollisionMeshCpu) → CollisionProxy
├── AutoFitProxy.cpp                               heuristique : aspect-ratio + vertex count
├── ProxyWireframe.h                               GenerateWireframeEdges(proxy) → vector<Edge3D>
├── ProxyWireframe.cpp                             arêtes capsule (cap rings + longitudinal) / hull (12 edges bbox) / trimesh (triangle edges)
└── tests/
    ├── CollisionProxyRoundtripTests.cpp           5 cas : save/load byte-exact + bad magic + bad hash
    ├── AutoFitProxyTests.cpp                      4 cas : tall→capsule, compact→hull, complex→trimesh, flat→hull
    └── ProxyWireframeTests.cpp                    4 cas : edge counts par type + non-empty

engine/editor/world/panels/
├── CollisionEditorPanel.h                         IPanel + Init(contentRoot)
└── CollisionEditorPanel.cpp                       form fields + mini-preview 3D + Save/Load

engine/editor/world/CollisionPreviewCamera.h/.cpp  [nouveau]
                                                   Mini orbit camera (drag mouse → rotate, wheel → zoom)
                                                   ComputeViewProj() pour projection 3D→2D

engine/editor/world/WorldEditorShell.cpp           [modif] instancie CollisionEditorPanel
CMakeLists.txt                                     [modif] sources + 3 test executables
```

### Boundaries

- **`engine/world/collision/`** : pure CPU, aucune dépendance Vulkan ni ImGui. Réutilisable côté serveur si nécessaire (mais pas lié aujourd'hui).
- **`engine/editor/world/panels/CollisionEditorPanel`** : dépend de `engine/world/collision/` + ImGui. Pas de Vulkan direct.
- **`CollisionPreviewCamera`** : math pur (Vec3, Mat4). Pas de dépendance render.
- **Aucun fichier ajouté à `engine/server/CMakeLists.txt`** (collision = client/éditeur seulement).
- **Aucune modification de FrameGraph** (preview = ImGui DrawList).
- **Aucune modification de GeometryPass** (le ticket exige "aucune branche `m_editorEnabled` dans GeometryPass" — respecté trivialement puisque rien ne touche GeometryPass).

### Lifecycle

- `CollisionEditorPanel` instancié par `WorldEditorShell::Init()` après `SurfaceTablePanel`. Hidden par défaut, toggle via View menu.
- `Init(contentRoot)` initialise le panel avec un `contentRoot` mais ne charge **pas** un proxy (panel vide jusqu'à clic "Open .collision.bin..." ou "New Capsule").

## Data structures & APIs

### `CollisionProxy.h`

```cpp
namespace engine::world::collision
{
    enum class ProxyType : uint32_t
    {
        Capsule    = 0,
        ConvexHull = 1,
        TriMesh    = 2,
    };

    /// Magic "COLL" little-endian. Format binaire `<asset>.collision.bin` (M100.12).
    constexpr uint32_t kCollisionMagic   = 0x4C4C4F43u;
    constexpr uint32_t kCollisionVersion = 1u;

    struct CollisionProxy
    {
        ProxyType type = ProxyType::Capsule;

        // Capsule (utilisé si type == Capsule)
        engine::math::Vec3 capsuleA{ 0.0f, -0.5f, 0.0f };
        engine::math::Vec3 capsuleB{ 0.0f,  0.5f, 0.0f };
        float              capsuleRadius = 0.5f;

        // ConvexHull / TriMesh (utilisés selon type)
        std::vector<engine::math::Vec3> vertices;
        std::vector<uint32_t>           indices;     // TriMesh seulement

        bool LoadFromFile(const std::filesystem::path& path, std::string& outError);
        bool SaveToFile(const std::filesystem::path& path, std::string& outError) const;
    };
}
```

### Layout binaire `.collision.bin`

```
Offset  Size      Content
[0..23]  24 B     OutputVersionHeader { magic=COLL (0x4C4C4F43u), formatVersion=1,
                  builderVersion=1, engineVersion=1, contentHash=xxhash64(payload post-header) }
[24..27]  4 B     uint32 proxyType (0=Capsule, 1=ConvexHull, 2=TriMesh)

If Capsule (type == 0):
  [28..39] 12 B   float a[3]
  [40..51] 12 B   float b[3]
  [52..55]  4 B   float radius
  Total payload size : 28 bytes après header (24+28 = 52 bytes total fichier)

If ConvexHull (type == 1):
  [28..31]  4 B   uint32 vertexCount
  [32..N]  varies float[3] vertices  (vertexCount * 12 bytes)

If TriMesh (type == 2):
  [28..31]  4 B   uint32 vertexCount
  [32..35]  4 B   uint32 indexCount
  [36..N]  varies float[3] vertices  (vertexCount * 12 bytes)
  [N+1..M] varies uint32[]  indices  (indexCount * 4 bytes)
```

`OutputVersionHeader` (24 bytes) est le pattern partagé du projet (cohérent avec `terrain.bin`, `splat.bin`). Le `contentHash` xxhash64 est calculé sur tout ce qui suit l'header, validé au Load.

### `CollisionMeshCpu.h`

```cpp
namespace engine::world::collision
{
    /// Représentation CPU minimale d'un mesh consommée par AutoFit.
    /// Pas de matériaux/UVs/normales — juste géométrie.
    /// Aucune dépendance loader (le caller fournit les données).
    struct CollisionMeshCpu
    {
        std::vector<engine::math::Vec3> vertices;
        std::vector<uint32_t>           indices;
        bool                            isStatic = false; // hint pour le dispatch
    };
}
```

### `AutoFitProxy.h`

```cpp
namespace engine::world::collision
{
    /// Choisit automatiquement un proxy à partir d'un mesh CPU :
    /// - Capsule si height/widthMax > 3 (mesh très vertical, ex. tronc d'arbre)
    /// - TriMesh si vertices.size() > 500 OU mesh.isStatic == true
    /// - ConvexHull (= bounding box 8 vertices) sinon
    ///
    /// Note M100.12 : ConvexHull est un placeholder bounding box. Un vrai
    /// quickhull viendra dans un follow-up si nécessaire pour le gameplay.
    /// Le ticket dit explicitement "single pass" — le dispatch lui-même est
    /// le pass unique.
    CollisionProxy AutoFit(const CollisionMeshCpu& mesh);
}
```

### Algorithme `AutoFit`

```cpp
CollisionProxy AutoFit(const CollisionMeshCpu& mesh)
{
    CollisionProxy out;

    // 1. Compute axis-aligned bounding box
    engine::math::Vec3 bmin{ +FLT_MAX, +FLT_MAX, +FLT_MAX };
    engine::math::Vec3 bmax{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (const auto& v : mesh.vertices)
    {
        bmin.x = std::min(bmin.x, v.x); bmax.x = std::max(bmax.x, v.x);
        bmin.y = std::min(bmin.y, v.y); bmax.y = std::max(bmax.y, v.y);
        bmin.z = std::min(bmin.z, v.z); bmax.z = std::max(bmax.z, v.z);
    }

    const float height   = bmax.y - bmin.y;
    const float widthX   = bmax.x - bmin.x;
    const float widthZ   = bmax.z - bmin.z;
    const float widthMax = std::max(widthX, widthZ);

    // 2. Dispatch
    if (mesh.isStatic || mesh.vertices.size() > 500u)
    {
        out.type = ProxyType::TriMesh;
        out.vertices = mesh.vertices;
        out.indices  = mesh.indices;
    }
    else if (widthMax > 0.0f && height / widthMax > 3.0f)
    {
        out.type = ProxyType::Capsule;
        const float r = widthMax * 0.5f;
        const engine::math::Vec3 center{
            (bmin.x + bmax.x) * 0.5f, 0.0f, (bmin.z + bmax.z) * 0.5f };
        out.capsuleA      = { center.x, bmin.y + r, center.z };
        out.capsuleB      = { center.x, bmax.y - r, center.z };
        out.capsuleRadius = r;
    }
    else
    {
        out.type = ProxyType::ConvexHull;
        // 8 vertices du bounding box (placeholder pour vrai quickhull)
        out.vertices = {
            {bmin.x, bmin.y, bmin.z}, {bmax.x, bmin.y, bmin.z},
            {bmin.x, bmax.y, bmin.z}, {bmax.x, bmax.y, bmin.z},
            {bmin.x, bmin.y, bmax.z}, {bmax.x, bmin.y, bmax.z},
            {bmin.x, bmax.y, bmax.z}, {bmax.x, bmax.y, bmax.z},
        };
    }
    return out;
}
```

### `ProxyWireframe.h`

```cpp
namespace engine::world::collision
{
    using Edge3D = std::pair<engine::math::Vec3, engine::math::Vec3>;

    /// Génère les arêtes 3D du wireframe d'un proxy. Pour :
    /// - Capsule : 2 cap rings (16 segments chacun) + 4 longitudinal lines = 36 edges
    /// - ConvexHull : 12 edges du bounding box (assume 8 vertices structuraux)
    /// - TriMesh : 3 edges par triangle (peut être beaucoup d'edges, sans dédup MVP)
    /// Pure function, aucune allocation Vulkan, aucune dépendance externe.
    std::vector<Edge3D> GenerateWireframeEdges(const CollisionProxy& proxy);
}
```

### `CollisionPreviewCamera.h`

```cpp
namespace engine::editor::world
{
    /// Mini-camera orbitale pour le preview 3D du CollisionEditorPanel.
    /// Drag souris (LMB) sur la zone preview → ajuste yaw/pitch.
    /// Wheel → zoom (distance à l'origine).
    class CollisionPreviewCamera
    {
    public:
        void HandleDrag(float deltaX, float deltaY) noexcept;
        void HandleZoom(float deltaWheel) noexcept;
        void Reset() noexcept;

        /// Calcule la matrice view*projection pour la zone preview de
        /// dimensions `viewportPx`. Output : NDC [-1, 1].
        engine::math::Mat4 ComputeViewProj(engine::math::Vec2 viewportPx) const;

        // Accesseurs lecture seule (pour HUD du panel : "Yaw: 40° / Pitch: 23°").
        float GetYawDegrees() const noexcept;
        float GetPitchDegrees() const noexcept;
        float GetDistance() const noexcept { return m_distance; }

    private:
        float m_yaw      = 0.7f;  // radians
        float m_pitch    = 0.4f;  // radians
        float m_distance = 3.0f;  // meters from origin
    };
}
```

## CollisionEditorPanel UI

### Layout

```
┌─ Collision Editor ──────────────────────────────────────────────────────┐
│ [Open .collision.bin...]  [New Capsule]  [New ConvexHull]  [New TriMesh] │
│ Source: <empty>  ou  C:/.../rock_large_01.collision.bin                  │
│                                                                          │
│ Type:  ( ) Capsule    (•) ConvexHull    ( ) TriMesh                      │
│                                                                          │
│ ── ConvexHull ──────────────────────────────────────────────────────── │
│ Vertex count: 8                                                          │
│ [Re-run AutoFit] (grayed: requires mesh CPU data — see M100.34)         │
│                                                                          │
│ ── Capsule (when Capsule type) ─────────────────────────────────────── │
│ A    [X: -0.000] [Y: -0.500] [Z:  0.000]  (sliders ±5 m)                │
│ B    [X: -0.000] [Y:  0.500] [Z:  0.000]  (sliders ±5 m)                │
│ Radius [0.500]                            (slider 0.05–2 m)              │
│                                                                          │
│ ── TriMesh (when TriMesh type) ─────────────────────────────────────── │
│ Vertex count: 1234   Tri count: 2456    (read-only)                      │
│                                                                          │
│ ── Preview (3D mini-viewport) ───────────────────────────────────────── │
│ ┌────────────────────────────────────────┐                               │
│ │                                        │  Test mesh: ▼ [Cube]         │
│ │  [wireframe vert du proxy + mesh test] │  [Reset Camera]               │
│ │  drag souris LMB pour orbiter          │                               │
│ │  molette pour zoom                     │  Yaw: 40°   Pitch: 23°       │
│ │     ~300 × 200 px                      │  Distance: 3.0 m              │
│ └────────────────────────────────────────┘                               │
│                                                                          │
│ [Save .collision.bin]                                                    │
│ Status: Saved ✓ — 3 frames ago / Parse error: ...                        │
└──────────────────────────────────────────────────────────────────────────┘
```

### Comportement

**Toolbar haut**
- `[Open .collision.bin...]` : `ImGui::InputText` simple (chemin relatif à `paths.content`) + bouton Load. Pas de native dialog en MVP.
- `[New Capsule/ConvexHull/TriMesh]` : crée un proxy par défaut (capsule 0.5/0.5/0.5 ; hull/trimesh empty avec status "vertices vides").

**Type radio buttons** : switch entre les 3 types, garde les données du type précédent (au cas où l'utilisateur revient).

**Sliders Capsule** : `ImGui::SliderFloat3("A", ..., -5.0f, 5.0f)`, `ImGui::SliderFloat3("B", ..., -5.0f, 5.0f)`, `ImGui::SliderFloat("Radius", ..., 0.05f, 2.0f)`. Edits immédiats, le wireframe se met à jour en temps réel dans la preview.

**ConvexHull / TriMesh** : read-only en M100.12. L'édition manuelle vertex-par-vertex est out-of-scope.

**Preview 3D**
- Zone `ImGui::BeginChild("##preview", ImVec2(300, 200), true)`.
- Mesh test sélectionnable via dropdown : Cube (synthétique 8 verts / 12 tris), Cylinder (16 segments × 2 caps = 96 tris), Sphere (icosphere 20 tris), Slab (4 verts / 2 tris).
- À chaque frame :
  1. Compute `viewProj = m_camera.ComputeViewProj(previewSize)`.
  2. Génère arêtes test mesh + arêtes proxy via `GenerateWireframeEdges`.
  3. Pour chaque edge `(a, b)` : projette `a, b` via `viewProj` → NDC `(x, y)`.
  4. NDC → pixel space dans la zone preview.
  5. `ImGui::GetWindowDrawList()->AddLine(p2dA, p2dB, color, 1.5f)` :
     - Test mesh en gris foncé `IM_COL32(140, 140, 140, 200)`.
     - Proxy wireframe en vert pétant `IM_COL32(80, 255, 80, 255)`.
- Drag souris LMB → `m_camera.HandleDrag(deltaX, deltaY)`.
- Wheel → `m_camera.HandleZoom(io.MouseWheel)`.
- `[Reset Camera]` → `m_camera.Reset()`.

**Save .collision.bin**
- Si `m_currentPath` vide → input text demande où sauvegarder.
- Sinon écrit au même path.
- Status vert "Saved ✓" / rouge "Save failed: ..." pour ~60 frames (≈1 s).

### API panel

```cpp
namespace engine::editor::world::panels
{
    class CollisionEditorPanel final : public engine::editor::world::IPanel
    {
    public:
        const char* GetName() const override { return "Collision Editor"; }
        void Render() override;
        bool IsVisible() const override { return m_visible; }
        void SetVisible(bool v) override { m_visible = v; }

        void Init(const std::filesystem::path& contentRoot);

    private:
        void RenderToolbar();
        void RenderTypeRadios();
        void RenderCapsuleFields();
        void RenderHullFields();
        void RenderTriMeshFields();
        void RenderPreview();
        void RenderSaveButton();

        bool m_visible = false;
        std::filesystem::path m_contentRoot;
        std::filesystem::path m_currentPath;
        engine::world::collision::CollisionProxy m_proxy;
        engine::editor::world::CollisionPreviewCamera m_camera;
        int m_testMeshIndex = 0;  // 0=Cube, 1=Cylinder, 2=Sphere, 3=Slab
        std::string m_status;
        int m_statusFramesLeft = 0;
        char m_pathInputBuf[260] = {};  // ImGui::InputText buffer
    };
}
```

## Tests TDD (3 suites, ~13 cas)

Framework REQUIRE maison, pattern M100.11. Pure CPU, pas de Vulkan, pas d'ImGui.

### `collision_proxy_roundtrip_tests` (5 cas)

| Test | Vérifie |
|---|---|
| `Test_Roundtrip_Capsule` | `SaveToFile(capsule)` puis `LoadFromFile()` → struct identique champ par champ (a, b, radius) |
| `Test_Roundtrip_ConvexHull` | Save 8 vertices, Load → vector<Vec3> identique (memcmp byte-exact sur 96 bytes) |
| `Test_Roundtrip_TriMesh` | Save vertices+indices (synthétique 12 tris d'un cube), Load → vertices et indices byte-exact |
| `Test_Load_BadMagic_Fails` | Fichier avec magic `0xDEADBEEF` → `LoadFromFile` retourne false, err mentionne "magic mismatch" |
| `Test_Load_BadContentHash_Fails` | Fichier avec payload corrompu (xxhash invalide) → fail avec err "contentHash mismatch" |

Pattern : écriture dans `temp_directory_path() / "test_proxy_*.bin"`, vérif, suppression.

### `auto_fit_proxy_tests` (4 cas)

```cpp
// Helpers : meshes synthétiques en C++ pur, pas de fichier
CollisionMeshCpu MakeTallCylinderMesh();   // height=4.0, radius=0.3 → ratio 6.7 → Capsule
CollisionMeshCpu MakeCompactCubeMesh();    // height=widthMax=1.0 → ratio 1.0 → ConvexHull
CollisionMeshCpu MakeStaticBuildingMesh(); // 800 vertices, isStatic=true → TriMesh
CollisionMeshCpu MakeFlatPlaneMesh();      // height=0.01, widthMax=10 → ratio 0.001 → ConvexHull
```

| Test | Vérifie |
|---|---|
| `Test_AutoFit_TallSlim_PicksCapsule` | `AutoFit(MakeTallCylinderMesh()).type == Capsule` ; capsule.a/b alignés sur Y axis ; radius ≈ 0.3 |
| `Test_AutoFit_Compact_PicksConvexHull` | `AutoFit(MakeCompactCubeMesh()).type == ConvexHull` ; vertices.size() == 8 (bounding box) |
| `Test_AutoFit_StaticComplex_PicksTriMesh` | `AutoFit(MakeStaticBuildingMesh()).type == TriMesh` ; vertices et indices = copie source |
| `Test_AutoFit_FlatPlane_PicksConvexHull` | Mesh très plat (ratio <<<1) → ConvexHull (pas Capsule, pas TriMesh) |

### `proxy_wireframe_tests` (4 cas)

| Test | Vérifie |
|---|---|
| `Test_Wireframe_Capsule_EdgeCount` | `GenerateWireframeEdges(capsule).size() == 36` (2 rings × 16 + 4 longitudinal) |
| `Test_Wireframe_ConvexHull_BoundingBox_12Edges` | Hull 8 vertices → 12 edges (cube bounding box) |
| `Test_Wireframe_TriMesh_3EdgesPerTriangle` | TriMesh 4 tris (12 indices) → 12 edges |
| `Test_Wireframe_EdgesNotEmpty` | Aucun proxy ne produit 0 arêtes (sentinel) |

### CMake tests

```cmake
foreach(suite IN ITEMS
    collision_proxy_roundtrip_tests
    auto_fit_proxy_tests
    proxy_wireframe_tests)
  if(WIN32)
    add_executable(${suite} engine/world/collision/tests/${suite}.cpp)
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

- **CollisionPreviewCamera** : math triviale (yaw/pitch/distance → matrix). Vérifié visuellement dans le panel.
- **CollisionEditorPanel** : ImGui UI, pas de surface programmable testable. Validation visuelle dans World Editor.

## Risques techniques

| Risque | Mitigation |
|---|---|
| **ConvexHull bounding-box placeholder** moins précis qu'un vrai hull pour meshes non-cubiques | Documenté explicitement comme placeholder. Follow-up ticket si gameplay le révèle problématique. Pour MVP les meshes typiques (rocher, prop simple) sont OK. |
| **Wireframe overlay sans depth test** (ImGui DrawList toujours par-dessus) = arêtes traversent murs | Acceptable pour debug/authoring. Documenté. Vraie passe Vulkan avec depth est follow-up. |
| **Round-trip byte-exact des floats** : risque NaN/denormal mal sérialisés | `std::memcpy` byte-exact (pattern terrain.bin). Tests vérifient `memcmp` après round-trip. |
| **`isStatic` flag de `CollisionMeshCpu` n'a pas de source réelle** (mesh import absent) | Acceptable : code testé avec les deux valeurs (synthetic mesh dans test). Câblage réel viendra avec import pipeline. |
| **`Open .collision.bin...` path input** : InputText simple ou native dialog ? | MVP = `ImGui::InputText` avec path relatif à `paths.content` + bouton Load. Native file dialog est follow-up. |
| **Test mesh dropdown synthétique** | Inline dans `CollisionEditorPanel.cpp` — 4 fonctions privées `MakeCubeMesh/MakeCylinderMesh/MakeSphereMesh/MakeSlabMesh`. ~50 lignes total. |

## Critères d'acceptation revus

| Critère ticket | Statut design |
|---|---|
| À l'import d'un mesh, `.collision.bin` créé automatiquement | ⏭ Out-of-scope (AssetBrowserPanel stub) |
| 3 types de proxies sérialisables avec round-trip parfait | ✅ Tests Roundtrip_Capsule/ConvexHull/TriMesh (memcmp byte-exact) |
| Panneau Collision Editor permet de basculer entre les 3 types | ✅ Radio buttons + sliders Capsule + read-only Hull/TriMesh |
| Overlay wireframe rendu uniquement quand WorldEditorShell actif | ✅ Le panel n'existe que dans WorldEditor binary |
| `GeometryPass` ne contient aucune branche `m_editorEnabled` | ✅ Trivial : aucune modif de GeometryPass |
| Placement de props ne refuse pas la superposition ; compteur incrémente | ⏭ Out-of-scope (`PlacementOverlapInfo` deferred M100.17) |
| Solveur runtime physique consomme les `.collision.bin` produits | ⏭ Out-of-scope (pas de solveur physique aujourd'hui) |

## Hors scope explicite

- ❌ V-HACD réel / quickhull / external lib (placeholder bounding box documenté)
- ❌ Vulkan render pass `EditorOverlayPass` — remplacé par ImGui DrawList
- ❌ Compound shapes (multi-proxy combinés)
- ❌ Soft body / cloth / fluid
- ❌ Mesh loader (.obj / .gltf)
- ❌ AssetBrowserPanel hook auto-fit à l'import
- ❌ PlacementOverlapInfo (dépend de M100.17)
- ❌ Native file dialog
- ❌ Édition vertex-par-vertex de hull/trimesh (read-only)

## Déploiement

✅ **Client/éditeur uniquement, pas de redéploiement serveur.** Le binaire serveur ne lie pas `engine_core` et ne touche jamais à `engine/world/collision/` ni `engine/editor/world/`. Aucun nouveau opcode, aucune migration DB.
