# STAB.12 — Instrumentation `fprintf` : couverture maximale des zones critiques

**Priorité :** Haute  
**Périmètre :** 18 fichiers `.cpp` sous `engine/`  
**Dépendances :** Aucune (ticket autonome, indépendant de STAB.11)

---

## Objectif

Ajouter des `std::fprintf(stderr, "...\n"); std::fflush(stderr);` à toutes les
étapes importantes du boot, du shutdown et des chemins Vulkan critiques, afin de
localiser tout futur crash à la ligne près **sans recompiler avec un debugger**.

**Règle universelle** : un `fprintf` **avant** et **après** chaque étape atomique.
Format : `[TAG] avant <action>` / `[TAG] <action> OK` ou `[TAG] <action> r=<code>`.

---

## Fichiers et zones à instrumenter

---

### 1. `engine/platform/FileWatcher.cpp`

**Tag : `[FW]`**

Dans `FileWatcher::Init` :
```cpp
std::fprintf(stderr, "[FW] Init enter dir='%s'\n", directory.c_str()); std::fflush(stderr);
// après CreateFileW :
std::fprintf(stderr, "[FW] CreateFileW r=%p\n", (void*)m_impl->hDir); std::fflush(stderr);
// après CreateEventW hEvent :
std::fprintf(stderr, "[FW] CreateEventW hEvent r=%p\n", (void*)m_impl->hEvent); std::fflush(stderr);
// après CreateEventW hStopEvent :
std::fprintf(stderr, "[FW] CreateEventW hStopEvent r=%p\n", (void*)m_impl->hStopEvent); std::fflush(stderr);
// après buffer.resize :
std::fprintf(stderr, "[FW] Init OK\n"); std::fflush(stderr);
```

Dans `FileWatcher::WaitForChange` :
```cpp
// au début (optionnel — peut être bruyant, activer si besoin) :
std::fprintf(stderr, "[FW] WaitForChange timeout=%u pending=%d\n", timeoutMs, m_impl ? (int)m_impl->pending : -1); std::fflush(stderr);
// après WaitForMultipleObjectsEx :
std::fprintf(stderr, "[FW] WaitForMultipleObjects wait=%lu\n", (unsigned long)wait); std::fflush(stderr);
```

Dans `FileWatcher::Destroy` :
```cpp
std::fprintf(stderr, "[FW] Destroy enter\n"); std::fflush(stderr);
// après m_impl->Destroy() :
std::fprintf(stderr, "[FW] Destroy OK\n"); std::fflush(stderr);
```

Dans `Impl::Destroy` :
```cpp
std::fprintf(stderr, "[FW] Impl::Destroy enter hDir=%p pending=%d\n", (void*)hDir, (int)pending); std::fflush(stderr);
// après CancelIoEx :
std::fprintf(stderr, "[FW] Impl::Destroy CancelIoEx done\n"); std::fflush(stderr);
// après CloseHandle(hDir) :
std::fprintf(stderr, "[FW] Impl::Destroy hDir closed\n"); std::fflush(stderr);
// après CloseHandle(hEvent) :
std::fprintf(stderr, "[FW] Impl::Destroy hEvent closed\n"); std::fflush(stderr);
// après CloseHandle(hStopEvent) :
std::fprintf(stderr, "[FW] Impl::Destroy hStopEvent closed\n"); std::fflush(stderr);
```

Dans `StartReadDirectoryChanges` :
```cpp
std::fprintf(stderr, "[FW] StartRDC enter hDir=%p\n", (void*)impl->hDir); std::fflush(stderr);
// après ReadDirectoryChangesW :
std::fprintf(stderr, "[FW] StartRDC ReadDirectoryChangesW ok=%d\n", (int)ok); std::fflush(stderr);
```

---

### 2. `engine/render/ShaderHotReload.cpp`

**Tag : déjà `[POLL]`, `[AP]`, `[SHR]`** — compléter :

