# Sous-projet C (MVP) — Races multi-mesh fantasy

**Date** : 2026-05-20
**Branche cible** : à créer (`claude/races-c-mvp`) après le merge de B.1.
**Prérequis** : A (mergé) + B.1 (poussé sur `claude/locomotion-b1`, en attente de merge).

## 1. Objectif

Remplacer le mesh unique "Y Bot" partagé par tous les personnages par un **mesh choisi en fonction de la race du perso** stockée en DB. Trois races MVP visuellement contrastées : Humain (reprend Y Bot), Nain, Orc. Les 5 autres races déjà définies dans `races.json` (elfes, morts-vivants, corrompus, divins, démons) sont renvoyées à un sous-projet C.2 ultérieur.

Le sous-projet est **cosmétique uniquement** :
- Pas de modification gameplay (les `racials` de `races.json` restent du texte affiché, pas de stat/abilité).
- Pas de changement de capsule (`r=0.3 h=1.8` partagés par les 3 races).
- Pas de skin swap dynamique en jeu : la race est figée à la création de personnage (déjà persistée en DB via la colonne `race_str` ajoutée en migration 0033).
- Aucun changement réseau, aucun redéploiement serveur.

## 2. Contexte et état actuel

### Acquis hérité de A et B.1

- Runtime skinning + animation glTF/cgltf opérationnel.
- 8 clips Mixamo "No Skin" chargés via `SkinnedMeshLoader::LoadClipsAnimOnly` (Idle, StartWalking, Walk, WalkBack, Run, Jump, Fall, Land), retargetés par nom de bone Mixamo (`mixamorig:*`).
- State machine de locomotion 8 états + crossfade lerp/slerp TRS (B.1).
- `CharacterController` + `TerrainCollider` branchés, capsule sweep.
- `OrbitalCameraController` pure (suit `ccPos`).
- Loader : `SkinnedMeshLoader::LoadFromGltf` (with-skin) charge mesh + skeleton.

### Acquis côté data / DB / UI

- Table SQL `races` + colonne `characters.race_str VARCHAR(32)` (migration 0033).
- 8 races définies dans `game/data/races/races.json` : `humains`, `elfes`, `orcs`, `nains`, `morts_vivants`, `corrompus`, `divins`, `demons`. Chaque race a `id`, `displayName`, `description`, `racials[]`, `iconPath`, `defaultSkinColors[]`, `defaultHairColors[]`, `defaultEyeColors[]`.
- Icônes 2D présentes pour les 8 races dans `game/data/ui/races/<race>/theme.json`.
- UI de création de personnage existante (`AuthImGuiCharacterCreate`) qui transmet `raceId` au master via `BuildCharacterCreateRequestPayload`.

### Gap à combler

- Côté rendu 3D : **un seul mesh** (`game/data/models/avatars/y_bot/y_bot.glb`) chargé en dur dans `Engine` et utilisé pour tous les persos peu importe la race.
- `races.json` ne porte pas de champ `meshPath` — il faut l'ajouter.
- L'écran de création de personnage ne preview pas le mesh de la race sélectionnée — juste l'icône 2D.

## 3. Décisions de cadrage (validées avec l'utilisateur)

