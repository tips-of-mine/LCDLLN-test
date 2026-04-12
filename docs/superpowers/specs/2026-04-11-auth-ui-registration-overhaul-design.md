# Auth UI — Correctifs visuels & refonte de l'inscription

**Date :** 2026-04-11
**Résolution de test principale :** 1920×1080
**Périmètre :** 4 sous-projets indépendants à livrer dans l'ordre A → B → C → D

---

## Sous-projet A — Correctifs visuels (chevauchement + titre décalé)

### Problème 1 : Chevauchement titre de section / libellé du premier champ

**Fichier :** `engine/render/AuthUiRenderer.cpp` — `BuildAuthUiLayoutMetrics`

**Cause :** Le calcul de `topOffset` utilise un plancher de 88px qui ne tient pas compte du fait que le libellé du premier champ est dessiné à `topOffset - labelAboveFieldPx` (environ 28px au-dessus). Résultat : le titre de section et le libellé du premier champ se chevauchent de 3 à 10px selon la résolution.

**Correctif :** Avant d'appliquer le `std::max`, calculer un minimum supplémentaire garantissant que le libellé du premier champ commence après la fin du titre de section avec une marge de 6px :

```cpp
const int32_t smallScaleGlyph    = std::max(2, smallScale - 1);
const int32_t labelAboveFieldPxGlyph = smallScaleGlyph * 11 + 6;
const int32_t sectionTitleGlyphH = 7 * smallScale;
const int32_t minTopFromSection  =
    metrics.authSectionTitleOffsetFromPanelTopPx
    + sectionTitleGlyphH + 6 + labelAboveFieldPxGlyph;

// Remplacer les lignes topOffset existantes par :
metrics.topOffset = std::max({afterSection + 12, 88, minTopFromSection});
// (même remplacement dans la branche infoBanner)
```

### Problème 2 : Titre horizontalement décalé sur certains écrans

**Fichier :** `engine/render/AuthUiRenderer.cpp` — `BuildAuthUiLayoutMetrics`

**Cause :** La condition `minimalAuthWide` ne couvre que `login`, `registerMode` et `error`. Les pages `verifyEmail`, `forgotPassword` et `characterCreate` reçoivent `authTitleUseViewportWidth = false`, forçant leur titre dans la colonne de contenu (~500px décalée à droite) plutôt que centré sur toute la fenêtre.

**Correctif :**
```cpp
const bool minimalAuthWide =
    (state.login || state.registerMode || state.verifyEmail
     || state.forgotPassword || state.characterCreate || state.error)
    && state.minimalChrome && !state.loginArtColumn;
```

---

## Sous-projet B — Refonte UX de la page d'inscription

### B1 — Layout grille 3 colonnes

Remplacement de la liste verticale de champs par une grille à colonnes variables.
Les champs reçoivent deux nouveaux attributs dans `RenderField` : `gridColumn` (0, 1, 2) et `gridSpan` (1, 2 ou 3).

Disposition :

```
┌─────────────────────────┬──────────────┬──────────────┐
│ Nom d'utilisateur       │              │ Pays         │
│ (col 0, span 1)         │              │ (col 2)      │
├─────────────────────────┬──────────────┤              │
│ Nom (col 0)             │ Prénom       │              │
│                         │ (col 1)      │              │
├─────────────────────────┴──────────────┴──────────────┤
│ Email                   (col 0, span 3)               │
├──────────────┬──────────────────────────┬─────────────┤
│ Jour (col 0) │ Mois (col 1)             │ Année(col 2)│
├──────────────┴──────────────────────────┴─────────────┤
│ Mot de passe            (col 0, span 3)               │
├───────────────────────────────────────────────────────┤
│ Confirmation MDP        (col 0, span 3)               │
└───────────────────────────────────────────────────────┘
```

