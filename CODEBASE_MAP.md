# CODEBASE MAP — Lune Noire (LCDLLN-test)

> Référence rapide à inclure dans un prompt pour éviter la ré-analyse complète.
> Dernière mise à jour : 2026-05-14 — **M100.46 Zone Presets Library — incrément 1 (socle data)** sur la branche `claude/m100-46-zone-presets` (M100.45 mergé via PR #611). Catalogue de templates de zones jouables prêtes à l'emploi : chaque preset JSON décrit une séquence ordonnée d'opérations éditeur (outils M100.35-43) à exécuter sur une zone vide. **Cet incrément livre le socle data uniquement** ; le moteur d'exécution (`ZonePresetExecutor` / `OperationDispatcher` / `CustomizationApplier`) et l'UI (dialog + modale de progression) sont des incréments suivants. (a) **`zone_presets/ZonePreset.h`** : struct `ZonePreset` (version, id, `LocalizedString` displayName/description FR/EN, thumbnail, tags, estimatedExecutionSeconds, `operations[]`, `decorationEntryCount`). `ZonePresetOperation` = `type` + `toolPresetId` + `affectedBy[]` parsés en structuré + `rawJson` (objet opération brut, conservé pour le futur `OperationDispatcher` typé — évite d'introduire un DOM JSON récursif générique dans cet incrément). Helpers `IsKnownOperationType` (14 types M100.35-43) / `IsKnownAffectedByTag` (relief / water_density / dryness). (b) **`ZonePresetIo`** : parseur JSON hand-rolled (pattern du repo). Parse le top-level + chaque opération (type/preset/affectedBy + rawJson) + compte les entrées `decoration` (réservée Phase 13). `MatchSpanEnd` gère l'imbrication objet/tableau. (c) **`ZonePreset::Validate`** : checks structurels — id non vide, version > 0, ≥ 1 opération, chaque `type` connu, chaque `affectedBy` connu, **`decoration` vide** (réservée Phase 13 — un preset MVP avec décoration est rejeté). (d) **`ZonePresetRegistry`** : singleton, charge `editor/zone_presets/*.json`, tolérant (parse fail ou Validate fail → log warning + ignoré), tri stable par id. Câblé au boot de `WorldEditorShell::Init`. (e) **8 presets livrés** dans `game/data/editor/zone_presets/` : temperate_forest, rocky_coast, desert, snowy_plateau, marshland, elven_valley, dead_lands, volcanic_island — chacun 4-8 opérations référençant les tool presets M100.45 réels, `decoration: []`. Les 8 validés localement (parse + Validate OK). Thumbnails 256×144 = ticket d'art séparé (README dans `thumbnails/`). (f) Tests CTest `zone_presets_tests` (14 tests : parse valide, rejets id/operations manquants, rawJson préservé, comptage decoration, Validate accepte/rejette type/affectedBy inconnus/operations vide/decoration non vide, helpers de type, LocalizedString fallback, registry chargement + résilience JSON corrompu + dossier absent). Compilés + exécutés localement : 14/14 OK. (g) **MVP increment-1 strict** : pas de moteur d'exécution, pas d'UI, pas de `CustomizationApplier`, pas de `WorldMapEditDocumentReset`. Les opérations sont parsées mais pas exécutées. (h) `engine_core` étendu de 3 sources C++ (nouveau dossier `src/world_editor/zone_presets/`). Aucun nouvel `.exe`. **Déploiement : ✅ client/éditeur uniquement, aucun changement serveur, aucun format binaire.** **Précédent** : 2026-05-14 — **M100.45 Simple/Advanced Mode + Tool Parameter Presets (démarre Phase 12 « Accessibilité éditeur ») — COMPLET** sur la branche `claude/m100-phase12-accessibilite` (PR #611 ; Phase 2.5 + Phase 11 mergées via PR #606, merge commit `543024b`). M100.45 livré intégralement en 13 commits : **Phase A** (infrastructure transverse) + **A.5** (toggle menu Options) + **Phase B** (migration des 15 outils en 8 incréments). 2 suites de tests CTest : `editor_modes_tests` (14) + `tool_migration_tests` (24). **A.5** : sous-menu `Options > Mode editeur` (Simple/Avancé) câblé sur `EditorModeRegistry`. **Phase B** : les 15 outils de l'éditeur migrés Simple/Advanced ; 9 reçoivent en plus un dropdown de presets via `PresetDropdownWidget` (A.6) + `ToolPresetApply` (mapping pur preset→struct, testable) + 11 fichiers `game/data/editor/tool_presets/*.json` — hydraulic, thermal_wind, sculpt, splat, river_network, mountain_macro, valley_macro, river_manual, stamp ; cave/overhang/arch/dungeon font le split Simple/Advanced seul (leur catalogue joue déjà le rôle des presets), lake/coastline idem (params marginaux). Hot-reload `Ctrl+Shift+R` des presets en build debug. Reste hors PR #611 : détection « Custom (modifié) » dans le dropdown, `FirstLaunchToast`. Détail de la **Phase A** (infrastructure) ci-dessous. (a) **`modes/EditorMode.h` + `EditorModeRegistry`** : enum `Simple`/`Advanced` + singleton centralisant le mode courant, `SetCurrentMode` persiste via `UserPrefsStore` puis notifie les abonnés (callbacks synchrones — un `ToolPropertiesPanel` peut se re-render au basculement), `SetCurrentModeSilent` pour aligner le registry au boot sans ré-écrire le fichier. (b) **`presets/ToolPreset.h` + `ToolPresetIo` + `ToolPresetRegistry`** : `ToolPreset` = id + displayName + description + `parameters` (sac clé→`double`, pas de dépendance JSON générique — tous les params d'outils en preset sont numériques). `ToolPresetIo::ParseToolPresetJson` = parseur hand-rolled tolérant (pattern WorldMapIo). `ToolPresetRegistry` (singleton) charge `<contentRoot>/editor/tool_presets/*.json`, indexe par `toolId` ; fichier corrompu → log warning + ignoré (les autres chargent), `Reload()` pour le hot-reload dev. (c) **`prefs/UserPrefs.h` + `UserPrefsStore`** : struct `UserPrefs` versionnée (`editorMode`, `lastPresetByTool`, `showAdvancedTooltips`, `tutorialCompletionFlags` — ces 2 derniers consommés plus tard par M100.47/M100.49). `UserPrefsStore` (singleton) persiste dans `<contentRoot>/editor/user_prefs.json` ; **sauvegarde atomique** (`.tmp` + `rename`) ; premier lancement (fichier absent) → crée les défauts ; lecture **tolérante** (champ manquant → défaut, JSON corrompu → pas de crash). (d) **`ui/IToolPropertiesPanelContent.h`** : interface que les 13 outils implémenteront en Phase B (`GetToolId`, `IsModeAwareInPropertiesPanel`, `RenderPropertiesPanel(mode)`, `GetAvailablePresets`, `ApplyPreset`). Définie seulement — aucun outil ne l'implémente encore (backward-compat pendant la migration : un outil non migré retourne `IsModeAwareInPropertiesPanel() == false`). (e) **Boot wiring** dans `WorldEditorShell::Init` : charge `UserPrefsStore` + `ToolPresetRegistry` depuis `paths.content`, aligne `EditorModeRegistry` sur la pref chargée. (f) **`game/data/editor/`** : `.gitignore` local + entrée racine `.gitignore` pour `user_prefs.json` (jamais committé). 4 fichiers `tool_presets/*.json` canoniques livrés (`hydraulic_erosion`, `thermal_wind_erosion`, `mountain_macro`, `valley_macro`) — les autres viennent avec la Phase B. (g) Tests CTest `editor_modes_tests` (14 tests, exécutés localement 14/14 OK : EditorMode set+notify+silent+string round-trip, ToolPresetIo parsing valide / rejet toolId manquant / tolérance presets absent / skip preset sans id, ToolPresetRegistry chargement répertoire / résilience JSON corrompu / dossier absent, UserPrefs premier lancement / round-trip disque / atomic save sans .tmp résiduel / tolérance fichier corrompu). (h) **Hors scope Phase A** (→ Phase B / tickets suivants) : migration des 13 outils, widget `PresetDropdownWidget`, toggle menu Options, `FirstLaunchToast`, hot-reload `Ctrl+Shift+R`. (i) **Invariant terrain visible** inchangé (le toggle de mode ne touche rien au rendu). (j) `engine_core` étendu de 4 sources C++ (nouveaux dossiers `src/world_editor/modes/`, `presets/`, `prefs/`). Aucun nouvel `.exe` hors `editor_modes_tests`. **Déploiement : ✅ client/éditeur uniquement, aucun changement serveur, aucun format binaire.** Premier lancement post-déploiement crée `user_prefs.json` avec les défauts (mode Simple). **Précédent** : 2026-05-13 — **M100.44 VMap Bridge & Phase 11 Validation (CLÔTURE Phase 11 « Volumes 3D »)** stacké sur M100.35-43, mergé via PR #606. **Ticket lock-step client+serveur** : câble le handler master des opcodes 197/198 réservés par M100.43. (a) **`VMapBridge`** (`src/world_editor/volumes/bridge/`) : pont éditeur → collision serveur. Lit `MeshInsertDocument` (LCMI M100.40-42) + `DungeonPortalDocument` (LCDP M100.43), résout les AABB locales via les catalogues (cave/overhang/arch, matchées par `gltfRelativePath`), et produit une liste de `VolumeAabbProxy` world-space. `TransformLocalAabbToWorld` englobe les 8 coins transformés (translation + rotation Y + scale uniforme) → AABB conservatrice jamais sous-estimée. Les portails de donjon (sans mesh) donnent un cube de côté `2 * triggerRadius`. Volumes au catalogue absent → proxy dégénéré cube ±0.5 m + `outUnresolvedCount` incrémenté. (b) **Format binaire `LCVC` v1** (Lcdlln Volume Collision) dans `instances/volume_collision.bin` : header 16 octets + N × `[sourceGuid u64][volumeKind u8][worldMin 3×f32][worldMax 3×f32]`. `volumeKind` ∈ {Cave=0, Overhang=1, Arch=2, DungeonPortal=3, Unknown=255}. `Deserialize` statique pour le futur loader shard. (c) **`Phase11Validator`** : passe de cohérence pure (pas d'I/O) sur tous les fichiers de volumes. Détecte (sévérités Info/Warning/Error) : guids dupliqués, guid 0 (sentinelle), `gltfRelativePath` vide, `insertCategory` inconnue, asset fantôme (gltf absent des catalogues), `dungeonTemplateId` orphelin (le master rejettera EnterDungeon), difficulty range incohérent ou hors catalogue, `triggerRadius` ≤ 0, positions NaN/infinies, `uniformScale` ≤ 0. `ValidationReport::HasBlockingErrors()` garde l'export VMap. (d) **`EnterDungeonHandler`** (`src/masterd/handlers/dungeon/`) : **handler master câblé** pour `kOpcodeEnterDungeonRequest` (197). Suit le pattern `CharacterEnterWorldHandler` : valide session (connId→sessionId→accountId), valide ownership du personnage (SELECT characters gated par account_id), `INSERT INTO dungeon_instances` (migration 0063, dungeonTemplateId échappé via `mysql_real_escape_string`), renvoie `EnterDungeonResponsePayload` (opcode 198) avec le `instanceId` via `mysql_insert_id`. Câblé dans `main_linux.cpp` (instanciation + dispatch `else if (opcode == kOpcodeEnterDungeonRequest)`). `shardEndpoint` reste vide (résolution multi-instance = follow-up post-Phase 11). Pas de cap d'instances ni de gating progression en MVP. (e) **`BuildEnterDungeonResponsePacket`** ajouté à `DungeonPayloads` : wrappe `BuildEnterDungeonResponsePayload` dans un `PacketBuilder::Finalize` (opcode 198). (f) **Intégration UI éditeur** : le bloc `RenderDungeonPortalParams` du `ToolPropertiesPanel` gagne une section « VMap Bridge & validation » — exécute `Phase11Validator::Validate` à chaque frame, affiche le rapport couleur-codé (rouge/ambre/bleu), bouton « Exporter collision VMap » gardé derrière `!HasBlockingErrors()` qui appelle `VMapBridge::Build` + `WriteToDisk`. (g) Tests CTest : `phase11_bridge_tests` (12 tests : transform translation/scale/rotation Y 90°, Build cave+portail, proxy dégénéré non résolu, LCVC round-trip, LCVC bad magic, Validator docs propres / gltf vide / template orphelin / params portail invalides / guid dupliqué). `dungeon_payloads_tests` étendu (+1 test : `BuildEnterDungeonResponsePacket` non vide). (h) **Phase 11 « Volumes 3D » COMPLÈTE** : M100.40 (Mesh Insert Foundation + Cave) → M100.41 (Overhang) → M100.42 (Arch) → M100.43 (Dungeon Portal + opcodes/migration) → M100.44 (VMap Bridge + handler câblé). Reste pour la suite : Phase 12 (accessibilité : Simple/Advanced mode, presets, tutoriel, deployment pipeline) ; follow-ups Phase 11 : rendu glTF runtime (tinygltf), gizmo M100.17, raycast viewport, extraction mesh fine (triangle soup) en remplacement des proxies AABB, résolution shard multi-instance pour les donjons. (i) **Invariant terrain visible** strict respecté. (j) `engine_core` étendu de 2 sources C++ éditeur (`volumes/bridge/`) ; `master_app` étendu de 1 handler + `DungeonPayloads.cpp`. Aucun nouvel `.exe`. **Déploiement : ⚠️ REDÉPLOIEMENT SERVEUR (master) REQUIS** — le handler `EnterDungeonHandler` est désormais câblé et lit `dungeon_instances` (migration 0063 de M100.43). Master neuf + migration 0063 appliquée doivent être déployés ensemble. Pas de bump `kProtocolVersion` (UDP gameplay inchangé). Côté client/éditeur : nouveau fichier `instances/volume_collision.bin` produit par l'export VMap — le serveur ne le consomme pas encore (loader shard = follow-up), donc pas de lock-step sur ce fichier précis. **Précédent** : 2026-05-13 — **M100.43 Dungeon Portal System (Phase 11)** stacké sur M100.35-42, même branche `claude/review-m100-tickets-z3aUB`. **Premier ticket Phase 11 qui touche au serveur** (migration DB + opcodes réservés). (a) **`DungeonPortalInstance`** : struct distincte de `MeshInsertInstance` (M100.40) car porte des metadata gameplay : `dungeonTemplateId` (string ≤ 64 octets), `triggerRadius`, `requiredLevel`, `minDifficulty` / `maxDifficulty`, `isOneShot`, `persistsAcrossLogin`, `decorativeMeshPath` (optionnel, cosmétique). (b) **Format binaire `LCDP` v1** (Lcdlln Dungeon Portal) dans `instances/dungeon_portals.bin` — distinct de LCMI (M100.40). Header 16 octets `[magic(4)][version(4)][instanceCount(4)][reserved(4)]`, instances séquentielles avec strings length-prefixed u16 et flags packés bit0=isOneShot, bit1=persistsAcrossLogin. (c) **`DungeonPortalDocument`** : CRUD + Save/Load identique au pattern MeshInsertDocument, events `OnAdded`/`OnUpdated`/`OnRemoved`, générateur Guid monotone, init au max+1 des chargés. (d) **`DungeonCatalog`** : loader JSON hand-rolled lisant `game/data/meshes/dungeons/catalog.json` : id, displayName, description (multi-ligne), decorativeMesh, requiredLevel, minDifficulty / maxDifficulty. Tolérant absent / vide. (e) **`PlaceDungeonPortalCommand`** (ICommand) : insertion pure (pas de patch terrain) + Undo strict par guid. (f) **`DungeonPortalTool`** : sélection par templateId (préremplit niveau + difficulty range depuis catalog), position cible, slider yaw, triggerRadius, requiredLevel, difficulty (gating cohérence avec range catalog), flags isOneShot / persistsAcrossLogin. Place rejette si template inconnu, difficulty incohérente, ou hors range catalog. (g) **Opcodes serveur réservés 197/198** : `kOpcodeEnterDungeonRequest` (197) et `kOpcodeEnterDungeonResponse` (198) dans `ProtocolV1Constants.h`. Payloads `engine::network::EnterDungeonRequestPayload` (characterId u64, dungeonTemplateId string ≤ 64, difficulty u8 1..5) et `EnterDungeonResponsePayload` (success u8, instanceId u64, shardEndpoint string, errorCode u8). 6 codes d'erreur définis : `kEnterDungeonErrorNone`, `TemplateNotFound`, `InstanceFull`, `DifficultyLocked`, `Unauthorized`, `NotYetImplemented`. (h) **Migration DB `0063_dungeon_instances.sql`** : table `dungeon_instances (id, dungeon_template_id VARCHAR(64), owner_character_id, difficulty, shard_endpoint, created_at, expires_at)` avec indices sur owner / template / expires. Idempotente `CREATE TABLE IF NOT EXISTS`. (i) **Intégration UI** : `ActiveTool::DungeonPortal = 15`, raccourci `Ctrl+Shift+D`, toolbar lettre 'D' (15 outils + bouton X), bloc dédié `RenderDungeonPortalParams` dans `ToolPropertiesPanel` (catalog radio avec description multi-ligne, position cible, sliders, flags, compteur). (j) Tests CTest : `dungeon_portal_tests` (7 tests : LCDP round-trip avec tous les champs, Document Add/Remove/Update, Catalog parse JSON, Command Apply/Undo, Tool::Place rejette sans selection, Tool::Place rejette difficulty range incohérent, LCDP bad magic). `dungeon_payloads_tests` (6 tests : Request round-trip, Response success, Response error, rejet difficulty=0, rejet difficulty > kMaxDungeonDifficulty, rejet truncated). (k) **Statut M100.43 strict éditeur-side + protocole réservé** : opcodes 197/198 **réservés mais pas câblés** à un handler — un client qui les enverrait recevrait BAD_REQUEST côté master. Le wiring serveur (`EnterDungeonHandler`, instance lifecycle, shard endpoint resolution, VMap bridge overworld ↔ dungeon-instance) est explicitement reporté à M100.44. (l) Catalogue placeholder `dungeons/catalog.json` 3 templates (starter_keep niveau 1, crypt_of_echoes niveau 15-25 Normal+Héroïque, abyssal_caverns niveau 40+ 3 modes). README documente l'architecture complète + statut de chaque composant. (m) **Invariant terrain visible** strict respecté. (n) `engine_core` étendu de 5 sources C++ éditeur + 1 source shared/network. Aucun nouvel `.exe` (tests via 2 binaires WIN32). **Déploiement : ⚠️ REDÉPLOIEMENT SERVEUR REQUIS** — migration 0063 idempotente mais doit être rejouée au boot pour que `dungeon_instances` existe. Pas de bump `kProtocolVersion` (UDP gameplay inchangé, c'est du TCP master). **Précédent** : 2026-05-13 — **M100.42 Natural Arch Tool (Phase 11)** stacké sur M100.35-41, même branche `claude/review-m100-tickets-z3aUB`. (a) **Réutilise `MeshInsertSystem` de M100.40** sans modification du format binaire LCMI v1 — les instances `insertCategory = "arch"` partagent `instances/mesh_inserts.bin` avec les grottes (M100.40) et les overhangs (M100.41). Troisième catégorie d'inserts, pas de nouvelle migration. (b) **`ArchCatalog`** : loader JSON hand-rolled lisant `game/data/meshes/arches/catalog.json`. Champs spécifiques : `archAnchorA` / `archAnchorB` (les deux pieds pivot-relatifs au sol modélisé), `archHeight` (distance corde→clé d'arc, indicatif). Helper `NativeSpanMeters()` = `||archAnchorB - archAnchorA||_XZ`. (c) **`PlaceArchCommand`** (ICommand) : insertion pure d'une `MeshInsertInstance` (`insertCategory = "arch"`, `hasInteriorVolume = false`, `allowsWaterIngress = false`, `receivesAudioReverb = false`). Pas de patch splat (une arche est posée au-dessus du sol). Undo strict : retire par guid. (d) **`ArchTool`** : workflow basé sur **deux points monde** (pieds A et B) au lieu d'un point unique comme Cave/Overhang. Le tool calcule automatiquement : `worldPosition = midpoint(A, B)`, `eulerRotationDeg.y = atan2(B.z - A.z, B.x - A.x) * rad→deg`, `uniformScale = span_monde / span_natif`. Garde-fou : `Place` refuse si scale dérivé est hors `[minScaleRatio, maxScaleRatio]` (par défaut `[0.25, 4.0]`) pour éviter écrasement / étirement implausible. Helpers `SpanMeters()`, `DerivedYawDeg()`, `DerivedScale()` exposés en lecture pour preview UI temps réel. (e) **Intégration** : `ActiveTool::Arch = 14`, raccourci `Ctrl+Shift+A`, entrée toolbar lettre 'A', bloc dédié `RenderArchParams` dans `ToolPropertiesPanel` (saisie A.xyz / B.xyz + valeurs dérivées en lecture seule + sliders bornes scale + Place gating). Compteur "Arches posées" lit `MeshInsertDocument::GetByCategory("arch").size()`. (f) Tests CTest `arch_tool_tests` (5 tests : catalog parse champs spécifiques + `NativeSpanMeters` correct, catalog vide toléré, Command Apply/Undo + scale préservé, Tool dérivation span/yaw (3-4-5 triangle → 5m / 53.13°), Tool::Place rejette sans selection). (g) Catalogue placeholder `arches/catalog.json` 3 entrées (small/medium/large, spans 5m/10m/20m). README documente la convention (`archAnchorA/B` pivot-relatifs, Y au sol modélisé). (h) **MVP éditeur-side strict** : pas de raycast viewport pour cliquer les pieds (besoin du gizmo M100.17). Pas de validation que le terrain descend bien sous chaque pied (besoin de SurfaceQuery). En MVP, saisie manuelle des deux points. (i) **Invariant terrain visible** strict respecté. (j) `engine_core` étendu de 3 sources C++ (nouveau dossier `src/world_editor/volumes/arches/`). Aucun nouvel `.exe`. Aucun opcode, aucune migration DB. **Précédent** : 2026-05-13 — **M100.41 Overhang Cliff Tool (Phase 11)** stacké sur M100.35-40, même branche `claude/review-m100-tickets-z3aUB`. (a) **Réutilise `MeshInsertSystem` de M100.40** sans aucune modification du format binaire LCMI v1 — les instances `insertCategory = "overhang"` partagent `instances/mesh_inserts.bin` avec les grottes. Pas de nouveau document, pas de migration. (b) **`OverhangCatalog`** : loader JSON hand-rolled lisant `game/data/meshes/overhangs/catalog.json`. Schéma parallèle à `CaveCatalog` mais avec champs spécifiques : `wallAnchorPoint` (pivot-relatif, contact falaise), `wallNormalDirection` (vecteur sortant vers le vide — le tool ajuste yaw pour aligner avec la normale terrain), `coverageRadius` (rayon ombre projetée pour SurfaceQuery/lighting). Plus de `entrancePoint` ni `interiorAabbMin/Max` (un overhang n'a pas de volume intérieur jouable). (c) **`PlaceOverhangCommand`** (ICommand) : insertion pure d'une `MeshInsertInstance` (pas de patch splat — l'overhang est adossé à une falaise rocheuse qui porte déjà la couche rock). Undo strict : retire l'instance par guid. (d) **`OverhangTool`** : sélection catalogue, position cible X/Y/Z, sliders yaw normal mur / tilt latéral / scale, validation cliff manuelle (slider `RequiredSlopeDeg` + `ObservedSlopeDeg`, gating `Place` si `observed < required`), flags casts shadow / audio reverb / probe intensity. `hasInteriorVolume = false` + `allowsWaterIngress = false` figés (sémantique overhang). (e) **Intégration** : `ActiveTool::Overhang = 13`, raccourci `Ctrl+Shift+O`, entrée toolbar lettre 'O', bloc dédié `RenderOverhangParams` dans `ToolPropertiesPanel`. Compteur "Overhangs posés" lit `MeshInsertDocument::GetByCategory("overhang").size()`. (f) Tests CTest `overhang_tool_tests` (5 tests : catalog parse champs spécifiques, catalog vide toléré, Command Apply/Undo + `hasInteriorVolume=false` figé, Tool::Place rejette sans selection, Tool::Place rejette si slope insuffisante). (g) Catalogue placeholder `overhangs/catalog.json` 3 entrées (small/medium/large). README documente la convention (`wallAnchorPoint` + `wallNormalDirection` à modéliser pivot-relatifs dans l'asset glTF). (h) **MVP éditeur-side strict** : raycast normal terrain automatique non câblé (besoin du gizmo M100.17 + `TerrainSlopeProbe`). En MVP, l'utilisateur saisit position manuelle + valide slope locale via slider. Le tool est néanmoins fonctionnel end-to-end : insère une `MeshInsertInstance` dans `MeshInsertDocument` sans modifier le splat. (i) **Invariant terrain visible** strict respecté. (j) `engine_core` étendu de 3 sources C++ (nouveau dossier `src/world_editor/volumes/overhangs/`). Aucun nouvel `.exe`. Aucun opcode, aucune migration DB. **Précédent** : 2026-05-13 — **M100.40 Mesh Insert Foundation + Cave Tool (démarre Phase 11 « Volumes 3D »)** stacké sur M100.35-39, même branche `claude/review-m100-tickets-z3aUB`. (a) **`MeshInsertInstance`** + **format binaire LCMI v1** dans `instances/mesh_inserts.bin` : guid (uint64), gltfRelativePath, worldPosition, eulerRotationDeg, uniformScale, insertCategory ("cave"/"overhang"/"arch"/"dungeon"), displayName, flags packés (hasInteriorVolume + castsShadow + receivesAudioReverb + allowsWaterIngress), lightProbeIntensity. Header 16 octets `[magic LCMI(4)][version(4)][instanceCount(4)][reserved(4)]`, instances séquentielles avec strings length-prefixed u16. (b) **`MeshInsertDocument`** : CRUD + events (`OnAdded`/`OnUpdated`/`OnRemoved`), générateur Guid monotone (jamais 0), Save/LoadFromDisk avec init du compteur au max+1 des instances chargées. (c) **`CaveCatalog`** : loader JSON hand-rolled (pattern `WorldMapIo`) lisant `game/data/meshes/caves/catalog.json` (id, gltf, displayName, thumbnail, aabbMin/Max pivot-relatifs, entrancePoint, interiorAabbMin/Max). Tolérant absent / vide. (d) **`CaveCamouflage`** : helper pur `ComputeCaveSplatWeights` qui calcule des poids smoothstep ∈ [0, 1] pour chaque cellule dans le rayon autour de la grotte, indexés par `GlobalChunkCoord` + cellIndex linéaire (réutilise `SparseChunkDeltas` M100.35). (e) **`PlaceCaveCommand`** : Apply insère le mesh insert + applique le patch splat avec maintien strict de l'invariant somme=255 par cellule (incrémente la layer cible "rock", ré-équilibre les autres proportionnellement). Snapshot per-cell (chunkCoord/cellIndex/layer/prevWeight/prevOtherSum) pour Undo bit-à-bit. (f) **`CaveTool`** : sélection par id dans le catalogue, position cible (X/Y/Z éditables — raycast viewport viendra avec le gizmo M100.17), sliders rotation/scale/camouflage/probe, flags volume intérieur / reverb / water ingress. Snap au sol : `worldPosition.y -= entrancePoint.y` pour aligner avec le terrain. (g) **Intégration** : `ActiveTool::Cave = 12`, raccourci `Ctrl+Shift+G`, entrée toolbar lettre 'G', bloc dédié dans `ToolPropertiesPanel`. `MeshInsertDocument` membre du shell, chargé au boot depuis `instances/mesh_inserts.bin`. (h) Tests CTest `mesh_insert_tests` (7 tests : binary v1 round-trip avec toutes les valeurs incl. flags, Document Add/Remove/Update/GetByCategory, auto-Guid, CaveCatalog parse JSON + cas vide/malformé, CaveCamouflage poids dans rayon, PlaceCaveCommand Apply préserve invariant somme=255 + Undo restaure splat à ±5% près d'arrondis u8). (i) Catalogue placeholder `catalog.json` avec 3 entrées (small/medium/large). README dans `game/data/meshes/caves/` documente la convention de fichiers. (j) **MVP éditeur-side strict** : pas de rendu glTF runtime (besoin de tinygltf), pas de gizmo translate/rotate visuel (besoin de M100.17 EasyPlacementTool), pas d'auto-props rochers (besoin de M100.17 InstanceDocument), pas de streaming `MeshInsertRuntime` côté client jeu, pas de `MeshInsertSurfaceHook` ↔ SurfaceQueryService. Le format binaire est stable pour M100.41 (overhangs), M100.42 (arches), M100.43 (dungeons) qui réutiliseront `MeshInsertSystem` ; le runtime rendu sera câblé en follow-up. (k) **Invariant terrain visible** strict respecté (pas de modification de `m_noUserTextures`, frustum cull, caméra ; rien dans la chaîne de rendu pour l'instant). (l) `engine_core` étendu de 6 sources C++ (nouveaux dossiers `src/world_editor/volumes/` et `src/world_editor/volumes/caves/`). Aucun nouvel `.exe`. Aucun opcode, aucune migration DB. **Précédent** : 2026-05-13 — **M100.39 Thermal & Wind Erosion (clôt Phase 2.5)** stacké sur M100.35-38, même branche `claude/review-m100-tickets-z3aUB`. **Phase 2.5 « Terrain naturaliste » complète** : montagnes (M100.35) → rivières (M100.36) → côtes (M100.37) → vallées hydrauliques (M100.38) → crêtes adoucies + sable éolien (M100.39). (a) **Thermal** : relaxation cell-à-cell vers angle de repos sur N passes (early-exit sur convergence). Pour chaque cellule, calcule l'excès de pente vs ses 8 voisins et transfère `excess * forcePerPass`. Conservation de masse par construction. `preserveSteepSlopes` optionnel pour ne pas aplanir des falaises voulues. (b) **Wind** : particle system analogue à M100.38 mais direction de transport fixe (`windAngleDeg`). Chaque particule sample `exposure = h_pos - h_upwind`, érode quand >0, dépose en aval. RNG `std::mt19937` seedé déterministe. (c) **Mode `Both`** : Thermal mute le grid consolidé localement, Wind utilise ce post-thermal comme input. Une seule `ThermalWindErosionCommand` poussée à l'Apply avec les 2 jeux de deltas, undo strict inverse Wind puis Thermal. (d) **Outil + UI** : sliders sous-mode + paramètres physiques + stats résultat (passes exécutées, convergence, masse transférée, particles, cells érodées/déposées). `ActiveTool::ThermalWindErosion = 11`, raccourci `Ctrl+Shift+T`, entrée toolbar lettre 'T'. (e) Tests CTest `thermal_wind_erosion_tests` (9 tests : flat→0 thermal, cone→transfert, mass conservation, convergence early-exit, wind flat negligible, wind déterminisme byte-equal, wind 0 particules, command Apply/Undo/Redo, preserveSteepSlopes). (f) **MVP simplifications** : single-thread, assemblage zone 2×2 chunks, beach splat / restrictToSandSplat flags non câblés au splat (follow-up). (g) `engine_core` étendu de 4 sources C++ (sous-dossier `terrain/erosion/` existant). Aucun nouvel `.exe`, aucun opcode, aucune migration DB. **Avec M100.35-39 livrés, l'éditeur Phase 2.5 « Terrain naturaliste » est complet** ; reste pour la suite : Phase 11 (volumes 3D : caves, overhangs, arches, donjons, vmap bridge) et Phase 12 (accessibilité : Simple/Advanced mode, presets, tutoriel, deployment pipeline). **Précédent** : 2026-05-13 — **M100.38 Hydraulic Erosion (particle-based)** stacké sur M100.35/.36/.37, même branche `claude/review-m100-tickets-z3aUB`. (a) **Algorithme particle-based** : N gouttes parcourent la heightmap (`ConsolidatedHeightGrid` de M100.36), chacune transporte du sédiment, érode quand sa capacité l'autorise (≈ vitesse × pente × water), dépose sinon. Lit la grid pristine en lecture seule pour garantir déterminisme + invariance parallèle (no feedback entre gouttes). (b) **3 distributions de seeding** : `Uniform`, `WeightedAltitude` (CDF par altitude — les sommets émettent plus), `WeightedFlowAccum` (réutilise `D8FlowDirection` + `FlowAccumulation` de M100.36). RNG `std::mt19937` seedé reproductible. (c) **Helpers `BilinearGradientSample`** : `SampleHeightAndGradient(grid, cellX, cellZ)` interpole bilinéairement hauteur + gradient ; `DistributeBilinearDelta(grid, cellX, cellZ, value, outDeltas)` distribue le delta sur les 4 cellules voisines selon les poids bilinéaires (Σ poids = 1), avec gestion des frontières inter-chunks (cellule pile sur le bord émise dans les chunks adjacents — couture préservée bit-à-bit). (d) **Kernel `RunSingleDroplet`** : boucle ≤ maxLifetimeSteps avec inertie, gravité, capacité, érosion/déposition, évaporation, stop sous sea level ou hors-grid. Clamp `maxDeltaPerCellMeters` en post-process pour borner les runaways. (e) **`HydraulicSimulation::RunHydraulicOnGrid`** (pure-function testable) + `RunHydraulicSimulation` (orchestrateur éditeur qui assemble la grid depuis `TerrainDocument::EnsureLoaded` et lit le sea level via `WaterDocument::GetOcean()`). (f) **`HydraulicErosionCommand`** (ICommand) : Apply applique les deltas avec clamp aux bornes terrain et `MarkDirty/OnCommit`. Undo strict : delta × −1. (g) **`HydraulicErosionTool`** : sliders physique (sediment capacity, erosion/deposition rates, gravity, inertia, evaporation), distribution + seed, bornes (maxDeltaPerCell, stopUnderSeaLevel, preserveFlatAreas), boutons Simulate/Apply/Cancel/Re-simulate, stats (gouttes simulées, total steps, cellules érodées/déposées, delta min/max, temps total ms). Toggle preview overlay. (h) **Intégration** : `ActiveTool::HydraulicErosion = 10`, raccourci `Ctrl+Shift+H`, entrée toolbar lettre 'H', bloc dédié dans `ToolPropertiesPanel`. (i) Tests CTest `hydraulic_erosion_tests` (11 tests : flat → no erosion, 0 droplets → empty, déterminisme byte-equal, isolated peak has erosion, stop sea level, bilinear conservation, bilinear single-cell on integer pos, gradient sampling uniform slope, single droplet flat = 1 step max, Command Apply/Undo/Redo bit-equal, max delta clamp). (j) **Invariant terrain visible** strict respecté : pas de mutation de `m_noUserTextures`, frustum cull, caméra ; preview prévue overlay 2D `ImGui::GetForegroundDrawList` (rouge=érosion, vert=déposition) — l'overlay 2D actif est laissé en follow-up car nécessite un projection world→screen ; les stats numériques suffisent pour l'MVP. (k) **MVP simplifications** : single-thread (parallèle byte-equal byte-equal possible en follow-up via réduction par thread), assemblage zone 2×2 chunks autour de l'origine (extension zone-complète en follow-up). (l) `engine_core` étendu de 6 sources C++. Aucun nouvel `.exe`. Aucun opcode, aucune migration DB, aucun bump format binaire. **Précédent** : 2026-05-13 — **M100.37 Coastline & Sea Level Editor** stacké sur M100.35+M100.36, même branche `claude/review-m100-tickets-z3aUB`. (a) **Enrichit `OceanSettings`** introduite par M100.36 : ajoute `bottomColor[3]`, `turbidity`, `windInfluence`, `enabled`. Aucun refactor du champ `seaLevelMeters` existant ; le contrat M100.36 (lu via `WaterDocument::GetOcean()`) reste intact. (b) **`LakeInstance::isOcean = false`** : nouveau flag informatif marquant la surface océan globale (un seul par zone). (c) **Bump `kWaterVersion` v2 → v3** : ajout de la section ocean étendue (~32 octets) + flag isOcean (1 octet par lac). Reader rétrocompat v1 et v2 : champs absents prennent leur valeur d'entrée (typiquement les défauts `OceanSettings{}` / `isOcean=false`). Signatures `SaveWaterBin` / `LoadWaterBin` étendues pour prendre une struct `OceanSectionData` (mirror POD côté `engine::world::water`, conversion dans WaterDocument). Tous les callers mis à jour (WaterDocument, StreamCache, ChunkPackageWriter, tests M100.13/.36). (d) **Algorithmes purs** : `ExtractCoastlineSegments` (marching squares 2D 16-cas + tie-break selle), `ComputeCoastlineStats` (terre/océan/longueur côte/cellules bande plage, O(N)), `ComputeCoastlineSmoothingDeltas` (Gaussien 3×3 read-stable limité à la bande verticale autour de sea level, émet `SparseChunkDeltas`), `ComputeCoastlineCliffsDeltas` (différences finies → slope, élève côté terre / abaisse côté mer si slope > seuil dans la bande). (e) **`CoastlineCommand`** (ICommand) : Apply écrit `OceanSettings` complètes, insère OU met à jour in-place le `LakeInstance` océan (polygone englobant zone + marge 1000 m, 5 vertices), applique les deltas heightmap (smoothing + cliffs cumulés). Undo strict : restaure le snapshot ocean, retire l'insert (ou restaure le snapshot du lac existant), inverse les deltas. (f) **`CoastlineEditorTool`** : sliders ocean (sea level, color, turbidity, wind influence, enabled), checkboxes smoothing / cliffs / beach splat avec leurs paramètres, `RefreshPreview()` recalcule segments + stats, Apply pousse la commande. **Note MVP** : la passe "beach splat" (sand layer) est conservée comme flag UI mais non câblée à la commande (snapshot per-cell complexe pour Undo). Suivra un follow-up. (g) **Intégration** : `ActiveTool::Coastline = 9`, raccourci `Ctrl+Shift+C`, entrée toolbar (lettre 'C'), bloc dédié dans `ToolPropertiesPanel` (color edit, sliders, stats live, Apply/Cancel). (h) Tests CTest `coastline_tests` (9 tests : marching squares flat/below/hill, stats land/ocean, smoothing band-filter, cliffs slope-filter, v3 round-trip toutes valeurs, OceanSettings default étendu, Command Apply/Undo/Redo). (i) **Invariant terrain visible** strict respecté : aucune mutation de `m_noUserTextures`, `SetFrustumCullEnabled`, caméra, render passes. (j) `engine_core` étendu de 6 sources C++. Aucun nouvel `.exe`. **Précédent** : 2026-05-13 — **M100.36 River Network Generator (Watershed D8)** stacké sur M100.35, même branche `claude/review-m100-tickets-z3aUB`. (a) Introduit `OceanSettings { seaLevelMeters }` (header-only `src/world_editor/water/OceanSettings.h`) comme **source de vérité unique** du sea level de la zone. `WaterDocument` gagne un membre `m_ocean` + accesseurs `GetOcean() / SetOceanSettings()` ; tout consommateur du sea level (watershed, futurs coastline/erosions) lit via ces accesseurs, jamais via un buffer local. (b) **Bump `kWaterVersion` v1 → v2** : `instances/water.bin` reçoit une section "ocean" terminale (4 octets seaLevelMeters). Reader rétrocompat v1 : la version 1 est acceptée silencieusement, `seaLevelMeters` reste à la valeur d'entrée (typiquement le défaut `OceanSettings{}.seaLevelMeters = 50`). Signatures `SaveWaterBin` / `LoadWaterBin` étendues — `StreamCache` + `WaterDocument` + tests M100.13 ajustés. (c) Algorithmes purs partagés (M100.38/.39 réutiliseront) : `ConsolidatedHeightGrid` (`src/world_editor/water/`), `ComputeD8FlowDirection` (D8 avec tie-break déterministe NE→E→SE→S→SW→W→NW→N, sink encodé `kSinkDir=255`), `ComputeFlowAccumulation` (tri stable par altitude décroissante + propagation cumulative), `SimplifyPolylineDouglasPeucker` (récursion itérative, distance XZ uniquement). (d) `WatershedSimulation` orchestrateur en deux saveurs : `RunWatershedOnGrid(grid, seaLevel, params)` pure (testable) + `RunWatershedSimulation(terrainDoc, waterDoc, cfg, params)` qui assemble la grille depuis les chunks via `TerrainDocument::EnsureLoaded` et lit `seaLevel` depuis `WaterDocument::GetOcean()`. Génère rivers (filtrage par `minFlowThresholdCells`, Douglas-Peucker), lacs auto (flood-fill 8-conn + convex hull du bassin), deltas de carving sparse multi-chunks (réutilise le typedef `SparseChunkDeltas` introduit par M100.35). (e) `RiverNetworkCommand` (ICommand) : Apply = insère rivers + lakes par nom dans `WaterDocument`, applique carving sur `TerrainDocument`, écrit `OceanSettings` (snapshot précédent capturé à la construction). Undo strict : retire par nom, inverse les deltas, restaure le snapshot ocean. (f) `RiverNetworkTool` : gestion ≤ 64 sources, buffer local pour le slider sea level (écrit dans `WaterDocument` uniquement à Apply), boutons Simulate / Apply / Cancel. (g) Enum `ActiveTool` étendue avec `RiverNetwork = 8`. Raccourci `Ctrl+Shift+N`. Toolbar M100.35 mise à jour (8 outils + bouton X). `ToolPropertiesPanel` dispatche désormais sur `RiverNetwork` avec un canvas top-down 2D pour poser/retirer des sources + tous les sliders de simulation + preview des rivières simulées + compteurs résultat. (h) Tests CTest `river_network_tests` (16 tests : D8 steepest descent, sink encodé, flow acc somme amont, déterminisme, terminaisons sea/sink/boundary, Douglas-Peucker, OceanSettings default 50m, water.bin v2 round-trip + v1 rétrocompat, Command Apply/Undo/Redo, sea level lu depuis externe, déterminisme global, auto-lake polygon, carving deltas négatifs, inputs vides). (i) **Invariant terrain visible** strict respecté : pas de mutation de `m_noUserTextures`, frustum cull, caméra, render passes. La preview du réseau de rivières est dessinée via `ImDrawList` overlay 2D (lecture seule sur depth). (j) `engine_core` étendu de 7 sources C++. Aucun nouvel `.exe`. **Précédent** : 2026-05-13 — **M100.35 Toolbar à icônes + Outils macros terrain** sur la branche `claude/review-m100-tickets-z3aUB`. (a) Toolbar à icônes ImGui rendue par `WorldEditorShell::RenderFrame` juste sous le menu bar, hauteur fixe 48 px ; chaque outil (Sculpt / Stamp / Splat / Lake / River / Mountain Range / Valley Chain / X désélection) est un bouton 40×40 dessiné via `ImDrawList::AddRectFilled` + lettre centrale (placeholder procédural, pas de PNG embarqué — fallback déjà testé par `Test_Toolbar_MissingIcon_FallsBackToPlaceholder`). L'outil actif a un fond ambre `IM_COL32(255, 196, 64, 96)`. `BeginTabBar("OutilsTabs")` historique de `WorldEditorImGui` désormais supprimé (bloc préservé sous `#if 0` pour migration progressive des sous-outils "Herbe"/"Objets"/"Routes" vers leurs nouveaux tickets dédiés). (b) Deux nouveaux outils terrain `MountainRangeTool` et `ValleyChainTool` : pose de polyline (≤ 32 vertices) avec largeur, hauteur, bruit, asymétrie locaux interpolés linéairement. Algo de rasterisation pure `RasterizeMacroPolyline(params, invert)` dans `src/world_editor/terrain/PolylineMacroCore.{h,cpp}` : profil radial (Smoothstep/Linear/Exp), asymétrie via cross-product tangent vs perpendiculaire, bruit Simplex 2D déterministe (réutilise `EvalSimplex2D` de M100.6), couture inter-chunks par construction (rasterisation en coords monde, cellule pile sur la frontière émet le même delta dans les deux chunks). (c) Nouveau contrat partagé `SparseChunkDeltas = unordered_map<GlobalChunkCoord, unordered_map<uint32_t, float>>` consommé par M100.36 (rivers), M100.37 (coastline), M100.38/.39 (erosions). Spécialisation `std::hash<engine::world::GlobalChunkCoord>` ajoutée dans `PolylineMacroCore.h`. (d) `MacroPolylineCommandBase` (ICommand) : Execute applique les deltas, Undo soustrait, clamp aux bornes `kTerrainHeightMin/MaxMeters`, `MarkDirty` + `OnCommit` par chunk pour la régen LOD M100.8. Sous-classes triviales `MountainRangeCommand` ("Mountain Range") et `ValleyChainCommand` ("Valley Chain") avec sémantique additive / soustractive (invert au moment de la rasterisation, le delta stocké est déjà signé). (e) Enum `ActiveTool` étendue avec `MountainRange = 6`, `ValleyChain = 7`. Accesseurs `Mutable/Get MountainRangeTool() / ValleyChainTool()` dans le shell. Raccourcis optionnels `Ctrl+Shift+M` / `Ctrl+Shift+V`. `ToolPropertiesPanel` dispatche désormais sur ces deux outils avec un bloc "Macro polyline" complet (mode Open/Loop, profil flanc, seed/freq bruit, vertex sélectionné, canvas 2D top-down pour poser/déplacer les vertices, boutons Apply/Cancel, estimation chunks impactés). (f) Tests CTest WIN32 `macro_terrain_tests` (9 tests : rasterize ridge width, couture inter-chunks bord-à-bord, asymétrie ±, déterminisme bruit, Apply/Undo multi-chunk, valley == -mountain, Loop, < 2 vertices, 3 profils distincts) et `editor_toolbar_tests` (5 tests : layout ne couvre pas le viewport, click → SetActiveTool, hit-test hors zone, fallback placeholder atlas, SetActiveTool ne mute pas le rendu state). (g) Invariant de visibilité terrain documenté dans `docs/INVESTIGATION_terrain_invisible.md` strictement respecté : aucune mutation de `m_noUserTextures`, `SetFrustumCullEnabled`, caméra, ni `clearValues` du render pass terrain. La toolbar est une fenêtre ImGui en haut du viewport principal qui n'empiète pas sur la zone 3D. (h) `engine_core` étendu de 8 sources C++. Aucun nouveau `.exe`. Aucun impact serveur. **Précédent** : 2026-05-11 — **Wave 17 Entities suite livrée** (WorldObject + Unit + Player + Creature + UpdateFieldIndices, 5 tests CTest dédiés + cross-classe delta tests). Section 22 mise à jour. Précédente : 2026-05-11 — Ajout sections 18-23 (structure `src/shardd/`, Admin/RBAC, Persistence stores, PacketLog, Entities foundation, Boot wiring du shard). Avant : 2026-05-09 — Sous-organisation domaine (`src/client/{quest,combat,chat,inventory,economy,crafting,character_creation,social,trade,settings,hud,ui_common,debug,localization,fx}/`, `src/shardd/gameplay/{auction,crafting,gathering,guild,social,quest,spawner,event,character,economy,trade,chat}/`, `src/world_editor/{terrain,water,splat,camera,core}/`, `src/masterd/handlers/{character,shard,auth,chat,password,terms}/`). Précédente : 2026-05-09 — Réorganisation cmangos-style (`engine/` → `src/{shared,client,masterd,shardd,world_editor}/`, `db/` → `sql/`). Les mentions `engine/server/...` ci-dessous sont des références historiques aux phases passées (les fichiers ont depuis été déplacés sous `src/masterd/...` ou `src/shared/...`). **Phase 5.2 chat (friends routing)** sur la branche `claude/chat-friends-routing`. Même pattern que guild routing : `SELECT friend_id FROM friends WHERE player_id = ? AND status = 1` → set d'account_id des amis acceptés (le sender est ajouté au set pour qu'il voie son propre écho, par cohérence avec guild). Si seul le sender match (aucun ami en ligne), notice "Server" "No friends online to receive your message" renvoyée à l'expéditeur seul. Reste limité : `/p` (Party) et `/zone` toujours broadcast global (state shard-side, inaccessible au master sans RPC). **Phase 5.1 chat (guild routing)** sur la branche `claude/chat-guild-routing`. Le canal `Guild` (`/g`) ne broadcast plus à toutes les sessions : `ChatRelayHandler` interroge `guild_members` (`SELECT player_id FROM guild_members WHERE guild_id = (SELECT guild_id FROM guild_members WHERE player_id = ?)`), construit le set des account_id co-membres, puis filtre le snapshot `ConnectionSessionMap` (résolu via `SessionManager::GetAccountId`) pour n'envoyer le `CHAT_RELAY` qu'aux membres en ligne. Si le sender n'est pas dans une guilde (sub-query NULL → résultat vide), notice "You are not in a guild." renvoyée à l'expéditeur seul (channel=Server). `ChatRelayHandler` gagne `SetConnectionPool` ; câblé dans `main_server_linux.cpp`. Limites : `/p` (Party) et `/zone` toujours broadcast global (state vit en RAM côté shard, pas accessible au master). Friends pourrait suivre en 5.2 (table `friends` existe). **Phase 5 reconnect MVP (master auto-reconnect post-EnterWorld)** sur la branche `claude/master-reconnect-mvp`. Avant : si la connexion master tombait pendant le jeu (network blip, kick, restart serveur), `PumpPostAuthEvents` détectait le `Disconnected`, libérait silencieusement `m_masterClient`, et le chat / SAVE_POSITION échouait sans feedback utilisateur. Après : détection → bannière "Connexion perdue, reconnexion en cours..." (overlay Win32 prioritaire sur welcome banner et chat) → tentative auto après 2 s → re-Connect + re-AUTH (réutilise `m_login`/`m_password` toujours en mémoire post-login) + re-`SendEnterWorldAsync` (pour ré-enregistrer le character actif côté `SessionCharacterMap` master). Sur succès : nouvelle session master, bannière "Connexion rétablie", chat reprend. Sur échec (timeout ou rejet) : `EnterAuthErrorPhase(Phase::Login)` avec message "Reconnexion impossible — retour à la connexion". Implémentation : `AuthUiPresenter` ajoute `m_postEnterWorld{CharacterId,CharacterName}` (mémorisés via `RememberPostEnterWorldCharacter` appelé depuis `Engine.cpp` à la consommation d'`EnterWorldCommand`), `m_reconnect{InProgress,Attempt,MaxAttempts=1,NextAt,StatusText}`, `m_reconnectAsyncDone` (atomic) + `m_reconnectAsyncSuccess`. Méthodes publiques : `RememberPostEnterWorldCharacter`, `TickReconnect(cfg)` (appelée chaque frame post-auth depuis `Engine.cpp`), `IsReconnecting()`, `ReconnectStatusText()`. Worker reconnect réutilise le pattern de `StartMasterFlowWorker` (instance `m_masterClient` allouée en main thread, raw pointer au worker, survit après l'exit). Helpers locaux `ReconnectWaitConnected` + `ReconnectApplyTls` dupliqués en anonymous namespace dans `AuthUiPresenterSettings.cpp` (les originaux sont privés au TU `AuthUiPresenterCore.cpp`). Localisation : 3 nouvelles clés FR/EN (`auth.info.reconnect_in_progress`, `auth.info.reconnect_success`, `auth.error.reconnect_failed_back_to_login`). Limite MVP : 1 seule tentative ; pas de backoff exponentiel ; pas de reconnect UDP shard (à voir Phase 5.1+). **Phase 4 chat (character display name + whisper)** sur la branche `claude/chat-character-name-whisper` (stack après Chat MVP). Nouveaux opcodes 47/48 (`kOpcodeCharacterEnterWorldRequest`/`Response`). Wire-breaking sur `ChatSendRequestPayload` : ajout du champ `targetToken` (string vide pour non-whisper, character_name normalisé pour /whisper). Côté serveur : `engine/server/SessionCharacterMap.h/.cpp` (mapping `connId → {character_id, character_name, normalizedName}` + reverse `normalizedName → connId` pour le whisper). Nouveau `engine/server/CharacterEnterWorldHandler.h/.cpp` valide ownership en DB (`SELECT name FROM characters WHERE id=? AND account_id=? AND deleted_at IS NULL`), compare le name DB au name client byte-pour-byte (anti-spoof), enregistre dans le map. `ChatRelayHandler` étendu : sender display = character_name via `m_charMap->GetByConnId(connId)`, fallback login d'account. Whisper résolu via `FindConnByNormalizedName` : target offline → notice "Server" à l'expéditeur ; target online → 2 paquets (destinataire reçoit "[from sender] body", expéditeur reçoit "[to target] body"). Câblage dans `main_server_linux.cpp` + watchdog purge `sessionCharMap.Remove(connId)` à la fermeture. Côté client : `AuthUiPresenter::SendChatAsync` signature widened (channel, targetToken, text) ; nouvelle `SendEnterWorldAsync(characterId, characterName)` fire-and-forget ; `Engine.cpp` l'appelle juste après `m_currentCharacterId = enterCmd.characterId` à la consommation `EnterWorldCommand`. `ChatUiPresenter::SubmitInputLine` passe `parsed.whisperTargetToken` au callback (vide pour non-whisper) au lieu de pré-formatter "[to X]" dans le body. Tests `chat_payloads_tests` étendus avec round-trip whisper. Limites restantes Phase 4.5+ : pas de routage par canal (Say/Yell/Zone/Party/Guild/Friends broadcast tous global), normalisation whisper case-insensitive ASCII seulement (les caractères accentués matchent exactement). **Chat MVP réseau (CHAT_SEND_REQUEST + CHAT_RELAY)** sur la branche `claude/chat-network-mvp`. Premier câblage end-to-end du chat : opcodes 45/46 (`kOpcodeChatSendRequest`, `kOpcodeChatRelay`), payloads `src/shared/network/ChatPayloads.h/.cpp` (`ChatSendRequestPayload { uint8 channel, string text }` ≤ 256 octets ; `ChatRelayPayload { uint64 ts_ms, uint8 channel, string sender, string text }`), tests round-trip `chat_payloads_tests`. Master handler `engine/server/ChatRelayHandler.h/.cpp` valide la session (ConnectionSessionMap + SessionManager), résout le sender via `AccountStore::FindByAccountId` (login = sender display pour l'instant), broadcast CHAT_RELAY à toutes les sessions actives via le nouveau `ConnectionSessionMap::Snapshot()`. Whisper (channel=Whisper) renvoie une notice « not yet supported » à l'expéditeur seulement. Câblé dans `main_server_linux.cpp` (handler `chatRelayHandler`). Côté client : `AuthUiPresenter::SendChatAsync(channel, text)` fire-and-forget sur la connexion master vivante ; `AuthUiPresenter::SetMasterPushHandler(...)` permet à l'engine d'installer un dispatcher qui parse les paquets push (request_id=0) reçus dans `PumpPostAuthEvents`. `ChatUiPresenter::SetSendCallback` + `SubmitInputLine` envoie maintenant au master via la callback ; pas d'écho local si l'envoi réussit (le serveur rebroadcast → `PushNetworkLine`), fallback écho local "Local" si offline. `Engine.cpp` câble la callback ChatUi → AuthUi.SendChatAsync au boot et installe un push handler qui parse CHAT_RELAY → décode le canal via `engine::net::TryDecodeChannelWire` → `m_chatUi.PushNetworkLine`. Limites volontaires v1 : routage par canal non implémenté (Say/Yell/Zone/etc broadcastent tous global, le canal est juste echoé pour la couleur), whisper non câblé (target lookup nécessite character display_name côté master qui ne sera dispo qu'après EnterWorld), sender = login (character display_name plus tard). **Hotfix `Send CHARACTER_CREATE failed` (réutilisation connexion master post-flow)** sur la branche `claude/fix-master-flow-reuse-connection`. Symptôme utilisateur : sur l'écran CharacterCreate, clic Créer → écran d'erreur « Send CHARACTER_CREATE failed ». Cause : `StartMasterFlowWorker` créait un `NetClient` **local** sur la stack du worker thread. Conséquences en cascade : (1) ce local NetClient ouvre une 2e connexion master, dont l'AUTH kick par duplicate-login la session AuthOnly de `m_masterClient` ; (2) à la fin du worker, le local NetClient est détruit → connexion fermée ; (3) `m_masterClient` (AuthOnly) ferme aussi (heartbeat timeout après kick session). Quand l'utilisateur clique Créer, `m_masterClient` est dans l'état `Disconnected` → `RequestResponseDispatcher::SendRequest` retourne `false` → message d'erreur. **Fix** : `MasterShardClientFlow::Run` populate désormais `result.session_id` avec la session créée à l'AUTH. `AuthUiPresenter::AsyncResult::sessionId` propagé en main thread → `m_masterSessionId = copy.sessionId` quand le flow réussit (les futurs `RequestResponseDispatcher::SetSessionId` reçoivent ainsi la session post-Flow, pas l'AuthOnly kickée). `StartMasterFlowWorker` rewrité : ferme proprement l'ancien `m_masterClient` puis ré-alloue + bind TLS en main thread AVANT de lancer le worker, qui ne capte qu'un raw pointer. La connexion **survit** au worker et reste utilisable post-flow par CharacterCreate, CharacterDelete, SAVE_POSITION, EnterWorld. **Phase 3.7.5 (uint64 character_id end-to-end via Hello)** sur la branche `claude/hello-uint64-character-id`. Bump `kProtocolVersion` 1 → 2 (wire-breaking UDP gameplay) : `HelloMessage::clientNonce` passé de `uint32` à `uint64` ; `EncodeHello` / `DecodeHello` utilisent `WriteU64` / `ReadU64`, payload 8 → 12 octets. `GameplayUdpClient::SendHello` signature widened. `Engine.cpp` ne tronque plus à `& 0xFFFFFFFF` lors de la consommation `EnterWorldCommand` ; lecture config `client.gameplay_udp.character_key` reinterpret-cast int64→uint64 pour préserver les valeurs bit 63 set. Côté shard : `ServerApp::HandleHello(uint64)`, `ConnectedClient::{helloNonce, persistenceCharacterKey}` widened, `m_bannedCharacterKeys: unordered_set<uint64>`, ban file parser/writer alignés. `CharacterPersistence::{LoadCharacter, BuildCharacterStateRelativePath}` + `PersistedCharacterState::characterKey` widened ; le path filename `character_<key>.ini` accepte `std::to_string(uint64)` directement. `AuctionHouse` widened en cohérence (`{seller,buyer,highBidder}CharacterKey: uint64`) pour éviter les troncations à la liste/bid/buyout. `RefundGoldToCharacter`, `DepositMailboxDelivery`, `FindConnectedClientByCharacterKey` signatures uint64. **Phase 3.11.1 (panneau chat Dear ImGui scrollable)** sur la branche `claude/chat-imgui-panel`. Remplace l'overlay Win32 mono-couleur de Phase 3.11 par un vrai panneau ImGui ancré en bas-gauche (520×220 px par défaut, configurable via `render.chat_imgui.{width_px, height_px, anchor_margin_px, enabled}`). Nouveau renderer `src/client/render/ChatImGuiRenderer.h/.cpp` (Windows uniquement, partage le contexte ImGui de `m_worldEditorImGui` exactement comme `m_authImGui`). Couleurs par canal (10) lues depuis `engine::net::ChannelColorArgb` et converties ARGB→ImVec4 ; auto-scroll bottom uniquement quand `ChatUiPresenter::ScrollLinesFromEnd()==0` ; ligne d'invite bas écho l'`InputLine` quand le focus est actif (caret `_`), sinon hint `[/] tchatter`. `ChatUiPresenter` expose 4 nouveaux accesseurs const (`History()`, `InputLine()`, `ChannelFilterMask()`, `ScrollLinesFromEnd()`) — aucune mutation, le renderer est lecture seule. `Engine.h` ajoute `m_chatImGui` (unique_ptr, créé alongside `m_authImGui` après init `m_worldEditorImGui`). `Engine.cpp` étend la condition `NewFrame` ImGui post-auth + nouvelle branche `else if (chat ImGui actif)` qui appelle `m_chatImGui->Render()` puis `ImGui::Render()` ; `RecordToBackbuffer` est étendu pareil. La logique d'input (`/` focus, Enter submit, 1-0 filtres, scroll PageUp/Dn) reste dans `ChatUiPresenter::Update()` — pas d'`ImGui::InputText` actif donc pas de capture clavier. Quand le panneau ImGui est actif, le path legacy `m_window.SetOverlayText(BuildHudPanelText())` est volontairement skippé pour éviter le double affichage ; le fallback texte legacy reste utilisé sur Linux ou si `render.chat_imgui.enabled=false`. Limites encore ouvertes : pas encore de toggles cliquables sur les chips canaux (l'utilisateur passe par 1-0 clavier), pas d'`ImGui::InputText` natif (l'écho de saisie est statique avec caret `_`), pas de scrollbar ImGui visuelle (utilise `SetScrollHereY` pour le sticky-bottom). **Phase 3.6.6 (CHARACTER_SAVE_POSITION client trigger)** sur la branche `claude/character-save-position-client` (stack au-dessus de Phase 3.6.5). `AuthUiPresenter` expose `SavePositionAsync(characterId, x, y, z, yawDeg, pitchDeg)` qui sérialise via `BuildCharacterSavePositionRequestPayload` et envoie un paquet opcode 43 (requestId=0, fire-and-forget) sur `m_masterClient` toujours vivant grâce au fix Phase 2/3 (suppression des `ResetMasterSession()` avant MasterFlow). Méthode `PumpPostAuthEvents()` drain `m_masterClient->PollEvents()` chaque frame post-auth : responses CHARACTER_SAVE_POSITION_RESPONSE loggées en debug (pas de matching), `Disconnected` → `m_masterSessionId=0` + `m_masterClient.reset()` pour échec propre des futurs Save. Côté `Engine` : 4 nouveaux membres (`m_currentCharacterId`, `m_nextSavePositionTime`, `m_savePositionIntervalSec` configurable `client.save_position.interval_sec` défaut 30 s plancher 5 s, `m_shutdownPositionSaved`). Dans la consommation `EnterWorldCommand` : capture `enterCmd.characterId` + arme `m_nextSavePositionTime = now + interval`. Branche `else` du gate auth (chaque frame post-auth) : `m_authUi.PumpPostAuthEvents()` puis tick périodique qui appelle `SavePositionAsync` avec `out.camera.{position,yaw,pitch}` (rad→deg). `Engine::Shutdown` : sauvegarde finale fire-and-forget juste avant `m_authUi.Shutdown()`, lit la caméra depuis `m_renderStates[m_renderReadIndex.load()&1].camera` (master encore vivant à ce point). **Phase 3.6.5 (CHARACTER_SAVE_POSITION protocol, server-side)** sur la branche `claude/character-save-position`. Nouveaux opcodes 43/44 (41/42 utilisés Phase 3.9 — déjà mergée). Payloads `CharacterSavePositionRequestPayload { uint64 characterId, float x, y, z, yawDeg, pitchDeg }` (28 octets) + `CharacterSavePositionResponsePayload { uint8 success }`. Handler `engine/server/CharacterSavePositionHandler.h/.cpp` côté master : résout `connId → sessionId → accountId`, rejette NaN/Inf, `UPDATE characters SET spawn_x=?, spawn_y=?, spawn_z=?, spawn_yaw_deg=?, spawn_pitch_deg=? WHERE id=? AND account_id=? AND deleted_at IS NULL` (gating par account_id empêche cross-compte ; deleted_at gating empêche save sur perso supprimé). Câblé dans `main_server_linux.cpp` + CMake. Aucune migration. Tests `character_save_position_payloads_tests` (round-trip request/response, rejet short/null, position zero edge case). Le **déclenchement client** (envoi périodique + au shutdown) est en Phase 3.6.6 (PR #396, stack au-dessus). **Phase 3.11 (premier rendu visuel du chat HUD)** sur la branche `claude/chat-hud-overlay`. Ajoute `ChatUiPresenter::BuildHudPanelText()` : version utilisateur du panneau (pas de header debug, pas de code couleur ARGB, pas de filter mask hex) — N lignes "[hh:mm TAG] Sender: text" filtrées par `m_channelFilterMask` + une ligne d'invite ("> _input_" en focus, "[/] pour tchatter — [1..0] filtres canal" sinon). Engine.cpp post-auth else branch : priorité d'affichage = welcome banner (Phase 3.5) > chat HUD (Phase 3.11) > vide. Le chat est désormais visible à l'écran via `m_window.SetOverlayText` (overlay Win32 natif). Pas de nouvelle infrastructure ImGui (un vrai panneau ImGui scrollable sera une Phase 3.11.1 ulterieure si besoin). L'input clavier (`/` pour focus, Enter pour submit, 1-0 toggle filters, scroll history) est déjà câblé dans `ChatUiPresenter::Update()` appelé chaque frame post-auth (`Engine.cpp` ~3027). **Phase 3.8 (race / class string identifiers)** sur la branche `claude/character-race-class-strings` (stack au-dessus de Phase 3.7). Migration 0033 ajoute `characters.race_str` + `class_str` (VARCHAR(32) NOT NULL DEFAULT ''). `CharacterCreateHandler` persiste désormais les strings reçues du payload (auparavant ignorées et hardcodées à 0). `CharacterListHandler` SELECT inclut les 2 colonnes ; `CharacterListEntry` payload étendu avec `race_str` + `class_str` (length-prefixed UTF-8). Renderer `AuthImGuiCharacterSelect` humanise via clé localisation `auth.character_select.race.<id>` / `class.<id>` (FR/EN ajoutées pour 8 races + 8 classes), fallback = id capitalisé. Si row pré-migration (champs vides), affichage retombe sur l'ancien subline (`Slot N - Niveau X`). **Phase 3.7 (character_id propagated to shard)** sur la branche `claude/character-id-to-shard` (stack au-dessus de Phase 3.6). Découverte clé : `ServerApp::HandleHello` traite déjà `clientNonce` comme `tentativeCharacterKey`. Phase 3.7 = simple branchement client : à la consommation d'`EnterWorldCommand`, on `m_cfg.SetValue("client.gameplay_udp.character_key", enterCmd.characterId & 0xFFFFFFFF)` AVANT `InitGameplayNet()`. `SendHello` envoie alors le `character_id` réel comme `clientNonce`. Aucun changement de wire / crypto ticket. Limite : uint32 suffit pour < 4G persos (cas de test largement dépassable). **Phase 3.6 (per-character spawn from DB)** sur la branche `claude/character-spawn-from-db` (stack au-dessus de Phase 3.5). Migration 0032 : ajoute `characters.spawn_x/y/z/yaw_deg/pitch_deg` (FLOAT, défauts `0/100/0/0/-10`). `CharacterCreateHandler` initialise les colonnes depuis la config serveur `character_creation.default_spawn.*` à l'INSERT. `CharacterListHandler` SELECT inclut les 5 colonnes. Wire format CHARACTER_LIST_RESPONSE étendu : 5 floats (20 octets) ajoutés à la fin de chaque entry — wire-breaking, master + client doivent se déployer ensemble. `CharacterListEntry` côté client expose les champs ; `EnterWorldCommand` les porte (`spawnX/Y/Z`, `spawnYawDeg`, `spawnPitchDeg`, `hasSpawn`). `Engine.cpp` priorité 1 = perso si `hasSpawn`, fallback `client.world.default_spawn.*` config. Tests `character_list_payloads_tests` mis à jour (round-trip avec spawns). **Note chat global** : `ChatUiPresenter::BuildPanelText` est appelé chaque frame mais la chaîne n'est utilisée que pour `LOG_DEBUG` (Engine.cpp:3287). Aucun rendu visuel n'existe — feature jamais implémentée, pas une régression. Phase à part entière (UI ImGui chat panel) à planifier. **Phase 3.5 (post-auth spawn + welcome HUD)** sur la branche `claude/post-auth-spawn-hud`. À la consommation de `EnterWorldCommand` dans `Engine.cpp` (branche `else` du gate auth) : (1) la caméra est téléportée à la position lue dans `client.world.default_spawn.{x,y,z,yaw_deg,pitch_deg}` (`config.json`) ; (2) une bannière « Bienvenue, {name} ! » est postée via `m_window.SetOverlayText` et expire après 5 s (`m_enterWorldBannerText` + `m_enterWorldBannerExpiry` dans `Engine.h`) ; (3) la connexion UDP gameplay reste câblée comme en Phase 3. Clé localisation `auth.enter_world.welcome` ajoutée FR/EN. Limites encore ouvertes : pas de `characters.spawn_x/y/z` en DB (Phase 3.6) ; `character_id` non propagé via le ticket shard (Phase 3.7). **Phases 2 + 3 du flux post-auth** déjà mergées (PR #386). **Phase 2** : `MasterShardClientFlow.Run()` envoie un `CHARACTER_LIST_REQUEST` sur la connexion master (toujours active après `TICKET_ACCEPTED`, échec non-fatal) et remplit `MasterShardFlowResult::character_list`. `AuthUiPresenter` introduit `Phase::CharacterSelect` + membres `m_characterList` / `m_selectedCharacterIndex`. Dans la branche succès du flow, `m_flowComplete = true` est remplacé par : `m_postRegistrationCharacterCreatePending` ou liste vide → `Phase::CharacterCreate` ; sinon → `Phase::CharacterSelect`. Nouveaux fichiers `src/client/auth/screens/AuthScreenCharacterSelect.cpp` (presenter) et `src/client/render/auth/screens/AuthImGuiCharacterSelect.cpp` (renderer : liste cliquable + boutons Retour / Créer / Jouer). **Phase 3** : nouveau struct `AuthUiPresenter::EnterWorldCommand` (one-shot, mirror de `VideoSettingsCommand`) émis sur clic « Jouer », consommé par `Engine.cpp` dans la branche `else` du gate auth (~ligne 2853) ; `MasterShardFlowResult::shard_endpoint` ajouté + persisté dans `m_chosenShardEndpoint` côté presenter. Le consume-side override les clés `client.gameplay_udp.{host,port,enabled}` puis appelle `ShutdownGameplayNet()` (si déjà init au boot) + `InitGameplayNet()` pour câbler la connexion UDP au shard accepté. Clés de localisation `auth.character_select.*` ajoutées en FR/EN. Phase 1 (protocole CHARACTER_LIST) déjà mergée : opcodes 39/40, payloads, handler master, tests `character_list_payloads_tests`. Itération 7 du login : gap CONNEXION→IDENTIFIANT resserré à ~9 px avec trait centré (ImGui::Spacing après Separator retiré dans BeginPanel — affecte tous les écrans), champs Identifiant et Mot de passe plus hauts (FramePadding y 3 → 8 dans DrawAuthGoldField, hauteur ≈ 19 → 29 px), descente accrue des 4 boutons (Dummy 18 → 32 avant Récupération/Portail, 14 → 28 avant Créer/Se connecter). Itération 6 : aération formulaire (extraSpacingPx 6, Dummy 12 entre les deux champs). Itération 5 : cadre +30 px hauteur, Tweaks 218 → 160 px collé en bas. Itération 4 : recentrage via `BeginPanel(stageW, titleZoneW, ...)`. Itération 3 : titre stage 96 %, sous-titre 2.5x, cadre +10 px, Tweaks sans header. Itération 2 : titre 5.0x + marge sup., persistance suppression infoBanner langue, retrait cédilles. Itération 1 : cadre 570 px, chips Tab/Entrée masquées, tooltip « Se souvenir de moi », badge éphémère, Tweaks 0.85x + boutons interactifs. Plus en amont : corrections migrations 0017-0031, ajout passes auth Vulkan, templates email déplacés vers `web-portal/email-templates/` et `game/data/email/`.

---

## 1. Vue d'ensemble architecturale

```
┌─────────────────────────────────────────────────────────────────┐
│                        CLIENT (Windows)                         │
│  src/client/auth/   ←→   src/client/render/auth/                │
│  Presenter (logique)         Renderer (affichage ImGui/Vulkan)  │
│         ↕ RenderModel (struct de données UI)                    │
│  src/client/        ←→   src/client/render/                      │
│  HUD, inventaire, chat       Passes Vulkan, terrain, particules │
└──────────────────────────────────┬──────────────────────────────┘
                                   │ UDP / TCP (src/shared/network/)
┌──────────────────────────────────▼──────────────────────────────┐
│                     SERVEUR MASTER (Linux)                      │
│  src/masterd/     →  handlers auth, register, shards, terms     │
│  src/shared/db/   →  pool MySQL, migrations                     │
└──────────────────────────────────┬──────────────────────────────┘
                                   │ shard tickets (ShardToMasterClient)
┌──────────────────────────────────▼──────────────────────────────┐
│                     SERVEUR SHARD (Linux)                       │
│  src/shardd/      →  gameplay : quêtes, craft, guildes, combat  │
└─────────────────────────────────────────────────────────────────┘
```

**Technologies :** C++20, Vulkan, ImGui, MySQL, UDP maison, CMake + vcpkg.

---

## 2. Flux d'authentification — interaction complète

Ce flux est le plus important pour comprendre les écrans d'auth.

```
Utilisateur → AuthUiPresenter (logique) → RenderModel → AuthImGuiRenderer (affichage)
                    ↓
             StartXxxWorker()  (thread background)
                    ↓
             NetClient → RequestResponseDispatcher → Serveur Master
                    ↓
             BuildModel_Xxx() → RenderModel mis à jour → re-render
```

### Ordre des écrans (état actuel après réordonnancement 2026-04-27)

```
Premier lancement (compte inexistant) :
  LanguageSelectionFirstRun → Login → Register → EmailConfirmationPending →
  VerifyEmail → Login (re-saisie credentials) → Terms (si CGU à accepter) →
  ShardPick (forcé, même avec un seul royaume) →
  MasterFlow (TICKET_ACCEPTED + CHARACTER_LIST) →
  CharacterCreate (post-Register forcé) → Game

Connexion utilisateur existant :
  Login → Terms (si CGU mises à jour, sinon sauté) →
  ShardPick → MasterFlow (TICKET_ACCEPTED + CHARACTER_LIST) →
  CharacterSelect (≥1 perso) ou CharacterCreate (0 perso) →
  [clic « Jouer » → EnterWorldCommand → Engine consume → InitGameplayNet shard host:port] →
  Game (scène 3D rendue par m_world.Update inconditionnel)
```

Drapeaux clés (`src/client/auth/AuthUi.h`) :
- `m_postRegistrationCharacterCreatePending` (bool) — armé sur `Register` succès, désarmé sur
  `CharacterCreate` succès / annulation / Escape / `flowComplete`. Quand vrai, `ShardPick` redirige
  vers `Phase::CharacterCreate` au lieu de `MasterFlow`.
- `m_chosenShardId` (uint32_t) — royaume sélectionné par l'utilisateur sur `ShardPick`. Persiste à
  travers `Phase::CharacterCreate` et sert d'override `m_shardFlowOverrideId` pour le `MasterFlow`
  final qui connecte le client au shard.
- `MasterShardClientFlow::SetShardPickWhenMultiple(true)` (appelé par AuthUi) force le retour
  `shard_choice_required` même quand un seul shard est en ligne. Le défaut de la classe est `false`
  pour que le client headless `ClientFlowMain` continue d'auto-sélectionner.

### Fichiers impliqués par phase d'auth

| Phase UI | Presenter (logique) | Renderer (affichage) |
|---|---|---|
| Sélection langue | `auth/screens/AuthScreenLanguageSelect.cpp` | `render/auth/screens/AuthImGuiLanguageSelect.cpp` |
| Connexion | `auth/screens/AuthScreenLogin.cpp` | `render/auth/screens/AuthImGuiLogin.cpp` |
| Inscription | `auth/screens/AuthScreenRegister.cpp` | `render/auth/screens/AuthImGuiRegister.cpp` |
| Vérif email | `auth/screens/AuthScreenVerifyEmail.cpp` | `render/auth/screens/AuthImGuiVerifyEmail.cpp` |
| Mot de passe oublié | `auth/screens/AuthScreenForgotPassword.cpp` | `render/auth/screens/AuthImGuiForgotPassword.cpp` |
| Choix shard | `auth/screens/AuthScreenShardPick.cpp` | `render/auth/screens/AuthImGuiShardPick.cpp` |
| Création personnage | `auth/screens/AuthScreenCharacterCreate.cpp` | `render/auth/screens/AuthImGuiCharacterCreate.cpp` |
| Sélection personnage | `auth/screens/AuthScreenCharacterSelect.cpp` | `render/auth/screens/AuthImGuiCharacterSelect.cpp` |
| Options | `auth/screens/AuthScreenOptions.cpp` | `render/auth/screens/AuthImGuiOptions.cpp` |
| CGU | `auth/screens/AuthScreenTerms.cpp` | `render/auth/screens/AuthImGuiTerms.cpp` |
| Erreur | `auth/screens/AuthScreenError.cpp` | `render/auth/screens/AuthImGuiError.cpp` |

---

## 3. Comment lire un écran d'auth (règle de lecture)

Chaque écran est découpé en **deux fichiers** :

### Fichier Presenter — `src/client/auth/screens/AuthScreenXxx.cpp`
- `BuildModel_Xxx(RenderModel& model)` → remplit la struct `RenderModel` avec les données à afficher (textes, champs, boutons, états actif/survolé).
- `Update_Xxx(Input, Config, Window, ...)` → gère la navigation clavier hors ImGui.
- `ImGuiXxx(...)` → méthodes appelées par le renderer quand l'utilisateur clique/tape.
- `StartXxxWorker(cfg)` → lance le thread réseau pour envoyer la requête.

### Fichier Renderer — `src/client/render/auth/screens/AuthImGuiXxx.cpp`
- Lit le `RenderModel` fourni par le presenter.
- Dessine avec ImGui (panneaux, champs de saisie, boutons, couleurs).
- Appelle les méthodes `ImGuiXxx()` du presenter en réponse aux interactions utilisateur.
- **C'est ICI qu'on modifie le visuel** : couleurs, polices, disposition, animations.

### Fichiers communs
| Fichier | Rôle |
|---|---|
| `src/client/auth/AuthUi.h` | Déclaration complète d'`AuthUiPresenter` : toutes les phases, membres, méthodes. **Lire en premier pour comprendre la structure.** |
| `src/client/auth/AuthUiPresenterCore.cpp` | Init, `Update()` global, dispatch des phases, `SubmitCurrentPhase()`, gestion async. |
| `src/client/auth/AuthUiPresenterSettings.cpp` | Persistance locale : remember-me, locale, settings JSON. |
| `src/client/auth/AuthUiPresenterNative.cpp` | Auth native Windows (hors ImGui). |
| `src/client/render/AuthUiRenderer.h` | Interface du renderer (méthode `Render(RenderModel)`). |
| `src/client/render/AuthImGuiRenderer.h/.cpp` | Implémentation ImGui du renderer. Dispatch vers les sous-renderers par phase. |
| `src/client/render/auth/AuthImGuiCommon.h/.cpp` | Helpers partagés : couleurs, polices, style, boutons communs, champs de saisie. |

### Struct centrale : `RenderModel` (dans `src/client/auth/AuthUi.h`)
```
RenderModel
├── sectionTitle          : titre du panneau
├── fields[]              : champs de saisie (label, valeur, secret, actif, survolé)
├── bodyLines[]           : lignes de texte (liens, hints, CGU...)
├── actions[]             : boutons (label, primaire, actif, survolé)
├── languageFirstRunCards[]: cartes langue (premier lancement)
├── infoBanner / errorText: messages info/erreur
└── ... (flags de layout)
```

---

## 4. Couche réseau — fichiers clés

| Fichier | Rôle |
|---|---|
| `src/shared/network/NetClient.h/.cpp` | Socket TCP bas niveau : connexion, envoi, réception. Thread IO interne. |
| `src/shared/network/NetClient_linux.cpp` | Implémentation Linux de NetClient. |
| `src/shared/network/RequestResponseDispatcher.h/.cpp` | Associe requêtes et réponses via `request_id`. Pump() = boucle principale. Gère les timeouts. |
| `src/shared/network/PacketBuilder.h/.cpp` | Construit un paquet binaire v1 (en-tête + payload). |
| `src/shared/network/PacketView.h/.cpp` | Vue lecture sur un paquet reçu (sans copie). |
| `src/shared/network/ByteReader.h/.cpp` | Désérialisation séquentielle (ReadU32, ReadString…). |
| `src/shared/network/ByteWriter.h/.cpp` | Sérialisation séquentielle (WriteU32, WriteString…). |
| `src/shared/network/ProtocolV1Constants.h` | Opcodes, tailles max, constantes du protocole. |
| `src/shared/network/NetErrorCode.h` | Enum des codes d'erreur réseau. |
| `src/shared/network/ErrorPacket.h/.cpp` | Paquet ERROR : build (serveur→client) + parse (client). |
| `src/shared/network/AuthRegisterPayloads.h/.cpp` | Payloads auth, register, reset password, vérif email, disponibilité pseudo. |
| `src/shared/network/CharacterPayloads.h/.cpp` | Payloads création/liste de personnages. |
| `src/shared/network/ShardPayloads.h/.cpp` | Payloads liste shards. |
| `src/shared/network/ShardTicketPayloads.h/.cpp` | Payloads tickets de connexion shard. |
| `src/shared/network/ServerListPayloads.h/.cpp` | Payloads liste serveurs. |
| `src/shared/network/TermsPayloads.h/.cpp` | Payloads CGU. |
| `src/shared/network/ShardToMasterClient.h/.cpp` | Connexion shard→master (enregistrement, heartbeat). |
| `src/shared/network/MasterShardClientFlow.cpp` | Flux complet master→shard (flow d'auth). |

### Format paquet v1
```
[ uint16 opcode ][ uint16 flags ][ uint32 request_id ][ uint64 session_id ][ uint32 payload_size ][ payload... ]
```
- `request_id == 0` → push serveur (pas de requête associée).
- `request_id > 0` → réponse à une requête client.

---

## 5. Couche serveur — fichiers clés

### Handlers principaux
| Fichier | Rôle |
|---|---|
| `src/masterd/handlers/auth/AuthRegisterHandler.h/.cpp` | Traite AUTH_REQUEST et REGISTER_REQUEST. Valide, hashe, crée compte. |
| `src/masterd/handlers/password/PasswordResetHandler.h/.cpp` | Reset mot de passe par email. |
| `src/masterd/handlers/character/CharacterCreateHandler.h/.cpp` | Création de personnage. |
| `src/masterd/handlers/shard/ShardRegisterHandler.h/.cpp` | Enregistrement d'un shard auprès du master. |
| `src/masterd/handlers/shard/ShardTicketHandler.h/.cpp` | Génération de ticket de connexion shard. |
| `src/masterd/handlers/shard/ServerListHandler.h/.cpp` | Retourne la liste des shards disponibles. |
| `src/masterd/handlers/terms/TermsHandler.h/.cpp` | Acceptation des CGU. |

### Comptes et validation
| Fichier | Rôle |
|---|---|
| `src/masterd/account/AccountRecord.h` | Struct d'un compte (id, login, email, hash, état…). |
| `src/masterd/account/AccountStore.h` | Interface abstraite du store de comptes. |
| `src/masterd/account/MysqlAccountStore.h/.cpp` | Implémentation MySQL du store. |
| `src/masterd/account/InMemoryAccountStore.h/.cpp` | Implémentation mémoire (tests). |
| `src/masterd/account/AccountValidation.h/.cpp` | Règles de validation (login, email, password, nom perso…). |

### Infrastructure serveur
| Fichier | Rôle |
|---|---|
| `src/shared/network/NetServer.h/.cpp` | Serveur TCP/UDP : accept, dispatch paquets entrants. |
| `src/shared/server_bootstrap/ServerApp.h/.cpp` | Application serveur principale (init, boucle, shutdown). |
| `src/masterd/session/SessionManager.h/.cpp` | Gestion des sessions actives (session_id ↔ account). |
| `src/shared/security/RateLimitAndBan.h/.cpp` | Rate limiting par IP + bannissements. |
| `src/masterd/shards/ShardRegistry.h/.cpp` | Registre des shards connectés au master. |
| `src/shared/db/ConnectionPool.h/.cpp` | Pool de connexions MySQL. |

---

## 6. Couche rendu Vulkan — fichiers clés

> Pour les modifications visuelles d'auth, seul `src/client/render/auth/` est pertinent.
> Le reste concerne le rendu 3D du jeu.

### Auth rendering (le plus utile pour toi)
| Fichier | Rôle |
|---|---|
| `src/client/render/AuthImGuiRenderer.h/.cpp` | Point d'entrée rendu auth ImGui. Dispatch vers les sous-renderers. |
| `src/client/render/AuthUiRenderer.h` | Interface abstraite du renderer (ancienne piste non-ImGui — toujours présente). |
| `src/client/render/auth/AuthUiRendererCore.cpp` | Cœur partagé du renderer auth (init, transitions de phase). |
| `src/client/render/AuthLogoPass.h/.cpp` | Passe Vulkan dédiée au logo « Lune Noire » de l'écran d'auth. |
| `src/client/render/AuthGlyphPass.h/.cpp` | Passe Vulkan pour le rendu typographique haut de gamme (glyphes Windlass/Morpheus) en complément d'ImGui. |
| `src/client/render/FontAtlasTtf.h/.cpp` | Construction d'un atlas TTF (Windlass.ttf) chargé dans ImGui — voir commit `00ad2b5` (2026-04-27). |
| `src/client/render/auth/AuthImGuiCommon.h/.cpp` | **Styles partagés : couleurs, polices, boutons, champs.** Modifier ici impacte tous les écrans. |
| `src/client/render/auth/screens/AuthImGuiLogin.cpp` | Rendu écran connexion. |
| `src/client/render/auth/screens/AuthImGuiRegister.cpp` | Rendu écran inscription. |
| `src/client/render/auth/screens/AuthImGuiVerifyEmail.cpp` | Rendu écran vérification email. |
| `src/client/render/auth/screens/AuthImGuiForgotPassword.cpp` | Rendu écran mot de passe oublié. |
| `src/client/render/auth/screens/AuthImGuiShardPick.cpp` | Rendu écran choix de shard. |
| `src/client/render/auth/screens/AuthImGuiCharacterCreate.cpp` | Rendu écran création personnage. |
| `src/client/render/auth/screens/AuthImGuiOptions.cpp` | Rendu écran options. |
| `src/client/render/auth/screens/AuthImGuiTerms.cpp` | Rendu écran CGU. |
| `src/client/render/auth/screens/AuthImGuiLanguageSelect.cpp` | Rendu écran sélection langue. |
| `src/client/render/auth/screens/AuthImGuiError.cpp` | Rendu écran erreur. |

### Passes Vulkan (rendu 3D jeu)
| Fichier | Rôle |
|---|---|
| `src/client/render/Camera.h/.cpp` | `Camera` (matrices view/proj), `FpsCameraController` (mode --editor), **`OrbitalCameraController`** (vue 3ᵉ personne post-EnterWorld : orbite arrière, clic droit pivote, molette zoom, WASD déplace cible, walk-bob locomotion). |
| `src/client/render/DeferredPipeline.h/.cpp` | Pipeline déferred principal (orchestration des passes). |
| `src/client/render/GeometryPass.h/.cpp` | GBuffer (albedo, normales, roughness…). |
| `src/client/render/LightingPass.h/.cpp` | Calcul éclairage PBR. |
| `src/client/render/BloomPass.h/.cpp` | Post-process bloom. |
| `src/client/render/TaaPass.h/.cpp` | Anti-aliasing temporel. |
| `src/client/render/TonemapPass.h/.cpp` | Tonemapping final. |
| `src/client/render/CascadedShadowMaps.h/.cpp` | Ombres en cascade. |
| `src/client/render/ParticleBillboardPass.h/.cpp` | Rendu particules. |
| `src/client/render/terrain/TerrainRenderer.h/.cpp` | Rendu terrain. |
| `src/client/render/vk/VkDeviceContext.h/.cpp` | Device Vulkan (GPU, queues, allocateur). |
| `src/client/render/vk/VkSwapchain.h/.cpp` | Swapchain (frames présentées à l'écran). |

---

## 7. Localisation

| Fichier | Rôle |
|---|---|
| `src/client/localization/LocalizationService.h/.cpp` | Charge les fichiers JSON de traduction, expose `Tr("clé")`. |
| `game/data/localization/fr/fr.json` | Traductions françaises. |
| `game/data/localization/en/en.json` | Traductions anglaises. |

Les clés utilisées dans les écrans auth commencent par `auth.`, `common.`, `language.`.

---

## 8. Configuration et build

| Fichier | Rôle |
|---|---|
| `CMakeLists.txt` | Config build racine (C++20, cibles, liens). |
| `CMakePresets.json` | Presets : `vs2022-x64` (Windows), `linux-x64` (serveur). |
| `vcpkg.json` | Dépendances vcpkg (Vulkan, ImGui, MySQL connector…). |
| `src/shared/core/Config.h/.cpp` | Lecture du fichier de config JSON au runtime (`GetInt`, `GetString`…). |
| `config.json` | Config runtime par défaut (logging, endpoints, timeouts). |
| `deploy/docker/config/master.config.json` | Config serveur master en production. |
| `deploy/docker/config/shard.config.json` | Config serveur shard en production. |

---

## 9. Base de données

| Dossier/Fichier | Rôle |
|---|---|
| `sql/schema.sql` | Schéma complet (référence). |
| `sql/migrations/000N_*.sql` | Migrations numérotées (0001 → 0031). Appliquées en ordre par lcdlln-master au démarrage. |
| `sql/migrations/0007_terms_cgu.sql` | Système CGU initial : `terms_editions`, `terms_localizations`, `account_terms_acceptances`. |
| `sql/migrations/0008_bug_reports_exploit.sql` | Table `bug_reports` initiale (catégorie, titre, corps, exploit accordé). |
| `sql/migrations/0017_game_servers.sql` | Registre des serveurs de jeu (`game_servers`) — le master s'y enregistre/désenregistre. |
| `sql/migrations/0018_player_trade_log.sql` | Audit des échanges joueur-à-joueur (anti-scam). |
| `sql/migrations/0019_player_professions.sql` | Tracking professions/skill levels par personnage (Linux/MySQL). |
| `sql/migrations/0022_character_stats.sql` | Table `character_stats` — `total_play_seconds` + `server_id` (FK → `game_servers`). |
| `sql/migrations/0023_accounts_profile.sql` | Profil joueur sur `accounts` : adresse complète, `email_pending` flow, `disabled_reason`, `role`, colonnes parental_*. Idempotent. |
| `sql/migrations/0023_accounts_profile_fields.sql` | Champs de profil ajoutés à `accounts` : `first_name`, `last_name`, `birth_date` (corrige les champs ignorés à l'inscription). |
| `sql/migrations/0024_characters_soft_delete.sql` | Colonnes `deleted_at` (soft delete) + `force_rename` sur `characters`. |
| `sql/migrations/0025_privacy_settings.sql` | Nouvelle table `account_privacy_settings` (visibilité profil : public/friends/none). FK INT UNSIGNED imprécise — corrigée par 0031. |
| `sql/migrations/0026_roadmap_items.sql` | Nouvelle table `roadmap_items` pour la roadmap publique gérée depuis le portail web. |
| `sql/migrations/0027_faq_items.sql` | Nouvelle table `faq_items` (FAQ portail web avec contrôle de publication). |
| `sql/migrations/0028_bug_reports_admin.sql` | Extension `bug_reports` : `admin_status`, `admin_comment`, `exploit_awarded`. |
| `sql/migrations/0029_terms_retired_reason.sql` | Colonne `retired_reason` sur `terms_editions` (raison de retrait d'une édition CGU). |
| `sql/migrations/0030_terms_editions_nullable_published_at.sql` | `terms_editions.published_at` passe en `TIMESTAMP NULL DEFAULT NULL` (les brouillons n'ont pas de date). |
| `sql/migrations/0031_privacy_settings_ensure.sql` | Recrée `account_privacy_settings` avec `BIGINT UNSIGNED` correct — idempotente (`IF NOT EXISTS`). |
| `sql/migrations/0032_characters_spawn_position.sql` | Colonnes `spawn_x/y/z/yaw_deg/pitch_deg` sur `characters` (spawn personnalisé par perso, persistance position de déconnexion). |
| `sql/migrations/0033_characters_race_class_strings.sql` | Colonnes `race_str` / `class_str` sur `characters` (identifiants chaîne stables remplaçant les FK numériques `race_id`/`class_id` côté UI). |
| `sql/migrations/0034_roadmap_items_v2.sql` | Roadmap publique v2 : ajoute 6 items (chat in-game, audio immersif, système personnages, reconnexion auto, guildes/amis, boutique). |
| `sql/migrations/0035_seed_servers_default.sql` | Seed `(id=1, name='main')` dans `servers` — débloque la FK `characters.server_id → servers.id` quand le master n'auto-register pas. |
| `sql/migrations/0036_seed_races_default.sql` | Seed `races` avec id=0 'default' + les 6 races jouables (humains/elfes/orcs/nains/demons/chevaliers_dragons). Utilise `NO_AUTO_VALUE_ON_ZERO` pour pouvoir insérer id=0 explicite sur AUTO_INCREMENT. |
| `sql/migrations/0037_roadmap_items_v3.sql` | Roadmap publique v3 : vue 3ème personne, menu pause in-game, sélection de race à la création, CGU. |
| `src/masterd/migrations/MigrationRunner.h/.cpp` | Applique les migrations au démarrage du serveur. |
| `src/shared/db/ConnectionPool.h/.cpp` | Pool de connexions MySQL réutilisables. |
| `src/shared/db/DbHelpers.h/.cpp` | Helpers requêtes SQL (bind params, lecture résultats). |

**Migrations notables sur `accounts` :**
| Migration | Colonnes ajoutées |
|---|---|
| 0006 | `email_locale`, `email_verified` |
| 0016 | `country_code`, `tag_id` |
| 0023 (`accounts_profile`) | adresse postale complète, `email_pending` + token, `disabled_reason`, `role`, colonnes contrôle parental |
| 0023 (`accounts_profile_fields`) | `first_name`, `last_name`, `birth_date` (fix : champs ignorés à l'inscription) |

---

## 10. Outils et CI

| Fichier | Rôle |
|---|---|
| `.github/workflows/build-windows.yml` | CI build Windows (MSVC). |
| `.github/workflows/build-linux.yml` | CI build Linux (GCC/Clang). |
| `.gitea/workflows/build-test-linux.yml` | Tests Linux sur Gitea. |
| `cmake/LCDLLNHelpers.cmake` | Helpers CMake réutilisables (`lcdlln_add_simple_test()`). Parité [cmangos-tbc/cmake/](https://github.com/cmangos/mangos-tbc/tree/master/cmake). |
| `cmake/README.md` | Doc des modules CMake (chargement, ajout d'un nouveau module). |
| `tools/hlod_builder/` | Génère les niveaux de détail (HLOD) pour les zones 3D. |
| `tools/zone_builder/` | Construit les packages de zones (chunks, GLTF → PAK). |
| `tools/load_tester/` | Simule des connexions massives pour tester la charge serveur. |
| `tools/migration_checksum/` | Vérifie l'intégrité des migrations SQL. |

**Modules CMake** : le `CMakeLists.txt` racine charge les helpers via :
```cmake
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(LCDLLNHelpers)
```
`lcdlln_add_simple_test(target_name srcs...)` est ainsi disponible partout (racine + sous-CMakeLists). Pour ajouter un helper : créer `cmake/<NomModule>.cmake` avec un `include_guard(GLOBAL)` puis l'inclure depuis le racine.

---

## 11. Tickets / Documentation des milestones

Le dossier `tickets/` contient 405 fichiers Markdown documentant chaque milestone :

- `M00` : Core (logging, mémoire, job system, platform, game loop)
- `M01` : Vulkan (instance, device, swapchain, shaders)
- `M02` : Frame graph, compilation, barriers, assets
- `M03` : Deferred rendering (GBuffer, lighting, matériaux, tonemap)
- `M04` : Shadows (CSM, shadow pass, PCF)
- `M05–M10` : Éclairage avancé, terrain, eau, météo
- `M11+` : Gameplay (skills, craft, guildes, combat, quêtes)
- `M20` : Authentification Argon2
- `M33` : Auth UI complète (inscription, reset pwd, vérif email)
- `M43` : Panneaux ImGui spécialisés (M43.4 = ImGui foundation, prérequis M100)
- `M44` : Infrastructure serveur (auth, persistence stores, packetlog, RBAC)
- `M100` : **Éditeur de monde 3D AAA + simulation environnementale gameplay** — 51 tickets organisés en 12 phases logiques (Fondations, Terrain, Terrain naturaliste, Splat/Surfaces/Collision, Hydrologie/Hazards, Placement/Végétation, Atmosphère, Saisons/Météo/Thermal, Routes/Ponts, Objets interactifs, Polissage, Volumes 3D, Accessibilité éditeur). Voir `tickets/M100/INDEX.md` pour la liste complète, les statuts (Done/Ready/Draft/Blocked), les contrats partagés et les gates de déploiement serveur (3 tiers).

Les plans d'implémentation récents sont dans `docs/superpowers/plans/`.

---

## 12. Web portal (Next.js)

**Design system :** Lune Noire (thème médiéval dark fantasy).
**Branche de référence :** `claude/update-web-portal-design-tcKMO`
**Polices :** Cinzel (≈ Windlass) + EB Garamond (≈ Morpheus) via Google Fonts.

### Fichiers de style

| Fichier | Rôle |
|---|---|
| `web-portal/app/globals.css` | **Source unique de vérité CSS.** Tokens `--ln-*`, overlays race, reset, toutes les classes `wp-*`, boutons `.btn`, formulaires `.field`. |
| `design/lune-noire-design-system/colors_and_type.css` | Référence officielle tokens couleurs + typo (Windlass/Morpheus). Ne pas modifier. |
| `design/lune-noire-design-system/ui_kits/web_portal/portal.css` | Source des classes `wp-*` (layout, cards, timeline, accordéon…). |

### Système de classes CSS (`wp-*`)

| Classe | Rôle |
|---|---|
| `wp-main` / `.narrow` / `.wide` | Conteneur de page (max-width 1100 / 700 / 1200 px). |
| `wp-page-header` | Titre + sous-titre de page avec séparateur bas. |
| `wp-hero` | Section héro centrée avec gradient de fond. |
| `wp-stats` / `wp-stat` / `wp-stat-value` / `wp-stat-label` | Grille de statistiques. |
| `wp-section-title` / `wp-section-sub` | Titres de section intermédiaires. |
| `wp-grid` / `wp-grid-2/3/4` | Grilles responsive auto-fit. |
| `wp-card` / `.interactive` | Carte fond semi-transparent ; `.interactive` ajoute hover doré. |
| `wp-badge` / `.done` / `.active` / `.planned` | Pastilles de statut (vert / or / gris). |
| `wp-alert` / `.info` / `.success` / `.warning` / `.error` | Bannières d'alerte colorées. |
| `wp-accordion` / `wp-acc-item` / `wp-acc-trigger` / `wp-acc-body` | FAQ accordéon. |
| `wp-timeline` / `wp-tl-item` / `.done` / `.active` | Timeline roadmap avec ligne verticale et point coloré (`::before`). |
| `wp-tiers` / `wp-tier` / `wp-tier-num` / `wp-tier-label` | Grille de paliers (exploits). |
| `wp-table-wrap` / `wp-table` | Tableau responsive avec header stylisé. |
| `wp-progress` / `wp-progress-fill` | Barre de progression. |
| `wp-divider` | Séparateur horizontal. |
| `wp-header` / `wp-logo` / `wp-nav` | Topbar sticky (voir `SiteHeader.tsx`). |
| `wp-footer` / `wp-footer-links` | Pied de page. |

### Boutons et formulaires

| Classe | Rôle |
|---|---|
| `btn btn-primary` | Bouton principal bleu dégradé. |
| `btn btn-ghost` | Bouton secondaire transparent bordure. |
| `btn btn-accent` | Bouton doré contour. |
| `btn btn-danger` | Bouton rouge destruction. |
| `field` | Wrapper label + input/textarea/select stylisé. |
| `form-stack` | Colonne de champs avec gap. |
| `error-box` / `success-box` | Boîtes de feedback formulaire (composants existants). |

### Overlays race

Appliquer `data-race="elfes|orcs|nains|morts_vivants|corrompus|divins|demons|humains"` sur un ancêtre pour rebrancher toutes les variables `--ln-*` automatiquement.

### Pages et composants

| Fichier | Rôle |
|---|---|
| `web-portal/app/layout.tsx` | Layout racine : `SiteHeader` + `<main>` + `wp-footer`. |
| `web-portal/components/SiteHeader.tsx` | **Server Component** — appelle `getSession()`, délègue nav interactive à `HeaderActions`. |
| `web-portal/components/HeaderActions.tsx` | **Client Component** — menu mobile, liens conditionnels (TAG-ID, Espace joueur, Admin, Déconnexion). |
| `web-portal/app/page.tsx` | Page d'accueil : hero, stats, grille fonctionnalités, accès rapide. |
| `web-portal/app/login/page.tsx` | Connexion : logo lune, `wp-card`, champs `.field`, `wp-alert error`. |
| `web-portal/app/roadmap/page.tsx` | Roadmap dynamique : lecture depuis `roadmap_items` DB, `wp-timeline`. |
| `web-portal/app/bugs/page.tsx` | Signalement bugs : affiche `BugReportForm` si authentifié, sinon message "Connexion requise". |
| `web-portal/app/support/page.tsx` | FAQ dynamique : lecture depuis `faq_items` DB (published=1), accordéon. |
| `web-portal/app/contact/page.tsx` | Contact : infos + formulaire dans `wp-grid-2`. |
| `web-portal/app/admin/page.tsx` | Hub admin : 6 modules (CGU, acceptations, joueurs, roadmap, FAQ, bugs). |
| `web-portal/app/admin/cgu/page.tsx` | CGU admin CRUD : draft/published/retired avec règles métier strictes. |
| `web-portal/app/admin/acceptances/page.tsx` | Suivi acceptations CGU (lecture seule). |
| `web-portal/app/admin/players/page.tsx` | Gestion joueurs : liste paginée, filtres, actions (email, statut, personnages). |
| `web-portal/app/admin/roadmap/page.tsx` | Roadmap admin CRUD : ajouter/modifier/supprimer items. |
| `web-portal/app/admin/faq/page.tsx` | FAQ admin CRUD : questions/réponses publiées ou archivées. |
| `web-portal/app/admin/bugs/page.tsx` | Suivi bugs : changement statut, commentaire admin, attribution exploits. |
| `web-portal/app/player/page.tsx` | Hub espace joueur : nav vers 5 sections + sections existantes. |
| `web-portal/app/player/account/page.tsx` | Détail du compte : profil, email (avec re-validation), adresse postale. |
| `web-portal/app/player/chronicles/page.tsx` | Mes Chroniques : temps de jeu par serveur, exploits, personnages + suppression. |
| `web-portal/app/player/parental/page.tsx` | Contrôle parental : validation tuteur légal pour joueurs mineurs. |
| `web-portal/app/player/security/page.tsx` | Sécurité : changement mot de passe, placeholder MFA. |
| `web-portal/app/player/privacy/page.tsx` | Vie privée : liste CGU (lire + accepter), visibilité du profil. |
| `web-portal/app/player/cgu/page.tsx` | Mes CGU : bannière statut, section "À accepter", historique complet des acceptations. |
| `web-portal/app/cgu/[id]/page.tsx` | Lecture publique d'une CGU publiée (FR/EN), langue via `?lang=` — `notFound()` si non publié. |
| `web-portal/app/player/exploits/page.tsx` | Exploits : délègue à `ExploitsProfile`. |
| `web-portal/app/player/recovery-profile/page.tsx` | Profil récupération : `wp-alert warning` si pas de compte. |
| `web-portal/app/password-recovery/page.tsx` | Récupération mot de passe : `wp-card` info. |
| `web-portal/components/ExploitsProfile.tsx` | Exploits : progress bar, cartes visibles/masquées, stats. |
| `web-portal/components/AccountForm.tsx` | Formulaire compte joueur (Client Component) — infos perso, email, adresse. |
| `web-portal/components/CharacterDeleteButton.tsx` | Suppression personnage en 2 confirmations (Client Component). |
| `web-portal/components/PasswordChangeForm.tsx` | Changement mot de passe (Client Component). |
| `web-portal/components/PrivacyForm.tsx` | Visibilité profil radio buttons (Client Component). |
| `web-portal/components/CguAcceptButton.tsx` | Bouton acceptation CGU (Client Component). |
| `web-portal/components/BugReportForm.tsx` | Formulaire signalement bug : catégorie, titre, corps — POST `/api/bugs` (Client Component). |
| `web-portal/components/admin/PlayerActions.tsx` | Actions joueur admin : email, statut, désactivation motif, personnages. |
| `web-portal/components/admin/CguManager.tsx` | Gestion CGU admin : create/edit/publish/retire (Client Component). |
| `web-portal/components/admin/FaqAdmin.tsx` | CRUD FAQ admin (Client Component). |
| `web-portal/components/admin/BugAdmin.tsx` | Gestion bugs admin : statut, commentaire, exploit award (Client Component). |
| `web-portal/middleware.ts` | Protection routes `/player/*` et `/admin/*` via cookies (Edge Runtime). |
| `web-portal/lib/session.ts` | `getSession()` — lit cookie `lcdlln_portal_account`, retourne `Session \| null`. |
| `web-portal/lib/email.ts` | Module email centralisé — 7 fonctions d'envoi, templates HTML Lune Noire. Lit la config SMTP depuis `config/smtp.local.json` (racine dépôt) en fallback si les variables d'environnement `SMTP_HOST` etc. sont absentes. |
| `web-portal/lib/db.ts` | Pool MySQL partagé, `query<T>()`. |
| `web-portal/lib/portalLogin.ts` | `verifyPortalCredentials()` — double Argon2id + legacy scrypt. |
| `web-portal/lib/gamePasswordHash.ts` | Hash/verify double Argon2id (`@node-rs/argon2`). |
| `web-portal/app/api/auth/login/route.ts` | POST login — set cookies `lcdlln_portal_account` + `lcdlln_portal_role`. |
| `web-portal/app/api/auth/logout/route.ts` | POST logout — supprime les deux cookies session. |
| `web-portal/app/api/player/` | APIs joueur : account PATCH, email change, password, parental, cgu accept, privacy, characters delete. |
| `web-portal/app/api/bugs/route.ts` | POST bug report — insère dans `bug_reports`, auth via cookie session, valide catégorie. |
| `web-portal/app/api/admin/` | APIs admin : players (verify-email, activate, disable), characters (force-rename), roadmap CRUD, faq CRUD, cgu CRUD+publish+retire, bugs PATCH. |
| `web-portal/email-templates/` | Templates HTML email FR/EN actifs lus par `web-portal/lib/email.ts` (welcome, verification, password-reset, account-confirmed, account-disabled, parental-validation, email-change — 14 fichiers). |
| `game/data/email/` | Mêmes templates HTML FR/EN consommés côté C++ par `SmtpMailer` (commit `d7b490b`). |
| `design/lune-noire-design-system/ui_kits/email/` | Source design d'origine — référence visuelle, pas chargée à l'exécution. |

---

## Configuration SMTP (envoi d'e-mail)

`web-portal/lib/email.ts` supporte deux modes de configuration, par ordre de priorité :

### Mode 1 — Variables d'environnement (production recommandée)
| Variable | Description |
|----------|-------------|
| `SMTP_HOST` | Hôte SMTP (ex. `10.0.4.52`) |
| `SMTP_PORT` | Port (défaut : `587`) |
| `SMTP_SECURE` | `"true"` pour SSL/TLS port 465, sinon laisser vide (STARTTLS sur 587) |
| `SMTP_USER` | Identifiant de connexion SMTP |
| `SMTP_PASS` | Mot de passe SMTP |
| `SMTP_FROM` | Adresse expéditeur (ex. `"Lune Noire" <noreply@lune-noire.fr>`) |

### Mode 2 — Fichier local (développement / serveur sans gestionnaire d'env)
Créer `config/smtp.local.json` **dans le dossier `config/`** à la racine du dépôt.
Ce fichier est ignoré par git (`.gitignore`). Un exemple est disponible dans `smtp.local.json.example`.

```json
{
  "smtp": {
    "host": "10.0.4.52",
    "port": 587,
    "user": "user@domain.fr",
    "password": "mot-de-passe",
    "from": "user@domain.fr",
    "starttls": 1
  }
}
```

> **Priorité** : les variables d'environnement priment toujours sur `smtp.local.json`.
> Si `SMTP_HOST` est défini en env, le fichier JSON n'est pas lu du tout.

---

## 13. Tweaks et badges éphémères de l'écran de connexion

### 13.1 Panneau « Tweaks » (bas-droite)

Source unique : `AuthImGuiRenderer::DrawAuthTweaksPanel` dans `src/client/render/AuthImGuiRenderer.cpp`.
État maintenu par l'instance du renderer (`m_langTweakRace`, `m_langTweakAnimBg`).
La police du panneau est volontairement plus petite que celle du cadre principal
(`SetWindowFontScale(0.85f)`), et la sélection courante d'une race ou du toggle
ACTIVE / DESACTIVE est signalée à la fois par la **bordure** et la **couleur du texte**
en accent (`LnTheme::kAccent`).

Le titre « TWEAKS » et son bouton de réduction (- / +) ont été retirés en itération 3 :
le panneau est désormais toujours affiché expansé, sans header.

**Hauteur calibrée serrée** (itération 5) : `winH = 160 px` (auparavant 218) pour que le
contenu — « THEME DE RACE » + grille 3×3 + « FOND ANIME » + ACTIVE/DESACTIVE à
`SetWindowFontScale(0.85f)` ≈ 152 px — touche naturellement le bas du cadre. Aucun Dummy
au-dessus du premier label : la position est dictée uniquement par `WindowPadding(12, 12)`.
**Ancrage bas-droite** : `pos = (vpW - winW - 22, vpH - winH - 10)` pour conserver la même
marge externe (10 px du bord bas, 22 px du bord droit) quelle que soit la résolution.
`m_authTweakPanelMinimized` reste comme placeholder mais n'est plus relu — gardé pour
réintroduire la fonctionnalité minimize sans réécrire la struct si besoin.

### 13.2 Toggle « FOND ANIME » → animation décorative du fond auth

`m_langTweakAnimBg` (`bool`) est la source de vérité du toggle ACTIVE / DESACTIVE.
Contrat futur : quand la passe Vulkan d'animation de fond auth sera ajoutée (probablement à
côté de `src/client/render/AuthLogoPass` / `src/client/render/AuthGlyphPass`), elle devra observer ce
flag et activer/désactiver son émission de commandes en conséquence. Tant que la passe
n'existe pas, le toggle ne fait que conserver l'état visuellement (sélection de bordure +
texte) — voir le commentaire `// Le toggle ACTIVE / DESACTIVE pilote le futur fond animé`
dans `DrawAuthTweaksPanel`.

### 13.3 Badge éphémère « Langue : … »

Lors de la transition `LanguageSelectionFirstRun → Login`, le renderer capture
`rm.infoBanner` (posé par `AuthUiPresenter::ApplyLocaleSelection`) puis l'affiche au-dessus
du cadre central pendant `kLoginLangBadgeDurationSec` secondes (4 s par défaut, dont
`kLoginLangBadgeFadeOutSec` = 1 s de fade-out final).

**Suppression permanente de la double affichage** : tant que `rm.infoBanner` est égal à
`m_loginLangBadgeText` (le texte capturé), le panneau de connexion n'affiche **plus jamais**
ce bandeau à l'intérieur — pas seulement pendant la fenêtre éphémère. Cette persistance évite
que le bandeau « saute » dans le cadre après le fade-out (effet visuel signalé en revue UX).
À l'expiration du timer, `m_loginLangBadgeStartTime` repasse à `-1.0` (plus de fenêtre
flottante), mais `m_loginLangBadgeText` reste mémorisé jusqu'au prochain `Reset()` ou jusqu'à
ce qu'une nouvelle transition LangSel → Login le remplace.

État : `m_loginLangBadgeText`, `m_loginLangBadgeStartTime`, `m_prevPhaseToken` dans
`AuthImGuiRenderer.h`.
Détection : `SyncTransientFromModel` compare `m_prevPhaseToken` au nouveau token (bit 0 =
language sel, bit 1 = login, bit 31 = active) avec un masque `0x7FFFFFFFu` pour ignorer le
bit "active". Rendu : `DrawLoginLanguageBadge(vpW, vpH)`.

### 13.4 Titre login agrandi + marge supérieure

`RenderLoginScreen` applique `SetWindowFontScale(5.0f)` au titre principal (Windlass 13 px →
≈ 65 px), précédé d'un `SetCursorPosY(max(24, vpH * 0.05f))` pour ajouter une bande d'air
au-dessus. Objectif maquette : que le titre remplisse plus de la moitié de l'espace vide
entre le bord supérieur de l'écran et le panneau (qui démarre à `vpH * 0.28f`).

**Stage englobante élargie** : à 5.0x, « LES CHRONIQUES » mesure ~720 px et était clipée
quand le BeginChild faisait 570 px de large. La stage est désormais à `vpW * 0.96f` pour
englober titre + sous-titre + cadre central sans clipping. Le cadre central (panneau de
connexion) reste fixé à 580 px et **doit être centré dans la stage**, pas dans le viewport :
on appelle donc `BeginPanel(stageW=580, titleZoneW, vpH, ...)` avec `titleZoneW` (la largeur
réelle du child englobant) comme 2e argument, sinon `panelX = (vpW - vpW) / 2 = 0` aligne
le panneau contre le bord gauche du child (bug observé en itération 3 → corrigé en it. 4).

**Sous-titre `de la Lune Noire`** : `SetWindowFontScale(2.5f)` (auparavant 1.5f) précédé
d'un `Dummy(0, 8)` pour le descendre légèrement sous la baseline du titre principal.

### 13.6 Aération formulaire login (itération 6 + 7)

`DrawAuthGoldField` accepte maintenant `extraSpacingPx` (défaut 0 → comportement Register
inchangé). Le login passe 6 px → un `Dummy` est inséré entre le libellé en majuscules
accent et le `InputText`. Entre les deux champs, un `Dummy(0, 12)` au call site sépare
« IDENTIFIANT » et « MOT DE PASSE ».

**Hauteur des champs (it. 7)** : `DrawAuthGoldField` pousse également
`FramePadding(10, 8)` autour de l'`InputText` (auparavant le défaut ImGui de
`(4, 3)`). Hauteur effective des champs ≈ 29 px (au lieu de 19 px par défaut).

**Boutons descendus (it. 7)** : au-dessus des liens secondaires (Récupération du mot de
passe, Portail web) : `Dummy(0, 32)` (auparavant 18). Au-dessus des actions principales
(Créer un compte, Se connecter) : `Dummy(0, 28)` (auparavant 14).

**Gap CONNEXION→IDENTIFIANT (it. 7)** : retrait du `ImGui::Spacing()` après le `Separator`
dans `BeginPanel` → gap visuel ≈ 9 px (ItemSpacing.y * 2 + Separator) avec le trait
naturellement centré. Affecte tous les écrans qui utilisent `BeginPanel`.

### 13.5 Limitations Windlass.ttf (police principale)

`game/data/fonts/Windlass.ttf` ne contient pas tous les glyphes Latin-1 supplément :
- Majuscules accentuées : É (0xC9), À (0xC0), Ô (0xD4)…
- Minuscules accentuées spécifiques : ç (0xE7) a notamment été observé manquant.

ImGui les rend en `?` (ou un placeholder visuel). Tant qu'aucune fallback font n'est
fusionnée dans l'atlas (cf. `WorldEditorImGui::Init` — la solution propre serait
`AddFontFromMemoryTTF` en `MergeMode = true` avec une police qui couvre 0x0080-0x00FF), les
libellés français de l'UI auth doivent rester en ASCII pur. Liste des clés concernées :
- `auth.login.maquette_create` (« CREER UN COMPTE »)
- `auth.login.remember_detail` (« CONSERVE L'IDENTIFIANT A LA PROCHAINE OUVERTURE »)
- `language.native_line.fr`, `language.name.fr` (« Francais » au lieu de « Français »)
- `language.apply_success` (« Langue appliquee immediatement : … »)

---

## 14. Vue 3ème personne (post-EnterWorld) — chantier 2026-05-01

Ajouté sur la PR #419. La caméra in-game (post-clic « Jouer » dans CharacterSelect)
n'est plus FPS mais **orbitale 3ᵉ personne** autour d'une position cible représentant le joueur.

### Fichiers clés
| Fichier | Rôle |
|---|---|
| `src/client/render/Camera.h/.cpp` | `OrbitalCameraController` : membre `m_target` (position cible), `m_distance` (zoom), `LocomotionState` (Idle/Walk/Run), `m_walkBobPhase` (phase oscillation). Méthode `Update` lit input et écrit `camera.position` + `camera.yaw/pitch`. Gère collision caméra-sol. |
| `src/client/app/Engine.h` | Membre `m_orbitalCameraController` + `m_lastSyncedPosition` (étape 6). |
| `src/client/app/Engine.cpp` (branche `!m_editorMode` post-EnterWorld) | Appelle `m_orbitalCameraController.Update`, calcule `out.objectModelMatrix = T(target) × R_y(yaw) × bobY`, déclenche `SavePositionAsync` adaptatif au mouvement. |
| `game/data/meshes/avatar_placeholder.mesh` | Cube 0.5×1.8×0.5 m — **fallback** si chargement du modèle skinné échoue. L'avatar par défaut est désormais `game/data/models/avatars/y_bot/y_bot.glb` (cf. §14.5). |

### Contrôles
- **Souris libre** par défaut (curseur cliquable sur UI).
- **Clic droit maintenu** → rotation yaw/pitch en orbite.
- **Molette** → zoom in/out (clamp [1.5 ; 20] m, défaut 6 m).
- **WASD/ZQSD** → déplace la cible ; la caméra suit.
- **Shift** → courir (et accélérer le walk-bob).

### Limites assumées (à enrichir dans des PR ultérieurs)
- Avatar = humanoïde Y Bot Mixamo animé en boucle Walking permanente (sous-projet A, cf. §14.5). Texture diffuse, variantes raciales et state machine sont les sous-projets C et B respectivement.
- Pas d'orientation différenciée selon direction de mouvement (perso suit la caméra).
- Walk-bob = oscillation Y synthétique de `OrbitalCameraController` toujours appliquée au modelMatrix, **en plus** de la vraie animation squelettique du clip Y Bot. Légèrement redondant (~4-7 cm d'amplitude superposée à la marche). Cleanup planifié en sous-projet B (state machine locomotion).
- Sol supposé plat à Y=0 (pas de raycast contre la heightmap terrain).
- Synchro position via `CHARACTER_SAVE_POSITION_REQUEST` (TCP master, ~1 s en mouvement) — pas un vrai protocole UDP gameplay temps-réel.

---

## 14.5 Runtime skinning + animation (sous-projet A — chantier 2026-05-18)

Premier sous-projet de la décomposition A→K (animation + races + animaux). Remplace
le cube `avatar_placeholder.mesh` par un humanoïde Mixamo skinné qui joue une animation
en boucle permanente, dans **le client de jeu ET l'éditeur monde** (init placé dans
la branche boot commune aux deux binaires).

Voir [`docs/superpowers/specs/2026-05-18-skinning-animation-foundations-design.md`](docs/superpowers/specs/2026-05-18-skinning-animation-foundations-design.md)
pour le contexte complet et [`docs/superpowers/plans/2026-05-18-skinning-animation-foundations.md`](docs/superpowers/plans/2026-05-18-skinning-animation-foundations.md)
pour le plan d'implémentation détaillé.

### Format runtime : glTF 2.0 binaire (.glb)

Asset Mixamo téléchargé en FBX Binary → converti par `tools/asset_pipeline/fbx_to_gltf.ps1`
(qui appelle `FBX2glTF.exe`, fork Godot v0.13.0, gitignored sous `tools/asset_pipeline/bin/`)
→ `.glb` final dans `game/data/models/<category>/<entity>/<entity>.glb`.

Parsing au runtime via **cgltf** (single-header MIT, vendored dans `external/cgltf/cgltf.h`, v1.14).

### Fichiers clés

| Fichier | Rôle |
|---|---|
| `external/cgltf/cgltf.h` | Parser glTF/glb single-header MIT (v1.14 pinné). |
| `src/shared/math/Quat.h/.cpp` | Type quaternion + slerp (arc court + nlerp fallback dot>0.9995). |
| `src/client/render/skinned/Skeleton.h/.cpp` | Bones (nom + parent index + bindLocal + inverseBindGlobal). Invariant : parent index < self index. |
| `src/client/render/skinned/AnimationClip.h/.cpp` | Keyframes T/R/S par bone + interp (lerp Vec3 / slerp Quat) + clamp out-of-range + fallback empty. |
| `src/client/render/skinned/AnimationSampler.h/.cpp` | `SamplePose(t)` → `ComputeGlobalMatrices` → `ComputeFinalMatrices` (palette G×IB). |
| `src/client/render/skinned/SkinnedMesh.h/.cpp` | VkBuffers vertex/index host-visible + clips + skeleton + `FindClip(name)` + `Destroy`. |
| `src/client/render/skinned/SkinnedMeshLoader.h/.cpp` | `LoadCpuOnlyForTests(path)` (cgltf → CPU data) + `Load(device, phys, path)` (CPU + GPU upload). |
| `src/client/render/skinned/SkinnedRenderer.h/.cpp` | Pipeline Vulkan dédié skinné (init + record). Renderpass adapté de `m_renderPassLoad` du GeometryPass (draw au-dessus du G-buffer terrain). |
| `game/data/shaders/skinned_gbuffer.vert` | Applique skinning par matrices d'os ; outputs identiques à `gbuffer_geometry.vert` → réutilise `gbuffer_geometry.frag` tel quel. |
| `game/data/models/avatars/y_bot/y_bot.glb` | Premier humanoïde de référence (Mixamo Y Bot, mannequin gris, ~65 bones + clip "mixamo.com" walking 36 frames). |
| `game/data/models/avatars/y_bot_idle/y_bot_idle.glb` | Mixamo Y Bot + clip "Standing Idle" baked (~2 MB). |
| `game/data/models/avatars/y_bot_start_walking/y_bot_start_walking.glb` | Mixamo Y Bot + clip "Start Walking" baked (~2 MB). |
| `tools/asset_pipeline/download_fbx2gltf.ps1` | Télécharge `FBX2glTF.exe` v0.13.0 (Godot fork) avec vérif SHA256. Binaire gitignored. |
| `tools/asset_pipeline/fbx_to_gltf.ps1` | Wrapper convertit FBX inbox → glTF binaire game/data/models/. Param `-SourceFbx` optionnel. |
| `tools/asset_pipeline/README.md` | Procédure utilisateur Mixamo → drop → conversion. |
| `src/client/render/skinned/tests/*.cpp` | 5 tests unitaires (Quat, Skeleton, AnimationClip, AnimationSampler, SkinnedMeshLoader vs y_bot.glb). Linux-only (`lcdlln_add_simple_test` UNIX-gated). |

### Intégration dans `Engine.cpp` (Task 15 du plan, commit `b4f7418`)

L'init du `SkinnedRenderer` et le load de `y_bot.glb` sont **placés dans la branche boot
de `Engine::Init` (autour de la ligne 3738)**, donc s'exécutent dans les deux binaires
(`lcdlln.exe` et `lcdlln_world_editor.exe`) — Task 16 du plan satisfaite sans patch séparé.

Le draw par frame :
1. `AnimationSampler::SamplePose(skeleton, clip, t)` avec `t = elapsed % clip.duration`
2. `ComputeGlobalMatrices` (walk hiérarchie parent-before-child)
3. `ComputeFinalMatrices` (G × inverseBindGlobal → palette envoyée au shader)
4. `SkinnedRenderer::Record` (upload SSBO bone matrices + model matrix + bind + drawIndexed)

**Fallback préservé** : si `y_bot.glb` absent / SPV manquant / init Vulkan échoue, le cube
`avatar_placeholder.mesh` reste affiché avec un log warning. Pas de crash.

### State machine de locomotion (3 états — sortie partielle du sous-projet B)

L'utilisateur a demandé après validation de A un comportement réaliste : Y Bot
en pose Idle quand immobile, transition "Start Walking" quand il commence à
marcher, puis cycle "Standard Walk" en boucle. Implémenté **en minimal** au
sein de A (le sous-projet B propre apportera blend / crossfade / surface
modulation / saut).

État courant : `Engine::AvatarLocomotionState { Idle, StartWalking, Walking }`
(`Engine.h`).

Transitions (évaluées par frame dans le lambda FrameGraph "Geometry") :

| Depuis | Vers | Condition |
|---|---|---|
| Idle | StartWalking | `movingNow == true` (delta XZ > 1e-4 m sur la frame) |
| StartWalking | Walking | `stateElapsed >= StartWalking.duration` |
| StartWalking | Idle | `movingNow == false` (interruption en cours de transition) |
| Walking | Idle | `movingNow == false` |

Sélection de clip : `m_playerSkinnedMesh->FindClip(name)` où name vaut
`"Idle"`, `"StartWalking"`, ou `"Walking"`. Les 3 clips sont chargés depuis
3 .glb séparés et fusionnés au boot via `SkinnedMeshLoader::LoadClipsRetargeted`
(retarget par nom de bone — robuste aux différences d'ordre des joints).

Temps dans le clip : `fmod(stateElapsed, clip.duration)` pour les clips loop
(Idle, Walking), `min(stateElapsed, clip.duration)` pour les clips one-shot
(StartWalking).

**Hard cuts** entre clips — la pose snap d'une frame à l'autre, pas de blend.
Acceptable comme placeholder ; le crossfade propre est full sous-projet B.

Détection de mouvement : delta XZ du modelMatrix entre 2 frames consécutives,
threshold 1e-4 m (équivalent ~6 mm/s à 60 FPS). **Fragile** : sous-projet B
branchera ça sur un vrai signal input/gameplay au lieu du delta de position.

### Orientation 180° de l'avatar

Mixamo Y Bot a son "forward" en +Z par défaut. La convention caméra
3ᵉ personne de LCDLLN attend que l'avatar fasse FACE à l'opposé de la
caméra (on voit son dos). Fix : rotation 180° autour de Y appliquée à la
model matrix juste avant `SkinnedRenderer::Record`. Code :
`finalModelMat = cameraModel * Quat::FromAxisAngle({0,1,0}, π).ToMat4()`.

### Convention winding (anti-régression critique)

Le pipeline `SkinnedRenderer` utilise **`frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE`**
parce que la spec glTF 2.0 mandate CCW pour les faces front. C'est **différent du
`GeometryPass` actuel** qui est CW (correct pour le cube `avatar_placeholder.mesh` qui est
CW dans son fichier). Cf. CLAUDE.md section « Convention winding / face culling » :
chaque pipeline a sa propre convention selon la source de son mesh. **Ne PAS aligner
les frontFace entre pipelines** — vérifier le winding réel du mesh concerné.

### Push constants / SSBO / vertex layout (cohérence shader ↔ pipeline)

- **Push constants** (144 bytes, stages VS+FS) : `prevViewProj (mat4) + viewProj (mat4)
  + materialIndex (uint) + 3 uint padding`. Identique à `gbuffer_geometry.vert`.
- **Set 0** : material descriptor (réutilisé depuis `m_pipeline->GetMaterialDescriptorCache()`).
- **Set 1 binding 0** : `BonesSSBO { mat4 bones[] }` (host-visible coherent, max 256 bones,
  réécrit chaque frame par `Record`).
- **Vertex binding 0** (stride 56) : `pos@0 (vec3) + normal@1 (vec3) + uv@2 (vec2)
  + boneIdx@7 (uvec4 R16G16B16A16_UINT) + weights@8 (vec4)`.
- **Vertex binding 1** (stride 64, rate INSTANCE) : `instanceRow0..3 @ locations 3..6`
  → forme la `mat4` du modelMatrix (column-major).

### Limites assumées de A (statut mis à jour après B.1)

- ~~State machine minimale 3 états + hard cuts + delta XZ + Run absent~~ → **résolu par B.1** (cf. §14.6) : 7 états avec crossfade 0.15s, driven par CharacterController + input réel.
- ~~Le bobY synthétique de OrbitalCameraController reste appliqué EN PLUS de l'animation~~ → **résolu par B.1** (refacto OrbitalCameraController en caméra pure, suppression du bobY).
- ~~Sol supposé plat à Y=0~~ → **partiellement résolu par B.1** (TerrainCollider via heightmap query bilinéaire, le perso suit le relief).
- Pas de variantes raciales — Y Bot affiché pour TOUS les personnages quelle que soit
  la race choisie à la création (→ sous-projet C, non démarré).
- Pas de remote players visibles animés — seul l'avatar local est skinné (→ sous-projet B.3,
  redéploiement serveur requis pour UDP gameplay).
- Pas de texture diffuse — couleur unie via `avatar_skin` placeholder.
- Pas de skinned shadow casting — shadow map utilise toujours le cube, donc l'ombre
  au sol reste cubique (→ futur, hors A et B).
- Pas de surface modulation (water/sand/snow) → walk speed × modifier (→ sous-projet B.2,
  dépend de M100.11 SurfaceQuery).
- Tests `quat_tests / skeleton_tests / animation_clip_tests / animation_sampler_tests
  / skinned_mesh_loader_tests` enregistrés via `lcdlln_add_simple_test`, **Linux-only**
  (gated `elseif(UNIX)` dans `src/CMakeLists.txt` — convention du repo, pas spécifique
  à ce sous-projet).
- FIF race connue : bone SSBO et model instance buffer sont host-coherent rewrites
  par frame, pas dupliqués par frame in-flight (acceptable single-avatar ; à fixer
  si remote players ajoutent du multi-avatar — sous-projet B.3).

### Plan complet et état d'avancement

Cf. [`docs/superpowers/plans/2026-05-18-skinning-animation-foundations.md`](docs/superpowers/plans/2026-05-18-skinning-animation-foundations.md)
pour la décomposition en 17 tâches. Toutes complètes au moment du merge de la PR
« sous-projet A — fondations skinning + runtime animation ».

---

## 14.6 Locomotion state machine (sous-projet B.1 — chantier 2026-05-18)

Post-A : décomposition supplémentaire du sous-projet B (state machine de locomotion
complète) en 3 morceaux indépendants pour cadrer l'effort :

- **B.1** (ce chantier, en cours / mergeable) : locomotion fluide LOCALE — vraie
  détection input/gameplay, crossfade, Run distinct, Jump+Fall+Land, sans surface
  modulation ni remote players.
- **B.2** (à venir) : surface modulation (water/sand/snow → walk speed × modifier).
  Dépend de M100.11 (`SurfaceQuery::At(x, z)`), non encore livré.
- **B.3** (à venir, **redéploiement serveur requis**) : remote players animés —
  protocol UDP gameplay + GameplayUdpServer + sync animation state réseau.

Voir [`docs/superpowers/specs/2026-05-18-locomotion-state-machine-design.md`](docs/superpowers/specs/2026-05-18-locomotion-state-machine-design.md)
pour le contexte et [`docs/superpowers/plans/2026-05-18-locomotion-state-machine.md`](docs/superpowers/plans/2026-05-18-locomotion-state-machine.md)
pour le plan d'implémentation en 14 tâches.

### Découverte clé exploitée par B.1

`src/client/gameplay/CharacterController.h/.cpp` **existait déjà** dans le repo
(167 lignes, production-ready : `Config { walkSpeed, runSpeed, gravity, jumpSpeed,
coyoteTimeSec, jumpBufferSec, maxSlopeDeg, ... }`, modes Ground/Air/Water/Fly,
capsule sweep collision via interface `IWorldCollider`, walkable slope + step-up).
**N'était branché nulle part.** B.1 le branche enfin via `TerrainCollider` (nouveau).

### Fichiers clés (nouveaux et modifiés par B.1)

| Fichier | Rôle |
|---|---|
| `src/shared/math/Math.h` | Ajouté : `Mat4::Identity()`, `Mat4::Translate(Vec3)`, `Mat4::RotateY(rad)` (statiques inline). Évite la duplication de la composition `T(pos) * R_y(yaw)` à chaque call site. |
| `src/client/render/skinned/AnimationCrossfade.h/.cpp` | Blend TRS lerp/slerp entre 2 poses sur 0.15s. Démarré quand `Play(newClip)` est appelé alors qu'une autre clip est en cours. Recompose via `AnimationSampler::ComposeTRS`. |
| `src/client/gameplay/TerrainCollider.h/.cpp` | Impl `IWorldCollider` via `TerrainRenderer::SampleHeightAtWorldXZ` (bilinéaire). `SweepCapsule` = raycast vertical approximé. `QueryWater` hérite de la default (toujours false — B.2/B.3 surchargera). |
| `src/client/render/skinned/AnimationSampler.h/.cpp` | Modifié : `ComposeTRS` passé de l'anonymous namespace à static public — réutilisé par AnimationCrossfade. |
| `src/client/render/skinned/SkinnedMeshLoader.h/.cpp` | Modifié : ajouté `LoadClipsAnimOnly(path, targetSkeleton)` pour fichiers Mixamo "without skin" (~50 KB au lieu de 2 MB). `LoadClipsRetargeted` bail si `skins_count == 0` — le nouveau helper parse les channels directement et retargete par `target_node->name`. |
| `src/client/render/Camera.h/.cpp` | Refacto majeur : `OrbitalCameraController` rétrogradé en **caméra pure** (~211 lignes → ~58 lignes pour `Update`). Suppression des membres mouvement (`m_locomotion`, `m_walkBobPhase`, `m_verticalVelocityY`, `m_verticalOffsetY`, `m_isCrouching`), de l'enum `LocomotionState`, du paramétrage `applyKeyboardMove/groundYAtTarget/speedMultiplier`. Garde uniquement souris (yaw/pitch clic droit), molette (zoom), calcul `camera.position = m_target + offset_orbital(yaw,pitch,distance)`. Nouveaux getters `GetForwardXZ()`, `GetRightXZ()`, `GetYawRad()` pour projeter input clavier dans repère caméra. |
| `src/client/app/Engine.h` | Étendu : enum `AvatarLocomotionState { Idle, StartWalking, Walk, Run, Jump, Fall, Land }` (7 états, renomme `Walking` → `Walk`). L'enum est passée en `public:` pour que les helpers free-function `StateToClipName` / `ClipLoops` de `Engine.cpp` puissent y accéder. Nouveaux membres : `m_characterController`, `m_terrainCollider`, `m_avatarYaw`, `m_lastMoveInput`, `m_avatarCrossfade`. |
| `src/client/app/Engine.cpp` | Init au boot : instancie `TerrainCollider.BindTerrain(&m_terrain)` + `CharacterController(ccCfg).Init(spawnPos)`. Per-frame `Update` : `BuildMoveInput(m_input, m_orbitalCameraController)` → `cc.Update(dt, input, m_terrainCollider)` → `m_orbitalCameraController.SetTargetPosition(cc.GetPosition())` → state machine étendue → `m_avatarCrossfade.Play(*newClip, loops, nowSec)` si state change. Lambda Geometry simplifié : juste `m_avatarCrossfade.Sample` → `ComputeGlobalMatrices` → `ComputeFinalMatrices` → `Record` avec modelMatrix = `T(feetPos) * R_y(m_avatarYaw + π)`. Suppression définitive du bobY synthétique. |
| `config.json` | Nouvelle section `player.movement` : `walk_speed`, `run_speed`, `gravity`, `jump_speed`, `coyote_time_s`, `jump_buffer_s`. Defaults dans `CharacterController::Config` font le boulot si absent. |
| `game/data/models/avatars/y_bot_run/y_bot_run.glb` | Mixamo `running.fbx` (animation-only, ~43 KB) → renommé `Run` au load via `LoadClipsAnimOnly`. |
| `game/data/models/avatars/y_bot_jump/y_bot_jump.glb` | Mixamo `Jump.fbx` (with-skin, ~2 MB, 65 frames) → renommé `Jump`. |
| `game/data/models/avatars/y_bot_fall/y_bot_fall.glb` | Mixamo `falling idle.fbx` (animation-only, ~43 KB, 21 frames) → renommé `Fall`. |
| `game/data/models/avatars/y_bot_land/y_bot_land.glb` | Mixamo `hard landing.fbx` (animation-only, ~70 KB, 60 frames) → renommé `Land`. |
| `src/shared/math/tests/Mat4HelpersTests.cpp` | 5 tests : identity, translate, rotateY(π), rotateY(π/2), T*R compose. |
| `src/client/render/skinned/tests/AnimationCrossfadeTests.cpp` | 5 tests : pas de blend, alpha=0/1/0.5, one-shot clamp. |
| `src/client/gameplay/tests/TerrainColliderTests.cpp` | 4 tests : no-bound fallback Y=0, sweep descending hit, ascending no-hit, both-above no-hit. |

### State machine 7 états (remplace celle minimale de §14.5)

Enum dans `Engine.h` (`public:`) : `Idle, StartWalking, Walk, Run, Jump, Fall, Land`.

Évaluée par frame dans `Engine::Update` (déménagement depuis le lambda Geometry d'A,
qui ne voyait que le delta XZ). Driven par `CharacterController::IsGrounded()`,
`input.jumpPressed`, `input.run` et `moveInput.moveDirXZ.LengthSq()`.

#### Transitions

| Depuis | Vers | Condition |
|---|---|---|
| Idle | StartWalking | `moving == true` (cf. note `moving`) |
| Idle | Jump | `input.jumpPressed && grounded` |
| StartWalking | Walk | `stateElapsed >= clip.duration` ET `!input.run` |
| StartWalking | Run | `stateElapsed >= clip.duration` ET `input.run` |
| StartWalking | Idle | `!moving` (interruption en cours de transition) |
| StartWalking | Jump | `input.jumpPressed && grounded` |
| Walk | Run | `input.run == true` (Shift maintenu) |
| Walk | Idle | `!moving` |
| Walk | Jump | `input.jumpPressed && grounded` |
| Run | Walk | `input.run == false` (Shift relâché) |
| Run | Idle | `!moving` |
| Run | Jump | `input.jumpPressed && grounded` |
| Jump | Fall | `stateElapsed >= clip.duration * 0.4f` (= takeoff terminé, ~0.4s) |
| Fall | Land | `cc.IsGrounded()` redevient true (touch ground) |
| Land | Idle / Walk / Run | `stateElapsed >= clip.duration`, choix selon input courant |

Note `moving` : `moveInput.moveDirXZ.x != 0 || moveInput.moveDirXZ.z != 0` (vraie
détection input, plus le delta XZ fragile d'A).

#### Sélection de clip + crossfade

À chaque changement d'état (`newState != m_avatarLocoState`) :
1. `m_avatarLocoState = newState`
2. `m_avatarLocoStateEnterTime = now`
3. `clipName = StateToClipName(newState)` (helper anon-ns d'`Engine.cpp`)
4. `m_avatarCrossfade.Play(*m_playerSkinnedMesh->FindClip(clipName), loops, nowSec)`

`loops` = true pour Idle / Walk / Run / Fall, false pour StartWalking / Jump / Land
(one-shot).

Le lambda FrameGraph "Geometry" est désormais minimal :
```
nowSec = steady_clock_now_in_seconds
locals = m_avatarCrossfade.Sample(skel, nowSec)   // gère blend si crossfade en cours
globals = ComputeGlobalMatrices(skel, locals)
finals = ComputeFinalMatrices(skel, globals)
modelMat = Mat4::Translate(feetPos) * Mat4::RotateY(m_avatarYaw + π)
m_skinnedRenderer.Record(..., finals, modelMat.m, ...)
```

### Architecture mouvement (refacto OrbitalCameraController)

**Avant B.1** (A) : `OrbitalCameraController` était caméra ET mouvement. Lisait
WASD, calculait `m_target`, gérait bobY synthétique, simulait gravité verticale.

**Après B.1** : séparation des responsabilités.

| Composant | Responsabilité |
|---|---|
| `OrbitalCameraController` | **Caméra pure** : yaw/pitch (souris clic droit) + zoom (molette) + suit `m_target` posé de l'extérieur via `SetTargetPosition(Vec3)`. |
| `CharacterController` (existant, jamais branché jusqu'ici) | **Mouvement physique** : reçoit `MoveInput` par frame, applique gravité/jump, sweep capsule contre `IWorldCollider`, expose `GetPosition() / GetVelocity() / IsGrounded()`. |
| `TerrainCollider` (nouveau B.1) | **Collision terrain** : impl `IWorldCollider::SweepCapsule` via heightmap query bilinéaire (`TerrainRenderer::SampleHeightAtWorldXZ`). MVP : pas de collision contre props/buildings (rien dans le monde encore). |
| `Engine::Update` | **Orchestrateur** : `BuildMoveInput(input, camera)` → `cc.Update(dt, input, collider)` → `camera.SetTargetPosition(cc.GetPosition())` → state machine → `crossfade.Play` si état change. |

### Input → MoveInput pipeline

`BuildMoveInput` (anonymous ns d'`Engine.cpp`) projette WASD/ZQSD dans le repère
caméra via les nouveaux getters `OrbitalCameraController::GetForwardXZ() /
GetRightXZ()`. Yaw du modèle = `atan2(moveDirXZ.x, moveDirXZ.z)` (perso pivote
pour faire face à la direction de mouvement — convention « free-look 3ᵉ personne
classique » Diablo-like, pas de strafe distinct).

```
W / Z      → forward
S          → backward
A / Q      → -right
D          → +right
Shift      → input.run = true
Space      → input.jumpPressed = true (edge: WasPressed)
```

### Asset pipeline (étendu de A)

`SkinnedMeshLoader::LoadClipsAnimOnly(path, targetSkeleton)` ajouté pour les
fichiers Mixamo exportés "without skin" (animation-only, ~50 KB au lieu de 2 MB).
Parse les `cgltf_animation` channels directement, retargete par
`target_node->name` contre les noms de bones du target skeleton. Bones absents
du source = skip silencieux.

Réutilisable pour tous les futurs imports animation-only (sous-projets E/F/G
auront beaucoup de clips Mixamo).

### Limites assumées de B.1 (à enrichir B.2/B.3+)

- **Hard cut sur transition stop** : quand le perso passe Walk→Idle ou Run→Idle,
  le crossfade fonctionne (0.15s vers Idle), mais ce n'est pas un clip "stop"
  dédié. Mixamo a `run to stop.fbx` dans l'inbox, non utilisé en B.1 (option
  "Riche : 9 états" écartée au brainstorm). À ajouter dans une extension B
  ultérieure si visuellement gênant.
- **Pas de turn-in-place** : `left turn.fbx` / `right turn.fbx` dans l'inbox,
  non utilisés. Le perso tourne en pivotant le model matrix sur `m_avatarYaw`
  (snap immédiat). À ajouter en B ultérieure si demandé.
- **Pas de saut en longueur distinct** du saut vertical. `Running Jump.fbx`
  dans l'inbox, non utilisé.
- **Pas de strafe / walk backward distincts** : le perso pivote toujours pour
  faire face à la direction de mouvement (convention free-look).
- **First-frame** : à la première frame post-EnterWorld, `m_avatarYaw = 0` →
  `R_y(0 + π) = R_y(π)` → perso affiché de face une frame avant que la première
  touche d'input ne snap le yaw. Cosmétique mineur, snap au premier input.
- **Threshold mouvement = `moveInput.moveDirXZ != 0`** : strict (toute pression
  WASD compte). Plus robuste que le delta XZ d'A mais ne gère pas le cas
  "input pressé mais perso bloqué par mur" (pas d'obstacles dans le monde
  actuellement, problème théorique).
- **Pas de collision contre props/buildings** : MVP TerrainCollider seul. Quand
  le système d'objets dans le monde sera ajouté, il faudra un `WorldCollider`
  composite (TerrainCollider + PropsCollider + ...).
- **FIF race héritée d'A** : bone SSBO + model instance buffer host-coherent
  rewrites par frame. Single avatar OK, à fixer pour B.3 multi-avatar.

### Plan complet et état d'avancement

Cf. [`docs/superpowers/plans/2026-05-18-locomotion-state-machine.md`](docs/superpowers/plans/2026-05-18-locomotion-state-machine.md)
pour la décomposition en 14 tâches. Toutes complètes au moment du merge de la
PR « sous-projet B.1 — locomotion state machine fluide locale ».

### Suite (post-B.1)

- **B.2** (surface modulation) — débloqué dès que M100.11 (SurfaceQuery) est livré.
- **B.3** (remote players animés) — indépendant, peut démarrer dès B.1 mergé.
  **Redéploiement serveur requis** (UDP gameplay protocol + GameplayUdpServer +
  bump `kProtocolVersion`).
- **C** (variantes raciales) — débloqué par A, indépendant de B. Peut être
  traité en parallèle de B.2/B.3.

---

## 15. Menu pause in-game

Touche **Échap** post-EnterWorld toggle un menu ImGui centré au-dessus du monde.

### Fichiers clés
| Fichier | Rôle |
|---|---|
| `src/client/app/Engine.h` | Membre `m_inGamePauseMenuVisible` + méthodes `ToggleInGamePauseMenu()`, `RequestLogoutToLoginScreen()`. |
| `src/client/app/Engine.cpp` (branche Échap in-game) | Toggle au lieu de `OnQuit()` ; rendu ImGui inline dans la branche du chat HUD (même `ImGui::Render` final). |
| `src/client/auth/AuthUi.h` + `AuthUiPresenterCore.cpp` | Méthode publique `RequestReturnToLogin()` : reset `m_flowComplete=false`, repasse en `Phase::Login`. |

### Actions
- **Reprendre** → ferme le menu.
- **Options** → ouvre un mini-panel options in-game (volume général, plein écran, vsync, sensibilité souris). Membre `m_inGameOptionsPanelVisible`. Le full panel auth Options reste accessible via `Se déconnecter` → écran Login → Options.
- **Se déconnecter** → coupe gameplay UDP, reset auth presenter, ré-affiche écran de connexion.
- **Quitter le jeu** → `OnQuit()` (comportement original).

### Distinction chat post-auth vs post-shard
- `AuthUiPresenter::IsMasterAuthenticated()` (vrai dès AUTH OK : `m_masterSessionId != 0`) gate l'apparition du chat HUD. Chat visible donc dès Login OK.
- `AuthUiPresenter::IsInWorldShard()` (alias `IsFlowComplete`) passé à `ChatImGuiRenderer::Render(..., bool inWorldShard)` : si `false` (post-auth pas in-world), seuls **Global + Friends** sont exposés ; si `true` (post-EnterWorld), ajout de **Zone**.

---

## 16. Sélection de race à la création de personnage

L'écran CharacterCreate expose désormais un combo des 6 races jouables.

### Fichiers clés
| Fichier | Rôle |
|---|---|
| `src/client/render/auth/screens/AuthImGuiCharacterCreate.cpp` | Combo `kRaceIds[]` / `kRaceLabels[]` ; passe l'id string au submit. |
| `src/client/render/AuthImGuiRenderer.h` | Membre `m_charRaceIdx`. |
| `src/client/auth/AuthUi.h` + `AuthScreenCharacterCreate.cpp` | Méthode `ImGuiSubmitCharacterCreate(cfg, name, raceId)` ; membre `m_characterRaceId` ; passé à `BuildCharacterCreateRequestPayload(name, raceId)`. |
| `sql/migrations/0036_seed_races_default.sql` | Seed `races` avec les 6 races jouables (humains, elfes, orcs, nains, demons, chevaliers_dragons) + id=0 'default' (compat code legacy). |
| `src/masterd/handlers/character/CharacterCreateHandler.cpp` | Persiste `parsed->raceId` dans `characters.race_str` (colonne 0033). |
| `src/client/render/auth/screens/AuthImGuiCharacterSelect.cpp` | Affichage post-création : symbole d'identification race (initiale ASCII en accent) + libellé localisé via `auth.character_select.race.<id>`. |

### Mapping race id ↔ libellé FR
| `race_str` (DB) | Libellé UI (FR) | Symbole |
|---|---|---|
| `humains` | Humain | H |
| `elfes` | Elfe | E |
| `orcs` | Orc | O |
| `nains` | Nain | N |
| `demons` | Demon | D |
| `chevaliers_dragons` | Chevalier-dragon | C |

---

## 17. Éditeur monde — création/chargement de carte (chantier 2026-05-04)

Périmètre : `lcdlln_world_editor.exe` uniquement. Aucun impact client jeu /
serveur.

### Carte par défaut

`engine::editor::WorldEditorSession::ActionNewMap` produit, sous
`paths.content` + `world_editor/maps/<zoneId>/` :
- `height.r16h` : heightmap plate (valeur 32768 = `0.5 * height_scale`).
- `splat.slap` : 100 % couche herbe (1024×1024).
- `grass.grms` : masque herbe à zéros.
- `map.lcdlln_edit.json` : `splatLayerTextureRefs[0..3]` toutes vides
  (aucune texture utilisateur assignée).

La propriété "refs vides après ActionNewMap" est gardée par le test
`world_editor_session_tests` (`src/world_editor/tests/WorldEditorSessionTests.cpp`).

### Reset caméra automatique

`Engine::RebuildWorldEditorTerrainGpu()` (src/client/app/Engine.cpp) repositionne la
caméra à chaque création/chargement de carte sous garde `m_worldEditorExe` :
- Position : centre XZ du terrain, altitude `height_scale * 0.5 + 80 m`.
- Pitch : `0.35 rad` (~20° vers le bas) ; yaw 0.
- `farZ = max(5000, terrainWorldSize * 1.5)` pour les grands terrains.

### Fallback orange (terrain sans texture utilisateur)

Tant qu'aucune couche splat n'a reçu de mapping texture, le shader terrain
écrit une couleur orange uni dans l'albedo (les normales et l'ORM restent
inchangées, le shading lambert reste lisible).

- Détection (CPU) : dans `RebuildWorldEditorTerrainGpu`, on vérifie que
  toutes les entrées de `Doc().splatLayerTextureRefs` sont vides ; sinon le
  flag retombe à `false`. Garde stricte `m_worldEditorExe` : le client jeu
  ne lève **jamais** ce flag.
- Propagation : `TerrainRenderer::SetNoUserTexturesFallback(bool)` →
  membre `m_noUserTextures` → push-constant `int noUserTextures`.
- Shader : branche `if (pc.noUserTextures != 0) outAlbedo = vec4(1.0, 0.55,
  0.1, 1.0)` dans `game/data/shaders/terrain.frag`. La struct
  `PushConstants` côté CPU et le `layout(push_constant)` côté `terrain.vert`
  / `terrain.frag` ont été étendus en lockstep (20 octets, bien sous la
  limite Vulkan de 128).

Bascule automatique : dès qu'une texture est posée sur une couche, le
prochain rebuild via le bouton « Recharger terrain GPU » repasse en rendu
normal sans intervention.

### Cause racine "terrain invisible" — fix permanent (PR24, 2026-05-05)

Le commit `ee181da` ("Reapply view matrix transposee") a changé la convention
de `Camera::ComputeViewMatrix` vers Vulkan LH +Z forward. Conséquence :
les meshes générés en CCW world-space apparaissent en **CW** dans le clip
space. Tous les pipelines configurés `frontFace=CCW + cullMode=BACK_BIT`
rejetaient silencieusement leurs triangles (terrain, avatar, etc.) malgré
un frustum cull CPU qui passait.

Pipelines fixés par PR24 puis PR25 (2026-05-06) :
| Pipeline | Fichier | Ligne du fix | Symptôme avant fix |
|---|---|---|---|
| Terrain principal | `src/client/render/terrain/TerrainRenderer.cpp` | 528 | Sol invisible (viewport gris-beige uniforme = sky tonemappée) |
| Terrain falaises | `src/client/render/terrain/TerrainRenderer.cpp` | 1193 | Pas de cliffs sur la map de test, fix préventif |
| Geometry (avatar / props) | `src/client/render/GeometryPass.cpp` | 376 | Humanoïde invisible (mode éditeur ET client de jeu post-EnterWorld) |

Si après une régression future un mesh world-space disparaît, vérifier en
priorité `frontFace` du pipeline concerné. Tous les fix utilisent
`VK_FRONT_FACE_CLOCKWISE` (au lieu de `_COUNTER_CLOCKWISE`).
`ShadowMapPass`, `DecalPass` utilisent encore CCW —
applicable seulement si on observe des ombres / decals invisibles.

### Finalisation éditeur (PR25, 2026-05-06)

Plusieurs ajustements demandés par l'utilisateur après validation PR24 :

| Item | Fichier | Modif |
|---|---|---|
| Caméra : monter/descendre | `src/client/render/Camera.cpp:117-145` | `FpsCameraController::Update` lit **R** (Y+=) et **F** (Y-=) en mode éditeur |
| Grille : maille 5 m | `src/world_editor/WorldEditorSession.h:171` | `m_gridCellMeters` défaut 8 → 5 |
| Avatar humanoïde visible en éditeur | `src/client/app/Engine.cpp:3525-3548` | Déjà en place (avant PR25). Le fix `GeometryPass.cpp:376` (frontFace=CW) le rend effectivement visible. |
| Grille collée au sol | `src/world_editor/WorldEditorImGui.cpp:269-306` | Déjà en place : chaque ligne échantillonne `TerrainWorldY` aux extrémités via la heightmap. **Note** : le rendu utilise `ImGui::GetForegroundDrawList()` (overlay 2D sans depth test contre le 3D), donc visuellement la grille est toujours dessinée par-dessus le sol même quand elle est mathématiquement au même niveau Y. Pour avoir un depth test correct il faudrait un line mesh 3D dédié (refactor non fait). |
| **Fix Y-flip grille** (test PR25.5) | `src/world_editor/WorldEditorImGui.cpp:199` | `WorldToScreen` faisait un flip Y convention OpenGL (`sy = (1.0 - (ndcY * 0.5 + 0.5)) * vh`) alors que le rendu 3D utilise Vulkan (NDC.y +1 = bas écran). Conséquence : la grille était rendue avec Y inversé par rapport au sol/ciel ; sur le test diag PR25.5 (sol forcé ROUGE, ciel forcé BLEU) l'utilisateur observait **deux horizons distincts** — transition rouge/bleu en haut + point de fuite de la grille en bas. Fix : retirer le `1.0f -`. Affecte aussi le brush preview (cercle orange) qui partage `WorldToScreen`. |

Items 6 (sculpt terrain) et 7 (paint splat) restent en investigation — voir PR27 (PR26 étant les fixes préventifs frontFace shadow/water/decal).

---

## 18. Structure `src/shardd/` — serveur de gameplay (Linux)

Le shard est le binaire qui exécute le **gameplay temps réel** (combat, mouvement, spawns, IA, sorts). Communique avec le master via TCP shard tickets (`ShardToMasterClient`) pour la persistance et l'auth.

### Arborescence shard

| Dossier | Rôle |
|---------|------|
| `src/shardd/ai/` | EventAI DSL DB-driven, `EventAI.h/cpp`, `EventAIRuntime`, `MotionGeneratorStack`. |
| `src/shardd/anticheat/` | `AntiCheatGameplay` + Runtime — validators position/vitesse server-side (Wave 9 #577). |
| `src/shardd/app/` | Point d'entrée binaire shard, init engine. |
| `src/shardd/arena/` | Arena runtime côté shard (proposition match, écho score). Master gère le team/MMR persistant. |
| `src/shardd/auction/` | Hooks shard pour création/expiration listings (master gère le tick global). |
| `src/shardd/battleground/` | BG runtime (score, objectifs in-game). Master gère queue + history. |
| `src/shardd/cinematics/` | `CinematicSequence` validation client-side parsing + anticheat skip detection. |
| `src/shardd/combat/` | `ThreatList` + Runtime, calcul threat, current target. **Manque** : `HostileRefManager` bidirectionnel (Wave 19 roadmap). |
| `src/shardd/dbscripts/` | `DBScript.h/cpp` + Runtime — DSL data-driven 30 commandes (Say/Yell/Move/Spawn/etc.) hot-reload via `/reload`. |
| `src/shardd/entities/` | Base `Object.h/cpp`, `ObjectGuid.h`, `UpdateField.h`, `UpdateMask.h` (Wave 7 #575). **Manque** : Unit/Player/WorldObject/Creature (Wave 17 roadmap). |
| `src/shardd/gameplay/` | Sous-domaines runtime : `auction/`, `crafting/`, `gathering/`, `guild/`, `social/`, `quest/`, `spawner/`, `event/`, `character/`, `economy/`, `trade/`, `chat/`. Chaque sous-dossier expose un Runtime + Store interface. |
| `src/shardd/guild/` | Hooks shard pour broadcast guild chat (master = persistance). |
| `src/shardd/internals/` | Modules infrastructure : `globals/` (ConditionMgr, GraveyardManager, LocaleStrings, ObjectAccessor), `vmap/` (VMapManager + LOS + Streamer), `entities/` (ObjectGuid extension). |
| `src/shardd/loot/` | `LootTable.h`, loot generation runtime. **Manque** : ReferenceLootResolver, LootRule plug. |
| `src/shardd/maps/` | `InstanceManager` + Runtime. **Manque** : sous-classes WorldMap/DungeonMap/BattlegroundMap (Wave 21 roadmap). |
| `src/shardd/net/` | UDP gameplay (`GameplayUdpServer`, ClientSession). |
| `src/shardd/outdoorpvp/` | OutdoorPvP runtime côté shard. |
| `src/shardd/playerbot/` | Playerbot infra (load testing). |
| `src/shardd/pools/` | `PoolManager` weighted spawns + Runtime. **Manque** : nested + persistence (Wave 20 roadmap). |
| `src/shardd/skills/` | Skill snapshot + discovery hooks. |
| `src/shardd/spell/` | `SpellFamilyMask` + Runtime (Wave 9 #577). **Manque** : SpellMgr/Spell/Aura/ProcMgr (Wave 23 roadmap). |
| `src/shardd/trade/` | Trade FSM 2-phase commit. |
| `src/shardd/weather/` | Weather Markov chain runtime. |
| `src/shardd/world/` | `SpatialPartition`, `GridState`, `LunarCalendar`, `LagCompensation`, `TickScheduler`, `UdpTransport`, `ZoneTransitions`. **Manque** : GridVisitor + GridNotifier (Wave 18 roadmap). |

### Point d'entrée shard

`src/shardd/main_linux.cpp` (Linux) / `src/shardd/main_win.cpp` (Windows dev). Init ordonnée des runtimes :
1. ConnectionPool, MigrationRunner.
2. EventAI + PoolManager (Wave 6 #573).
3. ThreatList + DBScripts (Wave 8 #576).
4. AntiCheat + SpellFamily + InstanceManager (Wave 9 #577).
5. CinematicStore wiring (Wave 11 #580).
6. NetServer + PacketLog (Waves 12-16 #581-#589).

---

## 19. Admin / RBAC system (Waves 1-3, #568-#570)

### Hiérarchie de rôles

Source de vérité : `src/masterd/account/AccountRole.h` (CMANGOS.06 Phase 1c).

| Valeur DB | Constante C++ | Description |
|-----------|---------------|-------------|
| `player` | `AccountRole::Player` | Joueur standard. |
| `moderator` | `AccountRole::Moderator` | Modérateur chat, ne peut pas kick/ban. |
| `game_master` | `AccountRole::GameMaster` | GM avec accès aux commandes in-game (/kick, /mute, /ban, /announce). |
| `administrator` | `AccountRole::Administrator` | Administrateur full access (/promote, /reload, /packetlog, etc.). |

Sentinel runtime : `AccountRole::Console` (pas persisté DB) — pour commandes admin émises depuis la console serveur.

### Helpers AccountValidation

- `HasLowerSecurity(actor, target)` : actor a-t-il strictement moins de privilège que target ? Empêche un GM de promouvoir un admin.
- `RequireMinRole(actor, minRole)` : actor a-t-il au moins `minRole` ? Utilisé par chaque command dispatch.

### AccountRoleService

`src/masterd/account/AccountRoleService.{h,cpp}` — façade au-dessus d'`AccountStore` qui :
- Expose `HasLowerSecurity` + `RequireMinRole`.
- Câble l'audit via `SecurityAuditLog` (toute opération RBAC est loggée).

### AdminCommandHandler

`src/masterd/admin/` — handler central pour les commandes admin reçues via chat (`/promote`, `/kick`, `/mute`, `/ban`, `/announce`, `/who`, `/report`, `/reload`, `/packetlog`, `/sky time`, `/sky info`, `/sky moon`, `/loot`).

Chaque command dispatch :
1. Vérifie le rôle via `RequireMinRole`.
2. Loggue l'invocation (`SecurityAuditLog`).
3. Exécute l'action.

### Slash commands registry

- Source : `game/data/config/slash_commands.json` — liste exhaustive des commandes + role minimal requis.
- Doc humaine : `docs/slash_commands_rbac.md`.
- Code : `src/masterd/admin/SlashCommandRegistry.{h,cpp}`.

### Référence mémoire

- `feedback_admin_commands_logging.md` — toute commande admin passe par master, exige rôle admin, est loggée serveur.
- `reference_slash_commands.md` — fichier source + doc RBAC.

---

## 20. Persistence stores (Waves 5, 10, 11)

### Pattern Store

Chaque domaine persistant expose une **interface abstraite** + **implémentation MySQL** + **implémentation InMemory** (pour tests). Naming convention : `XxxStore.h` (interface), `MysqlXxxStore.{h,cpp}` (impl prod), `InMemoryXxxStore.{h,cpp}` (impl tests).

### Stores actuels

| Domaine | Interface | Impl MySQL | Wave PR |
|---------|-----------|------------|---------|
| Auction | `src/masterd/auction/AuctionStore.h` | `MysqlAuctionStore` | Wave 5 #572 |
| Arena | `src/masterd/arena/ArenaStore.h` | `MysqlArenaStore` | Wave 5 #572 |
| GameEvents | `src/masterd/events/GameEventStore.h` | `MysqlGameEventStore` | Wave 5 #572 |
| Skills | `src/masterd/skills/SkillStore.h` | `MysqlSkillStore` | Wave 5 #572 |
| OutdoorPvp | `src/masterd/outdoorpvp/OutdoorPvpStore.h` | `MysqlOutdoorPvpStore` | Wave 5 #572 |
| Guild | `src/masterd/guild/GuildStore.h` | `MysqlGuildStore` | Wave 5 phase 2 #574 |
| BattleGround | `src/masterd/battleground/BattlegroundStore.h` | `MysqlBattlegroundStore` | Wave 5 phase 2 #574 |
| Loot | `src/masterd/loot/LootStore.h` | `MysqlLootStore` | Wave 5 phase 2 #574 |
| Mail | `src/masterd/mail/MailStore.h` | `MysqlMailStore` + `IsAvailable` helper | Wave 10 #579 |
| Cinematic | `src/masterd/cinematics/CinematicStore.h` | `MysqlCinematicStore` | Wave 11 #580 |
| Quests | `src/masterd/quests/QuestStateStore.h` | `MysqlQuestStateStore` | - |
| Reputation | `src/masterd/reputation/ReputationStore.h` | `MysqlReputationStore` | - |

### Fallback InMemory

Si la connexion MySQL n'est pas dispo au boot ou pour les tests :
- `MysqlMailStore::IsAvailable()` retourne false → bascule sur `InMemoryMailStore`.
- Tests unitaires utilisent toujours `InMemory*Store` (pas de dépendance MySQL).

### Couches DB partagées

`src/shared/db/` :
- `ConnectionPool` (existe depuis le début)
- `SQLStorage<T>` (Wave 18 — cache RAM read-only)
- `SqlDelayThread` (Wave 18 — async writes)
- `SqlPreparedStatement` (Wave 18 — cache prepared)
- `DbHelpers` (bind params, lecture résultats)

Migrations associées : `0041_phase_1a_test_storage.sql`, `0058_cinematic_seen.sql`, etc.

---

## 21. PacketLog (Waves 12-16, #581-#589)

### Vue d'ensemble

Système de trace réseau RX/TX pour debug des anomalies protocole. Activable on-demand par les admins.

### Composants

| Composant | Fichier | Rôle |
|-----------|---------|------|
| Ring buffer | `src/shared/network/PacketLog.{h,cpp}` | Buffer circulaire en RAM, capture les N derniers paquets RX+TX. |
| RX/TX wiring | `src/shared/network/NetServer.cpp` | Hooks d'écriture dans le ring buffer à chaque paquet reçu/envoyé. |
| Dump on close | `src/shared/network/NetServer.cpp` | À la fermeture d'une connexion anormale, dump du ring buffer dans un fichier. |
| Admin command | `src/masterd/admin/PacketLogCommand.{h,cpp}` | `/packetlog [dump|on|off|status]` — RBAC Administrator + audit log. |

### Migration progressive

- Wave 12 (#581) : ring buffer skeleton.
- Wave 14 (#587) : wire RX/TX paths.
- Wave 15 (#588) : dump on close pour anomalies protocole.
- Wave 16 (#589) : `/packetlog` admin command (on-demand dump).

### Format du dump

Texte structuré : `[timestamp] [direction] [opcode] [request_id] [session_id] [payload_size] [payload_hex_truncated]`.

---

## 22. Entities foundation (Wave 7 + Wave 17)

### Hiérarchie complète

```
Object (Wave 7 #575, base)
  └── WorldObject (Wave 17 — position 3D + mapId/zoneId + IsInWorld)
        └── Unit (Wave 17 — HP/MP/level/faction + IsAlive)
              ├── Player (Wave 17 — accountId/characterId/name/xp)
              └── Creature (Wave 17 — templateEntry/spawnId)
```

### Fichiers livrés

| Fichier | Wave | Rôle |
|---------|------|------|
| `src/shardd/entities/Object.{h,cpp}` | 7 | Base abstraite : `ObjectGuid m_guid`, UpdateMask, OnReplicationSent. |
| `src/shardd/entities/ObjectGuid.h` | 7 | GUID 64 bits : `[type 8 bits | id 56 bits]`. Constexpr helpers. |
| `src/shardd/entities/UpdateField.h` | 7 | Wrapper `UpdateField<T>` avec auto-flag dirty sur `Set()`. |
| `src/shardd/entities/UpdateMask.h` | 7 | Bitmask delta replication. SetBit/TestBit/PopCount O(1). |
| `src/shardd/entities/UpdateFieldIndices.h` | 17 | Enums stables `ObjectFieldIdx`, `WorldObjectFieldIdx`, `UnitFieldIdx`, `PlayerFieldIdx`, `CreatureFieldIdx`. Indices stables (wire format en dépend). |
| `src/shardd/entities/WorldObject.{h,cpp}` | 17 | Object + position 3D + mapId/zoneId + AddToWorld/RemoveFromWorld stubs. |
| `src/shardd/entities/Unit.{h,cpp}` | 17 | WorldObject + HP/MaxHP/MP/MaxMP/level/faction + clamp + IsAlive. |
| `src/shardd/entities/Player.{h,cpp}` | 17 | Unit + accountId/characterId (MarkDirty au ctor) + name (immutable) + xp. |
| `src/shardd/entities/Creature.{h,cpp}` | 17 | Unit + templateEntry/spawnId (MarkDirty au ctor, immutables). |

### Tests CTest

| Cible | Couverture |
|-------|------------|
| `entities_foundation_tests` (Wave 7) | ObjectGuid encoding, UpdateMask bit ops, UpdateField auto-flag, Object dirty tracking. |
| `worldobject_tests` (Wave 17) | Construction, SetPosition, AddToWorld/RemoveFromWorld, MapId/ZoneId, idempotence. |
| `unit_tests` (Wave 17) | HP/MaxHP/MP/level/faction, clamp overflow, IsAlive toggle, OnReplicationSent préserve les valeurs. |
| `player_tests` (Wave 17) | accountId/characterId MarkDirty au ctor, name immutable, XP, héritage Unit + WorldObject. |
| `creature_tests` (Wave 17) | templateEntry/spawnId MarkDirty au ctor, héritage Unit, death state. |
| `update_mask_delta_tests` (Wave 17) | Delta cross-classe : SetHealth sur Unit → seul kUnitFieldHealth dirty, etc. |

### Prochaines étapes (post-Wave 17)

- **Wave 18** (GridVisitor / GridNotifier) : `WorldObject::AddToWorld()` insérera dans `SpatialPartition` au lieu du simple flag actuel.
- **Wave 23** (Spells) : `Unit` recevra `m_auras` (composition) pour persistance buffs/debuffs.
- **Wave réseau** (numéro à déterminer) : opcode `UPDATE_OBJECT` push qui sérialise `UpdateMask` + valeurs dirty, en s'appuyant sur les indices stables de `UpdateFieldIndices.h`.

Voir [`docs/superpowers/specs/2026-05-11-waves-17-38-server-foundations-design.md`](docs/superpowers/specs/2026-05-11-waves-17-38-server-foundations-design.md) §4 Wave 17 pour le spec + [`docs/superpowers/plans/2026-05-11-wave-17-entities-suite.md`](docs/superpowers/plans/2026-05-11-wave-17-entities-suite.md) pour le plan d'implémentation TDD step-by-step.

---

## 23. Boot wiring du shard (`src/shardd/main_linux.cpp`)

Ordre d'initialisation des runtimes (au boot, après ConnectionPool + MigrationRunner) :

```
Phase 1 — Fondations
  ConnectionPool (master + read replica)
  MigrationRunner.ApplyAll() — migrations 0001..0058
  SqlStorage<T>.LoadAll() — quest_template, item_template, etc.
  ConditionMgr.Reload() — depuis SqlStorage
  GraveyardManager.Reload()
  LocaleStrings.Reload()

Phase 2 — Modules gameplay (Wave 6 #573)
  EventAIRuntime (registries + EventAI DSL)
  PoolManagerRuntime (pool_template + pool state)

Phase 3 — Modules avancés (Wave 8 #576)
  ThreatListRuntime
  DBScriptRuntime (hot-reloadable via /reload dbscripts)

Phase 4 — Modules tertiaires (Wave 9 #577)
  AntiCheatGameplayRuntime
  SpellFamilyRuntime
  InstanceManagerRuntime

Phase 5 — Persistance secondaire (Wave 11 #580)
  CinematicStore wiring

Phase 6 — Réseau (Waves 12-16)
  PacketLog ring buffer init
  NetServer.Listen() (UDP gameplay + TCP shard ticket)
  PacketLog wire dans RX/TX paths
```

Ordre **non-négociable** : Phase N+1 ne peut démarrer qu'après Phase N OK (asserts au boot).

### Hot-reload commands

Les modules suivants sont hot-reloadables (admin GM+, audit logged) :
- `/reload conditions` → `ConditionMgr.Reload()`
- `/reload dbscripts` → `DBScriptRuntime.Reload()`
- `/reload eventai` → `EventAIRuntime.Reload()` (Wave 34 roadmap pour la commande, runtime déjà capable)
- `/reload graveyards` → `GraveyardManager.Reload()`
- `/reload locales` → `LocaleStrings.Reload()`

### Shutdown ordering

Inverse du boot : NetServer.Stop() → Phase 5 down → ... → Phase 1 (ConnectionPool drain).

---

## Aide-mémoire : comment trouver un écran

1. **Je veux changer le visuel d'un écran** → `src/client/render/auth/screens/AuthImGuiXxx.cpp`
2. **Je veux changer la logique / les données affichées** → `src/client/auth/screens/AuthScreenXxx.cpp`
3. **Je veux changer un style global (couleur, police, bouton)** → `src/client/render/auth/AuthImGuiCommon.h/.cpp`
4. **Je veux changer un message / traduction** → `game/data/localization/fr/fr.json`
5. **Je veux changer le comportement de la caméra in-game** → `src/client/render/Camera.{h,cpp}` (`OrbitalCameraController`)
6. **Je veux changer le menu pause** → `src/client/app/Engine.cpp` (branche Échap + rendu inline du panel)
7. **Je veux ajouter / modifier la roadmap publique** → `sql/migrations/00NN_roadmap_items_*.sql` (incrémenter le numéro)
5. **Je veux changer la logique réseau d'un écran** → `StartXxxWorker()` dans le fichier presenter + payload dans `src/shared/network/AuthRegisterPayloads.h`
6. **Je veux changer ce que le serveur fait à la réception** → `src/masterd/handlers/XxxHandler.cpp`
7. **Je veux changer l'ordre d'enchaînement des écrans d'auth** → `PollAsyncResult()` et `SubmitCurrentPhase()` dans `src/client/auth/AuthUiPresenterCore.cpp`. Pour l'auto/forcé du choix de shard côté flux : `src/shared/network/MasterShardClientFlow.cpp` (variable `m_shardPickWhenMultiple`).

### Limitations connues (à compléter par fonctionnalités futures)

- Pas encore de requête « liste des personnages d'un compte sur un shard ». Après `ShardPick`, le client ne sait pas si le compte a déjà des personnages ; le drapeau `m_postRegistrationCharacterCreatePending` est utilisé en proxy pour décider `ShardPick → CharacterCreate` vs `ShardPick → MasterFlow`. La sélection « jouer avec un personnage existant ou en créer un autre (max 5/shard) » n'a pas encore de protocole ni d'écran dédié.
- Le drapeau `m_postRegistrationCharacterCreatePending` est en mémoire processus ; si l'utilisateur s'inscrit puis ferme l'application avant la création de personnage, il devra (au prochain lancement) s'authentifier puis le serveur devra fournir l'information « pas de personnage sur ce shard » pour rejouer la création (non implémenté).

---

## 24. Races multi-mesh (sous-projet C MVP — 2026-05-20)

**Objectif** : afficher un mesh skinned différent par race de personnage (humain / nain / orc en MVP). Pas de différenciation gameplay (les `racials` de `races.json` restent du texte affiché — cf. §16 pour la sélection UI). Pas de redéploiement serveur (`race_str` déjà en DB depuis migration 0033). 5 autres races (elfes / morts_vivants / corrompus / divins / démons) renvoyées à C.2 plus tard.

### Composants

| Composant | Rôle |
|---|---|
| `engine::client::RaceDefinition::meshPath` | Champ ajouté à la struct (M39.1 originale). Chemin relatif vers le `.glb` par race, vide pour les races hors-MVP. Parsé depuis `races.json` par `CharacterCreationPresenter::LoadRaces`. |
| `engine::Engine::m_raceMeshes` | `unordered_map<string, SkinnedMesh>` peuplée au boot par un `CharacterCreationPresenter` local. Stockage stable (pointeurs sûrs après remplissage, pas de modification après boot). |
| `engine::Engine::GetRaceMesh(raceId)` | Accesseur avec fallback humains (et `nullptr` si humains absent → fallback cube placeholder). Utilisé par `EnterWorld` + `RacePreviewViewport`. |
| `engine::Engine::m_currentSkinnedMesh` | `SkinnedMesh*` (pointe dans `m_raceMeshes`). Pointe vers humains par défaut au boot, réassigné par `EnterWorld` selon `enterCmd.raceId`. Consommé par la state machine de locomotion (B.1) + la lambda Geometry. |
| `engine::render::race::RacePreviewViewport` | Offscreen RT Vulkan 512×512 RGBA8 exposé via `ImGui::Image` dans l'écran de création (pattern copié de `EditorViewportRenderTarget` M100.34). MVP : fallback clear color (rendu mesh 3D dans un RT standalone trop complexe — `SkinnedRenderer::Record` écrit dans le framegraph principal, pas RT-agnostic. Refactor renvoyé à C.2). |
| `AuthImGuiCharacterCreate` (refactor) | Itère sur `CharacterCreationPresenter::GetRaces()` au lieu d'une liste hardcodée. Affiche `ImGui::Image(GetImguiTextureId(), {256, 384})` + overlay `ImGui::Text("Race : <displayName>")`. |

### Pipeline asset par race

```
tools/asset_pipeline/inbox/
├── (y_bot files à plat — Humain réutilise Y Bot)
├── orc/<character>.fbx          ← user upload Mixamo
└── nains/<character>.fbx        ← user upload Mixamo

game/data/models/avatars/
├── y_bot/y_bot.glb              (humain, hérité de A)
├── y_bot_<clip>/...             (8 clips partagés, hérité de B.1)
├── orc/orc.glb                  (généré par convert_race_meshes.py)
└── nains/nains.glb              (idem)
```

`tools/asset_pipeline/convert_race_meshes.py` : itère sur `inbox/<race>/`, trouve le premier FBX with-skin (exclut "*No Skin.fbx"), appelle `FBX2glTF.exe --binary --khr-materials-unlit --skinning-weights 4`. Skip si `.glb` plus récent que le `.fbx` source. Les clips d'anim restent partagés depuis `y_bot_<clip>.glb`, retargetés par `LoadClipsAnimOnly` sur chaque squelette de race (convention Mixamo : bones nommés `mixamorig:*`).

### Wiring runtime

```
Engine::Init (boot)
├── CharacterCreationPresenter (local) Init → parse races.json
├── pour chaque race MVP (humains, nains, orc) :
│   ├── SkinnedMeshLoader::Load(meshPath) → SkinnedMesh
│   ├── LoadClipsAnimOnly × 7 clips (Idle/StartWalking/WalkBack/Run/Jump/Fall/Land)
│   └── m_raceMeshes.emplace(raceId, std::move(mesh))
├── m_currentSkinnedMesh = &m_raceMeshes["humains"] (default)
├── m_racePreviewViewport.Init (Vulkan RT + ImGui descriptor)
├── m_authImGui.SetRacePreview(&m_racePreviewViewport)
└── m_authUi.SetEngineForRaceMeshLookup(this)

Engine::EnterWorld (à l'activation d'un perso)
└── m_currentSkinnedMesh = GetRaceMesh(enterCmd.raceId)
    (fallback humains si race inconnue)

AuthImGuiCharacterCreate::RenderCharCreateScreen (per frame)
├── races = m_authPresenter->GetCharacterCreationPresenter()->GetRaces()
├── ImGui::Combo affiche les displayName
├── Au changement de sélection :
│   m_racePreview->SetMesh(m_authPresenter->GetRaceMeshForId(selectedRaceId))
└── ImGui::Image + overlay "Race : <displayName>"
```

### Tests

`race_definition_tests` (`src/client/character_creation/tests/RaceDefinitionTests.cpp`) — Linux CI uniquement, valide :
- 3 races MVP exposent un `meshPath` non vide pointant vers le `.glb` attendu.
- Les races hors-MVP exposent une string vide (fallback humains côté Engine).

Pas de test unitaire pour `Engine::GetRaceMesh` ni `RacePreviewViewport` (les deux requièrent un VkDevice — validés au smoke test visuel).

### Limitations / renvoyé à plus tard (C.2)

- Les 5 races hors-MVP (elfes, morts_vivants, corrompus, divins, démons) n'ont pas de `meshPath` dans `races.json` → fallback humains à `EnterWorld`.
- `RacePreviewViewport::Render` ne dessine pas le mesh 3D pour MVP (clear color uniquement, noir si pas de mesh / bleu sombre si mesh attaché). Refactor de `SkinnedRenderer` pour exposer une variante "draw into arbitrary RT" requis.
- Tick/Render du viewport pas câblés dans la render loop principale → l'image reste au layout `SHADER_READ_ONLY_OPTIMAL` initial (clear noir). L'overlay `ImGui::Text` compense visuellement.
- Pas de capsule différenciée par race (`r=0.3 h=1.8` partagés). Si on veut un nain plus petit ou un orc plus grand, il faut adapter `CharacterController::Init` selon `race_str` + ajuster les heuristiques sticky-ground.
- Pas de personnalisation cheveux / peau / yeux (champs `defaultSkinColors`, `defaultHairColors`, `defaultEyeColors` existent en data mais ne sont pas câblés au mesh).
- Pas de modificateurs gameplay des racials (Diplomatie, Furie, etc. restent du texte affiché — sous-projet futur).
- Anims partagées via retargeting Mixamo : si les proportions sont très différentes, attendre des artefacts (pieds qui glissent, mains qui ne se rejoignent pas). Pas observé en pratique pour humain / nain / orc Mixamo standard.
- `races.json` est parsé 2× au worst-case : une fois par le `CharacterCreationPresenter` local dans `Engine::Init`, une fois par `AuthUiPresenter::m_characterCreationPresenter`. Coût négligeable (small JSON, parsé une fois par process).

### Bugs résiduels B.1 préservés

Le wiring C MVP ne touche ni `CharacterController` ni `TerrainCollider` (sticky ground probe + half-height threshold restent intacts). Cf. `docs/superpowers/audits/2026-05-20-B1-status-known-bugs.md` pour la liste des bugs ouverts à reprendre dans une session debug ultérieure (caméra tracking, inputs combinés, smoke test §11).

## 25. Système de personnalisation de personnages (CHAR-MODEL.25 — 2026-05-21)

**Objectif** : couche *data-driven* de customisation (corps, tête, cheveux, pilosité, traits raciaux, couleurs, proportions, morph targets) par race et genre. **Aligné sur l'existant** : réutilise les `raceId` de `races.json` (humains, elfes, orcs, nains, morts_vivants, corrompus, divins, demons), pas de taxonomie parallèle. Purement client/data → **pas de redéploiement serveur**. Doc complète : `docs/CHARACTER_CUSTOMIZATION.md`.

### Composants

| Composant | Rôle |
|---|---|
| `game/data/configuration/races/<id>.json` | 1 fichier par race : limites physiques, types de corps, têtes/cheveux/pilosité, traits raciaux, palettes, morph targets, gameplay. **Générés** depuis `races.json`. |
| `game/data/configuration/{customization,equipment,animations}/` | `body_proportions.json` (presets), `armor_sets.json`, `sockets_attachments.json` (sockets sur `humanoid_base`), `animation_sets.json`. |
| `tools/asset_pipeline/gen_race_configs.py` | Dérive `configuration/races/*.json` depuis `races.json` + table `RACE_SPECS`. Idempotent. |
| `engine::client::CharacterCustomization` (`src/client/character_creation/CharacterCustomization.h`) | État sérialisable d'un perso (race, genre, modules, couleurs, métriques, morphs, traits optionnels). `ToJson`/`FromJson` versionnés. |
| `engine::client::CharacterCustomizationSystem` (`.h/.cpp`) | Charge `configuration/races/*.json` (`std::filesystem`, parser `engine::core::Config`), valide, génère (défaut/aléatoire), **résout** en `ResolvedCharacterAssets` (mesh, attachements socket+mesh, scaling d'os, collision, textures). |
| `ResolvedCharacterAssets` | Plan d'instanciation concret consommable par un futur étage de rendu skinned. |
| `tools/asset_pipeline/{process_character_assets,validate_fbx}.py` | Pipeline inbox → `game/data/models/characters/<race>/...` + validation FBX (extension, taille, nommage). |
| Tests : `character_customization_tests` (CTest) | Chargement des 8 races, validation, génération valide, résolution, ordre des tailles (nain<humain<orc), round-trip JSON. |

### Intégration UI (Phase 3)

`CharacterCreationPresenter` charge le système à l'`Init` (`<paths.content>/configuration`) et l'expose via `GetCustomizationSystem()`. L'écran `AuthImGuiRenderer::RenderCharCreateScreen` (`AuthImGuiCharacterCreate.cpp`, `#if _WIN32`) affiche le panneau **« Apparence physique »** : slider Taille + section « Proportions avancées » (jambes / épaules / corpulence) bornés aux limites de la race + boutons **Presets rapides** (data-driven depuis `body_proportions.json`, via `ApplyProportionPreset` clampé). État édité dans `AuthImGuiRenderer::m_charBodyMetrics`. Presets de proportions : `GetProportionPresets` / `DefaultMetricsForRace` / `ApplyProportionPreset` / `ClampMetricsToRace` (testés par `character_customization_tests`). **À brancher** : transmission serveur des métriques + application au mesh 3D.

### Limite d'intégration (stub assumé)

`ApplyCustomization` est un **stub documenté** : le moteur n'a pas encore de scène `GameObject`/`Skeleton`/composants. La fonction résout les assets et trace le plan ; le câblage GPU réel (attachement aux sockets, scaling des os, upload textures) est renvoyé à un ticket ultérieur. La **résolution** (`ResolveCustomization`) est complète et testée.

### Câblage CMake

- `CharacterCustomizationSystem.cpp` ajouté à `engine_core` (CMakeLists racine).
- Test `character_customization_tests` ajouté dans `src/CMakeLists.txt` (lié via `engine_core`, ne pas re-ajouter le `.cpp`).

### Extensibilité

Ajouter une race = entrée dans `races.json` + `RACE_SPECS` du générateur → `gen_race_configs.py` → `Initialize()` découvre le fichier automatiquement (aucune recompilation). Nouvelles features raciales : synchroniser `kKnownRacialFeatures` (`.cpp`) et le générateur. Conventions assets : `docs/CONVENTIONS_NAMING.md`, exigences FBX : `docs/FBX_REQUIREMENTS.md`.

## 26. Disposition clavier par défaut au 1er lancement (2026-05-22)

**Objectif** : au tout premier lancement (aucune préférence persistée), choisir automatiquement la disposition de déplacement selon le **clavier de l'OS** — clavier **français (AZERTY) → `zqsd`**, sinon `wasd`. Le joueur peut toujours changer dans les Options ; la valeur persistée (`user_settings.json`) **prime ensuite**.

### Mécanique

- Réglage : `controls.movement_layout` = `"wasd"` | `"zqsd"` (lu dans `m_useZqsd` côté `AuthUiPresenter`, et en `engine::render::MovementLayout` côté `Engine`).
- Détection : `Engine.cpp` (namespace anonyme) `DetectDefaultMovementLayout()` — sur Windows `GetKeyboardLayout(0)` → `PRIMARYLANGID == LANG_FRENCH` ⇒ `zqsd` ; ailleurs (`#else`) ⇒ `wasd`.
- Injection : juste après `ApplyUserSettingsOverrides(m_cfg)`, **si** `!m_cfg.Has("controls.movement_layout")` (ni `config.json` ni `user_settings.json` ne le fixent), on `SetValue` le défaut OS. Les lectures aval (`Engine` + `AuthUiPresenter`) héritent donc du bon défaut, et le 1er enregistrement de `user_settings.json` le persiste (template via `replaceString("movement_layout", …)`).
- Priorité : `user_settings.json` > `config.json` > **défaut OS** > `"wasd"` (repli ultime). Aucune régression : si une source fixe déjà la disposition, le défaut OS est ignoré.

## 27. Animations UE5 — clips disponibles & mapping (2026-05-22)

La library `models/animations/humanoid_base/Humanoid_Base_Standard/…glb` contient **45 clips** (rig UE5), chargés/retargetés par `SkinnedMeshLoader::LoadClipsAnimOnly` et rattachés à l'avatar dans `Engine.cpp` (branche `isUe5Rig` de `loadOneRace`).

### Étape 1 (faite) — clips exposés
- **Mappés aux états de locomotion** (joués par la state machine) : `Idle←Idle_Loop`, `Walk←Walk_Loop`, `StartWalking/WalkBack←Walk_Loop`, `Run←Jog_Fwd_Loop`, `Jump←Jump_Start`, `Fall←Jump_Loop`, `Land←Jump_Land`.
- **Exposés par leur nom brut** (disponibles via `SkinnedMesh::FindClip("<nom>")`, **sans déclencheur** pour l'instant) : tous les autres clips retenus — `Sprint_Loop, Walk_Formal_Loop, Crouch_Idle_Loop, Crouch_Fwd_Loop, Roll, Roll_RM, Push_Loop, Idle_Talking_Loop, Idle_Torch_Loop, Sword_Idle, Sword_Attack, Sword_Attack_RM, Punch_Jab, Punch_Cross, Hit_Chest, Hit_Head, Death01, Spell_Simple_Enter/Idle_Loop/Shoot/Exit, Dance_Loop, Sitting_Enter/Idle_Loop/Talking_Loop/Exit, Interact, PickUp_Table, Fixing_Kneeling, Swim_Fwd_Loop, Swim_Idle_Loop`.
- **Exclus** (non chargés) : `Pistol_*` (pas d'arme à feu), `Driving_Loop` (pas de véhicule), `A_TPose` (pose de référence).
- Test : `skinned_mesh_loader_tests` vérifie la présence/retarget de plusieurs de ces clips.

### Étape 2 (à venir, 1 PR par feature, testée en jeu) — déclencheurs
La state machine n'a que `Idle/StartWalking/Walk/WalkBack/Run/Jump/Fall/Land` ; `Run` = touche **Shift**. Aucun système de **crouch / dodge / combat / emote** ; la **nage** existe dans `CharacterController` mais est forcée à `false` (B.1). « Câbler » chaque clip = créer l'input + l'état/le système gameplay. Ordre prévu : **Sprint → Crouch → Roll/esquive → emote `/dance`** → (combat & nage = plus gros, dépend possiblement des events serveur).

## 28. Sprint — palier de vitesse + état de locomotion (2026-05-22)

**Objectif** : 3ᵉ palier de déplacement (premier déclencheur d'anim UE5 de l'étape 2 de §27). Touches : **marche** (défaut) → **course = Shift** (jog) → **sprint = Alt maintenu**.

### Chaîne complète
- **Input** : `engine::platform::Key::Alt = 0x12` (VK_MENU, capturé via `WM_SYSKEYDOWN` dans `Input::HandleMessage`). `BuildMoveInput` : `out.sprint = input.IsDown(Key::Alt)`.
- **Vitesse** : `CharacterController::Config::sprintSpeed = 13` ; `targetSpeed = sprint ? sprintSpeed : (run ? runSpeed : walkSpeed)`.
- **État** : `AvatarLocomotionState::Sprint` (après `Run`) ; transitions dans la SM (`Walk/Run/StartWalking/Land` → `Sprint` si `moveInput.sprint`, et `Sprint` → Run/Walk/Idle/WalkBack/Jump). `StateToClipName(Sprint) = "Sprint"`, `ClipLoops(Sprint) = true`.
- **Anim** : `addRole("Sprint", "Sprint_Loop")` dans la branche UE5 de `loadOneRace` (clip de la library UE5). Pour une race Mixamo sans clip "Sprint", `FindClip` renvoie nullptr → l'anim précédente continue (repli gracieux).

Priorité d'intention : **sprint > run > walk**. Reste de l'étape 2 (§27) : Crouch → Roll/esquive → emote `/dance`.

## 29. Crouch (Ctrl) — accroupi (vitesse + états de locomotion) (2026-05-22)

**Objectif** : 2ᵉ déclencheur de l'étape 2 (§27). Touche : **Ctrl maintenu** = accroupi (idle + déplacement). Priorité **crouch > sprint > run > walk**.

### Chaîne
- **Input** : `BuildMoveInput` → `out.crouch = input.IsDown(Key::Control)` (Ctrl existe déjà dans l'enum).
- **Vitesse** : `CharacterController::Config::crouchSpeed = 2.5` ; `targetSpeed = crouch ? crouchSpeed : (sprint ? … : (run ? … : walk))`.
- **États** : `AvatarLocomotionState::CrouchIdle` (immobile) et `CrouchWalk` (en mouvement). Implémentés par un **override après le switch** de la SM : si `moveInput.crouch` (et pas en amorce de saut) → `CrouchWalk`/`CrouchIdle` selon `moving`. Les `case CrouchIdle/CrouchWalk` du switch calculent la **sortie debout** (relâche Ctrl → Idle/Walk/Run/Sprint/WalkBack, ou Jump). `StateToClipName`/`ClipLoops` à jour (loop).
- **Anim** : `addRole("CrouchIdle","Crouch_Idle_Loop")` + `addRole("CrouchWalk","Crouch_Fwd_Loop")` (branche UE5).

### Limites connues (1ère itération)
- **Pas de réduction de capsule** : l'anim est accroupie mais la collision (`r=0.3 h=1.8`) est inchangée → pas de passage sous obstacle bas pour l'instant (à ajouter avec un test "puis-je me relever ?").
- **Ctrl** sert aussi de modificateur de raccourcis (ex. Ctrl+L loot) : tenir Ctrl pour s'accroupir peut interférer avec ces combos (rebindable si gênant).

Reste de l'étape 2 : Roll/esquive → emote `/dance`.

## 30. Roll/esquive (Ctrl double-tap) + emote `/dance` (2026-05-22)

**Objectif** : 3ᵉ et 4ᵉ (dernier) déclencheurs de l'étape 2 (§27). Termine §27 étape 2 (Sprint ✅ → Crouch ✅ → **Roll** → **emote**).

### Roll / esquive — Ctrl **double-tap**
- **Input** : détecté dans la SM (pas dans `BuildMoveInput`). À chaque `m_input.WasPressed(Key::Control)`, si l'écart avec l'appui précédent (`m_lastCtrlTapSec`, horloge `EngineNowSec`) ≤ **0.30 s** → `dodgePressed = true`. Ctrl **maintenu** reste le crouch (§29) ; **deux appuis rapides** = Roll.
- **État** : `AvatarLocomotionState::Roll` (**one-shot**, non bouclé — absent de `ClipLoops`). Override après le switch : `if (dodgePressed && état ≠ Roll) newState = Roll` — **prioritaire sur le crouch**. Le `case Roll` du switch sort vers la locomotion debout (Idle/Walk/Run/Sprint/WalkBack) quand `stateElapsed ≥ rollClip->duration` (ou clip absent → sortie immédiate).
- **Anim** : `addRole("Roll", "Roll")` (branche UE5). `StateToClipName(Roll) = "Roll"`.

### Emote — commande chat `/dance`
- **Input** : slash command **locale** (pas d'aller-retour serveur) dans `SetSendCallback` (canal `Say`) → pose `m_danceRequested = true` + ligne chat `[Emote] Vous dansez.`. Consommée (remise à `false`) une fois par tick dans la SM.
- **État** : `AvatarLocomotionState::Dance` (**bouclé** — présent dans `ClipLoops`). Override : déclenché **uniquement à l'arrêt** (`!moving && !movingBack && !jumpPressed` et pas en Roll). Le `case Dance` du switch **interrompt** l'emote au moindre déplacement/saut (→ Jump/Walk/Run/Sprint/WalkBack). `StateToClipName(Dance) = "Dance"`.
- **Anim** : `addRole("Dance", "Dance_Loop")` (branche UE5).

### Priorité d'intention (au sol)
`Roll (double-tap) > Dance (/dance, à l'arrêt) > Crouch (Ctrl tenu) > Sprint (Alt) > Run (Shift) > Walk`.

### Limites connues
- **Roll sans déplacement réel** : l'anim joue mais le `CharacterController` n'applique pas d'impulsion/i-frames d'esquive (purement cosmétique pour l'instant).
- **Double-tap Ctrl** : la fenêtre 0.30 s peut occasionnellement déclencher un Roll lors d'un crouch « nerveux » (deux appuis rapprochés). Ajustable via le seuil.

§27 étape 2 **terminée**.

## 31. Attaque mêlée (clic gauche) — clip combat one-shot (2026-05-22)

**Objectif** : 1er déclencheur de l'étape « combat » de §27 (après l'étape 2 locomotion). Câble un clip d'attaque mêlée déclenché par l'input, **purement client/cosmétique** pour l'instant (pas de dégâts ni d'aller-retour serveur).

### Chaîne
- **Input** : **clic gauche** (`m_input.WasMousePressed(MouseButton::Left)`), edge-triggered, lu dans la SM. Le bloc gameplay est déjà gardé contre le focus chat / l'auth (Engine.cpp ~6969) ; on exclut en plus le drag inventaire (`!m_invUi.IsDragging()`) pour ne pas frapper en relâchant un objet.
- **État** : `AvatarLocomotionState::Attack` (**one-shot**, non bouclé — absent de `ClipLoops`). Override après le switch : `if (attackPressed && !jumpPressed && état ≠ Roll && état ≠ Attack) newState = Attack` — **prioritaire sur le crouch** (on peut frapper accroupi, retour debout l'attaque finie), mais ne coupe **pas** un Roll en cours et ne s'enclenche pas en plein saut. Le `case Attack` du switch sort vers la locomotion quand `stateElapsed ≥ attackClip->duration` (ou clip absent → sortie immédiate).
- **Anim** : `addRole("Attack", "Sword_Attack")` (branche UE5 ; clip dont l'existence est vérifiée par `SkinnedMeshLoaderTests`). `StateToClipName(Attack) = "Attack"`.
- **Déplacement** : non modifié pendant l'attaque (geste plein corps, pas de root motion) — le `CharacterController` continue de piloter la position selon l'input.

### Priorité d'intention (au sol), mise à jour
`Roll (double-tap Ctrl) > Attack (clic gauche) > Dance (/dance, à l'arrêt) > Crouch (Ctrl tenu) > Sprint (Alt) > Run (Shift) > Walk`.

### Limites connues
- **Cosmétique uniquement** : aucun dégât, aucune cible, aucun envoi serveur. Le HUD combat existant (`src/client/combat/`) reste piloté par les events serveur, indépendant de ce geste.
- **Pas d'arme visible** : le clip `Sword_Attack` est joué sans système d'équipement (l'avatar « frappe » à mains nues visuellement). À relier au futur système d'équipement.
- **Pas de combo** : un clic pendant l'attaque est ignoré (le clip doit finir). Combo `Sword_Attack` → enchaînement à ajouter plus tard.
- **Clic sur UI ouverte** : un clic gauche sur une fenêtre de jeu (inventaire/boutique) peut aussi déclencher le geste (curseur libre en vue 3ᵉ personne) ; seuls le focus chat et le drag inventaire sont exclus pour l'instant.
## 32. Menu de panneaux (barre de menus ImGui) + libération de la touche E (2026-05-22)

**Objectif** : offrir un accès **souris** à tous les panneaux togglables (sans raccourci clavier dédié) et **libérer la touche E** pour une future action « interagir » (hors combat). Premier menu ImGui du **client de jeu** (jusqu'ici, seul l'éditeur monde en avait).

### Chaîne
- **Barre de menus** : `ImGui::BeginMainMenuBar()` → menu `« Panneaux »` rendu dans la branche ImGui in-game de `Engine::Update` (`src/client/app/Engine.cpp`, juste après `m_chatImGui->Render(...)`, sous `#if defined(_WIN32)`). Toujours visible en jeu (tant que `render.chat_imgui.enabled` ou un menu pause/options est actif).
- **Items** : un `MenuItem` par panneau (Carnet de sorts, Arènes, Champs de bataille, PvP extérieur, Météo, Événements, Guilde, Hôtel des ventes, Jets de butin). Chaque item reflète l'état `m_*Visible` (coche) et, à l'ouverture, reproduit le `RequestList()`/`RequestTeams()` du toggle clavier correspondant.
- **Libellés ASCII** : la police ImGui par défaut (ProggyClean) n'a pas les glyphes accentués → libellés sans accents pour un rendu correct quelle que soit la police.
- **Touche E libérée** : le bloc `if (... Key::E) { m_gameEventVisible = ... }` (toggle GameEvents au clavier) est **supprimé**. GameEvents s'ouvre désormais **uniquement via le menu**. Les autres panneaux **gardent leur touche** (B/A/G/P/Y/U/H/L) **en plus** de l'accès menu.

### Limites connues
- **Barre toujours visible** : occupe un bandeau haut en jeu (choix assumé pour l'accès sans touche) ; à terme on pourra la masquer/replier.
- **E non rebranchée** : E ne fait plus rien tant que le système « interagir » (PNJ/objet/loot) n'existe pas — réservée volontairement, pas de bind mort.
- **Rendu non vérifié visuellement** : code ImGui Windows-only, non testable en headless ; validation au build Windows.

## 33. Sort — cast (touche R) — clip `Spell_Simple_Shoot` one-shot (2026-05-22)

**Objectif** : 2ᵉ déclencheur de l'étape « combat » de §27 (pendant du clic gauche de l'attaque #31). Câble un clip de **sort** piloté par l'input, **purement client/cosmétique** (pas de cible, de dégâts ni d'aller-retour serveur).

### Chaîne
- **Input** : **touche `R`** (`m_input.WasPressed(Key::R)`), edge-triggered, lue dans la SM. Le **clic droit n'est PAS utilisé** (déjà pris par le RMB-look de la caméra orbitale, `ThirdPersonCamera`/Engine.cpp). La touche **E reste libre/réservée** (cf. §32, action « interagir » future). Le bloc gameplay est déjà gardé contre le focus chat / l'auth (Engine.cpp ~6961), donc `R` ne se déclenche pas en tapant dans le chat.
- **État** : `AvatarLocomotionState::Cast` (**one-shot**, non bouclé — absent de `ClipLoops`). Override après le switch : `if (castPressed && !jumpPressed && état ∉ {Roll, Attack, Cast}) newState = Cast` — **prioritaire sur le crouch** (caster accroupi → retour debout le sort fini), mais ne coupe **pas** un Roll/Attack en cours et ne s'enclenche pas en plein saut. Le `case Cast` du switch sort vers la locomotion quand `stateElapsed ≥ castClip->duration` (ou clip absent → sortie immédiate).
- **Anim** : `addRole("Cast", "Spell_Simple_Shoot")` (branche UE5). `StateToClipName(Cast) = "Cast"`. L'attaque exclut aussi `Cast` en cours (pas de clic gauche pendant un sort, et inversement).
- **Déplacement** : non modifié pendant le sort (geste plein corps, pas de root motion).

### Priorité d'intention (au sol), mise à jour
`Roll (double-tap Ctrl) > Attack (clic gauche) > Cast (touche R) > Dance (/dance, à l'arrêt) > Crouch (Ctrl tenu) > Sprint (Alt) > Run (Shift) > Walk`.

### Limites connues
- **Cosmétique uniquement** : aucun projectile, aucune cible, aucun envoi serveur.
- **Clip unique (pas de séquence)** : on joue `Spell_Simple_Shoot` seul, sans le wind-up/exit (`Spell_Simple_Enter`/`Idle_Loop`/`Exit`). Le crossfade lisse le retour à la locomotion ; une vraie séquence Enter→(channel)→Shoot→Exit est une amélioration future.
- **Repli gracieux** : une race sans clip `Spell_Simple_Shoot` (`FindClip` nullptr) sort immédiatement de l'état (anim précédente conservée).

## 34. Touches d'action remappables depuis le menu Options (2026-05-22)

**Objectif** : rendre **modifiables in-game** (panneau Options) les touches des actions ajoutées récemment (Sprint, Accroupi/Roulade, Sort), sans toucher au protocole ni au serveur.

### Config
- Clés `controls.keybind.{sprint,crouch,cast}` (défauts `Alt`/`Ctrl`/`R`) dans `config.json`. Noms acceptés = ceux de la table `kRebindableKeys` (Engine.cpp) : lettres A-Z **sauf I/J/K/T** (absentes de `platform::Key`), chiffres 0-9, `Ctrl`/`Alt`/`Shift`/`Espace`/`Tab`.
- `KeyName(Key)` / `KeyFromName(nom, fallback)` (anonymous namespace de `Engine.cpp`) font la conversion enum ↔ nom. Pas de modif de l'API `Input`.

### Lecture gameplay (config-driven)
- Dans le bloc gameplay (`Engine.cpp`, `if (!authGateActive && !IsChatFocusActive() && !m_inGameOptionsPanelVisible)`), les 3 touches sont **résolues chaque frame** depuis la config (reflète un rebind immédiatement).
- `BuildMoveInput(..., sprintKey, crouchKey)` : `out.sprint = IsDown(sprintKey)`, `out.crouch = IsDown(crouchKey)` (avant : `Alt`/`Control` en dur). La **roulade** réutilise `crouchKey` (double-appui). Le **sort** lit `WasPressed(castKey)`. Run (Shift) et Jump (Espace) restent fixes (hors périmètre « nouvelles touches »).

### UI Options (rebind par capture)
- Section « Controles » ajoutée au **panneau Options in-game** (`Engine.cpp`, `#if defined(_WIN32)`). Une ligne par action (Sprint/Accroupi/Sort) : libellé + touche courante + bouton « Modifier ».
- « Modifier » arme `m_rebindingAction` (1/2/3) ; le rendu suivant capture la **1re touche connue pressée** (`kRebindableKeys`) et écrit `controls.keybind.*` (Échap = annuler).
- **Gameplay suspendu** tant que le panneau Options est ouvert (ajout de `!m_inGameOptionsPanelVisible` au garde) → la touche capturée ne déclenche pas l'action en même temps.
- Roulade et Attaque affichées en **info** (non remappables : la roulade suit la touche Accroupi, l'attaque est le clic gauche — souris).

### Limites connues
- **Persistance** : le rebind est **persisté** dans un fichier dédié `keybinds.json` (écrit par le panneau Options via `FileSystem::WriteAllText`, format contrôlé). Au boot, `ApplyUserSettingsOverrides` fait `cfg.LoadFromFile("keybinds.json")` qui **merge** par-dessus les défauts de `config.json`. Choix d'un **fichier dédié** (et non un patch de `user_settings.json`) pour que tout échec d'écriture/lecture soit **bénin** (retour aux défauts) et ne corrompe jamais les autres réglages. Les sliders volume/sensibilité de ce panneau restent eux session-only (hors périmètre).
- **Pas de détection de conflit** : binder deux actions sur la même touche est permis (ex. réutiliser une touche de panneau B/G/…). À durcir si besoin.
- **Souris/modificateurs** : l'attaque (clic gauche) n'est pas remappable en v1 ; rebinder un modificateur (Alt/Ctrl) vers une lettre fonctionne mais peut entrer en conflit avec d'autres usages (Ctrl = modificateur de raccourcis).

## 35. Action « interagir » (touche E) — geste `Interact` one-shot (2026-05-22)

**Objectif** : donner enfin un usage à la **touche E** réservée au §32 (libérée du toggle GameEvents). Premier maillon d'un futur système d'interaction (PNJ/objet/loot) : pour l'instant un **geste cosmétique** one-shot, comme l'attaque/le sort. Livré **dans la même PR que les keybinds (§34)** (consigne « minimum de PR »).

### Chaîne
- **Input** : touche **remappable** `controls.keybind.interact` (défaut `E`), résolue chaque frame (`KeyFromName`). `interactPressed = WasPressed(interactKey)`, dans le bloc gameplay gardé (chat/auth/Options).
- **État** : `AvatarLocomotionState::Interact` (**one-shot**, absent de `ClipLoops`). Override : `if (interactPressed && !jump && état ∉ {Roll, Attack, Cast, Interact}) newState = Interact` — prioritaire sur le crouch, ne coupe pas Roll/Attack/Cast. Le `case Interact` sort vers la locomotion quand `stateElapsed ≥ interactClip->duration`.
- **Anim** : `addRole("Interact", "Interact")`. `StateToClipName(Interact) = "Interact"`.
- **Remap** : 4ᵉ ligne « Interagir » dans la section Controles du panneau Options (capture clavier, comme sprint/crouch/sort).

### Priorité d'intention (au sol), mise à jour
`Roll > Attack > Cast > Interact > Dance > Crouch > Sprint > Run > Walk` (les actions one-shot s'excluent mutuellement tant que l'une joue).

### Limites connues
- **Geste cosmétique seulement** : aucune cible, aucun objet ramassé, aucun PNJ adressé — c'est l'animation de base. Le vrai système « interagir » (raycast vers une entité interactible, prompt, loot) reste à construire (nécessitera des entités interactibles, voire des events serveur).
- **Repli gracieux** : race sans clip `Interact` → sortie immédiate de l'état.

## 36. Emotes génériques par slash command (généralisation de `/dance`) (2026-05-22)

**Objectif** : transformer le `/dance` mono-usage (§30) en **système d'emotes data-driven** — ajouter une emote = **une ligne** + un `addRole`. Livré dans la même PR que §34/§35 (« minimum de PR »).

### Mécanique
- **État unique `Emote`** (renommé depuis `Dance`) : anim **en boucle**, interrompue par tout déplacement/saut (le `case Emote` sort vers Walk/Run/Jump/…). `ClipLoops(Emote) = true`.
- **Clip dynamique** : le rôle d'anim joué n'est pas fixe. `m_pendingEmoteRole` (posé par la slash command) → consommé par la SM → `m_currentEmoteRole`. Au point de lecture du clip (`Engine.cpp`, transition d'état), si `newState == Emote` on joue `m_currentEmoteRole` au lieu de `StateToClipName`.
- **Table des emotes** (`kEmotes` dans le handler chat) : `{ commande, rôle, message }`. Actuellement : `/dance`, `/sit` & `/assis`, `/talk`, `/torch`, `/kneel` (Fixing_Kneeling), `/sittalk` (Sitting_Talking_Loop), `/push` (Push_Loop). Rôles mappés via `addRole("Dance","Dance_Loop")`, `addRole("Sit","Sitting_Idle_Loop")`, `addRole("Talk","Idle_Talking_Loop")`, `addRole("Torch","Idle_Torch_Loop")`.
- **Priorité** : `Roll > Attack > Cast > Interact > Emote > Crouch > Sprint > Run > Walk` (emote uniquement à l'arrêt, hors Roll).

### Ajouter une emote
1. Une entrée `{ "/macommande", "MonRole", "Mon message." }` dans `kEmotes`.
2. Un `addRole("MonRole", "Clip_Loop")` (clip présent dans la library UE5).

### Limites connues
- **Pas de changement d'emote « à chaud »** : enchaîner deux emotes sans bouger ne relance pas le clip (le state reste `Emote`, pas de transition). Bouger puis ré-emoter. (Acceptable ; à raffiner si besoin via un re-trigger sur changement de rôle.)
- **Emotes en boucle simple** : pas de séquence Enter/Exit (ex. `Sitting_Enter`/`Exit` non utilisés) — on joue directement le `*_Idle_Loop`, le crossfade lisse l'entrée/sortie.

## 37. Coup de poing (touche C) + factorisation des actions one-shot (2026-05-22)

**Objectif** : 2ᵉ attaque mêlée (coup de poing) **et** nettoyage de la logique d'exclusion mutuelle des actions one-shot, devenue verbeuse au fil des ajouts (attaque/sort/interaction).

### Coup de poing
- **Input** : touche **remappable** `controls.keybind.punch` (défaut `C`), edge. `addRole("Punch", "Punch_Jab")`.
- **État** : `AvatarLocomotionState::Punch` (one-shot, comme l'attaque). 5ᵉ ligne « Coup de poing » dans la section Controles d'Options.

### Factorisation `busyOneShot()`
- Une lambda `busyOneShot()` (dans la SM) retourne vrai si l'avatar est dans **une action one-shot ou la roulade** (`Roll/Attack/Cast/Interact/Punch`). Les overrides d'attaque/coup/sort/interaction se réduisent à `if (xPressed && !jump && !busyOneShot()) newState = X;` — **comportement identique** à l'ancienne liste d'exclusions, mais plus lisible et **extensible** (ajouter une action one-shot = une entrée dans la lambda + un override).
- **Priorité (au sol)** : `Roll > Attack > Punch > Cast > Interact > Emote > Crouch > Sprint > Run > Walk`.

### Limites connues
- **Cosmétique** : aucun dégât/cible (comme l'attaque épée).
- **Alternance Jab/Cross** : chaque coup alterne `Punch_Jab` et `Punch_Cross` (variété ; clip dynamique via `m_currentPunchRole`, comme l'état Emote). Pas de vrai « combo » chaîné sur presses rapides — un coup pendant l'autre est ignoré (one-shot) ; échec bénin (au pire le mauvais clip de poing, jamais d'état bloqué).