Dans `Poll`, remplacer `[POLL] watcher.Init OK` par :
```cpp
std::fprintf(stderr, "[POLL] watcher.Init retourne (watcherInited=%d)\n", (int)m_watcherInited); std::fflush(stderr);
```

Dans `ApplyPending`, avant le lock :
```cpp
std::fprintf(stderr, "[AP] avant lock pendingMutex\n"); std::fflush(stderr);
// après le lock (première ligne dans le bloc) :
std::fprintf(stderr, "[AP] lock OK pending=%zu\n", m_pending.size()); std::fflush(stderr);
```

---

### 3. `engine/render/DeferredPipeline.cpp`

**Tag : `[PIPELINE]`** — déjà présent pour Init, compléter pour `Destroy` :

```cpp
void DeferredPipeline::Destroy(VkDevice device)
{
    std::fprintf(stderr, "[PIPELINE] Destroy enter\n"); std::fflush(stderr);
    // avant chaque pass.Destroy() :
    std::fprintf(stderr, "[PIPELINE] Destroy taaPass\n"); std::fflush(stderr);
    m_taaPass.Destroy(device);
    std::fprintf(stderr, "[PIPELINE] Destroy autoExposure\n"); std::fflush(stderr);
    m_autoExposure.Destroy(device);
    std::fprintf(stderr, "[PIPELINE] Destroy bloomCombine\n"); std::fflush(stderr);
    m_bloomCombinePass.Destroy(device);
    std::fprintf(stderr, "[PIPELINE] Destroy bloomUpsample\n"); std::fflush(stderr);
    m_bloomUpsamplePass.Destroy(device);
    std::fprintf(stderr, "[PIPELINE] Destroy bloomDownsample\n"); std::fflush(stderr);
    m_bloomDownsamplePass.Destroy(device);
    std::fprintf(stderr, "[PIPELINE] Destroy bloomPrefilter\n"); std::fflush(stderr);
    m_bloomPrefilterPass.Destroy(device);
    std::fprintf(stderr, "[PIPELINE] Destroy tonemap\n"); std::fflush(stderr);
    m_tonemapPass.Destroy(device);
    std::fprintf(stderr, "[PIPELINE] Destroy lighting\n"); std::fflush(stderr);
    m_lightingPass.Destroy(device);
    std::fprintf(stderr, "[PIPELINE] Destroy decal\n"); std::fflush(stderr);
    m_decalPass.Destroy(device);
    std::fprintf(stderr, "[PIPELINE] Destroy shadow\n"); std::fflush(stderr);
    m_shadowMapPass.Destroy(device);
    std::fprintf(stderr, "[PIPELINE] Destroy hiZ\n"); std::fflush(stderr);
    m_hiZPyramidPass.Destroy(device);
    std::fprintf(stderr, "[PIPELINE] Destroy gpuCull\n"); std::fflush(stderr);
    m_gpuDrivenCullingPass.Destroy(device);
    std::fprintf(stderr, "[PIPELINE] Destroy materialDescCache\n"); std::fflush(stderr);
    m_materialDescriptorCache.Destroy(device);
    std::fprintf(stderr, "[PIPELINE] Destroy geometry\n"); std::fflush(stderr);
    m_geometryPass.Destroy(device);
    std::fprintf(stderr, "[PIPELINE] Destroy ssaoBlur\n"); std::fflush(stderr);
    m_ssaoBlurPass.Destroy(device);
    std::fprintf(stderr, "[PIPELINE] Destroy ssao\n"); std::fflush(stderr);
    m_ssaoPass.Destroy(device);
    std::fprintf(stderr, "[PIPELINE] Destroy ssaoKernelNoise\n"); std::fflush(stderr);
    m_ssaoKernelNoise.Destroy(device);
    std::fprintf(stderr, "[PIPELINE] Destroy specularPrefilter\n"); std::fflush(stderr);
    m_specularPrefilterPass.Destroy(device);
    std::fprintf(stderr, "[PIPELINE] Destroy brdfLut\n"); std::fflush(stderr);
    m_brdfLutPass.Destroy(device);
    std::fprintf(stderr, "[PIPELINE] Destroy pipelineCache\n"); std::fflush(stderr);
    m_pipelineCache.Destroy(device);
    std::fprintf(stderr, "[PIPELINE] Destroy OK\n"); std::fflush(stderr);
```

