# Format binaire `.texr` — spécification v1

Document aligné sur les décisions produit (Endianness little-endian, magic 8 octets, LZ4, AES-256-GCM fichier entier, index trié + dichotomie, etc.).

---

## 1. Vue d’ensemble

Un **fichier publié** (CDN / disque) est une **enveloppe externe** (`OuterFile`) qui contient soit :

- le **contenu interne en clair** (mode non chiffré, ex. développement local), soit  
- **IV + ciphertext + tag** AES-GCM enveloppant le **fichier interne** (`InnerFile`).

Le **fichier interne** (`InnerFile`) est la charge utile logique : en-tête, **index** (métadonnées + pool de chemins UTF-8), **section données** (payloads compressés LZ4, alignés sur 64 octets).

```
OuterFile
├── OuterHeader (64 octets, clair)
└── Si chiffré : IV(12) || Ciphertext || Tag(16)
    où Ciphertext déchiffre vers InnerFile
    Sinon : InnerFile brut immédiatement après OuterHeader
```

---

## 2. Conventions générales

| Règle | Valeur |
|--------|--------|
| Endianness | **Little-endian** pour tous les entiers multi-octets |
| Alignement payloads | Chaque payload commence à un offset **multiple de 64** depuis le début du **InnerFile** |
| Chemins logiques | **UTF-8**, **NFC**, **minuscules** (normalisation au build) ; longueur **1..1024** octets ; **pas** de `NUL` terminal dans le fichier |
| Unicité chemins | **Interdit** d’avoir deux entrées avec le même chemin |
| Ordre des entrées | **Ordre lexicographique** des chemins (ordre des octets UTF-8 après NFC + lower) ; **même ordre** pour l’enregistrement des payloads dans la section données |
| Taille max InnerFile | **2 Gio** (2³¹ octets) ; suffisant pour `uint32` sur les tailles internes si bornées |
| Taille max asset décompressé | **1 Gio** par entrée |
| Codec compression v1 | **1 = LZ4** ; **0 = none** (réservé / exceptionnel) |

---

## 3. Enveloppe externe — `OuterHeader`

Offset | Taille | Type | Champ | Description |
|------|--------|------|--------|-------------|
| 0 | 8 | `u8[8]` | `magic` | `54 45 58 52 00 00 00 00` = `"TEXR\0\0\0\0"` |
| 8 | 4 | `u32` | `outer_version` | **1** pour cette spec |
| 12 | 4 | `u32` | `outer_flags` | bit0 : **1** = inner chiffré (GCM) ; **0** = inner en clair après ce header |
| 16 | 8 | `u64` | `inner_plaintext_len` | Taille attendue du `InnerFile` après déchiffrement (validation) ; si non chiffré = taille du inner qui suit |
| 24 | 8 | `u64` | `ciphertext_len` | Longueur du **ciphertext** en octets ; si non chiffré = **0** |
| 32 | 32 | `u8[32]` | `reserved` | Zéros |

**Taille totale `OuterHeader` : 64 octets.**

### 3.1 Suite du fichier si `outer_flags & 1` (chiffré)

| Suite | Taille | Contenu |
|--------|--------|---------|
| Après +0 | 12 | `iv` — octets aléatoires (CSPRNG), **unique par fichier** pour une clé donnée |
| Après +12 | `ciphertext_len` | Ciphertext AES-256-GCM du `InnerFile` complet |
| Fin | 16 | `tag` — tag d’authentification GCM |

**AAD** : vide (comme convenu).

### 3.2 Suite du fichier si chiffrement désactivé (bit0 = 0)

| Suite | Taille | Contenu |
|--------|--------|---------|
| Octet 64 → | `inner_plaintext_len` | `InnerFile` brut |

---

## 4. Fichier interne — `InnerFile`

### 4.1 `InnerHeader`

Taille fixe **128 octets** (alignée ; extensible via `format_minor` sans déplacer les champs v1).

