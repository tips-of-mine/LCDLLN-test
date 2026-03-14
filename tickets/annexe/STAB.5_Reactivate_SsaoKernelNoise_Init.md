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
