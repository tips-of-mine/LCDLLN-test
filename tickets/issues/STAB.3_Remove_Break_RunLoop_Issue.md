# Issue: STAB.3 — Supprimer le `break` temporaire dans `Run()`

**Status:** Closed

---

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

---

## Rapport final

### 1) FICHIERS

- Créés :
  - `tickets/issues/STAB.3_Remove_Break_RunLoop.md`
- Modifiés :
  - `engine/Engine.cpp`
- Supprimés (si explicitement demandé) :
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
  - La validation runtime "300 frames sans crash ni freeze" n'a pas pu être vérifiée ici.

## Résumé technique

- Aucun `break` non conditionnel n'était encore présent dans `Run()` dans l'état courant du code.
- La correction minimale restante a consisté à ajouter les logs demandés par le ticket :
  - `LOG_DEBUG(Core, "[Engine] Entering render loop");`
  - `LOG_INFO(Core, "[Engine] Render loop exited cleanly");`
- La condition de sortie normale de boucle est déjà en place :
  - `!m_quitRequested && !m_window.ShouldClose()`

## Vérifications réalisées

- Recherche ciblée dans `engine/Engine.cpp` :
  - log d'entrée présent
  - log de sortie présent
  - aucun `break;` dans la boucle `Run()`
- `ReadLints` : OK
- `cmake` : indisponible dans cet environnement

## Note

- `BUILD_CHECK.md` n'a pas pu être localisé via un chemin lisible depuis le workspace courant.
