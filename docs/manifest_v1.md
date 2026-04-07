# Manifest de distribution v1 (`manifest.json`)

Spécification du manifest **HTTPS** servant à :

- connaître les **versions** des packages `.texr` ;
- obtenir **chemins** (ou URLs) et **intégrité** (`hash_plain`, `hash_cipher`) ;
- disposer de **miroirs** pour les reprises après échec de signature ou réseau.

Aligné sur les décisions : JSON **UTF-8** sans BOM, signature **Ed25519**, encodage signature **Base64 standard** (RFC 4648, avec padding), clés triées **par ordre alphabétique récursif** pour la canonicalisation, miroirs **dans** le document signé.

Référence format binaire : [`texr_format_v1.md`](texr_format_v1.md).

---

## 1. Fichier et encodage

| Règle | Valeur |
|--------|--------|
| Encodage | **UTF-8**, **sans BOM** |
| Type logique | `application/json` (recommandé côté CDN) |
| Extension | `.json` (souvent `manifest.json`) |

---

## 2. Structure JSON (vue d’ensemble)

Le fichier est **un seul objet** racine. Champs prévus v1 :

| Clé | Type | Obligatoire | Description |
|-----|------|-------------|-------------|
| `artifacts` | objet | oui | Carte **clé logique → descripteur d’artefact** (voir §4) |
| `manifest_version` | nombre entier | oui | **1** pour ce document |
| `mirrors` | tableau de chaînes | non | URLs complètes de **manifests de repli** (même schéma, même règles de signature) |
| `published_at` | chaîne | non | Horodatage ISO 8601 UTC (ex. `2026-04-07T12:00:00Z`) |
| `signing_key_id` | chaîne | non | Identifiant de la clé utilisée pour signer (lien avec `keys.json`) |
| `signature` | chaîne | oui | Signature **Ed25519** (64 octets) encodée en **Base64 standard** |

**Règle de signature** : le champ `signature` **n’entre pas** dans l’octet-string signé. Tous les **autres** champs de l’objet racine entrent dans le message signé, après **canonicalisation** (§3).

Les clés **inconnues** futures à la racine (hors `signature`) : les clients v1 **doivent les ignorer** après vérification de la signature (compatibilité ascendante), sauf si une future spec dit le contraire.

---

## 3. Canonicalisation pour la signature

Le **message signé** est la représentation **UTF-8** du JSON **canonical** de l’objet racine **privé de la clé `signature`**.

### 3.1 Règles (`canonical_json`)

Appliquées **récursivement** :

1. **`null`, booléens, nombres** : sérialisation JSON standard (`null`, `true`, `false`, nombre sans espaces superflus).
2. **Chaînes** : JSON standard avec échappement Unicode (`\uXXXX` uniquement si nécessaire ; préférer UTF-8 brut dans le fichier source, la lib produit l’échappement attendu).
3. **Tableaux** : `[` puis éléments séparés par `,` **sans espace**, ordre des éléments **inchangé** (pas de tri des tableaux).
4. **Objets** : `{` puis paires `clé` : `valeur` séparées par `,` **sans espace** ; les **clés** sont triées par ordre **lexicographique strict des octets UTF-8** (memcmp sur la représentation UTF-8 des chaînes clé).

**Interdit** dans le JSON canonical : tout espace superflu (pas de `\n`, pas d’indentation).

### 3.2 Algorithme de vérification (client)

1. Parser le JSON en objet.  
2. Extraire `signature` (Base64). Décoder en 64 octets.  
3. Construire un clone de l’objet racine **sans** la clé `signature`.  
4. Calculer `canonical_json(clone)` → chaîne → octets UTF-8 `M`.  
5. Vérifier **Ed25519** : `Verify(public_key, M, signature)`.  
6. Si échec : essayer les entrées de `mirrors` (ordre défini), même algorithme, jusqu’à succès ou épuisement → **arrêt** (pas de lancement jeu selon règles MMORPG).

### 3.3 Algorithme de production (builder / serveur)

1. Construire l’objet racine **sans** `signature`, avec toutes les clés requises.  
2. Calculer `M` = UTF-8 de `canonical_json(objet)`.  
3. `signature` = `Sign(private_key, M)` → Base64.  
4. Ajouter `signature` à l’objet ; sérialiser le fichier **pour publication** en JSON **minifié** (peut différer légèrement de `canonical_json` pour l’ordre des clés **à la racine** tant que les clients **ne** re-vérifient **que** via `canonical_json` sans `signature`).  

**Recommandation** : pour éviter toute ambiguïté, le fichier publié peut être **exactement** `canonical_json(objet_avec_signature)` où `signature` est ajouté **en dernier** dans l’objet — mais la vérification **ignore** toujours `signature` pour recomposer `M`.  
**Obligation** : implémentations **builder et client** partagent la **même** fonction `canonical_json` (tests unitaires + vecteurs fixes §7).

