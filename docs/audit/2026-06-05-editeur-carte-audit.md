# Audit de l'éditeur de carte (`lcdlln_world_editor.exe`) — 2026-06-05

Périmètre : `src/world_editor/` (354 fichiers, ~47 000 lignes), compilé dans
`engine_core` et linké par l'exécutable `world_editor_app`. Audit **lecture seule**
mené via 9 analyses parallèles par sous-domaine + vérifications croisées manuelles.

Grille : **Orphelin** · **Anomalie** · **Optimisation** · **Bug**.
Sévérité : Critique / Majeur / Mineur. Confiance : Certain / Probable / À vérifier.

> **Déploiement** : tout est client/éditeur (Windows). Aucune correction listée ici
> n'implique de redéploiement serveur, sauf si on modifiait un layout binaire
> exporté (LCDP/LCVC) — ce qui n'est pas proposé.

---

## Thème 1 — Câblage UI incomplet (le plus structurant)

Beaucoup de code livré « noyau pur + tests » sans branchement à l'UI (stratégie
connue « server-first / 2e passe UI »). Conséquence : du code **compilé et testé
mais inatteignable par l'utilisateur**.

| # | Constat | Réf | Sév. |
|---|---------|-----|------|
| 1.1 | **14 outils inatteignables** : Placement, Foliage, Forest, Field, Hazard, Zone, Spline, WindZone, Bridge, Wall, Hamlet, Selection, Layers, Delete ne sont ni dans l'enum `ActiveTool` ni dans la toolbar. Seuls les 15 outils de l'enum sont câblés. | `WorldEditorShell.h:42-60` | Majeur |
| 1.2 | **Raccourcis clavier outils morts** : `B/N/P/L/R/Ctrl+Shift+{M,V,N,C,H,T,G,O,A,D}` + `F1..F12` jamais déclenchés (Engine ne forwarde que `Z`/`Y`). La toolbar compense pour les 15 outils, mais les raccourcis documentés ne marchent pas. | `WorldEditorShell.cpp:585-716`, `Engine.cpp:7263-7266` | Majeur |
| 1.3 | **`EditorCameraController` entièrement stubbé** : `Update`/`UpdateFPS/Orbital/TopDown` sont des corps vides, jamais appelés → ce contrôleur ne déplace jamais la caméra. `EditorCameraConfig` (vitesses, distances) mort. | `EditorCameraController.cpp:131-166` | Majeur |
| 1.4 | **`ZoneValidator` (M100.48) orphelin runtime** : le service de validation + ses 6 règles MVP ne sont référencés QUE par les tests. Aucun appel depuis `ui/` ou `core/` → la validation ne bloque aucun export en pratique. | `validation/**`, `ZoneValidationTests.cpp` | Majeur |
| 1.5 | **`MinimapPanel` orphelin** : `Render` n'est appelé nulle part en prod (pas un `IPanel`, pas dans `m_panels`). Seule `BuildMinimapView` est testée. | `panels/MinimapPanel.*` | Majeur |
| 1.6 | **`EditorModeRegistry` Subscribe/Notify mort** : le mécanisme d'abonnement (et son anti-réentrance) n'a aucun abonné ; les panels pollent `GetCurrentMode()` chaque frame. | `modes/EditorModeRegistry.*` | Mineur |
| 1.7 | **`ResetLayoutToDefault` ne dispose rien** : les 4 entrées de menu « Default/Sculpting/Painting/Placement Layout » mappent toutes sur un simple `SetVisible(true)`, sans DockBuilder. (Atténué : le menu bar anglais du shell est de toute façon supprimé dans l'exe via `SetMenuBarSuppressed(true)`.) | `WorldEditorShell.cpp:389,750` | Mineur |
| 1.8 | **Éditeur de routines : pas de connexion de nœuds** : aucun `AddLinkCommand`/`RemoveNodeCommand`/`RemoveLinkCommand` n'est poussé depuis l'UI (seul le rendu des liens existe). Conforme à M101.8/9 différés, mais l'éditeur nodal ne permet pas encore de relier les nœuds. | `routine/RoutineGraphPanel.cpp:48` | Mineur |

**Note doc** : plusieurs en-têtes affirment l'inverse du code (persistance du mode
caméra « persistée entre sessions » mais jamais écrite `WorldEditorShell.cpp:319` ;
`EditorViewportRenderTarget` « aucun contenu écrit » alors que le blit existe). Ces
docs trompeuses sont le vrai risque pour les futurs contributeurs.

---

## Thème 2 — Undo/redo non exact (récurrent, sérieux)

