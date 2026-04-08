# Codes `asset_type` (uint32) v1

Valeurs du champ **`asset_type`** dans l’index binaire `.texr` ([`texr_format_v1.md`](texr_format_v1.md)).  
**Règle** : codes **stables** ; nouveaux types = **nouveaux** identifiants sans recycler les anciens. Le client **ignore** (ou rejette selon politique) les types inconnus si `format_minor` le permet.

---

## 1. Plages réservées

| Plage | Usage |
|-------|--------|
| `0x00000000` | **Invalide / réservé** — ne pas produire |
| `0x00000001` – `0x0000FFFF` | Types **standard** v1 (ci-dessous) |
| `0x00010000` – `0x7FFFFFFF` | Réservé projet / extensions nommées |
| `0x80000000` – `0xFFFFFFFF` | **Expérimental / debug** uniquement (ne pas publier en prod sans reassignment) |

---

## 2. Types standard v1

| Valeur hex | Valeur déc | Nom | Contenu du payload (après LZ4) | Notes |
|------------|------------|-----|----------------------------------|--------|
| `0x00000001` | 1 | `TextureDds` | Fichier **DDS** brut (header + données) | Pipeline PNG→DDS (BC7 UI) |
| `0x00000002` | 2 | `TexturePng` | Fichier **PNG** brut | Option secours / outils ; décodage CPU |
| `0x00000003` | 3 | `Font` | **TTF / OTF / TTC** binaire | Pas de conversion |
| `0x00000004` | 4 | `Json` | UTF-8 JSON (ex. `theme.json`, données) | Valider UTF-8 au build |
| `0x00000005` | 5 | `Stylesheet` | UTF-8 **QSS** | |
| `0x00000006` | 6 | `Shader` | UTF-8 source shader | `.vert` / `.frag` / `.comp` |
| `0x00000007` | 7 | `Text` | UTF-8 texte brut | `.txt`, tables, etc. |
| `0x00000008` | 8 | `Audio` | Binaire audio (ex. **OGG** / **WAV** — format figé par dossier) | |
| `0x00000009` | 9 | `Binary` | Octets opaques sans interprétation builder | GLTF, SQL client si jamais embarqué, etc. |

---

## 3. Extension

- Ajouter une ligne dans ce tableau et incrémenter **`format_minor`** du `.texr` si le loader doit **accepter** de nouveaux champs ; sinon **`format_minor`** seule si sémantique rétrocompatible.  
- Documenter le **chemin logique** attendu par type (ex. shaders sous `shaders/`) dans [`texr_builder_cli_v1.md`](texr_builder_cli_v1.md) (règles par package).

---

## 4. Côté loader C++

Enum ou constantes nommées alignées sur ce tableau ; `switch` défensif sur `default` → log + erreur contrôlée.
