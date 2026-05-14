# INVESTIGATION — Terrain invisible dans World Editor (et probablement client jeu)

**Date** : 2026-05-04
**Statut** : ⛔ Bloquant (impossible d'avancer client + éditeur)
**PR de contexte** : #427 (branche `claude/terrain-default-fallback-JlWpU`)
**Auteur initial du chantier** : Hubert Cornet + Claude Sonnet/Opus

---

## 1. Symptôme

Dans `lcdlln_world_editor.exe`, après "Créer une nouvelle carte" ou "Charger
la carte sélectionnée" :
- Statut UI : "Terrain : pret" (vert).
- Viewport 3D : **uniformément gris/beige (clear color du ciel)**, aucun
  triangle de terrain visible.
- **Aucune couleur orange** alors que le fallback `noUserTextures` est censé
  écrire `vec4(1.0, 0.55, 0.1, 1.0)` dans l'albedo.

Ce bug existe **probablement aussi côté client jeu** (`lcdlln.exe`) mais
moins visible car la majorité du temps de l'utilisateur est passé dans
l'auth UI 2D, pas le rendu 3D in-world.

---

## 2. Ce qui a déjà été fixé dans la PR #427

### 2.1 Caméra reset (Engine::RebuildWorldEditorTerrainGpu)
Centre XZ des **patches réellement maillés** (pas de `world_size`), altitude
`height_scale*0.5 + 80m`, pitch 0.35 rad, farZ adaptatif.

```cpp
const float actualExtX = m_terrain.GetActualRenderedExtentX();
const float centerX    = ox + actualExtX * 0.5f;
```

Nouveau getter `TerrainRenderer::GetActualRenderedExtentX/Z()` qui retourne
`patchCountX * kPatchQuads * vertStepWorld`. Important parce que
`vertStepWorld = world_size / 1024` est codé en dur, et avec une heightmap
256×256 + world_size 10000m, les patches ne couvrent que 2344m, pas 10000m.

### 2.2 Push-constant `noUserTextures` + branche orange
Étendu le push-constant terrain de 16 à 20 octets (`int noUserTextures`).
`Engine::RebuildWorldEditorTerrainGpu` détecte les
`Doc().splatLayerTextureRefs` vides et appelle
`m_terrain.SetNoUserTexturesFallback(true)`.

`terrain.frag` :
```glsl
if (pc.noUserTextures != 0) {
    outAlbedo = vec4(1.0, 0.55, 0.1, 1.0);
} else {
    outAlbedo = vec4(albedo, 1.0);
}
```

### 2.3 Frustum cull bypass (workaround temporaire)
`TerrainRenderer::SetFrustumCullEnabled(bool)`. `Engine` le passe à `false`
pour le World Editor uniquement (client jeu garde le cull actif). Sans ça,
les 225 patches étaient tous CPU-cull.

### 2.4 **Fix de fond `Camera::ComputeViewMatrix`** (commit `ee181da`)
Bug latent confirmé : la matrice était stockée transposée + sign-flip sur
`m[10]` et `m[14]`. Conséquence : `viewProj.row3 ≈ viewProj.row2` au lieu
de `(0, 0, 1, 0)` → `clip.w = -96` pour des points pourtant devant la
caméra → rasterizer Vulkan rejetait tout.

**Vérifié par diagnostic** : après fix, `proj(centerPatch).w = +27.43`
(positif), `proj(cam-Z100).w = +93.94` (devant, positif),
`proj(cam+Z100).w = -93.94` (derrière, négatif). Convention cohérente.

**Vérifié non-régression** : client jeu testé manuellement, pas cassé.

### 2.5 Diagnostic verbeux dans `TerrainRenderer::Record`
Au premier appel après init terrain, log :
- Matrice viewProj brute (4 lignes row-major)
- Projection de 4 points test : cam, cam±Z100, centerPatch
- Pour le centerPatch, distance signée à chacun des 6 plans frustum

Permet d'isoler rapidement où la pipeline lâche.

---

## 3. Preuves diagnostiques accumulées

Logs du dernier build (commit `a9e6e26` artifact, avant restore matrix fix —
mais les conclusions restent valides) :

```
[TerrainRenderer] Init OK (15×15 patches, worldSize=10000 heightScale=200)
[TerrainEditingTools] Init OK (origin=(-512,-512) size=10000 ...)
[TerrainRenderer] noUserTextures fallback: 0 -> 1
[WorldEditor] Camera reset: pos=(659.9,180.0,659.9) farZ=5000 actualExt=(2344x2344) origin=(-512,-512)
[TerrainRenderer] Record diag: patches total=225 kept=225 culled=0 cam=(659.9,180.0,659.9) noUserTex=1
[TerrainRenderer] viewProj row3=(0.0000,-0.3429,-0.9394,681.5902)   <- avec fix matrix
[TerrainRenderer] proj(centerPatch=(660,100.0,660))=(-0.00,107.31,27.33,27.43)  <- clip.w POSITIF
[TerrainRenderer]   plane[0..5] all "pass"
```

→ La matrice viewProj est correcte, le frustum cull passe les 225 patches,
`Record()` est invoqué par le frame graph. Et pourtant **aucun pixel** de
terrain n'arrive à l'écran.

Conclusion : **le bug est en aval du vertex shader** (entre rasterizer et
présentation au swapchain).

---

## 4. Hypothèses prioritaires pour la suite

### 4.1 Lighting pass mange l'albedo terrain (priorité 🔴 haute)
La lighting pass (`engine/render/LightingPass.cpp`) lit `GBufferA` (albedo),
`GBufferB` (normale), `GBufferC` (ORM), `GBufferDepth` et calcule le pixel
lit final. Si elle :
- attend une convention de view-space différente de celle écrite par le
  terrain (depth-test, normal decoding),
- ou multiplie par 0 quelque part (light_dir mal calculé, NdotL négatif),
- ou utilise l'inverse view matrix avec une convention incompatible,

→ l'albedo orange est correctement écrit dans le GBuffer mais le pixel
final est noir.

**Test simple** : afficher `outAlbedo` directement dans le swapchain en
court-circuitant la lighting (debug mode). Si on voit l'orange, c'est la
lighting qui mange. Sinon, problème plus en amont (rasterizer/depth).

### 4.2 Framebuffer attachment mismatch (priorité 🟠 moyenne)
`TerrainRenderer::Record` crée son propre render pass + framebuffer avec
`LOAD_OP_CLEAR` sur les 5 attachments (cf. TerrainRenderer.cpp:1505-1520).
La `GeometryPass` (LOAD variant) utilise un autre framebuffer avec
`LOAD_OP_LOAD`. **Si les deux framebuffers ne référencent pas les mêmes
images physiques**, la geometry pass écrasera/clearera ce que le terrain
a écrit.

**À vérifier** : au create de framebuffer dans TerrainRenderer, les
`VkImageView` passés en `views[5]` correspondent-ils aux mêmes images que
celles utilisées par la geometry pass dans le frame graph ?

### 4.3 Depth test reverse-Z (priorité 🟡 basse)
Si la geometry pass utilise reverse-Z (depth clear=0, comparison GREATER)
mais le terrain écrit avec convention standard (depth clear=1, LESS), tous
les fragments terrain se feront depth-fail.

**À vérifier** : `clearValues[4].depthStencil = { 1.0f, 0 }` dans
`TerrainRenderer::Record` (ligne ~1511) vs `GeometryPass::Init`. Si l'un
utilise 0 et l'autre 1, mismatch.

### 4.4 Push-constant `noUserTextures` taille / alignment (priorité 🟢 faible)
La struct C++ `PushConstants` fait 20 octets après ajout de `int32_t
noUserTextures`. La SPV générée devrait aussi faire 20 octets. Si désync
(SPV bloqué à 16 octets), `pc.noUserTextures` sera lu hors de la range
allouée. **Mais ça ne supprimerait que la branche orange, pas le terrain
entier.**

→ Probablement pas la cause du terrain invisible global, juste de l'absence
spécifique de l'orange.

### 4.5 Terrain pass non incluse dans le frame graph (priorité 🟢 faible)
Le `terrainBeforeGeometry` boolean dans `Engine.cpp:1514` gate le
`m_terrain.Record(...)`. Si `m_pipeline->GetGeometryPass().HasLoadPass()`
retourne false, Record n'est jamais appelé. **MAIS** le diagnostic prouve
que Record EST appelé (logs `Record diag` apparaissent), donc ce gate
passe.

→ Probablement pas la cause.

---

## 5. Fichiers clés à investiguer en priorité

| Fichier | Pourquoi |
|---|---|
| `engine/render/LightingPass.cpp` + `lighting.frag/.vert` | Plus probable cause (transformation des GBuffer en pixel lit) |
| `engine/render/GeometryPass.cpp` | LOAD render pass + sync avec terrain |
| `engine/render/terrain/TerrainRenderer.cpp` Record() | Render pass terrain (clear values, framebuffer) |
| `engine/Engine.cpp` lignes 1503-1576 | FrameGraph addPass("Geometry") avec terrain inline |
| `engine/render/DeferredPipeline.cpp` | Init des passes, formats GBuffer |
| `game/data/shaders/lighting.frag` | Pixel shader qui lit GBuffer, fait Lambert+specular |
| `game/data/shaders/terrain.frag` | Écrit l'albedo orange (et normale/ORM/velocity) |
| `engine/render/Camera.cpp` | View matrix (déjà corrigée — vérif que c'est bien la bonne convention) |
| `engine/math/Math.h::PerspectiveVulkan` | Convention proj (Y inversé, Z[0,1]) |
| `engine/math/Frustum.cpp::ExtractFromMatrix` | Convention Gribb-Hartmann (utilise OpenGL `r3+r2` pour Near, devrait être `r2` seul pour Vulkan — pas critique mais à corriger un jour) |

---

## 6. Outils recommandés

### 6.1 RenderDoc (le plus rapide pour isoler)
1. Lancer `lcdlln_world_editor.exe` sous RenderDoc.
2. Capturer une frame après "Créer une nouvelle carte".
3. Inspecter l'EventBrowser :
   - Voir si les drawcalls terrain (`vkCmdDrawIndexed` × 225) sont présents.
   - Pour chaque drawcall, voir Mesh Output → vertices en clip-space → si
     `clip.w > 0` et `|clip.x|, |clip.y| < clip.w` → vertices visibles.
   - Voir Output → quelle image ils écrivent → confirmer GBufferA reçoit
     l'orange (1.0, 0.55, 0.1, 1.0) sur les pixels du terrain.
4. Inspecter la Lighting pass :
   - L'image en entrée (GBufferA) contient-elle l'orange ?
   - L'image en sortie (SceneColor) contient-elle de l'orange ?
   - Si OUI à GBufferA mais NON à SceneColor → bug dans `lighting.frag`.

### 6.2 Vulkan Validation Layers
```powershell
$env:VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation"
.\lcdlln_world_editor.exe -log -console
```
Filtrer la sortie pour des erreurs `VUID-` qui pointeraient sur :
- attachment format mismatch entre render passes
- push constant range overflow
- descriptor set incompatibilities

### 6.3 Hack debug shader temporaire
Dans `lighting.frag`, court-circuit total :
```glsl
// Debug : afficher directement GBufferA (albedo) sans lighting
outColor = texture(uGBufferA, vUV);
return;
```
Si l'orange apparaît → c'est la lighting qui le mange. Sinon → bug encore
plus en amont (geometry pass, depth, framebuffer mismatch).

---

## 7. Reproduction

```bash
git checkout claude/terrain-default-fallback-JlWpU  # ou main si mergée
cd /path/to/repo
# Build CI Windows ou local Visual Studio + tools/compile_game_shaders.ps1
.\lcdlln_world_editor.exe -log
# Cliquer "Créer une nouvelle carte" (zone_id par défaut, taille 256)
# Observer le viewport : doit afficher orange. Affiche en réalité gris/beige.
# Inspecter le log .log généré : chercher [TerrainRenderer] et [WorldEditor].
```

---

## 8. Hors-scope mais à noter

### 8.1 `vertStepWorld = world_size / 1024` codé en dur
Dans `TerrainRenderer.cpp:119`. Hypothèse non-validée que la heightmap
fait 1025×1025. Pour les heightmaps plus petites (256×256 par défaut au
World Editor), seule une fraction du `world_size` est maillée.

→ Pas la cause du terrain invisible (corrigé via `GetActualRenderedExtentX/Z`),
mais à reconsidérer un jour.

### 8.2 `Frustum::ExtractFromMatrix` convention OpenGL pour Near
Ligne 43 utilise `r3 + r2` (OpenGL Z[-1,+1]) au lieu de `r2` seul
(Vulkan Z[0,+1]). Pardonne avec une matrice viewProj correcte mais reste
techniquement incorrect.

→ À corriger en même temps que l'investigation lighting pass.

### 8.3 Régression chunk M09.2
Logs montrent toujours `M09.2 Visible: drawcalls=0` même avec caméra dans
la zone mappée. Probablement frustum cull du système de chunks utilise
une autre matrice/extraction. À investiguer aussi quand on s'attaquera
au vrai pipeline rendu.

---

## 9. État des commits (PR #427)

```
ee181da  fix(camera): view matrix transposee (CAUSE RACINE confirmée)
65fe036  Revert (annulé par ee181da)
66a711c  fix(camera): view matrix transposee (initial, doublon de ee181da)
50470e2  diag(terrain): logs verbose viewProj + plans frustum
5e795e8  fix(editor): bypass frustum cull terrain pour World Editor (workaround)
603d720  fix(editor): camera centree sur etendue REELLE des patches
ab7953e  diag(terrain): one-shot log Record() (patches kept/culled, cam, noUserTex)
2309cf2  feat(editor): terrain par defaut visible et fallback orange (chantier 2026-05-04)
```

---

## 10. Action recommandée pour la prochaine session

1. **Lire** ce fichier en entier.
2. **Lire** `engine/render/LightingPass.cpp` + `game/data/shaders/lighting.frag` pour identifier ce qui transforme GBufferA en sortie finale.
3. **Lire** `engine/Engine.cpp` lignes 1500-1600 (FrameGraph addPass Geometry avec terrain inline) pour comprendre la chaîne render pass.
4. **Lancer RenderDoc** sur un build de la branche pour capturer une frame
   et voir où l'orange disparaît.
5. **Tester** le hack du §6.3 (court-circuit lighting → GBufferA direct
   au swapchain) pour isoler en 2 minutes.

Si après 4. le pixel orange est dans GBufferA mais pas dans SceneColor :
focus sur lighting.frag. Si l'orange n'est pas même dans GBufferA : focus
sur framebuffer/render pass attachment mismatch entre Terrain et Geometry.

**Ne PAS** repartir from scratch sur la matrix view, le frustum, ou le
camera placement — déjà corrigés dans cette PR.

---

## 11. Contexte : pourquoi cette investigation est bloquante

Sans terrain visible :
- Impossible de tester l'édition heightmap (sculpter le sol).
- Impossible de tester la peinture splat / les routes / les arbres.
- Impossible de valider la persistance d'une carte créée.
- Impossible de valider le chargement par le client jeu d'une carte
  exportée par l'éditeur.

→ Bloque tickets World Editor postérieurs ET vérification du flux jeu
in-world (zones, déplacement avatar, collision sol).

---

## 12. Reprise 2026-05-14 — audit pipeline + build de diagnostic

**Branche** : `claude/fix-terrain-invisible` (périmètre `src/client/render/` +
shaders uniquement — disjoint de la session éditeur).

### 12.1 Correction factuelle sur le §2.4

Le §2.4 conclut « matrice viewProj correcte » en ne vérifiant que `clip.w > 0`.
Or le log `proj(centerPatch)=(-0.00,107.31,27.33,27.43)` donne
**NDC.y = 107.31 / 27.43 = 3.91**, soit largement hors de `[-1,+1]` : le patch
central est *hors écran*. Ce n'est PAS un bug en soi (la caméra est pile
au-dessus du centre du terrain et regarde vers l'avant, donc le patch central
est sous le champ de vision), mais la preuve « la matrice est bonne » est
mal interprétée. Recalcul manuel : les patches **vers l'avant** (Z décroissant
depuis la caméra) projettent bien dans `[-1,+1]`. La matrice est donc
*probablement* correcte, mais ce n'est pas démontré par le diagnostic existant.

