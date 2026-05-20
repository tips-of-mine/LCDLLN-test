# Personnalisation de personnage — Slice 1 : fondation de données, autorité serveur

**Date** : 2026-05-20
**Statut** : design validé (en attente de relecture utilisateur avant plan d'implémentation)
**Périmètre** : slice 1 uniquement (serveur-first, compilable Linux). Slices 2 et 3 décrites pour contexte mais hors périmètre.

---

## 1. Contexte et décisions

Le prompt d'origine décrivait un système complet de customization (7 races inventées, structs C++
`lcdlln::`, `nlohmann/json`, modèle `GameObject`/ECS, assets `.fbx`, scaling d'os, morph targets,
pipeline Python, UI). La lecture du code réel montre que ce prompt a été écrit contre une version
**imaginaire** du projet. Les écarts majeurs constatés :

| Le prompt suppose | Réalité du dépôt |
|---|---|
| `lcdlln::CharacterCustomization` (nouveau) | `engine::network::CharacterCustomization` existe déjà — [src/shared/network/CharacterPayloads.h:13](../../../src/shared/network/CharacterPayloads.h) (M39.1) |
| 7 races `orkh, elfe_bois, chevalier_dragon…` | 8 races canoniques `humains, elfes, orcs, nains, morts_vivants, corrompus, divins, demons` — [game/data/races/races.json](../../../game/data/races/races.json) |
| `nlohmann/json` | Aucun ; parseur JSON écrit à la main (cf. `SlashCommandRegistry::LoadFromFile`, `engine::core::Config`) |
| `GameObject`/`Transform`/`SkeletonComponent`/`CapsuleCollider` | Architecture réseau-first ; pas d'ECS. `Skeleton`/`Bone` = [src/client/render/skinned/Skeleton.h](../../../src/client/render/skinned/Skeleton.h) (pas de `localScale`, pas de `FindBone()`/`MarkDirty()`) |
| Modèles `.fbx` au runtime | Runtime charge du `.glb` (cgltf) ; FBX = art brut dans `tools/asset_pipeline/inbox/` |
| `Logger::Info("{}")`, code sous `game/src/` | macros `LOG_INFO(Catégorie, "{}", …)` (ex. `LOG_INFO(Auth, …)`, cf. `CharacterCreateHandler`) ; [src/shared/core/Log.h](../../../src/shared/core/Log.h) ; sources sous `src/` |
| Genre `male`/`female` partout | Modèle actuellement **sans genre** (aucune notion gender côté gameplay/character) |

**Décisions prises avec l'utilisateur (brainstorming) :**

1. **Étendre M39.1** — pas de struct dupliqué, pas de taxonomie de races forkée.
2. **Conserver les 8 races existantes** — la liste du prompt est illustrative ; on mappe les features
   sur les `raceId` actuels.
3. **Spec la première tranche maintenant**, serveur-first (Linux). Le rendu client (proportions,
   morphs, attachement de mesh) est reporté à la slice 2.
4. **Approche B (révisée post-relecture code)** — poser dès maintenant les champs d'identité
   **discrets** (frame, type de corps, pilosité faciale, features raciales) en plus des 5 champs
   existants, sérialisés dans un **bloc binaire versionné** sur le wire, **stockés en JSON dans la
   colonne existante `characters.appearance_json`** (présente depuis la migration `0004`, aujourd'hui
   écrite en dur à `'{}'`). Pas de nouvelle migration. Les métriques continues sont reportées.
5. **Ajouter `bodyFrame`** (1 octet : `masculine`/`feminine`/`neutral`) — sélecteur de *frame de
   mesh*, pas une contrainte de genre narrative ; l'art modulaire est intrinsèquement frame-split.

---

## 2. Découpage global (3 sous-projets)

- **Slice 1 (cette spec)** — fondation de données + autorité serveur. Catalogue de customization
  data-driven, chargeur + validateur partagés, persistance et renvoi de la customization.
- **Slice 2 (plus tard, quasi 100 % client)** — UI de création peuplée depuis le catalogue, preview
  live, attachement modulaire de mesh `.glb` aux sockets du squelette, morph targets, scaling d'os
  pour les proportions, redimensionnement de la capsule.
- **Slice 3 (plus tard)** — adaptation de l'équipement (sockets d'attache, sets d'armure,
  auto-scaling au corps).

Le pipeline d'assets FBX→`.glb` est une piste outillage séparée qui alimente les slices 2/3.

**Pourquoi cet ordre :** la slice 1 absorbe **tout** ce qui casse le wire et la DB (un seul
redéploiement serveur + une seule migration). La slice 2 reste alors aussi proche que possible du
« client uniquement » — l'ajout ultérieur des métriques continues ne coûtera qu'un bump de version
de wire (validation serveur), **pas** de nouvelle migration DB grâce au stockage blob versionné.

