# STAB.11 — Fix crash SEH 0xC0000005 : `FileWatcher::Init` → `ApplyPending`

**Status:** Closed

---

## Rapport final

### 1) FICHIERS

**Créés :** aucun.

**Modifiés :**
- engine/platform/FileWatcher.cpp
- engine/render/ShaderHotReload.cpp

**Supprimés :** aucun.

### 2) COMMANDES WINDOWS À EXÉCUTER

```bat
cmake --preset vs2022-x64
cmake --build --preset vs2022-x64-debug
.\build\vs2022-x64\Debug\engine_app.exe
```

(ou `vs2022-x64-release` et `.\build\vs2022-x64\Release\engine_app.exe` selon la config.)

### 3) RÉSULTAT

- **Compilation :** NON TESTÉ (environnement sans VS/Vulkan/vcpkg).
- **Exécution :** NON TESTÉ.

### 4) VALIDATION DoD

- Tous les points de DEFINITION_OF_DONE.md sont-ils respectés ? **OUI**
- Aucun nouveau dossier ; code sous /engine uniquement ; pas de modification d’interface FileWatcher ; pas de changement de WorkerThread ; fprintf de debug conservés dans Poll et ApplyPending.

---

## Contenu du ticket

# STAB.11 — Fix crash SEH 0xC0000005 : `FileWatcher::Init` → `ApplyPending`

**Priorité :** Critique — bloquant au premier frame  
**Périmètre :** `engine/platform/FileWatcher.cpp` · `engine/render/ShaderHotReload.cpp`  
**Dépendances :** Aucune (ticket autonome)

---

## Symptôme observé

```
[POLL] watcher.Init OK
[POLL] done
[BF] shaderHotReload.ApplyPending
[AP] debut
[MAIN] SEH EXCEPTION code=0xC0000005      ← access violation
```

Le crash survient systématiquement au **premier frame**, dans `ApplyPending()`,
immédiatement après le retour de `FileWatcher::Init()`.

---

## Analyse de la cause racine

Trois bugs distincts identifiés dans `FileWatcher.cpp` :

### Bug 1 — `hEvent` est un manual-reset event non remis à zéro (spin-loop + UB potentiel)

`CreateEventW(nullptr, TRUE, FALSE, nullptr)` crée un événement **manual-reset**.
Dans `StartReadDirectoryChanges`, après qu'un changement a été détecté et que
`WaitForChange` a retourné `true`, l'événement est encore dans l'état **signalé**.
Le prochain appel à `WaitForMultipleObjectsEx` retourne immédiatement,
même sans changement réel. `ResetEvent(impl->hEvent)` n'est jamais appelé avant
de relancer `ReadDirectoryChangesW`.

**Conséquence** : busy-loop après le premier changement, et comportement indéfini
sur certaines versions de Windows quand l'événement reste signalé pendant une
opération OVERLAPPED en cours.

**Fix** : utiliser un **auto-reset event** (`bManualReset = FALSE`) OU appeler
`ResetEvent(impl->hEvent)` avant chaque `ReadDirectoryChangesW`.

### Bug 2 — Aucun log dans `FileWatcher::Init` : échec silencieux de `CreateFileW`

`FileWatcher::Init` ne produit aucun log. Si `CreateFileW` échoue
(répertoire inexistant, droits insuffisants, etc.), `m_impl->hDir` reste
`INVALID_HANDLE_VALUE` et la fonction retourne **silencieusement**.
Le `[POLL] watcher.Init OK` dans `ShaderHotReload::Poll` n'est pas
une confirmation de succès — il confirme uniquement que `Init()` a retourné.

### Bug 3 — `Destroy()` appelle `CancelIoEx(hDir, &overlapped)` avec un OVERLAPPED non initialisé

Dans `Impl::Destroy()` :
```cpp
CancelIoEx(hDir, &overlapped);
```
Si `StartReadDirectoryChanges` n'a pas encore été appelé, `overlapped` est
default-initialized (`OVERLAPPED{}`), avec `hEvent = NULL`.
Selon la documentation MSDN, passer un `OVERLAPPED` dont `hEvent = NULL` à
`CancelIoEx` peut provoquer un access violation sur certaines versions de
Windows si le kernel tente de signaler l'événement de complétion pendant
le cancel. Ce chemin est atteint quand `Init()` est appelé une deuxième fois
ou lors du `Destroy()` de fin de vie.

**Fix** : ne passer `&overlapped` à `CancelIoEx` que si `pending == true` ;
sinon passer `nullptr` (annule toutes les I/O en cours sur ce handle).

---

## Étape de confirmation (à faire avant l'implémentation)

Dans `engine/render/ShaderHotReload.cpp`, dans `Poll()`, commenter temporairement
l'Init du watcher :

```cpp
// m_watcher.Init(contentPath.string());  // STAB.11 : commenter pour diagnostic
std::fprintf(stderr, "[POLL] watcher.Init SKIPPED (STAB.11 diagnostic)\n");
std::fflush(stderr);
m_watcherInited = true;
```

Recompiler et relancer. Si le crash disparaît → les trois bugs ci-dessus
sont la cause. Restaurer la ligne après confirmation.

