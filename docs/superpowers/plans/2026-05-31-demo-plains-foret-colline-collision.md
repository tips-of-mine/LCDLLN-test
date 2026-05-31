# Forêt riveraine, colline et collision props (demo_plains) — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Peupler la carte par défaut `demo_plains` (retrait des props d'eau sauf coffre+villageois, conversion des 244 props manquants, forêt de ~80 arbres, colline ~15 m) et rendre tous les objets solides via une collision cylindrique.

**Architecture:** Données (config.json + heightmap + scenery) générées par scripts locaux (Blender 5.1 headless pour la conversion FBX→glTF, Python pour la forêt et la colline). Le seul code C++ est un `CompositeWorldCollider` qui combine `TerrainCollider` + cylindres de props, branché dans `CharacterController`. Tout est client ; aucun redéploiement serveur.

**Tech Stack:** C++17 (Vulkan client), Python 3 + Blender 5.1 (bpy headless), CMake/ctest via CI GitHub (build-linux), JSON de config.

**Contexte d'environnement (IMPORTANT) :**
- **Pas de toolchain C++ locale.** Le code C++ ne se compile/teste **que via la CI** (push → workflow `build-linux` qui lance `ctest`, et `build-windows`). Les étapes « run test » des tâches C++ se vérifient **après push, dans la CI**, pas en local.
- **Blender 5.1** est installé : `C:/Program Files/Blender Foundation/` (binaire `blender.exe`). Les scripts Python de génération (forêt, colline) tournent avec le Python système (modules `struct`, `random`, `json` ; pas de numpy requis).
- Shell = PowerShell (Windows). Branche de travail : `feat/demo-plains-foret-colline-collision` (déjà créée, spec déjà commité dessus).

**Constantes terrain vérifiées (defaults, non surchargés dans config.json) :**
- Grille heightmap 1025×1025, format `HAMP` (en-tête 12 o : magic u32 `0x504D4148`, width u32, height u32), puis `uint16` LE row-major `index = z*width + x`.
- `terrain.world_size = 1024 m`, `origin_x = origin_z = -512`, `vertStepWorld = 1024/1024 = 1.0 m/texel`.
- Donc `texel_x = round(worldX + 512)`, `texel_z = round(worldZ + 512)`.
- `terrain.height_scale = 200 m` ; hauteur monde `Y = (h/65535) * 200`. Terrain plat actuel `h = 0x8000 = 32768` → `Y = 100 m`.
- Eau : centre monde (55, 0), `half_size 40` → texels x∈[527,607], z∈[472,552].

---

## Vue d'ensemble des fichiers

**Créés :**
- `tools/asset_pipeline/convert_props_blender.py` — conversion FBX→glTF des props manquants (Blender headless).
- `tools/world_gen/scatter_forest.py` — génère le bloc `world.scenery` (~80 arbres).
- `tools/world_gen/raise_hill.py` — ajoute une colline gaussienne au heightmap.
- `src/client/gameplay/CompositeWorldCollider.h` / `.cpp` — collisionneur composite (terrain + cylindres).
- `src/client/gameplay/tests/CompositeWorldColliderTests.cpp` — tests unitaires du sweep cylindre.
- `game/data/meshes/props/*.gltf` + `*.bin` (244 paires, produites par le script Blender).

**Modifiés :**
- `config.json` — retrait `world.interactables` 2–9 ; ajout `world.scenery` (par script) ; (aucune clé serveur).
- `src/client/app/Engine.h` / `Engine.cpp` — struct `SceneryInstance`, `m_scenery`, `LoadScenery()`, helper de baking partagé, `m_worldCollider` (CompositeWorldCollider), branchement dans l'appel `CharacterController::Update`, registre des cylindres (props + scenery + villageois).
- `src/client/render/static_mesh/StaticMeshLoader.{h,cpp}` — exposer `maxY` (haut du mesh) en plus de la géométrie (pour `topY` du cylindre).
- `src/CMakeLists.txt` — ajouter `CompositeWorldCollider.cpp` aux sources client ; déclarer le test.
- `game/data/zones/demo_plains/terrain_height.r16h` — colline (par script).
- `tools/asset_pipeline/README.md` — section conversion props Blender.
- `docs/superpowers/specs/2026-05-31-foret-colline-collision-demo-plains-design.md` — statut « implémenté ».

---

## Task 1 : Script de conversion props Blender + validation par diff

**Files:**
- Create: `tools/asset_pipeline/convert_props_blender.py`

- [ ] **Step 1 : Écrire le script de conversion**

Le script reproduit l'export Blender d'origine (générateur « Khronos glTF Blender I/O »), pour les FBX de `inbox/meshes/props/` sans `.gltf` dans `game/data/meshes/props/`. Il référence les textures *trim* partagées (déjà présentes) sans les redupliquer.

```python
# tools/asset_pipeline/convert_props_blender.py
# Conversion FBX -> glTF (.gltf + .bin) des props non encore convertis.
# Reproduit l'export Blender d'origine (matériaux MI_Trim_*, ORM packé, COLOR_0,
# doubleSided, textures trim partagées référencées par URI relatif).
#
# Lancement (PowerShell) :
#   & "C:/Program Files/Blender Foundation/Blender 5.1/blender.exe" --background `
#       --python tools/asset_pipeline/convert_props_blender.py -- [--only NAME] [--validate]
#
# Args après "--" :
#   --only NAME   : ne traiter qu'un FBX (sans extension), ex. --only Barrel
#   --out DIR     : dossier de sortie (défaut game/data/meshes/props)
#   --validate    : convertit vers un dossier temp (n'écrase pas), pour diff
#
# Effet de bord : écrit des .gltf/.bin dans le dossier de sortie ; logge les échecs.
import bpy, sys, os, glob

def parse_args():
    argv = sys.argv
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    only = None; out = None; validate = False
    i = 0
    while i < len(argv):
        if argv[i] == "--only": only = argv[i+1]; i += 2
        elif argv[i] == "--out": out = argv[i+1]; i += 2
        elif argv[i] == "--validate": validate = True; i += 1
        else: i += 1
    return only, out, validate