Plusieurs commandes violent le contrat « Undo = inverse strict ».

| # | Constat | Réf | Sév. |
|---|---------|-----|------|
| 2.1 | **Clamp dans `ApplyDeltas` casse la réversibilité** : `clamp(oldH ± delta)` appliqué côté commande (pas à la capture). Si une cellule sature à la borne, `Undo` ne restitue pas l'état initial. Touche Macro polyline, érosion hydraulique, érosion thermique/éolienne — alors que les en-têtes promettent un undo « bit-à-bit ». | `MacroPolylineCommandBase.cpp:67`, `HydraulicErosionCommand.cpp:55`, `ThermalWindErosionCommand.cpp:61` | Majeur |
| 2.2 | **Undo asymétrique sur éviction de chunk** : `CoastlineCommand`/`RiverNetworkCommand` font `Find()` (pas de chargement disque) et `continue` si null. Si un chunk est évincé du cache RAM entre Execute et Undo, le delta n'est jamais inversé → terrain reste creusé de façon permanente, sans erreur. | `CoastlineCommand.cpp:104`, `RiverNetworkCommand.cpp:112` | Majeur |
| 2.3 | **Couture splat inter-chunks incohérente** : la cellule miroir du chunk voisin reçoit les poids *de la cellule source* (pas un recalcul) ; le `prev` enregistré ≠ poids réel du voisin. Contredit le commentaire. Bord visuellement cassé. | `SplatPaintTool.cpp:427-444` | Majeur |
| 2.4 | **Couture sculpt** : le delta miroir est clampé indépendamment → divergence des rangées partagées près des bornes (micro-fissures de bord). | `TerrainSculptTool.cpp:157-169` | Majeur |
| 2.5 | **Undo LIFO fragile sur documents partagés** : `FoliagePaintCommand::Undo` fait `RemoveLast(n)` ; sûr uniquement si la commande au sommet est le dernier `Append`. Forest/Field poussent aussi des `FoliagePaintCommand` dans le même document → retrait des mauvaises instances possible. Idem `Delete` (par index) vs `PlaceProps` (par id) sur le même vecteur. | `FoliagePaintCommand.h:26`, `DeleteCommand.cpp:49`, `PlacementCommand.h:33` | Majeur |
| 2.6 | **`PlaceCaveCommand::Undo` lossy** : ne snapshote que le poids de la layer cible + somme des autres ; restaure les autres layers par re-normalisation proportionnelle (double arrondi). Contredit « Undo strict ». | `PlaceCaveCommand.cpp:62,122` | Majeur |
| 2.7 | **`SetEntityTransformCommand` & index instable** : `EntityId.index` = position dans la liste source, non stable après édition structurelle. Une insertion/suppression entre édition et undo fait écrire le transform sur la mauvaise entité ; le `mergeKey (kind,index)` peut aussi fusionner deux entités distinctes. | `EditorSceneModel.h:40`, `SetEntityTransformCommand.cpp:13` | Majeur |

---

## Thème 3 — Robustesse I/O & parsing

