# Anti-occlusion caméra — fondu tramé des props + clamp terrain

> Design validé le 2026-06-21. Chantier **100 % client** (rendu/caméra). Indépendant
> du chantier collision bâtiments (#919 / building-collision-catalog).

## Problème

En vue 3e personne, quand un élément du décor (mur de l'auberge, prop) ou le terrain
passe **entre le joueur et la caméra**, il bouche la vue et gâche l'expérience
(constaté en jeu, 2 captures : caméra sous le terrain ; mur d'auberge plein écran).

### Cause racine

Il existe **deux** caméras 3e personne dans le code :

- `engine::gameplay::ThirdPersonCamera` (`src/client/gameplay/ThirdPersonCamera.cpp`)
  possède un anti-occlusion complet (`SweepSphere` tête→caméra) **mais n'est jamais
  instanciée** dans `Engine`.
- `engine::render::OrbitalCameraController` (`src/client/render/Camera.cpp`) est la
  caméra **réellement utilisée en jeu** (`Engine::m_orbitalCameraController`) et fait
  un simple `position = cible − direction × distance`, **sans aucun test de
  collision** (le commentaire `Camera.cpp:228` l'avoue : « la collision camera-decor
  … reviendra … un futur module collision camera »).

On comble ce manque, sans réveiller `ThirdPersonCamera` (swap d'API risqué pour le
mouvement, qui dépend de `GetForwardXZ/GetRightXZ/GetYawRad`).

## Décision de design

Deux traitements complémentaires, choisis avec l'utilisateur :

1. **Props + pièces de bâtiment** (tout ce qui passe par `GeometryPass`) :
   **fondu tramé** (screen-door / dither) quand l'objet occulte le joueur. Le perso
   reste visible « à travers » l'obstacle.
2. **Terrain** : **pas** de transparence (rendrait une colline « trouée » sur le
   ciel/vide). À la place, un **clamp anti-sous-sol** : la caméra ne descend jamais
   sous la surface du terrain.

L'avatar du joueur n'est **jamais** fondu.

## Architecture

Tout se passe **côté client**, dans la boucle de frame de `Engine`, **après**
`m_orbitalCameraController.Update(...)` qui produit `out.camera`.

### Flux par frame

```
out.camera = OrbitalCameraController.Update(...)          // position caméra brute
        │
        ├─ [1] clamp terrain : cam.y = max(cam.y, SampleHeightAtWorldXZ(cam.xz) + marge)
        │
        ├─ [2] CameraOcclusionFade.Update(cam.position, focusPoint, occluders, dt)
        │         → met à jour un fade lissé ∈ [fadeMin, 1] par occulteur
        │
        └─ [3] au rendu des props/bâtiments : GeometryPass reçoit le fade par draw
                  → fragment shader : screen-door discard
```

### Composant 1 — `CameraOcclusionFade` (nouveau, testable sans Vulkan)

Fichiers : `src/client/render/CameraOcclusionFade.{h,cpp}`.

Rôle : à partir de la position caméra, du point focal (tête joueur) et d'une liste
d'occulteurs (sphères englobantes monde), calculer un **fade cible** par occulteur,
puis le **lisser temporellement** vers ce cible.

Type d'entrée (un occulteur) :

```cpp
struct OccluderSphere
{
    std::uint32_t id = 0;   ///< identifiant stable de l'objet (clé de suivi du fade)
    engine::math::Vec3 center{}; ///< centre monde de la sphère englobante
    float radius = 0.5f;    ///< rayon monde
};
```

Configuration :

```cpp
struct Config
{
    float fadeMin = 0.15f;        ///< opacité minimale d'un occulteur au cœur de l'occlusion (0 = invisible)
    float radiusMargin = 0.5f;    ///< marge ajoutée au rayon pour la zone de transition (m)
    float fadeInPerSec = 6.0f;    ///< vitesse de retour à l'opaque (1.0) quand non occultant
    float fadeOutPerSec = 8.0f;   ///< vitesse de passage vers fadeMin quand occultant
    float playerProtectRadius = 0.6f; ///< un occulteur dont le centre est à moins de ça du focal n'est pas fondu (évite de fondre ce qui touche le joueur)
};
```

API :

```cpp
class CameraOcclusionFade
{
public:
    void Init(const Config& cfg);

    /// Met à jour les fades. \p occluders : liste reconstruite chaque frame.
    /// \p dt en secondes. Après l'appel, FadeFor(id) renvoie le fade lissé courant.
    void Update(const engine::math::Vec3& cameraPos,
                const engine::math::Vec3& focusPoint,
                const std::vector<OccluderSphere>& occluders,
                float dt);

    /// Fade lissé courant d'un objet (1.0 = opaque par défaut si inconnu).
    float FadeFor(std::uint32_t id) const;

private:
    Config m_cfg{};
    std::unordered_map<std::uint32_t, float> m_fade; ///< id -> fade lissé courant
};
```

Calcul du fade cible d'un occulteur (par frame) :

1. **Profondeur** : projeter `center` sur le segment `camera → focus`. Soit
   `t = clamp(dot(center - camera, dir) / segLen, 0, 1)` avec `dir` unitaire et
   `segLen = |focus - camera|`. Si `t <= 0` ou `t >= 1`, l'objet n'est **pas entre**
   les deux → cible = 1.0 (opaque).
2. **Garde joueur** : si `|center - focus| < playerProtectRadius` → cible = 1.0.
3. **Distance latérale** : `closest = camera + dir * (t * segLen)` ;
   `d = |center - closest|`. Zone : `r0 = radius` (cœur), `r1 = radius + radiusMargin`
   (bord).
   - `d <= r0` → cible = `fadeMin` (occlusion forte).
   - `d >= r1` → cible = 1.0 (pas d'occlusion).
   - entre les deux → interpolation linéaire : `cible = lerp(fadeMin, 1, (d - r0)/(r1 - r0))`.

Lissage temporel (par id) :

- vers une cible **plus opaque** (cible > courant) : `courant += fadeInPerSec * dt`,
  borné à cible.
- vers une cible **plus transparente** (cible < courant) : `courant -= fadeOutPerSec * dt`,
  borné à cible.
- ids absents de `occluders` cette frame : on les fait **revenir à 1.0** au rythme
  `fadeInPerSec` puis on purge l'entrée une fois à 1.0 (évite la croissance mémoire).

`FadeFor(id)` : renvoie le fade lissé, ou **1.0** si l'id est inconnu.

### Composant 2 — Collecte des occulteurs (`Engine`)

Les occulteurs réutilisent les **données de collision déjà construites** : chaque prop
a un `PropCylinder` (centre `cx,cz`, `radius`, `baseY..topY`) et chaque pièce de
bâtiment idem (et, à terme, des `PropBox`). On en dérive une `OccluderSphere` :

- `center = (cx, (baseY+topY)/2, cz)`
- `radius = max(radius_xz, (topY-baseY)/2)` (sphère englobant le cylindre)
- `id` = identifiant stable de l'objet (indice de prop / pièce). Cet id doit être
  **le même** que celui passé au draw `GeometryPass` correspondant, pour relier le
  fade calculé au bon objet rendu.

Le terrain et l'avatar ne sont **pas** des occulteurs.

> Détail d'implémentation (à câbler dans le plan) : `Engine` tient déjà la liste des
> props/pièces rendus et leurs transforms. On ajoute, à la construction de ces objets,
> la mémorisation `(id, OccluderSphere)` ; à chaque frame on rebâtit le `std::vector`
> d'occulteurs (positions statiques → on peut le bâtir une fois et le réutiliser, mais
> un rebuild par frame reste trivial pour quelques centaines d'objets).

### Composant 3 — Propagation du fade au rendu (`GeometryPass` + shader)

Push-constants (`GeometryPass.cpp`) : on **réutilise un `uint` de padding** comme
`float fade`. La taille reste **144 octets**, le `VkPipelineLayout` est **inchangé**.

```cpp
struct GeometryPushConstants
{
    float prevViewProj[16]{};
    float viewProj[16]{};
    uint32_t materialIndex = 0;
    float    fade = 1.0f;   // <-- ex-padding0 : 1.0 = opaque (défaut)
    uint32_t padding1 = 0;
    uint32_t padding2 = 0;
};
```

Les méthodes de draw de `GeometryPass` gagnent un paramètre `float fade = 1.0f`
(défaut → comportement actuel pour tous les appelants non modifiés : terrain
n'utilise pas GeometryPass ; avatar et props non-occultants passent 1.0).

Shader [`game/data/shaders/gbuffer_geometry.frag`] — bloc `push_constant` aligné :

```glsl
layout(push_constant) uniform PushConstants {
    mat4 prevViewProj;
    mat4 viewProj;
    uint materialIndex;
    float fade;     // 1.0 = opaque
    uint _pad1;
    uint _pad2;
} pc;
```

Tout en haut de `main()`, avant le travail coûteux :

```glsl
// Anti-occlusion : transparence tramée (screen-door). fade=1 -> rien à faire.
if (pc.fade < 0.999) {
    // matrice de Bayer 4x4 normalisée (seuils 0/16 .. 15/16)
    const float bayer[16] = float[16](
        0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
       12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
        3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
       15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0);
    ivec2 p = ivec2(gl_FragCoord.xy) & 3;
    if (pc.fade < bayer[p.y * 4 + p.x])
        discard;
}
```

Recompilation SPIR-V via la CI (pas de toolchain locale).

### Composant 4 — Clamp terrain (`Engine`)

Après `m_orbitalCameraController.Update(...)`, avant d'utiliser `out.camera` :

```cpp
const float groundY = m_terrainRenderer.SampleHeightAtWorldXZ(out.camera.position.x,
                                                              out.camera.position.z);
const float minCamY = groundY + cfg.terrainClampMargin; // ex. 0.5 m
if (out.camera.position.y < minCamY)
    out.camera.position.y = minCamY;
```

(Le chemin d'accès exact à `SampleHeightAtWorldXZ` — via `m_terrainRenderer` ou un
accesseur — est à confirmer dans le plan ; la fonction existe et est déjà utilisée
par `TargetReticleSystem`.)

## Configuration (`config.json`, préfixe `client.camera.*`)

| Clé | Défaut | Rôle |
|---|---|---|
| `client.camera.occlusion_fade.enabled` | `true` | Active le fondu tramé |
| `client.camera.occlusion_fade.fade_min` | `0.15` | Opacité mini d'un occulteur |
| `client.camera.occlusion_fade.radius_margin` | `0.5` | Marge de transition (m) |
| `client.camera.occlusion_fade.fade_in_per_sec` | `6.0` | Vitesse retour opaque |
| `client.camera.occlusion_fade.fade_out_per_sec` | `8.0` | Vitesse passage transparent |
| `client.camera.occlusion_fade.player_protect_radius` | `0.6` | Rayon de garde joueur (m) |
| `client.camera.terrain_clamp_margin` | `0.5` | Marge anti-sous-sol (m) |

Lues via `engine::core::Config` (`GetBool/GetDouble`). Valeurs par défaut si absentes →
rétro-compatible.

## Tests

`src/client/render/tests/CameraOcclusionFadeTests.cpp`, enregistré via
`lcdlln_add_simple_test` dans `src/CMakeLists.txt`. Aucune dépendance Vulkan (math pur).

Cas couverts :

1. **Occulteur pile sur le segment** caméra→focus, proche → cible = `fadeMin`
   (après convergence du lissage sur plusieurs `Update`).
2. **Occulteur derrière le joueur** (`t >= 1`) → reste 1.0 (opaque).
3. **Occulteur derrière la caméra** (`t <= 0`) → reste 1.0.
4. **Occulteur latéral** (au-delà de `radius + margin`) → reste 1.0.
5. **Zone de transition** : `d` entre `r0` et `r1` → fade intermédiaire strictement
   entre `fadeMin` et 1.
6. **Garde joueur** : occulteur dont le centre touche le focal → 1.0 (jamais fondu).
7. **Lissage** : un occulteur qui apparaît ne saute pas instantanément à `fadeMin`
   (un seul `Update` court ne suffit pas à atteindre la cible) ; après assez de temps,
   il converge.
8. **Purge** : un id absent revient à 1.0 puis `FadeFor` renvoie 1.0 (et la map ne
   grossit pas indéfiniment).
9. **Inconnu** : `FadeFor(id jamais vu)` = 1.0.

Validation **en jeu** (visuel) : entrer dans l'auberge, faire tourner la caméra
derrière un mur → le mur se trame et le perso reste visible ; reculer la caméra dans
une pente → elle ne plonge plus sous le sol.

## Hors périmètre (YAGNI)

- Pas de fondu du terrain (décidé).
- Pas de vraie transparence triée / alpha-blend (le screen-door suffit en pipeline
  deferred opaque).
- Pas de réveil de `ThirdPersonCamera` ni de refonte de la caméra.
- Pas de fade des PNJ/mobs distants à ce stade (les occulteurs = props + bâtiments ;
  extensible plus tard si besoin).

## Déploiement

✅ **Client uniquement** — rendu + caméra. Aucun opcode, payload, handler ni migration.
**Pas de redéploiement serveur.**