REPO = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
INBOX = os.path.join(REPO, "tools", "asset_pipeline", "inbox", "meshes", "props")
PROPS_OUT = os.path.join(REPO, "game", "data", "meshes", "props")

def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)

def remap_trim_images():
    # Les FBX référencent T_Trim_*; on pointe chaque image vers le PNG partagé du
    # dossier props pour que l'export écrive l'URI relatif attendu par le loader.
    for img in bpy.data.images:
        base = os.path.basename(img.filepath_raw or img.name)
        cand = os.path.join(PROPS_OUT, base)
        if os.path.exists(cand):
            img.filepath = cand
            img.source = 'FILE'

def convert_one(name, out_dir):
    reset_scene()
    src = os.path.join(INBOX, name + ".fbx")
    bpy.ops.import_scene.fbx(filepath=src)
    remap_trim_images()
    dst = os.path.join(out_dir, name + ".gltf")
    bpy.ops.export_scene.gltf(
        filepath=dst,
        export_format='GLTF_SEPARATE',   # .gltf + .bin + textures référencées
        export_materials='EXPORT',
        export_keep_originals=True,       # ne pas redupliquer les textures trim
        export_vertex_color='MATERIAL',   # conserve COLOR_0
        export_yup=True,
        use_selection=False,
    )
    return dst

def main():
    only, out, validate = parse_args()
    out_dir = out or (os.path.join(REPO, "tools", "asset_pipeline", "_validate_out")
                      if validate else PROPS_OUT)
    os.makedirs(out_dir, exist_ok=True)
    if only:
        targets = [only]
    else:
        targets = []
        for fbx in sorted(glob.glob(os.path.join(INBOX, "*.fbx"))):
            n = os.path.splitext(os.path.basename(fbx))[0]
            if not os.path.exists(os.path.join(PROPS_OUT, n + ".gltf")):
                targets.append(n)
    ok, failed = [], []
    for n in targets:
        try:
            convert_one(n, out_dir); ok.append(n)
            print(f"[convert_props] OK {n}")
        except Exception as e:
            failed.append((n, str(e)))
            print(f"[convert_props] FAIL {n}: {e}")
    print(f"[convert_props] terminé : {len(ok)} ok, {len(failed)} échec(s)")
    for n, e in failed:
        print(f"[convert_props]   échec {n}: {e}")

main()
```

- [ ] **Step 2 : Valider la recette par diff sur un prop déjà commité (`Barrel`)**

Re-convertir `Barrel` dans un dossier temporaire et comparer au `.gltf` commité.

Run (PowerShell) :
```powershell
& "C:/Program Files/Blender Foundation/Blender 5.1/blender.exe" --background `
  --python tools/asset_pipeline/convert_props_blender.py -- --only Barrel --validate
```
Puis comparer la structure (matériaux, noms `MI_Trim_*`, URIs de textures, `doubleSided`, attributs de primitives `POSITION/NORMAL/TEXCOORD_0/COLOR_0`) :
```powershell
$ref = Get-Content game/data/meshes/props/Barrel.gltf -Raw | ConvertFrom-Json
$new = Get-Content tools/asset_pipeline/_validate_out/Barrel.gltf -Raw | ConvertFrom-Json
"materials ref : " + ($ref.materials.name -join ", ")
"materials new : " + ($new.materials.name -join ", ")
"images ref    : " + ($ref.images.uri -join ", ")
"images new    : " + ($new.images.uri -join ", ")
```
Expected : les noms de matériaux (`MI_Trim_*`) et les URIs d'images (`T_Trim_*.png`) **correspondent**. Si écart (ex. textures embarquées, URI absolu, matériau renommé), ajuster les options `export_*` du Step 1 et recommencer jusqu'à correspondance structurelle.

- [ ] **Step 3 : Commit du script validé (sans assets encore)**

```powershell
git add tools/asset_pipeline/convert_props_blender.py
git commit -m @'
feat(asset-pipeline): script Blender headless de conversion props FBX->glTF

Reproduit l'export Blender d'origine (MI_Trim_*, ORM, COLOR_0, doubleSided,
textures trim partagees referencees). Valide par diff contre Barrel.gltf.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
'@
```

---

## Task 2 : Convertir les 244 props manquants et commiter les assets

**Files:**
- Create: `game/data/meshes/props/*.gltf` + `*.bin` (244 paires)

- [ ] **Step 1 : Lancer le batch de conversion**

Run (PowerShell) :
```powershell
& "C:/Program Files/Blender Foundation/Blender 5.1/blender.exe" --background `
  --python tools/asset_pipeline/convert_props_blender.py
