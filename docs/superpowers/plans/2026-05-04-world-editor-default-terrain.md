# World Editor — terrain par défaut visible et fallback orange — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Corriger l'invisibilité du terrain à la création/chargement d'une carte dans `lcdlln_world_editor.exe` (reset caméra centré sur le terrain) et ajouter un fallback visuel orange tant qu'aucune texture utilisateur n'a été assignée. Aucun impact côté client jeu ni serveur.

**Architecture:** Petite extension du push-constant terrain (`int noUserTextures` ajouté en queue), branche shader unique en début de calcul albedo, recalcul du flag à chaque rebuild GPU dans `Engine::RebuildWorldEditorTerrainGpu`. Le repositionnement caméra existant est corrigé (caméra placée au centre du terrain plutôt que derrière le bord) et déplacé sous garde `m_worldEditorExe` au lieu de `m_editorMode` (qui peut être null). Aucun changement au format des fichiers `.r16h`/`.slap`/`.grms`/`.lcdlln_edit.json`.

**Tech Stack:** C++ (engine), GLSL (shaders Vulkan compilés via `glslangValidator` / `tools/compile_game_shaders.ps1`), CMake build via presets Windows. Pas de framework de tests C++ dans le repo (M40.1 TODO) → validation manuelle uniquement.

**Spec source:** `docs/superpowers/specs/2026-05-04-world-editor-default-terrain-design.md` (commits `24c53ba` + `ec2fdce`).

---

## Task 1: Étendre la struct CPU `PushConstants` du `TerrainRenderer`