### 12.2 Mécanisme central identifié

Le symptôme « écran **uniformément** couleur ciel + **aucun orange** » n'est
PAS compatible avec « la lighting pass mange l'albedo » (§4.1) : si l'albedo
orange était dans GBufferA et la profondeur correcte, `lighting.frag` ferait
du PBR sur l'orange → orange ombré visible, pas la couleur du ciel.

`lighting.frag` (≈ ligne 119) :
```glsl
if (depth >= 1.0) { outSceneColorHDR = vec4(skyOut, 1.0); return; } // ne lit jamais GBufferA
```
Le seul état produisant un écran *uniforme* couleur ciel est
**`depth >= 1.0` sur tout l'écran**. La lighting pass sort alors la sky color
partout et **ne regarde même pas l'albedo** (donc l'orange est invisible par
construction, quoi qu'écrive `terrain.frag`).

Vérifié au passage : `SkyPass` a en réalité `depthTestEnable = VK_TRUE`
+ `LESS_OR_EQUAL` (`SkyPass.cpp:104`) — le commentaire `Engine.cpp:3868`
« depthTest=FALSE » est faux/obsolète. SkyPass est donc correctement gated par
la profondeur, ce qui **confirme** que tout dépend de la profondeur écrite
par le terrain.

**Conclusion** : le bug est que le terrain **n'écrit pas de profondeur
`< 1.0`** dans `m_fgDepthId` au moment où la LightingPass l'échantillonne —
soit il ne rasterise aucun fragment, soit ses écritures de depth ne tiennent
pas. L'hypothèse §4.1 est rétrogradée.

