# Aperçus de textures dans l'éditeur monde — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Afficher des vignettes ImGui pour les 4 textures procédurales et les `.texr` importées dans l'éditeur monde, et appliquer en live les textures importées sur le terrain 3D quand un layer est mappé.

**Architecture:** Nouveau `TexturePreviewCache` (cache lazy de descriptors ImGui pour vignettes) + nouveau panneau `TextureLibraryPanel` (UI dockable) + extension de `TerrainSplatting` pour repacker l'albedo array depuis 4 buffers CPU. Cache et panneau possédés par `Engine`, observés via dirty flag dans `WorldEditorSession`.

**Tech Stack:** C++20, Vulkan 1.3, ImGui (Win32+Vulkan backend), stb_image (déjà présent dans `external/stb/`), CMake (ajout de fichiers à `engine_core` + une exécutable de tests).

**Spec:** `docs/superpowers/specs/2026-05-04-editor-texture-previews-design.md`

---

## File Structure

### Nouveaux fichiers
- `engine/editor/TexturePreviewCache.h` — API publique (Init/Shutdown, GetProceduralThumb, GetTexrThumb, Invalidate, GetCpuRgba256, Tick)
- `engine/editor/TexturePreviewCache.cpp` — implémentation Vulkan (sampler partagé, descriptor pool, deferred destruction, decode .texr via stb_image, resampling box filter)
- `engine/editor/TextureLibraryPanel.h` — déclaration `DrawTextureLibrary(...)`
- `engine/editor/TextureLibraryPanel.cpp` — implémentation ImGui (Win32-only, no-op ailleurs)
- `engine/editor/tests/TexturePreviewCacheTests.cpp` — tests unitaires `ResampleRgba8Box` + `GenerateProceduralAlbedoLayer`

### Fichiers modifiés
- `engine/render/terrain/TerrainSplatting.h` — déclarations `kSplatLayerResolution`, `GenerateProceduralAlbedoLayer` (free function), `SetLayerCpuRgba256`, `RebuildAlbedoArrayFromCpuLayers`
- `engine/render/terrain/TerrainSplatting.cpp` — refactor de la lambda procédurale en free function, promotion 64→256, implémentation des 2 nouvelles méthodes
- `engine/editor/WorldMapEditDocument.h` — RAS (déjà bon)
- `engine/editor/WorldEditorSession.h` — ajout `m_splatRefsDirty` + `MarkSplatRefsDirty()` + `ConsumeSplatRefsDirty()`
- `engine/editor/WorldEditorSession.cpp` — implem accessors + appel de `MarkSplatRefsDirty` au load de carte
- `engine/editor/WorldEditorImGui.h` — ajout `m_showTextureLibrary` + `SetTexturePreviewCache(cache)`
- `engine/editor/WorldEditorImGui.cpp` — vignettes inline dans onglet « Peindre », item de menu « Affichage > Bibliothèque de textures »
- `engine/Engine.h` — déclaration `std::unique_ptr<TexturePreviewCache> m_texturePreviewCache` + méthode privée `ProcessSplatRefsDirty()`
- `engine/Engine.cpp` — Init/Shutdown du cache, Tick par frame, ProcessSplatRefsDirty appelé après les autres ticks éditeur, hook sur `ActionImportTexture` pour `Invalidate`
- `CMakeLists.txt` — ajout des nouveaux .cpp à `engine_core`, déclaration de l'exe de tests
- `docs/world_editor_zone_pipeline.md` — section « Validation textures »

---

## Task 1: Préparer la base — extraire la génération procédurale en free function (no-op fonctionnel)

**Pourquoi :** la lambda interne dans `TerrainSplatting::Init` doit devenir une fonction libre exportée pour être réutilisable par le cache. Cette task ne change PAS le comportement runtime.

**Files:**
- Modify: `engine/render/terrain/TerrainSplatting.h`
- Modify: `engine/render/terrain/TerrainSplatting.cpp`

- [ ] **Step 1: Ajouter la déclaration de la free function dans le header**

Ajouter dans `engine/render/terrain/TerrainSplatting.h`, juste après la définition de `kSplatLayerCount` (avant la struct `SplatMapGpu`) :

```cpp
    /// Génère un layer RGBA8 procédural (bruit déterministe par layer).
    /// Algorithme : combinaison micro/macro de hash xx-style, identique au boot
    /// de TerrainSplatting (cf. b93a14d). Mêmes octets exacts pour mêmes paramètres.
    /// \param resolution Côté en pixels (carré). Min 4, max 4096.
    /// \param layer 0=grass, 1=dirt, 2=rock, 3=snow.
    /// \param outRgba Sortie : resolution * resolution * 4 octets, RGBA8 sRGB.
    /// \return false si layer >= kSplatLayerCount ou resolution < 4.
    bool GenerateProceduralAlbedoLayer(uint32_t resolution, uint32_t layer,
                                       std::vector<uint8_t>& outRgba);
```

- [ ] **Step 2: Implémenter la free function dans le .cpp (copie de la lambda actuelle)**

Dans `engine/render/terrain/TerrainSplatting.cpp`, juste après la fermeture du namespace anonyme interne (ligne ~270, avant la définition de `TerrainSplatting::Init`), ajouter :

```cpp
    bool GenerateProceduralAlbedoLayer(uint32_t resolution, uint32_t layer,
                                       std::vector<uint8_t>& outRgba)
    {
        if (layer >= kSplatLayerCount || resolution < 4u || resolution > 4096u)
        {
            return false;
        }

        // Couleurs de base par layer (identiques au boot TerrainSplatting).
        static const uint8_t kAlbedo[kSplatLayerCount][4] = {
            {  89u, 140u,  51u, 255u },   // grass
            { 128u,  82u,  38u, 255u },   // dirt
            { 128u, 107u,  82u, 255u },   // rock
            { 242u, 242u, 250u, 255u },   // snow
        };
        static const uint8_t kAmplitude[kSplatLayerCount] = { 38u, 28u, 50u, 12u };
        static const uint32_t kMacroCell[kSplatLayerCount] = { 8u, 12u, 4u, 16u };

        auto hashNoise = [](uint32_t x, uint32_t y, uint32_t seed) -> float {
            uint32_t h = x * 0x27d4eb2du ^ y * 0x165667b1u ^ seed * 0x9e3779b9u;
            h ^= h >> 15; h *= 0x85ebca6bu;
            h ^= h >> 13; h *= 0xc2b2ae35u;
            h ^= h >> 16;
            return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000u);
        };

        const uint8_t* col = kAlbedo[layer];
        const float amp = static_cast<float>(kAmplitude[layer]);
        const uint32_t mc = kMacroCell[layer];
        const uint32_t pixels = resolution * resolution;
        outRgba.resize(static_cast<size_t>(pixels) * 4u);

        for (uint32_t y = 0; y < resolution; ++y)
        {
            for (uint32_t x = 0; x < resolution; ++x)
            {
                const float micro = hashNoise(x, y, layer);
                const float macro = hashNoise(x / mc, y / mc, layer + 1000u);
                const float n = (micro * 0.6f + macro * 0.4f) * 2.0f - 1.0f;
                const float delta = n * amp;
                auto clampU8 = [](float v) -> uint8_t {
                    if (v < 0.0f) v = 0.0f;
                    if (v > 255.0f) v = 255.0f;
                    return static_cast<uint8_t>(v);
                };
                uint8_t* dst = outRgba.data() + (y * resolution + x) * 4u;
                dst[0] = clampU8(static_cast<float>(col[0]) + delta);
                dst[1] = clampU8(static_cast<float>(col[1]) + delta);
                dst[2] = clampU8(static_cast<float>(col[2]) + delta);
                dst[3] = 255u;
            }
        }
        return true;
    }
```

- [ ] **Step 3: Remplacer la lambda interne par un appel à la free function dans `Init`**

Dans `engine/render/terrain/TerrainSplatting.cpp`, remplacer le bloc « Build albedo array data » (entre les lignes ~454-490) par :

```cpp
        // Build albedo array data via la free function GenerateProceduralAlbedoLayer.
        {
            std::vector<uint8_t> albedoData(kSplatLayerCount * kPixels * 4u);
            std::vector<uint8_t> oneLayer;
            for (uint32_t layer = 0; layer < kSplatLayerCount; ++layer)
            {
                if (!GenerateProceduralAlbedoLayer(kTexW, layer, oneLayer))
                {
                    LOG_ERROR(Render, "[TerrainSplatting] GenerateProceduralAlbedoLayer({},{}) failed", kTexW, layer);
                    Destroy(device);
                    return false;
                }
                std::memcpy(albedoData.data() + layer * kPixels * 4u,
                            oneLayer.data(), kPixels * 4u);
            }
            if (!UploadTextureArray(device, physDev, albedoData, kTexW, kTexH, kSplatLayerCount,
                                    queue, queueFamilyIndex, m_albedoArray))
            {
                LOG_ERROR(Render, "[TerrainSplatting] Failed to upload albedo array");
                Destroy(device);
                return false;
            }
        }
```

Garder les blocs "normal array" et "ORM array" inchangés.

- [ ] **Step 4: Build complet pour vérifier non-régression**

Run :
```
cmake --build build --target engine_core --config Release
```

Expected : compile sans erreur ni warning. Si warning C4189 (variable non utilisée) sur `kAlbedo`, `kAmplitude`, `kMacroCell` du scope d'origine, supprimer ces lignes (plus utilisées).

- [ ] **Step 5: Lancer `lcdlln_world_editor.exe` et vérifier que le terrain affiche les textures procédurales identiques à avant**

Pas de changement visuel attendu. Si le terrain a changé d'apparence, c'est un bug de refactor à corriger avant de continuer.

- [ ] **Step 6: Commit**

```bash
git add engine/render/terrain/TerrainSplatting.h engine/render/terrain/TerrainSplatting.cpp
git commit -m "$(cat <<'EOF'
refactor(terrain): extrait la generation procedurale en free function

Prep pour M-XX (apercus textures editeur). La lambda interne de
TerrainSplatting::Init devient GenerateProceduralAlbedoLayer (free
function exportee dans TerrainSplatting.h), reutilisable par le futur
TexturePreviewCache pour generer les memes octets dans les vignettes.

Aucun changement runtime : le boot terrain produit exactement les memes
albedo array bytes qu'avant.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Promouvoir la résolution interne du splat array de 64 → 256

**Pourquoi :** la spec exige 256×256 pour le sweet spot qualité/mémoire. Cette task le fait sans toucher aux nouvelles APIs.

**Files:**
- Modify: `engine/render/terrain/TerrainSplatting.h`
- Modify: `engine/render/terrain/TerrainSplatting.cpp`

- [ ] **Step 1: Déclarer la constante de résolution dans le header**

Dans `engine/render/terrain/TerrainSplatting.h`, juste après `kSplatLayerCount` :

```cpp
    /// Resolution (carre) des layers du texture array albedo. Constante a la
    /// compilation : si on veut configurer plus tard, exposer via Config.
    /// 256x256 = sweet spot qualite/memoire pour terrain MMO (cf. spec
    /// 2026-05-04-editor-texture-previews-design.md, section 3).
    static constexpr uint32_t kSplatLayerResolution = 256u;
```

- [ ] **Step 2: Remplacer les `kTexW`/`kTexH` 64u par `kSplatLayerResolution`**

Dans `engine/render/terrain/TerrainSplatting.cpp::Init`, remplacer :

```cpp
        constexpr uint32_t kTexW = 64u;
        constexpr uint32_t kTexH = 64u;
```

par :

```cpp
        constexpr uint32_t kTexW = kSplatLayerResolution;
        constexpr uint32_t kTexH = kSplatLayerResolution;
```

(le reste du code utilise `kTexW`/`kTexH`, pas besoin de changer).

- [ ] **Step 3: Build + test visuel**

Run :
```
cmake --build build --target engine_core --config Release
cmake --build build --target world_editor_app --config Release
```

Expected : compile OK, pkg/world_editor/lcdlln_world_editor.exe lance, terrain visible. Le bruit procédural sera plus fin (pixels plus petits à la même distance).

VRAM : ancien = 4×64×64×4×3 arrays = 196 KB. Nouveau = 4×256×256×4×3 arrays = 3 MB. Acceptable.

- [ ] **Step 4: Commit**

```bash
git add engine/render/terrain/TerrainSplatting.h engine/render/terrain/TerrainSplatting.cpp
git commit -m "$(cat <<'EOF'
feat(terrain): promotion albedo array 64x64 -> 256x256 (constante kSplatLayerResolution)

Sweet spot qualite/memoire pour textures terrain : 256x256 donne ~3cm/pixel
au tiling 8m/tile (vs 12.5cm avant), tout en gardant ~3 MB VRAM total
(4 layers x 3 arrays albedo/normal/ORM x 256x256 x 4 oct).

Constante kSplatLayerResolution pour reuse facile dans le futur
TexturePreviewCache (vignettes editeur, meme bruit identique octet pour octet).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Ajouter ResampleRgba8Box (free function CPU pure) avec tests unitaires