Offset | Taille | Type | Champ | Description |
|------|--------|------|--------|-------------|
| 0 | 8 | `u8[8]` | `magic` | Identique à l’externe : `TEXR\0\0\0\0` |
| 8 | 4 | `u32` | `format_major` | Incrémenté pour **incompatibilité** (client refuse si `client_major < file_major`) |
| 12 | 4 | `u32` | `format_minor` | Compatible ascendante : client **accepte** si même `major` et `file_minor >=` éventuellement (politique : **accepter** minor supérieure, champs inconnus ignorés) |
| 16 | 4 | `u32` | `inner_flags` | Réservé v1 = 0 |
| 20 | 4 | `u32` | `entry_count` | Nombre d’entrées ; doit correspondre à l’index |
| 24 | 8 | `u64` | `index_offset` | Offset du début de la **section index** depuis le début du `InnerFile` |
| 32 | 8 | `u64` | `index_size` | Taille en octets de la **section index** |
| 40 | 8 | `u64` | `data_offset` | Offset du début de la **section données** (premier payload aligné 64) |
| 48 | 8 | `u64` | `data_size` | Taille totale de la section données (somme des blocs alignés) |
| 56 | 16 | `u8[16]` | `package_id` | UUID (identifiant build / package) |
| 72 | 56 | `u8[56]` | `reserved` | Zéros |

**Contraintes** :  
- `index_offset` ≥ 128 et **multiple de 64** recommandé.  
- `data_offset` **multiple de 64**.  
- `index_offset + index_size` ≤ `data_offset`.

### 4.2 Section index

La section index permet la **recherche dichotomique** : enregistrements **à taille fixe** + **pool de chaînes** contigu (chemins UTF-8 triés, sans séparateur).

#### 4.2.1 En-tête de section index

Offset (relatif au début de la section index) | Taille | Type | Champ |
|-----------------------------------------------|--------|------|--------|
| 0 | 4 | `u32` | `entry_count` | Doit égaler `InnerHeader.entry_count` |
| 4 | 4 | `u32` | `_reserved` | 0 |

Immédiatement après : **`entry_count` × `IndexRecord`** (voir ci-dessous), puis le **pool de chaînes**.

Soit `strings_base` l’offset absolu (depuis le début du `InnerFile`) du premier octet du pool de chaînes :

`strings_base = index_offset + 8 + entry_count × 40`

#### 4.2.2 `IndexRecord` (40 octets, taille fixe)

| Offset | Taille | Type | Champ | Description |
|--------|--------|------|--------|-------------|
| 0 | 4 | `u32` | `path_offset` | Offset du premier octet du chemin, **relatif au début du pool de chaînes** (voir §4.2.4) |
| 4 | 2 | `u16` | `path_len` | Longueur du chemin en octets UTF-8 (1..1024) |
| 6 | 2 | `u16` | `_reserved` | 0 |
| 8 | 4 | `u32` | `asset_type` | Enum extensible (texture DDS, PNG legacy, font, json, etc.) |
| 12 | 4 | `u32` | `compression` | 0 = none, **1 = LZ4** (v1) |
| 16 | 4 | `u32` | `compressed_size` | Taille du payload **sur disque** (octets LZ4) |
| 20 | 4 | `u32` | `uncompressed_size` | Taille après décompression LZ4 |
| 24 | 8 | `u64` | `payload_offset` | Offset du payload depuis le **début du InnerFile** ; **multiple de 64** |
| 32 | 8 | `u8[8]` | `_reserved` | Zéros |

**Recherche dichotomique** : comparer la clé requête (normalisée comme au build) avec la sous-chaîne du pool à `strings_base + path_offset` sur `path_len` octets (ordre lexicographique **memcmp** sur UTF-8, valide car chemins NFC + lower selon règles build).

#### 4.2.3 Ordre des `IndexRecord`

Les enregistrements sont **triés** par chemin (ordre octets UTF-8 du pool), identique à l’ordre des payloads dans la section données.

#### 4.2.4 Pool de chaînes

- Commence à l’octet suivant le dernier `IndexRecord`.  
- Contient les chemins **concaténés** dans l’ordre des entrées triées ; chaque entrée référence sa sous-chaîne via `path_offset` + `path_len`.  
- `path_offset` du **premier** chemin est typiquement **0** ; les offsets suivants sont cumulatifs (le builder peut aussi compacter sans trous — tant que `(path_offset, path_len)` restent cohérents).

### 4.3 Section données

- Démarre à `data_offset`.  
- Chaque payload est stocké à `payload_offset` (multiple de 64), longueur `compressed_size`.  
- Espace jusqu’au prochain multiple de 64 peut être **padding** (octets 0).  
- Ordre des payloads : **même ordre lexicographique** que les `IndexRecord`.

