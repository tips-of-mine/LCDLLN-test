# Fichier `keys.json` v1 (rotation Ed25519)

Spécification du fichier **`keys.json`**, distinct du [`manifest.json`](manifest_v1.md), servant à publier les **clés publiques Ed25519** autorisées à signer les manifests, avec **rotation** sans rebuild immédiat du client (délégations signées).

Références : [`manifest_v1.md`](manifest_v1.md) (canonicalisation JSON), [`texr_format_v1.md`](texr_format_v1.md).

---

## 1. Rôle

| Objectif | Détail |
|----------|--------|
| Lister les signataires | Identifiants stables (`id`) → clé publique **32 octets** (Ed25519, encodée **Base64 standard**) |
| Chaîne de confiance | Ancrage sur **`K_embedded`** (clé publique **embarquée** dans l’exécutable) |
| Rotation | Ajout de nouveaux signataires par **délégation signée** par un signataire **déjà** validé |

Le client **ne** fait confiance **à aucune** clé du fichier tant que la **structure + signatures** ne sont pas validées.

---

## 2. Encodage et transport

| Règle | Valeur |
|--------|--------|
| Encodage | **UTF-8** sans BOM |
| Transport | **HTTPS**, TLS strict (comme le manifest) |

---

## 3. Structure JSON racine

| Clé | Type | Obligatoire | Description |
|-----|------|-------------|-------------|
| `delegations` | tableau | oui | Liste ordonnée de **blocs de délégation** (voir §4) |
| `keys_version` | nombre entier | oui | **1** pour ce document |
| `signature` | chaîne | oui | Signature **Ed25519** (64 octets) en **Base64 standard** du message défini en §5 |

**Champs futurs** à la racine (hors `signature`) : le client v1 **les ignore** après vérification réussie de `signature`, sauf spec ultérieure.

---

## 4. Éléments de `delegations`

Chaque élément du tableau est un **objet** avec les champs suivants (tous obligatoires pour l’élément) :

| Clé | Type | Description |
|-----|------|-------------|
| `delegate_id` | chaîne | Identifiant stable du **nouveau** signataire de manifest (ex. `"2026-04"`) |
| `issuer_id` | chaîne | `"embedded"` **ou** un `delegate_id` **déjà** accepté plus tôt dans le tableau |
| `public_key` | chaîne | Clé publique Ed25519 **raw** (32 octets), **Base64 standard** |
| `signature` | chaîne | Signature Ed25519 (64 octets) **Base64** du bloc **sans** cette clé, après canonicalisation (§4.1) |

### 4.1 Message signé pour une délégation

Soit `D` l’objet délégation **sans** la clé `signature`. Le message signé est :

`M = UTF-8( canonical_json(D) )`

où `canonical_json` est **la même règle** que pour le manifest : objets avec clés triées par octets UTF-8, pas d’espaces superflus, tableaux non réordonnés.

**Ordre alphabétique des clés dans `D`** : `delegate_id`, `issuer_id`, `public_key`.

### 4.2 Vérification de la signature d’une délégation

- Si `issuer_id` == `"embedded"` : vérifier avec **`K_embedded`**.  
- Sinon : vérifier avec la clé publique enregistrée pour le signataire `issuer_id` (déjà validé à une étape précédente du tableau).

### 4.3 Construction de l’ensemble des signataires manifest

1. Initialiser `Known = {}` (id → pubkey 32 octets).  
2. Parcourir `delegations` **dans l’ordre du tableau** :  
   - Vérifier la signature du bloc (§4.2).  
   - En cas de succès : `Known[delegate_id] = DecodeBase64(public_key)`.  
   - En cas d’échec : **rejeter tout le fichier `keys.json`**.  
3. Après succès global : les manifests dont `signing_key_id` ∈ `Known` peuvent être vérifiés avec `Known[signing_key_id]`.

**Unicité** : un même `delegate_id` ne doit **pas** réapparaître ; si réapparition → **rejet** (implémentation stricte).

---

## 5. Signature du fichier entier (`signature` racine)

Le champ racine `signature` authentifie **l’intégrité** du contenu publié (liste des délégations telle qu’affichée).

Soit `R` l’objet racine **sans** la clé `signature`. Message :

`M_root = UTF-8( canonical_json(R) )`

Vérification : **Ed25519** avec **`K_embedded`**.

**Ordre** des vérifications côté client (recommandé) :

1. Parser JSON.  
2. Vérifier `signature` racine avec `K_embedded` sur `canonical_json(R)`.  
3. Si OK, parcourir et valider chaque entrée de `delegations` (§4).  
4. Si une délégation échoue → **rejet** (ne pas utiliser un sous-ensemble partiel).

> Ainsi, même si un attaquant ajoute des délégations falsifiées, la **signature racine** échoue sauf compromission de `K_embedded` ou du pipeline de publication.

---

## 6. Lien avec le manifest

- Le manifest porte `signing_key_id` (optionnel mais recommandé).  
- Le client résout `public_key = Known[signing_key_id]` puis vérifie le manifest comme en §3 de [`manifest_v1.md`](manifest_v1.md).  
- Si `signing_key_id` absent : politique **stricte** recommandée → **rejet** ; ou politique optionnelle : unique clé dans `Known` si une seule entrée (à documenter si vous l’autorisez).

---

## 7. Exemple minimal (illustratif)

> Indenté pour lecture ; publication **minifiée**. Les signatures sont fictives.

**Première délégation** (issuer = embarqué) :

```json
{
  "delegate_id": "2026-04",
  "issuer_id": "embedded",
  "public_key": "BASE64_32_BYTES",
  "signature": "BASE64_64_BYTES"
}
```

**Fichier `keys.json` complet** :

```json
{
  "delegations": [
    {
      "delegate_id": "2026-04",
      "issuer_id": "embedded",
      "public_key": "BASE64_32_BYTES",
      "signature": "SIG_DELEGATION_1"
    }
  ],
  "keys_version": 1,
  "signature": "SIG_RACINE_SUR_OBJET_SANS_SIGNATURE"
}
```

**Ordre des clés racine pour `canonical_json` (hors `signature`)** : `delegations`, `keys_version`.

---

## 8. Miroirs et reprises

Le client peut réutiliser la **même** logique que pour le manifest : liste d’URL de repli **dans** un manifest signé, ou fichier de config local listant des URL `keys.json` de secours. À documenter au même endroit que le flux réseau ([`texr_client_boot_v1.md`](texr_client_boot_v1.md)).

---

## 9. Vecteurs de test (à compléter)

Après implémentation : paire Ed25519 de test, un `keys.json` minimal signé correctement, un cas `issuer_id` chaînée (`2026-04` signe `2026-07`), un cas d’échec (signature délégation altérée).