**Files:**
- Create: `engine/editor/TexturePreviewCache.h` (squelette minimal pour cette task)
- Create: `engine/editor/TexturePreviewCache.cpp` (squelette minimal)
- Create: `engine/editor/tests/TexturePreviewCacheTests.cpp`
- Modify: `CMakeLists.txt` (ajout sources + exe de tests)

- [ ] **Step 1: Créer `engine/editor/TexturePreviewCache.h` avec uniquement `ResampleRgba8Box` (le reste viendra task 5)**

```cpp
#pragma once

#include <cstdint>
#include <vector>

namespace engine::editor
{
    /// Resample un buffer RGBA8 a une nouvelle resolution carree par box filter
    /// separable (passe horizontale + verticale). Si le source n'est pas carre,
    /// crop centre vers carre avant resample (pas de stretch, pas de letterbox).
    /// \param src Buffer source RGBA8, srcW * srcH * 4 octets.
    /// \param srcW Largeur source en pixels (>=1).
    /// \param srcH Hauteur source en pixels (>=1).
    /// \param dstSize Cote du carre de sortie en pixels (>=4, <=4096).
    /// \param outRgba Sortie : dstSize * dstSize * 4 octets.
    /// \return true si succes ; false sur params invalides.
    bool ResampleRgba8Box(const uint8_t* src, uint32_t srcW, uint32_t srcH,
                          uint32_t dstSize, std::vector<uint8_t>& outRgba);
} // namespace engine::editor
```

- [ ] **Step 2: Créer `engine/editor/TexturePreviewCache.cpp` avec uniquement `ResampleRgba8Box`**

```cpp
#include "engine/editor/TexturePreviewCache.h"

#include <algorithm>
#include <cstring>

namespace engine::editor
{
    namespace
    {
        /// Crop centre rectangulaire vers carre. Renvoie le pointeur vers le
        /// pixel (0,0) du sous-rectangle carre + son cote.
        void CropToSquare(const uint8_t* src, uint32_t srcW, uint32_t srcH,
                          const uint8_t*& outSrc, uint32_t& outSize, uint32_t& outRowStrideBytes)
        {
            outRowStrideBytes = srcW * 4u;
            if (srcW == srcH)
            {
                outSrc = src;
                outSize = srcW;
                return;
            }
            const uint32_t side = std::min(srcW, srcH);
            const uint32_t offX = (srcW - side) / 2u;
            const uint32_t offY = (srcH - side) / 2u;
            outSrc = src + (offY * srcW + offX) * 4u;
            outSize = side;
        }
    } // namespace

    bool ResampleRgba8Box(const uint8_t* src, uint32_t srcW, uint32_t srcH,
                          uint32_t dstSize, std::vector<uint8_t>& outRgba)
    {
        if (src == nullptr || srcW == 0 || srcH == 0 || dstSize < 4u || dstSize > 4096u)
        {
            return false;
        }

        const uint8_t* sqSrc = nullptr;
        uint32_t sqSize = 0;
        uint32_t srcStride = 0;
        CropToSquare(src, srcW, srcH, sqSrc, sqSize, srcStride);

        outRgba.assign(static_cast<size_t>(dstSize) * dstSize * 4u, 0u);

        // Box filter direct (pas de mipmap chain) : pour chaque pixel dst,
        // moyenner tous les pixels src couverts. Couts O(dstSize^2 * (sqSize/dstSize)^2).
        // Pour 1024->256 : 256^2 * 16 = ~1M reads, ~3ms en debug, <1ms en release.
        for (uint32_t dy = 0; dy < dstSize; ++dy)
        {
            const uint32_t y0 = (dy * sqSize) / dstSize;
            const uint32_t y1 = ((dy + 1u) * sqSize) / dstSize;
            const uint32_t ySpan = std::max(1u, y1 - y0);
            for (uint32_t dx = 0; dx < dstSize; ++dx)
            {
                const uint32_t x0 = (dx * sqSize) / dstSize;
                const uint32_t x1 = ((dx + 1u) * sqSize) / dstSize;
                const uint32_t xSpan = std::max(1u, x1 - x0);
                uint32_t accR = 0, accG = 0, accB = 0, accA = 0;
                const uint32_t count = xSpan * ySpan;
                for (uint32_t sy = y0; sy < y0 + ySpan; ++sy)
                {
                    const uint8_t* row = sqSrc + sy * srcStride;
                    for (uint32_t sx = x0; sx < x0 + xSpan; ++sx)
                    {
                        const uint8_t* p = row + sx * 4u;
                        accR += p[0]; accG += p[1]; accB += p[2]; accA += p[3];
                    }
                }
                uint8_t* dst = outRgba.data() + (dy * dstSize + dx) * 4u;
                dst[0] = static_cast<uint8_t>(accR / count);
                dst[1] = static_cast<uint8_t>(accG / count);
                dst[2] = static_cast<uint8_t>(accB / count);
                dst[3] = static_cast<uint8_t>(accA / count);
            }
        }
        return true;
    }
} // namespace engine::editor
```

- [ ] **Step 3: Créer le fichier de tests `engine/editor/tests/TexturePreviewCacheTests.cpp`**

```cpp
/// Tests unitaires pour les fonctions CPU pures de TexturePreviewCache :
/// - ResampleRgba8Box (crop + box filter)
/// - GenerateProceduralAlbedoLayer (deterministe)
/// Pas de Vulkan ici : ces tests doivent tourner en CI sans GPU.

#include "engine/editor/TexturePreviewCache.h"
#include "engine/render/terrain/TerrainSplatting.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{
    int g_failed = 0;

    #define REQUIRE(cond) do { \
        if (!(cond)) { \
            std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed; \
        } \
    } while (0)

    /// 256x256 rouge uni -> 64x64 rouge uni (preserve la couleur).
    void Test_ResampleDownsampleSquare()
    {
        std::vector<uint8_t> red(256u * 256u * 4u);
        for (size_t p = 0; p < 256u * 256u; ++p)
        {
            red[p * 4u + 0] = 200u;
            red[p * 4u + 1] = 50u;
            red[p * 4u + 2] = 30u;
            red[p * 4u + 3] = 255u;
        }
        std::vector<uint8_t> out;
        REQUIRE(engine::editor::ResampleRgba8Box(red.data(), 256u, 256u, 64u, out));
        REQUIRE(out.size() == 64u * 64u * 4u);
        // Tous les pixels resamples doivent etre exactement la couleur source.
        for (size_t p = 0; p < 64u * 64u; ++p)
        {
            REQUIRE(out[p * 4u + 0] == 200u);
            REQUIRE(out[p * 4u + 1] == 50u);
            REQUIRE(out[p * 4u + 2] == 30u);
            REQUIRE(out[p * 4u + 3] == 255u);
        }
    }

    /// 1024x512 -> 256x256 : crop centre (la zone large hors centre est ignoree).
    /// On met du rouge dans le carre central [256..768] et du bleu hors-zone.
    /// Apres resample, attendu = rouge uniforme.
    void Test_ResampleNonSquareCropsCenter()
    {
        const uint32_t W = 1024u, H = 512u;
        std::vector<uint8_t> img(W * H * 4u);
        for (uint32_t y = 0; y < H; ++y)
        {
            for (uint32_t x = 0; x < W; ++x)
            {
                const bool inSquare = (x >= 256u && x < 768u);
                uint8_t* p = img.data() + (y * W + x) * 4u;
                if (inSquare)
                {
                    p[0] = 200u; p[1] = 50u; p[2] = 30u; p[3] = 255u;
                }
                else
                {
                    p[0] = 30u; p[1] = 60u; p[2] = 200u; p[3] = 255u;
                }
            }
        }
        std::vector<uint8_t> out;
        REQUIRE(engine::editor::ResampleRgba8Box(img.data(), W, H, 256u, out));
        // Tous les pixels resamples doivent etre rouge (zone centree croppee).
        for (size_t p = 0; p < 256u * 256u; ++p)
        {
            REQUIRE(out[p * 4u + 0] == 200u);
            REQUIRE(out[p * 4u + 1] == 50u);
            REQUIRE(out[p * 4u + 2] == 30u);
        }
    }

    /// GenerateProceduralAlbedoLayer doit etre deterministe : memes parametres
    /// -> memes octets exacts. Layers differents -> buffers differents.
    void Test_GenerateProceduralDeterminism()
    {
        std::vector<uint8_t> a, b, c;
        REQUIRE(engine::render::terrain::GenerateProceduralAlbedoLayer(64u, 0u, a));
        REQUIRE(engine::render::terrain::GenerateProceduralAlbedoLayer(64u, 0u, b));
        REQUIRE(engine::render::terrain::GenerateProceduralAlbedoLayer(64u, 1u, c));
        REQUIRE(a.size() == 64u * 64u * 4u);
        REQUIRE(a == b); // determinisme
        REQUIRE(a != c); // layers differents
    }

    /// Tailles invalides : layer >=4, resolution <4 ou >4096.
    void Test_GenerateProceduralInvalidParams()
    {
        std::vector<uint8_t> out;
        REQUIRE(!engine::render::terrain::GenerateProceduralAlbedoLayer(64u, 4u, out));
        REQUIRE(!engine::render::terrain::GenerateProceduralAlbedoLayer(2u, 0u, out));
        REQUIRE(!engine::render::terrain::GenerateProceduralAlbedoLayer(8192u, 0u, out));
    }

    /// Resample params invalides : null src, taille 0, dstSize hors bornes.
    void Test_ResampleInvalidParams()
    {
        std::vector<uint8_t> out;
        REQUIRE(!engine::editor::ResampleRgba8Box(nullptr, 64u, 64u, 32u, out));
        std::vector<uint8_t> img(64u * 64u * 4u, 0u);
        REQUIRE(!engine::editor::ResampleRgba8Box(img.data(), 0u, 64u, 32u, out));
        REQUIRE(!engine::editor::ResampleRgba8Box(img.data(), 64u, 64u, 2u, out));
        REQUIRE(!engine::editor::ResampleRgba8Box(img.data(), 64u, 64u, 8192u, out));
    }
} // namespace

int main()
{
    Test_ResampleDownsampleSquare();
    Test_ResampleNonSquareCropsCenter();
    Test_GenerateProceduralDeterminism();
    Test_GenerateProceduralInvalidParams();
    Test_ResampleInvalidParams();
    if (g_failed == 0)
    {
        std::fprintf(stdout, "[OK] all texture_preview_cache_tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "[FAIL] %d assertions failed\n", g_failed);
    return 1;
}
```

- [ ] **Step 4: Modifier `CMakeLists.txt` — ajouter les sources à `engine_core` + l'exe de tests**

Dans `CMakeLists.txt`, ajouter `engine/editor/TexturePreviewCache.cpp` à la liste de sources de `engine_core` (autour de la ligne 256, juste après `engine/editor/WorldEditorSession.cpp`) :

```cmake
  engine/editor/WorldEditorSession.cpp
  engine/editor/TexturePreviewCache.cpp
```

Puis, à la fin du fichier (après le dernier `add_test`, vers la ligne ~620), ajouter :

```cmake
# Tests unitaires CPU pour TexturePreviewCache (ResampleRgba8Box, GenerateProceduralAlbedoLayer).
# Pas de dependance Vulkan : tournables en CI sans GPU.
add_executable(texture_preview_cache_tests engine/editor/tests/TexturePreviewCacheTests.cpp)
target_include_directories(texture_preview_cache_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(texture_preview_cache_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(texture_preview_cache_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME texture_preview_cache_tests COMMAND texture_preview_cache_tests)
```

- [ ] **Step 5: Build et lancer les tests, vérifier qu'ils passent**

Run :
```
cmake --build build --target texture_preview_cache_tests --config Release
ctest --test-dir build -C Release -R texture_preview_cache_tests --output-on-failure
```

Expected : `[OK] all texture_preview_cache_tests passed`, exit 0. Si un test échoue, fix le code avant de continuer.

- [ ] **Step 6: Commit**

