# Issue: STAB.5 — Réactiver `SsaoKernelNoise::Init`

**Status:** Closed

---

# STAB.5 — Réactiver `SsaoKernelNoise::Init`

**Priorité :** Haute  
**Périmètre :** fichier(s) SSAO (`engine/render/` — classe ou struct `SsaoKernelNoise`)  
**Dépendances :** STAB.3 livré

---

## Objectif

Réactiver l'appel à `SsaoKernelNoise::Init` qui a été commenté lors d'une session de débogage. Sans cette initialisation, le kernel SSAO et la texture de bruit ne sont pas générés, ce qui produit un SSAO visuellement incorrect (noir, blanc uniforme ou artefacts) sans erreur explicite.

---

## Contexte

L'appel à `SsaoKernelNoise::Init` a été temporairement désactivé pour isoler un problème au boot. Il doit être réactivé à l'emplacement correct dans la séquence d'initialisation du moteur, après que le device Vulkan et les ressources GPU de base sont disponibles.

---

## Changements requis

### Fichier d'appel (ex. : `Engine.cpp`, méthode `Init` ou `InitRenderPasses`)

- Localiser l'appel commenté à `SsaoKernelNoise::Init` (ou équivalent)
- Le **décommenter et réactiver**
- Vérifier que l'appel se situe **après** l'initialisation du device Vulkan et **avant** le premier enregistrement des passes FrameGraph
- Ajouter le log suivant après l'appel :
  ```cpp
  LOG_INFO("[SSAO] Kernel and noise texture initialized (%zu samples)", kernel.size());
  ```

### Fichier `SsaoKernelNoise` (si nécessaire)

- Si `Init` produit des erreurs à la compilation ou au link suite à la réactivation, corriger uniquement les erreurs directement liées à ce ticket — pas de refactor global

---

## Critères d'acceptation

- [ ] `SsaoKernelNoise::Init` est appelé et s'exécute sans erreur au boot
- [ ] Le log `[SSAO] Kernel and noise texture initialized` apparaît dans les logs de démarrage
- [ ] Le SSAO produit un résultat visuellement cohérent (occlusion visible sur les coins et recoins)
- [ ] Aucune régression sur les autres passes

---

## Interdit

- Ne pas modifier les paramètres du kernel SSAO (nombre de samples, rayon, biais)
- Ne pas modifier la passe SSAO FrameGraph
- Ne pas refactorer la classe `SsaoKernelNoise`

---

## Rapport final

### 1) FICHIERS

- Créés :
  - `tickets/issues/STAB.5_Reactivate_SsaoKernelNoise_Init.md`
- Modifiés :
  - `engine/render/DeferredPipeline.cpp`
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
  - La validation visuelle SSAO en runtime n'a pas pu être vérifiée ici.

## Résumé technique

- L'appel à `m_ssaoKernelNoise.Init(...)` était déjà réactivé dans l'état courant du code, au bon endroit :
  - après l'initialisation du device Vulkan et des ressources GPU de base
  - avant l'usage par la passe SSAO
- La correction minimale restante a consisté à ajouter le log demandé juste après l'initialisation réussie :
  - `LOG_INFO(Render, "[SSAO] Kernel and noise texture initialized ({} samples)", static_cast<size_t>(SsaoKernelNoise::kKernelSize));`
- Aucun paramètre du kernel SSAO, aucune passe FrameGraph SSAO, et aucune logique de génération n'ont été modifiés.

## Vérifications réalisées

- Présence de l'appel `m_ssaoKernelNoise.Init(...)` dans `engine/render/DeferredPipeline.cpp` : vérifiée
- Présence du log `[SSAO] Kernel and noise texture initialized` : vérifiée
- `ReadLints` : OK
- `cmake` : indisponible dans cet environnement

## Note

- `BUILD_CHECK.md` n'a pas pu être localisé via un chemin lisible depuis le workspace courant.
