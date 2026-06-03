# Issue: SERVER-CORE.37

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- src/shared/platform/CrashContext.h

## Note
Crash dump Windows

---

## Contenu du ticket (SERVER-CORE.37)

# SERVER-CORE.37_Platform_crash_dump_windows_client

## Objectif

Adapter `WheatyExceptionReport` server-core pour le **client Windows
LCDLLN** : crash dump auto-généré sur exception non catchée, avec types
et valeurs des variables locales via DbgHelp. **Très précieux** quand un
joueur crash : on récupère un rapport exploitable sans symboles externes.

Avantage vs Crashpad/Breakpad : zéro dépendance externe, intégration
native Win32, format texte lisible.

C'est un **P3 client**.

## Dépendances

- M00.1 (build base Windows)
- DbgHelp (Win32 API, livré avec Windows SDK)

## Livrables

### Côté client (`engine/client/platform/`)

- `WheatyCrashReport.{h,cpp}` (Windows-only) — adapter server-core :
  - `Install()` — installe `SetUnhandledExceptionFilter`.
  - `static LONG WINAPI Filter(EXCEPTION_POINTERS* ep)` — handler.
  - Génère un fichier `crash_<timestamp>.txt` :
    ```
    [Header] LCDLLN client crash, version, date, OS info
    [Exception] code, address
    [Modules] all loaded DLLs with base + size
    [Stack] frames avec symbols si .pdb dispo
    [Locals] pour chaque frame, variables locales avec types et valeurs
    [Registers] EAX/EBX/...
    [Memory dump] context mémoire autour de l'exception
    ```
- `engine/client/platform/CrashReportConfig.h` — chemins, max files
  retenus.

### Configuration (`config.json`)

```json
"client": {
  "crash_dump": {
    "enabled": true,
    "directory": "crashes",
    "max_files": 20,
    "include_memory_context": true
  }
}
```

### Tests

- Manuel uniquement : provoquer un crash deliberé (`*nullptr = 0`),
  vérifier qu'un `crash_*.txt` est généré et lisible.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- Crash dumps : sous `paths.content + crashes/` (relatif)
- ❌ Interdit : chemins absolus, dossier racine non autorisé

## Spécification technique

### Format dump (extrait simplifié)

```
=== LCDLLN Client Crash Report ===
Version: 0.1.0
Build:   2026-05-07 12:34:56
OS:      Windows 11 (Build 22621)
CPU:     Intel(R) Core(TM) i7-12700K
RAM:     32 GB

Exception Code: 0xC0000005 (ACCESS_VIOLATION)
Exception Address: 0x00007FF7A1234567 (lcdlln_client.exe + 0x1234567)

=== Module List ===
0x00007FF7A1000000 lcdlln_client.exe (build 0.1.0)
0x00007FFE12340000 vulkan-1.dll
0x00007FFE12450000 ntdll.dll
...

=== Stack Trace (Thread 12345) ===
#00 0x00007FF7A1234567 lcdlln_client.exe!engine::render::TerrainRenderer::Render+0x123 (TerrainRenderer.cpp:456)
    Locals:
      this    = 0x000001A234567890 (TerrainRenderer*)
      device  = 0x000001A234567A00 (VkDevice)
      cmd     = 0x000001A234567B00 (VkCommandBuffer)
      heightmap = nullptr (HeightmapData*)        <-- LIKELY CULPRIT

#01 0x00007FF7A1234600 lcdlln_client.exe!engine::Engine::RenderFrame+0x42 (Engine.cpp:1234)
...
```

`heightmap = nullptr` est exactement le genre d'info précieuse pour
diagnostiquer sans avoir le repro.

## Étapes d'implémentation

1. Créer `engine/client/platform/WheatyCrashReport.{h,cpp}` (Windows-only via `#ifdef _WIN32`).
2. Câbler `SetUnhandledExceptionFilter(WheatyCrashReport::Filter)` au boot client.
3. Ajouter `dbghelp.lib` au link pour le client Windows.
4. Implémenter formatage : modules, stack walking, locals via DbgHelp.
5. Smoke test manuel : injecter `*((int*)0) = 0;` en debug → vérifier dump.
6. Doc : section « Client crash reports » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Windows OK (client uniquement, server non concerné)
- [ ] Crash provoqué → fichier `crash_*.txt` généré dans `crashes/`
- [ ] Le dump contient stack trace lisible avec symbols (.pdb chargé)
- [ ] Locals exposés (au moins 1-2 frames hors libs système)
- [ ] Limite `max_files` respectée (purge des plus anciens)
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Symbols .pdb** : sans .pdb à côté de l'exe, pas de noms de fonctions ni de locals. Distribuer `lcdlln_client.pdb` dans le pkg ou le déposer sur un symbol server interne pour les builds publics.
- **Stripped builds** : ne **pas** stripper la table de symbols dans le build Release distribué — sinon dump inutilisable. Ou alors, garder un build "debug-symbols" parallèle pour les crash dumps.
- **Re-entrant crash** : si le handler crash lui-même, infinite loop. Wrap en try/catch avec fallback "minimal dump".
- **Privacy** : un dump peut contenir des données sensibles (noms de fichiers ouverts, contenu de strings). Avant upload externe, audit par l'utilisateur.
- **Upload auto** : pas dans ce ticket. Pour MVP, le joueur partage manuellement ; plus tard, upload optionnel vers un serveur de crash reports.
- **Linux client** ? Pas couvert ici (LCDLLN client = Windows uniquement). Si un jour Linux/Mac, utiliser `signal(SIGSEGV)` + `backtrace()` (autre ticket).

## Références

- `SERVER-CORE_ANALYSIS.md` § Platform (P3 client)
- server-core `src/shared/Platform/WheatyExceptionReport.cpp`
- DbgHelp API : https://learn.microsoft.com/en-us/windows/win32/debug/dbghelp-functions
