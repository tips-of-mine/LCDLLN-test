# Issue: STAB.4 — Réactiver le mutex `ApplyPending`

**Status:** Closed

---

# STAB.4 — Réactiver le mutex `ApplyPending`

**Priorité :** Haute  
**Périmètre :** fichier(s) contenant `ApplyPending` (à identifier dans `engine/`)  
**Dépendances :** STAB.3 livré (la boucle doit tourner en multi-frame pour que le mutex soit utile)

---

## Objectif

Réactiver la protection mutex sur `ApplyPending`, désactivée temporairement lors du débogage. Sans ce mutex, les modifications de ressources soumises depuis un thread différent du thread de rendu peuvent provoquer des data races indétectables.

---

## Contexte

Le mutex a été commenté ou bypassé lors d'une session de débogage pour isoler un problème de performance ou de deadlock. Avec la boucle de rendu maintenant stable (STAB.3), il doit être réactivé pour garantir la thread-safety de `ApplyPending`.

---

## Changements requis

### Fichier contenant `ApplyPending` (ex. : `Engine.cpp` ou le système de resource streaming)

- Localiser le bloc commenté ou bypassé autour du mutex dans `ApplyPending`
- **Décommenter / réactiver** la section critique protégée par le mutex
- S'assurer que le mutex est bien de type `std::mutex` ou équivalent et que le lock est un `std::lock_guard` ou `std::unique_lock` (RAII obligatoire — pas de `lock()` / `unlock()` manuels)
- Émettre **en mode DEBUG uniquement** :
  ```cpp
  LOG_DEBUG("[Engine] ApplyPending: mutex acquired, %zu pending ops", pendingOps.size());
  ```

---

## Critères d'acceptation

- [ ] Le mutex est réactivé et protège correctement la section critique de `ApplyPending`
- [ ] Aucun `lock()` / `unlock()` manuel — RAII exclusivement
- [ ] Le moteur tourne 300+ frames sans deadlock ni data race détectable (ThreadSanitizer si disponible)
- [ ] Le log DEBUG apparaît uniquement en build debug
- [ ] Aucune régression sur les autres systèmes

---

## Interdit

- Ne pas modifier la logique métier de `ApplyPending`
- Ne pas changer le type de mutex (ex. : passer à `shared_mutex`) sans demande explicite
- Ne pas ajouter de nouveau threading non demandé

---

## Rapport final

### 1) FICHIERS

- Créés :
  - `tickets/issues/STAB.4_Reactivate_ApplyPending_Mutex.md`
- Modifiés :
  - `engine/render/ShaderHotReload.cpp`
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
  - La validation runtime "300+ frames sans deadlock ni data race détectable" n'a pas pu être vérifiée ici.

## Résumé technique

- `ApplyPending` a été réactivé avec protection systématique par `std::lock_guard<std::mutex>` sur `m_pendingMutex`, y compris sur le chemin `no worker`.
- Aucun `lock()` / `unlock()` manuel n'a été introduit.
- Le log DEBUG demandé a été ajouté sous garde de compilation debug uniquement :
  - `LOG_DEBUG(Render, "[Engine] ApplyPending: mutex acquired, {} pending ops", m_pending.size());`
- Le type de mutex existant (`std::mutex`) a été conservé, sans changement de logique métier.
- Le fichier modifié reçoit aussi les logs init/shutdown requis :
  - `LOG_INFO/LOG_WARN` dans `ShaderHotReload::ShaderHotReload()`
  - `LOG_INFO` dans `ShaderHotReload::~ShaderHotReload()`

## Vérifications réalisées

- Recherche ciblée dans `engine/render/ShaderHotReload.cpp` :
  - mutex actif dans `ApplyPending`
  - RAII via `std::lock_guard<std::mutex>`
  - aucun `lock()` / `unlock()` manuel
  - log DEBUG présent sous `#if !defined(NDEBUG)`
- `ReadLints` : OK
- `cmake` : indisponible dans cet environnement

## Note

- `BUILD_CHECK.md` n'a pas pu être localisé via un chemin lisible depuis le workspace courant.