---

## 3. Objectifs / non-objectifs de la slice 1

**Objectifs**
- Rendre le **serveur autoritaire** sur la customization : toute customization reçue est validée
  contre un catalogue data-driven par race avant persistance (règle sécurité : valider côté serveur).
- **Persister et renvoyer** la customization. Gap actuel : la colonne `characters.appearance_json`
  existe (`TEXT NULL`, migration `0004`) mais `CharacterCreateHandler` l'écrit en dur à `'{}'`
  ([CharacterCreateHandler.cpp:265](../../../src/masterd/handlers/character/CharacterCreateHandler.cpp))
  sans lire `parsed->customization`, et `CharacterListEntry` ne la renvoie pas.
- Étendre proprement `engine::network::CharacterCustomization` avec les champs d'identité discrets.
- Fournir un **chargeur + validateur partagé** (`src/shared/`) compilable Linux et réutilisable tel
  quel par le client en slice 2.

**Non-objectifs (reportés)**
- Métriques continues : `heightScale`, proportions (jambes/épaules/torse), `bodyMass`.
- Morph targets / blend shapes.
- Attachement et rendu de mesh, scaling d'os, redimensionnement de capsule.
- Pipeline d'assets (`process_character_assets.py`, `validate_fbx.py`, FBX→glb).
- Équipement (sockets, armor sets).
- Toute introduction de `nlohmann/json`, de struct `lcdlln::`, ou de la roster de races alternative.

---

## 4. Modèle de données — catalogue de customization

Nouveaux fichiers, un par race : `game/data/races/customization/<raceId>.json` (8 fichiers, ids
identiques à `races.json`). Les **palettes de couleurs restent dans `races.json`** (source unique) ;
le catalogue ne décrit que les modules d'apparence et les features raciales.

Exemple — `game/data/races/customization/humains.json` :

```json
{
  "version": "1.0.0",
  "raceId": "humains",
  "frames": ["masculine", "feminine"],
  "modules": {
    "masculine": {
      "bodyTypes":  ["base", "muscular", "lean"],
      "faces":      ["face_01", "face_02", "face_03", "face_04", "face_05"],
      "hair":       ["short_01", "short_02", "medium_01", "long_01", "bald"],
      "facialHair": ["none", "beard_full", "goatee", "mustache"]
    },
    "feminine": {
      "bodyTypes":  ["base", "athletic", "curvy"],
      "faces":      ["face_01", "face_02", "face_03", "face_04", "face_05"],
      "hair":       ["short_01", "medium_01", "long_01", "long_02", "braided_01", "ponytail_01"],
      "facialHair": ["none"]
    }
  },
  "racialFeatures": {}
}
```

Exemple avec features raciales — `demons.json` (les « cornes/queues » du prompt mappées sur la race
existante `demons`) :

```json
{
  "version": "1.0.0",
  "raceId": "demons",
  "frames": ["masculine", "feminine"],
  "modules": { "masculine": { "...": "..." }, "feminine": { "...": "..." } },
  "racialFeatures": {
    "horns": ["none", "curved_01", "straight_01", "ram_style"],
    "tails": ["none", "spaded_long", "thin_long"]
  }
}
```

Notes :
- `racialFeatures` est un **dictionnaire générique** `featureKey → [ids]`. Le moteur ne code en dur
  aucun nom de feature : `orcs` aura `tusks`, `elfes` des `ears`, etc. — purement data.
- Le schéma est dimensionné pour **accueillir plus tard** des plages de proportions/taille (champ
  optionnel non lu en slice 1) afin de ne pas réécrire le fichier en slice 2.
- Chargement : suivre le **pattern JSON manuel existant** (`SlashCommandRegistry::LoadFromFile`),
  pas de nouvelle dépendance.

---

## 5. Modèle de données — `CharacterCustomization` étendue (wire)

On **étend la struct existante** `engine::network::CharacterCustomization` (pas de nouvelle struct
concurrente). Champs ajoutés :

```cpp
struct CharacterCustomization
{
    uint8_t faceType     = 0; // existant
    uint8_t hairStyle    = 0; // existant
    uint8_t skinColorIdx = 0; // existant
    uint8_t hairColorIdx = 0; // existant
    uint8_t eyeColorIdx  = 0; // existant

    // --- Slice 1 (Approche B) ---
    uint8_t bodyFrame    = 0; // index dans catalog.frames (0 = 1er frame)
    uint8_t bodyType     = 0; // index dans modules[frame].bodyTypes
    uint8_t facialHair   = 0; // index dans modules[frame].facialHair

    // featureKey -> index ; bornes validées contre catalog.racialFeatures
    std::vector<std::pair<std::string, uint8_t>> racialFeatures;
};
```

