# CHAR-MODEL.23 — Code couleur de race (placeholder skin) — `MaterialOverride`

## Dépendances
- CHAR-MODEL.5 (pipeline skinned dans deferred — base d'extension)
- CHAR-MODEL.15 à 22 (les 8 races livrées avec leur `colorCode`)

## Cadrage

Implémenter un **code couleur de race** qui colorie chaque race avec
une teinte distincte tant que les vraies skins ne sont pas livrées.
La teinte est **un material override** appliqué au matériau de base
du submesh `body` (et autres `submeshMaterialSlots: 0`) — pas une
texture, juste un facteur de teinte uploadé en UBO matériau.

À l'issue de ce ticket :
- une table `RaceColorTable` lit les `colorCode` depuis les manifests
  race ;
- chaque `SkinnedRenderable` peut référencer un `MaterialOverride` qui
  applique la teinte ;
- `races.json` est étendu pour inclure les races manquantes (les huit
  races jouables canoniques) si nécessaire.

---

## Pré-requis vérifiables

```bash
git status
ls engine/render/SkinnedRenderable.h           # CHAR-MODEL.5
ls game/data/models/humain/humain.race.json   # CHAR-MODEL.15
ls game/data/models/{elfe,nain,orc,demon,chevalier_dragon,orkh,gobelin}/*.race.json
cat game/data/races/races.json | head -10
```

---

## Spécification technique

### Table de couleurs

```cpp
// engine/render/RaceColorTable.h
namespace engine::render
{
    struct RaceColorEntry
    {
        std::string id;          // ex. "humains"
        engine::math::Vec3 tint; // RGB linéaire [0,1]
    };

    /// Charge les `colorCode` depuis tous les manifests race trouvés sous
    /// game/data/models/<race>/<race>.race.json. Construit une table indexée
    /// par id de race.
    class RaceColorTable
    {
    public:
        bool LoadFromDataDir(std::string_view dataDir = "game/data/models");

        const RaceColorEntry* Find(std::string_view raceId) const;
        std::span<const RaceColorEntry> All() const;

    private:
        std::vector<RaceColorEntry> m_entries;
        std::unordered_map<std::string, uint32_t> m_byId;
    };
}
```

### MaterialOverride

```cpp
// engine/render/MaterialOverride.h (nouveau)
namespace engine::render
{
    /// Surcharge légère de matériau, uploadée en push constant ou small UBO.
    /// Identifié par un id stable pour batching.
    struct MaterialOverride
    {
        engine::math::Vec3 tint    = {1, 1, 1}; // multiplie albedo
        float              tintMix = 0.0f;       // 0 = pas de tint, 1 = tint pure
    };

    class MaterialOverridePool
    {
    public:
        uint32_t Register(const MaterialOverride& m); // 0 = identité (defaut)
        const MaterialOverride& Get(uint32_t id) const;
    };
}
```

`SkinnedRenderable.materialOverrideId` (déjà déclaré en CHAR-MODEL.5)
référence un id retourné par `Register`. Le shader fragment skinné
applique :

```glsl
vec3 finalAlbedo = mix(baseAlbedo, baseAlbedo * tint, tintMix);
```

Le `MaterialOverride` est passé via un **push constant** (16 B) attaché
au pipeline skinned, pour éviter un descriptor set par instance.

### Extension `races.json`

Vérifier que les 8 races canoniques sont présentes :

```
humains, elfes, nains, orcs, demons, chevalier_dragon, orkh, gobelin
```

Mappage avec les ids existants :
- `humains`, `elfes`, `nains`, `orcs`, `demons` → déjà présents.
- `chevalier_dragon` → ajouter (renommer `divins` ou créer entrée à
  côté, selon décision produit ; **par défaut : ajouter en plus, ne pas
  supprimer `divins`**).
- `orkh` → ajouter (idem, à côté de `corrompus`).
- `gobelin` → ajouter (à côté de `morts_vivants`).

Chaque entrée nouvelle doit comporter `displayName`, `description`,
`racials`, `iconPath`, `defaultSkinColors`, `defaultHairColors`,
`defaultEyeColors` (cohérent avec les entrées existantes).

### Câblage CharacterAnimator

Le `CharacterAnimator` (CHAR-MODEL.26) résout `race.id` → `RaceColorTable.
Find(raceId)` → `MaterialOverridePool.Register({tint, 0.5})` → écrit
l'id dans `SkinnedRenderable.materialOverrideId`.

---

## Liste des fichiers

**Créés :**
- `engine/render/RaceColorTable.h` + `.cpp`
- `engine/render/MaterialOverride.h` + `.cpp`
- `tests/render/RaceColorTable_LoadAll_test.cpp`
- `tests/render/MaterialOverride_PushConstant_test.cpp`

**Modifiés :**
- `engine/render/GeometryPass.cpp` (push constant pipeline skinned, frag
  shader bind tint)
- `engine/render/shaders/skinned_geometry.frag.glsl` (ou frag G-Buffer
  partagé : ajout `pc.tint` + `pc.tintMix`)
- `game/data/races/races.json` (3 nouvelles races : `chevalier_dragon`,
  `orkh`, `gobelin`)
- `CMakeLists.txt`

---

## CMakeLists.txt

```cmake
target_sources(engine_core PRIVATE
    engine/render/RaceColorTable.h
    engine/render/RaceColorTable.cpp
    engine/render/MaterialOverride.h
    engine/render/MaterialOverride.cpp
)
```

---

## Critères d'acceptation

- [ ] Build Windows + Linux propre.
- [ ] Test `RaceColorTable_LoadAll_test` : charge les 8 manifests race,
      `Find("humains")` retourne `tint = #C68642`, `Find("orcs")` retourne
      `tint = #5E6E3F`, `Find("inexistant")` retourne `nullptr`.
- [ ] Test `MaterialOverride_PushConstant_test` : rend deux instances
      du même mesh avec deux overrides différents → couleurs visibles
      distinctes sur le FBO test.
- [ ] `races.json` passe la validation JSON ; les 8 ids canoniques
      présents.
- [ ] Aucune régression sur le rendu statique (qui n'utilise pas
      `MaterialOverride`).

---

## Anti-objectifs

- **Ne pas** introduire de texture par race (placeholder coloré
  uniquement).
- **Ne pas** supprimer les entrées existantes de `races.json` (`divins`,
  `corrompus`, `morts_vivants` peuvent rester en place pour
  compatibilité).
- **Ne pas** brancher au gameplay : seul l'animator (CHAR-MODEL.26)
  applique la table.
- **Ne pas** étendre le système d'override aux meshes statiques —
  réservé skinned dans cette release.
