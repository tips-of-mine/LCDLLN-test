# Auth UI — Correctifs visuels (Sous-projet A) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Corriger deux bugs visuels dans `BuildAuthUiLayoutMetrics` : chevauchement titre-de-section / libellé-de-champ, et titre horizontalement décalé sur les pages verifyEmail / forgotPassword / characterCreate.

**Architecture:** Deux modifications chirurgicales dans la fonction pure `BuildAuthUiLayoutMetrics` (`engine/render/AuthUiRenderer.cpp`). Aucun nouveau fichier, aucune dépendance ajoutée.

**Tech Stack:** C++20, Vulkan (headers only pour les types), CMake / MSVC sur Windows.

---

## Fichiers modifiés

| Fichier | Rôle |
|---|---|
| `engine/render/AuthUiRenderer.cpp` | Calcul des métriques de layout (seul fichier à modifier) |

---

### Task 1 : Correctif — chevauchement titre de section / libellé du premier champ

Le libellé du premier champ est dessiné à `panelY + topOffset - labelAboveFieldPxGlyph`.
Le `topOffset` actuel ne garantit pas que cette position est **après** la fin du titre de section.

**Files:**
- Modify: `engine/render/AuthUiRenderer.cpp:201-210`

- [ ] **Step 1 : Localiser le bloc à modifier**

Ouvrir `engine/render/AuthUiRenderer.cpp`. Repérer les lignes suivantes (vers la ligne 201) :

```cpp
			const int32_t afterSection = metrics.authSectionTitleOffsetFromPanelTopPx + bodyLineStep;
			if (!model.infoBanner.empty() && !metrics.authStatusBannerBesideLogo)
			{
				// Bannière : fond à panelY + topOffset - 42, hauteur 34 ; marge sous la section avant la bannière.
				metrics.topOffset = std::max(afterSection + 48, 146);
			}
			else
			{
				metrics.topOffset = std::max(afterSection + 12, 88);
			}
```

- [ ] **Step 2 : Appliquer le correctif**

Remplacer ces 9 lignes par :

```cpp
			const int32_t afterSection = metrics.authSectionTitleOffsetFromPanelTopPx + bodyLineStep;
			// Le libellé du premier champ (AuthGlyphPass) est dessiné à topOffset - labelAboveFieldPxGlyph.
			// smallScaleGlyph = max(2, smallScale - 1) car le glyph pass applique un niveau supplémentaire.
			const int32_t smallScaleGlyph = std::max(2, smallScale - 1);
			const int32_t labelAboveFieldPxGlyph = smallScaleGlyph * 11 + 6;
			const int32_t sectionTitleGlyphH = 7 * smallScale;
			const int32_t minTopFromSection =
				metrics.authSectionTitleOffsetFromPanelTopPx + sectionTitleGlyphH + 6 + labelAboveFieldPxGlyph;
			if (!model.infoBanner.empty() && !metrics.authStatusBannerBesideLogo)
			{
				// Bannière : fond à panelY + topOffset - 42, hauteur 34 ; marge sous la section avant la bannière.
				metrics.topOffset = std::max({ afterSection + 48, 146, minTopFromSection });
			}
			else
			{
				metrics.topOffset = std::max({ afterSection + 12, 88, minTopFromSection });
			}
```

- [ ] **Step 3 : Vérifier que le code compile**

```bash
cmake --build . --target engine_app --config Release 2>&1 | tail -20
```

Résultat attendu : `Build succeeded` (ou équivalent MSVC), zéro erreur.

- [ ] **Step 4 : Vérification visuelle**

Lancer `lcdlln.exe`. Sur l'écran de connexion (1920×1080) :
- Le titre de section (ex. "Connexion") et le libellé "Identifiant" ne doivent plus se superposer.
- Un espace d'au moins 6px doit être visible entre les deux textes.

- [ ] **Step 5 : Commit**

```bash
git add engine/render/AuthUiRenderer.cpp
git commit -m "fix(auth-ui): topOffset prend en compte labelAboveFieldPxGlyph pour eviter chevauchement section/label"
```

---

### Task 2 : Correctif — titre décalé sur verifyEmail / forgotPassword / characterCreate

Ces pages reçoivent `authTitleUseViewportWidth = false` car la condition `minimalAuthWide` ne les couvre pas, forçant leur titre dans la colonne de contenu (~500px décalée à droite) plutôt que centré sur toute la fenêtre.

**Files:**
- Modify: `engine/render/AuthUiRenderer.cpp:170-172`

- [ ] **Step 1 : Localiser le bloc à modifier**

Dans le même fichier, repérer (vers la ligne 170) :

```cpp
			const bool minimalAuthWide =
				(state.login || state.registerMode || state.error) && state.minimalChrome && !state.loginArtColumn;
```

- [ ] **Step 2 : Appliquer le correctif**

Remplacer ces 2 lignes par :

```cpp
			const bool minimalAuthWide =
				(state.login || state.registerMode || state.verifyEmail
				 || state.forgotPassword || state.characterCreate || state.error)
				&& state.minimalChrome && !state.loginArtColumn;
```

- [ ] **Step 3 : Compiler**

```bash
cmake --build . --target engine_app --config Release 2>&1 | tail -20
```

Résultat attendu : zéro erreur.

- [ ] **Step 4 : Vérification visuelle**

Tester le flux complet : connexion → inscription → vérification email → création de personnage.
Sur chaque page, le titre principal ("Les Chroniques de la Lune Noire") doit être centré à la même position horizontale que sur la page de connexion.

- [ ] **Step 5 : Commit**

```bash
git add engine/render/AuthUiRenderer.cpp
git commit -m "fix(auth-ui): etendre minimalAuthWide a verifyEmail/forgotPassword/characterCreate pour centrage titre coherent"
```