---

### 4. `engine/Engine.cpp`

**Tag : `[RUN]`** — compléter la séquence de shutdown dans `Run()` :

```cpp
// après vkDeviceWaitIdle :
std::fprintf(stderr, "[RUN] vkDeviceWaitIdle OK\n"); std::fflush(stderr);
// avant m_pipeline->Destroy :
std::fprintf(stderr, "[RUN] avant pipeline->Destroy\n"); std::fflush(stderr);
// après m_pipeline->Destroy :
std::fprintf(stderr, "[RUN] pipeline->Destroy OK\n"); std::fflush(stderr);
// avant chaque Shutdown/Destroy subsystem :
std::fprintf(stderr, "[RUN] avant profilerHud.Shutdown\n"); std::fflush(stderr);
std::fprintf(stderr, "[RUN] avant profiler.Shutdown\n"); std::fflush(stderr);
std::fprintf(stderr, "[RUN] avant audioEngine.Shutdown\n"); std::fflush(stderr);
std::fprintf(stderr, "[RUN] avant decalSystem.Shutdown\n"); std::fflush(stderr);
std::fprintf(stderr, "[RUN] avant assetRegistry.Destroy\n"); std::fflush(stderr);
std::fprintf(stderr, "[RUN] avant frameGraph.destroy\n"); std::fflush(stderr);
std::fprintf(stderr, "[RUN] avant stagingAllocator.Destroy\n"); std::fflush(stderr);
std::fprintf(stderr, "[RUN] avant DestroyFrameResources\n"); std::fflush(stderr);
std::fprintf(stderr, "[RUN] avant vmaDestroyAllocator\n"); std::fflush(stderr);
std::fprintf(stderr, "[RUN] avant vkSwapchain.Destroy\n"); std::fflush(stderr);
std::fprintf(stderr, "[RUN] avant vkDeviceContext.Destroy\n"); std::fflush(stderr);
std::fprintf(stderr, "[RUN] avant vkInstance.Destroy\n"); std::fflush(stderr);
std::fprintf(stderr, "[RUN] avant glfwDestroyWindow\n"); std::fflush(stderr);
std::fprintf(stderr, "[RUN] avant glfwTerminate\n"); std::fflush(stderr);
std::fprintf(stderr, "[RUN] shutdown complete\n"); std::fflush(stderr);
```

Dans `Render()`, avant et après `vkQueueSubmit` et `vkQueuePresentKHR` :
```cpp
std::fprintf(stderr, "[RENDER] avant vkQueueSubmit frame=%u\n", m_currentFrame); std::fflush(stderr);
// après :
std::fprintf(stderr, "[RENDER] vkQueueSubmit r=%d\n", (int)submitResult); std::fflush(stderr);
std::fprintf(stderr, "[RENDER] avant vkQueuePresentKHR\n"); std::fflush(stderr);
// après :
std::fprintf(stderr, "[RENDER] vkQueuePresentKHR r=%d\n", (int)presentResult); std::fflush(stderr);
```

---

### 5. `engine/render/AssetRegistry.cpp`

**Tag : `[ASSET]`**

Dans `Init` :
```cpp
std::fprintf(stderr, "[ASSET] Init enter device=%p vma=%p\n", (void*)device, vmaAllocator); std::fflush(stderr);
// fin :
std::fprintf(stderr, "[ASSET] Init OK\n"); std::fflush(stderr);
```

