# AGENTS.md — Règles pour Codex (MMORPG Engine)

## But
Codex doit implémenter le projet **ticket par ticket**, en respectant strictement la structure du repo et le périmètre du ticket.

---

## 1) Structure du repo (VERROUILLÉE)
Aucun dossier “contenu” ne doit apparaître à la racine en dehors de ceux-ci :

- `/engine` : **code moteur uniquement** (C++)
- `/game` : **code jeu** + contenu
  - `/game/data` : **contenu** (textures/meshes/json/audio…)
- `/tools` : outils offline (zone_builder, etc.)
- `/external` : dépendances vendored si nécessaire
- `/tickets` : backlog markdown (**lecture seule**)
- fichiers racine autorisés : `CMakeLists.txt`, `CMakePresets.json`, `.gitignore`, `README.md`, `AGENTS.md`, `DEFINITION_OF_DONE.md`, `config.json`

❌ Interdit : créer des dossiers racine du type `/assets`, `/textures`, `/shaders`, `/data` (hors `/game/data`), etc.
❌ Interdit : d'implémenter du code Python, pas de scripts requis
❌ Interdit : Pas de dépendances externes nouvelles (pas de tinygltf, pas de rapidjson si non déjà présent)

---

## 2) Règle “un ticket = une branche = un PR”
- Branche : `feature/<ticket>` (ex: `feature/M3.3`)
- Implémenter **uniquement** le ticket demandé.
- Pas de refactor global, pas de “tant qu’on y est”.

---

## 3) Dépendances
Dépendances autorisées (MVP) :
- C++20, CMake
- GLFW (window/input)
- Vulkan SDK
- fmt (logging) ou équivalent déjà présent
- nlohmann/json (config)

Toute nouvelle dépendance doit être explicitement demandée.

---

## 4) Contenu vs Code (TRÈS IMPORTANT)
- Le moteur **n’embarque pas** de contenu “jeu” dans `/engine`.
- Le contenu de test (textures, meshes) va dans `/game/data/...`
- Les chemins de contenu sont **relatifs** à `paths.content` (config.json) :
  - `fullPath = paths.content + "/" + relativePath`
- Interdit : chemins absolus, chemins hardcodés vers un dossier racine arbitraire (`assets/...` à la racine).

---

## 5) Conventions de nommage et modules
- Namespaces recommandés : `engine::core`, `engine::platform`, `engine::render`, `engine::tools`
- Le code moteur vit sous `/engine/<module>/...`
- `main()` doit rester minimal et appeler `engine::Engine`.

---

## 6) Build & exécution (OBLIGATOIRES)
À la fin de chaque ticket :
- Configurer + compiler (`cmake --preset ...`, puis `cmake --build ...`)
- Lancer l’exécutable si applicable
- Rapporter les commandes + résultats (OK/KO)

---

## 7) Rapport final obligatoire
À la fin du ticket, fournir :
1) Liste des fichiers créés/modifiés/supprimés
2) Liste des commandes exécutées (configure/build/run/tests)
3) Résultats + éventuels problèmes restants (si KO)
4) Confirmation DoD : “Tous les points DoD sont validés” ou liste des points non validés
