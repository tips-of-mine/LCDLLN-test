# Lot C — Plan de câblage des sous-systèmes orphelins (2026-06-13)

> Audit de câblage **en lecture seule** (aucun fichier modifié), 4 agents parallèles
> (éditeur-accessibilité, éditeur-outils, client, serveur+portail). **Aucune
> suppression** : l'objectif est de déterminer, pour chaque sous-système livré mais
> non câblé, **ce qu'il faut faire pour le brancher au runtime**. `legacy/`/`external/` exclus.
>
> Effort : **S** <½j · **M** 1-2j · **L** >2j. Déploiement noté par fiche.

---

## 0. Faits structurants découverts (à lire avant tout)

1. **Point d'ancrage UI éditeur = `src/world_editor/ui/WorldEditorImGui.cpp`** (menu **français**, propriétaire de `m_shell`). Le menu anglais `M100.1` de `core/WorldEditorShell.cpp:405-517` est **supprimé au boot** (`Engine.cpp:1383 SetMenuBarSuppressed(true)`). Tout nouveau menu/panneau éditeur va dans le menu français.
2. **`EnterDungeonHandler` master est DÉJÀ câblé et complet** (`main_linux.cpp:452,1129`) — il ne répond *pas* dans le vide ; seul l'**émetteur client** manque. Câblage **100% client** (si migration 0063 déjà appliquée).
3. **`SkillSystem` n'est PAS instancié dans Engine** (`grep SkillSystem src/client/app/` = 0). C'est le bloqueur racine de l'aperçu AoE.
4. **Deux pièces éditeur à fort effet de levier** : (a) un **pont SplatMap chunks→GPU** manquant (symétrique de la sync heightmap déjà en place) débloque à lui seul SplatPaint + Cave/Overhang/Arch/Portal + tagging splat Forest/Field ; (b) **un seul élargissement du switch viewport** (`Engine.cpp:9957-10018`) branche d'un coup SplatPaint, RiverNetwork, Coastline et plusieurs outils racine.
5. **`ClientPrediction` est server-first bloqué** : il dépend de toute la pile réplication UDP Linux + autorité shard + snapshots + envoi d'input UDP (TC.1), non mergée. À traiter en dernier, après les PR serveur.

---

## 1. Quick wins — faible effort, pas (ou peu) de serveur

| Sous-système | Ce qu'il manque | Effort | Déploiement |
|---|---|---|---|
| **AutoFitProxy** (client) | 1 seul site d'appel (`CollisionEditorPanel` éditeur, ou import de mesh runtime). Fonction pure testée. | **S** | ✅ client |
| **Consolidation AuctionUi** (client) | `auction/AuctionHousePresenter` (`m_auctionHouseUi`) est la seule câblée réseau (opcodes 174/176/178/180/181). Rerouter les interactions souris (drop-zone post, hit-test) depuis `economy/AuctionUiPresenter` (`m_auctionUi`, HUD debug) vers elle, puis débrancher `m_auctionUi` de la boucle (sans supprimer les fichiers). | **S-M** | ✅ client |
| **EnterDungeon émetteur** (client) | Émettre `kOpcodeEnterDungeonRequest(197)` sur interaction portail (patron `LfgUi.cpp:83-92`) + `case kOpcodeEnterDungeonResponse(198)` dans le switch réponses d'`Engine.cpp`. Payloads déjà dans `engine_core`, master déjà câblé. **S** pour l'émission ; **M** avec détection de volume portail in-world. | **S-M** | ✅ client (vérifier migration 0063 déjà jouée) |
| **exploitTier.ts** (portail) | Importer `bugReportThresholdsEligible` dans `admin/bugs/[id]/route.ts:90`. Option 1 (recommandée, sûre) : garde-fou de cohérence seed-SQL↔constante via `logError`. Option 2 : faire du helper l'autorité (filtre JS) — valider l'alignement des seuils d'abord. | **S** | ✅ portail |
| **Hiérarchie de rôles** (portail) | Généraliser `requireAdmin()` en `requireRole(min)` (importe `hasAtLeast`), reclasser ~14 routes admin (administrator/game_master/moderator) + gardes de pages. Colonne `accounts.role` (0043) déjà à 4 valeurs. Nécessite une **décision produit** sur le mapping niveau↔route + des comptes réellement seedés aux rôles intermédiaires. | **M** | ✅ portail |