```bash
git add engine/editor/TexturePreviewCache.h engine/editor/TexturePreviewCache.cpp engine/editor/tests/TexturePreviewCacheTests.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(editor): ResampleRgba8Box (CPU pur) + tests unitaires

Squelette de TexturePreviewCache avec d'abord la fonction CPU pure
(ResampleRgba8Box, box filter avec crop centre vers carre). 5 tests
unitaires couvrent : downsample carre, crop d'un non-carre, determinisme
GenerateProceduralAlbedoLayer, params invalides.

Tests CPU-only, executable standalone (pattern de log_rotation_smoke_tests).
Tournables en CI Windows sans GPU.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Ajouter `LoadTexrFile` (helper de décodage TEXR + fallback PNG via stb_image)

**Files:**
- Modify: `engine/editor/TexturePreviewCache.h`
- Modify: `engine/editor/TexturePreviewCache.cpp`
- Modify: `engine/editor/tests/TexturePreviewCacheTests.cpp` (un test de plus)

- [ ] **Step 1: Déclarer `LoadTexrFile` dans le header**

Ajouter dans `engine/editor/TexturePreviewCache.h`, après la déclaration de `ResampleRgba8Box` :

```cpp
    /// Decode un fichier .texr (magic TEXR + RGBA8) ou un PNG/JPG (via stb_image).
    /// Le format .texr est defini dans engine/render/AssetRegistry.cpp :
    ///   bytes 0..3 : magic 'TEXR' (0x52584554 LE)
    ///   bytes 4..7 : width (uint32 LE)
    ///   bytes 8..11: height (uint32 LE)
    ///   bytes 12..15: sRGB flag (uint32 LE, 0 = lineaire, !=0 = sRGB)
    ///   bytes 16.. : width*height*4 octets RGBA8
    ///
    /// \param absolutePath Chemin absolu sur disque (string UTF-8). Le caller
    ///   est responsable de resoudre les chemins content-relatifs en absolus
    ///   via Config::ResolveContentPath.
    /// \param outRgba Buffer de sortie (width * height * 4 octets RGBA8).
    /// \param outWidth Largeur du buffer decode.
    /// \param outHeight Hauteur du buffer decode.
    /// \return true si succes. false si fichier introuvable, magic invalide,
    ///   buffer trop petit, ou erreur stb_image. LOG_ERROR emis.
    bool LoadTexrFile(const std::string& absolutePath,
                      std::vector<uint8_t>& outRgba,
                      uint32_t& outWidth, uint32_t& outHeight);
```

Et ajouter l'include `<string>` en haut du header.

- [ ] **Step 2: Implémenter `LoadTexrFile` dans le .cpp**

Ajouter en haut de `engine/editor/TexturePreviewCache.cpp` :

```cpp
#include "engine/core/Log.h"

// stb_image : single header, definition une seule fois dans tout le binaire.
// Si STB_IMAGE_IMPLEMENTATION est deja defini ailleurs (engine_core), retirer
// ces 2 lignes et utiliser le symbole global. Verification au build (linker
// erreur multiple definition).
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "stb_image.h"

#include <filesystem>
#include <fstream>
```

**ATTENTION :** vérifier d'abord avec `Grep "STB_IMAGE_IMPLEMENTATION"` dans le repo — si déjà défini ailleurs (ex: dans WorldMapIo.cpp), retirer le `#define` et garder juste `#include "stb_image.h"`.

Puis ajouter la fonction (après `ResampleRgba8Box`) :

```cpp
    namespace
    {
        constexpr uint32_t kTexrMagic = 0x52584554u;  // "TEXR"
    }

    bool LoadTexrFile(const std::string& absolutePath,
                      std::vector<uint8_t>& outRgba,
                      uint32_t& outWidth, uint32_t& outHeight)
    {
        outRgba.clear();
        outWidth = 0;
        outHeight = 0;

        std::error_code ec;
        if (!std::filesystem::exists(absolutePath, ec))
        {
            LOG_ERROR(Render, "[TexturePreviewCache] Texture file not found: {}", absolutePath);
            return false;
        }

        std::ifstream f(absolutePath, std::ios::binary);
        if (!f)
        {
            LOG_ERROR(Render, "[TexturePreviewCache] Cannot open texture file: {}", absolutePath);
            return false;
        }
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());

        // Si .texr (header magic) : decoder directement.
        if (data.size() >= 16u)
        {
            uint32_t magic = 0, w = 0, h = 0, srgb = 0;
            std::memcpy(&magic, data.data(), 4);
            if (magic == kTexrMagic)
            {
                std::memcpy(&w, data.data() + 4, 4);
                std::memcpy(&h, data.data() + 8, 4);
                std::memcpy(&srgb, data.data() + 12, 4);
                (void)srgb;
                if (w == 0 || h == 0 || w > 4096u || h > 4096u)
                {
                    LOG_ERROR(Render, "[TexturePreviewCache] TEXR invalid dims {}x{}: {}", w, h, absolutePath);
                    return false;
                }
                const size_t pixelBytes = static_cast<size_t>(w) * h * 4u;
                if (data.size() < 16u + pixelBytes)
                {
                    LOG_ERROR(Render, "[TexturePreviewCache] TEXR truncated: {}", absolutePath);
                    return false;
                }
                outRgba.assign(data.begin() + 16, data.begin() + 16 + static_cast<long long>(pixelBytes));
                outWidth = w;
                outHeight = h;
                return true;
            }
        }

        // Fallback : PNG/JPG/TGA/BMP via stb_image.
        int w = 0, h = 0, comp = 0;
        stbi_uc* pixels = stbi_load_from_memory(data.data(), static_cast<int>(data.size()),
                                                &w, &h, &comp, 4);
        if (pixels == nullptr || w <= 0 || h <= 0 || w > 4096 || h > 4096)
        {
            LOG_ERROR(Render, "[TexturePreviewCache] stb_image decode failed or out of range: {}", absolutePath);
            if (pixels) stbi_image_free(pixels);
            return false;
        }
        const size_t pixelBytes = static_cast<size_t>(w) * h * 4u;
        outRgba.assign(pixels, pixels + pixelBytes);
        outWidth = static_cast<uint32_t>(w);
        outHeight = static_cast<uint32_t>(h);
        stbi_image_free(pixels);
        return true;
    }
```

- [ ] **Step 3: Ajouter un test sur `LoadTexrFile` avec un .texr écrit en mémoire**

Dans `engine/editor/tests/TexturePreviewCacheTests.cpp`, ajouter avant `int main()` :

```cpp
    /// Ecrit un .texr 4x4 RGBA8 dans tmp et le decode via LoadTexrFile.
    void Test_LoadTexrRoundTrip()
    {
        const std::filesystem::path tmpPath = std::filesystem::temp_directory_path() / "lcdlln_texr_test.texr";
        std::vector<uint8_t> file;
        const uint32_t magic = 0x52584554u;
        const uint32_t w = 4u, h = 4u, srgb = 1u;
        file.resize(16u + w * h * 4u);
        std::memcpy(file.data() + 0,  &magic, 4);
        std::memcpy(file.data() + 4,  &w,     4);
        std::memcpy(file.data() + 8,  &h,     4);
        std::memcpy(file.data() + 12, &srgb,  4);
        for (size_t p = 0; p < w * h; ++p)
        {
            file[16u + p * 4u + 0] = 100u;
            file[16u + p * 4u + 1] = 150u;
            file[16u + p * 4u + 2] = 200u;
            file[16u + p * 4u + 3] = 255u;
        }
        {
            std::ofstream f(tmpPath, std::ios::binary);
            f.write(reinterpret_cast<const char*>(file.data()), static_cast<long long>(file.size()));
        }

        std::vector<uint8_t> out;
        uint32_t outW = 0, outH = 0;
        REQUIRE(engine::editor::LoadTexrFile(tmpPath.string(), out, outW, outH));
        REQUIRE(outW == 4u);
        REQUIRE(outH == 4u);
        REQUIRE(out.size() == 64u);
        REQUIRE(out[0] == 100u && out[1] == 150u && out[2] == 200u && out[3] == 255u);

        std::filesystem::remove(tmpPath);
    }
```

Ajouter les includes manquants en haut du test : `<filesystem>`, `<fstream>`, `<cstring>`.

Appeler `Test_LoadTexrRoundTrip()` dans `main()`.

- [ ] **Step 4: Build et lancer le test**

Run :
```
cmake --build build --target texture_preview_cache_tests --config Release
ctest --test-dir build -C Release -R texture_preview_cache_tests --output-on-failure
```

Expected : tous les tests passent, exit 0.

- [ ] **Step 5: Commit**

```bash
git add engine/editor/TexturePreviewCache.h engine/editor/TexturePreviewCache.cpp engine/editor/tests/TexturePreviewCacheTests.cpp
git commit -m "$(cat <<'EOF'
feat(editor): LoadTexrFile decode .texr + fallback PNG/JPG via stb_image

Decoder unifie pour vignettes editeur :
- Si magic 'TEXR' en tete : parse les 16 octets header + RGBA8 brut.
- Sinon : fallback stb_image (PNG/JPG/TGA/BMP, force 4 canaux).
Cap a 4096x4096 (cf. spec sec 4.2). Tests round-trip avec .texr 4x4 ecrit
en tmp.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Ajouter `TerrainSplatting::SetLayerCpuRgba256` + `RebuildAlbedoArrayFromCpuLayers`

**Files:**
- Modify: `engine/render/terrain/TerrainSplatting.h`
- Modify: `engine/render/terrain/TerrainSplatting.cpp`

- [ ] **Step 1: Déclarer les 2 nouvelles méthodes dans le header**

Dans `engine/render/terrain/TerrainSplatting.h`, ajouter dans la section publique de `TerrainSplatting` (après `ReuploadSplatMap`) :

```cpp
        /// Stocke un buffer CPU RGBA8 kSplatLayerResolution^2 pour le layer
        /// donne. Le buffer doit faire exactement
        /// kSplatLayerResolution * kSplatLayerResolution * 4 octets — sinon
        /// l'appel est ignore (LOG_ERROR). N'uploade pas tout de suite : il
        /// faut appeler RebuildAlbedoArrayFromCpuLayers ensuite.
        /// \param layer 0..kSplatLayerCount-1.
        /// \param rgba Pixels RGBA8 row-major.
        void SetLayerCpuRgba256(uint32_t layer, const std::vector<uint8_t>& rgba);

        /// Repacke les 4 layers CPU (m_layerCpuData) en un buffer staging
        /// 4*kSplatLayerResolution^2*4 et re-upload via vkCmdCopyBufferToImage
        /// dans m_albedoArray.image. Pattern identique a ReuploadSplatMap :
        /// SHADER_READ_ONLY -> TRANSFER_DST -> copies -> SHADER_READ_ONLY,
        /// submit + vkQueueWaitIdle.
        ///
        /// Pour les layers sans donnee CPU stockee (SetLayerCpuRgba256 jamais
        /// appele), regenere la procedurale via GenerateProceduralAlbedoLayer.
        ///
        /// \return true si succes.
        bool RebuildAlbedoArrayFromCpuLayers(VkDevice device, VkPhysicalDevice physDev,
                                             VkQueue queue, uint32_t queueFamilyIndex);
```

Dans la section privée, ajouter le storage CPU :

```cpp
        /// Buffers CPU des 4 layers, alimentes par SetLayerCpuRgba256.
        /// Vide pour un layer = utilise la procedurale au prochain rebuild.
        std::array<std::vector<uint8_t>, kSplatLayerCount> m_layerCpuData;
```

Et ajouter `#include <array>` si pas déjà présent.

- [ ] **Step 2: Implémenter `SetLayerCpuRgba256` dans le .cpp**

À la fin de `engine/render/terrain/TerrainSplatting.cpp` (avant la fermeture du namespace), ajouter :

```cpp
    void TerrainSplatting::SetLayerCpuRgba256(uint32_t layer, const std::vector<uint8_t>& rgba)
    {
        if (layer >= kSplatLayerCount)
        {
            LOG_ERROR(Render, "[TerrainSplatting] SetLayerCpuRgba256: layer {} out of range", layer);
            return;
        }
        const size_t expected = static_cast<size_t>(kSplatLayerResolution) * kSplatLayerResolution * 4u;
        if (rgba.size() != expected)
        {
            LOG_ERROR(Render, "[TerrainSplatting] SetLayerCpuRgba256: rgba size {} != expected {} ({}x{}x4)",
                      rgba.size(), expected, kSplatLayerResolution, kSplatLayerResolution);
            return;
        }
        m_layerCpuData[layer] = rgba;
    }

    bool TerrainSplatting::RebuildAlbedoArrayFromCpuLayers(VkDevice device, VkPhysicalDevice physDev,
                                                            VkQueue queue, uint32_t queueFamilyIndex)
    {
        if (m_albedoArray.image == VK_NULL_HANDLE)
        {
            LOG_ERROR(Render, "[TerrainSplatting] RebuildAlbedoArrayFromCpuLayers: array not initialized");
            return false;
        }
        const uint32_t res = kSplatLayerResolution;
        const size_t layerBytes = static_cast<size_t>(res) * res * 4u;
        std::vector<uint8_t> packed(static_cast<size_t>(kSplatLayerCount) * layerBytes);
        std::vector<uint8_t> tmpProc;

        for (uint32_t layer = 0; layer < kSplatLayerCount; ++layer)
        {
            const uint8_t* srcLayer = nullptr;
            if (m_layerCpuData[layer].size() == layerBytes)
            {
                srcLayer = m_layerCpuData[layer].data();
            }
            else
            {
                if (!GenerateProceduralAlbedoLayer(res, layer, tmpProc))
                {
                    LOG_ERROR(Render, "[TerrainSplatting] Rebuild: procedural fallback failed for layer {}", layer);
                    return false;
                }
                srcLayer = tmpProc.data();
            }
            std::memcpy(packed.data() + layer * layerBytes, srcLayer, layerBytes);
        }

        // Staging buffer + copy via existing helper (reuse internal pattern).
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        if (!CreateStagingBuffer(device, physDev, packed.size(), staging, stagingMem))
        {
            return false;
        }
        // Map + memcpy.
        void* mapped = nullptr;
        if (vkMapMemory(device, stagingMem, 0, packed.size(), 0, &mapped) != VK_SUCCESS)
        {
            vkDestroyBuffer(device, staging, nullptr);
            vkFreeMemory(device, stagingMem, nullptr);
            return false;
        }
        std::memcpy(mapped, packed.data(), packed.size());
        vkUnmapMemory(device, stagingMem);

        // Build copy regions (1 per layer).
        std::vector<VkBufferImageCopy> regions(kSplatLayerCount);
        for (uint32_t layer = 0; layer < kSplatLayerCount; ++layer)
        {
            VkBufferImageCopy& r = regions[layer];
            r = {};
            r.bufferOffset = layer * layerBytes;
            r.bufferRowLength = 0;
            r.bufferImageHeight = 0;
            r.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            r.imageSubresource.mipLevel = 0;
            r.imageSubresource.baseArrayLayer = layer;
            r.imageSubresource.layerCount = 1;
            r.imageOffset = { 0, 0, 0 };
            r.imageExtent = { res, res, 1 };
        }

        // Use existing UploadViaStaging helper (handles barriers SHADER_READ_ONLY <-> TRANSFER_DST).
        // NOTE: UploadViaStaging assume oldLayout=UNDEFINED. Pour reuploader sur une image deja
        // en SHADER_READ_ONLY, on duplique le pattern ici avec barrier explicite. Voir Step 3.

        const bool ok = ReuploadAlbedoArrayInternal(device, queue, queueFamilyIndex,
                                                     staging, regions);
        vkDestroyBuffer(device, staging, nullptr);
        vkFreeMemory(device, stagingMem, nullptr);
        return ok;
    }
```

