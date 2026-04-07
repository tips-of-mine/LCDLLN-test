# CI/CD — pipelines assets, code et release v1

Logique d’intégration continue alignée sur les specs [`.texr`](texr_format_v1.md), [manifest](manifest_v1.md), [keys](keys_v1.md) et le [builder](texr_builder_cli_v1.md).  
**Aucun outil n’est imposé** : les étapes s’adaptent à GitHub Actions, GitLab CI, Azure DevOps, Jenkins, etc.

---

## 1. Principes

| Principe | Détail |
|----------|--------|
| **Sources dans Git** | Uniquement les assets sous `game/data/` (et la config builder) ; **jamais** les `.texr` générés. |
| **Même pipeline** | Les packages **livrés avec l’install** et les **mises à jour** partielles sont produits par le **même** job (ou workflow) builder ; seule la **promotion** (tag, branche, environnement) change. |
| **Artefacts immuables** | Un `.texr` publié est identifié par **hash** ; une nouvelle version = **nouveau** fichier (nouveau chemin ou nouveau nom de version dans `relative_path`). |
| **Secrets** | Clé **AES** (chiffrement packages), clé **privée Ed25519** (signature manifest / délégations `keys.json`), jamais dans le dépôt. |
| **Reproductibilité** | Builder en **ordre trié** ; même commit sources → mêmes hashes **`hash_plain`** (à défaut de timestamps volatils dans les assets). |

---

## 2. Vue d’ensemble des trois pipelines

```
┌─────────────────┐     ┌─────────────────┐     ┌──────────────────────┐
│  A — Assets     │     │  B — Code       │     │  C — Release         │
│  sources → .texr│     │  moteur → exe   │     │  exe + .texr + inst. │
│  + manifest     │     │                 │     │                      │
└────────┬────────┘     └────────┬────────┘     └──────────┬───────────┘
         │                         │                          │
         ▼                         ▼                          ▼
   Stockage artefacts         Artefact exe              Package joueur /
   (S3, blob, etc.)           (lcdlln.exe)              installeur / zip
```

Les pipelines **A** et **B** peuvent être **parallèles**. Le pipeline **C** dépend des **artefacts réussis** de **A** et **B** (ou des derniers promus).

---

## 3. Pipeline A — Assets

### 3.1 Déclenchement

- Sur **push** / **merge** touchant `game/data/**` ou la **config** `texr_builder` (chemins à paramétrer).  
- Option : **planifié** (nightly) pour détecter régressions outils (texconv, LZ4).

### 3.2 Environnement d’exécution

- **OS** : **Windows** en v1 (texconv / build DDS).  
- **Outils** : `texr_builder`, **texconv** (DirectXTex), **LZ4**, crypto (SHA-256, AES-GCM), runtime pour **signer** Ed25519 (CLI ou script).

### 3.3 Étapes logiques

1. **Checkout** du dépôt (ou sous-module contenant `game/data/`).  
2. **Restauration** des dépendances du builder (si projet séparé).  
3. **`texr_builder build`** avec `--config` pointant vers le fichier packages (ex. [`examples/texr_packages.full.example.json`](examples/texr_packages.full.example.json)).  
4. **Profil release** : `--encrypt` + injection de la clé AES depuis **secret CI** (`TEXR_AES_KEY` ou fichier éphémère).  
5. **Agrégation** des `*.meta.json` → génération du **`manifest.json`** (sans `signature` d’abord) : remplir `artifacts`, `manifest_version`, `mirrors`, `published_at`, `signing_key_id`.  
6. **Signature** du manifest selon [`manifest_v1.md`](manifest_v1.md) (`canonical_json` sans `signature`).  
7. **Mise à jour** de **`keys.json`** si rotation de clé (selon [`keys_v1.md`](keys_v1.md)) — peut être un job manuel ou rare.  
8. **Publication** vers le **stockage** :
   - upload de chaque `.texr` ;
   - upload de `manifest.json` et `keys.json` à des chemins stables (ex. `lcdlln/manifest.json`) ;
   - **ACL** publique en lecture pour CDN en frontal, ou accès authentifié pour phase interne.

### 3.4 Sorties attendues

