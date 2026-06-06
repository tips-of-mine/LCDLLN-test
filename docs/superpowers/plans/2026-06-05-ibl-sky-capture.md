# IBL par capture du ciel procédural (MVP statique) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Activer l'IBL (réflexions d'environnement + ambiant directionnel) en capturant le ciel procédural dans une cubemap au boot, puis en générant irradiance + specular prefilter et en câblant `irrView`/`useIBL`.

**Architecture:** 3 passes compute one-shot au boot : [1] `SkyCubeCapturePass` (ciel → cube RGBA16F 128²) → [2] `SpecularPrefilterPass::Generate` (existe, à appeler) → [3] `IrradiancePass` (convolution cosine → cube 32²). Câblage dans `DeferredPipeline::Init` + site Lighting de l'Engine. Statique (1×/boot) ; suivi jour/nuit différé.

**Tech Stack:** C++17, Vulkan (compute one-shot, raw images sans VMA), GLSL compute (SPIR-V auto-compilé par `tools/compile_game_shaders.ps1`). **Pas de build local** : compile + SPIR-V validés en CI ; rendu validé manuellement.

**Référence spec :** `docs/superpowers/specs/2026-06-05-ibl-sky-capture-design.md`.

**Portée :** client uniquement — **pas de redéploiement serveur**. Modèles de référence à calquer : `BrdfLutPass`, `SpecularPrefilterPass`, `specular_prefilter.comp`, `sky.frag`.

---

## Contexte vérifié (à connaître avant d'exécuter)

- **`BrdfLutPass`/`SpecularPrefilterPass`** = modèle de passe compute one-shot : image storage Vulkan brut (`vkCreateImage`+`vkAllocateMemory` DEVICE_LOCAL+`vkBindImageMemory`, **pas de VMA** malgré le param `vmaAllocator` conservé), descriptor set, pipeline compute SPIR-V, command pool interne, `Generate()` = begin → barrières → dispatch → barrière → `vkQueueSubmit`+`vkQueueWaitIdle`.
- **Cube storable+samplable** : `SpecularPrefilterPass::Init` crée déjà le pattern : `flags=VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`, `arrayLayers=6`, `format=VK_FORMAT_R16G16B16A16_SFLOAT`, `usage=STORAGE_BIT|SAMPLED_BIT` ; vue `VK_IMAGE_VIEW_TYPE_CUBE` (sampling) + vues 2D par face (storage). **Réutiliser tel quel.**
- **Faces cube** : `specular_prefilter.comp` définit `FaceUvToDirection(face,u,v)` (`u,v∈[-1,1]`). **Copier à l'identique** dans les 2 nouveaux compute (sinon réflexions miroir).
- **Couleur ciel** : `game/data/shaders/sky.frag:118-172` (gradient zénith/horizon, sun glow, below-horizon, `RenderMoonDisk:61-116`). Pour le compute, remplacer la reconstruction `viewDir` par `FaceUvToDirection` ; porter le reste tel quel.
- **Uniformes ciel = `DayNightCycle::State`** (`DayNightCycle.h:33-73`) : `lightDir[3]`, `skyZenith[3]`, `skyHorizon[3]`, `isDaytime`, `moonPhase`, `moonIllumination` ; `moonDir = -lightDir` (`Engine.cpp:5394`).
- **Site Lighting** : `Engine.cpp:5753-5759` (`irrView=VK_NULL_HANDLE` 5753, `prefilterView` lue 5755, `useIBL` 5759) ; Record reçoit les vues 5836.
- **Boot** : `Engine.cpp:3952` appelle `m_pipeline->Init`. Specular est `Init`/`Generate` dans `DeferredPipeline::Init` (`DeferredPipeline.cpp:31-73`) → **orchestrer [1][2][3] là** (a accès device/physicalDevice/queue/queueFamily/config/loadSpirv/pipelineCache). Manque : l'état ciel initial → **étendre `DeferredPipeline::Init`** d'un param `const SkyCaptureParams&` rempli par l'Engine depuis `m_dayNight.GetState()` avant 3952.
- **Compile shaders** : `tools/compile_game_shaders.ps1` compile auto tout `*.comp` de `game/data/shaders/`. **Déposer les 2 `.comp` suffit**, aucune liste à éditer.
- **CMake** : ajouter les 2 `.cpp` à `engine_core` du **root** `CMakeLists.txt` (~ligne 626, à côté de `SpecularPrefilterPass.cpp`). Pas de glob.
- **Config** : `m_cfg.GetBool("gi.ibl.enabled", true)`.

