# STAB.8 — Nettoyage de tous les `fprintf` de debug

**Priorité :** Normale  
**Périmètre :** Tous les fichiers `.cpp` et `.h` sous `engine/` et `game/`  
**Dépendances :** STAB.1 à STAB.7 doivent être livrés **en premier** (ce ticket est le dernier de la série)

---

## Objectif

Supprimer ou convertir l'ensemble des traces `fprintf`, `printf`, `cout`, `cerr` introduites lors des sessions de débogage. Le moteur doit utiliser **exclusivement** les macros `LOG_*` pour toute sortie de diagnostic.

---

## Contexte

Des centaines de `fprintf(stderr, ...)` et `printf(...)` ont été ajoutés lors du débogage du premier frame et des crashes VMA. Ces traces polluent la sortie standard, ne respectent pas le format `[MODULE]` du système de logging, et ne sont pas filtrables par niveau (DEBUG / INFO / WARN / ERROR). Elles doivent toutes être supprimées ou converties.

Ce ticket est volontairement **dernier** : supprimer des traces de debug pendant que des bugs sont encore présents ferait perdre de la visibilité. Il ne doit être exécuté qu'une fois STAB.1–STAB.7 livrés et validés.

---

## Changements requis

### Fichiers concernés (liste non exhaustive — scanner l'intégralité de `engine/` et `game/`)

Fichiers identifiés comme prioritaires lors de l'audit :
- `engine/render/FrameGraph.cpp`
- `engine/render/Engine.cpp`
- `engine/render/GeometryPass.cpp`
- `engine/render/DeferredPipeline.cpp`
- `engine/io/FileSystem.cpp`

### Règle de conversion

Pour chaque occurrence de `fprintf`, `printf`, `cout`, `cerr` :

| Cas | Action |
|---|---|
| Trace temporaire sans valeur diagnostique durable | **Supprimer** |
| Trace utile pour le diagnostic en production | **Convertir** en `LOG_INFO` / `LOG_DEBUG` / `LOG_WARN` / `LOG_ERROR` avec préfixe `[MODULE]` |
| Trace de valeur critique (erreur Vulkan, allocation échouée) | **Convertir** en `LOG_ERROR` |

### Format obligatoire des logs convertis

```cpp
LOG_INFO("[ModuleName] Message descriptif: valeur=%d", valeur);
LOG_ERROR("[ModuleName] Échec de X: vkResult=%d", result);
```

### Commande de vérification post-implémentation

L'agent doit exécuter (ou simuler) la commande suivante sur le scope modifié et confirmer zéro résultat :
```
grep -rn "fprintf\|printf\|std::cout\|std::cerr" engine/ game/ --include="*.cpp" --include="*.h"
```

---

## Critères d'acceptation

- [ ] Zéro occurrence de `fprintf`, `printf`, `std::cout`, `std::cerr` dans `engine/` et `game/`
- [ ] Toutes les traces conservées utilisent exclusivement les macros `LOG_*`
- [ ] Tous les messages convertis respectent le format `[MODULE] message`
- [ ] Le build compile sans warning supplémentaire
- [ ] Le moteur tourne 300+ frames avec la même lisibilité de log qu'avant le nettoyage

---

## Interdit

- Ne pas supprimer des logs `LOG_*` existants
- Ne pas modifier la logique de code — uniquement les instructions de sortie de diagnostic
- Ne pas créer de nouveaux systèmes de logging
- Ne pas modifier `AGENTS.md` ni `DEFINITION_OF_DONE.md`
