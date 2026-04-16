# Checklist — zone démo `demo_plains` (≈ 20–30 min)

Objectif : vérifier le **terrain** du dépôt et la **chaîne World Editor → zones** sans parcours opaque. Le `zone_id` de référence est **`demo_plains`** (`game/data/zones/demo_plains/`).

Références : [world_editor_zone_pipeline.md](world_editor_zone_pipeline.md) (spec complète), [terrain_et_world_editor.md](terrain_et_world_editor.md) (rendu terrain).

---

## 0. Prérequis (5 min)

1. Cloner le dépôt ; `paths.content` par défaut = **`game/data`** (voir `config.json` à la racine du dépôt).
2. Dépendances build : Vulkan SDK, CMake, toolchain (Visual Studio 2022 x64 ou Ninja + GCC/Clang), **vcpkg** selon `CMakePresets.json`.
3. Configurer puis compiler le moteur (ex. `cmake --preset vs2022-x64` puis build **Release** ou **Debug**).

---

## 1. Vérifier les fichiers zone démo (2 min)

Sous **`game/data/zones/demo_plains/`** vous devez trouver au minimum :

| Fichier | Rôle |
|---------|------|
| `terrain_height.r16h` | Heightmap HAMP 64×64 (plat, léger pour le dépôt). |
| `runtime_manifest.json` | Trace export / tooling (format v2). |
| `layout_from_editor.json` | Layout minimal (`instances` vide), compatible `zone_builder`. |
| `zone.meta`, `probes.bin`, `atmosphere.json` | Sortie alignée **zone_builder** (hashes contenu à 0 pour cette démo statique). |
| `chunks/chunk_0_0/chunk.meta`, `instances.bin` | Chunk MVP (0 instance). |

Pour **régénérer** uniquement les binaires (hors JSON texte) :  
`powershell -NoProfile -ExecutionPolicy Bypass -File tools/world/Generate-DemoPlainsAssets.ps1`  
(depuis la racine du dépôt).

---

## 2. Voir le terrain en jeu / moteur (10 min)

1. Ouvrir **`config.json`** (racine du dépôt).
2. Sous **`render.terrain`**, pointer temporairement le heightmap vers la démo (chemins **relatifs au content**) :

   - **`heightmap`** : `zones/demo_plains/terrain_height.r16h`

3. Lancer le client / binaire moteur habituel (hors World Editor si vous testez le chemin client).
4. Vérifier dans les logs l’absence d’erreur de chargement heightmap ; le terrain doit s’afficher (plat à cette résolution).

Remettre ensuite `render.terrain.heightmap` à la valeur d’équipe si besoin (souvent `terrain/heightmap.r16h`).

**Clés utiles** : `paths.content`, `render.terrain.heightmap`, `render.terrain.splatmap`, `render.terrain.hole_mask` (souvent vides en démo).

**Optionnel — sondes / ambiance depuis la démo** (même `contentHash` = 0 entre `zone.meta` et `probes.bin` dans ce bundle) : ajouter sous **`world`** dans `config.json` :

- `zone_meta_path` : `zones/demo_plains/zone.meta`
- `probes_path` : `zones/demo_plains/probes.bin`
- `atmosphere_path` : `zones/demo_plains/atmosphere.json`

Retirer ces clés après test si l’équipe utilise d’autres chemins par défaut.

---

## 3. World Editor + export + chunks (10 min, optionnel mais recommandé)

1. Lancer **`lcdlln_world_editor`** (cible CMake **`world_editor_app`** ; sortie typique :  
   `<build>/<preset>/pkg/world_editor/lcdlln_world_editor(.exe)` — voir [README racine](../README.md) section World).
2. Charger ou créer une carte ; **Exporter runtime** vers `zones/<votre_id>/`.
3. Enchaîner avec **`zone_builder`** et le script **`tools/world/export_zone_with_chunks.ps1`** ou `.sh` (voir §5.1 du pipeline).

---

## 4. Critère « succès » minimal

- Heightmap **`demo_plains`** visible quand `render.terrain.heightmap` pointe dessus.
- Aucune erreur bloquante de parsing / fichiers manquants pour les chemins ci-dessus.

*(Délai indicatif 30 min : prévoir marge si première install toolchain / vcpkg.)*
