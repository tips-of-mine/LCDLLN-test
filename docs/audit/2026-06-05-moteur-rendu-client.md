# Audit du moteur de rendu & du client — LCDLLN

**Date** : 2026-06-05
**Périmètre** : moteur graphique en profondeur (`src/client/render/`) + reste du client (`src/client/`) en couverture. `legacy/` exclu.
**Angles prioritaires** : fidélité visuelle, architecture/dette (code orphelin), anomalies/bugs.
**Méthode** : analyse en éventail par 5 agents spécialisés (architecture rendu, fidélité visuelle, anomalies Vulkan, code orphelin client, sous-systèmes hors-rendu), avec preuves `fichier:ligne` et recoupement croisé.

---

## Verdict global

Le moteur **n'est pas un moteur indie sous-développé** : c'est un deferred renderer Vulkan moderne dont les fondations sont **réellement de niveau AA/AAA** sur le papier — PBR Cook-Torrance/GGX de référence, IBL split-sum correct, CSM mathématiquement bonne (snap au texel anti-swimming), DDGI, auto-exposure par histogramme, frame graph à tri topologique avec barrières sync2 automatiques, terrain splat triplanaire.

**Le problème n'est pas la qualité du code des features, c'est qu'une grande partie de ces features de luxe sont CODÉES MAIS DÉBRANCHÉES du rendu réel.** On paie le coût CPU/GPU de les calculer, mais leur résultat n'arrive jamais à l'écran. C'est l'explication centrale du ressenti « pas à la hauteur d'un AAA » : l'image finale est bridée alors que le moteur en a les moyens.

> **Métaphore** : une voiture de sport dont le moteur tourne, mais dont l'embrayage n'est pas connecté aux roues sur la moitié des rapports.

---

## 1. Le « moteur fantôme » — systèmes calculés mais non consommés

C'est, de loin, le constat le plus important. Plusieurs agents l'ont confirmé indépendamment.

| Système | État | Preuve | Conséquence visible |
|---|---|---|---|
| **Ombres soleil (CSM)** | 4 cascades **rendues** chaque frame, mais **jamais échantillonnées** dans `lighting.frag` | `Engine.cpp:5410-5432` (rendu) ; aucun sampling shadow dans `lighting.frag` ; lues seulement par DDGI + fog | **Le soleil ne projette aucune ombre** sur terrain/objets. On paie 4 rendus de depth pour les seuls god-rays. |
| **Lumières ponctuelles** | `DynamicLightSystem` calcule les lampes/torches chaque frame, `GetActiveLights()` **a 0 consommateur** | `DynamicLightSystem.h:32,93` ; `LightingPass` n'a aucun support point-light | **Nuit sans lampes** : lampadaires, torches, fenêtres invisibles. Une seule lumière directionnelle éclaire tout. |
| **IBL (irradiance + spéculaire)** | `irrView` reste `VK_NULL_HANDLE`, donc `useIBL` **toujours 0** | `Engine.cpp:5733,5739` | Réflexions d'environnement absentes. `BrdfLut` + `SpecularPrefilter` (1000 l.) calculés pour rien. |
| **GPU-driven culling + Hi-Z** | Ne traitent que le **cube placeholder** (≤1 item) ; tout le reste (terrain, props, avatars, impostors) contourne le culling par des `Record` directs | `Engine.cpp:5026-5052` | 1300+ lignes + 2 shaders compute exécutés par frame pour ~0 bénéfice. |
| **Eau (géométrie)** | Surface **totalement plate**, aucune Gerstner/FFT, animation uniquement par normal maps | `water.vert:30` (`viewProj*pos` sans displacement) | Eau d'étang stylisée, pas d'océan/houle crédible. |

**Conditionnelles OFF par défaut (saines, fallbacks propres)** : DDGI (`gi.ddgi.enabled=false`), volumetric fog (`density=0`), DoF (`focus_range_m=0`), impostors (`enabled=false`). Architecturalement correctes — juste inactives en config par défaut.