Dans `Destroy` :
```cpp
std::fprintf(stderr, "[ASSET] Destroy enter meshes=%zu textures=%zu\n", m_meshes.size(), m_textures.size()); std::fflush(stderr);
// fin :
std::fprintf(stderr, "[ASSET] Destroy OK\n"); std::fflush(stderr);
```

Dans `loadMeshInternal` et `loadTextureInternal` :
```cpp
std::fprintf(stderr, "[ASSET] loadMesh '%s'\n", relativePath.data()); std::fflush(stderr);
std::fprintf(stderr, "[ASSET] loadTexture '%s'\n", relativePath.data()); std::fflush(stderr);
```

---

### 6. `engine/render/BrdfLutPass.cpp`

**Tag : `[BRDF]`**

Dans `Init` :
```cpp
std::fprintf(stderr, "[BRDF] Init enter size=%u\n", size); std::fflush(stderr);
// après vkCreateImage :
std::fprintf(stderr, "[BRDF] vkCreateImage r=%d img=%p\n", (int)r, (void*)m_image); std::fflush(stderr);
// fin Init :
std::fprintf(stderr, "[BRDF] Init OK\n"); std::fflush(stderr);
```

Dans `Generate` :
```cpp
std::fprintf(stderr, "[BRDF] Generate enter\n"); std::fflush(stderr);
// fin :
std::fprintf(stderr, "[BRDF] Generate OK\n"); std::fflush(stderr);
```

Dans `Destroy` :
```cpp
std::fprintf(stderr, "[BRDF] Destroy enter\n"); std::fflush(stderr);
std::fprintf(stderr, "[BRDF] Destroy OK\n"); std::fflush(stderr);
```

---

### 7. `engine/render/SpecularPrefilterPass.cpp`

**Tag : `[SPECPF]`**

Même pattern que BRDF : Init (enter + vkCreateImage + OK), Generate (enter + OK), Destroy (enter + OK).

---

### 8. `engine/render/SsaoKernelNoise.cpp`

**Tag : déjà `[SSAO]`** — compléter :

Dans `Destroy` :
```cpp
std::fprintf(stderr, "[SSAO] Destroy enter\n"); std::fflush(stderr);
// après chaque vkDestroy* :
std::fprintf(stderr, "[SSAO] Destroy OK\n"); std::fflush(stderr);
```

---

### 9. `engine/render/GeometryPass.cpp`

**Tag : `[GEOM]`**

Dans `Init` :
```cpp
std::fprintf(stderr, "[GEOM] Init enter device=%p\n", (void*)device); std::fflush(stderr);
// après vkCreateRenderPass :
std::fprintf(stderr, "[GEOM] vkCreateRenderPass r=%d\n", (int)result); std::fflush(stderr);
// après vkCreatePipelineLayout :
std::fprintf(stderr, "[GEOM] vkCreatePipelineLayout r=%d\n", (int)result); std::fflush(stderr);
// après vkCreateGraphicsPipelines :
std::fprintf(stderr, "[GEOM] vkCreateGraphicsPipelines r=%d\n", (int)result); std::fflush(stderr);
// fin :
std::fprintf(stderr, "[GEOM] Init OK\n"); std::fflush(stderr);
```

Dans `Destroy` :
```cpp
std::fprintf(stderr, "[GEOM] Destroy enter\n"); std::fflush(stderr);
std::fprintf(stderr, "[GEOM] Destroy OK\n"); std::fflush(stderr);
```

---

### 10. `engine/render/ShadowMapPass.cpp`

**Tag : `[SHADOW]`**

Même pattern : Init (enter + après chaque vkCreate* majeur + OK), Destroy (enter + OK).

---

### 11. `engine/render/LightingPass.cpp`

**Tag : `[LIGHT]`**

