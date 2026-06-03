# Issue: STAB.1 — Fix Bloom multi-writer FrameGraph

**Status:** Closed

---

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

---

## Rapport final

### 1) FICHIERS

- Créés :
  - `tickets/issues/STAB.1_Fix_Bloom_MultiWriter.md`
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
- Pourquoi :
  - `cmake` n'est pas disponible dans l'environnement d'exécution utilisé ici, donc la configuration, la compilation et le lancement n'ont pas pu être validés localement.
  - La structure du repo, la portée du ticket, l'absence de dérive hors scope et les lints sur les fichiers modifiés ont été respectés.

## Résumé technique

- Le code utilisait déjà deux pyramides Bloom séparées (`m_fgBloomDownMipIds` / `m_fgBloomUpMipIds`), ce qui satisfaisait déjà la majeure partie du ticket.
- La correction minimale restante a consisté à :
  - ajouter le log obligatoire `[Bloom] FrameGraph resources registered: ...`
  - faire lire `Bloom_Combine` depuis `m_fgBloomUpMipIds[0]` au lieu de `m_fgBloomDownMipIds[0]`, afin d'utiliser la sortie finale de la chaîne d'upsample et d'éviter toute ambiguïté de lecture/écriture entre phases Bloom.

## Vérifications réalisées

- `ReadLints` : OK
- recherche de `m_fgBloomMipIds` dans le code moteur : aucune occurrence restante
- disponibilité de `cmake` : indisponible dans cet environnement

## Note

- `BUILD_CHECK.md` n'a pas pu être localisé via un chemin lisible depuis le workspace courant ; le ticket a été implémenté à partir des fichiers obligatoires accessibles et de l'état réel du code.
