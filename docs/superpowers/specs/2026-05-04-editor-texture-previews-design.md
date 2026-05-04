# Aperçus de textures dans l'éditeur monde — design

Date : 2026-05-04
Auteur : Hubert Cornet (avec assistance Claude)
Branche cible : `claude/editor-terrain-textures-skybox`

## Contexte

L'éditeur monde (`lcdlln_world_editor.exe`) gère le splatting du terrain via 4 layers
(grass / dirt / rock / snow) packés dans un texture array Vulkan. Les 4 textures
par défaut sont des bruits procéduraux 64×64 générés au boot dans
`TerrainSplatting::Init` (commit `b93a14d`).

L'éditeur permet déjà :

- d'importer un PNG/JPG/TGA/BMP via le panneau « Import assets » (conversion en
  `.texr` magic TEXR + RGBA8 + write `<content>/textures/<nom>.texr`),
- de mapper une `.texr` à chacun des 4 layers via `splatLayerTextureRefs[4]`
  (persisté en JSON, clé `splat_layer_texture_refs`).

Deux manques limitent l'UX :

1. **Aucun aperçu visuel.** Les textures importées sont listées en `BulletText`
   (juste leur nom). Le mapper ne sait pas à quoi ressemble `wet_sand.texr` sans
   ouvrir le fichier hors de l'éditeur.
2. **Le mapping n'est pas appliqué au rendu.** `splatLayerTextureRefs` est
   sauvegardé mais jamais relu au runtime. Le terrain 3D affiche toujours les
   procédurales par défaut, indépendamment de ce qui est mappé.

## Objectif

Combler les deux manques :

1. **Vignettes ImGui** dans l'éditeur pour visualiser les 4 procédurales et
   toutes les `.texr` importées.
2. **Application au rendu** : quand un layer est mappé à une texture importée,
   le terrain 3D doit afficher cette texture dans la viewport sans relancer
   l'éditeur.

Hors scope :

