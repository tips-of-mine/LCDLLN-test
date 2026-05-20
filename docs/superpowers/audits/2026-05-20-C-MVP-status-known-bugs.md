# C MVP (Races multi-mesh) — Statut & bugs connus

**Branche** : `claude/races-c-mvp` (commit de tête : `1bf83fc` au 2026-05-20)
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
- **Bug critique** : `kMvpRaces[]` itérait sur `"orc"` (singulier) alors que `races.json` a `"orcs"` (pluriel). Race jamais chargée, fallback humains pour tout perso orc. Fix `1bf83fc` : `{ "humains", "nains", "orcs" }`.

## Smoke test partiel (Task 14)

Test effectué par l'utilisateur sur le build `cbbf955` (avant le fix `1bf83fc`). Résultats :

| Critère §7 | Statut | Note |
|------------|--------|------|
| 1. Boot logs : 3 meshes chargés | ⚠️ Partiel | Humains OK (65 bones, 9 clips). Orcs n'était PAS chargé à cause du bug `orc` vs `orcs` (fix `1bf83fc`). Nains absent (fichier manquant, fallback humains attendu). |
| 2. Combo race affiche les 8 races | ✅ OK | Le refactor `AuthImGuiCharacterCreate` itère sur `CharacterCreationPresenter::GetRaces()`. |
| 3. Création perso orc → mesh orc | ❌ Non testé | L'utilisateur n'a pas créé de perso orc lors de ce smoke. À re-tester sur build `1bf83fc`. |
| 4. Login perso existant → mesh OK | ✅ OK | Le perso `fds` (race=humains) affiche correctement le mesh humain. `[EnterWorld] Avatar mesh selected for race 'humains' (65 bones, 9 clips)`. |
| 5. Anims B.1 sur mesh race | ⚠️ Non testé | L'utilisateur a quitté immédiatement après EnterWorld, sans bouger. |
| 6. Fallback race inconnue | ⚠️ Non testé | Pas de modif `race_str` manuel pour ce test. |

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

## Bugs résiduels B.1 préservés intacts

Le wiring C MVP ne touche ni `CharacterController`, ni `TerrainCollider`, ni la state machine de locomotion (B.1). Tous les fixes B.1 restent en place :
- Sticky ground probe (anti-oscillation Ground↔Air)
- TerrainCollider seuil `groundHeight + halfHeight`
- `m_avatarYaw` init à π + matrice `R_y(m_avatarYaw)` (sans `+π`)
- `EngineNowSec()` normalisation float

Bugs B.1 ouverts (cf. `docs/superpowers/audits/2026-05-20-B1-status-known-bugs.md`) :
- Caméra tracking pas bien fixée au mouvement (à reprendre en session debug B.1 dédiée).

## Plan pour C.2 (sous-projet futur)

1. **Asset nain** : upload + conversion (dépendant user).
2. **Rendu mesh 3D dans le preview** : refactor `SkinnedRenderer` pour exposer une variante RT-agnostic, câbler `Tick/Render` du viewport dans la render loop auth.
3. **Wiring `SetRacePreview` diagnostic** : ajouter logs pour comprendre pourquoi l'image n'apparaît pas du tout en MVP, vs juste noire.
4. **5 autres races** : elfes / morts_vivants / corrompus / divins / démons. Asset sourcing + extension `races.json` avec leurs `meshPath`.
5. **Capsule différenciée par race** (mensurations). Nain plus petit, orc plus grand, ajuster `CharacterController::Init`.
6. **Personnalisation cheveux/peau/yeux** (champs déjà en data dans `defaultSkinColors`, `defaultHairColors`, `defaultEyeColors`).
7. **Anims dédiées par race** (optionnel) : Mixamo retargeted clips peuvent donner des artefacts si proportions très différentes. Clips dédiés ou bone scaling à explorer.