| Décision | Choix |
|----------|-------|
| Périmètre MVP | 3 races : Humain + Nain + Orc |
| Sources des meshes | Mixamo distincts (Y Bot pour Humain, 2 Mixamo characters à choisir par l'utilisateur pour Nain et Orc) |
| Gameplay | Cosmétique seule (pas de racials effectifs, pas de capsule différente) |
| Preview 3D dans l'écran de création | Oui, anime Idle + rotation lente (avec fallback icône 2D si blocker Vulkan) |
| Skin swap dynamique en jeu | Non (figé à la création, lu depuis DB à EnterWorld) |

## 4. Architecture

```
+----------------------------------+      +---------------------------------+
| Engine::Init (au boot)           |      | Engine::EnterWorld              |
|                                  |      |                                 |
| RaceAssetRegistry::Load          |      | race_str = enterCmd.race_str    |
|   ("game/data/races/races.json") |      | m_currentSkinnedMesh =          |
|                                  |      |   m_raceMeshes[race_str].get()  |
| Pour chaque race MVP :           |      |   fallback "humains" si absent  |
|   m_raceMeshes[race_str] =       |      |                                 |
|     SkinnedMeshLoader::          |      | (consommé tel quel par la       |
|       LoadFromGltf(meshPath)     |      |  state machine de B.1, aucun    |
|   + LoadClipsAnimOnly partagés   |      |  changement dans la lambda      |
|                                  |      |  Geometry)                      |
+----------------------------------+      +---------------------------------+

+----------------------------------+
| AuthImGuiCharacterCreate         |
|                                  |
| ImGui combo : selection race     |
|   |                              |
|   v                              |
| RacePreviewViewport (singleton)  |
|   - offscreen RT 512x512         |
|   - mini-camera orbit            |
|   - Sample Idle anim @ now       |
|   - SkinnedRenderer::Record      |
|   - ImGui::Image(rtView, size)   |
+----------------------------------+
```

### Composants

1. **`RaceAssetRegistry`** (nouveau, header + impl)
   - Parse `races.json` étendu (champ `meshPath` ajouté à chaque race).
   - Expose `GetMeshPath(string raceId) → optional<string>`.
   - Fallback "humains" si race inconnue.
   - Tests unitaires : `race_asset_registry_tests`.

2. **`Engine::m_raceMeshes`** (modification existante)
   - `std::unordered_map<std::string, std::unique_ptr<SkinnedMesh>>`.
   - Rempli au boot via une boucle sur les races MVP.
   - Méthode `Engine::GetRaceMesh(const std::string& raceId) → SkinnedMesh*` (fallback humains, nullptr si même humain absent).

3. **`Engine::m_currentSkinnedMesh`** (renommage de `m_playerSkinnedMesh`)
   - Pointe vers le mesh de la race du perso courant (assigné à EnterWorld).
   - La state machine de B.1 + la lambda Geometry sampling consomment ce pointeur (aucun changement de logique).

4. **`RacePreviewViewport`** (nouveau, header + impl)
   - Encapsule un offscreen rendertarget Vulkan 512×512 RGBA8.
   - Mini-caméra orbit autour du mesh (target = (0, 0.9, 0), distance = 2.5 m, yaw avance de 30°/seconde).
   - `Tick(dt)` : avance le yaw, sample Idle clip à `EngineNowSec()`.
   - `Render(cmdBuf)` : clear color + `SkinnedRenderer::Record` (sans PBR/shadows, unlit).
   - `GetImGuiTexture()` : retourne le handle ImGui pour `ImGui::Image`.
   - `SetMesh(SkinnedMesh* mesh)` : change le mesh affiché (appelé quand la race UI change).

5. **`AuthImGuiCharacterCreate`** (modification existante)
   - Ajout d'un panneau ImGui à côté du formulaire qui affiche `ImGui::Image(viewport.GetImGuiTexture(), {256, 384})`.
   - À chaque sélection de race différente, appelle `viewport.SetMesh(engine.GetRaceMesh(raceId))`.

## 5. Pipeline asset

### Organisation des sources

```
tools/asset_pipeline/inbox/
├── (y_bot files à plat, inchangé pour Humain)
├── Standard Walk.fbx           (mesh + skeleton humain via Y Bot)
├── Standing Idle No Skin.fbx   (clips partagés, inchangé)
├── ... (autres clips d'anim)
├── orc/                         ← user upload Mixamo orc FBX ici
│   └── <character>.fbx          (with-skin pour mesh + skeleton orc)
└── nains/                       ← user upload Mixamo nain FBX ici
    └── <character>.fbx
```

### Organisation runtime

```
game/data/models/avatars/
├── y_bot/y_bot.glb              (humain, existe)
├── y_bot_idle/y_bot_idle.glb    (clips partagés, existent)
├── ... (Idle, StartWalking, Walk, WalkBack, Run, Jump, Fall, Land)
├── orc/orc.glb                  (nouveau)
└── nains/nains.glb              (nouveau)
```

### Script de conversion

`tools/asset_pipeline/convert_race_meshes.py` (nouveau) :
- Itère sur `tools/asset_pipeline/inbox/<race>/*.fbx`.
- Pour chaque fichier with-skin, appelle `FBX2glTF.exe --input <fbx> --output game/data/models/avatars/<race>/<race> --binary --khr-materials-unlit --skinning-weights 4`.
- Log la taille du fichier produit + nombre de frames d'anim (utile pour valider que c'est bien un mesh+skin).

Réutilise le même `FBX2glTF.exe` que B.1.

### Extension de `races.json`

Ajouter un champ `meshPath` à chaque race MVP :

```json
{
  "id": "humains",
  "displayName": "Humains",
  "meshPath": "models/avatars/y_bot/y_bot.glb",
  ...
}
```

Pour les races hors-MVP (elfes, morts_vivants, etc.), `meshPath` reste absent / vide → `RaceAssetRegistry` log un warn et fallback "humains" si on tente de les utiliser à EnterWorld.

## 6. Comportement runtime

### Boot

1. `Engine::Init` charge `races.json` via `RaceAssetRegistry::Load`.
2. Pour chaque race MVP (`{humains, nains, orcs}`) :
   - Lookup `meshPath` dans `RaceAssetRegistry`.
   - Charge le mesh + skeleton via `SkinnedMeshLoader::LoadFromGltf(meshPath)`.
   - Charge les 8 clips d'anim partagés via `LoadClipsAnimOnly` (retargeté sur ce squelette).
   - Stocke dans `m_raceMeshes[raceId]`.