---

## 2. Éditeur — outils (le gros chantier, mais très mutualisable)

### 2.1 Les 2 pièces maîtresses à faire en premier (débloquent le reste)

- 🔧 **Pont SplatMap chunks→GPU** (**M**) : ajouter un callback `m_onSplatChanged` jumeau de `m_onChunkChanged` dans `TerrainDocument` (aujourd'hui `MarkSplatDirty`, `TerrainDocument.cpp:321`, ne notifie rien), l'installer dans `Engine.cpp` (~:13824) pour lever un flag, et au tick (~:8171) un `SyncWorldEditorSplatFromDocument()` calqué sur `SyncWorldEditorHeightmapFromDocument` (`Engine.cpp:13925`) qui écrit dans `TerrainSplatting` + `ReuploadSplatMap`. → **débloque le rendu de SplatPaint, Cave/Overhang/Arch/Portal, et le tagging splat Forest/Field.**
- 🔧 **Élargir le switch viewport** (`Engine.cpp:9957-10018`) avec un patron unique `else if (tool == ActiveTool::X){ modernEditActive=true; if(freeClick && terrainPick && pressed) MutableXTool().OnMouseDown/AddVertex(...);}`. → branche **SplatPaint** (A), **RiverNetwork + Coastline** (B), et les outils racine qui choisissent le viewport.

### 2.2 SplatPaint — quasi prêt, 3 trous

Instancié, raccourci `P`, entrée ToolProperties OK. Manque : (1) branche dispatch viewport (S), (2) résoudre le « Config plumbing TODO » du bouton auto-rules (`ToolPropertiesPanel.cpp:243-253`, exposer `Config` au panneau ou méthode `shell.ApplySplatAutoRules`) (S), (3) le pont GPU §2.1 (M, sinon peinture invisible).

### 2.3 Outils déjà instanciés, manque juste la branche viewport / vérif bouton Apply

- **RiverNetwork, Coastline** : branche `AddVertex` dans le switch viewport. **S** chacun (mutualisé avec §2.1).
- **HydraulicErosion, ThermalWindErosion, Cave, Overhang, Arch, DungeonPortal** : pilotés par bouton « Apply » (pas de drag) — vérifier le câblage CommandStack ; Cave/Overhang/Arch/Portal deviennent **visibles** grâce au pont splat §2.1. **S** par outil.
- **Lake, River** (eau) : déjà câblés via canvas 2D top-down (`ToolPropertiesPanel.cpp:486-496`) — **rien à faire**, ils servent de modèle.

### 2.4 Outils racine non instanciés — câblage complet (5 maillons : membre shell + `enum ActiveTool` + raccourci + entrée ToolProperties + accesseur)

| Outil(s) | Input | Effort | Note |
|---|---|---|---|
| **ForestTool, WindZoneTool, ZoneTool, HamletGeneratorTool** | polygone XZ (réutiliser `RenderTopDownCanvas`→`AddPoint`, modèle Lake) | **M** chacun, mais répétitif (helper « enregistrer un outil ») | génération pure déjà testée |
| **FieldTool** | rectangle 2 coins | **M** | tagging splat → dépend §2.1 |
| **HazardTool** | clic sol unique (`CreateAt`, modèle TerrainStamp) | **M** (S si réutilise picking sol) | |
| **BridgeTool, WallTool** | spline (suite de points) | **M** chacun | header-only, génération dans `structures::` |
| **FoliagePaintTool** | pinceau densité | **L** | **seul outil sans API d'input** : écrire l'API stroke d'abord (patron SplatPaint) |
| **PlacementTool → InteractivePanel → SelectionTool → PlaytestMode** | placement/sélection (drag-rect, lasso, F5) | **L** (sous-système entier) | InteractivePanel dépend de PlacementTool ; PlaytestMode = touche F5 déjà réservée |

**Déploiement** : ✅ tous les outils éditeur = **100% client** (binaire éditeur), aucun serveur. Rappel : documenter `///` à la création (convention CLAUDE.md).

---

## 3. Éditeur — accessibilité / workflow (5 sous-systèmes, ordre imposé par les dépendances)

Aucune cible de widget n'existe aujourd'hui (`WidgetTargetRegistry` jamais rempli ; les ~13 cibles de `TutorialIo.cpp:45-71` sont fictives). Ordre :

1. **OverlayGuidanceSystem + WidgetTargetRegistry** (`help/`, **M**) — fondation, aucune dépendance. Travail : passe de rendu overlay (voile + rectangle pulsant + bulle) + instrumentation `Register(id, rect)` par frame sur les widgets cibles dans `WorldEditorImGui::Render`.
2. **ZoneValidator** (`validation/`, **M**) — indépendant ; livre une valeur immédiate (bouton/panneau « Valider ») **et** matérialise les cibles `toolbar.button.validate`/`panel.validation` dont le tutoriel a besoin. Travail : adaptateur Documents→`ValidationContext` + panneau + « Aller à » (caméra) + blocage export sur erreur.
3. **DiagnosticSystem** (`diagnostic/`, **M**) — indépendant (lien optionnel vers le `ValidationContext` de #2). 10 règles MVP prêtes. Travail : peupler le `DiagnosticContext` (dont **compteurs d'usage neufs** : `secondsSinceToolSelected`, `erosionAppliedAfterRivers`…) + panneau « Pourquoi ça ne marche pas ? ».
4. **QuickStartWizard** (`wizard/`, **M**, **L avec rollback**) — réutilise le pipeline `ZonePresetExecutor` déjà câblé par `ZonePresetDialog`. ⚠️ **Bloqueur P0** : `ZonePresetExecutor::Execute` fait un **reset destructif sans rollback** (audit 06-05 item 6.1) ; le wizard est un 2e déclencheur de ce flux → **traiter le rollback avant/avec**. De plus 4 opérations (`coastline`, `river_network`, `hydraulic_erosion`, `thermal_wind_erosion`) sont **ignorées silencieusement** par l'executor (`ZonePresetExecutor.h:55-59`).
5. **TutorialSystem** (`tutorial/`, **L**) — en dernier : dépend de #1, et ses cibles renvoient à #2 (validate) et #4 (zone_preset_dialog). Travail : intro/outro modales, ~13 points `NotifyAction`, persistance flag `first_launch_tutorial_completed` (UserPrefs, neuf), menu « Aide » à créer.

**Déploiement** : ✅ 100% client (éditeur).

---

## 4. Client — gameplay (couplé au serveur, server-first)

| Sous-système | Bloqueur principal | Effort | Déploiement |
|---|---|---|---|
| **ZonePreloadHook** (streaming+TAA) | opcode serveur `ZoneChange` à confirmer/produire (sinon wire-breaking). Volet streaming+TAA faisable seul ; flags entités → réplication d'entités (non mergée). | **M** | ⚠️ serveur si l'opcode est neuf |
| **Chaîne particules** (FXManager + ParticleSystem + ParticleBillboardPass) | étage **GPU entièrement à écrire** (pipeline transparent + shaders billboard + atlas + intégration FrameGraph, modèle `DecalSystem`) + colle FX→émetteurs + assets JSON `game/data/fx/*` à confirmer. CPU prêt. `CombatEvent` probablement déjà reçu (combat livré). | **L** | ✅ client si `CombatEvent` déjà reçu |
| **AoEPreviewSystem** | **`SkillSystem` à instancier dans Engine d'abord** (bloqueur racine) + déclencheur UI d'armement de sort + validation serveur de l'usage à une position. `DecalSystem` déjà dispo. | **M-L** | ⚠️ serveur si validation sort neuve |
| **ClientPrediction** | **server-first** : pile réplication UDP Linux + autorité shard + flux de snapshots + envoi input UDP (TC.1) non livrés. `SurfaceQueryService` (multiplicateur) déjà dispo. `ForcePosition` SP1 ≠ snapshots continus. | **L** | ⚠️ serveur (lock-step) |

---

## 5. Serveur — globals data-driven

**ConditionMgr / GraveyardManager / LocaleStrings** (`src/shardd/internals/globals/`) — testés, mais compilés **uniquement** dans les cibles de test. Câblage :
1. Ajouter les 3 `.cpp` à la liste de sources **`shard_app`** (bloc UNIX, `src/CMakeLists.txt:1097-1173`) — shard-only (namespace shard), pas `server_app`. Aucune nouvelle dépendance de link (ConnectionPool/DbHelpers/MySQL déjà là).
2. Instancier au boot dans `shardd/main_linux.cpp` sur le patron « Wave » (ex. EventAI :227), `Load(characterDbPool)` sous garde DB configurée, logger les compteurs.
3. Brancher fonctionnellement : `ClosestGraveyard` au flux mort/respawn (inexistant aujourd'hui), `Evaluate` aux conditions data-driven, `Format` aux messages serveur.

**Bloqueurs** : migration 0042 (idempotente, à rejouer au boot) ; **seeds = données de test** → seeder les **vraies** données prod (graveyards des maps réelles, locale_strings) sinon `ClosestGraveyard`=nullopt / `GetString`=marqueur debug. Convention d'ID : condition ∈[1,9999], group ∈[10000,∞).

**Effort** : **M**. **Déploiement** : ⚠️ **REDÉPLOIEMENT SHARD requis** (nouveaux .cpp + boot + migration + seed). Master non concerné, pas de wire-breaking.

---

## 6. Ordre de bataille recommandé (toutes parties confondues)

**Vague 1 — quick wins, valeur immédiate, risque quasi nul (tous S/M, client/portail)**
AutoFitProxy · consolidation AuctionUi · émetteur EnterDungeon · exploitTier (option garde-fou) · hiérarchie de rôles portail.

**Vague 2 — fondations éditeur à fort levier**
Pont SplatMap chunks→GPU + élargissement switch viewport → puis SplatPaint, RiverNetwork, Coastline, et la visibilité de Cave/Overhang/Arch/Portal.

**Vague 3 — outils racine éditeur** (helper « enregistrer un outil » mutualisé)
Forest/WindZone/Zone/Hamlet (polygone) → Hazard/Field/Bridge/Wall → placement/sélection (Placement→Interactive→Selection→Playtest) → FoliagePaint (API stroke d'abord).

**Vague 4 — accessibilité éditeur** (ordre imposé)
Overlay+Registry → ZoneValidator → Diagnostic → (P0 rollback executor) Wizard → Tutorial.

**Vague 5 — serveur globals** (shard) : ConditionMgr/Graveyard/LocaleStrings + seed prod → redéploiement shard.

**Vague 6 — gameplay couplé serveur (server-first)**
ZonePreloadHook (si opcode ZoneChange) · chaîne particules (étage GPU) · SkillSystem+AoEPreview · ClientPrediction (en dernier, après la pile réplication UDP).

---

*Source : 4 plans de câblage parallèles, 2026-06-13. Aucune modification de code, aucune suppression.
Complète `2026-06-13-audit-global-independant.md` (axe orphelins).*

> **Déploiement (de ce document)** : ✅ lecture seule, aucun redéploiement. Pour l'exécution :
> Vagues 1-4 = **100% client/portail** ; Vague 5 = **redéploiement shard** ; Vague 6 =
> **lock-step client+serveur** dès qu'un opcode/flux UDP neuf est requis.
