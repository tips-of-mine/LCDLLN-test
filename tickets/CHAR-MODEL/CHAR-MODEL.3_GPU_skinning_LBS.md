# CHAR-MODEL.3 — GPU skinning (LBS) + shader vertex skin

## Dépendances
- CHAR-MODEL.1 (`SkinnedVertex`, format `.skinmesh`)
- CHAR-MODEL.2 (`Skeleton`, `ComputeSkinPalette`)

## Cadrage

Implémenter le **skinning GPU** par Linear Blend Skinning (LBS) :
- vertex shader `skinned_geometry.vert.glsl` qui transforme position +
  normale par la palette de matrices,
- buffer **palette de matrices** uploadé par frame (descriptor set
  dédié skin),
- intégration **vertex input** (`VkPipelineVertexInputStateCreateInfo`)
  pour `SkinnedVertex`.

Ce ticket ne touche **pas** encore `AssetRegistry` (CHAR-MODEL.4) ni le
`GeometryPass` final intégré (CHAR-MODEL.5). Il fournit :
- les **shaders** compilés et leur descriptor layout,
- une **structure GPU** pour la palette,
- un **test off-screen** isolé qui rend un mesh skinné sur un FBO test
  à partir de données fournies en dur par le test (mesh + skel + pose
  bind).

---

## Pré-requis vérifiables

```bash
git status
ls engine/render/SkinnedMesh.h            # CHAR-MODEL.1 livré
ls engine/render/Skeleton.h               # CHAR-MODEL.2 livré
ls engine/render/shaders/                 # shaders existants
ls scripts/compile_game_shaders.ps1       # pipeline shader → SPIR-V
```

---

## Spécification technique

### Descriptor layout skin

Set **3** dédié au skinning (sets 0..2 réservés pour matériau / per-pass /
per-frame existants — vérifier dans `DeferredPipeline.cpp` les indices
réellement utilisés et **adapter** sans casser les bindings actuels).

Binding unique : SSBO de matrices de skin.

```glsl
// engine/render/shaders/skinned_geometry.vert.glsl
#version 450

layout(set = 3, binding = 0, std430) readonly buffer SkinPalette
{
    mat4 boneMatrices[];   // taille dynamique, ≤ 256
} u_skin;
```

Pourquoi SSBO et pas UBO : 256 mat4 = 16 ko, certaines GPU limitent les
UBO à 16 ko. SSBO est plus universel et déjà toléré ailleurs dans le
moteur.

### Vertex input

```cpp
// engine/render/SkinnedVertexInput.h (nouveau)
namespace engine::render
{
    /// Description Vulkan d'un sommet skinné. À utiliser pour tous les pipelines
    /// qui consomment .skinmesh.
    struct SkinnedVertexInputDesc
    {
        VkVertexInputBindingDescription   binding;
        std::array<VkVertexInputAttributeDescription, 5> attributes;
    };

    SkinnedVertexInputDesc MakeSkinnedVertexInputDesc();
}
```

Attributs :

| Loc | Format                         | Offset | Champ |
|-----|--------------------------------|--------|-------|
| 0   | VK_FORMAT_R32G32B32_SFLOAT     | 0      | position |
| 1   | VK_FORMAT_R32G32B32_SFLOAT     | 12     | normal |
| 2   | VK_FORMAT_R32G32_SFLOAT        | 24     | uv |
| 3   | VK_FORMAT_R8G8B8A8_UINT        | 32     | jointIndices |
| 4   | VK_FORMAT_R8G8B8A8_UNORM       | 36     | jointWeights |

Le format `UNORM` divise les poids par 255 → flottants `[0,1]` côté shader.

### Vertex shader

```glsl
#version 450

layout(set = 0, binding = 0) uniform CameraUBO { mat4 view; mat4 proj; } u_cam;
layout(set = 1, binding = 0) uniform ModelUBO  { mat4 model; }            u_obj;
layout(set = 3, binding = 0, std430) readonly buffer SkinPalette
{
    mat4 boneMatrices[];
} u_skin;

layout(location = 0) in vec3  in_position;
layout(location = 1) in vec3  in_normal;
layout(location = 2) in vec2  in_uv;
layout(location = 3) in uvec4 in_jointIndices;
layout(location = 4) in vec4  in_jointWeights;

layout(location = 0) out vec3 out_worldPos;
layout(location = 1) out vec3 out_worldNormal;
layout(location = 2) out vec2 out_uv;

void main()
{
    mat4 skin =
        in_jointWeights.x * u_skin.boneMatrices[in_jointIndices.x] +
        in_jointWeights.y * u_skin.boneMatrices[in_jointIndices.y] +
        in_jointWeights.z * u_skin.boneMatrices[in_jointIndices.z] +
        in_jointWeights.w * u_skin.boneMatrices[in_jointIndices.w];

    vec4 skinnedPos = skin * vec4(in_position, 1.0);
    vec3 skinnedN   = mat3(skin) * in_normal;

    vec4 worldPos   = u_obj.model * skinnedPos;
    out_worldPos    = worldPos.xyz;
    out_worldNormal = normalize(mat3(u_obj.model) * skinnedN);
    out_uv          = in_uv;

    gl_Position = u_cam.proj * u_cam.view * worldPos;
}
```