- [ ] **Step 3: Implémenter `ReuploadAlbedoArrayInternal` (helper privé)**

Ajouter dans la section privée du header :

```cpp
        /// Helper interne : barrier SHADER_READ_ONLY -> TRANSFER_DST, copy, retour
        /// SHADER_READ_ONLY. Submit + vkQueueWaitIdle. Utilise par
        /// RebuildAlbedoArrayFromCpuLayers (image deja en SHADER_READ_ONLY).
        bool ReuploadAlbedoArrayInternal(VkDevice device, VkQueue queue, uint32_t queueFamilyIndex,
                                         VkBuffer staging,
                                         const std::vector<VkBufferImageCopy>& regions);
```

Dans le .cpp, après `RebuildAlbedoArrayFromCpuLayers` :

```cpp
    bool TerrainSplatting::ReuploadAlbedoArrayInternal(VkDevice device, VkQueue queue,
                                                        uint32_t queueFamilyIndex,
                                                        VkBuffer staging,
                                                        const std::vector<VkBufferImageCopy>& regions)
    {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandPoolCreateInfo poolCI{};
        poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolCI.queueFamilyIndex = queueFamilyIndex;
        poolCI.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        if (vkCreateCommandPool(device, &poolCI, nullptr, &pool) != VK_SUCCESS) return false;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo aci{};
        aci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        aci.commandPool = pool;
        aci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        aci.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device, &aci, &cmd) != VK_SUCCESS)
        {
            vkDestroyCommandPool(device, pool, nullptr);
            return false;
        }

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);

        // Barrier SHADER_READ_ONLY -> TRANSFER_DST for all layers.
        {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = m_albedoArray.image;
            b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, kSplatLayerCount };
            b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &b);
        }

        vkCmdCopyBufferToImage(cmd, staging, m_albedoArray.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<uint32_t>(regions.size()), regions.data());

        // Barrier TRANSFER_DST -> SHADER_READ_ONLY for all layers.
        {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = m_albedoArray.image;
            b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, kSplatLayerCount };
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &b);
        }

        vkEndCommandBuffer(cmd);

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        if (vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS)
        {
            vkDestroyCommandPool(device, pool, nullptr);
            return false;
        }
        vkQueueWaitIdle(queue);
        vkDestroyCommandPool(device, pool, nullptr);
        return true;
    }
```

- [ ] **Step 4: Build complet**

Run :
```
cmake --build build --target engine_core --config Release
```

Expected : compile sans erreur. Si erreur de membre privé `CreateStagingBuffer` introuvable, vérifier qu'il est dans le namespace anonyme du .cpp (il l'est, accessible depuis n'importe quelle méthode du même fichier).

- [ ] **Step 5: Commit**

```bash
git add engine/render/terrain/TerrainSplatting.h engine/render/terrain/TerrainSplatting.cpp
git commit -m "$(cat <<'EOF'
feat(terrain): SetLayerCpuRgba256 + RebuildAlbedoArrayFromCpuLayers

Permet de remplacer un layer du splat array par un buffer CPU 256x256
arbitraire (texture importee) puis de re-uploader sur GPU via staging.
Pattern barrier SHADER_READ_ONLY -> TRANSFER_DST -> SHADER_READ_ONLY,
identique a ReuploadSplatMap. Layer sans donnee CPU = fallback procedural.

Pas encore branche cote editeur — cf. tasks suivantes.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: TexturePreviewCache — squelette Init/Shutdown (sampler + descriptor pool)

**Files:**
- Modify: `engine/editor/TexturePreviewCache.h`
- Modify: `engine/editor/TexturePreviewCache.cpp`

- [ ] **Step 1: Compléter le header avec la classe**

Dans `engine/editor/TexturePreviewCache.h`, après les free functions, avant la fermeture du namespace :

```cpp
} // namespace engine::editor

// Forward decl Vulkan + ImGui pour ne pas tirer les headers dans le .h.
struct VkDevice_T;       typedef VkDevice_T*       VkDevice;
struct VkPhysicalDevice_T; typedef VkPhysicalDevice_T* VkPhysicalDevice;
struct VkQueue_T;        typedef VkQueue_T*        VkQueue;
struct VkSampler_T;      typedef VkSampler_T*      VkSampler;
struct VkDescriptorPool_T; typedef VkDescriptorPool_T* VkDescriptorPool;
struct VkImage_T;        typedef VkImage_T*        VkImage;
struct VkImageView_T;    typedef VkImageView_T*    VkImageView;
struct VkDeviceMemory_T; typedef VkDeviceMemory_T* VkDeviceMemory;
struct VkDescriptorSet_T; typedef VkDescriptorSet_T* VkDescriptorSet;
typedef void* ImTextureID;

namespace engine::editor
{
    /// Cache lazy de textures decoders + uploadees a 256x256 RGBA8 pour rendu
    /// dans ImGui (vignettes editeur monde) et reupload du splat array terrain.
    /// Possede par Engine, vit le temps du device Vulkan.
    class TexturePreviewCache
    {
    public:
        TexturePreviewCache() = default;
        TexturePreviewCache(const TexturePreviewCache&) = delete;
        TexturePreviewCache& operator=(const TexturePreviewCache&) = delete;
        ~TexturePreviewCache();

        /// Initialise sampler + descriptor pool. Doit etre appelee apres le device
        /// Vulkan et apres ImGui_ImplVulkan_Init.
        /// \param contentDir Repertoire absolu pointant vers <content>/ (pour
        ///   resoudre les chemins .texr content-relatifs).
        /// \return true si succes.
        bool Init(VkDevice device, VkPhysicalDevice physDev,
                  VkQueue queue, uint32_t queueFamilyIndex,
                  const std::string& contentDir);
        void Shutdown();

        bool IsReady() const { return m_ready; }

        // ── API a etendre dans les tasks suivantes ─────────────────────────
        // ImTextureID GetProceduralThumb(uint32_t layer);   // Task 7
        // ImTextureID GetTexrThumb(const std::string& contentRelPath);  // Task 8
        // void Invalidate(const std::string& contentRelPath);  // Task 9
        // const std::vector<uint8_t>* GetCpuRgba256(const std::string& key) const; // Task 8
        // void Tick(uint64_t currentFrameIndex, uint32_t framesInFlight); // Task 9

    private:
        bool m_ready = false;
        VkDevice m_device = nullptr;
        VkPhysicalDevice m_physDev = nullptr;
        VkQueue m_queue = nullptr;
        uint32_t m_queueFamily = 0;
        std::string m_contentDir;

        VkSampler m_sampler = nullptr;
        VkDescriptorPool m_pool = nullptr;
    };

} // namespace engine::editor
```

(remettre la fermeture du namespace après la classe ; supprimer la fermeture intermédiaire avant les forward decl, c'était une transition incorrecte. Ajuste pour avoir la déclaration globale `typedef` PUIS le namespace.)

**Forme finale correcte** :

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Forward decl Vulkan + ImGui (evite #include vulkan.h dans le .h).
struct VkDevice_T;        typedef VkDevice_T*        VkDevice;
struct VkPhysicalDevice_T; typedef VkPhysicalDevice_T* VkPhysicalDevice;
struct VkQueue_T;         typedef VkQueue_T*         VkQueue;
struct VkSampler_T;       typedef VkSampler_T*       VkSampler;
struct VkDescriptorPool_T; typedef VkDescriptorPool_T* VkDescriptorPool;
struct VkImage_T;         typedef VkImage_T*         VkImage;
struct VkImageView_T;     typedef VkImageView_T*     VkImageView;
struct VkDeviceMemory_T;  typedef VkDeviceMemory_T*  VkDeviceMemory;
struct VkDescriptorSet_T; typedef VkDescriptorSet_T* VkDescriptorSet;
typedef void* ImTextureID;

namespace engine::editor
{
    bool ResampleRgba8Box(...);   // tel que defini precedemment
    bool LoadTexrFile(...);       // tel que defini precedemment

    class TexturePreviewCache { ... };
}
```

- [ ] **Step 2: Implémenter `Init` et `Shutdown` dans le .cpp**

Ajouter `#include <vulkan/vulkan.h>` en haut du .cpp (après les `#define STB_IMAGE_*`).

```cpp
    TexturePreviewCache::~TexturePreviewCache()
    {
        Shutdown();
    }

    bool TexturePreviewCache::Init(VkDevice device, VkPhysicalDevice physDev,
                                    VkQueue queue, uint32_t queueFamilyIndex,
                                    const std::string& contentDir)
    {
        if (m_ready) return true;
        if (device == nullptr || physDev == nullptr || queue == nullptr)
        {
            LOG_ERROR(Render, "[TexturePreviewCache] Init: invalid Vulkan handles");
            return false;
        }
        m_device = device;
        m_physDev = physDev;
        m_queue = queue;
        m_queueFamily = queueFamilyIndex;
        m_contentDir = contentDir;

        // Sampler partage : linear filter, clamp to edge.
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxAnisotropy = 1.0f;
        si.minLod = 0.0f; si.maxLod = 0.0f;
        if (vkCreateSampler(m_device, &si, nullptr, &m_sampler) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[TexturePreviewCache] vkCreateSampler failed");
            return false;
        }

        // Descriptor pool dedie : 64 sets max, 1 sampled image chacun (pour ImGui).
        VkDescriptorPoolSize ps{};
        ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps.descriptorCount = 64u;
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pi.maxSets = 64u;
        pi.poolSizeCount = 1;
        pi.pPoolSizes = &ps;
        if (vkCreateDescriptorPool(m_device, &pi, nullptr, &m_pool) != VK_SUCCESS)
        {
            vkDestroySampler(m_device, m_sampler, nullptr);
            m_sampler = nullptr;
            LOG_ERROR(Render, "[TexturePreviewCache] vkCreateDescriptorPool failed");
            return false;
        }

        m_ready = true;
        LOG_INFO(Render, "[TexturePreviewCache] Init OK (contentDir={})", m_contentDir);
        return true;
    }

    void TexturePreviewCache::Shutdown()
    {
        if (m_device != nullptr)
        {
            if (m_pool != nullptr)
            {
                vkDestroyDescriptorPool(m_device, m_pool, nullptr);
                m_pool = nullptr;
            }
            if (m_sampler != nullptr)
            {
                vkDestroySampler(m_device, m_sampler, nullptr);
                m_sampler = nullptr;
            }
        }
        m_device = nullptr;
        m_physDev = nullptr;
        m_queue = nullptr;
        m_queueFamily = 0;
        m_contentDir.clear();
        m_ready = false;
    }
```

- [ ] **Step 3: Build pour vérifier qu'on n'a rien cassé**

Run :
```
cmake --build build --target engine_core --config Release
cmake --build build --target texture_preview_cache_tests --config Release
ctest --test-dir build -C Release -R texture_preview_cache_tests --output-on-failure
```

Expected : compile OK, tests passent (Vulkan pas appelé dans les tests).

- [ ] **Step 4: Commit**

