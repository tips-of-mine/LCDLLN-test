# CHAR-MODEL.6 — Format clip `.anim` (TRS keyframes) + builder glTF → `.anim`

## Dépendances
- CHAR-MODEL.2 (`Skeleton`, conventions os / quaternion)

## Cadrage

Définir le **format binaire `.anim`** (un clip = un fichier) et le
**builder offline** `tools/anim_builder/` qui convertit un clip glTF 2.0
en `.anim`. Aucun runtime de sampling / blending dans ce ticket — uniquement
le format et l'outil.

Un clip = une animation nommée (ex. `walk_loop`, `idle_breathe`,
`attack_1h_swing`) ciblant un squelette donné, avec :
- une **durée** en secondes,
- un **flag de looping**,
- des **pistes par os** (translation/rotation/scale), keyframes
  irrégulièrement espacées (timestamps explicites).

---

## Pré-requis vérifiables

```bash
git status
ls engine/render/Skeleton.h          # CHAR-MODEL.2
ls tools/skinmesh_builder/           # template tool
ls external/cgltf/cgltf.h
```

---

## Spécification technique

### Conventions

- Temps : **secondes**, `float32`. t=0 = première keyframe.
- Translation : `Vec3` mètres.
- Rotation : `Vec4` quaternion `(x,y,z,w)` normalisé.
- Scale : `Vec3` (souvent 1,1,1 ; les clips glTF qui ont des scales
  non-uniformes sont conservés tels quels).
- Pistes optionnelles : si un os n'a pas de piste pour T/R/S, le sampler
  utilisera la `bindLocal` du squelette (non spécifié dans ce ticket,
  mais le format doit le permettre via `trackCount` par canal).
- Looping : si `flags & kAnimFlagLoop`, le sampler bouclera ; sinon
  clamp à la dernière keyframe.

### Spec binaire `.anim`

```
Offset  Bytes  Champ
0       4      magic         = 'A','N','M','1'  (0x314D4E41)
4       4      version       = 1
8       4      flags         uint32  (bit 0 = loop)
12      4      duration      float   (secondes)
16      32     debugName     ASCII NUL-terminé
48      32     skeletonName  ASCII NUL-terminé (ex. "humanoid_v1")
80      4      trackCount    uint32
84      4      reserved      uint32 (zéro)
88      ...    Track[trackCount]
```

`Track` (variable, aligné 4 B) :

```
0    2  jointIndex     uint16    (résolu par index du squelette ciblé)
2    1  channel        uint8     (0 = T, 1 = R, 2 = S)
3    1  reserved       0
4    4  keyframeCount  uint32
8    ...  timestamps[keyframeCount]   float
... ...  values[keyframeCount * componentCount]   float
                                  (T/S → 3, R → 4)
```

Total taille piste = 8 + 4·N + 4·N·componentCount.

### Validation (builder)

- `keyframeCount ≥ 1`.
- `timestamps` strictement croissants, premier ≥ 0, dernier ≤ `duration`
  (sinon le clip est étendu à `max(timestamps)` avec warning).
- Quaternions normalisés à 1e-3 près ; sinon renormalise et émet warning.
- Jamais deux pistes (jointIndex, channel) identiques.

### Structure runtime (header seulement à ce stade)

```cpp
// engine/render/AnimationClip.h
namespace engine::render
{
    constexpr uint32_t kAnimMagic   = 0x314D4E41; // 'ANM1'
    constexpr uint32_t kAnimVersion = 1;
    constexpr uint32_t kAnimFlagLoop = 1u << 0;

    enum class AnimChannel : uint8_t { Translation = 0, Rotation = 1, Scale = 2 };

    struct AnimationTrack
    {
        uint16_t                       jointIndex;
        AnimChannel                    channel;
        std::vector<float>             timestamps;
        std::vector<float>             values;     // T:3 / R:4 / S:3 par keyframe
    };

    struct AnimationClip
    {
        std::string                  debugName;
        std::string                  skeletonName;
        float                        duration = 0.0f;
        bool                         loop     = false;
        std::vector<AnimationTrack>  tracks;
    };

    bool LoadAnimationClipFromFile(std::string_view path, AnimationClip& out);
}
```

### Builder offline

`tools/anim_builder/anim_builder.exe` :
- Pattern identique à `skinmesh_builder` : autonome, **ne lie pas
  `engine_core`**.
- Entrée : `--input model.gltf`.
- Sortie : `--output clip.anim`.
- Flags :
  - `--clip-name walk_loop` (sélectionne le clip glTF par nom),
  - `--skeleton-name humanoid_v1` (écrit dans l'en-tête, pour validation
    runtime),
  - `--loop` (force `kAnimFlagLoop`),
  - `--validate`.

Le builder résout les noms d'os glTF → indices via le **fichier `.skel`
correspondant** (`--skeleton path.skel`). Sinon, il échoue avec un message
explicite (impossible de produire des `jointIndex` cohérents sans le rig).

---

## Liste des fichiers

**Créés :**
- `engine/render/AnimationClip.h` + `.cpp`
- `tools/anim_builder/CMakeLists.txt`
- `tools/anim_builder/main.cpp`
- `tools/anim_builder/GltfClipReader.{h,cpp}`
- `tools/anim_builder/AnimClipWriter.{h,cpp}`
- `tests/render/AnimationClip_Roundtrip_test.cpp`

**Modifiés :**
- `CMakeLists.txt` racine (`add_subdirectory(tools/anim_builder)`)

---

## CMakeLists.txt

```cmake
target_sources(engine_core PRIVATE
    engine/render/AnimationClip.h
    engine/render/AnimationClip.cpp
)

# tools/anim_builder/CMakeLists.txt :
add_executable(anim_builder main.cpp GltfClipReader.cpp AnimClipWriter.cpp)
target_include_directories(anim_builder PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/external/cgltf)
target_link_libraries(anim_builder PRIVATE engine_math)
set_target_properties(anim_builder PROPERTIES CXX_STANDARD 20)
```

---

## Critères d'acceptation

- [ ] Build Windows + Linux propre.
- [ ] `anim_builder --input rig.gltf --skeleton humanoid_v1.skel
      --clip-name walk_loop --output walk_loop.anim --loop --validate`
      exit 0.
- [ ] Test `AnimationClip_Roundtrip_test` : sérialisation/désérialisation
      d'un clip à 1 piste (rotation, 3 keyframes), égalité bit-à-bit
      des timestamps et values.
- [ ] `LoadAnimationClipFromFile` rejette : magic faux, version != 1,
      `keyframeCount = 0`, timestamps non-monotones, channel ∉ {0,1,2}.
- [ ] Pas de chemin absolu.

---

## Anti-objectifs

- **Ne pas** implémenter le sampling (CHAR-MODEL.7).
- **Ne pas** implémenter le blending (CHAR-MODEL.8).
- **Ne pas** uploader de données GPU (les clips restent CPU-side).
- **Ne pas** charger glTF runtime.
- **Ne pas** lier `engine_core` au builder offline.
- **Ne pas** extraire les `morph targets` glTF (pas de blendshapes dans
  cette release).
