# C MVP (Races multi-mesh) — Statut & bugs connus

**Branche** : `claude/races-c-mvp` (mergée dans main via PR #648 / squash `166d484` au 2026-05-20)
**Spec** : [docs/superpowers/specs/2026-05-20-races-mvp-design.md](../specs/2026-05-20-races-mvp-design.md)
**Plan** : [docs/superpowers/plans/2026-05-20-races-mvp.md](../plans/2026-05-20-races-mvp.md)

## Objectif C MVP

Charger un mesh skinned différent selon la race du personnage (humain / nain / orc), avec un preview 3D animé dans l'écran de création. Cosmétique seule (pas de gameplay diff, pas de capsule différente).

## Ce qui a été livré

### Tasks 1→13 du plan

1. `RaceDefinition::meshPath` + extension `races.json` pour 3 races MVP — commit `cf8346e`
2. `race_definition_tests` Linux CI — commit `2b9401b`
3. `tools/asset_pipeline/convert_race_meshes.py` — commit `ad882c1`
4. Conversion partielle `orc.glb` (nains en attente upload user) — commit `b65ac75`
5. Rename `m_playerSkinnedMesh` → `m_currentSkinnedMesh` — commit `eb91eb4`
6. `Engine::m_raceMeshes` map + boot loop 3 races + restore build — commit `8cd2506` + polish `26d89ba`
7. `Engine::GetRaceMesh` accessor avec fallback humains — commit `b044001`
8. EnterWorld assigne `m_currentSkinnedMesh` depuis `race_str` DB — commit `1e54436` (+ `raceId` ajouté à `EnterWorldCommand`)
9. `RacePreviewViewport` infra Vulkan RT + ImGui texture — commit `b6f7857`
10+11. `RacePreviewViewport::Tick` (sample Idle) + Render fallback clear color — commit `103db0c`
12. `AuthImGuiCharacterCreate` refactor (presenter + preview wiring multi-fichier) — commit `0c1ec85`
13. Documentation CODEBASE_MAP §24 — commit `cbbf955`

### Bugs corrigés post-test

- Merge conflict après squash de B.1 sur main — commit `b1883f2` (résolution mécanique : on garde HEAD partout).
- **Bug critique race id** : `kMvpRaces[]` itérait sur `"orc"` (singulier) alors que `races.json` a `"orcs"` (pluriel). Race jamais chargée, fallback humains pour tout perso orc. Fix `1bf83fc` : `{ "humains", "nains", "orcs" }`.
- **Bug viewport invisible** : `RacePreviewViewport::Init` faisait `UNDEFINED → SHADER_READ_ONLY` sans clear color → image undefined invisible. Fix `5348ebd` : ajout `vkCmdClearColorImage` bleu sombre.
- **Bug soft-delete slot** : `FindNextSlot` filtrait `deleted_at IS NULL` mais la unique key SQL `uq_characters_account_server_slot` était globale → collision après suppression. Fix migration `0064` (`5348ebd`) : generated VIRTUAL column `slot_active` + partial unique.
- **Bug soft-delete name (check programmatique)** : `CharacterNameExistsOnServer` ne filtrait pas `deleted_at IS NULL` → nom de perso supprimé restait réservé à vie. Fix `913acaa`.
- **Bug soft-delete name (unique SQL)** : seconde unique key `uq_characters_name_server` bloquait la réutilisation du nom au niveau DB. Fix migration `0065` (`ba767bc`) : generated VIRTUAL column `name_active` + partial unique.

## Smoke test final (Task 14, build post-`ba767bc`)

Test effectué par l'utilisateur après tous les fixes (viewport + soft-delete). Résultats :

| Critère §7 | Statut | Note |
|------------|--------|------|
| 1. Boot logs : 3 meshes chargés | ⚠️ Partiel | Humains OK (65 bones, 9 clips). Orcs OK (65 bones, 9 clips). Nains absent (FBX non uploadé, fallback humains attendu). |
| 2. Combo race affiche les 8 races | ✅ OK | Le refactor `AuthImGuiCharacterCreate` itère sur `CharacterCreationPresenter::GetRaces()`. |
| 3. Création perso orc → mesh orc | ✅ OK | `[EnterWorld] Avatar mesh selected for race 'orcs' (65 bones, 9 clips)` confirmé sur `grokorc`. |
| 4. Login perso existant → mesh OK | ✅ OK | Validé sur `fds` (humains) et `grokorc` (orcs). |
| 5. Anims B.1 sur mesh race | ⚠️ Partiel | Idle/Walk/Run/Idle transitions OK. Bugs visuels résiduels (cf. §6 ci-dessous). |
| 6. Fallback race inconnue | ⏸️ Non testé | Test optionnel, non bloquant pour MVP. |

## Bugs en suspens (renvoyés à C.2 ou session debug ultérieure)

### 1. ~~Preview viewport invisible dans l'écran de création~~ (FIXED)

**Symptôme initial** : "Rien du tout, pas d'image" pour la zone preview 256×384, alors que `[RacePreviewViewport] Init OK` était loggé.

**Cause racine identifiée** : `RacePreviewViewport::Init` transitionnait `UNDEFINED → SHADER_READ_ONLY_OPTIMAL` sans clear color explicite. Le contenu de l'image était donc UNDEFINED côté Vulkan (souvent rendu invisible/transparent par le driver). Et `Render()` n'étant jamais hooké dans la frame loop pour MVP, l'image n'était jamais re-clearée après Init.

**Fix appliqué** : Init fait désormais `UNDEFINED → TRANSFER_DST → vkCmdClearColorImage(bleu sombre 0.10/0.12/0.18) → SHADER_READ_ONLY`. La zone 256×384 affiche un fond bleu sombre visible + l'overlay `ImGui::Text("Race : <nom>")` par-dessus. C'est suffisant comme feedback MVP — le rendu mesh 3D réel reste C.2.

**Note** : la couleur du fond ne change pas dynamiquement quand l'utilisateur change de race (Render() reste non-hooké). Le texte overlay est l'unique élément informatif. Suffisant pour MVP.

### 2. RacePreviewViewport::Tick/Render jamais appelés

**Symptôme attendu** : même si le wiring `SetRacePreview` était bon, l'image resterait noire (clear initial par `Init`) car `Tick(dt)` + `Render(cmdBuf)` ne sont pas appelés dans la render loop de l'auth screen.

**Pré-requis pour fixer** : intégrer un appel `m_racePreviewViewport.Tick(dt)` + `m_racePreviewViewport.Render(cmdBuf)` à chaque frame quand l'auth screen rend la `CharacterCreate` UI. Probablement dans `Engine::Update` ou la lambda FrameGraph de l'auth screen.

**Pourquoi non fait pour MVP** : le rendu mesh 3D dans un RT standalone (Task 11) est documenté comme fallback "clear color", donc même avec Tick/Render câblés, on n'aurait que du bleu sombre au lieu de noir. Pas un gain visuel suffisant pour justifier le risque Vulkan.

### 3. Rendu mesh 3D dans le preview (renvoyé C.2)

**Constat** : `SkinnedRenderer::Record` est écrit pour le framegraph principal (SceneColor_LDR + GBuffer + depth). Le réutiliser dans un RT standalone (sans GBuffer, sans depth, sans lighting pass) nécessite un refactor non-trivial.

**Options pour C.2** :
- Extraire une variante `SkinnedRenderer::RecordIntoRT(rt, viewMatrix, projMatrix, ...)` qui n'écrit qu'en color.
- Faire un mini-pipeline `SkinnedUnlitRenderer` dédié au preview (PSO + descriptor sets séparés).
- Hot-swap : étendre le framegraph principal avec un "preview viewport" pass qui copie une partie de SceneColor_LDR vers le RT preview. Risque cohérence visuelle.

### 4. Asset `nains.glb` manquant

**État** : pas de FBX nain uploadé dans `tools/asset_pipeline/inbox/nains/` (juste un README placeholder). Le script `convert_race_meshes.py` skip silencieusement. Au runtime, `[SkinnedMeshLoader] Parse failed for 'nains.glb' (cgltf result=6)` — message imprécis (le fichier n'existe pas, result=6 est plutôt "data too short" mais cgltf semble retourner ça aussi pour fichier inexistant).

**Workaround** : fallback humains pour tout perso de race `nains` à EnterWorld. Aucun crash.

**Action user** : upload un FBX Mixamo with-skin (suggestion : "Mremireh O Desbiens" pour le côté trapu) dans `tools/asset_pipeline/inbox/nains/` via GitHub web UI. Puis re-runner `convert_race_meshes.py` pour générer `nains.glb`.

### 5. Doublon parsing `races.json` (cosmétique)

**Symptôme** : le log boot montre `[CharacterCreation] Loaded 8 race(s) from races.json` **deux fois**. Une fois par le `CharacterCreationPresenter` que possède `AuthUiPresenter`, une fois par le `CharacterCreationPresenter` local instancié dans `Engine::Init`.

**Impact** : négligeable (small JSON parsé 2× au worst-case, une fois par process). Cosmétique.

**Fix possible (C.2)** : Engine pourrait demander la liste des races à AuthUI via une méthode dédiée au lieu d'instancier son propre presenter local. Refactor architecture, pas critique.

### 6. Vibration des jambes pendant Walk/Run sur mesh orc

**Symptôme rapporté lors du smoke test final** : pendant les anims Walk et Run sur le perso `grokorc` (race=orcs, mesh `orc.glb`), vibration / tremblement visible au niveau des jambes du personnage. Pas explicitement reporté sur le mesh humain — peut-être présent mais moins visible vu les proportions.

**Hypothèses** :
1. **Retargeting Mixamo imparfait** : les clips d'animation (`y_bot_walk.glb`, `y_bot_run.glb`, etc.) sont chargées via `LoadClipsAnimOnly` qui retarget par nom de bone (`mixamorig:LeftLeg` etc.). Si le mesh orc a des proportions de jambes différentes du Y Bot, les keyframes mal interpolées peuvent produire des micro-translations parasites.
2. **Bone weights** du mesh orc mal exportés (problème dans le FBX original ou la conversion FBX2glTF).
3. **Bug de skinning shader** : interpolation linéaire entre matrices au lieu de quaternion, micro-flicker numérique.
4. **`inverseBindMatrix`** du mesh orc mal calculé (cf. `SkinnedMeshLoader`).

**Premier réflexe debug** : ouvrir `orc.glb` dans un viewer (gltf-viewer-app ou Blender) pour voir si la vibration est dans le mesh source ou si elle apparaît au runtime. Si runtime, dump les matrices de bones leg et voir si elles oscillent.

**Sévérité** : bas (cosmétique, pas bloquant). Chip-task créée pendant la session — à traiter dans branche dédiée `claude/debug-skinning-leg-vibration` après les bugs B.1 plus prioritaires.

### 7. Anims Jump incorrectes (asset issue, action user)

**Symptôme rapporté lors du smoke test final** : "On voit bien les modèles de saut, mais je n'ai pas dû te donner les bons, les visuels ne sont pas bons" — les clips Mixamo utilisés pour Jump/Fall/Land donnent un visuel incorrect.

**Action user** : re-uploader les bons FBX Mixamo Jump/Fall/Land dans `tools/asset_pipeline/inbox/` (variantes "No Skin"). Les FBX actuels sont `Jumping No Skin.fbx` etc. Choisir un set d'animations cohérent qui forme un cycle Jump→Fall→Land naturel.

**Sévérité** : moyenne (visuel pendant le saut). Pas un bug code, juste un remplacement d'asset suivi d'une régénération des `.glb` via `convert_race_meshes.py` ou pipeline équivalent pour les clips standalone.

### 8. Direction backward incorrecte après strafe latéral (B.1)

**Symptôme rapporté lors du smoke test final** : touche D (strafe droite) puis S (recule) → le perso recule mais à destination de la gauche. Symétrique avec A puis S.

**Statut** : bug B.1 (locomotion / state machine), pas C MVP. Documenté en détail dans [docs/superpowers/audits/2026-05-20-B1-status-known-bugs.md §2bis](2026-05-20-B1-status-known-bugs.md).

### 9. Caméra ne suit pas le perso en temps réel (B.1)

**Statut** : bug B.1 connu, confirmé une seconde fois lors du smoke test C MVP final. Documenté dans [docs/superpowers/audits/2026-05-20-B1-status-known-bugs.md §1 et §3](2026-05-20-B1-status-known-bugs.md).

### 10. Layout clavier AZERTY non auto-détecté (UX)

**Symptôme rapporté** : sur clavier AZERTY, les touches Z et Q ne déclenchent aucun mouvement. Seules W, A, S, D fonctionnent (mapping QWERTY hardcoded au défaut).

**Workaround utilisateur** : aller dans Options → Layout → basculer en ZQSD. L'option `controls.movement_layout` existe et est persistée dans `user_settings.json` (cf. fix B.1 `91d7279`).

**Bug réel** : pas de détection automatique du layout système au premier lancement. Le défaut WASD pénalise les utilisateurs AZERTY qui ne connaissent pas l'option.

**Sévérité** : basse (workaround simple), pas bloquant. À polish ultérieurement (détection via Windows `GetKeyboardLayout()` ou prompt au premier lancement).

## Bugs résiduels B.1 préservés intacts

Le wiring C MVP ne touche ni `CharacterController`, ni `TerrainCollider`, ni la state machine de locomotion (B.1). Tous les fixes B.1 restent en place :
- Sticky ground probe (anti-oscillation Ground↔Air)
- TerrainCollider seuil `groundHeight + halfHeight`
- `m_avatarYaw` init à π + matrice `R_y(m_avatarYaw)` (sans `+π`)
- `EngineNowSec()` normalisation float

Bugs B.1 ouverts (cf. `docs/superpowers/audits/2026-05-20-B1-status-known-bugs.md`) :
- §1 + §3 : Caméra tracking pas bien fixée au mouvement (confirmé à nouveau lors du smoke test C MVP final).
- §2bis : Direction backward incorrecte après strafe latéral (nouveau bug découvert lors du smoke test C MVP final).

## Plan pour C.2 (sous-projet futur)

1. **Asset nain** : upload + conversion (dépendant user).
2. **Rendu mesh 3D dans le preview** : refactor `SkinnedRenderer` pour exposer une variante RT-agnostic, câbler `Tick/Render` du viewport dans la render loop auth.
3. **Wiring `SetRacePreview` diagnostic** : ajouter logs pour comprendre pourquoi l'image n'apparaît pas du tout en MVP, vs juste noire.
4. **5 autres races** : elfes / morts_vivants / corrompus / divins / démons. Asset sourcing + extension `races.json` avec leurs `meshPath`.
5. **Capsule différenciée par race** (mensurations). Nain plus petit, orc plus grand, ajuster `CharacterController::Init`.
6. **Personnalisation cheveux/peau/yeux** (champs déjà en data dans `defaultSkinColors`, `defaultHairColors`, `defaultEyeColors`).
7. **Anims dédiées par race** (optionnel) : Mixamo retargeted clips peuvent donner des artefacts si proportions très différentes. Clips dédiés ou bone scaling à explorer.