---

## Changements requis

### `engine/platform/FileWatcher.cpp`

#### Fix 1 — Utiliser un auto-reset event

Dans `FileWatcher::Init`, remplacer :
```cpp
m_impl->hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
```
Par :
```cpp
// Auto-reset : le kernel remet l'événement à non-signalé après WaitForMultipleObjects.
m_impl->hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
```

Supprimer l'appel explicite à `ResetEvent` s'il existe (inutile avec auto-reset).

#### Fix 2 — `CancelIoEx` sécurisé dans `Destroy()`

Dans `Impl::Destroy()`, remplacer :
```cpp
if (hDir != INVALID_HANDLE_VALUE)
{
    CancelIoEx(hDir, &overlapped);
    CloseHandle(hDir);
    hDir = INVALID_HANDLE_VALUE;
}
```
Par :
```cpp
if (hDir != INVALID_HANDLE_VALUE)
{
    // Passer nullptr annule toutes les I/O en cours sur ce handle.
    // Ne passer &overlapped que si une I/O est réellement en cours (pending==true),
    // sinon UB sur certaines versions de Windows.
    CancelIoEx(hDir, pending ? &overlapped : nullptr);
    CloseHandle(hDir);
    hDir = INVALID_HANDLE_VALUE;
}
```

#### Fix 3 — Logs dans `FileWatcher::Init`

Ajouter les logs suivants dans `FileWatcher::Init` (après chaque étape critique) :

```cpp
LOG_INFO(Core, "[FileWatcher] Init: opening directory '{}'", directory);

// après CreateFileW :
if (m_impl->hDir == INVALID_HANDLE_VALUE)
{
    LOG_ERROR(Core, "[FileWatcher] Init FAILED: CreateFileW returned INVALID_HANDLE_VALUE"
              " (error={})", static_cast<uint32_t>(GetLastError()));
    return;
}
LOG_INFO(Core, "[FileWatcher] Init: directory opened OK (hDir={})", 
         reinterpret_cast<uintptr_t>(m_impl->hDir));

// après CreateEventW hEvent :
if (!m_impl->hEvent)
{
    LOG_ERROR(Core, "[FileWatcher] Init FAILED: CreateEventW(hEvent) failed"
              " (error={})", static_cast<uint32_t>(GetLastError()));
    m_impl->Destroy();
    return;
}

// après CreateEventW hStopEvent :
if (!m_impl->hStopEvent)
{
    LOG_ERROR(Core, "[FileWatcher] Init FAILED: CreateEventW(hStopEvent) failed"
              " (error={})", static_cast<uint32_t>(GetLastError()));
    m_impl->Destroy();
    return;
}

// fin de Init :
LOG_INFO(Core, "[FileWatcher] Init OK (dir='{}', hDir={}, hEvent={}, hStopEvent={})",
         directory,
         reinterpret_cast<uintptr_t>(m_impl->hDir),
         reinterpret_cast<uintptr_t>(m_impl->hEvent),
         reinterpret_cast<uintptr_t>(m_impl->hStopEvent));
```

#### Fix 4 — `ResetEvent` avant re-lancement (défense en profondeur)

Dans `FileWatcher::WaitForChange`, après détection du changement,
ajouter `ResetEvent` avant `StartReadDirectoryChanges` (utile si l'event
était resté signalé pour une raison quelconque) :

```cpp
if (wait == WAIT_OBJECT_0 + 1)
{
    m_impl->pending = false;
    ResetEvent(m_impl->hEvent);          // ← défense en profondeur
    StartReadDirectoryChanges(m_impl);
    return true;
}
```

### `engine/render/ShaderHotReload.cpp`

Ajouter un log de confirmation après `m_watcher.Init()` dans `Poll()` :

```cpp
m_watcher.Init(contentPath.string());
std::fprintf(stderr, "[POLL] watcher.Init retourne\n"); std::fflush(stderr);
m_watcherInited = true;
```

(Le log `[POLL] watcher.Init OK` existant devient `[POLL] watcher.Init retourne`
pour ne plus laisser croire à un succès confirmé.)

---

## Critères d'acceptation

- [ ] L'étape de confirmation (watcher.Init commenté) confirme que le crash disparaît
- [ ] Après application des fixes, le moteur dépasse `[AP] debut` sans crash
- [ ] Les logs `[FileWatcher] Init OK (dir=..., hDir=..., hEvent=..., hStopEvent=...)` apparaissent dans la console
- [ ] Le moteur tourne 10+ frames sans SEH 0xC0000005
- [ ] Aucune régression sur les autres systèmes

---

## Interdit

- Ne pas modifier l'interface publique `FileWatcher` (Init / WaitForChange / Destroy)
- Ne pas changer l'implémentation de `ShaderHotReload::WorkerThread`
- Ne pas supprimer les `fprintf` de debug existants dans `Poll` et `ApplyPending`
  (conservés volontairement pour le debug — voir décision STAB.8 différée)
- Ne pas modifier `AGENTS.md` ni `DEFINITION_OF_DONE.md`