- Liste d’objets : `v12/core.ui.texr`, … + `manifest.json` + éventuellement `keys.json`.  
- **Métadonnées** de build (numéro de build, commit SHA) en **tags** objet stockage ou fichier `build_info.json` **non** signé (facultatif).

---

## 4. Pipeline B — Code

### 4.1 Déclenchement

- Sur push touchant `engine/**`, sources du client, scripts de build (chemins projet à ajuster).

### 4.2 Étapes logiques

1. Checkout.  
2. Compilation **C++** (CMake, MSBuild, etc.) — **Windows** cible v1.  
3. Tests unitaires (si présents).  
4. Publication de **`lcdlln.exe`** (+ DLL dépendantes) comme **artefact** du job.  
5. **Inclusion** de **`K_embedded`** (clé publique ou matériau dérivé) : injectée au **build** via secret / fichier généré **hors commit** (aligné avec [`keys_v1.md`](keys_v1.md)).

---

## 5. Pipeline C — Release (assemblage)

### 5.1 Déclenchement

- **Manuel** (bouton « Promouvoir release ») ou sur **tag** `v1.2.3`.  
- Entrées : identifiant du **run** assets **validé** + identifiant du **run** code **validé** (ou « dernier vert » sur branche release).

### 5.2 Étapes logiques

1. **Télécharger** `lcdlln.exe` (pipeline B).  
2. **Télécharger** l’ensemble des `.texr` **et** le `manifest.json` **promus** (pipeline A) — ou copie depuis le stockage versionné.  
3. **Assembler** l’arborescence installable, par exemple :
   ```text
   release/
     lcdlln.exe
     packages_cache/   (vide ou .texr par défaut si politique « full offline interdit »)
     manifest.json     (copie pour premier boot ou URL uniquement)
   ```
   En pratique MMORPG : l’install peut ne contenir qu’un **sous-ensemble** de `.texr` ; le client complète au premier lancement via HTTPS ([`texr_client_boot_v1.md`](texr_client_boot_v1.md)).  
4. **Installeur** ou **archive zip** : étape optionnelle (WiX, Inno Setup, 7z, etc.).  
5. **Publication** : page de téléchargement, launcher, ou même bucket « releases ».

---

## 6. Secrets et variables (checklist)

| Secret / variable | Usage |
|-------------------|--------|
| `TEXR_AES_KEY` (ou fichier) | Chiffrement `--encrypt` des `.texr` en release |
| `MANIFEST_SIGNING_SEED` / fichier clé privée Ed25519 | Signature `manifest.json` |
| `KEYS_ROOT_SIGNING` | Signature racine de `keys.json` (rotation rare) |
| `CDN_UPLOAD_*` | Credentials API pour le stockage (S3, Azure Blob, etc.) |
| `EMBEDDED_ED25519_PUBLIC` | Vérification côté client déjà compilée ; le **privé** correspondant ne doit **jamais** être dans le client |

---

## 7. Politique Git — `.gitignore` recommandé

```gitignore
# Packages générés (ne pas versionner)
*.texr
*.texr.meta.json
/packages_out/
/packages_cache/

# Clés et caches locaux
*.dec.tmp
texr_local_secrets/
```

Adapter les chemins au repo réel.

---

## 8. Critère de succès v1 (rappel projet)

- **Mise à jour simulée réussie** : en CI ou script local, enchaîner : build `.texr` → publication sur **serveur HTTP(S) de test** → client de test qui **télécharge** manifest + un package → **vérifie** signatures + hashes → **charge** le pack.  
- Ce scénario peut vivre dans un **job d’intégration** « smoke » sans déployer en production.

---

## 9. Documents associés

| Document | Rôle |
|----------|------|
| [`texr_builder_cli_v1.md`](texr_builder_cli_v1.md) | Commandes et config packages |
| [`manifest_v1.md`](manifest_v1.md) | Champs et signature manifest |
| [`keys_v1.md`](keys_v1.md) | `keys.json` et chaîne de confiance |
| [`texr_client_boot_v1.md`](texr_client_boot_v1.md) | Comportement client au démarrage |
| [`examples/texr_packages.full.example.json`](examples/texr_packages.full.example.json) | Exemple de config packages multi-packs |
