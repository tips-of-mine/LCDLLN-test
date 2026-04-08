# API loader runtime `.texr` v1 (C++ — brouillon)

Objectif : contrat **minimal** entre le moteur et le sous-système de packages, aligné sur [`texr_format_v1.md`](texr_format_v1.md), [`texr_asset_types_v1.md`](texr_asset_types_v1.md) et [`texr_client_boot_v1.md`](texr_client_boot_v1.md). Les noms sont indicatifs ; à adapter au style du projet.

---

## 1. Types notionnels

```cpp
using PackageHandle = uint32_t;  // 0 = invalide
using RequestId       = uint64_t;

enum class AssetLoadStatus : uint8_t {
  Pending,
  Ready,
  Failed,
};

enum class LogLevel : uint8_t { Quiet, Normal, Verbose };
```

---

## 2. Cycle de vie des packages

| Fonction | Comportement |
|----------|----------------|
| `PackageHandle LoadPackage(std::filesystem::path const& outer_texr_path)` | Ouvre l’**OuterFile** ; si chiffré, déchiffre vers cache (voir spec) ; parse **InnerFile** ; construit l’index en mémoire (dichotomie). **Compteur de références** si déjà ouvert. **Max 32** handles ouverts. |
| `void AddPackageRef(PackageHandle h)` | Incrémente les références. |
| `void ReleasePackage(PackageHandle h)` | Décrémente ; à **0** : fermeture IO, libération index, **suppression du fichier déchiffré temporaire** si applicable. |
| `bool IsPackageReady(PackageHandle h)` | Inner valide et index chargé. |

Erreurs **format major** incompatible : échec immédiat de `LoadPackage`, pas de handle valide.

---

## 3. Résolution synchrone (debug / bootstrapping)

```cpp
// Chemin logique : UTF-8 NFC + minuscules, comme au build.
std::span<std::byte const> GetAssetBytes(
    PackageHandle pkg,
    std::string_view logical_path,
    uint32_t& out_type,
    std::error_code& ec);
```

- Recherche **dichotomique** ; lecture du payload à `payload_offset` ; **LZ4** → buffer **owned** par le package ou pool temporaire (implémentation).  
- Échec **LZ4** ou IO : `ec` positionné, span vide ; **pas** d’exception obligatoire (style moteur à trancher).

---

## 4. Chargement asynchrone (jeu)

```cpp
using OnAssetReady = std::function<void(
    RequestId id,
    AssetLoadStatus st,
    std::span<std::byte const> bytes,
    uint32_t asset_type,
    std::error_code ec)>;

RequestId RequestAssetAsync(
    PackageHandle pkg,
    std::string_view logical_path,
    int priority,   // plus grand = plus urgent
    OnAssetReady cb);
```

| Règle | Détail |
|--------|--------|
| Worker | **1** thread dédié IO + décompression |
| File d’attente | **Plafond 1024** requêtes ; refus ou blocage documenté |
| Déduplication | Même `(pkg, logical_path)` en vol : **une** charge, callbacks **multiples** notifiés |
| Ordre | Respect approximatif des **priorités** (file par priorités ou tri partiel) |

Annulation (option v1) : `void CancelRequest(RequestId id)` si non encore commencé.

---

## 5. Multi-packages

Le moteur peut tenir une **couche** au-dessus :

```cpp
// Cherche dans une liste ordonnée de handles (race.* après core.ui, etc.).
RequestId RequestAssetFromAnyPackageAsync(
    std::span<PackageHandle const> search_order,
    std::string_view logical_path,
    int priority,
    OnAssetReady cb);
```

Politique **core 100 % / race** : `search_order` ne mélange pas les préfixes interdits ; chemins race uniquement dans les packs `race.*`.

---

## 6. Hot reload (dev)

```cpp
void SetDevHotReload(bool on, std::span<std::string_view const> path_prefix_whitelist);
```

Si `on` et `logical_path` commence par un préfixe whitelisté : tenter **fichier disque** sous `game/data/` avant le `.texr` ; sinon **`.texr` seul** (spec UI).

---

## 7. Journalisation

```cpp
void SetAssetLogLevel(LogLevel level);
```

**Verbose** : temps de décompression, tailles, ids de requête ; **pas** de secrets.

---

## 8. Thread-safety

À préciser par implémentation ; cibles usuelles :

- `LoadPackage` / `ReleasePackage` depuis le **thread principal** uniquement, ou protégés par mutex.  
- `RequestAssetAsync` : thread-safe depuis n’importe quel thread ; callbacks **sur thread jeu** ou **depuis worker** selon option `RunCallbacksOnGameThread(bool)` (recommandé : repost sur thread principal).

---

## 9. Dépendances suggérées

- **LZ4** (décompression).  
- **AES-GCM** (déchiffrement enveloppe, si non fait en couche au-dessus).  
- **SHA-256** (optionnel côté loader si double vérification locale).

---

Ce document est un **brouillon d’API** ; figer les signatures exactes lors de l’intégration dans le moteur.
