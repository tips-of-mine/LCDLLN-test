# B.1 (Locomotion state machine) — Statut & bugs connus

**Branche** : `claude/locomotion-b1` (commit de tête : `91d7279` au 2026-05-20)
**Spec** : [docs/superpowers/specs/2026-05-18-locomotion-state-machine-design.md](../specs/2026-05-18-locomotion-state-machine-design.md)
**Plan** : [docs/superpowers/plans/2026-05-18-locomotion-state-machine.md](../plans/2026-05-18-locomotion-state-machine.md)

## Objectif B.1

Locomotion state machine production-ready en local, qui transforme la state machine 3-états + hard-cuts livrée par A en :
- 7 états (Idle / StartWalking / Walk / WalkBack / Run / Jump / Fall / Land) — 8 avec WalkBack ajouté post-spec
- Crossfade lissé entre clips (lerp/slerp TRS sur 150 ms)
- CharacterController + TerrainCollider branchés (capsule + sweep heightmap)
- OrbitalCameraController rétrogradé en caméra pure (target = position CC)
- 4 nouveaux clips Mixamo (Run / Jump / Fall / Land) + 1 ajouté post-test (WalkBack)
- Suppression du bobY synthétique de A

## Ce qui a été livré

### Tasks 1→14 du plan (livrées dans cet ordre)

1. **Math helpers** (commit `285ef28`) — `Mat4::Identity` / `Translate` / `RotateY` static.
2. **ComposeTRS public** (`c96114c`) — exposition de `AnimationSampler::ComposeTRS`.
3. **AnimationCrossfade** (`8360774`) — `Play(clip, loops, now)` / `Sample(skel, now)`, blend TRS, `kCrossfadeDuration=0.15s`.
4. **LoadClipsAnimOnly** (`1d660f0`) — variante de chargement pour clips Mixamo sans `cgltf_skin` (retarget par nom de bone).
5. **TerrainCollider** (`969ceb8`) — implémentation `IWorldCollider` qui interroge la heightmap via `TerrainRenderer::SampleHeightAtWorldXZ`.
6. **Tests TerrainCollider** (`2a88536`) — 4 cas de base, custom REQUIRE macro.
7. **Import clips Mixamo** (`17168ae`) — pipeline FBX2glTF.
8. **Camera refactor** (`1d1ab28`) — `OrbitalCameraController` ne tick plus la physique du perso (devient pure caméra orbitale avec `SetTargetPosition`).
9. **Wire CharacterController + TerrainCollider** (`b386654`) — branchement effectif au boot et dans la frame loop.
10. **Config player.movement** (`023822f`) — section `walkSpeed/runSpeed/gravity/jumpSpeed/coyoteTimeSec/jumpBufferSec`.
11. **State machine 7 états + crossfade** (`cfef19a`) — transitions Idle↔StartWalking↔Walk↔Run, Jump→Fall→Land.
12. **Chargement Run/Jump/Fall/Land** (`d87b748`) — 4 clips au boot.
13. **Smoke test visuel** — itératif, voir « Bugs corrigés post-test » ci-dessous.
14. **Doc CODEBASE_MAP §14.6** (`e7de01a`) — résumé de B.1 dans la map.

### Bugs corrigés post-test (commits successifs)

- `f6d0d98` — `AvatarLocomotionState` rendu `public` (C2248 sur les helpers free-function).
- `472ade1` — switch Run sur `running.fbx` (animation-only) plus propre que `Fast Run.fbx`.
- `f2130ef` — `CharacterController.Init({spawnX, ground+0.9, spawnZ})` à EnterWorld pour préserver la spawn position DB (sinon CC.GetPosition() = (0,0,0) overwrite la cible orbitale → `SavePositionAsync` persistait (0,0,0)).
- `f1e9a14`, `22c27a1`, `ac45fe1` — convergence assets « No Skin » : tous les clips d'anim chargés via `LoadClipsAnimOnly` depuis variantes Mixamo `*No Skin.fbx`. Seul `Standard Walk.fbx` (with-skin) sert de source mesh+skeleton.
- `d987663`, `e451d8e` — outillage debug (logs state machine + `detect_fbx_fps.py`).
- `7b2661c` + `c3ff67a` — **fix oscillation Ground↔Air 60 Hz** : deux bugs combinés.
  - `TerrainCollider::SweepCapsule` traitait la capsule comme un point ; correction : seuil = `groundHeight + halfHeight`.
  - `CharacterController::Update` ré-initialisait `grounded=false` chaque frame ; ajout d'un *sticky ground probe* (sweep court de 5 cm vers le bas) avant la boucle de sweep principale.