**Files:**
- Modify: `engine/render/terrain/TerrainRenderer.h:177-190` (struct `PushConstants` + commentaire d'en-tête)

- [ ] **Step 1.1: Mettre à jour le commentaire d'en-tête de la struct**

Remplace les lignes 178-183 (commentaire et début de la struct) par :

```cpp
        // ── Push constants ────────────────────────────────────────────────────────
        // All stages, 20 bytes total.
        // offset  0: float patchOriginX
        // offset  4: float patchOriginZ
        // offset  8: float morphFactor   [0,1]
        // offset 12: int   lodLevel      [0, kTerrainLodCount-1]
        // offset 16: int   noUserTextures (0 = rendu normal, 1 = fallback orange World Editor)
        struct PushConstants
        {
            float   patchOriginX   = 0.0f;
            float   patchOriginZ   = 0.0f;
            float   morphFactor    = 0.0f;
            int32_t lodLevel       = 0;
            int32_t noUserTextures = 0;
        };
```

- [ ] **Step 1.2: Ajouter méthode publique et membre privé pour piloter le flag**

Dans `TerrainRenderer.h`, après la déclaration de `SampleHeightAtWorldXZ` (ligne 175), juste avant `private:` (ligne 177), insère :

```cpp
        /// Active le fallback visuel orange (utilise par lcdlln_world_editor.exe quand
        /// le document de carte n'a aucune texture utilisateur assignee aux couches splat).
        /// Le client jeu n'appelle jamais cette methode -> flag reste a false par defaut.
        void SetNoUserTexturesFallback(bool enabled) { m_noUserTextures = enabled; }
```

Puis, à la fin des membres privés de la classe (avant la fermeture `};` de la classe), ajoute :

```cpp
        // ── World Editor fallback (Sujet 2 du design 2026-05-04) ──────────────────
        // Quand true, le push-constant noUserTextures = 1 est pousse a chaque draw
        // -> le shader fragment ecrit un albedo orange uni a la place du splatting.
        bool m_noUserTextures = false;
```

Trouve l'emplacement exact en cherchant la dernière ligne `bool m_xxx = ...;` ou similaire dans la zone privée (avant `};` final) — l'ordre exact n'a pas d'importance, c'est juste un membre.

- [ ] **Step 1.3: Compiler pour vérifier qu'on n'a pas cassé la build**

Run :
```bash
cmake --build --preset windows-release --target engine -- /m
```
Expected : compilation OK (warnings éventuels ignorés). Aucune erreur. Si la cible `engine` n'existe pas seule, build full :
```bash
cmake --build --preset windows-release -- /m
```

- [ ] **Step 1.4: Commit**

```bash
git add engine/render/terrain/TerrainRenderer.h
git commit -m "$(cat <<'EOF'
feat(terrain): extend PushConstants with noUserTextures flag

Ajoute le champ int32 noUserTextures au push-constant terrain (offset 16,
total 20 bytes). Setter SetNoUserTexturesFallback expose au World Editor
pour activer le fallback orange quand aucune texture utilisateur n'est
assignee aux couches splat. Comportement client jeu strictement inchange
(flag reste false par defaut).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Pousser la nouvelle valeur du push-constant dans `Record()`

**Files:**
- Modify: `engine/render/terrain/TerrainRenderer.cpp:1562-1570` (bloc `PushConstants pc{}; ...`)

- [ ] **Step 2.1: Ajouter le set du flag avant le `vkCmdPushConstants`**

Dans `TerrainRenderer.cpp`, repère le bloc lignes 1562-1570 :

```cpp
                PushConstants pc{};
                pc.patchOriginX = p.originX;
                pc.patchOriginZ = p.originZ;
                pc.morphFactor  = entry.morphFactor;
                pc.lodLevel     = static_cast<int32_t>(lod);

                vkCmdPushConstants(cmd, m_pipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(PushConstants), &pc);
```

Remplace-le par :

```cpp
                PushConstants pc{};
                pc.patchOriginX   = p.originX;
                pc.patchOriginZ   = p.originZ;
                pc.morphFactor    = entry.morphFactor;
                pc.lodLevel       = static_cast<int32_t>(lod);
                pc.noUserTextures = m_noUserTextures ? 1 : 0;

                vkCmdPushConstants(cmd, m_pipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(PushConstants), &pc);
```

`sizeof(PushConstants)` reflète automatiquement les 20 bytes (4*float + 1*int32 alignés sans padding sur ABI standard) ; `pcRange.size = sizeof(PushConstants)` à la création du pipeline layout (ligne 429) est lui aussi automatique. Aucune autre ligne à toucher côté C++ pour la taille.

- [ ] **Step 2.2: Compiler pour vérifier**

Run :
```bash
cmake --build --preset windows-release -- /m
```
Expected : compilation OK.

- [ ] **Step 2.3: Commit**

```bash
git add engine/render/terrain/TerrainRenderer.cpp
git commit -m "$(cat <<'EOF'
feat(terrain): push noUserTextures flag per draw call

Propage m_noUserTextures dans les push constants a chaque vkCmdDrawIndexed.
La taille du PC range (20 bytes) est calculee via sizeof(PushConstants)
donc aucune autre modification du pipeline layout n'est necessaire.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Étendre le push-constant côté shader vertex

**Files:**
- Modify: `game/data/shaders/terrain.vert:36-41` (bloc `layout(push_constant) uniform PC`)

- [ ] **Step 3.1: Ajouter le champ `noUserTextures` au PC vertex**

Dans `terrain.vert`, repère lignes 36-41 :

```glsl
layout(push_constant) uniform PC {
    float patchOriginX;
    float patchOriginZ;
    float morphFactor;
    int   lodLevel;
} pc;
```

Remplace par :

```glsl
layout(push_constant) uniform PC {
    float patchOriginX;
    float patchOriginZ;
    float morphFactor;
    int   lodLevel;
    int   noUserTextures;  // 0 = rendu normal, 1 = fallback orange World Editor
} pc;
```

Le vertex n'utilise pas le flag mais le bloc PC doit avoir la **même structure** que côté fragment, sinon le validator Vulkan refusera le link de pipeline.

- [ ] **Step 3.2: Commit (le shader vertex compile dans le step 4 avec le frag)**

```bash
git add game/data/shaders/terrain.vert
git commit -m "$(cat <<'EOF'
feat(terrain): mirror noUserTextures field in vertex push constant

Ajoute le champ int noUserTextures au bloc PC du shader vertex pour aligner
le layout avec terrain.frag (modifie au step suivant). Le vertex ne lit pas
ce champ — c'est juste pour valider le pipeline Vulkan.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Brancher la sortie orange dans le shader fragment

**Files:**
- Modify: `game/data/shaders/terrain.frag:43-49` (bloc `layout(push_constant) uniform PC`)
- Modify: `game/data/shaders/terrain.frag:113-122` (bloc albedo `triplanarSample` + `outAlbedo`)

- [ ] **Step 4.1: Ajouter `noUserTextures` au PC fragment**

Dans `terrain.frag`, lignes 43-49 actuellement :

```glsl
// ── Push constants ────────────────────────────────────────────────────────────
layout(push_constant) uniform PC {
    float patchOriginX;
    float patchOriginZ;
    float morphFactor;
    int   lodLevel;
} pc;
```

Remplace par :

```glsl
// ── Push constants ────────────────────────────────────────────────────────────
layout(push_constant) uniform PC {
    float patchOriginX;
    float patchOriginZ;
    float morphFactor;
    int   lodLevel;
    int   noUserTextures;  // 0 = rendu normal, 1 = orange uni (World Editor sans textures)
} pc;
```

- [ ] **Step 4.2: Brancher la sortie orange dans `main()`**

Dans `terrain.frag`, repère le bloc lignes 113-122 :

```glsl
    // ── Sample and blend albedo (triplanar per layer) ─────────────────────────
    vec3 albedo = vec3(0.0);
    albedo += triplanarSample(uAlbedoArray, 0.0, vWorldPos, macroN, tilingGrass).rgb * w.r;
    albedo += triplanarSample(uAlbedoArray, 1.0, vWorldPos, macroN, tilingDirt ).rgb * w.g;
    albedo += triplanarSample(uAlbedoArray, 2.0, vWorldPos, macroN, tilingRock ).rgb * w.b;
    albedo += triplanarSample(uAlbedoArray, 3.0, vWorldPos, macroN, tilingSnow ).rgb * w.a;
    float grassAmt = texture(uGrassMask, vUV).r * ubo.terrainParams.w;
    vec3 grassHue = vec3(0.88, 1.04, 0.82);
    albedo = mix(albedo, albedo * grassHue, clamp(grassAmt, 0.0, 1.0));
    outAlbedo = vec4(albedo, 1.0);
```

Remplace-le intégralement par :

```glsl
    // ── Sample and blend albedo (triplanar per layer) ─────────────────────────
    // Cas World Editor sans texture utilisateur : on court-circuite tout le splatting
    // et on ecrit un orange vif uni. Les normales (outNormal) restent calculees
    // normalement pour que la lighting pass applique le shading Lambert et garde
    // le relief lisible. Cas par defaut (pc.noUserTextures == 0) : rendu normal.
    if (pc.noUserTextures != 0)
    {
        outAlbedo = vec4(1.0, 0.55, 0.1, 1.0);  // orange vif (sec. 5.2 du design 2026-05-04)
    }
    else
    {
        vec3 albedo = vec3(0.0);
        albedo += triplanarSample(uAlbedoArray, 0.0, vWorldPos, macroN, tilingGrass).rgb * w.r;
        albedo += triplanarSample(uAlbedoArray, 1.0, vWorldPos, macroN, tilingDirt ).rgb * w.g;
        albedo += triplanarSample(uAlbedoArray, 2.0, vWorldPos, macroN, tilingRock ).rgb * w.b;
        albedo += triplanarSample(uAlbedoArray, 3.0, vWorldPos, macroN, tilingSnow ).rgb * w.a;
        float grassAmt = texture(uGrassMask, vUV).r * ubo.terrainParams.w;
        vec3 grassHue = vec3(0.88, 1.04, 0.82);
        albedo = mix(albedo, albedo * grassHue, clamp(grassAmt, 0.0, 1.0));
        outAlbedo = vec4(albedo, 1.0);
    }
```

Important : NE PAS mettre les sorties `outNormal` / `outORM` / `outVelocity` dans la branche `if`. Elles continuent d'être écrites de manière inchangée plus loin dans le fichier (lignes ~124+). Le shading Lambert sera appliqué par la lighting pass.

- [ ] **Step 4.3: Compiler les shaders en SPIR-V**

Run :
```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/compile_game_shaders.ps1
```
Expected : message `[compile_game_shaders] OK terrain.vert -> ...terrain.vert.spv` et idem pour `terrain.frag`. Pas d'erreur "ERROR:" dans la sortie. Les fichiers `game/data/shaders/terrain.vert.spv` et `terrain.frag.spv` sont mis à jour.

Si `glslangValidator` est introuvable, le script lève une exception explicite ; dans ce cas, vérifie que `VULKAN_SDK` est défini et pointe vers une installation valide.

- [ ] **Step 4.4: Commit shader frag + .spv recompilés**

Si le projet versionne les `.spv` (vérifie avec `git status`), les inclure :

```bash
git add game/data/shaders/terrain.frag game/data/shaders/terrain.vert.spv game/data/shaders/terrain.frag.spv
git commit -m "$(cat <<'EOF'
feat(terrain): add orange fallback branch in fragment shader

Quand pc.noUserTextures != 0, ecrit outAlbedo = vec3(1.0, 0.55, 0.1) au
lieu du splatting triplanaire. outNormal continue d'etre ecrit normalement
-> la lighting pass applique le shading Lambert et le relief reste lisible.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

Si les `.spv` ne sont pas versionnés (ils sont régénérés au build), ne stage que le `.frag` :
```bash
git add game/data/shaders/terrain.frag
git commit ...  # même message
```

Vérifie avec `git ls-files game/data/shaders/*.spv | head` si des `.spv` sont déjà tracés.

---

## Task 5: Détecter `noUserTextures` et corriger la caméra dans `RebuildWorldEditorTerrainGpu`

**Files:**
- Modify: `engine/Engine.cpp:4736-4837` (corps de la fonction `Engine::RebuildWorldEditorTerrainGpu`)

- [ ] **Step 5.1: Calculer le flag `noUserTextures` après l'init du terrain**

Dans `Engine.cpp`, repère le bloc juste après le `if (!m_worldEditorTerrainTools.Init(...))` qui se termine ligne 4802, et juste avant `m_terrain.InvalidateFramebufferCache(device);` (ligne 4803).

Insère immédiatement avant `m_terrain.InvalidateFramebufferCache(device);` :

```cpp
		// Sujet 2 du design 2026-05-04 : fallback visuel orange tant qu'aucune
		// texture utilisateur n'est assignee aux couches splat.
		// Detection : les 4 entrees de splatLayerTextureRefs doivent etre vides.
		// Garde m_worldEditorExe -> le client jeu n'active jamais ce fallback.
		bool noUserTextures = false;
		if (m_worldEditorExe && m_worldEditorSession)
		{
			noUserTextures = true;
			const auto& refs = m_worldEditorSession->Doc().splatLayerTextureRefs;
			for (const std::string& r : refs)
			{
				if (!r.empty()) { noUserTextures = false; break; }
			}
		}
		m_terrain.SetNoUserTexturesFallback(noUserTextures);
		LOG_INFO(Render, "[WorldEditor] noUserTextures fallback = {}", noUserTextures ? "ON" : "off");
```

- [ ] **Step 5.2: Corriger le bloc de repositionnement caméra**

Repère le bloc lignes 4805-4836 (`// Repositionne la camera ...` jusqu'au `}` qui ferme `if (m_editorMode)`).

Remplace **intégralement** ce bloc par :

```cpp
		// Sujet 1 du design 2026-05-04 : reset camera centre sur le terrain.
		// Avant : caméra a (centerX, midY+50, centerZ + ws*0.25) avec pitch 0.5 rad
		//         -> pour ws=10km, regard heurte le sol bien avant le terrain
		//         -> terrain hors champ.
		// Maintenant : camera AU CENTRE du terrain, altitude midY+80, pitch ~20deg,
		// farZ adapte a ws. Garde m_worldEditorExe (pas m_editorMode qui peut etre null).
		if (m_worldEditorExe)
		{
			const float ox = m_terrain.GetTerrainOriginX();
			const float oz = m_terrain.GetTerrainOriginZ();
			const float ws = m_terrain.GetTerrainWorldSize();
			const float hs = m_terrain.GetHeightScale();
			const float centerX     = ox + ws * 0.5f;
			const float centerZ     = oz + ws * 0.5f;
			const float midGroundY  = hs * 0.5f;          // heightmap mid-value -> sol moyen
			const float camAltitude = midGroundY + 80.0f; // marge confortable au-dessus du sol

			engine::render::Camera reset;
			reset.position.x = centerX;
			reset.position.y = camAltitude;
			reset.position.z = centerZ;          // au centre, pas a l'exterieur
			reset.yaw   = 0.0f;
			reset.pitch = 0.35f;                 // ~20 deg vers le bas, vue degagee
			reset.fovYDeg = 70.0f;
			reset.aspect  = static_cast<float>(std::max(1, m_width)) / static_cast<float>(std::max(1, m_height));
			reset.nearZ = 0.1f;
			reset.farZ  = std::max(5000.0f, ws * 1.5f);  // adapte aux grands terrains
			m_renderStates[0].camera = reset;
			m_renderStates[1].camera = reset;
			LOG_INFO(Render,
				"[WorldEditor] Camera reset: pos=({:.1f},{:.1f},{:.1f}) farZ={:.0f} ws={:.0f}",
				reset.position.x, reset.position.y, reset.position.z, reset.farZ, ws);
		}
```

- [ ] **Step 5.3: Compiler pour vérifier**

Run :
```bash
cmake --build --preset windows-release -- /m
```
Expected : compilation OK.

- [ ] **Step 5.4: Commit**

```bash
git add engine/Engine.cpp
git commit -m "$(cat <<'EOF'
fix(world-editor): camera centree sur terrain + detection no-textures

Sujet 1 (caméra) : remplace le reset camera (centerZ + ws*0.25) qui placait
la vue HORS du terrain pour ws=10km par un placement AU CENTRE, altitude
mid_ground_y + 80m, pitch 0.35rad, farZ = max(5000, ws*1.5). Garde
m_worldEditorExe au lieu de m_editorMode (qui peut etre null si init fail).

Sujet 2 (fallback orange) : detecte si splatLayerTextureRefs est entierement
vide -> active SetNoUserTexturesFallback(true) sur m_terrain. Le client jeu
n'execute jamais ce path (garde m_worldEditorExe).

Resout l'invisibilite du terrain a "Creer une nouvelle carte" et "Charger
la carte selectionnee" dans lcdlln_world_editor.exe.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Validation manuelle (golden path)

Pas de test C++ automatisé (pas d'infra). Validation entièrement manuelle. Cette tâche **ne produit pas de commit** mais documente la PR.

**Files:**
- (Aucun fichier modifié — capture d'écran à joindre à la description PR)

- [ ] **Step 6.1: Build complet release**

Run :
```bash
cmake --build --preset windows-release -- /m
```
Expected : compilation OK pour les cibles `lcdlln`, `lcdlln_world_editor`, `engine`.

- [ ] **Step 6.2: Lancer le World Editor depuis la racine du dépôt**

Run :
```bash
./build/windows-release/pkg/world_editor/lcdlln_world_editor.exe
```
(Ajuste le chemin si la sortie de build diffère sur ta machine ; le binaire doit avoir `game/data` accessible relatif à son cwd ou à un ancêtre.)

Expected : la fenêtre s'ouvre, mode éditeur actif, panneaux ImGui visibles. Aucun terrain au démarrage (comportement attendu).

- [ ] **Step 6.3: Cliquer sur « Creer une nouvelle carte »**

Dans la fenêtre ImGui « Carte courante / Nouvelle carte » :
- Laisse les valeurs par défaut (zone_id = `untitled_zone`, taille = 256).
- Clique sur **« Creer une nouvelle carte »**.

Expected :
- Statut bar bas : `Nouvelle carte OK - world_editor/maps/untitled_zone/height.r16h`.
- Viewport 3D : terrain plat **orange vif** lisible, occupant la majeure partie de l'écran, grille blanche superposée par-dessus.
- Logs (`logs/lcdlln-*.log` ou stdout) : `[WorldEditor] Camera reset: pos=(...) farZ=... ws=...` + `[WorldEditor] noUserTextures fallback = ON`.

- [ ] **Step 6.4: Cliquer sur « Charger la carte selectionnee » sur une carte existante**

Si une autre carte existe sous `world_editor/maps/`, sélectionne-la dans la liste « Cartes disponibles » et clique sur **« Charger la carte selectionnee »**.

Expected : viewport repositionné au centre de cette carte, terrain visible (orange si pas de textures, splatting normal sinon).

- [ ] **Step 6.5: Assigner une texture à la couche herbe (test de bascule)**

Dans la fenêtre ImGui « Outils » → mode « Splat » → assigne une texture importée à la couche herbe (suivre l'UI existante). Puis clique « Recharger terrain GPU ».

Expected : l'orange disparaît, le rendu splatting normal apparaît. Logs : `[WorldEditor] noUserTextures fallback = off`.

- [ ] **Step 6.6: Test de non-régression client jeu**

Run :
```bash
./build/windows-release/pkg/client/lcdlln.exe
```
Connecte-toi normalement, entre dans le monde.

Expected : le terrain s'affiche **comme avant** (textures officielles, pas d'orange). Aucun log `[WorldEditor]`. Aucun changement visuel par rapport au baseline.

- [ ] **Step 6.7: Capture d'écran**

Prends 3 captures d'écran à joindre à la PR :
1. World Editor après « Creer une nouvelle carte » (terrain orange + grille).
2. World Editor après assignation d'une texture (rendu normal).
3. Client jeu in-world (terrain inchangé, non-régression).

Sauvegarde-les sous `docs/screenshots/2026-05-04-world-editor-terrain-{1,2,3}.png` (le dossier est créé s'il n'existe pas) et stage-les dans un commit séparé :

```bash
mkdir -p docs/screenshots
# (déposer les 3 png)
git add docs/screenshots/2026-05-04-world-editor-terrain-*.png
git commit -m "$(cat <<'EOF'
docs(screenshots): captures golden path world editor terrain par defaut

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

Si la validation échoue à une étape, **NE PAS commit** les screenshots — corriger d'abord et reprendre Task 5 ou la branche shader.

---

## Task 7: Mettre à jour `CODEBASE_MAP.md`

**Files:**
- Modify: `CODEBASE_MAP.md` (insertion d'une nouvelle section 15 après la fin de la section 14)

- [ ] **Step 7.1: Localiser la fin de la section 14**

Run :
```bash
grep -n "^## 14\|^## 15\|^---" CODEBASE_MAP.md | head -20
```

Repère la dernière ligne de la section 14 (avant le prochain titre `## NN` ou la fin du fichier). Le contenu actuel s'arrête vers la fin du fichier (~ligne 698).

- [ ] **Step 7.2: Insérer la nouvelle section 15**

Ouvre `CODEBASE_MAP.md` et ajoute, juste avant la section finale "Réflexes rapides" / index FAQ (la dernière section thématique à indexation `^## NN.`), le bloc suivant :

```markdown
## 15. Éditeur monde — création/chargement de carte (chantier 2026-05-04)

Le binaire `lcdlln_world_editor.exe` partage l'engine avec le client de jeu mais
a son propre flux de gestion de carte. La création (`ActionNewMap`) et le
chargement (`ActionLoadMapByZoneId`) passent tous deux par
`Engine::RebuildWorldEditorTerrainGpu()` qui détruit et réinitialise `m_terrain`
+ `TerrainEditingTools`.

### Carte par défaut

`WorldEditorSession::ActionNewMap` génère sous
`<paths.content>/world_editor/maps/<zone_id>/` :
- `height.r16h` — heightmap plate, valeur normalisée 32768 (= mi-hauteur de
  `terrain.height_scale`).
- `splat.slap` — 1024×1024, R=255 (100 % couche herbe), autres couches à 0.
- `grass.grms` — masque herbe à zéros.
- `map.lcdlln_edit.json` — document d'édition versionné (`kFormatVersion = 1`).

### Reset caméra (chantier 2026-05-04)

Après chaque `RebuildWorldEditorTerrainGpu`, la caméra est repositionnée **au
centre du terrain**, altitude `mid_ground_y + 80 m`, pitch `0.35 rad` (~20° vers
le bas), `farZ = max(5000, ws * 1.5)`. Garantit la visibilité du terrain quel
que soit `terrain.world_size`. Condition de garde : `if (m_worldEditorExe)` (et
non `m_editorMode` qui peut être null).

### Fallback visuel orange (chantier 2026-05-04)

Lorsque les 4 entrées de `splatLayerTextureRefs` du document sont vides (carte
fraîchement créée, aucune texture utilisateur posée), `Engine::RebuildWorldEditorTerrainGpu`
appelle `m_terrain.SetNoUserTexturesFallback(true)`. Le push-constant terrain
`noUserTextures = 1` fait écrire au shader fragment `outAlbedo = vec3(1.0, 0.55, 0.1)`
(orange vif). Les normales macro restent inchangées → la lighting pass applique
naturellement le shading Lambert et le relief reste lisible. Dès qu'une texture
est assignée à au moins une couche, le rebuild bascule sur le rendu normal.
**Le client jeu n'active jamais ce fallback** (gardé par `m_worldEditorExe`).

### Fichiers clés

| Fichier | Rôle |
|---|---|
| `engine/editor/WorldEditorSession.cpp` (`ActionNewMap`) | Génère heightmap plate + splat + grass + JSON. |
| `engine/Engine.cpp` (`RebuildWorldEditorTerrainGpu`) | Reset caméra centré, détection `noUserTextures`. |
| `engine/render/terrain/TerrainRenderer.h/.cpp` | `SetNoUserTexturesFallback`, push-constant `noUserTextures`. |
| `game/data/shaders/terrain.vert` / `terrain.frag` | Champ `int noUserTextures` au PC, branche orange en fragment. |
```

- [ ] **Step 7.3: Mettre à jour l'index "Réflexes rapides" (si présent)**

Si `CODEBASE_MAP.md` se termine par une liste « Réflexes rapides » ou « Je veux X → fichier Y », ajouter une ligne :

```markdown
8. **Je veux comprendre pourquoi le terrain est orange dans l'éditeur** → section 15 (fallback visuel sans texture utilisateur).
```

(Numéro à adapter au dernier item existant.)

- [ ] **Step 7.4: Commit**

```bash
git add CODEBASE_MAP.md
git commit -m "$(cat <<'EOF'
docs(map): add section 15 world editor map creation flow

Documente le flux de creation/chargement de carte dans
lcdlln_world_editor.exe, le reset camera centre et le fallback orange
(chantier 2026-05-04).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Mettre à jour `docs/terrain_et_world_editor.md`

**Files:**
- Modify: `docs/terrain_et_world_editor.md` (ajout dans la section "World Editor (Windows)")

- [ ] **Step 8.1: Repérer la fin de la section "World Editor (Windows)"**

Run :
```bash
grep -n "^## " docs/terrain_et_world_editor.md
```

Repère la ligne de la section "## Limites / pistes" (qui suit "## World Editor (Windows)"). Insère le nouveau contenu **juste avant** ce titre.

- [ ] **Step 8.2: Insérer les paragraphes**

Insère juste avant la ligne `## Limites / pistes` :

```markdown
- **Reset caméra à la création/chargement (2026-05-04)** : le repositionnement caméra dans `RebuildWorldEditorTerrainGpu` place la vue **au centre du terrain**, altitude `mid_ground_y + 80 m`, pitch `~0.35 rad` (≈ 20° vers le bas), `farZ = max(5000, ws * 1.5)`. Le calcul est gardé par `m_worldEditorExe` (et non `m_editorMode`, qui peut être null). Avant ce fix, la caméra était positionnée à 25 % de `world_size` derrière le bord arrière du terrain et le regardait sous un angle insuffisant → terrain hors champ pour les zones de 10 km.
- **Fallback visuel sans texture (2026-05-04)** : tant qu'aucune couche splat n'a de texture utilisateur assignée (`splatLayerTextureRefs` toutes vides), le terrain est rendu **orange vif uni** (`vec3(1.0, 0.55, 0.1)`) avec shading Lambert préservé via les normales. Push-constant terrain `noUserTextures = 1`. Permet d'identifier au premier coup d'œil un terrain en cours de construction. Bascule automatique sur le rendu normal dès qu'une texture utilisateur est appliquée à au moins une couche (au prochain rebuild GPU). Activé uniquement dans `lcdlln_world_editor.exe` ; le client jeu n'active jamais ce mode.
```

- [ ] **Step 8.3: Commit**

```bash
git add docs/terrain_et_world_editor.md
git commit -m "$(cat <<'EOF'
docs(terrain): document camera reset + orange fallback in world editor

Ajoute deux paragraphes en fin de section World Editor (Windows) decrivant
le fix camera centre et le fallback visuel orange (chantier 2026-05-04).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: Ouvrir la PR

**Files:** aucun changement code

- [ ] **Step 9.1: Vérifier l'état avant push**

Run :
```bash
git log --oneline main..HEAD
git status
```
Expected : 7-8 commits sur la branche, working tree clean.

- [ ] **Step 9.2: Push et création de la PR**

Run :
```bash
git push -u origin HEAD
gh pr create --title "fix(world-editor): terrain visible par defaut + fallback orange" --body "$(cat <<'EOF'
## Summary

- Corrige l'invisibilité du terrain à `Creer une nouvelle carte` / `Charger la carte selectionnee` dans `lcdlln_world_editor.exe` : reset caméra centré sur le terrain (vs. position derrière le bord), `farZ` adaptatif aux grandes zones, garde `m_worldEditorExe` au lieu de `m_editorMode` (qui peut être null).
- Ajoute un fallback visuel **orange vif** au terrain quand aucune texture utilisateur n'est assignée aux couches splat. Push-constant `noUserTextures` dans le shader terrain. Bascule automatique vers le rendu normal dès qu'une texture est posée. Client jeu strictement inchangé.
- Met à jour `CODEBASE_MAP.md` (nouvelle section 15) et `docs/terrain_et_world_editor.md`.

## Spec et plan

- Design : `docs/superpowers/specs/2026-05-04-world-editor-default-terrain-design.md`
- Plan : `docs/superpowers/plans/2026-05-04-world-editor-default-terrain.md`

## Test plan

- [x] World Editor → "Creer une nouvelle carte" → terrain orange visible, grille blanche par-dessus, statut "Nouvelle carte OK"
- [x] World Editor → "Charger la carte selectionnee" → caméra repositionnée, terrain visible
- [x] Assigner une texture à une couche splat puis "Recharger terrain GPU" → terrain bascule sur rendu normal
- [x] Client jeu (`lcdlln.exe`) connecté au shard → terrain inchangé, pas d'orange (non-régression)
- [x] Logs `[WorldEditor] Camera reset: pos=(...) farZ=... ws=...` + `[WorldEditor] noUserTextures fallback = ON/off` au create/load

Captures d'écran : voir `docs/screenshots/2026-05-04-world-editor-terrain-{1,2,3}.png`.

**Déploiement** : ✅ client uniquement, pas de redéploiement serveur.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

Expected : PR créée, URL retournée. La poster en réponse à l'utilisateur.

---

## File Structure (récapitulatif)

| Fichier | Type | Responsabilité |
|---|---|---|
| `engine/render/terrain/TerrainRenderer.h` | Modify | Struct `PushConstants` étendue, méthode `SetNoUserTexturesFallback`, membre `m_noUserTextures`. |
| `engine/render/terrain/TerrainRenderer.cpp` | Modify | Set du flag dans le bloc `vkCmdPushConstants` du `Record()`. |
| `game/data/shaders/terrain.vert` | Modify | Champ `int noUserTextures` au PC (alignement layout). |
| `game/data/shaders/terrain.frag` | Modify | Champ `int noUserTextures` au PC + branche orange en `main()`. |
| `engine/Engine.cpp` | Modify | Détection `noUserTextures` + reset caméra centré dans `RebuildWorldEditorTerrainGpu`. |
| `CODEBASE_MAP.md` | Modify | Nouvelle section 15. |
| `docs/terrain_et_world_editor.md` | Modify | Deux paragraphes en fin de section World Editor. |
| `docs/screenshots/2026-05-04-world-editor-terrain-*.png` | Create | 3 captures pour la PR. |

Aucun fichier supprimé. Aucun nouveau fichier source. Aucun changement aux formats de fichiers de carte (`.r16h`, `.slap`, `.grms`, `.lcdlln_edit.json`). Aucun changement serveur (master/shard) ni DB.

**Déploiement final** : ✅ client uniquement, pas de redéploiement serveur.