Dans `Init` :
```cpp
std::fprintf(stderr, "[LIGHT] Init enter\n"); std::fflush(stderr);
// après vkCreateRenderPass, vkCreatePipelineLayout, vkCreateGraphicsPipelines :
std::fprintf(stderr, "[LIGHT] vkCreateRenderPass r=%d\n", (int)result); std::fflush(stderr);
std::fprintf(stderr, "[LIGHT] vkCreatePipelineLayout r=%d\n", (int)result); std::fflush(stderr);
std::fprintf(stderr, "[LIGHT] vkCreateGraphicsPipelines r=%d\n", (int)result); std::fflush(stderr);
std::fprintf(stderr, "[LIGHT] Init OK\n"); std::fflush(stderr);
```

Dans `Destroy` — déjà LOG_INFO, ajouter :
```cpp
std::fprintf(stderr, "[LIGHT] Destroy enter\n"); std::fflush(stderr);
std::fprintf(stderr, "[LIGHT] Destroy OK\n"); std::fflush(stderr);
```

---

### 12. `engine/render/TonemapPass.cpp`

**Tag : `[TONEMAP]`**

Même pattern : Init (enter + vkCreate* majeurs + OK), Destroy (enter + OK).

---

### 13. `engine/render/TaaPass.cpp`

**Tag : `[TAA]`**

Dans `Init` :
```cpp
std::fprintf(stderr, "[TAA] Init enter\n"); std::fflush(stderr);
// après vkCreateRenderPass, vkCreatePipelineLayout, vkCreateGraphicsPipelines :
std::fprintf(stderr, "[TAA] vkCreateGraphicsPipelines r=%d\n", (int)result); std::fflush(stderr);
std::fprintf(stderr, "[TAA] Init OK\n"); std::fflush(stderr);
```

Dans `Destroy` :
```cpp
std::fprintf(stderr, "[TAA] Destroy enter\n"); std::fflush(stderr);
std::fprintf(stderr, "[TAA] Destroy OK\n"); std::fflush(stderr);
```

---

### 14. `engine/render/AutoExposure.cpp`

**Tag : `[AUTOEXP]`**

Dans `Init` :
```cpp
std::fprintf(stderr, "[AUTOEXP] Init enter\n"); std::fflush(stderr);
// après chaque vkCreateBuffer/vkAllocateMemory/vkCreateComputePipelines :
std::fprintf(stderr, "[AUTOEXP] vkCreateBuffer histogram r=%d\n", (int)r); std::fflush(stderr);
std::fprintf(stderr, "[AUTOEXP] Init OK\n"); std::fflush(stderr);
```

Dans `Destroy` :
```cpp
std::fprintf(stderr, "[AUTOEXP] Destroy enter\n"); std::fflush(stderr);
std::fprintf(stderr, "[AUTOEXP] Destroy OK\n"); std::fflush(stderr);
```

---

### 15. `engine/render/BloomPass.cpp` (tous les sous-passes)

**Tag : `[BLOOM]`**

Pour `BloomPrefilterPass`, `BloomDownsamplePass`, `BloomUpsamplePass`, `BloomCombinePass` :
```cpp
std::fprintf(stderr, "[BLOOM] <PassName>::Init enter\n"); std::fflush(stderr);
std::fprintf(stderr, "[BLOOM] <PassName>::Init OK\n"); std::fflush(stderr);
std::fprintf(stderr, "[BLOOM] <PassName>::Destroy enter\n"); std::fflush(stderr);
std::fprintf(stderr, "[BLOOM] <PassName>::Destroy OK\n"); std::fflush(stderr);
```

---

### 16. `engine/render/vk/VkDeviceContext.cpp`

**Tag : `[VKDEV]`**

Dans `Create` :
```cpp
std::fprintf(stderr, "[VKDEV] Create enter\n"); std::fflush(stderr);
// après vkCreateDevice :
std::fprintf(stderr, "[VKDEV] vkCreateDevice r=%d device=%p\n", (int)result, (void*)m_device); std::fflush(stderr);
// fin :
std::fprintf(stderr, "[VKDEV] Create OK\n"); std::fflush(stderr);
```