Fragment shader : **réutiliser** le fragment shader G-Buffer du pass
géométrie statique existant — la sortie est identique (worldPos,
worldNormal, uv → matériau). Si nécessaire, extraire le code partagé
dans un `.glsl` inclus (`include` géré par le préprocesseur du compilateur
de shaders du repo).

### Palette uploadée par frame

```cpp
// engine/render/SkinPaletteBuffer.h (nouveau)
namespace engine::render
{
    /// Buffer GPU host-visible/coherent qui contient les matrices skin pour TOUS
    /// les renderables skinnés d'une frame, concaténées. Chaque renderable connaît
    /// son offset (en nombre de mat4) dans le buffer.
    class SkinPaletteBuffer
    {
    public:
        void Init(VkDevice device, VkPhysicalDevice phys, uint32_t maxMatrices);
        void Shutdown();

        /// Reset au début de frame.
        void BeginFrame();
        /// Append une palette ; retourne l'offset (en nb de mat4) à passer en
        /// uniform pushConstant ou en dynamic offset.
        uint32_t Append(std::span<const engine::math::Mat4> palette);
        void     EndFrame();

        VkBuffer        Buffer() const;
        VkDeviceSize    SizeBytes() const;
    };
}
```

Implémentation : un buffer host-coherent de taille `maxMatrices * 64` B
(par exemple 4096 matrices × 64 = 256 ko) suffit pour ≪ 256 personnages
avec des squelettes ≤ 256 os (bien plus en pratique).

### Test off-screen

`tests/render/SkinnedRender_OffscreenTriangle_test.cpp` :
- Mesh synthétique (3 sommets, 1 triangle) avec 1 seul os ;
- Squelette à 1 os (racine) ;
- Pose bind ;
- Pipeline minimal qui rend dans un FBO test 64×64 ;
- Lit le FBO, vérifie qu'au moins un pixel non-noir existe au centre.

Ce test garantit que **le pipeline est plombé end-to-end** sans dépendre
de `GeometryPass` ni d'`AssetRegistry`.

---

## Liste des fichiers

**Créés :**
- `engine/render/SkinnedVertexInput.h` + `.cpp`
- `engine/render/SkinPaletteBuffer.h` + `.cpp`
- `engine/render/shaders/skinned_geometry.vert.glsl`
- `engine/render/shaders/skinned_geometry.frag.glsl` *(ou re-use du frag G-Buffer existant via include)*
- `tests/render/SkinnedRender_OffscreenTriangle_test.cpp`

**Modifiés :**
- `CMakeLists.txt` (ajout des sources `engine_core`)
- `scripts/compile_game_shaders.ps1` *(ajout des deux nouveaux shaders à la liste)*

**Inchangés :**
- `engine/render/AssetRegistry.{h,cpp}` (CHAR-MODEL.4)
- `engine/render/GeometryPass.{h,cpp}` (CHAR-MODEL.5)

---

## CMakeLists.txt

```cmake
target_sources(engine_core PRIVATE
    engine/render/SkinnedVertexInput.h
    engine/render/SkinnedVertexInput.cpp
    engine/render/SkinPaletteBuffer.h
    engine/render/SkinPaletteBuffer.cpp
)
```

---

## Critères d'acceptation

- [ ] Compilation des deux shaders en SPIR-V via le pipeline existant,
      sans erreur ni warning glslangValidator.
- [ ] Build Windows + Linux propre.
- [ ] `SkinPaletteBuffer` : `Append` retourne l'offset attendu, `BeginFrame`
      le ramène à 0.
- [ ] Test `SkinnedRender_OffscreenTriangle_test` : produit un FBO avec
      ≥ 100 pixels couverts par le triangle skinné.
- [ ] Aucune régression visuelle sur les meshes statiques (le `GeometryPass`
      n'est pas modifié).

---

## Anti-objectifs

- **Ne pas** intégrer dans `GeometryPass` (CHAR-MODEL.5).
- **Ne pas** ajouter de SkinnedMeshAsset à `AssetRegistry` (CHAR-MODEL.4).
- **Ne pas** implémenter le sampler ni le blender d'animation (Phase 1).
- **Ne pas** dépasser 4 influences par sommet (contrainte LBS imposée).
- **Ne pas** changer le set 0/1/2 des descriptors existants — n'utiliser
  que le set 3 pour le skin.