- Normalmaps / ORM importés (les arrays `m_normalArray` / `m_ormArray` restent
  des placeholders, comme aujourd'hui).
- Édition de la splat map elle-même (déjà en place via `TerrainEditingTools`).
- Mipmaps / sRGB awareness avancé (le format `R8G8B8A8_SRGB` du sampler
  existant suffit).

## Architecture

Quatre composants, frontières claires.

### `engine::editor::TexturePreviewCache` (nouveau)

Fichiers : `engine/editor/TexturePreviewCache.{h,cpp}`.

Responsabilité unique : transformer un identifiant de texture (procédurale
builtin ou chemin `.texr` content-relatif) en `ImTextureID` ImGui-affichable, et
gérer le cycle de vie Vulkan associé.

API publique :

```cpp
class TexturePreviewCache {
public:
    bool Init(VkDevice, VkPhysicalDevice, VkQueue, uint32_t queueFamily);
    void Shutdown();

    /// 4 procédurales builtin. RGBA8 256×256 généré à la 1re demande.
    ImTextureID GetProceduralThumb(uint32_t layer);  // 0=grass,1=dirt,2=rock,3=snow

    /// .texr content-relatif (ex: "textures/sand.texr"). Décodé+uploadé à la 1re demande.
    /// Retourne nullptr si fichier introuvable/invalide.
    ImTextureID GetTexrThumb(const std::string& contentRelPath);

    /// Une .texr a été réimportée (overwrite sur disque) → invalider l'entrée.
    void Invalidate(const std::string& contentRelPath);

    /// Buffer CPU RGBA8 256×256 d'une entrée cachée. Utilisé par
    /// TerrainSplatting::SetLayerCpuRgba256 lors du rebuild de l'albedo array.
    /// La clé est soit un chemin .texr ("textures/sand.texr") soit
    /// "procedural:0..3".
    const std::vector<uint8_t>* GetCpuRgba256(const std::string& key) const;

    /// À appeler chaque frame en main thread (purge des entrées en deferred-delete).
    void Tick(uint64_t currentFrameIndex, uint32_t framesInFlight);
};
```

Possédé par `Engine`. Pointeur non-owning passé à `WorldEditorImGui` et au
panneau Bibliothèque.

### `engine::render::terrain::TerrainSplatting` (extension)

Promotion de la résolution interne `kSplatLayerResolution = 256` (constante
remplaçant le 64 hardcodé). Génération procédurale au boot adaptée. Ajout de
deux méthodes :

```cpp
/// Stocke le buffer CPU RGBA8 256×256 d'un layer (resample déjà fait par le cache).
void SetLayerCpuRgba256(uint32_t layer, const std::vector<uint8_t>& rgba);

/// Repack les 4 layers CPU en un buffer (4×256×256×4 = 1 MiB) et re-upload via staging.
/// Pattern identique à ReuploadSplatMap (transition layout → copy → retour SHADER_READ_ONLY).
bool RebuildAlbedoArrayFromCpuLayers(VkDevice, VkPhysicalDevice,
                                     VkQueue, uint32_t queueFamily);
```

La lambda interne `GenerateProceduralLayer` devient une **fonction libre
exportée** dans `TerrainSplatting.h` :

```cpp
/// Génère un layer RGBA8 procédural (bruit déterministe par layer).
/// \param resolution Côté en pixels (carré). Min 4.
/// \param layer 0=grass, 1=dirt, 2=rock, 3=snow.
/// \param outRgba Sortie : resolution * resolution * 4 octets.
void GenerateProceduralAlbedoLayer(uint32_t resolution, uint32_t layer,
                                   std::vector<uint8_t>& outRgba);
```

Bénéfice : un seul algo, utilisé par le boot (terrain) et par le cache
(vignettes), même octets bit-pour-bit.

### `engine::editor::TextureLibraryPanel` (nouveau)

Fichiers : `engine/editor/TextureLibraryPanel.{h,cpp}`.

Une fonction libre :

```cpp
void DrawTextureLibrary(WorldEditorSession& session,
                       TexturePreviewCache& cache,
                       bool& openFlag);
```

Ouvre son propre `ImGui::Begin("Bibliothèque de textures", &openFlag)`.
Décorrélée de `WorldEditorImGui` pour éviter de gonfler ce fichier déjà gros
(~1500 lignes).

### Modifications minimales

- **`WorldEditorImGui::BuildUi`** : vignettes inline 48×48 dans l'onglet
  « Peindre » (cf. UX 1) + appel à `DrawTextureLibrary` quand `m_showTextureLibrary == true`.
  Toggle via le menu « Affichage > Bibliothèque de textures ».
- **`WorldEditorSession`** : flag interne `m_splatRefsDirty`, exposé via
  `bool ConsumeSplatRefsDirty()`. Activé par les UIs (Peindre + Bibliothèque)
  quand `splatLayerTextureRefs` change.
- **`Engine`** : possède `TexturePreviewCache`, observe
  `ConsumeSplatRefsDirty()` chaque frame, pousse les buffers CPU au splatting
  et déclenche `RebuildAlbedoArrayFromCpuLayers`.

## UX

### Onglet « Peindre » (étendu)

Sous `CollapsingHeader("Textures personnalisées (par couche)")` (existant) :
pour chaque layer, une vignette 48×48 à gauche du combo.

```
[vignette 48×48] [Combo "Herbe" ▼ : (par défaut moteur) | sand.texr | grass_v2.texr | ...]
[vignette 48×48] [Combo "Terre" ▼ : ...]
[vignette 48×48] [Combo "Roc"   ▼ : ...]
[vignette 48×48] [Combo "Neige" ▼ : ...]
```

Combo « (par défaut moteur) » → vignette de la procédurale du layer. Sinon →
vignette de la `.texr` choisie. Vignette grise si chargement échoué.

### Panneau « Bibliothèque de textures » (nouveau)

Dockable, ouvrable via menu « Affichage > Bibliothèque de textures ».

```
┌─ Bibliothèque de textures ────────────────────────┐
│ Layer actif : ● Herbe   ○ Terre   ○ Roc   ○ Neige │  ← reflète SplatLayer()
│ ─────────────────────────────────────────────────  │
│ Procédurales (par défaut moteur)                   │
│  [grass]  [dirt]  [rock]  [snow]                   │  ← 96×96 vignettes
│                                                    │
│ Importées (5)                                      │
│  [sand]  [grass_v2]  [...]                         │
│                                                    │
│ ─────────────────────────────────────────────────  │
│ ℹ Cliquez une vignette pour l'assigner au layer    │
│   actif (Peindre : Herbe).                         │
└────────────────────────────────────────────────────┘
```

- Vignettes 96×96, label en dessous (nom court). Hover → tooltip avec chemin
  complet + dimensions originales (avant resample 256×256).
- **Clic gauche** → `splatLayerTextureRefs[SplatLayer()] = path` (ou `""` pour
  procédurales). Dirty flags activés → re-upload GPU à la frame suivante.
- **Bordure colorée** sur la vignette actuellement assignée au layer actif
  (liseré accent ImGui).
- **Section « Procédurales »** : 4 vignettes fixes. Clic = remet `""` dans la
  ref → revient au défaut moteur pour ce layer.
- **Section « Importées »** : peuplée depuis `Doc().textureAssets`. Vide =
  ligne d'aide « Importez via le panneau Import assets ».

### Synchronisation du « layer actif »

Les radios dans la Bibliothèque et le combo « Type de sol » dans Peindre
manipulent la même variable (`m_session->SplatLayer()`). Modifier l'un met
l'autre à jour automatiquement à la frame suivante.

### Flux d'import (récap)

1. User clique « Importer cette texture » dans Import assets (existant) →
   `.texr` écrite, entrée ajoutée à `Doc().textureAssets`.
2. Frame suivante : panneau Bibliothèque affiche la nouvelle vignette
   (cache miss → décodage + upload Vulkan + `ImGui_ImplVulkan_AddTexture`).
3. **Pas d'auto-assignation** : la texture apparaît dans la Bibliothèque mais
   le terrain 3D ne change pas tant qu'on ne clique pas dessus.
4. Clic sur la vignette → `splatLayerTextureRefs[active] = "textures/<nom>.texr"`,
   dirty flags. Frame suivante : Engine détecte le dirty, rebuild l'albedo
   array, terrain 3D mis à jour.

Réimport du même nom (overwrite) :
`ActionImportTexture` appelle `cache.Invalidate(rel)` à la fin (succès, fichier
déjà connu). Si la texture est référencée dans `splatLayerTextureRefs`, force
aussi `m_splatRefsDirty = true` → la 3D se rafraîchit.

## Data flow et détails techniques

### Resampling des `.texr` importés

Source : RGBA8 arbitraire. Cible : 256×256 carré.

Algorithme : **box filter séparable** (passe horizontale + verticale, ~3 ms
sur 1024×1024 → 256×256 en CPU). Pas de mipmaps. sRGB conservé via le format
`VK_FORMAT_R8G8B8A8_SRGB` du sampler existant.

Si ratio non-carré (ex: 1920×1080) : **crop centre vers carré** d'abord,
LOG_INFO une fois, puis box filter. Alternative letterboxing rejetée (donnerait
des bandes noires en 3D).

Code dans `TexturePreviewCache.cpp`, fonction libre privée
`ResampleRgba8Box(src, srcW, srcH, dst, dstW, dstH)`. Testable unitairement.

### Cycle de vie Vulkan d'une vignette

Une `GpuPreview` interne au cache contient :

```cpp
struct GpuPreview {
    std::vector<uint8_t> cpuRgba256;  // 256*256*4 = 256 KB
    VkImage              image;
    VkImageView          view;
    VkDeviceMemory       memory;
    VkSampler            sampler;     // partagé statiquement
    VkDescriptorSet      imguiDS;     // ImGui_ImplVulkan_AddTexture
};
```

Création : à la première demande. Submit + `vkQueueWaitIdle` sur la queue de
transfert (one-shot, pattern existant `TerrainSplatting::UploadSplatMap`).
Coût ~2-5 ms par texture.

Destruction (Invalidate / Shutdown) : **différée** pour éviter UAF sur
descriptor référencé en command buffer en vol.

```cpp
m_pendingDeletes.push_back({preview, currentFrameIndex});
// Tick(currentFrame, framesInFlight) :
//   purger entrées dont frameIndex + framesInFlight <= currentFrame
```

Sampler partagé : un seul `VkSampler` linear/clamp pour tout le cache (Init →
Shutdown).

Descriptor pool ImGui : `ImGui_ImplVulkan_AddTexture` alloue depuis le pool
ImGui interne. Vérifier que `m_descriptorPool` (`WorldEditorImGui.h:138`) a
`maxSets >= ~64` ; bumper si besoin.

### Repack et re-upload de l'albedo array

Quand `m_splatRefsDirty == true` côté Engine :

1. Allouer staging `4 × 256 × 256 × 4 = 1 MiB`.
2. Pour chaque layer 0..3 : copier 256 KB depuis le buffer CPU correspondant
   (procédural si ref vide, texr resamplée sinon, via
   `cache.GetCpuRgba256(...)`).
3. Transition `m_albedoArray.image` `SHADER_READ_ONLY` → `TRANSFER_DST`,
   `vkCmdCopyBufferToImage` avec 4 régions (une par layer), retour
   `SHADER_READ_ONLY`.
4. Submit + `vkQueueWaitIdle` (pattern `ReuploadSplatMap`).
5. Nettoyage staging.

Coût ~2-4 ms hors stalle GPU. Acceptable parce que rare (changement de combo
utilisateur, pas par frame).

## Gestion d'erreurs et cas limites

### Échecs au démarrage

| Scénario | Comportement |
|---|---|
| `TexturePreviewCache::Init` échoue | LOG_WARN, `m_ready = false`. `Get*Thumb` retournent `nullptr`. UI : combos texte sans vignette (UX dégradée). |
| `TerrainSplatting::Init` à 256×256 échoue | Retour `false`, comportement existant inchangé (terrain absent, géré par l'éditeur). |
| `external/stb/stb_image.h` introuvable au build | Erreur compilation explicite (CMake), bloquant — pas runtime. |

### Échecs runtime sur une `.texr`

| Scénario | Comportement |
|---|---|
| Fichier introuvable au `GetTexrThumb` | Retourne `nullptr`, vignette grise. **Pas de cache négatif** → retentée si fichier réapparaît. |
| `.texr` corrompu (magic incorrect, taille mismatch) | LOG_ERROR une seule fois, vignette grise, **cache négatif** (`std::optional` à `nullopt`) pour éviter retry chaque frame. Effacé par `Invalidate`. |
| Texr référencée dans `splatLayerTextureRefs` mais introuvable | Re-upload GPU utilise la procédurale du layer comme fallback. Status UI : warning « Texture *X* introuvable, layer rempli avec défaut moteur ». |
| Source `.texr` >4096×4096 | Refus du décodage (cap), vignette grise, log explicit. Sources entre 256 et 4096 acceptées. |

### Ratio non-carré

Crop centre vers carré, LOG_INFO une fois par texture. Pas de letterboxing.

### Imports concurrents / multi-mappage

| Scénario | Comportement |
|---|---|
| Même `.texr` mappée à 2 layers | Cache hit unique, buffer CPU partagé. Repack copie 2 fois le même buffer, pas de doublon mémoire. |
| Réimport pendant qu'une vignette est dessinée | `Invalidate` push l'ancienne entrée dans `m_pendingDeletes`. Frame courante : `ImGui::Image` dessine toujours l'ancien descriptor (valide). Frame N+`framesInFlight` : ancien détruit, nouveau créé à la 1re demande. |
| Crash entre import et upload vignette (GPU OOM) | LOG_ERROR, `Invalidate` automatique, vignette grise. `.texr` reste sur disque. |

### Layer fallback dans le rebuild GPU

```
ref = splatLayerTextureRefs[layer]
si ref.empty():
    rgba = cache.GetCpuRgba256("procedural:" + layer)   // toujours dispo
sinon:
    rgba = cache.GetCpuRgba256(ref)
    si rgba == nullptr:                                  // fichier disparu/corrompu
        rgba = cache.GetCpuRgba256("procedural:" + layer)  // fallback
        statusBar = "Texture <ref> introuvable - layer reverti au défaut"
```

Le re-upload ne peut jamais échouer faute de données. Au pire, 4 procédurales
et un message d'erreur visible.

### Compatibilité cartes existantes

`splat_layer_texture_refs` déjà en JSON. Cartes anciennes (clé absente) :
tableau vide → procédurales (comportement actuel). Cartes avec mapping :
au load, l'éditeur applique les refs → re-upload initial → terrain affiche
directement les bonnes textures. **Pas de migration nécessaire.**

### Mode `--editor` léger (sans `--world-editor`)

Vignettes inline et panneau Bibliothèque sont gardés derrière
`m_worldEditorExe == true`. Editor Hub léger (mode `--editor` seul) n'a pas
accès → pas de régression sur ce mode.

## Tests

### Unitaires (sans Vulkan)

Fichier : `engine/editor/tests/TexturePreviewCache_test.cpp` (Catch2).

| Test | Objectif |
|---|---|
| `ResampleRgba8Box_DownsampleSquare` | 256×256 rouge → 64×64 rouge uniforme. |
| `ResampleRgba8Box_UpsampleNearest` | 4×4 → 16×16 préserve la moyenne par cellule. |
| `ResampleRgba8Box_NonSquareCropsCenter` | 1024×512 → 256×256 = crop carré central, pas de stretch. |
| `GenerateProceduralAlbedoLayer_DeterministeParLayer` | Mêmes params → mêmes octets exacts. Layers différents → buffers différents. |
| `GenerateProceduralAlbedoLayer_Resolution64Et256` | Pattern visuel cohérent entre résolutions (test approximatif via histogramme). |

### Intégration (avec Vulkan, smoke CI Windows)

| Test | Objectif |
|---|---|
| `TexturePreviewCache_InitShutdown` | Création + destruction propre, validation layers Vulkan sans erreurs. |
| `TerrainSplatting_RebuildAlbedoArray256` | Init en 256×256, set CPU layer 0 = rouge, rebuild, readback GPU → confirme rouge. |

### Manuels (UX)

À documenter dans `docs/world_editor_zone_pipeline.md` (section « Validation
textures ») :

1. Lancer `lcdlln_world_editor.exe` sur carte vierge → Peindre montre 4
   vignettes procédurales.
2. Importer `test_sand.png` (1024×1024) → apparaît dans Bibliothèque sous
   « Importées » en <500 ms.
3. Cliquer la vignette `test_sand` avec layer Terre actif → terrain 3D mis à
   jour en <1 frame visible, vignette assignée encadrée.
4. Sauvegarder → `splat_layer_texture_refs[1] == "textures/test_sand.texr"`
   dans le JSON.
5. Recharger la carte → terrain 3D affiche directement `test_sand` sur Terre
   sans interaction.
6. Réimporter `test_sand.png` (overwrite) → vignette ET 3D rafraîchies, pas de
   crash, pas de fuite descriptor.

### Perf

| Scénario | Cible |
|---|---|
| Frame moyenne avec Bibliothèque ouverte (10 vignettes) | ≤ +0.2 ms vs Bibliothèque fermée |
| Click vignette → terrain 3D updated | ≤ 1 frame visible (rebuild + upload <16 ms) |
| VRAM additionnelle pour 10 textures cachées | ≤ ~3 MB |

### Documentation (règle CLAUDE.md éditeur)

Toutes les fonctions publiques de `TexturePreviewCache`, `TextureLibraryPanel`,
les 2 nouvelles méthodes de `TerrainSplatting`, et les modifs des structures
Engine/WorldEditorImGui auront un commentaire `///` Doxygen au-dessus de la
déclaration : rôle (1-2 phrases), paramètres non-évidents, effets de bord,
contraintes thread/timing.

## Déploiement

Cette PR est **purement client/éditeur** : aucun changement protocole,
aucun nouveau handler serveur, aucune migration DB.

> **Déploiement** : ✅ client/éditeur uniquement, pas de redéploiement serveur.
