# STAB.1 — Fix Bloom multi-writer FrameGraph

**Priorité :** Critique  
**Périmètre :** `engine/render/Engine.h` · `engine/render/Engine.cpp`  
**Dépendances :** Aucune (ticket autonome)

---

## Objectif

Éliminer le conflit multi-writer sur les ressources Bloom dans le FrameGraph.  
Actuellement, `m_fgBloomMipIds` est utilisé à la fois par les passes de downsample et d'upsample, ce qui constitue un accès concurrent illégal dans le DAG du FrameGraph.

---

## Contexte

Le FrameGraph interdit qu'une même ressource soit déclarée en écriture par plusieurs passes sans barrière explicite. Les passes de downsample Bloom et d'upsample Bloom écrivent toutes deux dans `m_fgBloomMipIds`, provoquant un comportement indéfini et un risque de corruption visuelle ou de crash en validation layer.

---

## Changements requis

### `engine/render/Engine.h`

- Supprimer le membre : `std::vector<ResourceId> m_fgBloomMipIds;`
- Ajouter à la place :
  ```cpp
  std::vector<ResourceId> m_fgBloomDownMipIds; // MIP produits par les passes de downsample
  std::vector<ResourceId> m_fgBloomUpMipIds;   // MIP produits par les passes d'upsample
  ```

### `engine/render/Engine.cpp`

- Lors de l'enregistrement des passes Bloom dans le FrameGraph :
  - Les passes de **downsample** lisent `m_fgBloomDownMipIds[i-1]` et écrivent `m_fgBloomDownMipIds[i]`
  - Les passes de **upsample** lisent `m_fgBloomUpMipIds[i+1]` (ou `m_fgBloomDownMipIds` pour le premier upsample) et écrivent `m_fgBloomUpMipIds[i]`
- Mettre à jour **toutes** les références à `m_fgBloomMipIds` dans ce fichier pour pointer vers le bon vecteur selon la phase (down ou up)
- Émettre un `LOG_INFO("[Bloom] FrameGraph resources registered: %zu down + %zu up mips", m_fgBloomDownMipIds.size(), m_fgBloomUpMipIds.size());` après l'enregistrement

---

## Critères d'acceptation

- [ ] `m_fgBloomMipIds` n'existe plus dans `Engine.h` ni dans `Engine.cpp`
- [ ] Aucune passe de downsample n'écrit dans un ID déclaré en écriture par une passe d'upsample
- [ ] Le FrameGraph compile et s'exécute sans erreur de validation layer liée aux ressources Bloom
- [ ] Le log `[Bloom] FrameGraph resources registered` apparaît au boot
- [ ] Aucune régression sur les autres passes (SceneColor, TAA, SSAO…)

---

## Interdit

- Ne pas modifier la logique de rendu Bloom (shaders, paramètres de flou, intensité)
- Ne pas refactorer d'autres systèmes hors scope
- Ne pas anticiper M19+
