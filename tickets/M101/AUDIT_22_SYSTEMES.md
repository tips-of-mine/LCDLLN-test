# M101 — Audit des 22 systèmes de référence (éditeur de monde UE5)

> **Objectif.** Prouver, fichier à l'appui, ce qui existe déjà dans l'éditeur de
> monde LCDLLN et son runtime, afin de **ne rien dupliquer**. Chaque ligne mappe
> un système de référence (vocabulaire Unreal Engine 5) sur le(s) ticket(s) M100
> et/ou les fichiers source réels du dépôt `tips-of-mine/LCDLLN-test` (branche
> `main`, audit du 2026-06-03).
>
> **Méthode.** Audit en lecture seule de l'arbre de travail local (qui *est* le
> dépôt public — remote `origin`), pas d'un clone `--depth=1` redondant. Tous les
> chemins ci-dessous ont été vérifiés.

## Légende des statuts

| Statut | Sens |
|--------|------|
| **Couvert** | Un ou plusieurs tickets M100 livrent le système, et/ou le code existe. |
| **Partiel** | Le besoin est partiellement adressé ; le manque est précisé. |
| **Absent** | Aucun ticket ni code ; candidat à un ticket neuf (non créé par cet audit, sauf le cluster routines M101). |

## Tableau de cartographie

