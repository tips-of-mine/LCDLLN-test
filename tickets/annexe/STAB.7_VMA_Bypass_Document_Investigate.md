# STAB.7 — VMA bypass : documenter le workaround et investiguer la cause racine

**Priorité :** Moyenne  
**Périmètre :** `engine/render/FrameGraph.cpp` · `engine/render/FrameGraph.h`  
**Dépendances :** Aucune (ticket autonome, non bloquant pour STAB.1–STAB.6)

---

## Objectif

Le bypass VMA (allocation manuelle via `vkAllocateMemory` + `vkBindImageMemory`) introduit lors du débogage du premier frame doit être :
1. **Documenté clairement** dans le code (commentaire d'intention + référence au ticket)
2. **Investigué** pour identifier la cause racine de l'échec de VMA
3. **Résolu si possible** — retour à VMA si une configuration correcte est trouvée ; maintien du bypass documenté sinon

---

## Contexte

Toutes les approches VMA testées (static, dynamic, fully manual function pointer population) ont échoué sur `vmaCreateImage` pour la première image FrameGraph (SceneColor). Le workaround actuel alloue la mémoire image en raw Vulkan avec sélection manuelle du memory type via `vkGetPhysicalDeviceMemoryProperties`.

Ce workaround fonctionne mais contourne la gestion de pools, la défragmentation et la sous-allocation offertes par VMA. Il représente une dette technique à caractériser précisément.

---

## Changements requis

### `engine/render/FrameGraph.cpp` — fonction `ensureImageResources`

**Étape 1 — Documentation du workaround (obligatoire)**

Ajouter un bloc de commentaire explicite au-dessus du code de bypass :
```cpp
// [STAB.7] VMA BYPASS — Raw Vulkan allocation
// Raison : vmaCreateImage échoue sur cette configuration (SDK/MSVC/VMA version mismatch ?).
// Toutes les approches VMA testées (static/dynamic/manual function pointers) ont échoué.
// Ce bypass utilise vkAllocateMemory + vkBindImageMemory directement.
// TODO STAB.7 : Investiguer la cause racine et restaurer VMA si possible.
```

**Étape 2 — Investigation (best effort)**

Tenter de reproduire et diagnostiquer l'échec VMA en ajoutant temporairement (en build DEBUG uniquement) des traces avant l'appel `vmaCreateImage` :
- Afficher la version VMA (`VMA_VERSION`)
- Afficher si `VmaAllocatorCreateInfo::physicalDevice` et `device` sont non-null
- Afficher le `VkResult` précis retourné par `vmaCreateImage`

```cpp
#ifndef NDEBUG
LOG_DEBUG("[VMA] Version: %u, physicalDevice: %p, device: %p",
    VMA_VERSION, (void*)allocatorInfo.physicalDevice, (void*)allocatorInfo.device);
#endif
```

**Étape 3 — Décision**

- **Si VMA fonctionne** après correction de la configuration → supprimer le bypass, restaurer `vmaCreateImage`, ajouter `LOG_INFO("[FrameGraph] Image allocated via VMA: %s", resourceName);`
- **Si VMA échoue toujours** → conserver le bypass documenté, ajouter `LOG_WARN("[FrameGraph] VMA unavailable for images, using raw Vulkan allocation (STAB.7)");` au boot

---

## Critères d'acceptation

- [ ] Le workaround est documenté avec le commentaire `[STAB.7]` dans `FrameGraph.cpp`
- [ ] Un log au boot indique clairement si VMA ou le bypass raw Vulkan est utilisé
- [ ] L'investigation est tracée dans le PR (résultat de l'analyse, version VMA, SDK, erreur retournée)
- [ ] Aucune régression : les 25 images FrameGraph s'allouent sans crash
- [ ] `vkQueuePresentKHR result=0` toujours obtenu

---

## Interdit

- Ne pas refactorer `FrameGraph` au-delà du scope documentaire et investigatoire
- Ne pas changer la signature de `ensureImageResources` sauf si nécessaire pour la correction VMA
- Ne pas introduire de nouvelle dépendance