---

## 2. Dualité terrain — défaut de correction réel

Deux systèmes terrain tournent **simultanément**, pas l'un OU l'autre :

- `terrain/TerrainRenderer` (heightmap legacy) — dessiné en premier (`Engine.cpp:5074`).
- `terrain_chunk/TerrainChunkRenderer` (chunks data-driven, canonique M100) — dessiné « par-dessus » (`Engine.cpp:5285-5305`), **sans éteindre** le premier (commentaire explicite `Engine.cpp:5281-5282`).

Conséquences :
- **Overdraw** systématique (deux terrains rendus).
- **Collision ≠ rendu** : `TerrainCollider` est bindé sur la heightmap `m_terrain` (`TerrainCollider.cpp:16-32`), jamais sur les chunks. `TerrainChunkRenderer` n'expose **aucune** API de hauteur. → **Le joueur marche à côté du terrain affiché** en zone chunkée (cause profonde du fix partiel PR #824).

**Proposition** : interface unique `IHeightField` implémentée par les deux backends ; rendre `m_terrain` exclusif (fallback uniquement si aucun chunk valide) ; binder la collision sur la source réellement rendue.

---

## 3. Anomalies & bugs Vulkan (correction)

| Sév. | Bug | Preuve | Risque |
|---|---|---|---|
| **Critique** | `HiZPyramidPass` : un seul descriptor set réécrit pendant qu'il est en vol (mis à jour `mipCount` fois après un bind unique, partagé entre 2 frames in-flight) | `HiZPyramidPass.cpp:271,448` + `.h:71` | Violation sync Vulkan active chaque frame ; descriptor « déchiré », culling occlusion corrompu. Toléré par chance sur beaucoup de drivers. |
| **Majeur** | `TerrainChunkRenderer` : l'éviction LRU détruit des `VkBuffer` **encore référencés** par une frame en vol (`Tick` dans `BeginFrame`, avant le `vkWaitForFences`) | `TerrainChunkRenderer.cpp:1149,443` ; `Engine.cpp:7344,10130` | Use-after-free GPU intermittent au streaming terrain ; crash device-lost difficile à reproduire. |
| **Majeur** | `DeferredDestroyQueue` : ne libère **jamais** la `VkDeviceMemory` + n'est **branché nulle part** | `DeferredDestroyQueue.cpp:40-43` ; `Engine.h:1210` | Fuite mémoire GPU si utilisé ; et c'est précisément le mécanisme qui rendrait l'éviction terrain (#ci-dessus) sûre — mort. |
| **Majeur (cond.)** | `FrameGraph` fallback barrières legacy lossy : `ToLegacyStage/Access` incomplets → `stageMask=0` possible | `FrameGraph.cpp:263-285,431-445` | Erreur de validation / sous-synchronisation, **uniquement** si synchronization2 absent (toujours présent en Vulkan 1.3). |
| **Mineur (latent)** | `GpuUploadQueue` : deadlock de file si un job dépasse le budget (`while sizeBytes<=budget` ne pop jamais) | `GpuUploadQueue.cpp:41` | Dormant (file non câblée), piège à retardement. |

**Conformité winding/culling** : ✅ vérifiée conforme à `CLAUDE.md` (terrain CCW+BACK respecté ; pipelines falaises/skinned/geometry ont chacun leur justification documentée). **Ne pas uniformiser.**

---

## 4. Fidélité visuelle — écarts vs AAA (même là où c'est branché)

