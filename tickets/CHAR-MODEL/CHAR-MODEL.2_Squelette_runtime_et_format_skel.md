# CHAR-MODEL.2 — Squelette runtime `Skeleton` + format `.skel` + builder

## Dépendances
- CHAR-MODEL.1 (livré : format `.skinmesh`, conventions binaires partagées)

## Cadrage

Définir le **squelette runtime** : structure C++ `Skeleton`, format binaire
`.skel`, et builder offline `tools/skeleton_builder/` (glTF → `.skel`).

Le squelette est **le contrat** entre :
- le `.skinmesh` (qui référence des indices d'os 0..N-1),
- les clips d'animation `.anim` (CHAR-MODEL.6),
- le système de sockets (CHAR-MODEL.12).

Aucun upload GPU dans ce ticket : la palette de matrices est calculée par
le sampler et uploadée par le pipeline de rendu (CHAR-MODEL.3).

---

## Pré-requis vérifiables

```bash
git status
ls engine/render/SkinnedMesh.h           # CHAR-MODEL.1 livré
ls tools/skinmesh_builder/CMakeLists.txt
ls external/cgltf/cgltf.h                # vendorisé en CHAR-MODEL.1
```

---

## Spécification technique

### Conventions

- Espace : **Y-up, main droite** (cohérent avec le moteur existant).
- Unités : **mètres** pour les translations, **radians** pour l'orientation
  (stockée en quaternion `Vec4 = {x, y, z, w}`).
- Pose **bind pose** = pose de référence dans laquelle la mesh est skinnée.
- Pose **rest pose** = pose initiale du runtime (souvent ≡ bind pose).
- Index parent : `int16_t`, **-1** = racine. **Un seul parent par os**.
  Os toujours stockés **dans l'ordre topologique** (parent < enfant).
- Limites : ≤ 256 os par squelette (cohérent avec `jointIndices : uint8`).

### Structure runtime

```cpp
// engine/render/Skeleton.h
namespace engine::render
{
    constexpr uint32_t kSkeletonMagic   = 0x314C4B53; // 'SKL1'
    constexpr uint32_t kSkeletonVersion = 1;
    constexpr uint16_t kInvalidJoint    = 0xFFFF;
    constexpr uint32_t kMaxJointsPerSkeleton = 256;
    constexpr uint32_t kMaxJointNameLen = 31;          // + 0 final = 32 B

    struct JointLocalTransform   // 32 B, identique CPU/GPU
    {
        engine::math::Vec3 translation;   // mètres, par rapport au parent
        engine::math::Vec4 rotation;      // quaternion (x,y,z,w), normalisé
        engine::math::Vec3 scale;         // 1,1,1 par défaut
    };

    struct Joint
    {
        char     name[32];                // ASCII, terminé par 0
        int16_t  parentIndex;             // -1 = racine
        uint16_t flags;                   // bit 0: animatedThisRig
        JointLocalTransform bindLocal;    // pose de bind, espace local
        engine::math::Mat4  inverseBindMatrix; // model-space inverse, pré-calculé
    };

    struct Skeleton
    {
        std::string         debugName;    // ex. "humanoid_v1"
        std::vector<Joint>  joints;       // ordre topologique
        // index par nom, construit après load
        std::unordered_map<std::string, uint16_t> jointByName;

        uint16_t FindJoint(std::string_view name) const; // kInvalidJoint si absent
        bool     IsAncestor(uint16_t ancestor, uint16_t descendant) const;
    };
}
```

### Spec binaire `.skel`

```
Offset  Bytes  Champ
0       4      magic         = 'S','K','L','1'  (0x314C4B53)
4       4      version       = 1
8       4      jointCount    uint32  (≤ 256)
12      4      flags         uint32  (réservé, 0)
16      32     debugName     ASCII NUL-terminé, padding 0
48      ...    JointOnDisk[jointCount]   (96 B chacun)
EOF
```

`JointOnDisk` (96 B) :

```cpp
struct JointOnDisk
{
    char     name[32];                  // 32 B
    int16_t  parentIndex;               //  2 B
    uint16_t flags;                     //  2 B
    JointLocalTransform bindLocal;      // 32 B
    engine::math::Mat4  inverseBindM;   // 64 B (4×4 floats)
};
static_assert(sizeof(JointOnDisk) == 132); // attention : 32+2+2+32+64 = 132
```

> Note : si l'alignement à 16 B est souhaité, padder à 144 B avec
> `uint8_t reserved[12]`. **Choix retenu** : padder à 144 B, plus sûr
> pour évolution. Header devient 48 B + jointCount × 144 B.

### Builder offline

`tools/skeleton_builder/skeleton_builder.exe` :
- Pattern identique à `skinmesh_builder` : autonome, **ne lie pas
  `engine_core`**, lie `engine_math`, dépend de `cgltf`.
- Entrée : `--input model.gltf`.
- Sortie : `--output rig.skel`.
- Flags : `--name humanoid_v1`, `--strip-unused` (retire les os non
  référencés par la skin), `--validate`.

Comportement :
1. Parse glTF, prend le **premier skin**.
2. Vérifie : ≤ 256 os, noms ASCII uniques, longueur ≤ 31.
3. Trie les os en **ordre topologique** (parent avant enfant). Si glTF
   les fournit déjà ordonnés, conserver l'ordre.
4. Calcule `inverseBindMatrix` à partir du `inverseBindMatrices`
   accessor du glTF (sinon le recompose depuis la bind pose).
5. Sérialise.
6. `--validate` : relit, vérifie chaîne parentale (chaque parentIndex
   < indexEnfant), pas de cycle, racine unique en index 0 (ou warning
   si plusieurs racines).

### Cohérence inter-asset

- L'index d'os référencé par `SkinnedVertex.jointIndices[i]` doit être <
  `Skeleton.joints.size()`.
- La validation cross-asset n'est **pas** dans ce ticket — elle viendra
  au câblage `AssetRegistry` (CHAR-MODEL.4).

### API utilitaires

```cpp
// engine/render/Skeleton.h
namespace engine::render
{
    bool LoadSkeletonFromFile(std::string_view path, Skeleton& out);

    /// Calcule la palette des transformations model-space pour une pose locale donnée.
    /// outModelMatrices.size() == skeleton.joints.size().
    /// Hot-path : appelé chaque frame par le sampler.
    void ComputeModelMatrices(
        const Skeleton& skeleton,
        std::span<const JointLocalTransform> localPose,
        std::span<engine::math::Mat4> outModelMatrices);

    /// Multiplie modelMatrices par inverseBindMatrix → palette finale skin.
    void ComputeSkinPalette(
        const Skeleton& skeleton,
        std::span<const engine::math::Mat4> modelMatrices,
        std::span<engine::math::Mat4> outSkinPalette);
}
```

`ComputeModelMatrices` exploite l'ordre topologique : un seul passage,
`model[i] = model[parent[i]] * local[i]` ou `local[i]` si racine.

---

## Liste des fichiers

**Créés :**
- `engine/render/Skeleton.h`
- `engine/render/Skeleton.cpp`
- `tools/skeleton_builder/CMakeLists.txt`
- `tools/skeleton_builder/main.cpp`
- `tools/skeleton_builder/GltfSkeletonReader.{h,cpp}`
- `tools/skeleton_builder/SkeletonWriter.{h,cpp}`
- `tests/render/Skeleton_Roundtrip_test.cpp`
- `tests/render/Skeleton_ComputeModelMatrices_test.cpp`

**Modifiés :**
- `CMakeLists.txt` racine (`add_subdirectory(tools/skeleton_builder)`)

---

## CMakeLists.txt

### `engine_core`

```cmake
target_sources(engine_core PRIVATE
    engine/render/Skeleton.h
    engine/render/Skeleton.cpp
)
```

### `tools/skeleton_builder/CMakeLists.txt`

```cmake
add_executable(skeleton_builder
    main.cpp
    GltfSkeletonReader.cpp
    SkeletonWriter.cpp
)
target_include_directories(skeleton_builder PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/external/cgltf
)
target_link_libraries(skeleton_builder PRIVATE engine_math)
set_target_properties(skeleton_builder PROPERTIES CXX_STANDARD 20)
```

---

## Critères d'acceptation

- [ ] Build Windows + Linux propre.
- [ ] `skeleton_builder --input human.gltf --output humanoid_v1.skel
      --name humanoid_v1 --validate` exit 0.
- [ ] Test `Skeleton_Roundtrip_test` : sérialisation/désérialisation
      d'un squelette synthétique (3 os : root → spine → head), égalité
      bit-à-bit.
- [ ] Test `Skeleton_ComputeModelMatrices_test` : pour un squelette
      simple, vérifie qu'une pose identité produit la bind pose model-space
      correcte (à 1e-5 près sur les composantes Mat4).
- [ ] `LoadSkeletonFromFile` rejette : magic faux, version != 1,
      jointCount > 256, parentIndex ≥ jointCount, parentIndex ≥ ownIndex
      (violation ordre topologique).
- [ ] Pas de chemin absolu.

---

## Anti-objectifs

- **Ne pas** étendre `AssetRegistry` (CHAR-MODEL.4).
- **Ne pas** uploader sur le GPU (CHAR-MODEL.3).
- **Ne pas** charger glTF runtime — boot-time uniquement.
- **Ne pas** introduire de système d'animation (CHAR-MODEL.6+).
- **Ne pas** modifier `MeshAsset` ni `SkinnedMesh` (CHAR-MODEL.1).
- **Ne pas** lier `engine_core` au builder offline.
