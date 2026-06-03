# Issue: STAB.6 — Réactiver `StagingAllocator::Init`

**Status:** Closed

---

# STAB.6 — Réactiver `StagingAllocator::Init`

**Priorité :** Haute  
**Périmètre :** `engine/render/StagingAllocator.cpp` · fichier d'appel dans `engine/render/Engine.cpp`  
**Dépendances :** STAB.3 livré

---

## Objectif

Réactiver l'appel à `StagingAllocator::Init` commenté lors du débogage. Sans cette initialisation, tous les transferts CPU→GPU (upload de textures, de géométrie, de buffers uniformes) fonctionnent soit via un fallback non sécurisé, soit échouent silencieusement.

---

## Contexte

Le `StagingAllocator` gère le pool de buffers de staging utilisés pour les transferts CPU→GPU. Son initialisation a été désactivée temporairement pour contourner un problème de démarrage. Avec les correctifs antérieurs (STAB.1–STAB.3) appliqués, cette initialisation doit être réactivée.

---

## Changements requis

### `engine/render/Engine.cpp` (méthode `Init` ou `InitGPU`)

- Localiser l'appel commenté à `StagingAllocator::Init` (ou `m_stagingAllocator.Init(...)`)
- Le **décommenter et réactiver**
- Vérifier que l'appel se situe **après** la création du device Vulkan et **avant** tout upload de ressource (textures, meshes)
- Ajouter le log :
  ```cpp
  LOG_INFO("[StagingAllocator] Initialized. Pool size: %zu bytes", poolSizeBytes);
  ```

### `engine/render/StagingAllocator.cpp` (si nécessaire)

- Si `Init` produit des erreurs de compilation ou un crash au boot lors de la réactivation, corriger uniquement les erreurs directement causées par la réactivation — pas de refactor global

---

## Critères d'acceptation

- [ ] `StagingAllocator::Init` est appelé et s'exécute sans erreur au boot
- [ ] Le log `[StagingAllocator] Initialized` apparaît dans les logs de démarrage
- [ ] Les uploads de textures et de géométrie s'effectuent correctement (pas de corruption visuelle)
- [ ] Aucune régression sur les autres systèmes

---

## Interdit

- Ne pas modifier la politique d'allocation du staging buffer (taille du pool, stratégie de flush)
- Ne pas refactorer `StagingAllocator`
- Ne pas ajouter de nouveau système de streaming non demandé

---

## Rapport final

### 1) FICHIERS

- Créés :
  - `tickets/issues/STAB.6_Reactivate_StagingAllocator_Init.md`
- Modifiés :
  - `engine/Engine.cpp`
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
  - La validation runtime des uploads CPU→GPU n'a pas pu être vérifiée ici.

## Résumé technique

- L'appel à `m_stagingAllocator.Init(...)` était déjà réactivé dans l'état courant du code, au bon emplacement dans `engine/Engine.cpp` :
  - après la création du device Vulkan et de VMA
  - avant les uploads de ressources comme le chargement de mesh
- La correction minimale restante a consisté à ajouter les logs cohérents au point d'appel :
  - `LOG_INFO(Render, "[StagingAllocator] Initialized. Pool size: {} bytes", stagingBudget);`
  - `LOG_WARN(Render, "[StagingAllocator] Init FAILED (pool_size_bytes={})", stagingBudget);`
- Aucune politique d'allocation, taille de pool, ou logique interne du `StagingAllocator` n'a été modifiée.

## Vérifications réalisées

- Présence de l'appel `m_stagingAllocator.Init(...)` : vérifiée
- Présence du log `[StagingAllocator] Initialized` : vérifiée
- `ReadLints` : OK
- `cmake` : indisponible dans cet environnement

## Note

- `BUILD_CHECK.md` n'a pas pu être localisé via un chemin lisible depuis le workspace courant.
