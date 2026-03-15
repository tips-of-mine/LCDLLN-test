# STAB.12 — Instrumentation fprintf : couverture maximale des zones critiques

**Status:** Closed

---

## Contenu du ticket

Ticket : `tickets/annexe/STAB.12_Instrumentation_fprintf_Coverage.md` — Instrumentation fprintf dans 18 fichiers engine/ (boot, shutdown, Vulkan). Tag [TAG] avant/après chaque étape, fflush(stderr) après chaque fprintf.

---

## Rapport final

### 1) FICHIERS

**Créés :** aucun.

**Modifiés :**
- engine/platform/FileWatcher.cpp
- engine/render/ShaderHotReload.cpp
- engine/render/DeferredPipeline.cpp
- engine/Engine.cpp
- engine/render/AssetRegistry.cpp
- engine/render/BrdfLutPass.cpp
- engine/render/SpecularPrefilterPass.cpp
- engine/render/SsaoKernelNoise.cpp
- engine/render/GeometryPass.cpp
- engine/render/ShadowMapPass.cpp
- engine/render/LightingPass.cpp
- engine/render/TonemapPass.cpp
- engine/render/TaaPass.cpp
- engine/render/AutoExposure.cpp
- engine/render/BloomPass.cpp
- engine/render/vk/VkDeviceContext.cpp
- engine/render/vk/VkSwapchain.cpp
- engine/audio/AudioEngine.cpp

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
- Aucun nouveau dossier racine ; code uniquement sous /engine ; pas de contenu hors game/data ; pas de chemins absolus ; instrumentation strictement conforme au ticket (fprintf stderr + fflush, tags [TAG], pas de log en hot path Record/Execute).