```
Expected : log final `terminé : N ok, M échec(s)`. Noter la liste des échecs éventuels (FBX hors norme) pour traitement manuel ; ils ne bloquent pas le batch.

- [ ] **Step 2 : Vérifier le compte produit**

Run (PowerShell) :
```powershell
(Get-ChildItem game/data/meshes/props/*.gltf).Count
```
Expected : **338** (94 existants + 244 nouveaux), moins les échecs éventuels. Vérifier en particulier que les 20 arbres sont présents :
```powershell
Get-ChildItem game/data/meshes/props/*.gltf | Where-Object { $_.Name -match "Tree|Pine" } | Select-Object -Expand Name
```
Expected : `CommonTree_1..5`, `Pine_1..5`, `DeadTree_1..5`, `TwistedTree_1..5` (20 fichiers).

- [ ] **Step 3 : Commit des assets convertis**

```powershell
git add game/data/meshes/props/
git commit -m @'
feat(assets): conversion glTF des 244 props manquants (Blender headless)

Catalogue props complet dans game/data/meshes/props/ (arbres, vegetation,
mobilier, modulaire batiment). Textures trim partagees reutilisees.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
'@
```

---

## Task 3 : Retirer les objets décoratifs autour de l'eau (garder coffre + villageois)

**Files:**
- Modify: `config.json` (bloc `world.interactables`, indices 2–9 + commentaire)

- [ ] **Step 1 : Éditer `config.json`**

Supprimer les entrées `"2"` à `"9"` du bloc `world.interactables`, ne garder que `"0"` (Villageois) et `"1"` (Coffre). Mettre à jour `_comment_interactables` pour retirer la mention « chemin de barils/caisses » et « bannières autour du bassin », et indiquer que le décor est désormais dans `world.scenery`.

Le bloc après édition :
```json
        "_comment_interactables": "Interagir (touche E) : count implicite = nombre de cles numeriques. Chaque index i a x/z (metres monde), radius (portee m), npc (bool), label, message, mesh/scale/yaw_deg/rot_x_deg, dialogue. Seuls les VRAIS interactibles ici (PNJ, coffre). Le DECOR (arbres, props non interactifs mais solides) est dans world.scenery.",
        "interactables": {
            "0": { "x": 4.0, "z": 0.0, "radius": 2.5, "npc": true, "label": "Villageois", "message": "Villageois : Bonjour !", "mesh": "models/characters/humains/Male_Ranger/Male_Ranger.glb", "scale": 95.0, "rot_x_deg": -90.0, "yaw_deg": 270.0, "dialogue": { "count": 3, "0": "Bonjour, aventurier ! Bienvenue par ici.", "1": "Le bassin de test est a l'est, si tu veux nager.", "2": "Reviens me voir quand tu veux." } },
            "1": { "x": -4.0, "z": 0.0, "radius": 2.0, "npc": false, "label": "Coffre", "message": "Vous fouillez le coffre... vide pour l'instant.", "mesh": "meshes/props/Chest_Wood.gltf", "scale": 1.0, "yaw_deg": 0.0 }
        },
```

- [ ] **Step 2 : Vérifier que le JSON reste valide**

Run (PowerShell) :
```powershell
Get-Content config.json -Raw | ConvertFrom-Json | Out-Null; if ($?) { "JSON OK" }
```
Expected : `JSON OK`.

- [ ] **Step 3 : Commit**

```powershell
git add config.json
git commit -m @'
feat(world): retire les props decoratifs autour de l'eau (garde coffre + villageois)

world.interactables ne contient plus que le Villageois (0) et le Coffre (1).
Le decor passe dans world.scenery (a venir).

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
'@
```

---

## Task 4 : Exposer le haut du mesh (`maxY`) dans StaticMeshLoader

**Files:**
- Modify: `src/client/render/static_mesh/StaticMeshLoader.h` (struct `StaticMeshCpuData`)
- Modify: `src/client/render/static_mesh/StaticMeshLoader.cpp` (remplissage)

Objectif : disposer des bornes verticales du mesh (local) pour calculer `topY` du cylindre de collision dans Engine. (On a déjà `minY` calculé au baking dans Engine ; on veut aussi `maxY`.) On ajoute les bornes locales `localMinY`/`localMaxY` au CPU data.

- [ ] **Step 1 : Ajouter les champs à la struct**

Dans `StaticMeshLoader.h`, struct `StaticMeshCpuData` (vérifier le nom exact à l'endroit de la déclaration), ajouter :
```cpp
    float localMinY = 0.0f;  ///< Y min des sommets (espace local du mesh)
    float localMaxY = 0.0f;  ///< Y max des sommets (espace local du mesh)
```

- [ ] **Step 2 : Calculer les bornes au chargement**

Dans `StaticMeshLoader.cpp`, après remplissage de `vertices`, calculer :
```cpp
    float mn = 1e30f, mx = -1e30f;
    for (const auto& v : out.vertices) { mn = std::min(mn, v.pos[1]); mx = std::max(mx, v.pos[1]); }
    if (!out.vertices.empty()) { out.localMinY = mn; out.localMaxY = mx; }
```
(Adapter `out` au nom de la variable de sortie locale ; inclure `<algorithm>` et `<cmath>` si absents.)

- [ ] **Step 3 : Vérifier la cohérence d'usage (lecture seule)**

Aucun test isolé (struct de données). La validation se fait à la compilation CI (Task 8) et via l'usage dans Task 6.

- [ ] **Step 4 : Commit**

```powershell
git add src/client/render/static_mesh/StaticMeshLoader.h src/client/render/static_mesh/StaticMeshLoader.cpp
git commit -m @'
feat(render): StaticMeshLoader expose localMinY/localMaxY (bornes verticales)

Necessaire pour calculer le topY des cylindres de collision des props.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
'@
```

---

## Task 5 : `CompositeWorldCollider` (terrain + cylindres) + tests

**Files:**
- Create: `src/client/gameplay/CompositeWorldCollider.h`
- Create: `src/client/gameplay/CompositeWorldCollider.cpp`
- Create: `src/client/gameplay/tests/CompositeWorldColliderTests.cpp`
- Modify: `src/CMakeLists.txt`

**Note exécution :** pas de build local → les « run test » se vérifient en CI (Task 11). Écrire test + implémentation ensemble ici.

- [ ] **Step 1 : Écrire le header**

```cpp
// src/client/gameplay/CompositeWorldCollider.h
#pragma once

#include "src/client/gameplay/CharacterController.h"  // IWorldCollider
#include "src/shared/math/Math.h"

#include <vector>

namespace engine::gameplay
{
    /// Cylindre vertical de collision pour un prop (arbre, coffre, PNJ...).
    /// Axe vertical en (cx, cz), borné en Y par [baseY, topY].
    struct PropCylinder
    {
        float cx = 0.0f;
        float cz = 0.0f;
        float radius = 0.5f; ///< mètres
        float baseY = 0.0f;  ///< Y monde du bas
        float topY = 2.0f;   ///< Y monde du haut
    };

    /// Collisionneur composite : combine un IWorldCollider de terrain (sol + eau)
    /// et une liste de cylindres de props. SweepCapsule retourne le hit le plus
    /// proche (plus petite fraction) entre terrain et cylindres. QueryWater est
    /// délégué au terrain.
    class CompositeWorldCollider final : public IWorldCollider
    {
    public:
        /// \param terrain  collisionneur de terrain (non possédé, doit survivre à this).
        explicit CompositeWorldCollider(const IWorldCollider* terrain = nullptr);

        void SetTerrain(const IWorldCollider* terrain) { m_terrain = terrain; }
        void ClearCylinders() { m_cylinders.clear(); }
        void AddCylinder(const PropCylinder& c) { m_cylinders.push_back(c); }
        std::size_t CylinderCount() const { return m_cylinders.size(); }

        bool SweepCapsule(const Capsule& capsule,
            const engine::math::Vec3& startCenter,
            const engine::math::Vec3& endCenter,
            SweepHit& outHit) const override;

        bool QueryWater(const engine::math::Vec3& worldCenter, WaterQuery& out) const override;

    private:
        const IWorldCollider* m_terrain = nullptr;
        std::vector<PropCylinder> m_cylinders;
    };
}
```

- [ ] **Step 2 : Écrire l'implémentation**

```cpp
// src/client/gameplay/CompositeWorldCollider.cpp
#include "src/client/gameplay/CompositeWorldCollider.h"

#include <cmath>

namespace engine::gameplay
{
    CompositeWorldCollider::CompositeWorldCollider(const IWorldCollider* terrain)
        : m_terrain(terrain) {}

    bool CompositeWorldCollider::QueryWater(const engine::math::Vec3& worldCenter,
                                            WaterQuery& out) const
    {
        if (m_terrain) return m_terrain->QueryWater(worldCenter, out);
        out = WaterQuery{};
        return false;
    }

    bool CompositeWorldCollider::SweepCapsule(const Capsule& capsule,
        const engine::math::Vec3& startCenter,
        const engine::math::Vec3& endCenter,
        SweepHit& outHit) const
    {
        SweepHit best;
        best.hit = false;
        best.fraction = 1.0f;

        // 1) Terrain
        if (m_terrain)
        {
            SweepHit th;
            if (m_terrain->SweepCapsule(capsule, startCenter, endCenter, th) && th.fraction < best.fraction)
                best = th;
        }

        // 2) Cylindres : test 2D cercle (capsule XZ) contre cercle (cylindre), borné en Y.
        // La capsule s'étend verticalement de center.y - h/2 - r à center.y + h/2 + r.
        const float halfH = capsule.height * 0.5f;
        const float sx = startCenter.x, sz = startCenter.z;
        const float dx = endCenter.x - sx, dz = endCenter.z - sz;

        for (const auto& c : m_cylinders)
        {
            const float R = capsule.radius + c.radius;
            // Vérifie le recouvrement vertical (au point d'arrivée, conservateur).
            const float capLo = endCenter.y - halfH - capsule.radius;
            const float capHi = endCenter.y + halfH + capsule.radius;
            if (capHi < c.baseY || capLo > c.topY) continue;

            // Plus petit t in [0,1] où dist( P(t), axe ) == R, P(t)=start+t*d (XZ).
            const float fx = sx - c.cx, fz = sz - c.cz;
            const float a = dx * dx + dz * dz;
            const float b = 2.0f * (fx * dx + fz * dz);
            const float cc = fx * fx + fz * fz - R * R;

            float tHit = -1.0f;
            if (a < 1e-8f)
            {
                // Déplacement XZ négligeable : test statique au départ.
                if (cc <= 0.0f) tHit = 0.0f;
            }
            else
            {
                const float disc = b * b - 4.0f * a * cc;
                if (disc >= 0.0f)
                {
                    const float sq = std::sqrt(disc);
                    const float t0 = (-b - sq) / (2.0f * a);
                    const float t1 = (-b + sq) / (2.0f * a);
                    if (cc <= 0.0f) tHit = 0.0f;            // déjà en intersection au départ
                    else if (t0 >= 0.0f && t0 <= 1.0f) tHit = t0;
                    else if (t1 >= 0.0f && t1 <= 1.0f) tHit = t1; // sortie : on bloque quand même
                }
            }

            if (tHit >= 0.0f && tHit < best.fraction)
            {
                best.hit = true;
                best.fraction = tHit;
                // Normale horizontale du cylindre vers la capsule au point de contact.
                const float px = sx + tHit * dx - c.cx;
                const float pz = sz + tHit * dz - c.cz;
                const float len = std::sqrt(px * px + pz * pz);
                if (len > 1e-6f) best.normal = engine::math::Vec3{ px / len, 0.0f, pz / len };
                else best.normal = engine::math::Vec3{ 1.0f, 0.0f, 0.0f };
            }
        }

        outHit = best;
        return best.hit;
    }
}
```

- [ ] **Step 3 : Écrire les tests**

```cpp
// src/client/gameplay/tests/CompositeWorldColliderTests.cpp
#include "src/client/gameplay/CompositeWorldCollider.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace engine::gameplay;
using engine::math::Vec3;

namespace
{
    // Terrain factice : ne collisionne jamais ; QueryWater renvoie inWater=true (pour
    // vérifier la délégation).
    class FakeTerrain final : public IWorldCollider
    {
    public:
        bool SweepCapsule(const Capsule&, const Vec3&, const Vec3&, SweepHit& out) const override
        { out = SweepHit{}; return false; }
        bool QueryWater(const Vec3&, WaterQuery& out) const override
        { out.inWater = true; out.surfaceY = 42.0f; return true; }
    };

    int g_fail = 0;
    void check(bool cond, const char* msg)
    { if (!cond) { std::printf("FAIL: %s\n", msg); ++g_fail; } }
}

int main()
{
    FakeTerrain terrain;
    IWorldCollider::Capsule cap; cap.radius = 0.3f; cap.height = 1.8f;

    // Cylindre centré en (5,0), rayon 0.5, de Y=0 à Y=3.
    PropCylinder cyl{ 5.0f, 0.0f, 0.5f, 0.0f, 3.0f };

    // 1) Capsule traversant le cylindre : doit être arrêtée, normale horizontale.
    {
        CompositeWorldCollider c(&terrain); c.AddCylinder(cyl);
        IWorldCollider::SweepHit hit;
        bool h = c.SweepCapsule(cap, Vec3{0,1,0}, Vec3{10,1,0}, hit);
        check(h && hit.hit, "traversee: hit attendu");
        check(hit.fraction < 1.0f, "traversee: fraction < 1");
        check(std::fabs(hit.normal.y) < 1e-3f, "traversee: normale horizontale");
        // Contact attendu vers x ~ 5 - (0.3+0.5) = 4.2 -> fraction ~0.42
        check(hit.fraction > 0.3f && hit.fraction < 0.55f, "traversee: fraction plausible");
    }

    // 2) Capsule passant à côté (z=5) : pas de hit.
    {
        CompositeWorldCollider c(&terrain); c.AddCylinder(cyl);
        IWorldCollider::SweepHit hit;
        bool h = c.SweepCapsule(cap, Vec3{0,1,5}, Vec3{10,1,5}, hit);
        check(!h && !hit.hit, "a_cote: pas de hit");
    }

    // 3) Capsule au-dessus du cylindre (Y=10) : pas de hit (hors [baseY,topY]).
    {
        CompositeWorldCollider c(&terrain); c.AddCylinder(cyl);
        IWorldCollider::SweepHit hit;
        bool h = c.SweepCapsule(cap, Vec3{0,10,0}, Vec3{10,10,0}, hit);
        check(!h, "au_dessus: pas de hit");
    }

    // 4) Sans cylindre : comportement = terrain seul (pas de hit).
    {
        CompositeWorldCollider c(&terrain);
        IWorldCollider::SweepHit hit;
        bool h = c.SweepCapsule(cap, Vec3{0,1,0}, Vec3{10,1,0}, hit);
        check(!h, "sans_cylindre: pas de hit");
    }

    // 5) QueryWater délégué au terrain.
    {
        CompositeWorldCollider c(&terrain);
        IWorldCollider::WaterQuery wq;
        bool inW = c.QueryWater(Vec3{0,0,0}, wq);
        check(inW && wq.inWater && std::fabs(wq.surfaceY - 42.0f) < 1e-3f, "querywater: delegation");
    }

    if (g_fail == 0) std::printf("CompositeWorldColliderTests: OK\n");
    return g_fail == 0 ? 0 : 1;
}
```

- [ ] **Step 4 : Déclarer source + test dans CMake**

Dans `src/CMakeLists.txt` : repérer la liste des sources du client (là où sont listés les autres `gameplay/*.cpp`, ex. `CharacterController.cpp`, `TerrainCollider.cpp`) et ajouter `gameplay/CompositeWorldCollider.cpp`. Repérer la déclaration des tests gameplay existants (ex. `add_executable(... Tests ...)` + `add_test(...)` pour `CharacterController`/`TerrainCollider`) et déclarer de la même manière `CompositeWorldColliderTests` (mêmes includes/link que les tests gameplay voisins). Suivre exactement le motif des tests voisins (target name, `add_test`, dossier `gameplay/tests/`).

- [ ] **Step 5 : Vérification compilation/tests → CI (Task 11)**

Pas de build local. Cohérence vérifiée à la compilation CI et `ctest` lancera `CompositeWorldColliderTests`.
Expected (en CI) : test `CompositeWorldColliderTests` PASS.

- [ ] **Step 6 : Commit**

```powershell
git add src/client/gameplay/CompositeWorldCollider.h src/client/gameplay/CompositeWorldCollider.cpp src/client/gameplay/tests/CompositeWorldColliderTests.cpp src/CMakeLists.txt
git commit -m @'
feat(gameplay): CompositeWorldCollider (terrain + cylindres de props)

Combine TerrainCollider et cylindres verticaux ; SweepCapsule retourne le hit
le plus proche, QueryWater delegue au terrain. Tests du sweep cylindre.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
'@
```

---

## Task 6 : Chargement de `world.scenery` + cylindres + branchement collision dans Engine

**Files:**
- Modify: `src/client/app/Engine.h` (struct `SceneryInstance`, `m_scenery`, `m_worldCollider`, déclarations `LoadScenery`, helper)
- Modify: `src/client/app/Engine.cpp` (parse scenery, baking partagé, registre cylindres, branchement Update)

**Note exécution :** vérification en CI (Task 11).

- [ ] **Step 1 : Déclarer les nouveaux membres dans `Engine.h`**

Près des déclarations existantes (`m_interactables`, `m_props`, `m_terrainCollider`, `LoadInteractableProps`, `RecordPropsGeometry`), ajouter :
```cpp
    // Décor solide non interactif (arbres, props) chargé depuis world.scenery.
    struct SceneryInstance
    {
        std::string meshPath;
        float x = 0.0f, z = 0.0f;
        float yawDeg = 0.0f;
        float scale = 1.0f;
        float collisionRadius = 0.0f; ///< 0 = auto (empreinte XZ du mesh)
    };
    std::vector<SceneryInstance> m_scenery;

    /// Charge world.scenery (décor solide) : mesh baké comme les props + cylindre
    /// de collision enregistré dans m_worldCollider. Appelée au boot après le terrain.
    void LoadScenery();

    // Collisionneur composite (terrain + cylindres props/scenery/PNJ). Branché dans
    // CharacterController::Update à la place du TerrainCollider nu.
    engine::gameplay::CompositeWorldCollider m_worldCollider;
```
Ajouter l'include `#include "src/client/gameplay/CompositeWorldCollider.h"` en tête de `Engine.h` (ou `.cpp` si Engine.h ne veut pas la dépendance ; préférer `.cpp` + forward si possible, mais le membre par valeur exige l'include dans `Engine.h`).

- [ ] **Step 2 : Initialiser le terrain dans le composite + factoriser le baking**

Dans `Engine.cpp`, là où `m_terrainCollider` est prêt (après `BindWater`, avant la création du `CharacterController` ~ligne 4806–4865), ajouter :
```cpp
    m_worldCollider.SetTerrain(&m_terrainCollider);
    m_worldCollider.ClearCylinders();
```
Refactor : extraire le corps de baking d'un prop (boucle interne de `LoadInteractableProps`, lignes ~9824–9957) dans une méthode privée réutilisable :
```cpp
    // Construit un PropRenderable (sommets cuits en monde, ancrés au sol) + enregistre
    // un cylindre de collision. \param solid : si true, ajoute le cylindre.
    // \param collisionRadius : rayon cylindre (0 = empreinte XZ auto du mesh baké).
    bool Engine::BuildPropFromMesh(const std::string& meshPath, float wx, float wz,
                                   float yawDeg, float rotXDeg, float scale,
                                   int interactableIndex, bool solid, float collisionRadius);
```
Le corps reprend la logique existante (chargement CPU, baking monde, lift au sol, création matériaux/parties) et, à la fin, si `solid`, calcule le cylindre :
```cpp
    // Cylindre de collision : base = sol, top = sol + hauteur monde du mesh ; rayon
    // = collisionRadius si > 0, sinon empreinte XZ max autour de (wx, wz).
    if (solid)
    {
        const float groundY = m_terrainCollider.GroundHeightAt(wx, wz);
        float topY = groundY + (cpu->localMaxY - cpu->localMinY) * scale; // hauteur approx.
        float radius = collisionRadius;
        if (radius <= 0.0f)
        {
            float maxR = 0.0f;
            for (const auto& v : cpu->vertices) // sommets déjà en monde après baking
            {
                const float ddx = v.pos[0] - wx, ddz = v.pos[2] - wz;
                maxR = std::max(maxR, std::sqrt(ddx*ddx + ddz*ddz));
            }
            radius = maxR > 0.05f ? maxR : 0.5f;
        }
        m_worldCollider.AddCylinder(engine::gameplay::PropCylinder{ wx, wz, radius, groundY, topY });
    }
```
`LoadInteractableProps` appelle ce helper pour chaque interactable avec un mesh **statique** (le coffre = solide, `collisionRadius=0` → auto). Le villageois (NPC, mesh `.glb` skinné) : ne pas le baker en static mesh ; lui ajouter directement un cylindre PNJ (cf. Step 3).

- [ ] **Step 3 : Cylindre du villageois (PNJ)**

Dans `LoadInteractableProps`, pour chaque interactable `isNpc == true`, ajouter un cylindre dédié (pas de baking static) :
```cpp
    if (e.isNpc)
    {
        const float gy = m_terrainCollider.GroundHeightAt(e.position.x, e.position.z);
        m_worldCollider.AddCylinder(engine::gameplay::PropCylinder{
            e.position.x, e.position.z, 0.4f, gy, gy + 1.8f });
        continue; // le rendu du PNJ passe par le système skinné, pas par m_props
    }
```
(Vérifier que le rendu actuel du villageois ne dépend pas de son passage dans `m_props` ; d'après le code, le mesh `.glb` ne se charge pas proprement en static — le PNJ est visuellement géré ailleurs. Si le `continue` retire un rendu attendu, retomber sur le comportement antérieur pour le rendu et n'ajouter QUE le cylindre.)

- [ ] **Step 4 : Implémenter `LoadScenery`**

```cpp
    void Engine::LoadScenery()
    {
        m_scenery.clear();
        const int n = static_cast<int>(m_cfg.GetInt("world.scenery.count", 0));
        for (int i = 0; i < n; ++i)
        {
            const std::string base = "world.scenery." + std::to_string(i) + ".";
            SceneryInstance s;
            s.meshPath = m_cfg.GetString(base + "mesh", "");
            if (s.meshPath.empty()) continue;
            s.x = static_cast<float>(m_cfg.GetDouble(base + "x", 0.0));
            s.z = static_cast<float>(m_cfg.GetDouble(base + "z", 0.0));
            s.yawDeg = static_cast<float>(m_cfg.GetDouble(base + "yaw_deg", 0.0));
            s.scale = static_cast<float>(m_cfg.GetDouble(base + "scale", 1.0));
            s.collisionRadius = static_cast<float>(m_cfg.GetDouble(base + "collision_radius", 0.0));
            m_scenery.push_back(s);
        }
        for (const auto& s : m_scenery)
            BuildPropFromMesh(s.meshPath, s.x, s.z, s.yawDeg, 0.0f, s.scale,
                              /*interactableIndex*/ -1, /*solid*/ true, s.collisionRadius);
        LOG_INFO(Render, "[Scenery] {} element(s) charge(s)", static_cast<int>(m_scenery.size()));
    }