3. Log les races chargées avec succès + warns pour les races sans `meshPath` ou avec échec de chargement.

### EnterWorld

1. Le payload `EnterWorldResponse` contient déjà `race_str` (depuis migration 0033).
2. `Engine::EnterWorld` :
   - `SkinnedMesh* mesh = GetRaceMesh(race_str);`
   - `m_currentSkinnedMesh = mesh ? mesh : GetRaceMesh("humains");` (fallback)
   - Log la race effective utilisée.

### Per frame

- La lambda Geometry et la state machine de B.1 lisent `m_currentSkinnedMesh` (renommé depuis `m_playerSkinnedMesh`). Aucun changement de logique d'animation ou de rendu.

### Création de perso (UI)

1. `AuthImGuiCharacterCreate` affiche un combo / radio list des races (data depuis `RaceAssetRegistry`).
2. À chaque changement de sélection : `racePreviewViewport.SetMesh(engine.GetRaceMesh(selectedRaceId))`.
3. Chaque frame de l'auth screen : `racePreviewViewport.Tick(dt)` puis `viewport.Render(cmdBuf)` puis `ImGui::Image(viewport.GetImGuiTexture(), {256, 384})`.

## 7. Tests

### Unitaires (Linux CI, `lcdlln_add_simple_test`)

- **`race_asset_registry_tests`** :
  - `GetMeshPath("humains")` retourne le path attendu.
  - `GetMeshPath("foobar")` retourne `nullopt`.
  - Parse robuste : champ `meshPath` manquant → race chargée mais `GetMeshPath` retourne `nullopt`.
- **`engine_get_race_mesh_tests`** : (si `Engine` peut être instancié sans Vulkan ; sinon ce test est gradé "manuel")
  - Avec mock de `m_raceMeshes` : `GetRaceMesh("humains")` retourne un pointeur valide.
  - `GetRaceMesh("inconnue")` retourne le fallback humains.

### Smoke test visuel manuel (critères MVP)

1. Boot : les 3 meshes sont chargés sans warning dans le log.
2. Création de perso : preview 3D s'affiche pour chaque race sélectionnée, mesh anime Idle, rotation lente OK.
3. Création d'un perso "Orc" → EnterWorld → le perso visible est bien l'orc (pas Y Bot).
4. Logout + login avec un perso "Nain" déjà créé → mesh nain s'affiche.
5. Les anims B.1 jouent correctement sur les 3 meshes sans artefact majeur (pieds qui glissent acceptables).
6. Fallback : créer un perso avec race inconnue (`race_str = "foobar"`) → client affiche le mesh humain, pas de crash.

## 8. Risques et mitigations

| Risque | Mitigation |
|--------|------------|
| Squelettes nain/orc non-Mixamo (noms de bones différents) | Check au boot, log warn et fallback Y Bot pour cette race. Refuser l'asset à l'upload si nécessaire. |
| Proportions très différentes → anims partagées avec artefacts | Accepter pour MVP. Document dans la PR. Sous-projet C.2 raffinera (clips dédiés ou bone scaling). |
| Mémoire : 3 meshes + 8 clips × 3 retargetés | Estimation ~5-10 MB. Acceptable. |
| Preview 3D dans auth screen : conflit ImGui/Vulkan | Utiliser le même pattern que `EditorViewportRenderTarget` (M100.34). Fallback : icône 2D si blocker. |
| Assets nain/orc dépendants de l'utilisateur | Démarrer l'implémentation avec Y Bot × 3 (humains pointe vers Y Bot, nains et orcs aussi temporairement). Brancher les vrais meshes dès qu'ils arrivent dans `inbox/<race>/`. |

## 9. Hors-scope (renvoyé à plus tard)

- Personnalisation cheveux / peau / yeux (champs `defaultSkinColors`, `defaultHairColors`, `defaultEyeColors` restent inutilisés en MVP).
- Customisation morphologique (taille, corpulence).
- Modificateurs gameplay des `racials` (Diplomatie, Furie, etc. — restent du texte affiché).
- Les 5 autres races (elfes, morts_vivants, corrompus, divins, démons) → sous-projet C.2.
- Capsule différenciée par race → resterait possible plus tard mais nécessite un sweep test plus rigoureux.
- Anims race-specific (clips dédiés au lieu de partagés).

## 10. Déploiement

✅ Client uniquement, pas de redéploiement serveur. La colonne `race_str` est déjà en DB. Aucun nouveau opcode UDP, aucun nouveau handler master/shard, aucune migration SQL.
