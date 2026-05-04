# World Editor — terrain par défaut visible et fallback orange

**Date** : 2026-05-04
**Auteur** : brainstorming Claude + Hubert Cornet
**Périmètre** : `lcdlln_world_editor.exe` uniquement (zéro impact client jeu / serveur)
**Déploiement** : ✅ client uniquement, pas de redéploiement serveur

## 1. Contexte et problème

Depuis l'introduction du World Editor (`lcdlln_world_editor.exe`), cliquer sur
**« Creer une nouvelle carte »** affiche le statut « Nouvelle carte OK » mais le
viewport 3D ne montre aucun terrain — seule la grille blanche en surimpression
ImGui apparaît. Le bouton **« Charger la carte selectionnee »** présente le même
symptôme. L'utilisateur n'a aucun retour visuel lui confirmant qu'un terrain
existe et où il se trouve, ce qui rend l'éditeur inutilisable pour démarrer une
nouvelle carte.

L'analyse du code montre que `WorldEditorSession::ActionNewMap` produit bien
tous les fichiers attendus (heightmap plate, splat 100 % herbe, masque herbe
à zéros) et que `TerrainRenderer::Init` charge correctement ces fichiers via
`RebuildWorldEditorTerrainGpu()`. Le terrain GPU est donc présent en mémoire ;
le problème est double :

1. **Caméra mal positionnée** : le reset caméra place la vue à
   `(centerX, midGroundY + 50 m, centerZ + ws*0.25)` avec pitch `0.5 rad`. Avec
   un terrain de 10 km (`engine::world::kZoneSize`), la caméra se retrouve à
   2.5 km derrière le bord arrière du terrain, à 150 m d'altitude, regardant
   28° vers le bas. Le rayon caméra heurte le sol à ~282 m devant la caméra,
   bien avant d'atteindre le terrain. Le terrain est hors champ.
2. **Aucun retour visuel "terrain présent mais sans texture"** : le rendu
   classique du shader terrain combine quatre couches splat × textures
   triplanaires. Sur une carte fraîchement créée (aucune texture utilisateur
   assignée), les samplers retombent sur les défauts moteur, dont le résultat
   final peut être très peu lisible (couleurs proches du ciel ou du fond).

## 2. Objectifs

- **Objectif principal** : tout `Creer une nouvelle carte` ou
  `Charger la carte selectionnee` doit afficher **immédiatement** un terrain
  visible dans le viewport 3D du World Editor, sans intervention manuelle de
  l'utilisateur.
- **Objectif secondaire** : différencier visuellement un terrain « en cours de
  construction » (sans textures utilisateur) d'un terrain « fini » (avec
  textures), pour qu'un mappeur sache au premier coup d'œil où il en est.

## 3. Non-objectifs

- **Pas de modification du client jeu** (`lcdlln.exe`) : il continue d'afficher
  les cartes finies avec leurs textures officielles. Aucune régression visuelle
  attendue côté joueurs.
- **Pas de modification du serveur** (master/shard) : zéro wire-breaking, pas
  de migration DB, pas de nouveau handler.
- **Pas de modification des formats de fichiers** : `.r16h`, `.slap`, `.grms`,
  `map.lcdlln_edit.json` (`kFormatVersion = 1`) restent inchangés.
- **Pas de toggle manuel** pour le fallback orange : il est entièrement piloté
  par l'état du document (`splatLayerTextureRefs`).
- **Pas de tests visuels automatisés** : la suite n'a pas d'infra screenshot
  diff, la validation visuelle se fait à la main et est documentée dans la PR.

## 4. Architecture et fichiers touchés

| Fichier | Modification |
|---|---|
| `engine/Engine.cpp` (`RebuildWorldEditorTerrainGpu`) | Reset caméra centré, gardé par `m_worldEditorExe`. Détection `noUserTextures` et propagation au `TerrainRenderer`. |
| `engine/render/terrain/TerrainRenderer.h/.cpp` | Nouvelle méthode `SetNoUserTexturesFallback(bool)`, membre `m_noUserTextures`, propagation au push-constant dans `Record`. |
| `game/data/shaders/terrain.frag` | Champ `int noUserTextures` ajouté au push-constant ; branche orange en début de calcul albedo. |
| `game/data/shaders/terrain.vert` | Champ `int noUserTextures` ajouté au PC (alignement CPU). |
| `tests/` (nouveau test C++) | `WorldEditorSession_DefaultMapHasEmptyTextureRefs` — vérifie que `ActionNewMap` produit `splatLayerTextureRefs` toutes vides. |
| `CODEBASE_MAP.md` | Nouvelle section 15 décrivant le flux carte par défaut, reset caméra et fallback orange. |
| `docs/terrain_et_world_editor.md` | Paragraphes en fin de section World Editor : fallback orange + reset caméra. |