### 12.3 Hypothèses re-classées

1. 🔴 **Le terrain ne rasterise aucun fragment** → depth reste à 1.0.
   Winding toujours faux malgré le fix `frontFace=CW` (`TerrainRenderer.cpp:537`),
   OU `terrain.vert` produit un `gl_Position` NaN/hors-range (heightmap GPU
   mal liée/uploadée dans `RebuildWorldEditorTerrainGpu`).
2. 🟠 **Le terrain rasterise la couleur mais pas la profondeur** (ou la depth
   ne persiste pas jusqu'à la LightingPass) — aspect du `VkImageView` depth,
   identité d'image entre le render pass privé du terrain et celui lu par la
   lighting.
3. 🟡 Mismatch framebuffer/barrière (§4.2) — vérifié sur le papier (mêmes
   ResourceId, `storeOp=STORE` partout, layouts qui s'enchaînent) : semble OK,
   à confirmer RenderDoc.
4. 🟢 La lighting pass « mange » l'albedo (§4.1) — **rétrogradée**,
   incompatible avec un écran *uniforme* couleur ciel.

### 12.4 Build de diagnostic ajouté

Ajout d'un mode debug dans `lighting.frag`, piloté par la clé config
`render.lighting_debug_mode` (entier, défaut 0) :

- `0` — rendu normal.
- `1` — `depth < 1.0` → **vert**, `depth >= 1.0` → **rouge**.
  *Écran tout rouge ⇒ le terrain n'écrit aucune profondeur (hyp. 1/2).*
  *Présence de vert ⇒ depth OK, bug en aval.*
- `2` — GBufferA (albedo) brut, sans éclairage.
  *Orange visible ⇒ terrain écrit l'albedo. Pas d'orange ⇒ ne rasterise pas.*
- `3` — profondeur brute en niveaux de gris.

Implémentation : champ `float debugMode` ajouté à `LightingPass::LightParams`
(push-constant 148 → 152 octets) + branches dans `lighting.frag` + lecture
config dans la lambda « Lighting » de `Engine.cpp`.

**À faire côté build Windows** :
1. Recompiler les shaders (`tools/compile_game_shaders.ps1`) — `lighting.frag.spv`
   est un fichier suivi, il sera régénéré.
2. Lancer l'éditeur avec `render.lighting_debug_mode` = 1 puis 2 dans
   `config.json`, coller ici les captures + les logs `[TerrainRenderer]`.
3. Selon le résultat : voir §12.3 pour l'hypothèse à creuser ensuite.

*À valider sur le build Windows — aucune validation graphique possible côté
session Claude (pas d'environnement Vulkan).*

### 12.5 Résultats du build de diagnostic (2026-05-14)

Captures fournies par l'utilisateur :
- **`render.lighting_debug_mode = 1`** → écran **entièrement ROUGE**.
  ⇒ `depth >= 1.0` sur **tout** l'écran : le terrain n'écrit **aucune**
  profondeur. Il ne rasterise **aucun fragment**.
- **`render.lighting_debug_mode = 2`** → écran uniformément gris/beige (pas
  d'orange). Cohérent : `SkyPass` (depthTest=`LESS_OR_EQUAL`, depth==1.0
  partout) remplit GBufferA avec la couleur du ciel sur tout l'écran.

Logs (`log.level` doit être à `Debug` ? — non, les lignes `[TerrainRenderer]`
sortent en `LOG_INFO`/`Render` et apparaissent bien) :
```
[TerrainRenderer] Init OK (15×15 patches, worldSize=10000 heightScale=200)
[WorldEditor] Camera reset: pos=(659.9,180.0,659.9) farZ=5000 actualExt=(2344x2344)
[TerrainRenderer] Record diag: patches total=225 kept=225 culled=0 noUserTex=1
[TerrainRenderer] viewProj row0=(0.7892,0.0000,0.0000,-520.7736)
[TerrainRenderer] viewProj row1=(0.0000,-1.3415,0.4900,-81.8793)
[TerrainRenderer] viewProj row2=(0.0000,-0.3429,-0.9394,681.5039)
[TerrainRenderer] viewProj row3=(0.0000,-0.3429,-0.9394,681.5902)
[TerrainRenderer] proj(cam-Z100)=(0.00,-49.00,93.84,93.94)   <- devant, w>0
[TerrainRenderer] proj(centerPatch=(660,100,660))=(0.00,107.31,27.33,27.43)
```

Analyse :
- La matrice viewProj est **correcte**. `row2 ≈ row3` est *normal* ici (near=0.1,
  far=5000 ⇒ `f/(f-n) ≈ 1`), ce n'est pas le bug pré-existant.
- `Record()` est appelé, 225 patches soumis (cull CPU bypassé).
- Recalcul manuel : un patch « vers l'avant » à `(660, 100, 0)` projette en
  NDC `(0, -0.33, 0.9999)` — **pleinement à l'écran**, profondeur valide.
- Pourtant rien ne rasterise et `depth` reste à 1.0.

⇒ Les triangles sont **on-screen + depth valide**, mais aucun fragment n'est
produit. Le seul mécanisme de rejet restant est le **backface culling**. Le
maillage est généré « CCW vu de dessus » (`TerrainMesh.cpp:146,166`,
`bl,tl,tr` / `bl,tr,br`) et le « fix » `frontFace=VK_FRONT_FACE_CLOCKWISE`
(`TerrainRenderer.cpp`) **est très probablement dans le mauvais sens**. Le
commentaire « confirmé avec cullMode=NONE : terrain visible » décrit un état
antérieur — soit le fix n'a jamais été testé avec `cullMode=BACK`, soit un
commit ultérieur a re-modifié la convention.

### 12.6 Build de diagnostic n°2 — sélecteur cull/winding

Ajout de la clé config `render.terrain_debug_cull` (lue par
`TerrainRenderer::Init`, donc rejouée à chaque « Nouvelle carte ») :

- `0` (défaut) — `cullMode=BACK`, `frontFace=CW` (comportement actuel).
- `1` — `cullMode=NONE` → **si le terrain APPARAÎT, le winding est confirmé
  comme cause racine** (les triangles étaient tous backface-cull).
- `2` — `cullMode=BACK`, `frontFace=CCW` → **le correctif suspecté**.
- `3` — `cullMode=FRONT` → vérification symétrique.

Avantage : un seul build, l'utilisateur teste les 4 cas en éditant
`config.json` + « Nouvelle carte ». Pas besoin de toucher `lighting.frag`
(remettre `render.lighting_debug_mode` à `0`).

**À faire côté build Windows** : recompiler le `.exe`, puis tester
`render.terrain_debug_cull` = 1 puis 2. Résultat attendu : 1 et 2 rendent le
terrain visible ⇒ on rend permanent le cas 2 (`frontFace=CCW`) et on retire
le sélecteur + le mode debug lighting.

*À valider sur le build Windows.*

---

*Fin du document de passation.*
