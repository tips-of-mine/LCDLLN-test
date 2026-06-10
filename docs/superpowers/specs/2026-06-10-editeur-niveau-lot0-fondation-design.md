# Éditeur de niveau — Lot 0 : Fondation d'édition (Selection / Delete / Layers)

- **Date** : 2026-06-10
- **Périmètre** : `src/world_editor/` + routage input éditeur dans `src/client/app/Engine.cpp`
- **Statut** : design validé, prêt pour plan d'implémentation
- **Déploiement** : ✅ client/éditeur uniquement — **pas de redéploiement serveur** (aucun
  opcode, aucun format binaire exporté, aucune migration DB touchés).

---

## 1. Contexte & motivation

L'éditeur de monde (`lcdlln_world_editor.exe`, ~47 000 lignes) embarque beaucoup de code
« noyau + tests » jamais branché à l'UI. L'audit du 2026-06-05
(`docs/audit/2026-06-05-editeur-carte-audit.md`, Thème 1) a recensé **14 outils
inatteignables**. Rendre l'éditeur réellement utilisable passe d'abord par une **fondation
d'édition** : pouvoir **sélectionner**, **supprimer** et **organiser en calques** les objets
déjà posés. Sans elle, tout outil de pose (Placement, Forest, Bridge…) est inexploitable :
on ne peut ni re-sélectionner, ni effacer, ni masquer ce qu'on a placé.

Ce Lot 0 est le premier d'une série de lots de câblage. Lots suivants (hors périmètre ici) :
Lot 1 (Bridge/Wall/Spline), Lot 2 (Placement/Foliage/Forest), Lot 3 (Zone/Hazard/WindZone/
Field/Hamlet).

## 2. Décision d'architecture centrale : le modèle d'objets cible

Il existe **deux modèles d'objets placés en parallèle** dans la base :

| Modèle | Type | Clé | État |
|---|---|---|---|
| **Vivant** | `WorldMapEditDocument::layoutInstances` (`WorldMapEditLayoutInstance`) | `guid` (string) + index | rendu, affiché Outliner/Inspector, sélectionné par `EditorSelection`, persisté JSON |
| **Orphelin** | `PlacementDocument::m_props` (`PropInstance`) | `uint32 instanceId` | non instancié dans le shell (audit 7.2) |

Les outils orphelins du Lot 0 (`SelectionTool` → liste de `uint32`, `DeleteCommand` →
`std::vector<PropInstance>&`) ont été codés contre le modèle **orphelin**.

**Décision (approche A)** : le Lot 0 opère sur le **modèle vivant** (`layoutInstances`).
Conséquences :
- On réutilise **la géométrie pure** de `SelectionTool` (`SelectInRect`/`SelectInLasso`) mais
  on l'alimente avec les `layoutInstances` et on remappe le résultat vers `EntityId`.
- On écrit une **nouvelle** commande de suppression sur le modèle vivant
  (`DeleteEntitiesCommand`). Le `DeleteCommand`/`PropInstance` orphelin reste **parqué**
  (réveillé au Lot 2 si `PlacementDocument` devient canonique).
- L'unification des deux modèles est un **ticket futur** explicite (hors périmètre).

## 3. Outillage existant réutilisé (ne pas réinventer)

- **Raycast viewport→sol** : `RaycastTerrainFromCamera(camera, vw, vh, mx, my, …) → (X,Z)`
  (`Engine.cpp:770`). Donne les coordonnées monde X/Z d'un clic.
- **Pattern de dispatch par `EntityId`** : `SetTransformWriter` (`Engine.cpp:8919`) — l'Engine
  installe un foncteur capturant les docs concrets et écrit par `EntityId`. Le Delete suivra
  **exactement** ce pattern (« delete dispatcher »).
- **Overlay de rendu** des `layoutInstances` (`Engine.cpp:9855`,
  `overlay.layoutInstancesOverlay`) — point d'accroche pour le gating de calque.
- **`SceneModel.Rebuild()`** appelé chaque frame (`Engine.cpp:8912`) : reflète immédiatement
  placements/suppressions.
- **Géométrie pure** : `SelectInRect` / `SelectInLasso` (`SelectionTool.{h,cpp}`).
- **`LayersDocument`** (`LayersDocument.h`) : 16 calques (nom/visibilité/verrou/couleur) +
  table d'assignement `entityKey(uint64) → layerIndex`. Logique pure déjà testée.
- **`EditorSelection`** (`scene/EditorSelection.h`) : mono-sélection actuelle (`m_current`).
- **`CommandStack`** : undo/redo (Ctrl+Z/Y déjà branchés).