## 5. Détails techniques

### 5.1 Reset caméra (Sujet 1)

Dans `Engine::RebuildWorldEditorTerrainGpu()`, remplacer la branche actuelle
`if (m_editorMode)` par `if (m_worldEditorExe)` et utiliser le calcul suivant :

```cpp
const float ox = m_terrain.GetTerrainOriginX();
const float oz = m_terrain.GetTerrainOriginZ();
const float ws = m_terrain.GetTerrainWorldSize();
const float hs = m_terrain.GetHeightScale();

const float centerX     = ox + ws * 0.5f;
const float centerZ     = oz + ws * 0.5f;
const float midGroundY  = hs * 0.5f;
const float camAltitude = midGroundY + 80.0f;

engine::render::Camera reset;
reset.position.x = centerX;
reset.position.y = camAltitude;
reset.position.z = centerZ;
reset.yaw        = 0.0f;
reset.pitch      = 0.35f;
reset.fovYDeg    = 70.0f;
reset.aspect     = float(std::max(1, m_width)) / float(std::max(1, m_height));
reset.nearZ      = 0.1f;
reset.farZ       = std::max(5000.0f, ws * 1.5f);
m_renderStates[0].camera = reset;
m_renderStates[1].camera = reset;
LOG_INFO(Render,
    "[WorldEditor] Camera reset: pos=({:.1f},{:.1f},{:.1f}) farZ={:.0f} ws={:.0f}",
    reset.position.x, reset.position.y, reset.position.z, reset.farZ, ws);
```

**Différences vs l'existant** :
- Position **au centre** du terrain (au lieu de `centerZ + ws*0.25` qui placait
  la caméra hors du terrain).
- Altitude `+80 m` (au lieu de `+50 m`) pour marge de manœuvre quand la
  heightmap sera sculptée plus tard.
- `farZ = max(5000, ws * 1.5)` pour garantir la visibilité des bords sur les
  grands terrains.
- Garde `m_worldEditorExe` au lieu de `m_editorMode` (qui peut être null si
  `EditorMode::Init` échoue).

### 5.2 Fallback orange (Sujet 2)

**Détection (C++)** dans `RebuildWorldEditorTerrainGpu`, après `m_terrain.Init` :

```cpp
bool noUserTextures = false;
if (m_worldEditorExe && m_worldEditorSession) {
    noUserTextures = true;
    const auto& refs = m_worldEditorSession->Doc().splatLayerTextureRefs;
    for (const std::string& r : refs) {
        if (!r.empty()) { noUserTextures = false; break; }
    }
}
m_terrain.SetNoUserTexturesFallback(noUserTextures);
```

**Push-constant terrain** — extension cohérente vert + frag :

```glsl
// terrain.vert et terrain.frag
layout(push_constant) uniform PC {
    float patchOriginX;
    float patchOriginZ;
    float morphFactor;
    int   lodLevel;
    int   noUserTextures;  // 0 = rendu normal, 1 = orange uni
} pc;
```

Côté `TerrainRenderer.cpp`, la struct CPU `TerrainPushConstants` (utilisée par
`vkCmdPushConstants`) ajoute en queue `int noUserTextures` ; la valeur est
`m_noUserTextures ? 1 : 0`. Le `range.size` de `VkPushConstantRange` est mis à
jour en conséquence.

**Branche shader (terrain.frag)** — insertion juste avant le bloc actuel
`outAlbedo = vec4(albedo, 1.0)` :

```glsl
if (pc.noUserTextures != 0) {
    outAlbedo = vec4(1.0, 0.55, 0.1, 1.0);  // orange vif
} else {
    outAlbedo = vec4(albedo, 1.0);
}
```

Les sorties `outNormal`, `outORM`, `outVelocity` restent **inchangées** : la
lighting pass dispose de la macro-normale (interpolée depuis la heightmap),
appliquera donc un shading Lambert sur l'orange et le relief sera lisible
(orange clair côté soleil, orange sombre face opposée).

**Bascule automatique** : à chaque appel de `RebuildWorldEditorTerrainGpu`
(create, load, ou reload manuel via le bouton « Recharger terrain GPU »), le
flag est recalculé. Dès qu'une couche splat reçoit un mapping texture
utilisateur dans le document, le prochain rebuild bascule sur le rendu normal.

**Côté client jeu (`lcdlln.exe`)** : `m_worldEditorExe = false` →
`noUserTextures` reste à `false` → push-constant à 0 → branche orange jamais
prise → comportement strictement inchangé.

## 6. Tests

### 6.1 Test unitaire C++

