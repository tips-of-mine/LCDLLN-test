# Issue: STAB.2 — Fix TAA multi-writer FrameGraph

**Status:** Closed

---

# STAB.2 — Fix TAA multi-writer FrameGraph

**Priorité :** Critique  
**Périmètre :** `engine/render/Engine.cpp` · passe TAA  
**Dépendances :** Aucune (ticket autonome)

---

## Objectif

Éliminer la passe fantôme `TAA_InitHistory` du FrameGraph et consolider sa logique dans la passe TAA principale, afin de supprimer le conflit multi-writer sur la ressource `TAA_History`.

---

## Contexte

La passe `TAA_InitHistory` et la passe TAA principale déclarent toutes deux la ressource `TAA_History` en écriture. Ceci est illégal dans le DAG du FrameGraph sans barrière explicite. La passe `TAA_InitHistory` est un vestige de l'initialisation au premier frame — sa logique doit être absorbée dans le lambda `execute` de la passe TAA principale, conditionnellement au premier frame.

---

## Changements requis

### `engine/render/Engine.cpp`

**Supprimer** l'enregistrement de la passe `TAA_InitHistory` dans le FrameGraph (sa déclaration, son lambda `setup` et son lambda `execute`).

**Dans la passe TAA principale**, modifier le lambda `execute` pour :
1. Détecter si c'est le **premier frame** (ex. : via un `bool m_taaHistoryInitialized` ou en testant un frame counter)
2. Si premier frame : exécuter la logique d'initialisation de l'historique (ex. : clear ou copie de `SceneColor` dans `TAA_History`)
3. Sinon : exécuter le resolve TAA normal

Émettre :
```cpp
LOG_INFO("[TAA] History initialized at frame 0");
```
au moment où l'init est effectuée au runtime.

### `engine/render/Engine.h` (si nécessaire)

- Ajouter `bool m_taaHistoryInitialized = false;` si non déjà présent

---

## Critères d'acceptation

- [ ] La passe `TAA_InitHistory` n'existe plus dans le FrameGraph (ni déclaration, ni lambda)
- [ ] La passe TAA principale gère le premier frame sans passe séparée
- [ ] Aucun conflit multi-writer sur `TAA_History` détecté par le FrameGraph ou les validation layers
- [ ] Le log `[TAA] History initialized at frame 0` apparaît exactement une fois au démarrage
- [ ] Aucune régression sur les autres passes

---

## Interdit

- Ne pas modifier les shaders TAA
- Ne pas modifier d'autres passes FrameGraph hors scope
- Ne pas ajouter de nouveaux paramètres de qualité TAA

---

## Rapport final

### 1) FICHIERS

- Créés :
  - `tickets/issues/STAB.2_Fix_TAA_MultiWriter.md`
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
  - La portée du ticket a été respectée strictement : aucune autre passe FrameGraph ni shader TAA n'a été modifié.

## Résumé technique

- L'état courant du code n'enregistrait déjà plus de passe `TAA_InitHistory` séparée.
- La correction minimale restante dans `engine/Engine.cpp` a consisté à :
  - journaliser explicitement l'initialisation runtime de l'historique via `LOG_INFO(Render, "[TAA] History initialized at frame 0");`
  - sortir immédiatement du lambda `execute` de la passe TAA après l'initialisation du tout premier frame, afin que l'init soit absorbée par la passe TAA principale sans exécuter le resolve TAA normal sur ce même frame.

## Vérifications réalisées

- Absence d'occurrence de `TAA_InitHistory` dans le code moteur courant : vérifiée
- Lints sur les fichiers modifiés : OK
- Disponibilité `cmake` : indisponible ici

## Note

- `BUILD_CHECK.md` n'a pas pu être localisé via un chemin lisible depuis le workspace courant.