## 4. Composants à livrer

### 4.1 `ActiveTool::Select` (outil mode)

- Ajouter `Select` à l'enum `ActiveTool` (`WorldEditorShell.h`).
- Ajouter à `EditorToolbar::m_orderedTools` + icône/lettre/tooltip FR dans `ToolbarIconAtlas`.
- C'est le **seul** des trois composants qui est un *mode* d'outil. Delete = action ;
  Layers = panneau.
- Aucun outil concret à instancier comme membre du shell pour la sélection : la logique de
  picking vit dans l'Engine (routage input) + `EditorSelection` (état). `SelectionTool` reste
  une bibliothèque de géométrie pure appelée par l'Engine.

### 4.2 Extension multi-sélection de `EditorSelection` (rétrocompatible)

- Conserver `m_current` comme **primaire** ; `Current()`, `HasSelection()`, `Select(id)`,
  `Clear()`, `SetOnChanged` **inchangés** (Outliner/Inspector continuent de marcher).
- Ajouter un **set** ordonné de `EntityId` sélectionnés et les API :
  - `void SelectMany(const std::vector<EntityId>& ids)` — remplace le set, primaire = premier.
  - `void ToggleInSelection(EntityId id)` — ajoute/retire ; ajuste le primaire.
  - `const std::vector<EntityId>& SelectedSet() const`
  - `bool IsSelected(EntityId id) const`
- `Select(id)` mono = `Clear` set + add + primaire (comportement actuel préservé).
- Le callback `OnChanged` est invoqué sur tout changement effectif (set inclus).
- **Inspector** : édite **le primaire** uniquement (pas de multi-édition de transform au Lot 0).
- **Outliner** : clic = `Select` mono (comportement actuel) ; **Ctrl+clic** = `ToggleInSelection`.

### 4.3 Picking viewport (routage dans `Engine.cpp`, branche `ActiveTool::Select`)

Brancher dans la chaîne `if (tool == …)` (près de `Engine.cpp:9907`).

- **Construire la liste des entités sélectionnables** (chaque frame d'interaction, ou à la
  demande) : pour chaque kind couvert (LayoutInstance / MeshInsert / DungeonPortal), produire
  des `SelectablePoint{ id = encodage(EntityId), x, z }` à partir de la position monde de
  l'entité. `id` encode `(kind, index)` sur 32 bits (ex. 8 bits kind + 24 bits index — borné,
  largement suffisant) pour transiter par l'API `uint32` de `SelectionTool`, décodé au retour.