- `eabd159` — **fix inversion W/S et Q/D** : matrice modèle calculait `R_y(m_avatarYaw + π)` mais `m_avatarYaw = atan2(forward.x, forward.z)` contenait déjà un `+π` implicite. Corrigé en `m_avatarYaw` initialisé à `π` (au lieu de 0) + retrait du `+π` dans la matrice.
- `6d49368` — **fix tremblement mesh** : `nowSec = duration<float>(time_since_epoch())` perdait sa précision (~10⁹ secondes en float 32 → résolution de plusieurs ms). Nouvelle fonction `EngineNowSec()` qui normalise par un temps de référence statique au démarrage.
- `e9f65a6` — **WalkBack + nouveau Jump** : chargement de `Walking Backwards No Skin.fbx` et remplacement du Jump par `Jumping No Skin.fbx`. Nouvel état `AvatarLocomotionState::WalkBack`. Logique back-step initiale basée sur `dot(moveDir, mesh_forward)`.
- `91d7279` — **fix mapping AZERTY + Q/D pivot** : `BuildMoveInput` discrimine WASD vs ZQSD via `controls.movement_layout` (sinon les deux touches W et Z faisaient avancer en AZERTY). Back-step simplifié : détection directe « S enfoncée seule » (l'heuristique vectorielle précédente rendait Q/D = strafe pur invisible quand le perso était déjà aligné forward).

## Corrections session debug (2026-05-21, branche `claude/review-bugs-status-9lkTj`)

Les bugs §1/§3, §2bis et §2 ci-dessous ont été investigués et corrigés. Les
corrections sont **client uniquement** (rendu / input / animation) — **pas de
redéploiement serveur**.

- **§1 / §3 — caméra qui « décroche » du modèle → root motion non strippé.**
  Diagnostic : la boucle est mono-thread (`Update()` → `SwapRenderState()` →
  `Render()`), donc pas de 1-frame lag réel entre mesh et caméra (les deux lisent
  la même `ccPos` dans la frame). La vraie cause : les clips Mixamo importés ne
  sont pas « In Place » ; leur Hips (root bone) translate horizontalement, et
  `AnimationCrossfade::Sample` appliquait cette translation telle quelle. Comme la
  position monde est déjà pilotée par `CharacterController`, le mesh glissait
  par-dessus la position CC puis snappait au loop → la caméra (qui suit `ccPos`)
  semblait décrocher.
  Fix : `AnimationCrossfade::SetRootMotionLockXZ(true)` (nouveau flag, off par
  défaut) verrouille la translation **X/Z** du/des bone(s) racine
  (`parentIndex == -1`) sur leur bind pose, **en conservant Y** (bob naturel de la
  marche). Appliqué sur les deux chemins de `Sample` (pose courante + blend
  crossfade). Activé par l'Engine au boot de l'avatar.
  Reste en suspens : le cadrage vertical (cause secondaire, l'axe optique vise
  `ccPos + (0,1,0)` ≈ 1,9 m au-dessus des pieds) — laissé tel quel volontairement,
  à réévaluer après validation visuelle du fix root motion.
- **§2bis — recul de travers après strafe → `m_avatarYaw` non reset.**
  Le `CharacterController` bouge bien en world-space (le déplacement physique en
  `S` seul était déjà correct, vers la caméra) ; seul le **yaw visuel** dérivait.
  Fix : en back-step (`movingBack`), on reforce `m_avatarYaw` sur le forward
  caméra (`atan2(camFwd.x, camFwd.z)`) à chaque frame → le mesh reste « dos cam »
  quel que soit le yaw résiduel d'un strafe précédent. Corrige aussi le critère
  §11.5 (S seul dos cam après rotation caméra).
- **§2 — inputs combinés.** Pas de bug fonctionnel : `S+autre direction` n'est pas
  « pure back » → free-mover diagonal (pivot mesh + Walk), cohérent. Avec le fix
  §2bis le comportement est prévisible. Risque mineur restant (polish) : toggle
  rapide `S` ↔ `S+Q` → snap de yaw + crossfade WalkBack↔Walk.

Tests : `animation_crossfade_tests` étendu (+4 cas : lock off = root motion
appliqué, lock on = X/Z → bind & Y conservé, bone enfant intact, lock actif
pendant un crossfade). Compilé + exécuté localement : 9/9 OK.

⚠️ **Validation visuelle non effectuée** (environnement sans build Vulkan / GPU) :
les 7 critères du smoke test §11 doivent être re-déroulés en jeu sur cette branche.

## Bugs en suspens (à traiter dans une session debug ultérieure)

> NOTE 2026-05-21 : les §1, §2, §2bis et §3 ci-dessous ont été **corrigés** (cf.
> section « Corrections session debug » plus haut). Ils restent décrits ici pour
> l'historique du diagnostic ; à re-valider visuellement en jeu.

### 1. Caméra pas bien fixée au modèle pendant le mouvement

**Symptôme rapporté** : « La caméra n'est pas bien fixée au modèle lorsqu'il bouge ».
**Reproduction** : déplacer le perso (Z, S, Q, D, ou diagonales).
**Hypothèses non vérifiées** :
- Décalage entre `feetPos = ccPos - 0.9*Y` (rendu) et `target = ccPos` (caméra) → caméra cible un point ~0.9 m au-dessus du mesh. Combiné au `height_offset = 1.0 m`, la caméra est 1.9 m au-dessus des pieds. Le mesh occupe la moitié inférieure du viewport ; un mouvement vertical pourrait sembler décaler la caméra.
- Possible 1-frame lag entre `SetTargetPosition` et `OrbitalCameraController::Update`. Ordre actuel correct (cf. `Engine.cpp:6761-6764`) mais à reconfirmer en debug.
- Effet d'optique lié à l'animation : root motion dans les clips Mixamo qui translate visuellement le mesh par-dessus la position CC. À vérifier en désactivant l'animation et en bougeant pour voir si la caméra "bouge".
**Premier réflexe debug** : ajouter un overlay debug qui dessine `target` + `feetPos` + `ccPos` en world space, et comparer avec la position screen du mesh.

### 2. Inputs combinés non testés à fond

**Symptôme à valider** : que se passe-t-il quand l'utilisateur appuie en même temps S+Q, S+D, ou S+Q+D ? La logique actuelle (`IsPureBackInput` = S seul) bascule en free-mover dès qu'une autre touche est ajoutée — donc S+Q se comporte comme Q seul (pivot mesh vers gauche+arrière). À tester pour valider qu'il n'y a pas de glitch d'animation.

### 2bis. Direction backward incorrecte après strafe latéral

**Symptôme rapporté lors du smoke test C MVP final (2026-05-20)** :
- L'utilisateur appuie sur D (strafe droite) → le perso se déplace bien à droite ✅
- Puis il relâche D et appuie sur S (recule) → le perso recule **mais à destination de la gauche** au lieu de reculer dans la direction logique (vers la caméra) ❌
- Symétrique avec A puis S : le perso recule à destination de la droite.

**Reproduction** : appuyer brièvement sur D, relâcher, puis appuyer sur S seul. Observer la direction de recul du personnage.

**Hypothèse** : le `m_avatarYaw` (ou le vecteur forward) n'est pas correctement reset entre la sortie du free-mover (Q/D) et l'entrée dans WalkBack (S seul). Le back-step utilise probablement un yaw qui a dérivé pendant le strafe.

**Fichiers à inspecter** :
- `src/client/app/Engine.cpp` : grep `m_avatarYaw`, `WalkBack`, `movingBack`, `IsPureBackInput` (zone ~6846-6926).
- Comprendre comment le yaw est calculé pendant strafe vs backward, et pourquoi la transition strafe→backward garde une orientation résiduelle.

**Premier réflexe debug** : tracer `m_avatarYaw` chaque frame et vérifier sa valeur quand on relâche Q/D et quand on enfonce S.

### 3. Caméra tracking (cf. §1 — confirmé à nouveau lors du smoke test C MVP final)

Le bug §1 ci-dessus a été confirmé une seconde fois lors du smoke test C MVP : la caméra ne suit pas le perso en temps réel. À traiter en priorité dans la session debug B.1 dédiée.

### 4. Smoke test §11 du spec non complètement validé

Les 7 critères de validation visuelle (§11) doivent être re-déroulés sur le dernier commit (`91d7279`) une fois les bugs ci-dessus résolus :
1. ☑ Spawn sur sol, pose Idle stable (sans oscillation)
2. ☑ Z + relâche → StartWalking → Walk → relâche → Idle
3. ☑ Shift+Z → Run, Shift relâché → Walk
4. ☑ Espace en mouvement → Jump → Fall → Land → Walk (mais asset Jump à remplacer, cf. C MVP audit §6)
5. ☐ S seul → WalkBack avec mesh dos cam (animation Walking Backwards visible)
6. ☐ Q et D → mesh pivote vers la direction et avance (free-mover, plus de strafe invisible)
7. ☐ Aucun tremblement de mesh ; caméra collée au perso sans saccade (cf. §2bis + §3)

## Limitations connues (hors-scope B.1, à traiter dans B.2 / B.3 / autres)

- **Q/D = strafe avec pivot mesh** : le mesh pivote en Q/D (free-mover) au lieu de jouer une animation Walk Strafe Left/Right dédiée. Les FBX `Walk Strafe Left No Skin.fbx` et `Walk Strafe Right No Skin.fbx` sont déjà dans `tools/asset_pipeline/inbox/` (uploads user) ; à brancher en C ou en polish ultérieur.
- **Layout clavier** : il faut basculer manuellement en `ZQSD` via le menu Options pour avoir le mapping AZERTY-natif. Pas de détection automatique de layout système. Default = `WASD`.
- **Pas de surface modulation** : eau / sable / neige ne ralentissent pas le perso (`IWorldCollider::QueryWater` renvoie toujours `false`). Sous-projet B.2 qui attend `M100.11 SurfaceQuery`.
- **Pas de remote players animés** : les autres joueurs visibles n'ont pas leur state machine synchronisée via UDP. Sous-projet B.3 (redéploiement serveur requis).
- **Pas d'anim Jump-Land dédiée** : Land est une animation one-shot mais elle pourrait avoir un blend continu de Fall→Land (impact + recovery). À polir si besoin.
- **Root motion non explicitement géré** : les clips Mixamo ont une translation du root bone qui pourrait causer des artefacts subtils sur le mesh global. Pas observé en pratique mais à garder en tête.

## CharacterController + TerrainCollider — points d'attention

- **`grounded` est sticky** via un sweep court de 5 cm vers le bas (cf. `CharacterController.cpp` après le commit `7b2661c`). Le jump impulse reset explicitement `grounded=false` pour éviter qu'on saute sans quitter le sol. **Ne pas casser cette logique** sous peine de retomber dans l'oscillation Ground↔Air à 60 Hz.
- **`TerrainCollider::SweepCapsule` utilise `groundHeight + halfHeight` comme seuil** (et non `groundHeight` seul). Les tests `terrain_collider_tests` valident cette convention. **Ne pas re-traiter la capsule comme un point** sous peine de re-faire le bug du perso qui "tombe" de 0.9 m au spawn.
- **`m_avatarYaw` initialisé à π** dans `Engine.h` (orientation dos caméra au spawn avec mesh Mixamo face intrinsèque +Z). La matrice modèle applique directement `R_y(m_avatarYaw)`, **pas** `R_y(m_avatarYaw + π)` (ancien bug eabd159).
- **`EngineNowSec()`** doit être utilisé partout où on calcule un temps animation (au lieu de `time_since_epoch()`). Sinon précision float 32 dégrade le sampling.

## Plan pour la session debug ultérieure

1. Reproduire le bug caméra avec le build actuel ; comparer mesh visuel position vs `ccPos` en overlay debug.
2. Si nécessaire, alignment `feetPos` ↔ `target` (peut-être `target = feetPos + (1.0, 0, 0)*halfHeight_screenOffset`).
3. Tester toutes les combinaisons d'inputs (S+Q, S+D, Z+Q, etc.) pour vérifier la state machine et la pivot logic.
4. Re-dérouler les 7 critères §11.
5. Si tout passe : nettoyer les logs debug (commits `d987663`) et merger.
