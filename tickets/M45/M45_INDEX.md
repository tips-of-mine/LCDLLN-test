# M45 — Cluster « Profondeur visuelle AAA » — INDEX

## But du cluster
Faire franchir au rendu du client le seuil visuel AAA en **complétant** les deux couches partielles du pipeline (atmosphère/profondeur et éclairage global) et en **étendant** la chaîne de post-traitement, sans rien casser de l'existant.

Ce cluster s'appuie sur un pipeline déjà mature : frame graph (`src/client/render/FrameGraph.*`), rendu différé (`DeferredPipeline`, `GeometryPass`, `LightingPass`), culling GPU + Hi-Z, ombres cascadées (`CascadedShadowMaps`), SSAO, TAA, bloom, auto-exposure, tonemap, ciel procédural (`SkyPass`), cycle jour/nuit, météo, eau, streaming par chunks (`src/client/world/StreamingScheduler` + `StreamCache`), HLOD (`HlodRuntime`), LOD terrain (`TerrainLodChain`).

## Règle de chemins (IMPORTANT — lire avant tout)
La structure **réelle** du dépôt n'utilise PAS `/engine/`. Les anciens tickets mentionnant `/engine/` sont obsolètes sur ce point. Les chemins réels sont :
- Code client de rendu : `src/client/render/...`
- Code client monde/streaming : `src/client/world/...`
- Shaders : `game/data/shaders/...` (un `.vert`/`.frag`/`.comp` source + son `.spv` compilé)
- Contenu de jeu : `game/data/...`, toujours résolu via `paths.content` de `config.json`, jamais en dur.
- `CMakeLists.txt` racine doit être mis à jour à chaque ticket qui ajoute un fichier.

Avant d'écrire le moindre chemin, Code doit `git clone` le dépôt et vérifier la structure réelle. Ne jamais inventer de chemin.

## Principe directeur de non-régression (s'applique à TOUS les tickets)
Chaque nouvelle passe ou fonctionnalité doit être :
1. **Additive et gated** : insérée dans le frame graph via `m_frameGraph.addPass(...)` en suivant le pattern existant (voir `src/client/app/Engine.cpp`), et activable/désactivable par un flag. Si la passe échoue à s'initialiser (shader manquant, etc.), elle se désactive proprement et le pipeline retombe sur le comportement actuel — exactement comme `m_skyPassReady` le fait déjà.
2. **Sans nouveau writer sur une ressource déjà écrite** sauf passe dédiée : le frame graph MVP interdit deux writers sur la même ressource. Respecter ce contrat.
3. **Sans modifier l'ordre des passes existantes** ni leurs signatures publiques, sauf mention explicite dans le ticket.
4. **Vérifiée par les tests existants** : tous les tests déjà présents (`*Tests.cpp`) doivent continuer à passer. Aucun test existant ne doit être modifié pour « faire passer » du nouveau code.

## Ordre d'implémentation (dépendances strictes)

| Ordre | Ticket | Titre | Couche | Risque |
|-------|--------|-------|--------|--------|
| 1 | M45.1 | Aerial perspective (scattering atmosphérique étendu) | Atmosphère | Faible |
| 2 | M45.2 | Volumetric fog (brouillard volumique + god rays) | Atmosphère | Moyen |
| 3 | M45.3 | Depth of field (bokeh) | Composition | Faible |
| 4 | M45.4 | Génération offline des impostors octaédriques | Géométrie/LOD | Moyen |
| 5 | M45.5 | Rendu runtime des impostors végétation | Géométrie/LOD | Moyen |
| 6 | M45.6 | GI dynamique — phase 1 : structure DDGI + intégration data | Éclairage | Élevé |
| 7 | M45.7 | GI dynamique — phase 2 : injection + propagation runtime | Éclairage | Élevé |
| 8 | M45.8 | GI dynamique — phase 3 : tuning, debug, fallback probes statiques | Éclairage | Élevé (itératif) |

## Logique de l'ordre
- **M45.1–M45.3** d'abord : meilleur rapport effort/résultat, faible risque, s'appuient sur des systèmes déjà solides (`SkyPass`, chaîne de post-process). Ce sont des extensions, pas du greenfield.
- **M45.4–M45.5** ensuite : indépendants de l'éclairage, donnent la densité de végétation caractéristique AAA. M45.4 (offline) doit précéder M45.5 (runtime) car le runtime consomme les assets produits par l'outil.
- **M45.6–M45.8** en dernier : le seul vrai gros chantier. Découpé en 3 phases pour rester scopé. La phase 3 garantit explicitement le fallback sur le système de probes IBL statiques existant (`ProbeData`/`ProbeSet`) — donc même si la GI dynamique est désactivée, le rendu reste correct.

## Recommandation de livraison
Livrer et valider M45.1, M45.2, M45.3 en premier lot (gain visuel immédiat, risque faible). Ne lancer M45.6+ qu'une fois ce premier lot stable et mergé.

Un ticket = une branche = une PR. Fichiers livrés complets (jamais en diff/patch).