- **Clic simple** : raycast → (X,Z) ; sélectionner l'entité **la plus proche en X/Z** sous un
  **rayon de pick** (paramètre monde, ex. dérivé d'un nombre de pixels via la distance caméra) ;
  `Select(primaire)`. Si rien sous le rayon → `Clear`.
- **Marquee drag** : capter début/fin de drag ; raycast des coins (rect) ou points (lasso) →
  rect/lasso **monde X/Z** ; `SelectInRect`/`SelectInLasso` sur les `SelectablePoint` ;
  `SelectMany`. Modificateur (Shift) = additif au set existant (optionnel ; sinon remplace).
- **Exclusion** : les entités d'un calque **caché ou verrouillé** ne sont **pas**
  sélectionnables (filtrées avant l'appel géométrie).
- **Limite assumée & documentée** : la sélection raisonne en projection top-down X/Z
  (cohérent avec le header de `SelectionTool`). En perspective rasante, le marquee est
  approximatif. Acceptable pour le MVP éditeur (caméra souvent top-down) ; un picking
  écran-espace exact est un raffinement ultérieur.

### 4.4 `DeleteEntitiesCommand` (nouvelle, réversible)

Fichier neuf (PascalCase) sous `src/world_editor/`, p. ex. `scene/DeleteEntitiesCommand.{h,cpp}`.

- **Pattern « delete dispatcher »** calqué sur `SetTransformWriter` : l'Engine installe sur le
  shell un foncteur `DeleteWriter`/`EntityDeleter` capturant les docs concrets. Deux primitives
  par kind : *retirer par index* (Execute) et *réinsérer (index, copie)* (Undo). Le shell
  expose `SetEntityDeleter(...)` (jumeau de `SetTransformWriter`).
- **Execute** : grouper la sélection **par kind** ; pour chaque doc, **trier les indices
  décroissants**, `erase`, et snapshot `(index, copie)`.
- **Undo** : réinsérer par index **croissant** par doc → ordre initial reconstruit exactement.
- **Déclenchement** : touche **Suppr** (l'Engine forwarde `VK_DELETE` au shell — aujourd'hui
  seuls Z/Y sont forwardés) + entrée de menu. Supprime **tout le set** sélectionné. No-op si
  set vide. Après Execute, vider la sélection.
- **Couverture kinds** : LayoutInstance (priorité — arbres/props), MeshInsert, DungeonPortal.
  Terrain/Water **non supprimables** (Terrain est implicite ; Water n'a pas de transform simple
  et est hors SceneModel).
- **Calque verrouillé** : les entités d'un calque verrouillé sont exclues de la suppression.
- **Limite assumée & documentée** : `EntityId.index` n'est pas stable après édition
  structurelle (audit 2.7). Le delete étant **immédiat** sur la sélection courante (même
  frame), et l'undo réinsérant aux index capturés, c'est sûr en pratique — **même compromis
  déjà assumé par `SetEntityTransformCommand`**. À documenter dans le `.h`.

### 4.5 Câblage des calques (`LayersDocument`)

- **Clé stable** : `entityKey = FNV1a(prefixe_kind + guid)`. Les 3 types portent un `guid`
  (string). La clé **survit aux `Rebuild`** (contrairement à l'index). Helper unique
  `uint64_t MakeEntityKey(EntityKind, std::string_view guid)` (réutiliser un FNV existant ;
  l'audit Thème 8 note FNV-1a réécrit 3× — **factoriser**, ne pas en ajouter un 4e).
  Résolution `EntityId → guid` via le doc concret (le shell/Engine sait le faire).
- **Nouveau panneau `LayersPanel`** (`panels/LayersPanel.{h,cpp}`, implémente `IPanel`,
  ajouté à `m_panels`) :
  - liste des 16 calques : renommer, toggle visibilité, toggle verrou, couleur d'overlay ;
  - bouton **« Assigner la sélection au calque N »** → `AssignEntity(entityKey, N)` pour chaque
    entité du set ;
  - indicateur du nombre d'entités par calque.
- **Gating rendu** : dans le remplissage de `overlay.layoutInstancesOverlay` (`Engine.cpp:9855`),
  **exclure** les entités dont `LayersDocument::IsEntityVisible(entityKey) == false`.
  *(MeshInsert/Dungeon : gating de rendu différé si leur overlay n'a pas de hook équivalent —
  à confirmer au plan ; la visibilité reste portée par le document pour cohérence.)*
- **Verrou** : consulté par le picking (4.3) et le Delete (4.4).
- **Outliner** : pastille couleur du calque + toggles visibilité/verrou par ligne (lecture/écrit
  `LayersDocument`).
- **Persistance** : assignement en mémoire uniquement (conforme au header `LayersDocument.h`).
  La persistance d'un `layerIndex` dans les formats binaires/JSON est **différée** (ticket
  ultérieur, bump de version dédié). À noter : à ce stade les calques ne survivent pas à un
  rechargement de carte — comportement assumé du Lot 0.

### 4.6 `ToolbarIconAtlas` & `ToolPropertiesPanel`

- `ToolbarIconAtlas::Get(ActiveTool::Select)` : lettre/couleur/tooltip FR (« Sélection »).
- `ToolPropertiesPanel` quand `ActiveTool::Select` : afficher le **rayon de pick**, le mode
  (rect/lasso), et le compte d'entités sélectionnées. Pas de propriété lourde au Lot 0.

## 5. Flux de données

```
Clic / drag viewport (ActiveTool::Select)
  └─ Engine: RaycastTerrainFromCamera → (X,Z) [+ marquee corners]
       └─ filtre calques cachés/verrouillés
            └─ SelectionTool::SelectInRect/Lasso  (géométrie pure)  [marquee]
            └─ nearest-by-XZ sous rayon                              [clic]
                 └─ EditorSelection (set + primaire)
                      ├─ Outliner / Inspector lisent (primaire = Inspector, set = surbrillance)
                      └─ LayersPanel: « assigner sélection au calque »

Suppr
  └─ DeleteEntitiesCommand (push CommandStack)
       └─ EntityDeleter (foncteur Engine) mute docs concrets (erase index décroissants)
            └─ SceneModel.Rebuild() (frame suivante) → UI à jour
       └─ Undo: réinsertion index croissants → état exact

Rendu
  └─ overlay.layoutInstancesOverlay filtré par LayersDocument::IsEntityVisible(entityKey)
```

## 6. Stratégie de tests (headless, sans ImGui — testables sur Linux/CI)

- **`EditorSelection` multi** : `SelectMany`/`ToggleInSelection`/primaire/`IsSelected`/`Clear` ;
  rétrocompat `Select` mono ; callback `OnChanged` sur changement effectif uniquement.
- **`DeleteEntitiesCommand`** : Execute retire les bons index ; Undo restitue **exactement**
  (ordre + contenu) ; multi-kind ; indices décroissants ; set vide = no-op ; exclusion calque
  verrouillé.
- **Mapping marquee** : encodage/décodage `EntityId ↔ uint32` ; rect/lasso monde → ensemble
  d'`EntityId` attendu (réutilise les tests existants de `SelectInRect/Lasso` + un test
  d'intégration de projection).
- **Picking clic** : nearest-by-XZ sous rayon ; aucun candidat → `Clear` ; exclusion calques
  cachés/verrouillés.
- **`LayersDocument` + clé stable** : `MakeEntityKey` déterministe et stable au `Rebuild` ;
  `IsEntityVisible`/`IsEntityLocked` ; gating logique (« entité d'un calque caché absente de la
  liste de rendu »).
- **Factorisation FNV** : test que le helper unique donne le même hash que les usages existants
  qu'il remplace (non-régression).

> Les parties ImGui (`LayersPanel::Render`, toolbar, ToolProperties) restent gardées `_WIN32`
> et non testées headless, conformément à la convention de l'éditeur.

## 7. Points de couture (récapitulatif d'intégration)

| # | Fichier | Changement |
|---|---|---|
| 1 | `core/WorldEditorShell.h` | `ActiveTool::Select` ; `SetEntityDeleter(...)` + membre `LayersDocument` + accesseurs ; accès `EditorSelection` (déjà présent) |
| 2 | `ui/EditorToolbar.cpp` | `Select` dans `m_orderedTools` |
| 3 | `ui/ToolbarIconAtlas.*` | icône/tooltip `Select` |
| 4 | `scene/EditorSelection.{h,cpp}` | set + API multi |
| 5 | `scene/DeleteEntitiesCommand.{h,cpp}` | **nouveau** (ICommand) |
| 6 | `panels/LayersPanel.{h,cpp}` | **nouveau** (IPanel) ; ajout à `m_panels` |
| 7 | `panels/OutlinerPanel.cpp` | Ctrl+clic toggle ; pastille couleur + toggles calque |
| 8 | `panels/ToolPropertiesPanel.cpp` | bloc `Select` (rayon, mode, compte) |
| 9 | un util commun | `MakeEntityKey` + factorisation FNV-1a (supprime 2 doublons) |
| 10 | `src/client/app/Engine.cpp` | routage picking (clic/marquee) ; forward `VK_DELETE` ; install `EntityDeleter` ; gating overlay par calque |
| 11 | tests | nouveaux fichiers de tests + entrées CMake |

> Convention CMake (cf. mémoire) : tout nouveau `.cpp` partagé côté serveur doit être ajouté à
> la liste `server_app` — **non applicable ici** (tout est éditeur/client, dans `engine_core`).
> Vérifier néanmoins que les nouveaux `.cpp` sont bien pris par le build de `engine_core`.

## 8. Hors périmètre (différé, assumé)

- `DeleteCommand`/`SelectionTool`-sur-`PropInstance` orphelins → **parqués** pour le Lot 2.
- **Unification** des deux modèles d'objets (layoutInstances ↔ props) → ticket futur dédié.
- **Persistance** d'un `layerIndex` dans les formats binaires/JSON → différée (bump version).
- **Multi-édition** de transform dans l'Inspector → un primaire seulement au Lot 0.
- **Picking écran-espace exact** en perspective → raffinement ultérieur (top-down X/Z au Lot 0).
- Gating de rendu MeshInsert/Dungeon si pas de hook overlay équivalent → à confirmer au plan.

## 9. Risques & mitigations

| Risque | Mitigation |
|---|---|
| Index `EntityId` instable entre Execute et Undo | Delete immédiat sur set courant ; undo aux index capturés ; même compromis que l'existant. Documenté. |
| 4e copie de FNV-1a | Factoriser en un helper unique + test de non-régression (réduit la dette Thème 8). |
| Marquee approximatif en perspective | Assumé/documenté ; éditeur souvent top-down ; écran-espace = raffinement futur. |
| Calques non persistés (perte au reload) | Comportement assumé du Lot 0 ; persistance = ticket version-bump. Mention UI possible. |
| Régression winding/culling rendu | Aucun pipeline rasterization touché — **ne pas** modifier `frontFace`/`cullMode` (cf. CLAUDE.md). |

## 10. Déploiement

> **Déploiement** : ✅ client/éditeur uniquement, **pas de redéploiement serveur**. Aucun
> opcode, format binaire exporté (LCDP/LCVC/LCMI), ni migration DB n'est touché.