```
Le format `world.scenery.count` + `world.scenery.<i>.<champ>` correspond à la convention de lecture indexée existante (cf. `world.interactables.count`). **Le script `scatter_forest.py` (Task 7) écrit ce format.**
Appeler `LoadScenery();` juste après `LoadInteractableProps();` (~ligne 4863).

- [ ] **Step 5 : Brancher le composite dans `CharacterController::Update`**

Repérer l'appel `m_characterController.Update(dt, input, m_terrainCollider)` (~ligne 5018) et remplacer le dernier argument par `m_worldCollider`. Vérifier qu'aucun autre appel n'utilise encore `m_terrainCollider` directement comme collider de mouvement (la nage via `QueryWater` reste correcte : déléguée au terrain par le composite).

- [ ] **Step 6 : Vérif compilation/tests → CI (Task 11)**

- [ ] **Step 7 : Commit**

```powershell
git add src/client/app/Engine.h src/client/app/Engine.cpp
git commit -m @'
feat(world): chargement world.scenery + collision props via CompositeWorldCollider

Decor solide (arbres) bake comme les props + cylindre de collision. Coffre et
villageois solides. CharacterController utilise desormais le collisionneur
composite (terrain + cylindres).

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
'@
```

---

## Task 7 : Générer la forêt (`scatter_forest.py` → `world.scenery`)

**Files:**
- Create: `tools/world_gen/scatter_forest.py`
- Modify: `config.json` (bloc `world.scenery` injecté par le script)

- [ ] **Step 1 : Écrire le script**

```python
# tools/world_gen/scatter_forest.py
# Génère ~80 arbres dans config.json > world.scenery (format indexé count + .<i>.<champ>).
# Déterministe (graine fixe). Disperse autour du bassin d'eau (centre 55,0, half 40),
# exclut l'eau, le spawn (0,0), le villageois (4,0), le coffre (-4,0), et impose une
# distance min entre arbres. Idempotent : remplace le bloc world.scenery existant.
#
# Lancement : python tools/world_gen/scatter_forest.py [--count 80]
import json, random, math, sys, os

