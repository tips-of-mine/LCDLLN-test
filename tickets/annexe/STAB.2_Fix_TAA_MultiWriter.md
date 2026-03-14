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
