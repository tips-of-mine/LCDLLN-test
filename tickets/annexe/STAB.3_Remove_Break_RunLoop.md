# STAB.3 — Supprimer le `break` temporaire dans `Run()`

**Priorité :** Haute  
**Périmètre :** `engine/render/Engine.cpp` — méthode `Run()`  
**Dépendances :** STAB.1 et STAB.2 doivent être livrés en premier (le break masque potentiellement leurs effets)

---

## Objectif

Supprimer le `break` introduit temporairement dans la boucle principale `Run()` pour contourner un crash en multi-frame. Le moteur doit tourner en boucle continue sans interruption artificielle.

---

## Contexte

Un `break` a été inséré dans la boucle de rendu (`Run()`) comme mesure d'urgence lors du débogage du premier frame. Ce break fait tourner le moteur **pour un seul frame** puis sort de la boucle, ce qui empêche toute validation du comportement multi-frame (Bloom, TAA, SSAO, staging, etc.).

STAB.1 (Bloom) et STAB.2 (TAA) doivent être livrés avant ce ticket, car ils corrigent les causes racines qui avaient motivé ce break.

---

## Changements requis

### `engine/render/Engine.cpp` — méthode `Run()`

- Localiser le `break` dans la boucle principale de rendu
- Le **supprimer**
- Vérifier que la condition de sortie de boucle normale est bien en place (ex. : `glfwWindowShouldClose`, signal d'arrêt, etc.)
- Émettre le log suivant au début de la boucle (si pas déjà présent) :
  ```cpp
  LOG_DEBUG("[Engine] Entering render loop");
  ```
- Émettre à la sortie propre de la boucle :
  ```cpp
  LOG_INFO("[Engine] Render loop exited cleanly");
  ```

---

## Critères d'acceptation

- [ ] Aucun `break` non conditionnel dans la boucle `Run()`
- [ ] Le moteur tourne au minimum 300 frames sans crash ni freeze
- [ ] La fenêtre se ferme proprement sur `glfwWindowShouldClose` ou équivalent
- [ ] Les logs `Entering render loop` et `Render loop exited cleanly` apparaissent respectivement au début et à la fin
- [ ] Aucune régression sur les passes FrameGraph

---

## Interdit

- Ne pas modifier la logique de la boucle (polling events, swapchain, etc.)
- Ne pas ajouter de cap FPS ou de sleep non demandés