**Préservation des saisies en cas d'erreur :**
- En cas d'erreur serveur partielle (nom pris, email déjà utilisé, etc.), tous les champs conservent leur valeur courante.
- Seul le champ incriminé reçoit un indicateur d'erreur visuel (barre rouge en bas + message inline dans `errorText` ciblé par champ, pas global).
- L'utilisateur ne re-saisit que le ou les champs fautifs.

**Fichiers :**
- `engine/client/AuthUi.h` : ajout de `gridColumn`, `gridSpan` sur `RenderField` ; ajout de `fieldErrorText` pour les erreurs par champ
- `engine/client/AuthUi.cpp` : logique du formulaire d'inscription, gestion des erreurs partielles
- `engine/render/AuthUiRenderer.cpp` : rendu grille (calcul X par colonne, positionnement Y par ligne logique)
- `engine/render/AuthGlyphPass.cpp` : rendu texte en grille (libellés + valeurs positionnés selon `gridColumn`/`gridSpan`)

### B2 — Deux polices TTF distinctes

- **Windlass.ttf** (existant) → libellés des champs (`label`, titres, messages)
- **Morpheus.ttf** (nouveau) → valeurs saisies dans les zones de texte

`AuthGlyphPass` acquiert un second atlas TTF (second pipeline Vulkan, second descripteur).
`AppendText` est routé vers l'atlas Windlass ou Morpheus selon un flag `useValueFont` passé en paramètre.

**Config :** nouvelles clés dans `config.json` :
```json
"value_font_path": "fonts/Morpheus.ttf",
"value_font_pixel_height": 24
```

**Fichiers :**
- `engine/render/AuthGlyphPass.h/.cpp` : second atlas TTF (`m_valueFontGpuReady`, pipeline dédié, `UploadValueFontFromTtf`, `AppendTextValueFont`)
- `engine/render/AuthUiRenderer.h` : constante `kAuthUiValueFontEnabled`
- `config.json` : nouvelles clés

### B3 — Validation mot de passe en temps réel

Pendant la saisie du champ "Confirmation MDP", afficher à droite du champ un indicateur coloré :
- Barre verte (2px en bas du champ) si les deux mots de passe correspondent
- Barre rouge + message court si différents et champ non vide

La comparaison est purement locale (aucun appel réseau).

**Fichiers :** `engine/client/AuthUi.cpp` (état de correspondance), `engine/render/AuthUiRenderer.cpp` (barre colorée conditionnelle).

### B4 — Sélecteurs de date et pays améliorés

| Champ | Ancien comportement | Nouveau comportement |
|---|---|---|
| Jour | Cycle clavier 1–31 | Saisie directe clavier (1–31) + flèches ±1 |
| Mois | Cycle numérique 1–12 | Cycle → nom complet localisé (Janvier, Février… / January, February…) |
| Année | Cycle depuis 1900 | Saisie directe 4 chiffres ; valeur initiale = année courante − 25 |
| Pays | Absent | Nouveau champ cycle : code ISO 2 lettres + nom localisé (ex. FR — France) |

Noms de mois et de pays ajoutés dans les fichiers de localisation `fr.json` et `en.json`.

**Fichiers :**
- `engine/client/AuthUi.cpp` : logique saisie date/pays, listes mois + pays
- `game/data/localization/fr/fr.json` : clés `month.1`…`month.12`, `country.FR`…
- `game/data/localization/en/en.json` : idem en anglais

---

## Sous-projet C — Vérification disponibilité du nom d'utilisateur

**Déclenchement :** 800 ms après la dernière frappe dans le champ "Nom d'utilisateur", si ≥ 3 caractères.

**Flux :**
1. Client envoie `USERNAME_AVAILABLE_REQUEST { login: string }` au master
2. Master : `SELECT COUNT(*) FROM accounts WHERE login = ?`
3. Master répond `USERNAME_AVAILABLE_RESPONSE { available: bool }`
4. Client : indicateur visuel sur le champ
   - Barre verte + icône ✓ : disponible
   - Barre rouge + message "Nom déjà utilisé" : indisponible
   - Barre grise + spinner : vérification en cours