REPO = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
CONFIG = os.path.join(REPO, "config.json")

SPECIES = (
    [(f"meshes/props/CommonTree_{i}.gltf", 0.6) for i in range(1, 6)] +
    [(f"meshes/props/Pine_{i}.gltf",       0.5) for i in range(1, 6)] +
    [(f"meshes/props/DeadTree_{i}.gltf",   0.5) for i in range(1, 6)] +
    [(f"meshes/props/TwistedTree_{i}.gltf",0.8) for i in range(1, 6)]
)

WATER_CX, WATER_CZ, WATER_HALF = 55.0, 0.0, 40.0
EXCLUDE = [(0.0, 0.0, 6.0), (4.0, 0.0, 6.0), (-4.0, 0.0, 6.0)]  # (x,z,rayon)
MIN_DIST = 3.0
AREA = (-25.0, 125.0, -75.0, 75.0)  # xmin, xmax, zmin, zmax

def in_water(x, z):
    return abs(x - WATER_CX) <= WATER_HALF and abs(z - WATER_CZ) <= WATER_HALF

def excluded(x, z):
    for ex, ez, er in EXCLUDE:
        if (x-ex)**2 + (z-ez)**2 < er*er:
            return True
    return False

def main():
    count = 80
    if "--count" in sys.argv:
        count = int(sys.argv[sys.argv.index("--count")+1])
    rng = random.Random(20260531)
    placed = []
    attempts = 0
    while len(placed) < count and attempts < count*200:
        attempts += 1
        x = rng.uniform(AREA[0], AREA[1])
        z = rng.uniform(AREA[2], AREA[3])
        if in_water(x, z) or excluded(x, z):
            continue
        if any((x-px)**2 + (z-pz)**2 < MIN_DIST*MIN_DIST for px, pz, *_ in placed):
            continue
        mesh, trunk = rng.choice(SPECIES)
        placed.append((x, z, mesh, trunk, rng.uniform(0, 360), rng.uniform(0.8, 1.3)))

    scenery = {"count": len(placed)}
    for i, (x, z, mesh, trunk, yaw, scale) in enumerate(placed):
        scenery[str(i)] = {
            "mesh": mesh, "x": round(x, 2), "z": round(z, 2),
            "yaw_deg": round(yaw, 1), "scale": round(scale, 2),
            "collision_radius": trunk,
        }

    with open(CONFIG, "r", encoding="utf-8") as f:
        cfg = json.load(f)
    cfg.setdefault("world", {})
    cfg["world"]["scenery"] = scenery
    with open(CONFIG, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=4, ensure_ascii=False)
    print(f"[scatter_forest] {len(placed)} arbres ecrits dans world.scenery "
          f"({attempts} tentatives)")

