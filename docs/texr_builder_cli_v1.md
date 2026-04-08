# Outil `texr_builder` — interface CLI v1

Spécification de la ligne de commande et du comportement du **builder** produisant les `.texr`, en cohérence avec [`texr_format_v1.md`](texr_format_v1.md), [`texr_asset_types_v1.md`](texr_asset_types_v1.md) et le découpage packages du projet.

---

## 1. Rôle

- Ingérer des fichiers sous **`game/data/`** (ou sous-arbre explicitement passé).  
- Appliquer les règles **par package** : filtrage, conversion **PNG→DDS** si applicable, **LZ4** par entrée, construction **index trié** + **blob aligné 64**, écriture **InnerFile** puis **OuterFile** (chiffrement optionnel / profil release).  
- Émettre les métadonnées nécessaires au **manifest** (`hash_plain`, `hash_cipher`, tailles) — souvent via **JSON** ou **stdout** consommé par la CI.

---

## 2. Exécution générale

```text
texr_builder [options globales] <commande> [arguments de commande]
```

### 2.1 Options globales (recommandées)

| Option | Description |
|--------|-------------|
| `--root <path>` | Racine des sources ; défaut : `game/data` (relatif au cwd ou absolu). |
| `--config <file>` | Fichier **JSON** ou **YAML** décrivant les **packages** (voir §4). |
| `--out <dir>` | Répertoire de sortie des `.texr` et des sidecars (hashes, listing). |
| `--log-level quiet|normal|verbose` | Niveau de log. |
| `--dry-run` | Scan + validations + rapport, **sans** écrire de `.texr`. |
| `--encrypt` | Après assemblage Inner : produire **OuterFile** chiffré (AES-256-GCM) ; clé via `--key` ou variable d’environnement documentée. |
| `--no-encrypt` | Forcer **OuterFile** non chiffré (dev local). |

---

## 3. Commandes

### 3.1 `build` (par défaut si omis)

Construit **tous** les packages définis dans `--config`, ou un sous-ensemble :

```text
texr_builder [--root <path>] [--config <path>] [--out <dir>] build [--only <id>[,<id>...]]
```

| Option | Description |
|--------|-------------|
| `--only` | N’exécuter que les packages dont l’**id** est listé (ex. `core.ui`, `world.zone_0`). |

**Sorties typiques** (par package) :

- `<out>/<nom_fichier>.texr` — nom défini dans la config (ex. `core.ui.texr`).  
- `<out>/<nom_fichier>.texr.meta.json` — `hash_plain`, `hash_cipher`, `cipher_size`, `entry_count`, `package_id` (UUID généré), `format_major` / `format_minor`, durée de build (optionnel).

### 3.2 `inspect` (debug)

Affiche le **header** outer + inner et un **résumé** de l’index (sans dump complet des payloads) :

```text
texr_builder inspect <fichier.texr> [--verbose]
```

### 3.3 `validate` (CI)

Vérifie qu’un `.texr` existant respecte la spec (magic, tailles, alignements, LZ4 décompressible, chemins uniques, ordre trié) :

```text
texr_builder validate <fichier.texr>
```
Code retour **0** = OK, **≠0** = erreur + message.

---

## 4. Fichier de configuration des packages

Un seul fichier listant les **packages** à produire. Structure **JSON** indicative :

```json
{
  "packages": [
    {
      "id": "core.ui",
      "output": "core.ui.texr",
      "includes": [
        { "path": "ui/", "asset_type": "Stylesheet", "glob": "**/*.qss" },
        { "path": "ui/", "asset_type": "Json", "glob": "**/theme.json" },
        { "path": "ui/", "asset_type": "TextureDds", "glob": "**/*.png", "convert": "png_to_dds_bc7_srgb" },
        { "path": "icons/", "asset_type": "TextureDds", "glob": "**/*.png", "convert": "png_to_dds_bc7_srgb" },
        { "path": "placeholders/", "asset_type": "TextureDds", "glob": "**/*.png", "convert": "png_to_dds_bc7_srgb" }
      ],
      "excludes_globs": ["**/*.prompt.txt", "**/README.md"]
    },
    {
      "id": "race.dynamic",
      "output_pattern": "race.{race_id}.ui.texr",
      "scan": { "path": "ui/races/", "each_subdirectory_is": "race_id" },
      "includes": [
        { "glob": "**/*.qss", "asset_type": "Stylesheet" },
        { "glob": "**/theme.json", "asset_type": "Json" },
        { "glob": "**/*.png", "asset_type": "TextureDds", "convert": "png_to_dds_bc7_srgb" }
      ],
      "excludes_globs": ["**/*.prompt.txt", "**/README.md"]
    }
  ],
  "png_to_dds": {
    "tool": "texconv",
    "profile_bc7_srgb": ["arguments documentés une fois texconv fixé"]
  }
}
```