| # | Système UE5 | Statut | Ticket(s) M100 / fichiers source | Manque éventuel |
|---|-------------|--------|----------------------------------|-----------------|
| 1 | Terrain heightmap sculptable (raise/lower/smooth/flatten) | **Couvert** | M100.5 (Heightmap), M100.6 (Sculpting Brushes) ; `src/world_editor/terrain/TerrainSculptTool.h`, `TerrainBrush.h`, `TerrainSculptCommand.h` | — |
| 2 | Splines de terrain (routes/rivières conformées) | **Couvert** | M100.29 (Spline Tool & Roads), M100.36 (River Network) ; `src/world_editor/water/RiverNetworkTool.h`, `MacroPolylineCommandBase.h` | — |
| 3 | Outils de sculpt avancés (noise/ramp/érosion) | **Couvert** | M100.7 (Stamps & Procedural Generators), M100.38 (Hydraulic Erosion), M100.39 (Thermal & Wind Erosion) ; `src/world_editor/terrain/erosion/*`, `ProceduralStampGenerators.h` | — |
| 4 | Foliage instancié (densité, règles pente/altitude) | **Couvert** | M100.18 (Vegetation Library & Density Painting), M100.19 (Procedural Forest & Field) ; `instances/foliage.bin`, `src/world_editor/ui/TreeSpeciesCatalog.h` | — |
| 5 | Placement manuel / drag & drop / snapping | **Couvert** | M100.17 (Easy Placement Tool) | — |
| 6 | PCG — génération procédurale par règles | **Partiel** | Règles dédiées par outil : M100.19 (forêt/champ), M100.31 (Hamlet Generator), M100.46 (Zone Presets), M100.50 (Quick Start Wizard, seed déterministe) | Pas de **graphe PCG générique** façon UE (règles arbitraires composables). Le VM de routines M101 **n'est pas** un système PCG (à ne pas confondre). Candidat ticket futur. |
| 7 | Partitionnement du monde (grille de cellules + streaming) | **Couvert** | M09/M10 + `src/client/world/WorldModel.h`, `StreamingScheduler.h`, `StreamCache.h`, `ZonePreloadHook.h` ; grille `N000_E000` (zones 4 km / chunks 256 m) | — |
| 8 | Data Layers (couches togglables) | **Couvert** | M100.34 (Selection, Layers, Minimap & Save/Load Zone) | — |
| 9 | Level Instances (sous-ensembles réutilisables, type hamlet) | **Partiel** | M100.31 (Hamlet Generator), M100.46 (Zone Presets Library) | Pas de **level instance générique imbriquée** éditable in-place. Couvert pour les cas concrets (hamlet, presets). Complément éventuel. |
| 10 | World Outliner (arborescence d'acteurs) | **Couvert** | `src/world_editor/panels/OutlinerPanel.h/.cpp` + M100.34 (sélection/layers) | — |
| 11 | Gizmos de transform + snapping | **Couvert** | M100.1 (shell + sélection/gizmos), M100.17 (snapping / align-to-ground) | — |
| 12 | Pivot Tool | **Partiel** | Pivot configurable sur props interactifs (M100.32 `pivotLocal`/`axisLocal`) + point focal orbital (M100.4) | Pas d'**outil Pivot global** (repositionnement d'origine d'un acteur). Complément mineur. |
| 13 | Viewports multiples (perspective + orthos) | **Partiel** | M100.4 (Editor Camera Modes : FPS / Orbital / **TopDownOrtho**) ; `src/world_editor/render/EditorViewportRenderTarget.h` (cible unique) | Un seul viewport, modes **commutables** (perspective + ortho dispo), pas **simultanés** (pas de quad-view). Complément éventuel. |
| 14 | Bookmarks de caméra | **Absent** | — | M100.4 déclare explicitement les bookmarks **Hors scope / no-op / « futur »** (lignes 26, 175-177). Candidat petit ticket. |
| 15 | Outil de mesure | **Absent** | — | Aucun `Ruler`/`Measure` dans `src/`. Candidat petit ticket. |
| 16 | Sky Atmosphere / nuages volumétriques / cycle jour-nuit | **Couvert** (nuages **Partiel**) | M100.24 (Sun, Sky & Probes), M100.25 (Season & Time-of-Year, cycle), M100.22 (Volumetric Fog) | **Nuages volumétriques** dédiés non spécifiés (le fog volumétrique existe). Complément éventuel. |
| 17 | Post Process Volumes | **Partiel** | Post-FX global : M07 (TAA), M08 (Bloom / auto-exposure / color grading) ; zones atmosphère M100.23/24 | Pas de **Post Process Volume générique** éditable par volume (override local de paramètres post). Complément éventuel. |
| 18 | Lighting Scenarios / probes IBL | **Couvert** (scenarios **Partiel**) | M100.24 (Probes Editor IBL), M05 (IBL split-sum) ; cycle jour/nuit via M100.25 | Pas de **lighting scenarios** génériques (presets d'éclairage commutables multi-états). Complément éventuel. |
| 19 | Brouillard (height fog / volumétrique) | **Couvert** | M100.22 (Volumetric Fog Volumes), M100.23 (Distance & Height Fog Tuning) | — |
| 20 | NavMesh (génération pour pathfinding PNJ) | **Partiel** | Segment `Nav` du chunk (`ChunkSegment::Nav`), M100.44 (VMap Bridge : LOS / GetHeight / Raycast serveur) | Pas de **génération navmesh dédiée au pathfinding PNJ** ni de pathfinding PNJ runtime spécifié. Lié à un futur milestone PNJ. |
| 21 | Blocking / Trigger Volumes | **Couvert** | M100.16 (Hazard Volume System), M100.28 (Gameplay Zones & Weather Zones) | — |
| 22 | Nav Modifier Volumes (zones de coût IA) | **Absent** / **Partiel** | M100.28 (gameplay zones) approche le concept | Pas de **modificateur de coût de nav** pour pathfinding IA. Lié au futur PNJ/nav. |

## Le manque réel : éditeur de routines visuel nodal

| # | Système | Statut | Constat |
|---|---------|--------|---------|
| **23** | **Éditeur de routines visuel nodal (type n8n / Blueprint / StateTree)** | **Absent** | Aucune lib node-graph réutilisable dans `src/` (ni `ImNodes`, ni `NodeEditor`, ni `RoutineGraph`). Les tickets `M43.1` (material node graph) et `M43.3` (quest flowchart) sont **d'anciens tickets jamais implémentés**, au format obsolète `/engine/` + DoD Windows-only, et ne fournissent **aucun code**. |

→ **C'est l'objet du cluster M101** (voir `INDEX.md`).

## Systèmes Absent / Partiel — candidats à un ticket neuf (NON créés ici)

Conformément aux règles d'exécution, l'audit **liste** ces manques sans créer de
ticket (hors cluster routines M101) :

| Système | Reco | Justification |
|---------|------|---------------|
| Bookmarks de caméra (#14) | **Complément M100.4 bis** | Petit ajout (`Save view 1..9`) déjà anticipé en « futur » par M100.4. Faible effort. |
| Outil de mesure (#15) | **Ticket neuf léger** | Outil overlay 2D/3D simple, indépendant. |
| Viewports multiples simultanés (#13) | **Ticket neuf (optionnel)** | Quad-view ortho. Effort moyen (multi-render-target). Priorité basse. |
| Pivot Tool global (#12) | **Complément M100.17** | Repositionnement d'origine d'acteur. Faible effort. |
| Graphe PCG générique (#6) | **Milestone séparé** | Système conséquent ; ne pas confondre avec M101 (routines ≠ PCG). |
| Level Instances génériques (#9) | **Complément M100.46** | Étendre les zone presets en instances imbriquées éditables. |
| Nuages volumétriques (#16) | **Ticket neuf rendu** | Effort rendu non trivial. Priorité basse. |
| Post Process Volumes (#17) | **Complément M100.23/24** | Override local de paramètres post par volume. |
| Lighting Scenarios (#18) | **Complément M100.24** | Presets d'éclairage commutables. |
| NavMesh PNJ + Nav Modifier Volumes (#20, #22) | **Milestone PNJ futur** | Dépend de l'infra PNJ (Role Registry / pathfinding) **absente** — voir note ci-dessous. |

## Note critique : infrastructure PNJ largement absente

L'audit du runtime (cf. §1bis du prompt directeur) révèle un écart important entre
les hypothèses et le code réel :

| Concept attendu | Réalité dans le code |
|-----------------|----------------------|
| `EventAI` (machine à états IA data-driven côté shard) | **EXISTE** — `src/shardd/ai/EventAI.h` + `EventAIRuntime.h`. Table simple : 6 triggers (`Timer`, `HpPctBelow`, `OnAggro`, `OnSpawn`, `OnDeath`, `OnTargetHpPctBelow`), 6 actions (`Say`, `Cast`, `Flee`, `Summon`, `Despawn`, `Custom`). **Ce n'est PAS** un StateTree hiérarchique à sélection par utilité. |
| `Role Registry` | **ABSENT** du code (`RoleRegistry` introuvable). |
| `Utility AI` | **ABSENT** (le pattern data-driven d'`EventAI` n'est pas de l'Utility AI). |
| `Smart Objects` | **ABSENT** (`SmartObject` introuvable). |
| `AI-LOD` | **Spécifié en ticket uniquement** — enum `AiLod` (Near/Medium/Far/Hibernate) dans `tickets/CHAR-MODEL/CHAR-MODEL.37_AmbientLifeSystem.md`, **pas encore en code**. |
| `ARCHITECTURE_PNJ.md` | **N'EXISTE PAS**. Docs réels : `docs/superpowers/specs/2026-06-02-dialogue-pnj-design.md`, `docs/superpowers/plans/2026-06-02-dialogue-pnj.md`, ticket `CHAR-MODEL.37`. |

**Conséquence pour M101.** La cible PNJ (cible A) ne peut pas s'appuyer sur Role
Registry / Smart Objects (inexistants). Le ticket **M101.7 est `Blocked`** : il
documente ses dépendances manquantes et fournit, en attendant, l'intégration
**réaliste** disponible — la génération d'`EventAIRow` depuis le graphe, consommée
par l'`EventAIRuntime` existant. Décision actée avec Hubert (2026-06-03).

## Écarts factuels corrigés par rapport au prompt directeur

| Affirmation du prompt | Réalité vérifiée | Correction appliquée dans M101 |
|-----------------------|------------------|--------------------------------|
| `kChunkMetaHasRoutines = 1u << 5` | Bit 5 = `kChunkMetaHasTerrain`, bit 6 = `kChunkMetaHasSplat` (déjà pris) | **`kChunkMetaHasRoutines = 1u << 7`** (M101.3) |
| `ChunkSegment::Routines` ajouté en fin | `kChunkSegmentCount` actuel = 7 (Geo…Probes) | `Routines = 7`, `kChunkSegmentCount → 8` ✓ correct |
| Writer dans `tools/zone_builder/lib/ChunkPackageWriter.*` | Chemin réel : `tools/zone_builder/lib/Public/zone_builder/ChunkPackageWriter.h` | Chemin corrigé dans M101.3 |
| Pattern VM « `engine_sim` » | **`engine_sim` n'existe pas** | M101.2 décrit une lib pure autonome `routine_vm` (aucune référence à `engine_sim`) |
| Opcodes `kOp*` dans `ProtocolV1Constants.h` | Le gameplay passe par l'enum `MessageKind` dans `ServerProtocol.h` (`kProtocolVersion = 8`) ; gabarit = `GameEventPayloads.h` (`GameEventErrorCode{Ok=0,…}` + Request/Response + Notification) | M101.8 suit le gabarit `GameEventPayloads.h` et l'enum `MessageKind` |
| `ARCHITECTURE_PNJ.md` (dépendance) | N'existe pas | Remplacé par les docs réels (cf. ci-dessus) |

---

*Audit produit le 2026-06-03. Lecture seule, aucun fichier de production modifié.*