if __name__ == "__main__":
    main()
```

**Attention format config :** vérifier d'abord comment `config.json` est structuré (le bloc `world` est-il imbriqué tel quel ?) et comment `ConfigStore` lit `world.scenery.count` / `world.scenery.<i>.<champ>`. Si la lecture indexée attend des **clés string** (`"0"`, `"1"`…) comme pour `world.interactables`, le format ci-dessus convient. Adapter `cfg["world"]["scenery"]` au chemin réel du bloc `world` dans le JSON (il peut être imbriqué sous une autre clé — inspecter avant d'écrire).

- [ ] **Step 2 : Lancer le script**

Run (PowerShell) :
```powershell
python tools/world_gen/scatter_forest.py
```
Expected : `[scatter_forest] 80 arbres ecrits dans world.scenery (...)`.

- [ ] **Step 3 : Vérifier le JSON et le compte**

Run (PowerShell) :
```powershell
$c = Get-Content config.json -Raw | ConvertFrom-Json
"scenery count = " + $c.world.scenery.count
```
Expected : `scenery count = 80` (adapter le chemin `$c.world.scenery` si le bloc `world` est imbriqué).

- [ ] **Step 4 : Commit**

```powershell
git add tools/world_gen/scatter_forest.py config.json
git commit -m @'
feat(world): foret riveraine de ~80 arbres (world.scenery) + script generateur