- **Normal mapping cassé (Important)** : pas de base tangente dans le G-buffer. La normale est « perturbée » par `N = normalize(N + normalSample.xy*0.5)` (`gbuffer_geometry.frag:76-80`) — géométriquement faux (pas de TBN), relief des matériaux plat/incohérent. → ajouter tangente+bitangente (ou dérivées d'écran).
- **Modèle matériau minimal** : metallic/roughness/AO/normal seulement ; pas d'émissif réel, ni clearcoat/sheen/SSS. Bindless plafonné à **64 textures / 64 matériaux** (`Material.h:80`) — très bas pour un MMO à monde large.
- **Bloom basique** : downsample box 2×2 + upsample bilinéaire 1-tap (`bloom_downsample.frag:23`, `bloom_upsample.frag:13`) → scintille et blocky. Standard AAA = 13-tap + tente 3×3 + Karis average.
- **TAA basique** : opère en **LDR** (post-tonemap), clamp AABB dur, pas de dilatation de vitesse, historique bilinéaire → ghosting + flou cumulatif. Standard = HDR + variance/YCoCg clipping + Catmull-Rom.
- **LUT tonemap échantillonnée en nearest** (`tonemap.frag:54-58`) → banding sur dégradés ; passer en lerp trilinéaire.
- **SSAO** : hémisphère 32 taps correct, mais pas HBAO/GTAO (pas de bent normals).
- **Bon niveau** : terrain (splat triplanaire 4 couches + detail/macro normals + ORM) = solide AA ; PBR/IBL de référence ; CSM bien calculée ; auto-exposure histogramme.

**Niveau visuel** : fondations AA bien architecturées, mais **rendu effectif aujourd'hui plus proche de indie+/AA-faible** à cause des systèmes débranchés (ombres, lampes, eau).

---

## 5. Architecture & dette

- **God-object `Engine`** : `Engine.cpp` = **12 391 lignes**, `Engine.h` = **245 variables membres**, ~70 includes. Concentre boot Vulkan, assets, gameplay, ~20 presenters UI, réseau master+UDP, orchestration **complète** du frame graph (27 `addPass`), éditeur, état d'auth.
  - Inverse de matrice 4×4 **copiée-collée 5+ fois** inline (Decals/Lighting/VolFog/DoF/Sky).
  - Push handler master = **switch de ~1150 lignes / 71 cas** copiés-collés (`Engine.cpp:2385-3523`).
  - `SetSendCallback` : même lambda dupliquée **~20 fois** (`Engine.cpp:1425…1614`).
  - Input gameplay (achat/vente/talk/tri enchères au clavier) enfoui dans `UpdateGameplayNet` (`Engine.cpp:11583+`).
- **Orchestration mal placée** : `FrameGraph` est un graphe générique sain, `DeferredPipeline` ne fait qu'`Init/Destroy` — mais c'est `Engine.cpp` qui enregistre et exécute toutes les passes. → extraire un `RenderGraphBuilder`.
- **Réseau** : couche modèle→UI (`UIModel`/`UIModelBinding`) saine et thread-checkée, pattern Presenter cohérent. Mais : UDP gameplay **sans reconnexion ni détection de timeout** (seul le master TCP reconnecte) ; `PollIncoming` draine la socket sans plafond par frame (`GameplayUdpClient.cpp:398`) ; accumulateur de snapshots chunkés sensible aux duplicatas UDP (`UIModel.cpp:647-680`).
- **`ClientPrediction`** (422 l. de prédiction/réconciliation) **jamais câblé** — le client envoie sa position brute. Soit l'activer, soit assumer explicitement client-authoritative.
- **213 occurrences « CMANGOS »** dans `src/client/` — viole une règle projet explicite (mémoire `feedback_no_cmangos_term`).

### Code orphelin confirmé (~7 400 lignes)

20 fichiers compilés mais **jamais référencés** (hors leur propre fichier / tests). Top 10 par taille :

| Lignes | Fichier |
|---:|---|
| 978 | `hud/HudLayoutEditor.{cpp,h}` |
| 812 | `combat/AdvancedCombatUi.{cpp,h}` |
| 810 | `settings/SettingsMenuUi.{cpp,h}` |
| 616 | `render/UnderwaterPass.{cpp,h}` (+ membre `m_underwaterPass` mort dans `Engine.h`) |
| 492 | `fx/FXManager.{cpp,h}` |
| 435 | `crafting/CraftingUi.{cpp,h}` |
| 396 | `combat/AuraFXSystem.{cpp,h}` |
| 393 | `gameplay/ThirdPersonCamera.{cpp,h}` (Engine utilise `OrbitalCameraController`) |
| 328 | `combat/BuffBarPresenter.{cpp,h}` |
| 325 | `ui_common/ThemeManager.{cpp,h}` (0 réf dans tout le repo) |

Autres orphelins : `AoEPreviewSystem` (310), `FriendsUi` (304), `PartyHud` (249), `HarvestCastBar` (187), `ParticleBillboardPass` (141), `PakReader`+`PakFormat` (160), `HamletKitLibrary` (127), `ZonePreloadHook` (127), et la feature « son de pas par couche splat » entièrement codée mais jamais reliée (`SplatSampling` 107 + `FootstepAudioSurfaceHook` 58).

**Utilisés seulement par les tests** (à surveiller, NE PAS supprimer) : `ClientPrediction`, `HazardSimulator`, `InteractiveSimulator`, `FoliageLibrary`, `EntityInfluenceCollector`, `SurfaceQueryService`, etc. — sous-systèmes monde testés mais pas branchés au runtime (architecture author→bake→stream défendable, mais nommage trompeur, ex. `InteractiveSimulator` est éditeur-only).

---

## Synthèse des propositions (à discuter, par thème)

### A. « Rebrancher le moteur » — fort impact visuel, coût modéré, client pur
Le meilleur ratio impact/effort. Tout est déjà calculé, il s'agit surtout de *consommer* l'existant.
1. **Échantillonner les CSM dans `lighting.frag`** (sélection cascade + PCF + visibilité soleil). Ombres portées = gain de réalisme énorme, coût quasi nul (depth déjà rendu).
2. **Passe de lumières ponctuelles (clustered/tiled)** consommant `DynamicLightSystem`. Débloque toutes les ambiances de nuit.
3. **Câbler l'IBL** (capture cubemap → prefilter → `irrView`) ou retirer BrdfLut/SpecularPrefilter du boot.

### B. Correction des bugs Vulkan — fiabilité
4. Corriger le descriptor set en vol de `HiZPyramidPass` (Critique).
5. Finir + brancher `DeferredDestroyQueue` (libérer la `VkDeviceMemory`) et l'utiliser pour l'éviction terrain (corrige le use-after-free).

### C. Unifier le terrain — correction + perf
6. Interface `IHeightField`, rendu exclusif (plus de double terrain), collision sur la source rendue.

### D. Polish visuel — là où c'est déjà branché
7. TBN réel pour le normal mapping. 8. Bloom 13-tap + Karis. 9. TAA HDR + variance clipping. 10. Eau Gerstner. 11. LUT tonemap trilinéaire.

### E. Hygiène / dette — abaisse le coût de tout le reste
12. Supprimer les ~7 400 lignes orphelines (risque faible).
13. Décider du sort des systèmes « débranchés mais coûteux » (GPU-cull/Hi-Z, ParticleSystem) : câbler ou retirer du chemin frame.
14. Extraire `RenderGraphBuilder` hors d'`Engine.cpp` ; table de dispatch d'opcodes (−1000 l.) ; factoriser `Mat4::Inverse` et `SetSendCallback`.
15. Reconnexion/timeout UDP gameplay ; décision `ClientPrediction` (câbler ou assumer). Purge « CMANGOS ».

---

**Déploiement** : ✅ Cet audit est en lecture seule — **aucun redéploiement serveur**. Les propositions A, B, C-rendu, D sont **client uniquement** (shaders/rendu Vulkan). C-collision et E touchent du gameplay/réseau client mais restent client (aucun wire/handler/DB modifié à ce stade). Attention à la convention winding de `CLAUDE.md` si on touche aux pipelines.