---

## Task 1 : `SkyCubeCapturePass` — compute, ciel → cube RGBA16F (128²)

### 1.1 Shader `game/data/shaders/sky_capture.comp`
- [ ] Créer le compute (1 binding storage image2D par face, push-constants `{uint face; uint faceSize; float pad0,pad1; vec4 lightDir; vec4 zenithColor; vec4 horizonColor; vec4 moonDir; vec4 moonParams;}`). `local_size 8×8`. Copier `FaceUvToDirection` de `specular_prefilter.comp`. Porter le gradient ciel + sun glow + below-horizon + `RenderMoonDisk` de `sky.frag` (substitutions `.xyz`/`moonParams`). `imageStore(uOut, coord, vec4(skyColor + sunContrib, 1.0))`. (Code GLSL détaillé : voir spec + `sky.frag`.)

### 1.2 Header `src/client/render/SkyCubeCapturePass.h`
- [ ] Classe `engine::render::SkyCubeCapturePass` (calque `BrdfLutPass.h`, mais cube 6 layers) + struct `SkyCaptureParams { float lightDir[3]; float zenithColor[3]; float horizonColor[3]; float moonDir[3]; float moonIntensity; float moonPhase; float moonIllumination; }`. Méthodes `Init(device, physDev, vma, faceSize, compSpirv, words, queueFamily, pipelineCache)`, `Generate(device, queue, const SkyCaptureParams&)`, `Destroy`, `GetImageView()` (cube), `GetSampler()`, `IsValid()`. Doc `///`.

### 1.3 Implémentation `src/client/render/SkyCubeCapturePass.cpp`
- [ ] Calquer `SpecularPrefilterPass.cpp` avec `mipLevels=1` : image cube RGBA16F (vérif `STORAGE_IMAGE_BIT` du format, repli `return false`), vue cube + 6 vues 2D par face, sampler linéaire (mip NEAREST, maxLod 0), descriptor layout 1 binding (STORAGE_IMAGE compute), push-constant range. Struct C++ miroir std430 (vec3→vec4) avec `static_assert(sizeof==96)`. Pipeline compute + `AssertPipelineCreationAllowed()` + `PipelineCache::RegisterWarmupKey(...)` (ne pas oublier). `Generate` : boucle 6 faces (update descriptor → barrière UNDEFINED→GENERAL face → push+dispatch `(faceSize+7)/8` → barrière GENERAL→SHADER_READ_ONLY) + submit+waitIdle. `Destroy` complet.
- [ ] **CMake** : ajouter `src/client/render/SkyCubeCapturePass.cpp` à `engine_core` (root).

> Vérif Task 1 : pas de build local → CI compile + SPIR-V. `static_assert` verrouille le push layout.

---

## Task 2 : `IrradiancePass` — compute convolution cosine (cube 32²)