scatter_forest.py disperse 4 essences autour du bassin d'eau (deterministe),
exclut eau/spawn/coffre/villageois. Arbres solides (collision tronc).

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
'@
```

---

## Task 8 : Colline (`raise_hill.py` → heightmap)

**Files:**
- Create: `tools/world_gen/raise_hill.py`
- Modify: `game/data/zones/demo_plains/terrain_height.r16h`

- [ ] **Step 1 : Écrire le script**

```python
# tools/world_gen/raise_hill.py
# Ajoute une colline gaussienne au heightmap demo_plains (format HAMP, 1025x1025 u16).
# Mapping terrain (defaults, non surcharges) : world_size 1024, origin -512,
# 1 m/texel -> texel = world + 512 ; height_scale 200 m -> dn = metres/200.
# Idempotent : sauvegarde .r16h.bak au 1er run et repart toujours de la sauvegarde.
#
# Lancement : python tools/world_gen/raise_hill.py
import struct, os, math, shutil

REPO = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
HM = os.path.join(REPO, "game", "data", "zones", "demo_plains", "terrain_height.r16h")
BAK = HM + ".bak"

HEIGHT_SCALE = 200.0
ORIGIN = -512.0
HILL_WX, HILL_WZ = -60.0, -55.0   # centre monde (nord-ouest)
HILL_RADIUS_M = 40.0
HILL_HEIGHT_M = 15.0

def main():
    if not os.path.exists(BAK):
        shutil.copyfile(HM, BAK)        # 1er run : sauvegarde de reference
    with open(BAK, "rb") as f:          # repart toujours du heightmap d'origine
        data = f.read()
    magic, w, h = struct.unpack_from("<III", data, 0)
    assert magic == 0x504D4148, "magic HAMP attendu"
    hdr = 12
    heights = list(struct.unpack_from(f"<{w*h}H", data, hdr))

    cx = HILL_WX + 512.0   # texel centre
    cz = HILL_WZ + 512.0
    r = HILL_RADIUS_M       # 1 m/texel
    peak_dn = (HILL_HEIGHT_M / HEIGHT_SCALE) * 65535.0
    two_sigma2 = 2.0 * (r / 2.0) ** 2  # sigma = r/2 : ~0 au bord du rayon

    x0, x1 = max(0, int(cx - r)), min(w-1, int(cx + r))
    z0, z1 = max(0, int(cz - r)), min(h-1, int(cz + r))
    changed = 0
    for z in range(z0, z1+1):
        for x in range(x0, x1+1):
            d2 = (x - cx)**2 + (z - cz)**2
            if d2 > r*r:
                continue
            add = peak_dn * math.exp(-d2 / two_sigma2)
            idx = z * w + x
            val = heights[idx] + add
            heights[idx] = max(0, min(65535, int(round(val))))
            changed += 1

    out = bytearray(data[:hdr])
    out += struct.pack(f"<{w*h}H", *heights)
    with open(HM, "wb") as f:
        f.write(out)
    print(f"[raise_hill] colline {HILL_HEIGHT_M}m r={HILL_RADIUS_M}m en "
          f"({HILL_WX},{HILL_WZ}) -> {changed} texels modifies")