```bash
git add engine/editor/TexturePreviewCache.h engine/editor/TexturePreviewCache.cpp
git commit -m "$(cat <<'EOF'
feat(editor): TexturePreviewCache squelette (Init/Shutdown, sampler, pool)

Cache lazy pour rendu de vignettes ImGui dans l'editeur monde. Cette
commit pose les fondations (cycle de vie Vulkan, sampler partage linear
clamp, descriptor pool dedie 64 sets). Les Get*/Invalidate/Tick suivent.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: TexturePreviewCache — `GetProceduralThumb` + entrée GPU

**Files:**
- Modify: `engine/editor/TexturePreviewCache.h`
- Modify: `engine/editor/TexturePreviewCache.cpp`

- [ ] **Step 1: Déclarer struct interne `GpuPreview` et `GetProceduralThumb`**

Dans `engine/editor/TexturePreviewCache.h`, dans la section publique de `TexturePreviewCache` :

```cpp
        /// Renvoie une ImTextureID utilisable par ImGui::Image() pour la
        /// vignette procedurale du layer (0=grass, 1=dirt, 2=rock, 3=snow).
        /// La 1re demande genere + uploade ; les suivantes hit le cache.
        /// Retourne nullptr si Init n'a pas reussi ou layer invalide.
        ImTextureID GetProceduralThumb(uint32_t layer);

        /// Renvoie le buffer CPU 256x256 d'une key cachee (ou nullptr).
        /// Cles : "procedural:0".."procedural:3" pour les builtins,
        ///        "textures/<rel>" pour les .texr importees.
        const std::vector<uint8_t>* GetCpuRgba256(const std::string& key) const;
```

Section privée :

```cpp
        struct GpuPreview {
            std::vector<uint8_t> cpuRgba256;
            VkImage              image       = nullptr;
            VkImageView          view        = nullptr;
            VkDeviceMemory       memory      = nullptr;
            VkDescriptorSet      imguiDS     = nullptr;
        };

        /// key -> preview. Cles cf. GetCpuRgba256.
        std::unordered_map<std::string, GpuPreview> m_entries;

        /// Cle procedurale standardisee.
        static std::string ProceduralKey(uint32_t layer);

        /// Cree image+view+descriptor ImGui a partir d'un buffer 256x256 RGBA8.
        /// Stocke dans m_entries[key]. Renvoie l'ImTextureID ou nullptr.
        ImTextureID CreateEntry(const std::string& key,
                                const std::vector<uint8_t>& rgba256);

        /// Detruit les ressources Vulkan d'une entree (helper interne).
        void DestroyEntry(GpuPreview& p);
```

Ajouter `#include <unordered_map>` en haut du .h.

- [ ] **Step 2: Implémenter `ProceduralKey`, `CreateEntry`, `DestroyEntry`, `GetProceduralThumb`, `GetCpuRgba256` dans le .cpp**

Ajouter `#include <imgui_impl_vulkan.h>` en haut du .cpp (Win32 only — voir step 3 si besoin de garder portable).

```cpp
    std::string TexturePreviewCache::ProceduralKey(uint32_t layer)
    {
        return std::string("procedural:") + std::to_string(layer);
    }

    ImTextureID TexturePreviewCache::CreateEntry(const std::string& key,
                                                  const std::vector<uint8_t>& rgba256)
    {
        constexpr uint32_t kRes = engine::render::terrain::kSplatLayerResolution;
        if (!m_ready || rgba256.size() != static_cast<size_t>(kRes) * kRes * 4u)
        {
            return nullptr;
        }

        GpuPreview preview;
        preview.cpuRgba256 = rgba256;

        // 1. VkImage 256x256 RGBA8_SRGB, OPTIMAL, SAMPLED + TRANSFER_DST.
        VkImageCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = VK_FORMAT_R8G8B8A8_SRGB;
        ici.extent = { kRes, kRes, 1 };
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(m_device, &ici, nullptr, &preview.image) != VK_SUCCESS) return nullptr;

        VkMemoryRequirements mr{};
        vkGetImageMemoryRequirements(m_device, preview.image, &mr);
        VkPhysicalDeviceMemoryProperties mp{};
        vkGetPhysicalDeviceMemoryProperties(m_physDev, &mp);
        uint32_t memType = UINT32_MAX;
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        {
            if ((mr.memoryTypeBits & (1u << i)) &&
                (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            {
                memType = i; break;
            }
        }
        if (memType == UINT32_MAX) { vkDestroyImage(m_device, preview.image, nullptr); return nullptr; }
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = memType;
        if (vkAllocateMemory(m_device, &ai, nullptr, &preview.memory) != VK_SUCCESS)
        {
            vkDestroyImage(m_device, preview.image, nullptr);
            return nullptr;
        }
        vkBindImageMemory(m_device, preview.image, preview.memory, 0);

        // 2. Upload via staging.
        VkBuffer staging = nullptr;
        VkDeviceMemory stagingMem = nullptr;
        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = rgba256.size();
        bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(m_device, &bci, nullptr, &staging);
        VkMemoryRequirements bmr{};
        vkGetBufferMemoryRequirements(m_device, staging, &bmr);
        uint32_t hvis = UINT32_MAX;
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        {
            if ((bmr.memoryTypeBits & (1u << i)) &&
                (mp.memoryTypes[i].propertyFlags &
                  (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                  (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            { hvis = i; break; }
        }
        VkMemoryAllocateInfo ai2{};
        ai2.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai2.allocationSize = bmr.size;
        ai2.memoryTypeIndex = hvis;
        vkAllocateMemory(m_device, &ai2, nullptr, &stagingMem);
        vkBindBufferMemory(m_device, staging, stagingMem, 0);
        void* mapped = nullptr;
        vkMapMemory(m_device, stagingMem, 0, rgba256.size(), 0, &mapped);
        std::memcpy(mapped, rgba256.data(), rgba256.size());
        vkUnmapMemory(m_device, stagingMem);

        // 3. Submit one-shot : barriers + copy.
        VkCommandPool pool = nullptr;
        VkCommandPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.queueFamilyIndex = m_queueFamily;
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        vkCreateCommandPool(m_device, &pci, nullptr, &pool);
        VkCommandBuffer cmd = nullptr;
        VkCommandBufferAllocateInfo cai{};
        cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        vkAllocateCommandBuffers(m_device, &cai, &cmd);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);

        VkImageMemoryBarrier b1{};
        b1.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b1.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b1.image = preview.image;
        b1.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b1.srcAccessMask = 0;
        b1.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b1);

        VkBufferImageCopy r{};
        r.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        r.imageExtent = { kRes, kRes, 1 };
        vkCmdCopyBufferToImage(cmd, staging, preview.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);

        VkImageMemoryBarrier b2 = b1;
        b2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b2);

        vkEndCommandBuffer(cmd);
        VkSubmitInfo sub{};
        sub.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        sub.commandBufferCount = 1; sub.pCommandBuffers = &cmd;
        vkQueueSubmit(m_queue, 1, &sub, nullptr);
        vkQueueWaitIdle(m_queue);

        vkDestroyCommandPool(m_device, pool, nullptr);
        vkDestroyBuffer(m_device, staging, nullptr);
        vkFreeMemory(m_device, stagingMem, nullptr);

        // 4. ImageView.
        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = preview.image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = VK_FORMAT_R8G8B8A8_SRGB;
        vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCreateImageView(m_device, &vci, nullptr, &preview.view);

        // 5. Descriptor ImGui.
        preview.imguiDS = ImGui_ImplVulkan_AddTexture(m_sampler, preview.view,
                                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        const ImTextureID id = static_cast<ImTextureID>(preview.imguiDS);
        m_entries.emplace(key, std::move(preview));
        return id;
    }

    void TexturePreviewCache::DestroyEntry(GpuPreview& p)
    {
        if (p.imguiDS != nullptr)
        {
            ImGui_ImplVulkan_RemoveTexture(p.imguiDS);
            p.imguiDS = nullptr;
        }
        if (p.view != nullptr) { vkDestroyImageView(m_device, p.view, nullptr); p.view = nullptr; }
        if (p.image != nullptr) { vkDestroyImage(m_device, p.image, nullptr); p.image = nullptr; }
        if (p.memory != nullptr) { vkFreeMemory(m_device, p.memory, nullptr); p.memory = nullptr; }
        p.cpuRgba256.clear();
    }

    ImTextureID TexturePreviewCache::GetProceduralThumb(uint32_t layer)
    {
        if (!m_ready || layer >= engine::render::terrain::kSplatLayerCount) return nullptr;
        const std::string key = ProceduralKey(layer);
        auto it = m_entries.find(key);
        if (it != m_entries.end())
        {
            return static_cast<ImTextureID>(it->second.imguiDS);
        }
        std::vector<uint8_t> rgba;
        if (!engine::render::terrain::GenerateProceduralAlbedoLayer(
                engine::render::terrain::kSplatLayerResolution, layer, rgba))
        {
            return nullptr;
        }
        return CreateEntry(key, rgba);
    }

    const std::vector<uint8_t>* TexturePreviewCache::GetCpuRgba256(const std::string& key) const
    {
        auto it = m_entries.find(key);
        if (it == m_entries.end()) return nullptr;
        return &it->second.cpuRgba256;
    }
```

Modifier `Shutdown()` pour nettoyer les entrées :

```cpp
    void TexturePreviewCache::Shutdown()
    {
        for (auto& kv : m_entries) DestroyEntry(kv.second);
        m_entries.clear();
        // ... reste inchange (destroy pool + sampler)
    }
```

- [ ] **Step 3: Vérifier inclusion de `imgui_impl_vulkan.h`**

`ImGui_ImplVulkan_AddTexture` n'est dispo que sur Win32 (cf. `WorldEditorImGui.cpp` qui guarde `#if defined(_WIN32)`). Pour rester portable, encadrer le code Vulkan/ImGui de `TexturePreviewCache.cpp` par `#if defined(_WIN32)` et fournir des stubs no-op sur autres plateformes (les binaires Linux/macOS ne lancent pas l'éditeur).

Pattern : déplacer toute la classe `TexturePreviewCache::*` (sauf le destructeur stub) dans un bloc `#if defined(_WIN32) ... #endif`. Les méthodes hors-Win32 retournent `false`/`nullptr` no-op.

- [ ] **Step 4: Build complet**

Run :
```
cmake --build build --target engine_core --config Release
cmake --build build --target world_editor_app --config Release
```

Expected : compile sans erreur. Si erreur sur `ImGui_ImplVulkan_AddTexture` ou `kSplatLayerCount`, ajouter les includes nécessaires.

- [ ] **Step 5: Commit**

```bash
git add engine/editor/TexturePreviewCache.h engine/editor/TexturePreviewCache.cpp
git commit -m "$(cat <<'EOF'
feat(editor): GetProceduralThumb + creation d'entrees GPU pour vignettes

Permet d'obtenir un ImTextureID pour les 4 procedurales builtin. Le cache
stocke les ressources Vulkan (image+view+memory+ImGui descriptor) en
unordered_map keyed par "procedural:N" ou "textures/<rel>".

Code Vulkan/ImGui Win32-only (l'editeur monde n'est lance que sous Windows
de toute facon, cf. world_editor_main.cpp).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: TexturePreviewCache — `GetTexrThumb` + résolution chemin content

**Files:**
- Modify: `engine/editor/TexturePreviewCache.h`
- Modify: `engine/editor/TexturePreviewCache.cpp`

- [ ] **Step 1: Déclarer `GetTexrThumb`**

Dans la section publique :

```cpp
        /// Renvoie l'ImTextureID d'une .texr content-relative (ex: "textures/sand.texr").
        /// 1re demande : decode (LoadTexrFile) -> resample 256x256 -> upload GPU.
        /// Suivantes : hit cache. Cache negatif (decode failed) = nullptr renvoye
        /// jusqu'a Invalidate.
        /// \return nullptr si Init non OK, fichier introuvable ou corrompu.
        ImTextureID GetTexrThumb(const std::string& contentRelPath);
```

Section privée — ajouter un set de "négatifs" (cache miss permanent jusqu'à Invalidate) :

```cpp
        /// Cles dont le decode a echoue : ne pas retenter avant Invalidate.
        std::unordered_set<std::string> m_negativeCache;
```

Ajouter `#include <unordered_set>` au .h.

- [ ] **Step 2: Implémenter `GetTexrThumb`**

