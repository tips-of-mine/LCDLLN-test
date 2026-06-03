# Issue: STAB.7 — VMA bypass : documenter le workaround et investiguer la cause racine

**Status:** Closed

---

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

---

## Rapport final

### 1) FICHIERS

- Créés :
  - `tickets/issues/STAB.7_VMA_Bypass_Document_Investigate.md`
- Modifiés :
  - `engine/render/FrameGraph.h`
  - `engine/render/FrameGraph.cpp`
- Supprimés :
  - Aucun

### 2) COMMANDES WINDOWS À EXÉCUTER

- `cmake --preset vs2022-x64`
- `cmake --build --preset vs2022-x64-debug`
- `.\build\vs2022-x64\pkg\game\lcdlln.exe`

### 3) RÉSULTAT

- Compilation : NON TESTÉ
- Exécution   : NON TESTÉ

### 4) VALIDATION DoD

- Tous les points de `DEFINITION_OF_DONE.md` sont-ils respectés ?
  - NON
- Si NON, expliquer précisément pourquoi.
  - `cmake` n'est pas disponible dans l'environnement d'exécution utilisé ici, donc la configuration, la compilation et le lancement n'ont pas pu être validés localement.
  - `ReadLints` sur les fichiers modifiés : OK.
  - La validation runtime complète ("25 images FrameGraph", `vkQueuePresentKHR result=0`) n'a pas pu être vérifiée ici.

## Résumé technique

- Le workaround raw Vulkan a été documenté directement dans `engine/render/FrameGraph.cpp` avec le bloc `[STAB.7]` demandé.
- L'investigation best effort a été ajoutée en build DEBUG uniquement :
  - log `VMA_VERSION`
  - log des handles `physicalDevice` et `device` issus de `VmaAllocatorInfo`
  - log du `VkResult` précis retourné par `vmaCreateImage`
- La décision de runtime est maintenant :
  - essayer `vmaCreateImage` d'abord
  - si VMA réussit : utiliser VMA et loguer `"[FrameGraph] Image allocated via VMA: ..."`
  - si VMA échoue : fallback raw Vulkan documenté et log unique `"[FrameGraph] VMA unavailable for images, using raw Vulkan allocation (STAB.7)"`
- Le handle d'allocation image est désormais distingué entre VMA et raw Vulkan pour permettre une destruction correcte dans les deux cas.

## Résultat de l'investigation

- Analyse statique : l'état courant du code utilisait toujours le bypass raw Vulkan sans tentative active de retour à `vmaCreateImage`.
- Action réalisée : tentative VMA restaurée avec fallback immédiat si échec.
- Cause racine : NON CONFIRMÉE dans cet environnement faute d'exécution locale, mais les traces DEBUG nécessaires à l'investigation sont maintenant en place.

## Vérifications réalisées

- `ReadLints` : OK
- Présence du commentaire `[STAB.7]` : OK
- Présence du log de décision VMA/fallback : OK
- `cmake` : indisponible dans cet environnement

## Note

- `BUILD_CHECK.md` n'a pas pu être localisé via un chemin lisible depuis le workspace courant.