| # | Constat | Réf | Sév. |
|---|---------|-----|------|
| 3.1 | **`reserve(count)` non plafonné** : la taille vient d'un champ 32-bit du fichier, réservée *avant* vérification des octets. Un `.bin` corrompu avec `count = 0xFFFFFFFF` → `std::bad_alloc` non catchée → crash au chargement. 3 formats concernés. | `MeshInsertIo.cpp:149`, `DungeonPortalIo.cpp:147`, `VMapBridge.cpp:265` | **Critique** |
| 3.2 | **OOB potentiel dans la règle de pente** : `c.heights[z*resolutionX + x]` sans vérifier `heights.size() == resX*resZ`. La règle censée détecter une heightmap corrompue peut crasher *sur* la donnée corrompue. | `HeightmapRules.cpp:49-60` | Majeur |
| 3.3 | **Round-trip atmosphère cassé** : le writer sérialise un objet imbriqué `"atmosphere": { "time_of_day_h": … }` mais le reader cherche la clé plate `"atmosphere.time_of_day_h"` (qui n'existe jamais) → l'atmosphère n'est jamais relue. Parsing par `find()` global non scopé. | `WorldMapIo.cpp:1310,1471` | Majeur |
| 3.4 | **`dungeonTemplateId` borne 64 o non validée** : un id de 70 o passe la validation et est persisté entier, mais le master le tronque → `TemplateNotFound` au runtime sans alerte éditeur. | `DungeonPortalInstance.h:27`, `Phase11Validator` | Majeur |
| 3.5 | **Parser JSON dupliqué 4× à l'identique** (SkipWs/ReadJsonString/ReadVec3/…) dans les 4 catalogues ; parsing « sans lib » fragile (détection backslash `s[i-1] != '\\'` cassée sur `\\"`). | `CaveCatalog.cpp`, `OverhangCatalog.cpp`, `ArchCatalog.cpp`, `DungeonCatalog.cpp` | Mineur |
| 3.6 | **`create_directories(ec)` ignoré** : 6 sites dans WorldMapIo n'inspectent jamais `ec` → échec d'écriture silencieux/générique (incohérent avec `WorldEditorExporter` qui le gère). | `WorldMapIo.cpp:1261…1879` | Mineur |

> **Vérifié sain** : round-trip write↔read des formats LCMI/LCDP/LCVC (ordre des
> champs identique, bornes de lecture vérifiées, little-endian explicite, version
> rejetée si 0 ou > max). Round-trip SLAP/HAMP OK.

---

## Thème 4 — Simulation numérique (érosion / eau)

| # | Constat | Réf | Sév. |
|---|---------|-----|------|
| 4.1 | **Thermal non conservatif** : le facteur anti-runaway borne l'émission d'une cellule mais pas les retraits cumulés quand elle est aussi voisine descendante d'autres ; `next` peut passer < borne basse (pas de clamp ici). Dérive de masse / altitudes négatives. | `ThermalSimulation.cpp:127-144` | **Critique** |
| 4.2 | **Perte de masse aux bords Est/Nord** : `EmitAt` émet inconditionnellement vers `chunk+1` (non gardé), alors que West/South sont gardés par `chunk>0`. Sur grid 2×2, deltas vers chunk inexistant → silencieusement perdus → non-conservatif asymétrique. | `BilinearGradientSample.cpp:91`, `ThermalSimulation.cpp:48` | Majeur |
| 4.3 | **Params de simulation non validés** : seuls `numDroplets==0` / `width<2` gardés. `erosionRate`/`depositionRate` > 1 ou < 0, `gravity`, `numDroplets` énorme passent → deltas absurdes ou boucle géante sur le main thread (freeze). | `DropletKernel.cpp`, `HydraulicSimulation.cpp:27` | Majeur |
| 4.4 | **Exposition vent biaisée aux bords** : l'échantillon upwind hors-grid renvoie height 0 → toute particule près du bord upwind voit une exposition artificiellement énorme → érosion biaisée. | `WindSimulation.cpp:65-68` | Majeur |
| 4.5 | **Seuil sea-level incohérent** `<` (vent) vs `<=` (hydraulique/thermique) → comportement divergent à exactement sea level. | `WindSimulation.cpp:51` | Mineur |
| 4.6 | **Seuil de convergence thermique dépend de la taille du grid** (`× width × height`) comparé à un transfert partiel → early-exit potentiellement prématuré sur grand grid peu pentu. | `ThermalSimulation.cpp:75-77` | Mineur |
| 4.7 | **Invariance parallèle annoncée mais fausse en flottant** : la doc dit hydraulique « thread-safe / invariant parallèle », mais l'ordre d'addition float changerait le résultat → piège si on parallélise un jour. | `DropletKernel.h:22` | Mineur |

> **Vérifié sain** : échantillonnage bilinéaire aux bords (clamp correct, pas
> d'OOB). Accumulation de flow D8 : pas de cycle possible (pente strictement
> descendante) + garde `visited` → pas de boucle infinie. RNG seedé déterministe.

---

## Thème 5 — Performance / coûts par frame

| # | Constat | Réf | Sév. |
|---|---------|-----|------|
| 5.1 | **Grille viewport recalculée O(n²) chaque frame** : jusqu'à ~200k échantillons `TerrainWorldY` + `WorldToScreen` par frame, sans cache (cacheable tant que caméra/heightmap/maille inchangées). | `WorldEditorImGui.cpp:302-339` | Majeur |
| 5.2 | **Validation Phase 11 reconstruite + ré-exécutée chaque frame** quand l'outil Dungeon Portal est actif : instancie un validateur, recâble 4 catalogues, valide toute la scène, juste pour afficher un compteur. | `ToolPropertiesPanel.cpp:1291` | Majeur |
| 5.3 | **Snapshot du chunk entier (~264 Ko) par tick** : brush Smooth (`TerrainBrush.cpp:135`) et splat in-flight scan O(cells²) (`SplatPaintTool.cpp:345`). | — | Majeur |
| 5.4 | **Watershed/flow recalcul complet à chaque tweak** : D8 + flow accumulation + tri O(N log N) refaits intégralement quand seules les sources changent ; pas de cache. | `WatershedSimulation.cpp:330`, `FlowAccumulation.cpp:26` | Majeur |
| 5.5 | **O(n²) divers** : `DeleteCommand` (`std::find` par prop), `PlacePropsCommand::Undo` (`RemoveById` O(n) × k), `OutlinerPanel` (~8 parcours/frame), `EditorSceneModel::Find` O(n) reconstruit chaque frame, `VMapBridge`/`Phase11Validator` scan catalogue O(N·M). | divers | Mineur |
| 5.6 | **`EditorToolbar` reconstruit chaque frame** (`EditorToolbar toolbar(*this)` dans `RenderFrame`) → ré-alloue `m_orderedTools` (15 entrées). À hisser en membre. | `WorldEditorShell.cpp:358` | Mineur |
| 5.7 | **Copie de grille thermique par passe** (`grid.heights = next` au lieu de `swap`) : jusqu'à 40 passes × 263k floats. Gradient ré-échantillonné 2× par step (réutiliser `hg1`→`hg0`). | `ThermalSimulation.cpp:150`, `DropletKernel.cpp:45,72` | Mineur |

---

## Thème 6 — Zone Presets (risque UX immédiat)

| # | Constat | Réf | Sév. |
|---|---------|-----|------|
| 6.1 | **Pas de transaction / rollback** : `Execute` fait d'abord un reset **destructif irréversible** (pas une commande), puis applique op par op. Si une op échoue/lève après le reset, la carte d'origine est déjà détruite et l'état est partiel incohérent, sans Ctrl+Z possible. | `ZonePresetExecutor.cpp:23`, `WorldMapEditDocumentReset.h:25` | **Critique** |
| 6.2 | **Main thread bloqué sans feedback** : `Execute` est synchrone sur le thread ImGui avec un callback de progression inerte ; les simulations (jusqu'à ~45 s) figent la fenêtre → « ne répond pas » perçu comme crash ; `RequestCancel` inaccessible. | `ZonePresetDialog.cpp:70-76` | Majeur |
| 6.3 | **Docs/UI mensongères sur le câblage** : l'écran de résultat et `ZonePresetExecutor.h` affirment que coastline/river_network/érosions « sont ignorées » — **faux**, elles sont en réalité câblées (12/14). Les vraies non-câblées sont `sculpt_brush`/`splat_paint` (`Unsupported`). Le mappeur est induit en erreur. | `ZonePresetExecutor.h:52`, `ZonePresetDialog.cpp:223` | Majeur |
| 6.4 | **Overlay de tool-preset JAMAIS appliqué (CONFIRMÉ 2026-06-05)** : `OperationParams::Parse` filtre la clé `"preset"` comme structurelle (non stockée dans `m_values`), mais `MaybeApplyToolPreset` la relit via `params.GetString("preset")` → retour anticipé systématique. Le champ structuré `op.toolPresetId` (parsé par `ZonePresetIo`) n'est jamais transmis aux dispatchers. Donc `"preset":"subtle"`/`"sand_and_talus"` sont silencieusement ignorés ; les sims tournent avec defaults + overrides JSON scalaires seulement. Fix trivial. | `OperationParams.cpp:135`, `OperationDispatcher.cpp:514-516,857` | Majeur |
| 6.5 | **`std::stod` bornes hautes non clampées** + re-parsing JSON par op + ré-assemblage `ConsolidatedHeightGrid` à chaque op simulation (4× pour un preset enchaînant 4 sims). | `OperationParams.cpp:48`, `OperationDispatcher.cpp:563…` | Mineur |

---

## Thème 7 — Bugs de génération procédurale

| # | Constat | Réf | Sév. |
|---|---------|-----|------|
| 7.1 | **Densité forêt erronée** : `++accepted` *avant* le filtrage per-asset (pente/altitude/splat) → un point filtré consomme quand même du budget → densité réelle systématiquement < demandée dès qu'il y a des règles. | `ForestFieldGen.cpp:92` | Majeur |
| 7.2 | **Dette latente — pas de bug actif (CONFIRMÉ 2026-06-05)** : `PlacementDocument` n'a **aucun `LoadFromDisk`** (asymétrique avec Water/MeshInsert/DungeonPortal) et n'est pas instancié dans le shell → pas de reload donc pas de collision d'id aujourd'hui. Mais : (a) `SaveToDisk` fait un `trunc` sans load → perte/écrasement des props entre sessions une fois câblé ; (b) `m_nextInstanceId=1` en dur → collision d'id réelle dès qu'un `LoadFromDisk` sera ajouté s'il ne fait pas `nextId = maxChargé+1`. À traiter **au moment du câblage** (Thème 1). | `PlacementDocument.h:46`, `PlacementTool.cpp:53` | Majeur (latent) |
| 7.3 | **`GenerateField` produit (nx+1)·(nz+1) instances** (boucles `<=`) au lieu de `nx·nz` → rangée/colonne en trop, dépassement léger possible. Contredit le commentaire. | `ForestFieldGen.cpp:131` | Mineur |
| 7.4 | **`HamletGen` assetId=0 si tous poids ≤ 0** (pas de garde de retour comme GenerateForest) → maisons avec asset invalide. | `HamletGen.cpp:92` | Mineur |

---

## Thème 8 — Code mort / doublons à nettoyer

- **Bloc `#if 0` de ~245 lignes** (onglets Sculpter/Peindre/… « préservés ») → à
  extraire en historique git. `WorldEditorImGui.cpp:1363-1607`.
- **Champs/membres morts** : `TerrainBrushParams::mirrorX/mirrorZ` (plumbed, jamais
  appliqués), `TerrainStampCommand::m_applied` (écrit jamais lu), `resX` neutralisé
  `(void)`, `FieldTool::FieldCrop`/`m_autoTagSplat`, `PlacementTool::PlacementSnap`/
  `gridSizeMeters` (jamais appliqués), `restrictToSandSplat`/`sandSplatLayerIndex`
  (vent), `interiorAabbMin/Max` (cave), `wallAnchorPoint/coverageRadius` (overhang),
  `archHeight` (arch), `tags`/`thumbnailPath`/`IsNeutral()` (zone presets),
  `TexturePreviewCache::m_pool` (créé jamais utilisé).
- **Doublons** : FNV-1a réécrit 3× (Placement/Forest/Hamlet), `PointInPolygon` 3×,
  parser JSON 4×, assemblage de grille (généralisation `BuildGridFromLoadedChunks`
  manquante pour Watershed/Hydraulic). `MoveVertex`/`ToggleLoop`/`SetGlobalParams`
  des outils macro jamais appelés.
- **Includes morts** : `<unordered_set>` (ThermalWindErosionCommand), `<optional>`
  (CaveTool).
- **`EditorToolbar::BuildLayout`/`HitTest`/`HandleClick` test-only** : le `Render`
  réel refait son propre hit-testing inline → les tests valident une logique qui
  n'est **pas** celle rendue (faux sentiment de couverture), et le `Render` a un
  bug de positionnement (mélange coords absolues + `SetCursorPos`/`SameLine`) →
  zones cliquables potentiellement décalées + double-déclenchement possible.
  `EditorToolbar.cpp:33-94,135-191`.
- **Catégorie `"dungeon"` pour MeshInsert** : acceptée par le validateur mais
  jamais produite par un outil et non gérée par `VMapBridge` (proxy dégénéré) →
  incohérence à trancher. `MeshInsertInstance.h:25`, `Phase11Validator.cpp:103`.

---

## Synthèse — priorités proposées

**P0 — Critiques (crash / perte de données)**
1. `reserve(count)` non plafonné → crash sur `.bin` corrompu (3.1) — *quick win*.
2. Zone Presets sans rollback : reset destructif puis échec = carte irréversiblement cassée (6.1).
3. Thermal non conservatif (altitudes négatives / dérive de masse) (4.1).

**P1 — Majeurs UX / correction**
4. Undo non exact sous clamp + éviction de chunk (2.1, 2.2) ; coutures splat/sculpt (2.3, 2.4).
5. Zone Presets : freeze main thread sans feedback (6.2) + docs/UI mensongères (6.3) + overlay preset à confirmer (6.4).
6. Round-trip atmosphère cassé (3.3) ; densité forêt erronée (7.1) ; id prop au reload (7.2).
7. Perf par frame : grille viewport (5.1), validation Phase 11 (5.2), snapshots par tick (5.3).

**P2 — Câblage / dette**
8. Décider du sort des 14 outils non câblés + raccourcis morts + caméra stubbée (1.1-1.3) : câbler ou retirer.
9. `ZoneValidator` orphelin (1.4), `MinimapPanel` orphelin (1.5).
10. Nettoyage code mort / doublons / docs trompeuses (Thème 8 + en-têtes contradictoires).

---

*Audit lecture seule — aucune modification de code effectuée.*