```cpp
    ImTextureID TexturePreviewCache::GetTexrThumb(const std::string& contentRelPath)
    {
        if (!m_ready || contentRelPath.empty()) return nullptr;
        auto it = m_entries.find(contentRelPath);
        if (it != m_entries.end())
        {
            return static_cast<ImTextureID>(it->second.imguiDS);
        }
        if (m_negativeCache.count(contentRelPath) != 0)
        {
            return nullptr; // deja echoue, ne pas spam les logs
        }

        // Resoudre en chemin absolu via m_contentDir.
        std::filesystem::path abs = std::filesystem::path(m_contentDir) / contentRelPath;
        std::vector<uint8_t> raw;
        uint32_t srcW = 0, srcH = 0;
        if (!LoadTexrFile(abs.string(), raw, srcW, srcH))
        {
            m_negativeCache.insert(contentRelPath);
            return nullptr;
        }

        std::vector<uint8_t> resampled;
        if (!ResampleRgba8Box(raw.data(), srcW, srcH,
                              engine::render::terrain::kSplatLayerResolution, resampled))
        {
            LOG_ERROR(Render, "[TexturePreviewCache] ResampleRgba8Box failed for {}", contentRelPath);
            m_negativeCache.insert(contentRelPath);
            return nullptr;
        }

        return CreateEntry(contentRelPath, resampled);
    }
```

- [ ] **Step 3: Build et lancer le world editor pour vérifier qu'on n'a rien cassé**

Run :
```
cmake --build build --target world_editor_app --config Release
pkg/world_editor/lcdlln_world_editor.exe
```

Expected : éditeur démarre normalement. Aucune vignette visible (pas encore branchée).

- [ ] **Step 4: Commit**

```bash
git add engine/editor/TexturePreviewCache.h engine/editor/TexturePreviewCache.cpp
git commit -m "$(cat <<'EOF'
feat(editor): GetTexrThumb (decode + resample + upload .texr importees)

Pipeline de decode pour vignettes des .texr utilisateur :
LoadTexrFile -> ResampleRgba8Box(256) -> CreateEntry. Cache negatif
(unordered_set) pour eviter de re-tenter en boucle un fichier corrompu
jusqu'a Invalidate.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: TexturePreviewCache — `Invalidate` + `Tick` (deferred destruction)

**Files:**
- Modify: `engine/editor/TexturePreviewCache.h`
- Modify: `engine/editor/TexturePreviewCache.cpp`

- [ ] **Step 1: Déclarer `Invalidate` et `Tick`**

Dans la section publique :

```cpp
        /// Marque l'entree pour destruction au prochain Tick(framesInFlight).
        /// Utilise quand l'utilisateur reimporte une .texr (le fichier disque
        /// a change). Procedurales : pas d'invalidation utile (octets fixes).
        void Invalidate(const std::string& contentRelPath);

        /// A appeler chaque frame en main thread. Detruit les entrees en
        /// pending depuis assez longtemps pour qu'aucune command buffer en
        /// vol ne les reference.
        void Tick(uint64_t currentFrameIndex, uint32_t framesInFlight);
```

Section privée :

```cpp
        struct PendingDelete {
            GpuPreview preview;
            uint64_t   frameIndex;
        };
        std::vector<PendingDelete> m_pendingDeletes;