Dans `Destroy` :
```cpp
std::fprintf(stderr, "[VKDEV] Destroy enter\n"); std::fflush(stderr);
// après vkDestroyDevice :
std::fprintf(stderr, "[VKDEV] Destroy OK\n"); std::fflush(stderr);
```

---

### 17. `engine/render/vk/VkSwapchain.cpp`

**Tag : `[SWAPCHAIN]`**

Dans `Create` :
```cpp
std::fprintf(stderr, "[SWAPCHAIN] Create enter w=%u h=%u\n", extent.width, extent.height); std::fflush(stderr);
// après vkCreateSwapchainKHR :
std::fprintf(stderr, "[SWAPCHAIN] vkCreateSwapchainKHR r=%d\n", (int)result); std::fflush(stderr);
// fin :
std::fprintf(stderr, "[SWAPCHAIN] Create OK images=%u\n", (unsigned)m_images.size()); std::fflush(stderr);
```

Dans `Destroy` :
```cpp
std::fprintf(stderr, "[SWAPCHAIN] Destroy enter\n"); std::fflush(stderr);
std::fprintf(stderr, "[SWAPCHAIN] Destroy OK\n"); std::fflush(stderr);
```

---

### 18. `engine/audio/AudioEngine.cpp`

**Tag : `[AUDIO]`**

Dans `Init` :
```cpp
std::fprintf(stderr, "[AUDIO] Init enter\n"); std::fflush(stderr);
std::fprintf(stderr, "[AUDIO] Init OK\n"); std::fflush(stderr);
```

Dans `Shutdown` :
```cpp
std::fprintf(stderr, "[AUDIO] Shutdown enter\n"); std::fflush(stderr);
std::fprintf(stderr, "[AUDIO] Shutdown OK\n"); std::fflush(stderr);
```

---

## Règles communes pour tous les fichiers

1. **Toujours** `std::fflush(stderr)` immédiatement après chaque `std::fprintf`.
2. **Format** : `[TAG] avant <action>` → `[TAG] <action> r=<code> handle=<ptr>` (quand applicable).
3. **Codes de retour Vulkan** : toujours afficher le résultat numérique (`%d`) — pas seulement "OK" ou "FAILED".
4. **Handles** : afficher les pointeurs avec `%p` pour détecter les handles null ou invalides.
5. **Ne pas ajouter** de `fprintf` dans les chemins chauds (Record, boucles de rendu) — uniquement Init, Destroy, et les étapes one-shot critiques.
6. **Ne pas supprimer** les `fprintf` existants — ajouter uniquement.

---

## Critères d'acceptation

- [ ] Les 18 fichiers listés contiennent des `fprintf` aux points identifiés
- [ ] Chaque `fprintf` est immédiatement suivi de `fflush(stderr)`
- [ ] Le boot complet produit une trace lisible de `[FW] Init enter` jusqu'à `[RUN] frame done` frame 0
- [ ] Le shutdown produit une trace de `[PIPELINE] Destroy enter` jusqu'à `[RUN] shutdown complete`
- [ ] Aucun appel `vkCreate*` ou `vkDestroy*` majeur n'est silencieux
- [ ] Build sans warning supplémentaire
- [ ] Le moteur tourne au minimum 1 frame sans crash

---

## Interdit

- Ne pas modifier la logique de code — uniquement ajouter des `fprintf`/`fflush`
- Ne pas ajouter de `fprintf` dans les Record/Execute/boucles chaudes
- Ne pas supprimer les `fprintf` existants
- Ne pas convertir les `fprintf` en `LOG_*` (c'est le rôle de STAB.8, différé)
- Ne pas modifier `AGENTS.md` ni `DEFINITION_OF_DONE.md`