if __name__ == "__main__":
    main()
```

- [ ] **Step 2 : Lancer le script**

Run (PowerShell) :
```powershell
python tools/world_gen/raise_hill.py
```
Expected : `[raise_hill] colline 15m r=40m en (-60,-55) -> N texels modifies` (N ≈ π·40² ≈ 5000).

- [ ] **Step 3 : Vérifier l'intégrité du fichier**

Run (PowerShell) :
```powershell
$len = (Get-Item game/data/zones/demo_plains/terrain_height.r16h).Length
"taille = $len (attendu 2101262)"
```
Expected : `taille = 2101262` (12 + 1025·1025·2). Vérifier aussi qu'un `.r16h.bak` a été créé.

- [ ] **Step 4 : Commit**

```powershell
git add tools/world_gen/raise_hill.py game/data/zones/demo_plains/terrain_height.r16h
git commit -m @'
feat(world): colline ~15m au nord-ouest (heightmap) + script generateur

raise_hill.py ajoute une bosse gaussienne (centre monde -60,-55, r 40m) au
heightmap demo_plains, idempotent (repart d'une sauvegarde .bak). Collision
terrain automatique. .bak non versionne (gitignore).

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
'@
```
(Ajouter `terrain_height.r16h.bak` au `.gitignore` racine ou de la zone si non couvert.)

---

## Task 9 : Documentation

**Files:**
- Modify: `tools/asset_pipeline/README.md`
- Modify: `docs/superpowers/specs/2026-05-31-foret-colline-collision-demo-plains-design.md`

- [ ] **Step 1 : Documenter la conversion props Blender**

Ajouter à `tools/asset_pipeline/README.md` une section « Conversion des props (Blender headless) » : objet du script `convert_props_blender.py`, prérequis Blender 5.1, commande de lancement, recette trim (textures partagées `T_Trim_*`, matériaux `MI_Trim_*`), et la validation par diff contre `Barrel.gltf`.

- [ ] **Step 2 : Marquer le spec comme implémenté**

En tête du spec, passer `Statut : design validé` à `Statut : implémenté (PR feat/demo-plains-foret-colline-collision)`.

- [ ] **Step 3 : Commit**

```powershell
git add tools/asset_pipeline/README.md docs/superpowers/specs/2026-05-31-foret-colline-collision-demo-plains-design.md
git commit -m @'
docs: conversion props Blender (README) + statut spec implemente

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
'@
```

---

## Task 10 : Push, PR et vérification CI

**Files:** aucun (intégration).

- [ ] **Step 1 : Push de la branche**

```powershell
git push -u origin feat/demo-plains-foret-colline-collision
```

- [ ] **Step 2 : Ouvrir la PR**

```powershell
gh pr create --base main --head feat/demo-plains-foret-colline-collision `
  --title "feat(world): foret riveraine + colline + collision props (demo_plains)" `
  --body @'
## Resume
- Retire les props decoratifs autour de l'eau (garde coffre + villageois).
- Convertit les 244 props FBX manquants en glTF (Blender headless).
- Foret riveraine de ~80 arbres (4 essences) via world.scenery.
- Colline ~15 m au nord-ouest (heightmap).
- Collision props <-> personnage via CompositeWorldCollider (cylindres) + tests.

## Deploiement
✅ Client uniquement, pas de redeploiement serveur (aucun opcode/handler/migration/cle serveur).

🤖 Generated with [Claude Code](https://claude.com/claude-code)
'@
```

- [ ] **Step 3 : Surveiller la CI (build-windows + build-linux/ctest)**

Attendre la fin des workflows (build Windows ~30 min). Vérifier que `build-linux` compile **et** que `ctest` passe (dont `CompositeWorldColliderTests`), et que `build-windows` compile.
```powershell
gh pr checks --watch
```
Expected : tous les checks verts. Corriger toute erreur de compilation (notamment includes, noms de struct `StaticMeshCpuData`, signature `BuildPropFromMesh`, intégration CMake) et re-push jusqu'au vert.

- [ ] **Step 4 : Indiquer à l'utilisateur quand merger**

Une fois la CI verte : informer l'utilisateur que la PR est prête à merger, et que **le client doit être relancé** par ses soins (pas de build local) pour vérifier visuellement : props d'eau retirés, coffre + villageois présents et **solides**, forêt visible et solide, colline marchable au nord-ouest. Rappeler le knob `--count` de `scatter_forest.py` si la forêt est trop lourde.

---

## Auto-revue (couverture spec)

- Spec A (retrait props eau) → Task 3. ✓
- Spec B (conversion 244 props Blender + validation diff) → Tasks 1, 2. ✓
- Spec C (forêt ~80 arbres, world.scenery, script) → Tasks 6 (chargement), 7 (génération). ✓
- Spec D (colline ~15 m heightmap, idempotent) → Task 8. ✓
- Spec E (CompositeWorldCollider + cylindres + branchement + tests) → Tasks 4 (maxY), 5 (collider+tests), 6 (registre+branchement). ✓
- Spec F (docs) → Task 9. ✓
- Déploiement client-only → Task 10 PR body. ✓
- Cohérence types : `PropCylinder`, `CompositeWorldCollider`, `SceneryInstance`, `BuildPropFromMesh`, `LoadScenery`, `localMinY/localMaxY` employés de façon cohérente entre tâches. ✓
- Contrainte « pas de build local » explicitée ; vérif C++ reportée en CI (Task 10). ✓
