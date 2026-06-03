# CHAR-MODEL.1 — Format mesh skinné `.skinmesh` + builder offline glTF → `.skinmesh`

> **Etat : PARTIEL** (verifie 2026-06-03)
> - Fait / preuves : src/client/render/skinned/SkinnedMesh.h;src/client/render/skinned/SkinnedMeshLoader.h
> - Manque : builder offline/assets binaires absents
> - Resume : Format runtime ok, builder absent

## Dépendances
- *(racine de la chaîne CHAR-MODEL — aucune dépendance amont)*

## Cadrage

Définir et implémenter le **format binaire interne `.skinmesh`** utilisé en
runtime par le client pour les mesh squelettiques (joueurs, animaux, créatures),
**et** le builder offline `tools/skinmesh_builder/` qui convertit un fichier
glTF 2.0 en `.skinmesh`.

Ce ticket ne touche **ni** `AssetRegistry`, **ni** le pipeline GPU. Il pose
uniquement :
- la **spec binaire** du format,
- la **structure C++** côté runtime (`engine/render/SkinnedMesh.h`),
- l'**outil offline** `skinmesh_builder.exe` qui produit ces fichiers.

Le format est conçu pour le skinning LBS (≤ 4 influences par sommet, palette
de matrices) imposé par la phase 0.

---

## Pré-requis vérifiables

```bash
git status                                 # working tree propre
ls engine/render/AssetRegistry.h           # confirme l'API mesh existante
ls tools/zone_builder/CMakeLists.txt       # template pour outils offline
ls external/                               # vérifie présence cgltf ou tinygltf
```

Si ni `cgltf` ni `tinygltf` ne sont vendorisés dans `external/`, vendorise
`cgltf` (single-header, MIT) dans `external/cgltf/cgltf.h`. Pas d'utilisation
de `vcpkg.json` pour cette dépendance — règle existante du repo pour les
outils offline.

---

## Spécification technique

### Layout sommet runtime

```cpp
// engine/render/SkinnedMesh.h
namespace engine::render
{
    /// Layout d'un sommet de mesh skinné, identique CPU/GPU (40 octets, packé).
    struct SkinnedVertex
    {
        engine::math::Vec3 position;     // 12 B
        engine::math::Vec3 normal;       // 12 B   (normalisé)
        engine::math::Vec2 uv;           //  8 B
        uint8_t  jointIndices[4];        //  4 B   (indices d'os, 0..255)
        uint8_t  jointWeights[4];        //  4 B   (poids fixed-point /255)
    };
    static_assert(sizeof(SkinnedVertex) == 40);
}
```

Justifications :
- `jointIndices[4]` en `uint8_t` → max **256 os** par squelette (humanoid_v1
  ≈ 60 os, dragon ≈ 90, large marge).
- `jointWeights[4]` en `uint8_t` → 8 bits suffisent pour LBS placeholder ;
  somme **doit valoir 255 ± 1** après normalisation.
- Pas de tangent : Dérivé en shader si needed (économise 12 B/sommet).

### Spec binaire `.skinmesh`

Endianness : **little-endian**. Alignement : tous les blocs alignés sur 16 B.

```
Offset  Bytes  Champ
0       4      magic           = 'S','K','M','1'   (0x314D4B53)
4       4      version         = 1
8       4      vertexCount     uint32
12      4      indexCount      uint32
16      4      submeshCount    uint32   (≤ 16, un par matériau)
20      4      indexStride     uint32   (2 ou 4)
24      4      lodLevelCount   uint32   (1..4)
28      4      jointCountHint  uint32   (taille palette attendue)
32      24     localBoundsMin/Max (Vec3 ×2)
56      8      reserved (zéro)
64      ...    SkinnedVertex[vertexCount]            (40 B chacun)
...     ...    indices[indexCount]                   (2 ou 4 B chacun)
...     ...    Submesh[submeshCount]                 (32 B chacun, voir ci-dessous)
...     ...    LodEntry[lodLevelCount]               (16 B chacun)
EOF
```

