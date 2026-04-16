# Checklist — zone démo `demo_plains` (≈ 20–30 min)

Objectif : vérifier le **terrain** du dépôt et la **chaîne World Editor → zones** sans parcours opaque. Le `zone_id` de référence est **`demo_plains`** (`game/data/zones/demo_plains/`).

Références : [world_editor_zone_pipeline.md](world_editor_zone_pipeline.md) (spec complète), [terrain_et_world_editor.md](terrain_et_world_editor.md) (rendu terrain), tickets `tickets/world/008`–`013` (splat, instances, arbres catalogue, herbe, routes, validation client).

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
| `terrain_splat.slap` | Splat SLAP 256×256 (bande « route » terre — ticket **012**). |
| `terrain_grass.grms` | Masque herbe R8 256×256 (bande légère — tickets **010** / **012**). |
| `runtime_manifest.json` | Trace export / tooling (format **v3** : inclut `terrain_splatmap` + `terrain_grass_mask` si présents). |
| `layout_from_editor.json` | Exemple avec **1** instance glTF (référence flux **009** ; le client jeu consomme surtout les chunks après `zone_builder` — voir §5). |
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

**Clés utiles** : `paths.content`, `render.terrain.heightmap`, `render.terrain.splatmap`, `render.terrain.grass_mask`, `render.terrain.grass_mask_visual_strength`, `render.terrain.hole_mask` (souvent vides en démo).

**Parcours « zone démo complète » (terrain WE → client)** — même principe qu’une zone exportée depuis l’éditeur :

```json
"terrain": {
  "heightmap": "zones/demo_plains/terrain_height.r16h",
  "splatmap": "zones/demo_plains/terrain_splat.slap",
  "grass_mask": "zones/demo_plains/terrain_grass.grms",
  "grass_mask_visual_strength": 0.35,
  "hole_mask": ""
}
```

Au boot, le client émet des logs explicites si `splatmap` ou `grass_mask` sont **vides** (fichiers optionnels → comportements par défaut documentés). Voir `Engine.cpp` autour de l’init `TerrainRenderer`.

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

---

## 5. Ticket **012** — validation bout-en-bout (sans accès au code source)

Objectif : prouver que les livrables **008–013** (splat, instances JSON d’édition, catalogue arbres WE, masque herbe, routes dans le SLAP, validation client) s’intègrent dans le **parcours joueur** (fichiers sous `paths.content`, logs au boot, rendu terrain). Références : [world_editor_zone_pipeline.md](world_editor_zone_pipeline.md), tickets `tickets/world/008` … `013`.

### 5.1 Prérequis (testeur)

1. Binaire **client jeu** (pas `lcdlln_world_editor`) + dossier **`game/data`** complet (clone dépôt ou package équivalent).
2. Aucun IDE ni checkout obligatoire si les données sont déjà fournies.

### 5.2 Données `demo_plains` (dépôt)

Sous `game/data/zones/demo_plains/` : après `Generate-DemoPlainsAssets.ps1`, présence de **`terrain_splat.slap`** et **`terrain_grass.grms`** (générés avec le heightmap démo). Le `runtime_manifest.json` embarqué est en **v3** et cite ces chemins (traçabilité export type World Editor).

### 5.3 Config client (copier-coller temporaire)

Dans `config.json` à la racine, section `render.terrain` :

- Pointer **`heightmap`**, **`splatmap`**, **`grass_mask`** vers les fichiers `zones/demo_plains/…` comme en **§2** (exemple JSON ci-dessus).
- **`grass_mask_visual_strength`** : `0` désactive la teinte masque ; `0.35` (défaut config) rend la bande GRMS visible sur le sol.

### 5.4 Lancement et observations

1. Lancer le client ; vérifier les logs **`[Boot]`** : chemins splat/herbe si renseignés, ou messages **« splatmap vide »** / **« grass_mask vide »** si vous testez la régression (clés vides → pas de crash).
2. **Sol** : heightmap démo (plat 64×64).
3. **Splat auteur** : bande verticale « terre » (canal G) au centre de la carte splat.
4. **Détail herbe (010)** : léger assombrissement / teinte sur la bande si masque + force > 0.
5. **Routes (011)** : toute route tracée en WE est **baked** dans le SLAP ; pas de relecture automatique du JSON `routes` côté client — la vérification 012 est **visuelle sur le SLAP** exporté ou en rejouant les mêmes chemins de fichiers.

### 5.5 Instances décor (009 / 013) et streaming

- Le fichier **`layout_from_editor.json`** sous la zone sert au pipeline **`zone_builder`** (fusion dans chunks, etc.) — voir **§3** et le pipeline §5.1 de [world_editor_zone_pipeline.md](world_editor_zone_pipeline.md).
- Le **client** charge en priorité les **chunks** (`instances.bin`, …) lorsque le streaming / packaging est branché ; la checklist **012** ne garantit pas l’affichage glTF direct depuis seul `layout_from_editor.json` sans étape builder.
- **013** : dans le JSON d’édition carte, vérifier qu’au moins une instance arbre porte `species_id` + `shape_variant` cohérents avec `world_editor/tree_species_catalog.json` après sauvegarde / rechargement (WE).
- Pour un test **décor 3D** bout-en-bout : exporter depuis le WE, exécuter `zone_builder` + scripts `tools/world/` comme en §3, puis valider les chunks.

### 5.6 Régression (sans nouveaux fichiers)

Remettre `render.terrain.splatmap` et `render.terrain.grass_mask` à `""` : le moteur retombe sur **splat par défaut** et **masque nul** ; les logs le précisent ; **aucun crash** attendu.