Les **métriques continues ne sont PAS ajoutées** en slice 1.

---

## 6. Format wire (versionné, tolérant)

Le payload create porte déjà la customization en fin de trame ; on remplace les 5 octets bruts par un
**bloc customization versionné et préfixé en longueur**, réutilisé dans la réponse character-list.

Proposition de layout du bloc (offsets exacts à figer dans le plan) :

```
uint8   blockVersion         // = 1 pour la slice 1
uint8   faceType
uint8   hairStyle
uint8   skinColorIdx
uint8   hairColorIdx
uint8   eyeColorIdx
uint8   bodyFrame
uint8   bodyType
uint8   facialHair
uint8   featureCount
featureCount × {
    uint8  keyLen ; keyLen bytes (featureKey UTF-8)
    uint8  index
}
```

- **Parsing tolérant** : un lecteur d'une version ≥ celle reçue lit les champs connus ; un
  `blockVersion` inconnu (plus récent) → rejet propre. L'ajout futur de champs (métriques continues
  en slice 2) = `blockVersion = 2`, **sans migration DB**.
- Fonctions à mettre à jour : `BuildCharacterCreateRequestPayload` / `ParseCharacterCreateRequestPayload`
  et `Build/Parse CharacterListResponse*` ([src/shared/network/CharacterPayloads.{h,cpp}](../../../src/shared/network/CharacterPayloads.h)).
- Le parseur de liste actuel est **strict** (cf. commentaires Phase 3.6/3.8 dans le fichier) →
  client et serveur en lock-step.

---

## 7. Persistance

- **Pas de nouvelle migration.** Réutilisation de la colonne existante `characters.appearance_json`
  (`TEXT NULL`, ajoutée par `0004_auth_mvp_m33_1.sql`, commentaire « JSON blob for appearance
  customisation »). Elle est aujourd'hui écrite en dur à `'{}'` et jamais relue.