Bloc `Submesh` (32 B) :

```cpp
struct SubmeshOnDisk
{
    uint32_t indexOffset;        // dans le buffer d'indices LOD0
    uint32_t indexCount;
    uint32_t materialSlot;       // 0..15, résolu par le manifest race
    uint8_t  name[20];           // ASCII, terminé par 0, ex. "head", "torso"
};
```

Bloc `LodEntry` (16 B) :

```cpp
struct LodEntryOnDisk
{
    uint32_t indexOffset;        // dans le buffer global d'indices
    uint32_t indexCount;
    float    screenSpaceErrorThreshold;   // ratio de couverture écran
    uint32_t reserved;
};
```

LOD 0 = pleine résolution. Tous les LODs **partagent** le même buffer de
sommets (subset d'indices). Cohérent avec la convention de `MeshAsset`
existante (M09.3).

### API runtime (header seulement à ce stade)

```cpp
// engine/render/SkinnedMesh.h
namespace engine::render
{
    constexpr uint32_t kSkinnedMeshMagic = 0x314D4B53; // 'SKM1'
    constexpr uint32_t kSkinnedMeshVersion = 1;
    constexpr uint32_t kMaxSubmeshesPerSkinnedMesh = 16;
    constexpr uint32_t kMaxJointsPerSkinnedMesh = 256;

    struct SkinnedMeshHeader { /* 64 B, identique au layout disque */ };
    struct SkinnedSubmesh    { /* miroir runtime de SubmeshOnDisk */ };
    struct SkinnedMeshLod    { /* miroir runtime de LodEntryOnDisk */ };

    /// Données chargées en RAM avant upload GPU. Pas de Vk* ici (CHAR-MODEL.4).
    struct SkinnedMeshCpuData
    {
        SkinnedMeshHeader              header;
        std::vector<SkinnedVertex>     vertices;
        std::vector<uint8_t>           indices;        // raw, indexStride en header
        std::vector<SkinnedSubmesh>    submeshes;
        std::vector<SkinnedMeshLod>    lods;
    };

    /// Charge un .skinmesh disque → CpuData. Retourne false sur magic/version invalide.
    /// Erreur loggée via engine::core::Log. Pas d'exception.
    bool LoadSkinnedMeshFromFile(std::string_view path, SkinnedMeshCpuData& out);
}
```

Implémentation : `engine/render/SkinnedMesh.cpp` — lecture binaire stricte,
validation des champs (`magic`, `version`, `vertexCount > 0`, `submeshCount
≤ 16`, somme des `lodIndexCount[i] ≤ indexCount`).

### Builder offline

Outil `tools/skinmesh_builder/skinmesh_builder.exe` (Windows + Linux).
Suit le pattern `tools/zone_builder/` :
- target CMake **autonome**,
- **ne lie pas** `engine_core`,
- ne lie que `engine_math` (header-only ou statique léger) si nécessaire,
- entrée : `--input model.gltf` (+ optionnel `--mesh-name root`),
- sortie : `--output model.skinmesh`,
- flags : `--lod 0:1.0,1:0.5,2:0.25,3:0.1` (seuils SSE par LOD),
  `--quantize-uv` (réservé pour plus tard, no-op),
  `--validate` (lit le fichier produit et vérifie les invariants).

Comportement :
1. Parse glTF via `cgltf` (lecture seule, MIT).
2. Sélectionne le premier mesh skinné (ou `--mesh-name`).
3. Vérifie : ≤ 4 influences par sommet (sinon erreur explicite avec index
   du sommet fautif).
4. Renormalise les poids en `uint8_t` (somme = 255).
5. Génère LODs **placeholder** pour ce ticket : LOD 0 = full, LOD 1..3 =
   copie de LOD 0 (le decimator vrai arrive plus tard ; on pose la
   structure). Émet un warning `[skinmesh] LOD decimation not implemented,
   LOD1..3 = LOD0`.
6. Écrit le fichier binaire.
7. Si `--validate` : relit, vérifie cohérence, exit 0/1.

Logs : un par étape, format `[skinmesh] <message>`. Exit code 0 = succès,
≠ 0 = échec avec message clair sur stderr.

---

## Liste des fichiers

**Créés :**
- `engine/render/SkinnedMesh.h`
- `engine/render/SkinnedMesh.cpp`
- `tools/skinmesh_builder/CMakeLists.txt`
- `tools/skinmesh_builder/main.cpp`
- `tools/skinmesh_builder/GltfSkinReader.{h,cpp}`
- `tools/skinmesh_builder/SkinMeshWriter.{h,cpp}`
- `external/cgltf/cgltf.h` *(si absent)*
- `tests/render/SkinnedMesh_LoadRoundtrip_test.cpp`

**Modifiés :**
- `CMakeLists.txt` racine (ajout du sous-projet `tools/skinmesh_builder`)
- `tools/CMakeLists.txt` *(si présent)*

**Aucune modification de :**
- `engine/render/AssetRegistry.{h,cpp}` (réservé à CHAR-MODEL.4)
- `engine/render/GeometryPass.{h,cpp}`
- shaders existants

---

## CMakeLists.txt

### `engine_core` (racine)

```cmake
target_sources(engine_core PRIVATE
    engine/render/SkinnedMesh.h
    engine/render/SkinnedMesh.cpp
)
```

### `tools/skinmesh_builder/CMakeLists.txt` (nouveau)

```cmake
add_executable(skinmesh_builder
    main.cpp
    GltfSkinReader.cpp
    SkinMeshWriter.cpp
)
target_include_directories(skinmesh_builder PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/external/cgltf
)
# IMPORTANT : ne lie PAS engine_core (outil offline, comme zone_builder).
target_link_libraries(skinmesh_builder PRIVATE engine_math)
set_target_properties(skinmesh_builder PROPERTIES CXX_STANDARD 20)
```

### Racine

```cmake
add_subdirectory(tools/skinmesh_builder)
```

Vérifier : pas d'inclusion de `engine/render/AssetRegistry.h` ni de
`<vulkan/vulkan.h>` dans le code de l'outil.

---

## Critères d'acceptation

- [ ] Build Windows (CMake + MSVC) — pas d'erreur, pas de warning C4xxx nouveau.
- [ ] Build Linux (CMake + clang) — idem.
- [ ] `skinmesh_builder.exe --input <gltf de test> --output out.skinmesh
      --validate` exit 0 sur un mesh humanoid de test.
- [ ] Le test `SkinnedMesh_LoadRoundtrip_test` :
      - écrit un `SkinnedMeshCpuData` synthétique (3 sommets, 1 triangle),
      - le sérialise via `SkinMeshWriter`,
      - le relit via `LoadSkinnedMeshFromFile`,
      - vérifie l'égalité bit-à-bit des champs.
- [ ] `LoadSkinnedMeshFromFile` rejette : magic faux, version != 1,
      vertexCount = 0, submeshCount > 16, indexStride ∉ {2, 4}.
- [ ] Aucun chemin absolu, aucun `\\` codé en dur, aucun `C:/`.
- [ ] Rapport final : fichiers créés, taille du binaire produit pour le
      mesh de test, sortie exécution `--validate`.

---

## Anti-objectifs

- **Ne pas** câbler `AssetRegistry` (réservé à CHAR-MODEL.4).
- **Ne pas** générer de buffers Vulkan (réservé à CHAR-MODEL.3 et 4).
- **Ne pas** implémenter le décimateur LOD (placeholder LOD1..3 = LOD0,
  ticket dédié plus tard si besoin).
- **Ne pas** charger glTF runtime — uniquement à l'offline-build.
- **Ne pas** modifier `MeshAsset` ni `MeshHandle` existants : les meshes
  statiques restent intacts.
- **Ne pas** introduire d'allocations dans le hot path runtime
  (`LoadSkinnedMeshFromFile` boot-time uniquement).