### 2.1 Shader `game/data/shaders/irradiance_convolve.comp`
- [ ] Créer le compute : binding 0 `samplerCube uEnv`, binding 1 `writeonly image2D uOut` (rgba16f), push `{uint face; uint faceSize; float pad0,pad1;}`. `FaceUvToDirection` copié. Convolution cosine-hemisphere (double boucle φ∈[0,2π) θ∈[0,π/2), `deltaPhi=deltaTheta=0.025`, échantillon `textureLod(uEnv, sampleVec, 0.0).rgb * cos(theta)*sin(theta)`, repère tangent autour de N), `irradiance = PI * sum / nrSamples` (⚠️ voir Risque #3 : vérifier la division par PI côté `lighting.frag` avant de figer le facteur). `imageStore`.

### 2.2 Header `src/client/render/IrradiancePass.h`
- [ ] Classe `engine::render::IrradiancePass` (calque `SpecularPrefilterPass.h`, 1 mip, 2 bindings) : `Init(...)`, `Generate(device, queue, sourceCubeView, sourceCubeSampler)`, `Destroy`, `GetImageView()`/`GetSampler()`/`IsValid()`. Doc `///`.

### 2.3 Implémentation `src/client/render/IrradiancePass.cpp`
- [ ] Calquer `SpecularPrefilterPass.cpp` : cube RGBA16F 32², 1 mip ; descriptor 2 bindings (0=COMBINED_IMAGE_SAMPLER source, 1=STORAGE_IMAGE sortie) ; push 16 o ; `Generate` boucle 6 faces (binding0=source SHADER_READ_ONLY, binding1=face GENERAL ; barrières ; dispatch `(32+7)/8`). `Destroy` complet.
- [ ] **CMake** : ajouter `src/client/render/IrradiancePass.cpp` à `engine_core` (root).

---

## Task 3 : Engine/DeferredPipeline — orchestration + wiring + gating

### 3.0 Vérifier l'ordre d'init
- [ ] Confirmer que `m_dayNight.Init(...)` (`Engine.cpp:1135`) s'exécute **avant** `m_pipeline->Init` (3952). Si oui → orchestration dans `DeferredPipeline::Init` (retenu). Sinon → déplacer l'orchestration IBL dans Engine via un `DeferredPipeline::GenerateIbl(...)` post-Init.

### 3.1 Membres `DeferredPipeline`
- [ ] `DeferredPipeline.h` : includes des 2 headers ; membres `SkyCubeCapturePass m_skyCubeCapturePass;` + `IrradiancePass m_irradiancePass;` (après `m_specularPrefilterPass`) ; accesseur `GetIrradiancePass()` (const+non-const) ; étendre `Init(...)` d'un param `const SkyCaptureParams& skyParams`.

### 3.2 Orchestration dans `DeferredPipeline::Init`
- [ ] Après le bloc Specular (~`DeferredPipeline.cpp:73`), bloc gardé `gi.ibl.enabled` :
  - charger `sky_capture.comp.spv` + `irradiance_convolve.comp.spv` (loadSpirv) ;
  - `m_skyCubeCapturePass.Init(...)` → `.Generate(skyParams)` → `m_specularPrefilterPass.Generate(device, queue, skyCubeView, skyCubeSampler)` → `m_irradiancePass.Init(...)` → `.Generate(device, queue, skyCubeView, skyCubeSampler)` ;
  - tout échec / shader absent / format non supporté → LOG_WARN + laisser `m_irradiancePass` `!IsValid()` (→ useIBL=0, repli ambient plat, pas de crash).
  - ⚠️ La prefilter `Generate` **tourne ici pour la 1re fois** — valider barrières en CI/jeu. Source 1-mip → `textureLod` clampe mip 0 (acceptable MVP).

### 3.3 Appel Engine
- [ ] Avant `Engine.cpp:3952`, remplir `SkyCaptureParams iblSky` depuis `m_dayNight.GetState()` (lightDir/skyZenith→zenithColor/skyHorizon→horizonColor/moonDir=-lightDir/moonIntensity=isDaytime?0:1/moonPhase/moonIllumination) et le passer à `m_pipeline->Init(...)`.

### 3.4 Wiring du site Lighting
- [ ] `Engine.cpp:5753-5754` : `irrView`/`irrSamp` = `GetIrradiancePass().IsValid() ? GetImageView()/GetSampler() : VK_NULL_HANDLE`. `prefilterView` (5755) inchangé (contenu désormais généré). `useIBL` (5759) inchangé (devient 1 quand les 3 vues sont non-NULL). Record (5836) inchangé.

### 3.5 Config
- [ ] Documenter `gi.ibl.enabled` (bool, défaut true, client). L'ajouter au `config.json` d'exemple à côté de `gi.ddgi.*` si une entrée explicite est attendue ; sinon défaut suffit. Pas de clé serveur.

### 3.6 Destruction
- [ ] `DeferredPipeline::Destroy` : `m_irradiancePass.Destroy(device)` + `m_skyCubeCapturePass.Destroy(device)` (ordre reverse, avec les autres passes IBL). Pas de fuite.

> Vérif Task 3 : compile CI. Confirmer ordre d'init (3.0) et noms réels (`m_dayNight`, `GetState()`, champs `State`).

---

## Task 4 : Validation

- [ ] **CI** : build Linux + Windows verts ; `sky_capture.comp`/`irradiance_convolve.comp` compilés en `.spv` (confirmer le step shaders dans `.github/workflows/`) ; **aucun warning de validation Vulkan** au boot (surtout `SpecularPrefilterPass::Generate`, 1re exécution).
- [ ] **En jeu** : métal/brillant (`Crate_Metal`, avatar) **reflète les teintes du ciel** ; ambiant **directionnel** ; `gi.ibl.enabled=false` → ambient plat sans crash (log `[Boot] gi.ibl.enabled=false`).
- [ ] **Pas de sur-exposition** : si laiteux/cramé → double-division par PI possible (irradiance_convolve ↔ lighting.frag). Lire `lighting.frag:419-432` AVANT de figer le facteur ; retirer `PI *` si l'irradiance est déjà divisée à la consommation.
- [ ] **Pas de réflexion miroir** : `FaceUvToDirection` identique dans les 3 shaders.

---

## Self-Review

- **Couverture spec** : [1] SkyCubeCapturePass (Task 1) ; [2] SpecularPrefilter.Generate enfin appelée (Task 3.2) ; [3] IrradiancePass (Task 2) ; [4] orchestration boot + wiring irrView/useIBL + gating gi.ibl.enabled + repli (Task 3). Statique 1×/boot. ✅
- **Formats/bindings** : toutes cubes RGBA16F (= format source attendu par prefilter + samplerCube lighting). `FaceUvToDirection` copié à l'identique (orientation cohérente). ✅
- **Gating/repli** : disabled / shader absent / Init|Generate échoué / format non supporté → `m_irradiancePass !IsValid()` → irrView NULL → useIBL=0 → ambient plat, pas de crash. ✅
- **SPIR-V** : 2 `.comp` → compilés auto (glob), aucune liste. **À confirmer** que la CI exécute `compile_game_shaders.ps1` (Task 4). ✅
- **CMake** : 2 `.cpp` → engine_core (root). ✅
- **CLAUDE.md** : chemin compute (pas de render-pass cube) → aucun `frontFace`/`cullMode`/winding touché. ✅ PascalCase respecté.

## Risques (exécutant)
1. **`SpecularPrefilterPass::Generate` jamais exécutée** — point le plus susceptible de révéler un bug latent (barrières/layouts). Valider en premier (CI validation + jeu).
2. **Ordre `m_dayNight` vs `m_pipeline->Init`** — confirmer avant de coder (Task 3.0).
3. **Facteur irradiance / sur-exposition** — vérifier la division par PI dans `lighting.frag` (Task 4) avant de figer.
4. **Format RGBA16F storage** — garde-fou présent (repli useIBL=0).
5. **Orientation faces** — `FaceUvToDirection` copié strict.
6. **Source 1-mip pour le prefilter** — `textureLod` clampe mip 0 (acceptable MVP ; mips sur skyCube différés).

## Fichiers
**Créer** : `game/data/shaders/sky_capture.comp`, `irradiance_convolve.comp` ; `src/client/render/SkyCubeCapturePass.{h,cpp}`, `IrradiancePass.{h,cpp}`.
**Modifier** : `src/client/render/DeferredPipeline.{h,cpp}`, `src/client/app/Engine.cpp`, `CMakeLists.txt` (root).
**Modèles (lecture seule)** : `BrdfLutPass.cpp`, `SpecularPrefilterPass.cpp`, `specular_prefilter.comp`, `sky.frag`.