**Remarques** :

- **`race.dynamic`** : une entrée par sous-dossier direct de `ui/races/<race_id>/` ; le **chemin logique** dans le `.texr` est relatif à `game/data/` (ex. `ui/races/humains/style.qss`).  
- Les packages **`fonts`**, **`localization`**, **`audio`**, **`shaders`**, **`game.data`**, **`world.zone_N`** suivent le même schéma avec `includes` / `glob` / `asset_type` appropriés (voir conventions projet).  
- **`persistence/`** : **exclu** du build client (serveur uniquement), sauf décision contraire explicite.

---

## 5. Règles de chemins logiques

| Règle | Détail |
|--------|--------|
| Préfixe | Tout chemin d’asset commence comme chemin **relatif** à `--root` (ex. `ui/login/background`). |
| Normalisation | **NFC** + **minuscules** ; séparateur **/** dans l’index. |
| Longueur | **1..1024** octets UTF-8. |
| Doublons | **Erreur** si deux fichiers sources mappent au même chemin logique. |
| Symlinks | **Interdits** — erreur si rencontrés. |

---

## 6. Pipeline par fichier source

1. **Glob** / scan selon règles du package.  
2. Exclusions (`excludes_globs`).  
3. Validation **UTF-8** pour texte ; binaire lu tel quel.  
4. Conversion **PNG→DDS** si `convert` défini : appel **texconv** (ou outil équivalent) vers buffer / fichier temp ; type **`TextureDds`**.  
5. **Compression LZ4** (mode **rapide** v1) → tailles `compressed` / `uncompressed`.  
6. Agrégation : tri **lexicographique** des chemins ; écriture **payloads** alignés **64** ; écriture **IndexRecord** + pool de chaînes ([`texr_format_v1.md`](texr_format_v1.md)).  
7. Écriture **InnerFile** ; calcul **`hash_plain`** (SHA-256).  
8. Si `--encrypt` : chiffrement **fichier entier**, IV aléatoire 12 octets.  
9. **`hash_cipher`** = SHA-256 du fichier **publié** (octets exacts écrits : `OuterHeader` + suite) ; **`hash_plain`** = SHA-256 du **`InnerFile`** seul ([`manifest_v1.md`](manifest_v1.md)). En mode non chiffré, le fichier publié est quand même un **OuterFile** avec `outer_flags` = 0 et `inner_plaintext_len` = taille du inner.

---

## 7. Sorties CI / manifest

Le builder ou un script wrapper agrège les `.meta.json` pour produire les entrées **`artifacts`** du manifest (`version`, `relative_path`, hashes, `cipher_size`).

---

## 8. Codes de retour

| Code | Signification |
|------|----------------|
| 0 | Succès |
| 1 | Erreur d’entrée (config, chemins) |
| 2 | Erreur de contenu (UTF-8, doublon, symlink) |
| 3 | Erreur outil externe (texconv, etc.) |
| 4 | Erreur d’écriture / disque |

---

## 9. Dépendances externes

- **LZ4** (compression).  
- **texconv** (DirectXTex) ou équivalent pour **PNG→DDS** sous Windows.  
- Bibliothèque **crypto** pour SHA-256 et AES-GCM (alignée sur le client).

---

## 10. Documents associés

| Document | Contenu |
|----------|---------|
| [`texr_format_v1.md`](texr_format_v1.md) | Layout binaire |
| [`texr_asset_types_v1.md`](texr_asset_types_v1.md) | Valeurs `asset_type` |
| [`manifest_v1.md`](manifest_v1.md) | Champs manifest |
| [`texr_ci_v1.md`](texr_ci_v1.md) | Intégration CI/CD |
| [`examples/texr_packages.full.example.json`](examples/texr_packages.full.example.json) | Config packages complète (exemple) |