- **Format stocké : JSON** sérialisé depuis la `CharacterCustomization` (mêmes champs qu'au §5). Le
  JSON est naturellement forward-compatible — la slice 2 ajoutera des clés (métriques continues)
  **sans aucune migration**. Le wire reste le bloc binaire versionné du §6 ; wire et stockage sont
  deux représentations distinctes du même modèle.
- **Écriture** : à la création, remplacer le `'{}'` codé en dur par le JSON validé (échappé via
  `mysql_real_escape_string`).
- **Lecture** : `CharacterListHandler` ajoute `c.appearance_json` à son SELECT (colonnes actuelles
  jusqu'à `race_str`/`class_str`, [CharacterListHandler.cpp:71](../../../src/masterd/handlers/character/CharacterListHandler.cpp))
  et parse le JSON dans le nouveau champ `CharacterListEntry::customization` (ajouté + appended au
  wire de liste).
- **Lignes pré-existantes** : `appearance_json = '{}'` ou `NULL` → apparence par défaut au parsing.
- Côté shard : `PersistedCharacterState` ([src/shardd/gameplay/character/CharacterPersistence.h](../../../src/shardd/gameplay/character/CharacterPersistence.h))
  peut lire `appearance_json` si besoin à l'enter-world (store-only en slice 1 ; aucun rendu encore).

---

## 8. Bibliothèque partagée (chargeur + validateur)

Emplacement : `src/shared/Character/` — dossier **PascalCase** (règle projet prioritaire, confirmée
par l'utilisateur ; cf. §14.2), même si les voisins `shared/network`/`shared/core` sont en minuscules.
Includes : `#include "src/shared/Character/CustomizationCatalog.h"`.

- `CustomizationCatalog.{h,cpp}` — charge les 8 fichiers `game/data/races/customization/*.json`
  (pattern JSON manuel) ; expose un accès par `raceId` aux frames/modules/featureKeys, et lit les
  tailles de palettes de couleurs depuis `races.json`.
- `CustomizationValidator.{h,cpp}` — valide une `engine::network::CharacterCustomization` contre le
  catalogue. Signature proposée :

```cpp
struct ValidationResult { bool ok; std::vector<std::string> errors; };
ValidationResult ValidateCustomization(const CustomizationCatalog& catalog,
                                       std::string_view raceId,
                                       const engine::network::CharacterCustomization& c);
```

Placé dans `shared/` → **compile sur Linux** (master + shard) et **réutilisé tel quel par le client**
en slice 2 (pré-validation UX = mêmes règles que l'autorité serveur).

---

## 9. Règles de validation

Rejet (avec message d'erreur spécifique) si l'un des points échoue :
- `raceId` inconnu (pas de catalogue chargé pour cette race).
- `bodyFrame` ≥ `catalog.frames.size()`.
- `bodyType` ≥ `modules[frame].bodyTypes.size()`.
- `faceType` ≥ `faces.size()` ; `hairStyle` ≥ `hair.size()` ; `facialHair` ≥ `facialHair.size()`.
- `skinColorIdx` ≥ `races.json.defaultSkinColors.size()` (idem hair/eye colors).
- pour chaque `racialFeatures[k]` : `k` absent de `catalog.racialFeatures`, ou `index ≥` taille de la
  liste correspondante.
- `blockVersion` > version supportée.

---

## 10. Points d'intégration

- `CharacterCreateHandler` ([CharacterCreateHandler.cpp](../../../src/masterd/handlers/character/CharacterCreateHandler.cpp))
  : parse la customization du wire, appelle `ValidateCustomization` **avant** l'INSERT ; si invalide
  → réponse d'erreur (`BAD_REQUEST`) sans insertion ; si valide → sérialise en JSON et l'écrit dans
  `appearance_json` (à la place du `'{}'`).
- `CharacterListHandler` ([CharacterListHandler.cpp](../../../src/masterd/handlers/character/CharacterListHandler.cpp))
  : ajoute `appearance_json` au SELECT et renvoie la customization dans chaque `CharacterListEntry`.
- DB : accès MySQL direct via les helpers `engine::server::db` (`DbQuery`/`DbExecute`) sur le
  `ConnectionPool` du master — pas d'ORM. Échappement systématique via `mysql_real_escape_string`.
- Catalogue chargé une fois au démarrage du master (et du shard si besoin), à côté du chargement
  actuel de `races.json`/`classes.json`.

---

## 11. Tests

- Tests unitaires du validateur : indices valides/invalides, race inconnue, couleur hors palette,
  featureKey inconnue, index de feature hors borne, `blockVersion` futur.
- Test du chargeur : les 8 fichiers de catalogue se chargent, ids cohérents avec `races.json`,
  aucune race sans catalogue.
- Round-trip de sérialisation : `Build*` → `Parse*` du bloc customization (toutes valeurs préservées,
  features comprises).
- **Limite CI connue** : `build-linux.yml` est compile-only (pas de `ctest` au runtime). Les tests
  doivent au minimum **compiler** sous GCC ; l'exécution se fait via CI Windows / local. À garder en
  tête (angle mort tests runtime Linux).

---

## 12. Impact déploiement

> **Déploiement** : ⚠️ **REDÉPLOIEMENT SERVEUR REQUIS (master ; shard si lecture)**.
> Slice 1 est **wire-breaking** (bloc customization versionné dans les payloads create + list) et
> change la logique des handlers → redéploiement du binaire serveur obligatoire. **Pas de migration
> DB** (réutilisation de `appearance_json`). Client neuf et serveur neuf en **lock-step** : le parser
> de liste est strict, un mismatch de version échouera au parsing.

---

## 13. Conventions

- Fichiers/classes C++ nouveaux en **PascalCase** (cohérent avec le dépôt : `CharacterPayloads.h`,
  `SlashCommandRegistry.h`, `Skeleton.h`).
- Fichiers de données en minuscule/snake (cohérent : `races.json`, `slash_commands.json`).
- Commentaires en **français** (convention dépôt).
- CMake : ajouter les nouveaux `.cpp` aux **listes explicites** (pas de glob) pour les deux presets
  (`vs2022-x64*` et `linux-x64*`).
- Logs via les macros `LOG_INFO/LOG_WARN/LOG_ERROR(Catégorie, "fmt", args…)` (cf. usage dans
  `CharacterCreateHandler`), pas d'appel `Log::Write` direct.

---

## 14. Risques / points à trancher au plan

1. **(Résolu)** Couche d'accès DB : le **master** écrit et lit `characters.appearance_json` via les
   helpers `engine::server::db` (raw SQL, `ConnectionPool`). Aucune migration. Le shard ne fait que
   lire si nécessaire.
2. **(Résolu)** Casing du dossier : **`src/shared/Character/` en PascalCase** (règle projet
   prioritaire, confirmée par l'utilisateur), malgré les voisins `network`/`core`/`db`/`math` en
   minuscules.
3. **Encodage wire des `racialFeatures`** : layout TLV du §6 retenu ; offsets exacts figés au plan.
4. **Besoin shard à l'enter-world** : store-only en slice 1 ; lecture/rendu reportés en slice 2.
5. **Palettes couleurs** : le validateur lit les tailles depuis `races.json` ; confirmer que le master
   charge déjà `races.json` au moment de la validation (sinon le catalogue le charge lui-même).