---

## 4. Objet `artifacts`

### 4.1 Clés de la carte

Chaque clé est un **identifiant logique stable** côté client (ex. `core.ui`, `race.humains`, `world.zone_0`, `game.loc`, `game.data`, `core.fonts`, …). Même convention que le pipeline de packages décrit dans les échanges projet.

### 4.2 Descripteur d’artefact (valeur)

Objet JSON avec champs (tous obligatoires pour un artefact listé) :

| Clé | Type | Description |
|-----|------|-------------|
| `cipher_size` | nombre entier | Taille du **fichier tel que téléchargé** (enveloppe `OuterFile` complète), en octets |
| `hash_cipher` | chaîne | **SHA-256** du fichier chiffré / publié, encodage **hexadécimal minuscule** (64 caractères) |
| `hash_plain` | chaîne | **SHA-256** du **`InnerFile`** (clair après déchiffrement), hex minuscule 64 caractères |
| `relative_path` | chaîne | Chemin relatif sous la **base CDN** (slash `/`), ex. `v12/hub.ui.texr` |
| `version` | chaîne | Version **affichée / logique** (ex. `"12"` ou semver si vous uniformisez) |

**Ordre des clés dans l’objet descripteur** lors du `canonical_json` : ordre alphabétique sur les clés (`cipher_size`, `hash_cipher`, `hash_plain`, `relative_path`, `version`).

**Téléchargement** : URL effective = concaténation contrôlée de `cdn_base` (config client ou champ futur) + `relative_path`, sauf si vous introduisez des URLs absolues dans une version ultérieure de spec.

---

## 5. Tableau `mirrors`

- Tableau de **chaînes** : chaque élément est une **URL HTTPS** pointant vers un **autre manifest** de **même schéma** (mêmes règles de signature et canonicalisation).  
- En cas d’échec de vérification de signature ou d’erreur réseau sur le manifest principal, le client **réessaie** avec chaque miroir (politique de retry globale : backoff manifest déjà définie ailleurs).  
- Les miroirs sont **signés** : une fois le manifest principal accepté, la liste `mirrors` est **authentifiée** ; pour les miroirs eux-mêmes, la chaîne de confiance repose sur **Ed25519** de chaque fichier récupéré.

---

## 6. Fichier `keys.json` (rotation)

Fichier **distinct** du manifest, servi en **HTTPS**. La spec détaillée (structure, `delegations`, signature racine, validation) est dans **[`keys_v1.md`](keys_v1.md)**.

En résumé : le client valide la **signature racine** du fichier avec la clé **`K_embedded`** de l’exécutable, puis la **chaîne `delegations`** pour obtenir l’ensemble **`Known`** des clés autorisées à signer les manifests.

---

## 7. Vecteurs de test (à compléter)

Après la première implémentation, ajouter à ce document :

1. Un **petit JSON** minimal (sans signature) et la **chaîne** `canonical_json` attendue octet pour octet.  
2. Une **paire de clés** Ed25519 de test et la **signature Base64** correspondante.  
3. Un exemple **échec** (signature altérée) pour tests de non-régression.

---

## 8. Exemple minimal (illustratif, non signé)

> L’exemple ci-dessous est **indenté pour lecture** ; la version publiée doit être **minifiée**, et la `signature` doit être calculée sur le canonical **sans** cette clé.

```json
{
  "artifacts": {
    "core.ui": {
      "cipher_size": 1048576,
      "hash_cipher": "0000000000000000000000000000000000000000000000000000000000000000",
      "hash_plain": "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
      "relative_path": "v12/core.ui.texr",
      "version": "12"
    }
  },
  "manifest_version": 1,
  "mirrors": [
    "https://cdn2.example.com/lcdlln/manifest.json"
  ],
  "published_at": "2026-04-07T12:00:00Z",
  "signing_key_id": "2026-04",
  "signature": "BASE64_ED25519_64_BYTES"
}
```

**Ordre alphabétique des clés racine pour canonical** : `artifacts`, `manifest_version`, `mirrors`, `published_at`, `signing_key_id` — puis `signature` **exclu** du message signé.

---

## 9. Cohérence avec `.texr`

- `hash_plain` : hash du **`InnerFile`** (déchiffré), c’est-à-dire le contenu décrit dans [`texr_format_v1.md`](texr_format_v1.md) **sans** l’enveloppe IV/tag si chiffré.  
- `hash_cipher` : hash du **fichier octets** exactement comme sur le CDN (enveloppe `OuterFile` complète).

Après téléchargement, le client vérifie `hash_cipher` ; après déchiffrement, `hash_plain` ; puis ouverture comme `InnerFile`.