`WorldEditorSession_DefaultMapHasEmptyTextureRefs` (à ajouter dans le fichier
de tests existant pour `WorldEditorSession`, ou créer un nouveau fichier si
absent) :

```cpp
TEST(WorldEditorSession, DefaultMapHasEmptyTextureRefs) {
    engine::core::Config cfg;
    cfg.SetString("paths.content", <tmp_dir>);  // écrit dans un répertoire temp
    engine::editor::WorldEditorSession session;
    ASSERT_TRUE(session.ActionNewMap(cfg));
    const auto& refs = session.Doc().splatLayerTextureRefs;
    for (const std::string& r : refs) {
        EXPECT_TRUE(r.empty());
    }
}
```

`ActionNewMap` écrit `height.r16h`, `splat.slap`, `grass.grms` et le JSON sur
disque sous `paths.content`. Le test doit donc utiliser un répertoire
temporaire (via `std::filesystem::temp_directory_path()` ou équivalent
gtest/repo) pour ne pas polluer `game/data/`. À l'implémentation, suivre la
convention en place dans les tests existants du repo (regarder comment d'autres
tests `WorldEditorSession_*` ou tests engine I/O gèrent leur cwd / path).

Ce test garantit que la condition de bascule du fallback orange reste
opérationnelle si quelqu'un modifie `ActionNewMap` plus tard.

### 6.2 Validation manuelle (PR golden path)

À exécuter et capturer en screenshot avant le merge :

1. Lancer `lcdlln_world_editor.exe` depuis la racine du dépôt.
2. Cliquer **« Creer une nouvelle carte »** (valeurs par défaut).
3. **Attendu** : viewport affiche un terrain plat **orange vif** lisible avec
   shading sun, grille blanche superposée nette. Statut : « Nouvelle carte
   OK ». Caméra au centre, vue plongeante.
4. **« Charger la carte selectionnee »** sur une carte existante → caméra
   repositionnée, terrain visible.
5. Assigner une texture à la couche herbe via l'UI Splat → après rebuild
   automatique, l'orange disparaît et le rendu normal apparaît.

### 6.3 Non-régression client jeu

6. Lancer `lcdlln.exe` connecté au shard. Le terrain de la zone doit s'afficher
   comme aujourd'hui (textures officielles, **pas d'orange**).

### 6.4 Logs à vérifier

- `[WorldEditor] Camera reset: pos=(...) farZ=... ws=...` au create/load.
- `[TerrainRenderer] Init begin/done` sans `LOG_ERROR` ni
  `LOG_WARN "fichier introuvable"`.

## 7. Risques et mitigations

| Risque | Mitigation |
|---|---|
| Le push-constant déborde la limite Vulkan (typ. 128 octets) après ajout du `int`. | Mesure : PC actuel = `4*float + 1*int = 20 octets`. Après ajout : 24 octets. Limite Vulkan minimum garantie : 128 octets. Aucun risque. |
| L'orange "casse" l'œil quand le mappeur travaille longtemps. | Le fallback est censé être transitoire — dès qu'une texture est posée, il disparaît. Si plainte utilisateur, on peut atténuer la saturation (`vec3(0.85, 0.50, 0.15)`). |
| Le client jeu reçoit accidentellement `noUserTextures=1` (régression). | Garde explicite `m_worldEditorExe` côté C++ avant de lever le flag. Test manuel #6 vérifie. |
| `farZ = ws * 1.5` réduit la précision du depth buffer pour les petits terrains. | `farZ = max(5000, ...)` préserve le minimum existant pour `ws < 3333 m`. Format depth `D32_SFLOAT` (32 bits float) gère bien les grandes plages. |

## 8. Mise à jour documentation

- **`CODEBASE_MAP.md`** : ajouter une nouvelle section 15 « Éditeur monde —
  création/chargement de carte (chantier 2026-05-04) » couvrant : carte par
  défaut (heightmap, splat, grass), reset caméra, fallback orange. Le contenu
  exact est dans la section 5 du présent design doc et sera reproduit dans le
  CODEBASE_MAP au moment de l'implémentation.
- **`docs/terrain_et_world_editor.md`** : deux paragraphes en fin de section
  « World Editor (Windows) » documentant le fallback orange et le reset
  caméra.

## 9. Ce que cette PR ne fait PAS

- N'introduit pas de toggle manuel "wireframe debug".
- Ne modifie pas les valeurs par défaut de `terrain.world_size`,
  `terrain.height_scale`, `terrain.origin_x/z`, `heightmapResolution`.
- Ne change pas le comportement par défaut quand on a au moins une texture
  posée (rendu normal préservé bit-pour-bit).
- Ne traite pas le cas "heightmap manquante / corrompue" : `TerrainRenderer`
  fait déjà un fallback plat à zero, comportement laissé tel quel.