```

- [ ] **Step 2: Implémenter `Invalidate` + `Tick`**

```cpp
    void TexturePreviewCache::Invalidate(const std::string& contentRelPath)
    {
        m_negativeCache.erase(contentRelPath);
        auto it = m_entries.find(contentRelPath);
        if (it == m_entries.end()) return;
        // Differer la destruction (descriptor potentiellement reference dans CB en vol).
        m_pendingDeletes.push_back({ std::move(it->second), 0u /* frameIndex set par Tick */ });
        m_pendingDeletes.back().frameIndex = m_lastTickFrame;
        m_entries.erase(it);
    }

    void TexturePreviewCache::Tick(uint64_t currentFrameIndex, uint32_t framesInFlight)
    {
        m_lastTickFrame = currentFrameIndex;
        // Purge les entrees dont l'invalidation date d'au moins framesInFlight.
        for (auto it = m_pendingDeletes.begin(); it != m_pendingDeletes.end(); )
        {
            if (currentFrameIndex >= it->frameIndex + framesInFlight)
            {
                DestroyEntry(it->preview);
                it = m_pendingDeletes.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
```

Et ajouter dans la section privée `uint64_t m_lastTickFrame = 0;`.

Modifier `Shutdown` pour vider `m_pendingDeletes` aussi :

```cpp
    void TexturePreviewCache::Shutdown()
    {
        for (auto& kv : m_entries) DestroyEntry(kv.second);
        m_entries.clear();
        for (auto& pd : m_pendingDeletes) DestroyEntry(pd.preview);
        m_pendingDeletes.clear();
        m_negativeCache.clear();
        // ... destroy pool + sampler (existant)
    }
```

- [ ] **Step 3: Build complet**

Run :
```
cmake --build build --target engine_core --config Release
```

Expected : compile OK.

- [ ] **Step 4: Commit**

```bash
git add engine/editor/TexturePreviewCache.h engine/editor/TexturePreviewCache.cpp
git commit -m "$(cat <<'EOF'
feat(editor): Invalidate + Tick (destruction differee des descriptors)

Suppression d'une entree (reimport .texr) repoussee de framesInFlight
frames pour eviter UAF sur descriptor reference dans une command buffer
en vol. m_pendingDeletes purge a chaque Tick.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: WorldEditorSession — dirty flag pour splatLayerTextureRefs

**Files:**
- Modify: `engine/editor/WorldEditorSession.h`
- Modify: `engine/editor/WorldEditorSession.cpp`

- [ ] **Step 1: Ajouter le dirty flag et les accessors**

Dans `engine/editor/WorldEditorSession.h`, dans la section publique :

```cpp
        /// Marque les references de textures de layer (splatLayerTextureRefs)
        /// comme modifiees. A appeler par les UIs apres avoir change un
        /// element du tableau. Engine::ProcessSplatRefsDirty consomme ce flag
        /// chaque frame pour reuploader le splat array GPU.
        void MarkSplatRefsDirty() { m_splatRefsDirty = true; }
        bool ConsumeSplatRefsDirty() { const bool d = m_splatRefsDirty; m_splatRefsDirty = false; return d; }
```

Section privée :

```cpp
        bool m_splatRefsDirty = false;
```

- [ ] **Step 2: Marquer dirty au load de carte**

Dans `engine/editor/WorldEditorSession.cpp`, à la fin de la fonction qui charge une carte (chercher `LoadMap` ou `OpenZone` selon ce qui existe ; sinon dans `ApplyLoadedDocument` ou équivalent), ajouter :

```cpp
        m_splatRefsDirty = true;  // au load, on doit reuploader le splat array si refs presentes.
```

Egalement dans la fonction qui crée une nouvelle carte vierge (mêmes refs vides → procédurales) :

```cpp
        m_splatRefsDirty = true;
```

(Si la fonction n'est pas évidente : `ActionNewMap` / `ActionLoadMap`. Chercher avec `git grep -n splatLayerTextureRefs engine/editor/WorldEditorSession.cpp` pour identifier les sites de mutation et le load.)

- [ ] **Step 3: Build**

Run :
```
cmake --build build --target engine_core --config Release
```

- [ ] **Step 4: Commit**

```bash
git add engine/editor/WorldEditorSession.h engine/editor/WorldEditorSession.cpp
git commit -m "$(cat <<'EOF'
feat(editor): dirty flag splatLayerTextureRefs (consomme par Engine)

Permet aux UIs Peindre / Bibliotheque de signaler un changement de
mapping texture-layer ; Engine consomme le flag chaque frame pour
declencher le reupload du splat array GPU. Marque aussi dirty au load
d'une carte (pour appliquer les refs persistees apres reboot).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 11: Engine — possession du cache + ProcessSplatRefsDirty

**Files:**
- Modify: `engine/Engine.h`
- Modify: `engine/Engine.cpp`

- [ ] **Step 1: Déclarer le membre + la méthode privée dans `Engine.h`**

Ajouter dans la section privée de la classe `Engine` :

```cpp
        std::unique_ptr<engine::editor::TexturePreviewCache> m_texturePreviewCache;

        /// Si WorldEditorSession::ConsumeSplatRefsDirty() == true, repack les
        /// 4 layers (procedural fallback + textures importees via le cache)
        /// dans m_terrainSplatting et reuploade le GPU array.
        /// A appeler chaque frame en world-editor mode, apres les autres ticks.
        void ProcessSplatRefsDirty();
```

Ajouter `#include "engine/editor/TexturePreviewCache.h"` aux includes du header (ou en forward decl si possible).

- [ ] **Step 2: Init du cache dans Engine**

Dans `engine/Engine.cpp`, dans la fonction d'init (après l'init Vulkan, l'init TerrainRenderer, et l'init WorldEditorImGui) — chercher l'endroit où `m_worldEditorImGui->Init(...)` est appelé, et ajouter juste après :

```cpp
        if (m_worldEditorExe)
        {
            m_texturePreviewCache = std::make_unique<engine::editor::TexturePreviewCache>();
            const std::string contentDir = m_cfg.GetString("paths.content", "game/data");
            std::filesystem::path absContent = std::filesystem::absolute(contentDir);
            if (!m_texturePreviewCache->Init(m_vkDeviceContext.GetDevice(),
                                              m_vkDeviceContext.GetPhysicalDevice(),
                                              m_vkDeviceContext.GetGraphicsQueue(),
                                              m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
                                              absContent.string()))
            {
                LOG_WARN(Render, "[Engine] TexturePreviewCache init failed -- vignettes editeur indisponibles");
                m_texturePreviewCache.reset();
            }
        }
```

(Adapter les noms exacts de méthodes `m_vkDeviceContext.GetXxx` selon ce qui existe — `Grep` `class VkDeviceContext` pour confirmer.)

Et dans la fonction Shutdown (avant la destruction de la device) :

```cpp
        if (m_texturePreviewCache) m_texturePreviewCache->Shutdown();
        m_texturePreviewCache.reset();
```

- [ ] **Step 3: Implémenter `ProcessSplatRefsDirty`**

Ajouter une méthode dans `engine/Engine.cpp` :

```cpp
    void Engine::ProcessSplatRefsDirty()
    {
        if (!m_worldEditorExe || !m_worldEditorSession) return;
        if (!m_texturePreviewCache || !m_texturePreviewCache->IsReady()) return;
        if (!m_worldEditorSession->ConsumeSplatRefsDirty()) return;
        if (!m_terrainSplatting.IsValid()) return;  // adapter selon nom du membre TerrainSplatting

        // Pour chaque layer : pousser le buffer CPU adequat dans TerrainSplatting.
        const auto& refs = m_worldEditorSession->Doc().splatLayerTextureRefs;
        for (uint32_t layer = 0; layer < engine::render::terrain::kSplatLayerCount; ++layer)
        {
            std::string key;
            const std::vector<uint8_t>* rgba = nullptr;
            if (!refs[layer].empty())
            {
                // Force la decode/upload (cree l'entree si pas encore demandee).
                m_texturePreviewCache->GetTexrThumb(refs[layer]);
                rgba = m_texturePreviewCache->GetCpuRgba256(refs[layer]);
            }
            if (rgba == nullptr)
            {
                // Fallback procedural : assure que l'entree procedurale existe en cache.
                m_texturePreviewCache->GetProceduralThumb(layer);
                rgba = m_texturePreviewCache->GetCpuRgba256(
                    "procedural:" + std::to_string(layer));
            }
            if (rgba != nullptr)
            {
                m_terrainSplatting.SetLayerCpuRgba256(layer, *rgba);
            }
        }

        if (!m_terrainSplatting.RebuildAlbedoArrayFromCpuLayers(
                m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(),
                m_vkDeviceContext.GetGraphicsQueue(),
                m_vkDeviceContext.GetGraphicsQueueFamilyIndex()))
        {
            LOG_ERROR(Render, "[Engine] Splat array rebuild failed");
        }
    }
```

**ATTENTION nom du membre TerrainSplatting :** vérifier d'abord avec `Grep "TerrainSplatting m_"` ou `Grep "TerrainRenderer m_"` dans `Engine.h`. Si TerrainSplatting est dans TerrainRenderer (comme le suggère TerrainRenderer.h), utiliser `m_terrainRenderer.GetSplatting().SetLayerCpuRgba256(...)` à la place.

- [ ] **Step 4: Appeler `ProcessSplatRefsDirty` chaque frame**

Chercher dans `Engine::Run()` ou `Engine::FrameUpdate()` (ou équivalent) l'endroit après l'update du `TerrainEditingTools` (qui flush la heightmap éditée), et ajouter :

```cpp
        ProcessSplatRefsDirty();
        if (m_texturePreviewCache)
        {
            m_texturePreviewCache->Tick(m_frameCounter, kMaxFramesInFlight);
        }
```

`m_frameCounter` et `kMaxFramesInFlight` : ajuster selon les conventions existantes (`Grep "frameIndex" engine/Engine.cpp`).

- [ ] **Step 5: Build complet**

Run :
```
cmake --build build --target engine_core world_editor_app --config Release
```

Expected : compile OK. Si erreur sur un nom membre, corriger et relancer.

- [ ] **Step 6: Smoke test manuel**

Lancer `lcdlln_world_editor.exe`. Aucun changement visuel attendu (pas encore d'UI). Vérifier dans les logs que `[TexturePreviewCache] Init OK` apparaît. Si crash au boot, débugger avant de continuer.

- [ ] **Step 7: Commit**

```bash
git add engine/Engine.h engine/Engine.cpp
git commit -m "$(cat <<'EOF'
feat(editor): Engine possede TexturePreviewCache + ProcessSplatRefsDirty

Le cache vit le temps du device Vulkan. Chaque frame :
1. Tick(framesInFlight) purge les descriptors invalides.
2. ProcessSplatRefsDirty consomme le flag de WorldEditorSession et
   reuploade l'albedo array (procedurales fallback pour les layers sans
   ref, texr resamplee pour les autres).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 12: Vignettes inline dans l'onglet "Peindre"

**Files:**
- Modify: `engine/editor/WorldEditorImGui.h`
- Modify: `engine/editor/WorldEditorImGui.cpp`

- [ ] **Step 1: Ajouter setter du cache dans `WorldEditorImGui`**

Dans `engine/editor/WorldEditorImGui.h`, section publique :

```cpp
        /// Branche le cache de vignettes (possede par Engine). Pointeur non
        /// possede. Si nul, les vignettes sont rendues comme cellules grises.
        void SetTexturePreviewCache(engine::editor::TexturePreviewCache* cache) { m_texturePreviewCache = cache; }
```

Section privée :

```cpp
        engine::editor::TexturePreviewCache* m_texturePreviewCache = nullptr;
        bool m_showTextureLibrary = false;
```

Forward decl : `namespace engine::editor { class TexturePreviewCache; }` en haut du header.

- [ ] **Step 2: Brancher le setter dans `Engine.cpp`**

Après `m_worldEditorImGui->Init(...)` et après l'init du cache :

```cpp
        if (m_worldEditorImGui && m_texturePreviewCache)
        {
            m_worldEditorImGui->SetTexturePreviewCache(m_texturePreviewCache.get());
        }
```

- [ ] **Step 3: Modifier l'onglet "Peindre" pour afficher des vignettes inline**

Dans `engine/editor/WorldEditorImGui.cpp`, dans le bloc `if (ImGui::CollapsingHeader("Textures personnalisees (par couche)"))` (ligne ~1211), remplacer le `for (int li = 0; li < 4; ++li) { Combo... }` par :

```cpp
                            for (int li = 0; li < 4; ++li)
                            {
                                // Vignette 48x48 a gauche du combo.
                                ImTextureID thumb = nullptr;
                                if (m_texturePreviewCache != nullptr)
                                {
                                    if (refs[static_cast<size_t>(li)].empty())
                                    {
                                        thumb = m_texturePreviewCache->GetProceduralThumb(static_cast<uint32_t>(li));
                                    }
                                    else
                                    {
                                        thumb = m_texturePreviewCache->GetTexrThumb(refs[static_cast<size_t>(li)]);
                                    }
                                }
                                if (thumb != nullptr)
                                {
                                    ImGui::Image(thumb, ImVec2(48.0f, 48.0f));
                                }
                                else
                                {
                                    ImGui::Dummy(ImVec2(48.0f, 48.0f));  // placeholder gris
                                }
                                ImGui::SameLine();

                                int sel = 0;
                                if (!refs[static_cast<size_t>(li)].empty())
                                {
                                    for (size_t i = 0; i < imported.size(); ++i)
                                    {
                                        if (imported[i] == refs[static_cast<size_t>(li)])
                                        {
                                            sel = static_cast<int>(i + 1);
                                            break;
                                        }
                                    }
                                }
                                char lbl[32];
                                std::snprintf(lbl, sizeof(lbl), "%s##splatTex%d", layers[li], li);
                                if (ImGui::Combo(lbl, &sel, itemsZ.c_str()))
                                {
                                    if (sel <= 0)
                                    {
                                        refs[static_cast<size_t>(li)].clear();
                                    }
                                    else if (static_cast<size_t>(sel - 1) < imported.size())
                                    {
                                        refs[static_cast<size_t>(li)] = imported[static_cast<size_t>(sel - 1)];
                                    }
                                    m_session->MarkSplatRefsDirty();
                                }
                            }
```

Includes nécessaires en haut du .cpp : `#include "engine/editor/TexturePreviewCache.h"`.

- [ ] **Step 4: Build et test manuel**

Run :
```
cmake --build build --target world_editor_app --config Release
pkg/world_editor/lcdlln_world_editor.exe
```

Lancer une carte, ouvrir onglet Peindre, replier `Textures personnalisees (par couche)`. Attendu :
- 4 lignes avec vignette 48×48 (couleurs grass/dirt/rock/snow visibles à l'œil) à gauche du combo.
- Si on importe un PNG, qu'on le sélectionne dans un combo : la vignette inline change pour le PNG.
- Si on revient à `(par defaut moteur)` : vignette procédurale revient.

- [ ] **Step 5: Commit**

```bash
git add engine/editor/WorldEditorImGui.h engine/editor/WorldEditorImGui.cpp engine/Engine.cpp
git commit -m "$(cat <<'EOF'
feat(editor): vignettes inline dans l'onglet Peindre

Chaque layer (Herbe/Terre/Roc/Neige) affiche une vignette 48x48 a gauche
du combo. Default moteur = procedurale ; selection = .texr resamplee.
MarkSplatRefsDirty appele a chaque modif de combo -> Engine reuploade le
splat array a la frame suivante.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 13: TextureLibraryPanel — squelette du panneau dockable

**Files:**
- Create: `engine/editor/TextureLibraryPanel.h`
- Create: `engine/editor/TextureLibraryPanel.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Créer le header**

```cpp
// engine/editor/TextureLibraryPanel.h
#pragma once

namespace engine::editor
{
    class WorldEditorSession;
    class TexturePreviewCache;

    /// Dessine le panneau ImGui "Bibliotheque de textures" (Win32 uniquement,
    /// no-op ailleurs). Affiche les 4 procedurales builtin + toutes les .texr
    /// importees, avec assignation au layer actif (m_session->SplatLayer())
    /// au clic. Dirty propage via MarkSplatRefsDirty.
    /// \param session Session editeur active (Doc().textureAssets, SplatLayer()).
    /// \param cache Cache de vignettes (peut etre nul -> grilles d'attente).
    /// \param openFlag Flag controlant l'ouverture du panneau ; modifie par la
    ///   case de fermeture ImGui ou par l'item de menu (cf. WorldEditorImGui).
    void DrawTextureLibrary(WorldEditorSession& session,
                            TexturePreviewCache* cache,
                            bool& openFlag);

} // namespace engine::editor
```

- [ ] **Step 2: Créer l'implémentation (Win32-only)**

```cpp
// engine/editor/TextureLibraryPanel.cpp
#include "engine/editor/TextureLibraryPanel.h"

#include "engine/editor/WorldEditorSession.h"
#include "engine/editor/TexturePreviewCache.h"
#include "engine/editor/WorldMapEditDocument.h"

#if defined(_WIN32)
#   include "imgui.h"
#endif

#include <array>
#include <cstdio>

namespace engine::editor
{
#if defined(_WIN32)
    namespace
    {
        constexpr float kThumbSize = 96.0f;
        constexpr int   kColumnsPerRow = 5;

        /// Dessine une vignette + label cliquable. Renvoie true si cliquee.
        /// \param highlighted Si true, encadre la vignette d'un liserer accent.
        bool DrawThumbButton(ImTextureID id, const char* label, bool highlighted)
        {
            const ImVec2 size(kThumbSize, kThumbSize);
            ImGui::BeginGroup();
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, highlighted ? 3.0f : 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(80, 180, 255, 255));
            bool clicked = false;
            if (id != nullptr)
            {
                clicked = ImGui::ImageButton(label, id, size);
            }
            else
            {
                clicked = ImGui::Button(label, ImVec2(kThumbSize, kThumbSize));
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            ImGui::TextWrapped("%s", label);
            ImGui::EndGroup();
            return clicked;
        }
    } // namespace

    void DrawTextureLibrary(WorldEditorSession& session,
                            TexturePreviewCache* cache,
                            bool& openFlag)
    {
        if (!openFlag) return;
        if (!ImGui::Begin("Bibliotheque de textures", &openFlag))
        {
            ImGui::End();
            return;
        }

        // Header : layer actif (radio sync sur SplatLayer()).
        static const char* kLayerNames[4] = { "Herbe", "Terre", "Roc", "Neige" };
        int& activeLayer = session.SplatLayer();
        activeLayer = std::clamp(activeLayer, 0, 3);
        ImGui::TextUnformatted("Layer actif :");
        for (int i = 0; i < 4; ++i)
        {
            ImGui::SameLine();
            if (ImGui::RadioButton(kLayerNames[i], activeLayer == i))
            {
                activeLayer = i;
            }
        }
        ImGui::Separator();

        // Section 1 : procedurales builtin.
        ImGui::TextUnformatted("Procedurales (par defaut moteur)");
        for (int li = 0; li < 4; ++li)
        {
            ImGui::PushID(li);
            ImTextureID id = (cache != nullptr) ? cache->GetProceduralThumb(static_cast<uint32_t>(li)) : nullptr;
            const bool highlighted = (li == activeLayer)
                && session.Doc().splatLayerTextureRefs[static_cast<size_t>(li)].empty();
            if (DrawThumbButton(id, kLayerNames[li], highlighted))
            {
                // Clic sur procedural = reset ref a "" pour le layer actif.
                session.MutableDoc().splatLayerTextureRefs[static_cast<size_t>(activeLayer)].clear();
                session.MarkSplatRefsDirty();
            }
            ImGui::PopID();
            if (li < 3) ImGui::SameLine();
        }
        ImGui::Separator();

        // Section 2 : importees.
        const auto& assets = session.Doc().textureAssets;
        ImGui::Text("Importees (%zu)", assets.size());
        if (assets.empty())
        {
            ImGui::TextDisabled("(aucune) — Importez via 'Import assets'");
        }
        else
        {
            int col = 0;
            for (size_t i = 0; i < assets.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                ImTextureID id = (cache != nullptr) ? cache->GetTexrThumb(assets[i]) : nullptr;
                // Highlight si cette texture est assignee au layer actif.
                const bool highlighted = (session.Doc().splatLayerTextureRefs[static_cast<size_t>(activeLayer)] == assets[i]);
                // Label court : basename
                std::string baseLabel = assets[i];
                const size_t slash = baseLabel.find_last_of("/\\");
                if (slash != std::string::npos) baseLabel = baseLabel.substr(slash + 1);
                if (DrawThumbButton(id, baseLabel.c_str(), highlighted))
                {
                    session.MutableDoc().splatLayerTextureRefs[static_cast<size_t>(activeLayer)] = assets[i];
                    session.MarkSplatRefsDirty();
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", assets[i].c_str());
                }
                ImGui::PopID();
                if (++col % kColumnsPerRow != 0 && i + 1 < assets.size()) ImGui::SameLine();
            }
        }

        ImGui::Separator();
        ImGui::TextDisabled("Cliquez une vignette pour l'assigner au layer actif (%s).", kLayerNames[activeLayer]);
        ImGui::End();
    }
#else
    void DrawTextureLibrary(WorldEditorSession&, TexturePreviewCache*, bool&) {}
#endif
} // namespace engine::editor
```

**ATTENTION :** `WorldEditorSession::SplatLayer()` doit retourner `int&` (référence mutable) pour que les radios fonctionnent. Vérifier avec `Grep "int& SplatLayer\|int SplatLayer"` ; sinon ajouter une surcharge ou changer la signature.

- [ ] **Step 3: Ajouter le .cpp à `engine_core` dans CMakeLists.txt**

Dans `CMakeLists.txt`, ajouter à la liste des sources de `engine_core`, après `TexturePreviewCache.cpp` :

```cmake
  engine/editor/TexturePreviewCache.cpp
  engine/editor/TextureLibraryPanel.cpp
```

- [ ] **Step 4: Build**

Run :
```
cmake --build build --target engine_core --config Release
```

Expected : compile OK. Aucun changement visuel — le panneau n'est pas encore appelé.

- [ ] **Step 5: Commit**

```bash
git add engine/editor/TextureLibraryPanel.h engine/editor/TextureLibraryPanel.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(editor): squelette du panneau Bibliotheque de textures

Fonction libre DrawTextureLibrary(session, cache, openFlag) — affiche les
4 procedurales builtin + toutes les .texr importees avec assignation au
layer actif au clic. Highlight de la vignette deja assignee. Pas encore
ouvert depuis WorldEditorImGui.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 14: Brancher le panneau depuis WorldEditorImGui (menu Affichage)

**Files:**
- Modify: `engine/editor/WorldEditorImGui.cpp`

- [ ] **Step 1: Ajouter l'item de menu "Affichage > Bibliothèque de textures"**

Dans `engine/editor/WorldEditorImGui.cpp`, chercher la barre de menu existante (`Grep "BeginMainMenuBar\|BeginMenuBar"`) ou si elle n'existe pas, ajouter au début de `BuildUi` un menu "Affichage" :

```cpp
            if (ImGui::BeginMainMenuBar())
            {
                if (ImGui::BeginMenu("Affichage"))
                {
                    ImGui::MenuItem("Bibliotheque de textures", nullptr, &m_showTextureLibrary);
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }
```

(Si une barre de menu existe déjà avec un menu "Affichage", ajouter juste l'item dedans.)

- [ ] **Step 2: Appeler `DrawTextureLibrary` quand `m_showTextureLibrary == true`**

Dans `BuildUi`, après les autres `ImGui::Begin/End` (ex: après "Import assets"), ajouter :

```cpp
            if (m_showTextureLibrary && m_session != nullptr)
            {
                engine::editor::DrawTextureLibrary(*m_session, m_texturePreviewCache, m_showTextureLibrary);
            }
```

Include : `#include "engine/editor/TextureLibraryPanel.h"` en haut du .cpp.

- [ ] **Step 3: Build et test manuel**

Run :
```
cmake --build build --target world_editor_app --config Release
pkg/world_editor/lcdlln_world_editor.exe
```

Expected :
- Barre de menu en haut affiche "Affichage > Bibliotheque de textures" cochable.
- Cocher → panneau dockable apparaît avec les 4 vignettes procédurales en haut + section "Importées (0)".
- Importer un PNG via "Import assets" → la vignette apparaît dans "Importées".
- Cliquer une vignette importée → terrain 3D mis à jour, vignette encadrée d'un liseré bleu.
- Cliquer une procédurale → terrain revient à la procédurale pour ce layer.

- [ ] **Step 4: Commit**

```bash
git add engine/editor/WorldEditorImGui.cpp
git commit -m "$(cat <<'EOF'
feat(editor): menu 'Affichage > Bibliotheque de textures' + panneau live

Toggle via la barre de menu, panneau dockable. Synchronise avec le combo
'Type de sol' de Peindre (m_session->SplatLayer()). Click vignette =
assignation au layer actif + dirty splat refs -> reupload GPU live.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 15: Invalider le cache au ré-import d'une texture

**Files:**
- Modify: `engine/editor/WorldEditorSession.h`
- Modify: `engine/editor/WorldEditorSession.cpp`
- Modify: `engine/Engine.cpp`

- [ ] **Step 1: Émettre un signal post-import**

Dans `engine/editor/WorldEditorSession.h`, ajouter une queue de noms de textures réimportées :

```cpp
        /// Liste des .texr (chemins content-relatifs) reimportees depuis la
        /// derniere consommation. Engine::Frame() consomme via
        /// ConsumeRecentlyImportedTextures et appelle TexturePreviewCache::Invalidate.
        const std::vector<std::string>& RecentlyImportedTextures() const { return m_recentlyImported; }
        void ClearRecentlyImportedTextures() { m_recentlyImported.clear(); }
```

Section privée :

```cpp
        std::vector<std::string> m_recentlyImported;
```

Dans `WorldEditorSession::ActionImportTexture` (déjà identifiée plus haut, ligne ~602), juste avant `return true;` à la fin (le succès), ajouter :

```cpp
        m_recentlyImported.push_back(rel);
        // Si la texture est referencee par un layer, on doit aussi reuploader le splat array.
        for (const std::string& r : m_doc.splatLayerTextureRefs)
        {
            if (r == rel) { m_splatRefsDirty = true; break; }
        }
```

- [ ] **Step 2: Consommer côté Engine**

Dans `Engine::Frame()` (juste avant `ProcessSplatRefsDirty`) :

```cpp
        if (m_worldEditorExe && m_worldEditorSession && m_texturePreviewCache)
        {
            for (const std::string& rel : m_worldEditorSession->RecentlyImportedTextures())
            {
                m_texturePreviewCache->Invalidate(rel);
            }
            m_worldEditorSession->ClearRecentlyImportedTextures();
        }
```

- [ ] **Step 3: Build et test manuel**

Run :
```
cmake --build build --target world_editor_app --config Release
pkg/world_editor/lcdlln_world_editor.exe
```

Test : importer `texture1.png`, l'assigner au layer Terre, modifier `texture1.png` sur disque (changer la couleur), réimporter avec le même nom. Attendu : la vignette ET le terrain 3D se mettent à jour avec la nouvelle version.

- [ ] **Step 4: Commit**

```bash
git add engine/editor/WorldEditorSession.h engine/editor/WorldEditorSession.cpp engine/Engine.cpp
git commit -m "$(cat <<'EOF'
fix(editor): invalide le cache vignette quand une .texr est reimportee

ActionImportTexture pousse le chemin reimporte dans une file consommee
chaque frame par Engine -> TexturePreviewCache::Invalidate. Si la texture
etait deja referencee par un layer, force aussi un reupload du splat
array pour que la 3D refete la nouvelle version.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 16: Validation manuelle complète + documentation

**Files:**
- Modify: `docs/world_editor_zone_pipeline.md`

- [ ] **Step 1: Test manuel exhaustif (cf. spec section 5.3)**

Lancer `lcdlln_world_editor.exe` et exécuter le checklist :

1. ☐ Carte vierge → onglet Peindre montre 4 vignettes procédurales (couleurs grass/dirt/rock/snow visibles).
2. ☐ Menu Affichage > Bibliotheque de textures → panneau s'ouvre, les 4 procédurales apparaissent.
3. ☐ Importer `test_sand.png` (1024×1024) → apparaît dans Bibliothèque sous "Importées" en <500 ms.
4. ☐ Sélectionner layer Terre dans Peindre → radio Bibliothèque suit (Terre coché).
5. ☐ Cliquer la vignette `test_sand` dans la Bibliothèque → terrain 3D affiche le sable sur les zones dirt en <1 frame, vignette encadrée bleu.
6. ☐ Re-cliquer sur la vignette procédurale "Terre" dans la Bibliothèque → terrain 3D revient à la procédurale dirt.
7. ☐ Sauvegarder la carte (`File > Save` ou raccourci) → ouvrir `map.lcdlln_edit.json`, vérifier `splat_layer_texture_refs[1] == "textures/test_sand.texr"`.
8. ☐ Quitter l'éditeur, le relancer, ouvrir la même carte → terrain 3D affiche directement `test_sand` sur Terre sans interaction.
9. ☐ Modifier `test_sand.png` source (changer la teinte), réimporter avec le même nom → vignette ET terrain 3D se rafraîchissent, pas de crash.
10. ☐ Importer 5 textures différentes, en assigner à 4 layers → terrain 3D affiche un vrai mix multi-couches.

Si un test échoue, fix avant de continuer.

- [ ] **Step 2: Mettre à jour `docs/world_editor_zone_pipeline.md`**

Ajouter une section à la fin du fichier :

```markdown
## Apercus de textures et application live

Depuis 2026-05-04, l'editeur affiche des vignettes ImGui pour les
textures de splatting et applique les .texr importees au terrain en
direct.

### Resolution interne

Le splat array (`TerrainSplatting::m_albedoArray`) est maintenant en
**256x256** (constante `kSplatLayerResolution`), promu depuis 64x64. Les
4 layers builtin (grass/dirt/rock/snow) sont generes par bruit
deterministe via la fonction libre `GenerateProceduralAlbedoLayer`, et
les .texr importees sont resamplees en 256x256 (box filter, crop centre
si non-carre).

### Cache de vignettes

`engine::editor::TexturePreviewCache` (possede par `Engine`) decode et
uploade les textures sur demande, cree un `VkDescriptorSet` ImGui via
`ImGui_ImplVulkan_AddTexture`, et gere la destruction differee (pour
eviter UAF sur descriptors en vol).

Les vignettes sont visibles dans :

- **Onglet Peindre > Textures personnalisees (par couche)** : 48x48 inline a gauche de chaque combo de layer.
- **Panneau Bibliotheque de textures** (menu Affichage) : grille 96x96, 4 procedurales + toutes les .texr importees.

### Flow d'application live

Quand l'utilisateur change un combo (Peindre) ou clique une vignette
(Bibliotheque), `WorldEditorSession::splatLayerTextureRefs` est mis a
jour et `MarkSplatRefsDirty()` appele. A la frame suivante,
`Engine::ProcessSplatRefsDirty` :

1. Pour chaque layer, recupere le buffer CPU 256x256 via le cache (procedural si ref vide, .texr resamplee sinon ; fallback procedural si .texr introuvable).
2. Pousse les 4 buffers dans `TerrainSplatting::SetLayerCpuRgba256`.
3. Appelle `RebuildAlbedoArrayFromCpuLayers` -> staging + barrier + `vkCmdCopyBufferToImage` -> retour `SHADER_READ_ONLY`.

Le cycle complet prend ~3-5 ms hors stalle GPU, executé hors framepath
hot (uniquement quand un combo change).

### Reimport d'une texture

`WorldEditorSession::ActionImportTexture` ajoute le chemin re-importe
dans `m_recentlyImported`. `Engine::Frame` consomme la file et appelle
`TexturePreviewCache::Invalidate`. La destruction du descriptor est
differee de `kMaxFramesInFlight` frames (cf. `Tick`) pour eviter d'invalider un
descriptor reference dans une command buffer en vol.

### Limites

- Resolution source max : 4096x4096 (au-dela : refus du decode, vignette grise).
- Capacite cache : 64 entrees (via le descriptor pool dedie). Au-dela, les vignettes ulterieures ne sont pas allouees (warning log).
- Pas de mipmaps (pas necessaire au tiling 8m+).
- Pas de support normal/ORM importe : `m_normalArray` et `m_ormArray` restent placeholders (flat normal + roughness fixe).
```

- [ ] **Step 3: Commit**

```bash
git add docs/world_editor_zone_pipeline.md
git commit -m "$(cat <<'EOF'
docs(world-editor): apercus de textures + application live au terrain

Section "Apercus de textures et application live" qui explique :
- promotion 64->256 du splat array
- cache de vignettes (TexturePreviewCache)
- flow d'application live des .texr aux layers du terrain
- reimport et invalidation du cache
- limites (4096 max source, 64 entrees max cache)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-review

Spec coverage check (sections de la spec ↔ tasks) :

| Spec | Task |
|---|---|
| Architecture §1.1 (TexturePreviewCache) | Task 6, 7, 8, 9 |
| Architecture §1.2 (TerrainSplatting extension 256+rebuild) | Task 2, 5 |
| Architecture §1.3 (TextureLibraryPanel) | Task 13 |
| Architecture §1.4 (WorldEditorImGui inline + WorldEditorSession dirty + Engine wiring) | Task 10, 11, 12, 14 |
| UX §2.1 (vignettes inline Peindre) | Task 12 |
| UX §2.2 (panneau Bibliotheque) | Task 13 |
| UX §2.3 (sync layer actif) | Task 13 (radios sur SplatLayer) |
| UX §2.4 (menu Affichage) | Task 14 |
| UX §2 Flux import | Task 15 |
| Data flow §3.1 (procedurale free function) | Task 1 |
| Data flow §3.2 (resampling box filter) | Task 3 |
| Data flow §3.3 (cycle vie Vulkan + deferred destruction) | Task 6, 7, 9 |
| Data flow §3.4 (repack + reupload) | Task 5 |
| Erreurs §4 | Task 4 (LoadTexrFile robust), 7 (CreateEntry fail), 9 (Invalidate negative cache) |
| Tests §5.1 (unitaires) | Task 3 (ResampleRgba8Box, GenerateProcedural), Task 4 (LoadTexrRoundTrip) |
| Tests §5.3 (manuels) | Task 16 |
| Tests §5.5 (doc CLAUDE.md) | Toutes les tasks (commentaires `///` Doxygen sur fonctions ajoutées) |

Pas de gap.

**Placeholder scan :** RAS — chaque task a du code complet, pas de "TODO", pas de "implement later".

**Type consistency :** vérifié `kSplatLayerResolution`, `kSplatLayerCount`, `MarkSplatRefsDirty`, `ConsumeSplatRefsDirty`, `GetProceduralThumb`, `GetTexrThumb`, `Invalidate`, `Tick`, `SetLayerCpuRgba256`, `RebuildAlbedoArrayFromCpuLayers` — noms cohérents entre toutes les tasks.

**Caveats à valider au moment de l'exécution :**
- `WorldEditorSession::SplatLayer()` doit renvoyer `int&` (mutable) pour que les radios du panneau Bibliothèque marchent. Si c'est `int` (valeur), Task 13 nécessitera un setter explicite (à ajouter dans Task 10 si besoin).
- L'instance `TerrainSplatting` côté Engine est probablement à l'intérieur de `TerrainRenderer` (cf. `TerrainRenderer::GetSplatting()`). Task 11 devra utiliser `m_terrainRenderer.GetSplatting()` au lieu de `m_terrainSplatting` direct.
- `STB_IMAGE_IMPLEMENTATION` peut déjà être défini ailleurs dans le repo : Task 4 step 2 demande de vérifier avec `Grep` avant d'ajouter le define.
