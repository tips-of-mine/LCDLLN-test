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
