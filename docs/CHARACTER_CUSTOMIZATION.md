# Système de personnalisation de personnages (CHAR-MODEL.25)

Système *data-driven* de customisation des personnages : corps, tête, cheveux,
pilosité, traits raciaux, couleurs, proportions et morph targets, par race et
par genre.

## Vue d'ensemble

| Couche | Emplacement | Rôle |
|--------|-------------|------|
| Données de race (source) | `game/data/races/races.json` | Identité + palettes (partagé avec le MVP de création de perso) |
| Config de customisation | `game/data/configuration/` | Limites physiques, modules, traits, animations, équipement |
| Générateur | `tools/asset_pipeline/gen_race_configs.py` | Dérive `configuration/races/*.json` depuis `races.json` |
| Code C++ | `src/client/character_creation/CharacterCustomization*.{h,cpp}` | Chargement, validation, génération, résolution en assets |
| Pipeline assets | `tools/asset_pipeline/` + `inbox/` | inbox → game/data |

**Alignement sur l'existant** : les `raceId` sont ceux de `races.json`
(`humains`, `elfes`, `orcs`, `nains`, `morts_vivants`, `corrompus`, `divins`,
`demons`). Pas de taxonomie parallèle.

## Code C++

```cpp
#include "src/client/character_creation/CharacterCustomizationSystem.h"
using namespace engine::client;

CharacterCustomizationSystem sys;
sys.Initialize();                                  // découvre configuration/races/*.json

CharacterCustomization c = sys.GenerateRandomCustomization("orcs", "male");
if (sys.ValidateCustomization(c)) {
    ResolvedCharacterAssets assets = sys.ResolveCustomization(c);
    // assets.bodyMeshPath, assets.attachments[], assets.boneScales[],
    // assets.collisionRadius/Height, assets.skin/hair/eye ...
}
```

- `Initialize(base="game/data/configuration")` : charge **toutes** les races
  présentes dans `<base>/races/` (`std::filesystem`, aucun id codé en dur).
- `ValidateCustomization` / `GetValidationErrors` : vérifie race, genre, bornes
  (taille, corpulence, proportions), index de modules, ids de couleurs et index
  de traits raciaux.
- `MakeDefaultCustomization` / `GenerateRandomCustomization` : produisent des
  customisations **toujours valides**.
- `ResolveCustomization` : transforme l'état en assets concrets
  (`ResolvedCharacterAssets`) — chemins de mesh, attachements (socket+mesh),
  scaling d'os, dimensions de collision, textures de couleur.
- `ApplyCustomization` : **stub documenté**. Le moteur n'expose pas encore de
  scène à base de `GameObject`/`Skeleton`/composants ; cette fonction résout les
  assets et trace le plan d'application. Le câblage au rendu skinné réel
  (attachement GPU, scaling des os, upload textures) est laissé à un futur
  ticket. La *résolution* (`ResolveCustomization`), elle, est complète et testée.

Proportions (presets `body_proportions.json`) :
- `GetProportionPresets()` : liste des presets chargés.
- `DefaultMetricsForRace(raceId)` : métriques par défaut de la race.
- `ApplyProportionPreset(raceId, presetId, metrics)` : applique un preset puis
  **clampe aux limites de la race** (renvoie false si race/preset inconnu).
- `ClampMetricsToRace(raceId, metrics)` : borne des métriques arbitraires.

Sérialisation : `CharacterCustomization::ToJson()` / `FromJson()` (round-trip,
versionné `"1.0.0"`).

## Intégration UI (écran de création de personnage)

Le presenter `engine::client::CharacterCreationPresenter` charge le système à
l'`Init` (`<paths.content>/configuration`) et l'expose via
`GetCustomizationSystem()`. L'écran ImGui
`AuthImGuiRenderer::RenderCharCreateScreen`
(`src/client/render/auth/screens/AuthImGuiCharacterCreate.cpp`, `#if _WIN32`)
affiche le panneau **« Apparence physique »** :
- slider **Taille** (`heightScale`) borné à `[scaleMin, scaleMax]` de la race ;
- section repliable **« Proportions avancées »** : Longueur des jambes, Largeur
  des épaules, Corpulence — bornées aux plages de la race ;
- **Presets rapides** : un bouton par preset de `body_proportions.json`
  (`ApplyProportionPreset`, clampé à la race).

Les valeurs éditées vivent dans l'état du renderer (`m_charBodyMetrics`) et sont
réinitialisées aux défauts de la race quand la race change. **À brancher**
(suivi) : transmission des métriques au serveur (le payload de création est
encore `name + race` en MVP) et application au mesh 3D (cf. stub
`ApplyCustomization`).

Tests : `src/client/character_creation/tests/CharacterCustomizationTests.cpp`
(cible CTest `character_customization_tests`).

## Ajouter une race

1. **races.json** : ajouter l'entrée race (id, displayName, description,
   `defaultSkinColors`/`defaultHairColors`/`defaultEyeColors`).
2. **Générateur** : ajouter une entrée dans `RACE_SPECS`
   (`tools/asset_pipeline/gen_race_configs.py`) — taille, corpulence,
   proportions, types de corps, têtes/cheveux/pilosité, traits raciaux,
   gameplay, factions.
3. Lancer `python3 tools/asset_pipeline/gen_race_configs.py`.
4. **Modèles** : créer `game/data/models/characters/<id>/{base,heads,hair,...}`
   et `tools/asset_pipeline/inbox/characters/<id>/...`.
5. Déposer les FBX dans l'inbox, lancer `process_character_assets.py --race <id>`.

Aucune recompilation : `Initialize()` découvre la nouvelle race automatiquement.
Si une feature raciale d'un type inédit est ajoutée, l'enregistrer aussi dans
`kKnownRacialFeatures` (`CharacterCustomizationSystem.cpp`) **et** la liste du
générateur.

## Ajouter un type d'équipement

1. Déclarer le `armorType` (ou la catégorie d'arme) dans
   `configuration/equipment/armor_sets.json`.
2. Définir les sockets nécessaires dans
   `configuration/equipment/sockets_attachments.json` (bone + offset/rotation/
   scale). Les sockets s'appuient sur le squelette `humanoid_base`.
3. Déposer les meshes dans `inbox/equipment/<famille>/...` puis les traiter vers
   `game/data/models/equipment/...`.
4. L'héritage de scale (`scaleInheritance`) adapte l'équipement aux variations
   de taille via `ResolvedCharacterAssets.boneScales`.

## Workflow artiste 3D → jeu

```
Artiste 3D
   │  exporte FBX (conventions : voir FBX_REQUIREMENTS.md / CONVENTIONS_NAMING.md)
   ▼
tools/asset_pipeline/inbox/characters/<race>/<type>/   (FBX non versionnés)
   │  python3 process_character_assets.py --race <race>
   ▼
game/data/models/characters/<race>/...                 (versionnés)
   │  + inbox/processed/characters/<race>/...           (sources archivées)
   ▼
validate_fbx.py --dir game/data/models/characters/<race>
   ▼
référencé par configuration/races/<race>.json (chemins model) → chargé runtime
```

## Points d'attention

- **Sécurité** : valider côté serveur toute customisation reçue du client
  (ne jamais faire confiance aux index/ids transmis).
- **Versioning** : les saves de customisation embarquent `"version"` — prévoir
  une migration si le schéma évolue.
- **PvP** : les hitboxes (`collisionRadius/Height`) varient avec la taille ;
  garder l'équité en tête lors du tuning des bornes par race.
- **Perf** : ne pas charger tous les FBX en mémoire ; prévoir du streaming.