---

## 5. Validation côté client (résumé)

1. Lire `OuterHeader` ; vérifier `magic` et `outer_version`.  
2. Si chiffré : lire IV, ciphertext, tag ; déchiffrer vers un buffer `inner_plaintext_len` ou fichier temporaire dans `packages_cache/` (politique projet).  
3. Parser `InnerHeader` ; vérifier cohérence `entry_count`, `index_*`, `data_*`, magics.  
4. **Refus** si `format_major` > **major** supporté par le client.  
5. Charger l’index ; vérifier tri (optionnel en release) ; recherche dichotomique.  
6. Pour chaque lecture d’asset : lire `compressed_size` octets à `payload_offset`, décompresser LZ4 vers `uncompressed_size` octets ; erreur **localisée à l’asset** si échec.

---

## 6. Manifest distant (résumé schéma)

- Fichier JSON **UTF-8**.  
- Clés **triées alphabétiquement à tous les niveaux** (récursif) pour la **canonicalisation** avant signature Ed25519.  
- Signature en **Base64** dans un champ dédié (ex. `signature`) **exclu** du calcul de signature, ou selon règle explicite : signer la concaténation canonique **sans** le champ signature (à implémenter de façon unique côté builder et client).  
- Inclure **`mirrors`** : tableau d’URL de base ou de manifest de repli (signé).  
- Pour chaque artefact : versions, **`hash_plain`** (hash du `InnerFile` ou du fichier déchiffré attendu — aligné sur votre pipeline), **`hash_cipher`** (hash du fichier tel que téléchargé), tailles, chemins relatifs CDN, etc.  
- En cas d’échec de signature : **réessayer** les miroirs puis arrêt.

Détail des champs, canonicalisation et signature : **[`manifest_v1.md`](manifest_v1.md)**. Des **vecteurs de test** y seront ajoutés après la première implémentation.

---

## 7. `keys.json` (rotation Ed25519)

- Fichier séparé du manifest, **signé** par la chaîne de confiance ancrée sur la clé publique **embarquée** dans l’exécutable.  
- Permet de publier une **nouvelle clé publique** de signature pour les manifests futurs sans rebuild immédiat du client (selon règles de rotation définies dans le même document).

---

## 8. Constantes suggérées (C++ / enum)

```text
OUTER_VERSION = 1
MAGIC = "TEXR\0\0\0\0"

OUTER_FLAG_ENCRYPTED = 1

COMPRESSION_NONE = 0
COMPRESSION_LZ4    = 1

INDEX_RECORD_SIZE = 40
INNER_HEADER_SIZE = 128
OUTER_HEADER_SIZE = 64
```

---

## 9. Notes de mise en œuvre

- **Builder** : interdire symlinks ; valider UTF-8 strict ; normaliser NFC + lower ; refuser doublons ; trier ; écrire index + données alignées 64 ; produire `hash_plain` / `hash_cipher` pour le manifest.  
- **Profondeur async** : file plafonnée (ex. 1024), **1 worker**, déduplication des chargements identiques en vol.  
- **LoadPackage** : compteur de références ; **fichier déchiffré temporaire** supprimé à `UnloadPackage`.

Ce document est la **référence v1** pour l’implémentation du `texr_builder` et du loader runtime.

### Documents associés

| Document | Contenu |
|----------|---------|
| [`manifest_v1.md`](manifest_v1.md) | Manifest, canonicalisation, artefacts, miroirs |
| [`keys_v1.md`](keys_v1.md) | `keys.json`, délégations, `K_embedded` |
| [`texr_client_boot_v1.md`](texr_client_boot_v1.md) | Flux démarrage, téléchargements, limites |
| [`texr_loader_api_v1.md`](texr_loader_api_v1.md) | Brouillon d’API C++ loader |
| [`texr_builder_cli_v1.md`](texr_builder_cli_v1.md) | CLI `texr_builder`, config packages |
| [`texr_asset_types_v1.md`](texr_asset_types_v1.md) | Codes `asset_type` (uint32) |
| [`texr_ci_v1.md`](texr_ci_v1.md) | Pipelines CI/CD assets / code / release |