**Concurrence :** Si l'utilisateur retape pendant qu'une vérification est en cours, la réponse en retard est ignorée (numéro de séquence côté client).

**Fichiers :**
- `engine/client/AuthUi.cpp` : timer debounce, worker async, état indicateur
- `engine/network/` : nouveau type de message (ou réutilisation du mécanisme existant)
- Serveur (handler à identifier) : nouveau handler `USERNAME_AVAILABLE`

---

## Sous-projet D — TAG-ID

### Format

```
CC  Y  MM  XXXXX
```

| Segment | Taille | Contenu |
|---|---|---|
| CC | 2 | Code pays ISO (depuis le champ pays saisi à l'inscription) |
| Y | 1 | Dernier chiffre de l'année d'inscription (ex. 2026 → `6`) |
| MM | 2 | Mois d'inscription, zero-paddé (ex. avril → `04`) |
| XXXXX | 5 | Séquence unique zero-paddée par préfixe `CCYMM` |

Exemple : `FR60400123`

### Règles
- Généré **côté serveur uniquement**, au moment de l'inscription
- **Jamais modifiable** une fois créé (pas d'endpoint de mise à jour)
- Unicité garantie par contrainte `UNIQUE` en base + logique de génération

### Migration BDD

Nouveau fichier `db/migrations/0016_tag_id.sql` :
```sql
ALTER TABLE accounts ADD COLUMN tag_id VARCHAR(10) NOT NULL DEFAULT '';
CREATE UNIQUE INDEX idx_accounts_tag_id ON accounts(tag_id);
```

Même migration à dupliquer dans `deploy/docker/db/migrations/`.

### Génération côté serveur

Au moment de l'inscription :
1. Construire le préfixe `CCYMM` depuis le pays et la date courante
2. `SELECT COALESCE(MAX(CAST(SUBSTR(tag_id, 6) AS INTEGER)), 0) + 1 FROM accounts WHERE tag_id LIKE 'CCYMM%'`
3. Zero-padder sur 5 chiffres → TAG-ID final
4. Insérer avec le compte (transaction atomique)

### Affichage client

Le TAG-ID est retourné dans la réponse de connexion (`LOGIN_RESPONSE`) et affiché :
- Dans l'écran de confirmation post-inscription
- Futur écran de profil (hors périmètre de cette spec)

**Fichiers :**
- `db/migrations/0016_tag_id.sql`
- `deploy/docker/db/migrations/0016_tag_id.sql`
- Serveur : handler inscription + handler login (pour retourner le tag_id)
- `engine/client/AuthUi.cpp` : affichage confirmation post-inscription

---

## Ordre de livraison recommandé

1. **A** — Correctifs visuels (2 fichiers, faible risque)
2. **B** — Refonte UX inscription (client uniquement, pas de dépendance réseau)
3. **D** — TAG-ID (BDD + serveur, puis client)
4. **C** — Vérification nom disponible (après D, partage le même mécanisme async)

---

## Fichiers récapitulatifs

| Fichier | Sous-projets |
|---|---|
| `engine/render/AuthUiRenderer.cpp` | A, B1, B3 |
| `engine/render/AuthGlyphPass.h/.cpp` | B2 |
| `engine/client/AuthUi.h` | B1 |
| `engine/client/AuthUi.cpp` | B1, B3, B4, C, D |
| `engine/render/AuthUiRenderer.h` | B2 |
| `config.json` | B2 |
| `game/data/localization/fr/fr.json` | B4 |
| `game/data/localization/en/en.json` | B4 |
| `db/migrations/0016_tag_id.sql` | D |
| `deploy/docker/db/migrations/0016_tag_id.sql` | D |
| Serveur — handler inscription/login | C, D |
