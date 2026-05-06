# CHAR-MODEL.24 — Viewer 3D off-screen dans `CharacterCreationUi`

## Dépendances
- CHAR-MODEL.5 (pipeline rendu skinné dans deferred)
- CHAR-MODEL.14 (`humanoid_v1.skel` + clips placeholder, dont `idle.anim`)
- CHAR-MODEL.23 (`RaceColorTable`, `MaterialOverride`)
- *(implicite)* CHAR-MODEL.7 + 8 (sampler + player) pour animer la preview

## Cadrage

Implémenter le **viewer 3D off-screen** intégré au wizard
`CharacterCreationUi` :
- rendu d'une frame du modèle de la race courante dans un `VkImage`
  off-screen,
- exposition du `VkImage` à ImGui via `ImTextureID`,
- caméra **orbitale** réutilisant le champ existant `previewRotationDeg`,
- animation **idle** jouée en boucle (pose vivante, pas figée),
- changement de race instantané (dispatch race id → manifest → asset
  chargé / cache).

Aucune modification de la logique du wizard (étapes, navigation, autres
sliders) en dehors du remplacement du panneau preview — les anciennes
implémentations 2D doivent être déposées au profit du rendu 3D.

---

## Pré-requis vérifiables

```bash
git status
ls engine/client/CharacterCreationUi.{h,cpp}
grep -n "previewRotationDeg" engine/client/CharacterCreationUi.h
ls engine/render/SkinnedRenderable.h
ls game/data/skeletons/humanoid_v1.skel
ls game/data/animations/humanoid/idle.anim
ls game/data/models/humain/humain.race.json
```

---

## Spécification technique

### `PreviewRenderer`

```cpp
// engine/render/PreviewRenderer.h
namespace engine::render
{
    /// Rendu off-screen d'un personnage skinné dans un VkImage exposable
    /// à ImGui. Une instance par session du wizard ; ne pas en créer
    /// plusieurs simultanément (coût VRAM).
    class PreviewRenderer
    {
    public:
        struct Config
        {
            uint32_t width  = 384;   // px
            uint32_t height = 512;
            VkFormat colorFormat = VK_FORMAT_R8G8B8A8_SRGB;
        };

        bool Init(VkDevice device, VkPhysicalDevice phys,
                  AssetRegistry& assets, const Config& cfg);
        void Shutdown();

        /// Charge / cache le modèle de la race demandée. À appeler quand
        /// l'utilisateur change de race dans le wizard.
        bool SetRace(std::string_view raceId);

        /// Avance l'animation idle de dtSec, fait tourner la caméra à
        /// l'angle (degrés) demandé.
        void Tick(float dtSec, float orbitDegrees);

        /// Encode dans `cmd` une passe de rendu off-screen qui écrit
        /// dans le VkImage exposé. Doit être appelé chaque frame avant
        /// le rendu ImGui.
        void RecordFrame(VkCommandBuffer cmd);

        /// ImTextureID stable (descriptor set ImGui) pour DrawList.
        ImTextureID ImGuiTexture() const;
    };
}
```

### Chargement race

`SetRace(raceId)` :
1. Lit `game/data/models/<id>/<id>.race.json`.
2. Résout le `skeleton` et le `skinmesh` via `AssetRegistry`
   (`LoadSkeleton`, `LoadSkinnedMesh`).
3. Résout `MaterialOverride` via `RaceColorTable`.
4. Cache : un même `raceId` réutilise les handles déjà chargés.
5. Pour la variante `winged` du Démon : chargée à la demande via un
   second `SetRace("demons:winged")` (l'id étendu signale la variante).

### Caméra orbitale

- Position : sur un cercle horizontal de rayon ≈ 2.0 m, hauteur 1.6 m,
  cible = (0, 1.0, 0) (centre du torse).
- `orbitDegrees` (passé par le wizard depuis `m_state.previewRotationDeg`)
  contrôle l'angle azimutal.
- Champ de vision : 35°. Projection : perspective.

### Animation idle

- Single-clip `humanoid/idle.anim`, loopée.
- Avancement par `Tick(dt)`.
- Démon ailé : utilise le même clip (les os ailes prennent leur
  bind pose).

### Pipeline de rendu off-screen

- Render target : color image `colorFormat` + depth.
- Reuse du `GeometryPass` skinned + d'une `LightingPass` minimale
  (1 directionnelle + ambient) — si trop coûteux à plomber, accepter
  un mini-pipeline forward dédié pour la preview, mais préférer la
  réutilisation du pipeline deferred existant pour une cohérence
  visuelle exacte.
- Synchronisation : `ImageMemoryBarrier` à la fin du record pour passer
  l'image en `SHADER_READ_ONLY_OPTIMAL`.

### Intégration wizard

Modifier `engine/client/CharacterCreationUi.cpp` :
- au lieu d'afficher l'ancien preview 2D / placeholder, appeler
  `PreviewRenderer.SetRace(state.selectedRaceId)` (au changement),
  `Tick(dt, state.previewRotationDeg)` chaque frame.
- ImGui : `ImGui::Image(previewer.ImGuiTexture(), ImVec2(384, 512))`.

---

## Liste des fichiers

**Créés :**
- `engine/render/PreviewRenderer.h` + `.cpp`
- `tests/render/PreviewRenderer_RenderHumanIdle_test.cpp`

**Modifiés :**
- `engine/client/CharacterCreationUi.h` (ajout d'un membre
  `PreviewRenderer m_preview` ou pointeur)
- `engine/client/CharacterCreationUi.cpp` (init, tick, draw)
- `CMakeLists.txt`

---

## CMakeLists.txt

```cmake
target_sources(engine_core PRIVATE
    engine/render/PreviewRenderer.h
    engine/render/PreviewRenderer.cpp
)
```

---

## Critères d'acceptation

- [ ] Build Windows + Linux propre.
- [ ] Test `PreviewRenderer_RenderHumanIdle_test` : init, `SetRace
      ("humains")`, `Tick(0.0, 0)`, `RecordFrame`, lit le VkImage,
      vérifie ≥ 100 pixels couverts (silhouette présente).
- [ ] Le wizard affiche le modèle 3D animé pour les 8 races
      (vérification visuelle ; au moins humain + 2 autres testés
      manuellement).
- [ ] Changer de race dans le wizard ne fuit pas de VRAM (les anciens
      handles sont relâchés correctement par le cache `AssetRegistry`).
- [ ] Pas de blocage frame > 16 ms sur le rendu preview.
- [ ] Pas de chemin absolu, pas de hot-reload.

---

## Anti-objectifs

- **Ne pas** modifier la state machine du wizard (étapes, navigation).
- **Ne pas** câbler la customisation morphologique (CHAR-MODEL.25).
- **Ne pas** câbler le `CharacterController` (CHAR-MODEL.26).
- **Ne pas** introduire d'éclairage propre au preview qui diverge du
  monde — la cohérence visuelle est obligatoire (1 directionnelle +
  ambient suffit).
- **Ne pas** charger des assets en main thread frame : préchargement
  au `SetRace` uniquement.
