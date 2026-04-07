# Client : démarrage, mise à jour et chargement des `.texr` v1

Description du **flux cible** au lancement du jeu (MMORPG, pas de mode hors-ligne) : récupération du **manifest** et des **`keys.json`**, téléchargement des packages, vérifications d’intégrité, déchiffrement, ouverture des packs, en cohérence avec les specs binaires et JSON.

Références : [`texr_format_v1.md`](texr_format_v1.md), [`manifest_v1.md`](manifest_v1.md), [`keys_v1.md`](keys_v1.md).

---

## 1. Objectifs

| Objectif | Comportement |
|----------|----------------|
| Sécurité | TLS strict ; **signature** manifest (clé issue de `keys.json`) ; **signature** `keys.json` (racine + délégations) |
| Intégrité | `hash_cipher` puis `hash_plain` par artefact |
| UX | Écran dédié + **barre de progression** jusqu’à succès des mises à jour nécessaires |
| Perf réseau | Jusqu’à **3** téléchargements **parallèles** ; retry manifest **5** tentatives backoff exponentiel ; timeout manifest **10 s** ; timeout par gros fichier **10 min** |
| Ressources | **Max 32** packages ouverts ; file async **1024** requêtes ; **1** worker IO |

---

## 2. Configuration locale (hors manifest)

Paramètres typiques (fichier config ou constantes build) :

| Paramètre | Rôle |
|-----------|------|
| `manifest_url` | URL HTTPS du manifest principal |
| `keys_url` | URL HTTPS du `keys.json` (peut être sur le même hôte) |
| `cdn_base` | Préfixe HTTPS pour concaténer avec `artifacts[*].relative_path` |
| `packages_cache_dir` | Répertoire des `.texr` téléchargés + **fichiers déchiffrés temporaires** (ex. `packages_cache/`) |
| `local_manifest_path` | Copie du dernier manifest **validé** (pour comparaison de versions) |

Les **miroirs** du manifest signé **remplacent** ou **complètent** les tentatives sur `manifest_url` selon la politique implémentée (voir §4).

---

## 3. Ordre global au démarrage

```
1. UI « Vérification des contenus… » (non bloquante pour le thread réseau si async)
2. Télécharger keys.json (avec retries / timeout analogues au manifest)
3. Valider keys.json (signature racine K_embedded + chaîne delegations)
4. Télécharger manifest.json (miroirs si échec)
5. Valider signature manifest avec Known[signing_key_id]
6. Comparer versions / hash avec local_manifest : décider la liste des artefacts à mettre à jour
7. Pour chaque artefact obsolète : télécharger vers fichier .tmp dans packages_cache_dir (même volume que la cible)
8. Vérifier hash_cipher ; rename atomique vers le nom final
9. (Optionnel immédiat ou paresseux) Déchiffrer OuterFile → InnerFile temp ; vérifier hash_plain
10. Écrire / mettre à jour local_manifest
11. Initialiser le sous-système de packages : LoadPackage dans l’ordre métier (core.ui, fonts, loc, …)
12. Lancer la boucle de jeu / écrans avec chargements async des assets
```

**Échec réseau ou signature** après épuisement des reprises : **message d’erreur** et **sortie** (pas de session MMORPG sans contenu validé), conformément aux règles projet.

---

## 4. Récupération `keys.json` et manifest

### 4.1 `keys_url`

- **GET** HTTPS ; timeouts / retries alignés sur le manifest (à défaut : mêmes valeurs).  
- En cas d’échec : même stratégie que le manifest — **miroirs** possibles via **config** locale (liste d’URL) si vous ne souhaitez pas dupliquer `mirrors` dans un autre fichier signé.  
- Validation stricte selon [`keys_v1.md`](keys_v1.md).

### 4.2 Manifest

- **GET** `manifest_url` ; en cas d’échec de **réseau** ou de **signature**, tenter chaque URL dans `mirrors` **dans l’ordre** du tableau.  
- Signature : voir [`manifest_v1.md`](manifest_v1.md).

---

## 5. Téléchargement des artefacts

| Règle | Détail |
|--------|--------|
| Nom temporaire | Ex. `nom.texr.part` ou `nom.texr.tmp` dans **le même répertoire** que la cible |
| Atomique | Après `hash_cipher` OK : `rename` / `MoveFileEx` vers le nom final |
| Parallélisme | **Jusqu’à 3** fichiers en parallèle ; les autres en attente en file |
| Reprise | v1 : **pas** de HTTP Range ; en cas d’interruption → **retélécharger entièrement** |
| Timeout | **10 min** par gros fichier (configurable) |

---

## 6. Vérifications post-téléchargement

1. **`hash_cipher`** : SHA-256 du fichier octets = valeur du manifest.  
2. **Déchiffrement** (si `outer_flags` chiffré) : produire `InnerFile` en RAM ou fichier temp sous `packages_cache_dir`.  
3. **`hash_plain`** : SHA-256 du `InnerFile` = valeur du manifest.  
4. **Suppression / cycle de vie** du fichier déchiffré : voir [`texr_format_v1.md`](texr_format_v1.md) (suppression à `UnloadPackage`).

---

## 7. Chargement runtime (`LoadPackage` et suite)

| Règle | Détail |
|--------|--------|
| Limite | **32** packages ouverts simultanément |
| Double ouverture | **Compteur de références** sur le même chemin |
| Lecture | **Fichier + blocs** (pas de mmap imposé v1) |
| Index | Recherche **dichotomique** sur enregistrements fixes + pool de chaînes |
| Erreur lecture / LZ4 | **Par asset** ; le reste du package reste utilisable |
| Async | **1 worker** ; file max **1024** ; **déduplication** des requêtes identiques en vol |

**Ordre métier recommandé** (indicatif) : `core.ui` → `core.fonts` → `localization` → `shaders` → `game.data` → `audio` → `race.*` → `world.zone_*` selon besoin.

---

## 8. Hot reload (développement)

- Flag **dev** : liste blanche par défaut des chemins disque override (ex. préfixe `game/data/ui/`) ; **hors** whitelist, le **`.texr` prime** (spec UI).  
- Ne s’applique **pas** aux builds release.

---

## 9. Journalisation

Niveaux **quiet / normal / verbose** : erreurs et warnings toujours visibles en normal ; détails réseau, chemins, durées en verbose (attention aux **PII** dans les URL si logs collectés).

---

## 10. Pistes CI/CD (rappel)

- Pipeline **assets** : sources sous `game/data/` → `texr_builder` → artefacts `.texr` + `manifest.json` + `keys.json` (selon process) → publication.  
- Pipeline **code** : `lcdlln.exe` avec `K_embedded`.  
- Les **`.texr`** ne sont **pas** versionnés dans Git.

---

Voir aussi le brouillon d’**API C++** du loader : [`texr_loader_api_v1.md`](texr_loader_api_v1.md).
