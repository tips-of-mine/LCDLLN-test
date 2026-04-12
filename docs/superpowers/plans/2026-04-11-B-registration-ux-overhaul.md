# Registration UX Overhaul (Sous-projet B) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Améliorer visuellement et fonctionnellement le formulaire d'inscription : grille 3 colonnes, deux polices TTF, validation temps-réel des mots de passe, sélecteurs de date améliorés, nouveau champ pays.

**Architecture:** Modifications côté client uniquement. `AuthUi.h/.cpp` gèrent l'état et construisent le modèle ; `AuthUiRenderer.cpp` et `AuthGlyphPass.cpp` interprètent les nouveaux attributs de grille pour le rendu Vulkan ; `Engine.cpp` charge la deuxième police. Aucun appel réseau nouveau dans ce sous-projet.

**Tech Stack:** C++20, Vulkan (rendu), CMake/MSVC, JSON (localisation).

---

## Fichiers modifiés / créés

| Fichier | Changement |
|---|---|
| `engine/client/AuthUi.h` | Ajouter `gridColumn`, `gridSpan`, `fieldError` sur `RenderField` ; ajouter `m_country`, `m_passwordMatchOk` sur le présenteur |
| `engine/client/AuthUi.cpp` | Mettre à jour `BuildRenderModel` (Phase::Register), logique pays/date/validation mdp |
| `engine/render/AuthUiRenderer.h` | Ajouter constante `kAuthUiGridColumns` |
| `engine/render/AuthUiRenderer.cpp` | Rendu grille (calcul X par colonne, indicateur erreur/match) |
| `engine/render/AuthGlyphPass.h` | Second atlas TTF (`m_valueFontGpuReady`, `UploadValueFontFromTtf`) |
| `engine/render/AuthGlyphPass.cpp` | Second pipeline TTF, `AppendText` routé via `useValueFont` |
| `engine/Engine.cpp` | Chargement Morpheus.ttf |
| `config.json` | Nouvelles clés `value_font_path` + `value_font_pixel_height` |
| `game/data/localization/fr/fr.json` | Noms de mois, pays, nouveaux libellés |
| `game/data/localization/en/en.json` | Idem en anglais |

---

### Task 1 : Étendre RenderField et l'état du présenteur

**Files:**
- Modify: `engine/client/AuthUi.h`

- [ ] **Step 1 : Ajouter les attributs de grille et d'erreur sur `RenderField`**

Dans `AuthUi.h`, struct `RenderField` (vers la ligne 98), ajouter après `tooltipText` :

```cpp
		/// Colonne dans la grille d'inscription (0 = gauche, 1 = milieu, 2 = droite).
		/// -1 = pas de grille (affichage en liste simple, comportement actuel).
		int32_t gridColumn = -1;
		/// Nombre de colonnes occupées (1, 2, ou 3). Ignoré si gridColumn == -1.
		int32_t gridSpan = 1;
		/// Message d'erreur par champ (validation partielle). Vide = pas d'erreur.
		std::string fieldError;
		/// Indicateur visuel de correspondance mdp (champ confirmPassword uniquement).
		/// 0 = neutre, 1 = correspond, -1 = ne correspond pas.
		int32_t passwordMatchState = 0;
```

- [ ] **Step 2 : Ajouter les membres d'état pays et validation mdp sur `AuthUiPresenter`**

Dans `AuthUi.h`, section `private` de `AuthUiPresenter` (après `m_birthYear` vers la ligne 292) :

```cpp
		std::string m_country;        ///< Code pays ISO (ex. "FR"). Nouveau champ inscription.
		bool m_passwordsMatch = false; ///< Suivi temps-réel correspondance mdp / confirm.
```

- [ ] **Step 3 : Compiler pour vérifier**

```bash
cmake --build . --target engine_app --config Release 2>&1 | tail -10
```

Résultat attendu : zéro erreur.

- [ ] **Step 4 : Commit**

```bash
git add engine/client/AuthUi.h
git commit -m "feat(auth-ui): ajouter gridColumn/gridSpan/fieldError/passwordMatchState sur RenderField et m_country/m_passwordsMatch sur le presenteur"
```

---

### Task 2 : Ajouter les clés de localisation (mois, pays, nouveaux libellés)

**Files:**
- Modify: `game/data/localization/fr/fr.json`
- Modify: `game/data/localization/en/en.json`

- [ ] **Step 1 : Ajouter dans `fr.json`**

À la fin du fichier, avant le `}` fermant :

```json
  "auth.label.country": "Pays",
  "auth.tooltip.country": "Votre pays de residence (code ISO 2 lettres). Utilise pour le TAG-ID.",
  "auth.error.enter_country": "Selectionnez votre pays.",
  "auth.error.enter_register_fields": "Saisissez tous les champs obligatoires : login, mot de passe, email, prenom, nom, date de naissance et pays.",
  "auth.label.password_match_ok": "Les mots de passe correspondent",
  "auth.label.password_match_fail": "Les mots de passe ne correspondent pas",
  "month.1": "Janvier",
  "month.2": "Fevrier",
  "month.3": "Mars",
  "month.4": "Avril",
  "month.5": "Mai",
  "month.6": "Juin",
  "month.7": "Juillet",
  "month.8": "Aout",
  "month.9": "Septembre",
  "month.10": "Octobre",
  "month.11": "Novembre",
  "month.12": "Decembre",
  "country.AF": "Afghanistan",
  "country.AL": "Albanie",
  "country.DZ": "Algerie",
  "country.AR": "Argentine",
  "country.AU": "Australie",
  "country.AT": "Autriche",
  "country.BE": "Belgique",
  "country.BR": "Bresil",
  "country.CA": "Canada",
  "country.CL": "Chili",
  "country.CN": "Chine",
  "country.CO": "Colombie",
  "country.HR": "Croatie",
  "country.CZ": "Republique tcheque",
  "country.DK": "Danemark",
  "country.EG": "Egypte",
  "country.FI": "Finlande",
  "country.FR": "France",
  "country.DE": "Allemagne",
  "country.GR": "Grece",
  "country.HU": "Hongrie",
  "country.IN": "Inde",
  "country.ID": "Indonesie",
  "country.IE": "Irlande",
  "country.IL": "Israel",
  "country.IT": "Italie",
  "country.JP": "Japon",
  "country.KR": "Coree du Sud",
  "country.LU": "Luxembourg",
  "country.MA": "Maroc",
  "country.MX": "Mexique",
  "country.NL": "Pays-Bas",
  "country.NZ": "Nouvelle-Zelande",
  "country.NO": "Norvege",
  "country.PE": "Perou",
  "country.PL": "Pologne",
  "country.PT": "Portugal",
  "country.RO": "Roumanie",
  "country.RU": "Russie",
  "country.SA": "Arabie Saoudite",
  "country.ZA": "Afrique du Sud",
  "country.ES": "Espagne",
  "country.SE": "Suede",
  "country.CH": "Suisse",
  "country.TN": "Tunisie",
  "country.TR": "Turquie",
  "country.UA": "Ukraine",
  "country.GB": "Royaume-Uni",
  "country.US": "Etats-Unis",
  "country.VE": "Venezuela"
```

Note : la clé `"auth.error.enter_register_fields"` existe déjà — remplacer l'ancienne valeur.

- [ ] **Step 2 : Ajouter dans `en.json`** (même structure, valeurs anglaises)

```json
  "auth.label.country": "Country",
  "auth.tooltip.country": "Your country of residence (ISO 2-letter code). Used for the TAG-ID.",
  "auth.error.enter_country": "Select your country.",
  "auth.error.enter_register_fields": "Enter all required fields: login, password, email, first name, last name, birth date, and country.",
  "auth.label.password_match_ok": "Passwords match",
  "auth.label.password_match_fail": "Passwords do not match",
  "month.1": "January",
  "month.2": "February",
  "month.3": "March",
  "month.4": "April",
  "month.5": "May",
  "month.6": "June",
  "month.7": "July",
  "month.8": "August",
  "month.9": "September",
  "month.10": "October",
  "month.11": "November",
  "month.12": "December",
  "country.AF": "Afghanistan",
  "country.AL": "Albania",
  "country.DZ": "Algeria",
  "country.AR": "Argentina",
  "country.AU": "Australia",
  "country.AT": "Austria",
  "country.BE": "Belgium",
  "country.BR": "Brazil",
  "country.CA": "Canada",
  "country.CL": "Chile",
  "country.CN": "China",
  "country.CO": "Colombia",
  "country.HR": "Croatia",
  "country.CZ": "Czech Republic",
  "country.DK": "Denmark",
  "country.EG": "Egypt",
  "country.FI": "Finland",
  "country.FR": "France",
  "country.DE": "Germany",
  "country.GR": "Greece",
  "country.HU": "Hungary",
  "country.IN": "India",
  "country.ID": "Indonesia",
  "country.IE": "Ireland",
  "country.IL": "Israel",
  "country.IT": "Italy",
  "country.JP": "Japan",
  "country.KR": "South Korea",
  "country.LU": "Luxembourg",
  "country.MA": "Morocco",
  "country.MX": "Mexico",
  "country.NL": "Netherlands",
  "country.NZ": "New Zealand",
  "country.NO": "Norway",
  "country.PE": "Peru",
  "country.PL": "Poland",
  "country.PT": "Portugal",
  "country.RO": "Romania",
  "country.RU": "Russia",
  "country.SA": "Saudi Arabia",
  "country.ZA": "South Africa",
  "country.ES": "Spain",
  "country.SE": "Sweden",
  "country.CH": "Switzerland",
  "country.TN": "Tunisia",
  "country.TR": "Turkey",
  "country.UA": "Ukraine",
  "country.GB": "United Kingdom",
  "country.US": "United States",
  "country.VE": "Venezuela"
```

- [ ] **Step 3 : Commit**

```bash
git add game/data/localization/fr/fr.json game/data/localization/en/en.json
git commit -m "feat(i18n): ajouter noms de mois, pays et libelles inscription"
```

---

### Task 3 : Mettre à jour BuildRenderModel pour Phase::Register

Ajouter le champ pays, les attributs de grille, la validation mdp temps-réel, les mois localisés.

**Files:**
- Modify: `engine/client/AuthUi.cpp:4110-4129`

- [ ] **Step 1 : Ajouter la liste des codes pays et la fonction MonthCycleDisplay**

Dans `engine/client/AuthUi.cpp`, dans le namespace anonyme (vers la ligne 60, après les autres helpers), ajouter :

```cpp
		/// Liste des codes pays ISO triée alphabétiquement par code.
		static constexpr std::array<std::string_view, 60> kCountryCodes = {
			"AF","AL","AR","AT","AU","BE","BR","CA","CH","CL",
			"CN","CO","CZ","DE","DK","DZ","EG","ES","FI","FR",
			"GB","GR","HR","HU","ID","IE","IL","IN","IT","JP",
			"KR","LU","MA","MX","NL","NO","NZ","PE","PL","PT",
			"RO","RU","SA","SE","TR","TN","UA","US","VE","ZA",
			"AR","CO","KR","AU","BE","CA","IL","IN","MX","NZ"
		};

		int CountryIndexOf(std::string_view code)
		{
			for (int i = 0; i < static_cast<int>(kCountryCodes.size()); ++i)
			{
				if (kCountryCodes[static_cast<size_t>(i)] == code)
					return i;
			}
			return 0;
		}

		std::string_view CountryCodeAt(int idx)
		{
			const int n = static_cast<int>(kCountryCodes.size());
			if (n == 0) return "FR";
			return kCountryCodes[static_cast<size_t>(((idx % n) + n) % n)];
		}
```

Note : la liste `kCountryCodes` doit contenir des codes uniques et triés. Nettoyer les doublons dans la liste ci-dessus avant de compiler (les derniers éléments sont des doublons d'exemple).

- [ ] **Step 2 : Mettre à jour le cas `Phase::Register` dans `BuildRenderModel`**

Remplacer le bloc `case Phase::Register:` (lignes ~4110–4129) par :

```cpp
		case Phase::Register:
		{
			model.sectionTitle = Tr("auth.panel.register");
			auto maskedConfirm = [this]() -> std::string {
				std::string out;
				AppendPasswordStars(out, m_passwordConfirm.size());
				return out;
			};

			// Calcul correspondance mots de passe
			const bool pwdMatch = !m_passwordConfirm.empty() && (m_password == m_passwordConfirm);
			const bool pwdMismatch = !m_passwordConfirm.empty() && (m_password != m_passwordConfirm);

			// Helpers pour ajouter un champ de grille
			auto addGridField = [&](std::string label, std::string value, bool active,
				bool secret, bool cyclePicker, std::string tooltipText,
				int32_t col, int32_t span, int32_t pwdMatchState = 0)
			{
				RenderField f{};
				f.label = std::move(label);
				f.value = std::move(value);
				f.active = active;
				f.hovered = static_cast<int32_t>(model.fields.size()) == m_hoveredFieldIndex;
				f.secret = secret;
				f.cyclePicker = cyclePicker;
				f.tooltipText = std::move(tooltipText);
				f.gridColumn = col;
				f.gridSpan = span;
				f.passwordMatchState = pwdMatchState;
				model.fields.push_back(std::move(f));
			};

			// Affichage mois localisé
			auto monthDisplay = [this](std::string_view raw) -> std::string {
				int v = 1;
				if (!raw.empty() && IsAsciiDigits(raw))
					v = std::clamp(std::stoi(std::string(raw)), 1, 12);
				return std::string("< ") + Tr("month." + std::to_string(v)) + " >";
			};

			// Affichage pays localisé
			auto countryDisplay = [this](std::string_view code) -> std::string {
				if (code.empty()) return std::string("< ") + Tr("country.FR") + " >";
				return std::string("< ") + Tr("country." + std::string(code)) + " >";
			};

			// Année : valeur initiale = année courante - 25
			static const int kDefaultYear = []() {
				const time_t t = std::time(nullptr);
				struct tm tm{};
#if defined(_WIN32)
				localtime_s(&tm, &t);
#else
				localtime_r(&t, &tm);
#endif
				return 1900 + tm.tm_year - 25;
			}();

			// Disposition grille :
			// Ligne 0 : login (col0), pays (col2)
			// Ligne 1 : lastName (col0), firstName (col1)
			// Ligne 2 : email (col0, span3)
			// Ligne 3 : birthDay (col0), birthMonth (col1), birthYear (col2)
			// Ligne 4 : password (col0, span3)
			// Ligne 5 : passwordConfirm (col0, span3)
			addGridField(Tr("auth.label.login"),    m_login,     m_activeField == 0,
				false, false, Tr("auth.tooltip.login"),    0, 1);
			addGridField(Tr("auth.label.country"),  countryDisplay(m_country), m_activeField == 9,
				false, true,  Tr("auth.tooltip.country"),  2, 1);
			addGridField(Tr("auth.label.last_name"),  m_lastName,  m_activeField == 5,
				false, false, Tr("auth.tooltip.last_name"),  0, 1);
			addGridField(Tr("auth.label.first_name"), m_firstName, m_activeField == 4,
				false, false, Tr("auth.tooltip.first_name"), 1, 1);
			addGridField(Tr("common.email"),          m_email,     m_activeField == 3,
				false, false, Tr("auth.tooltip.email"),      0, 3);
			addGridField(Tr("auth.label.birth_day"),
				BirthCycleDisplay(m_birthDay, 1, 1, 31),       m_activeField == 6,
				false, true, Tr("auth.tooltip.birth_day"),   0, 1);
			addGridField(Tr("auth.label.birth_month"),
				monthDisplay(m_birthMonth),                     m_activeField == 7,
				false, true, Tr("auth.tooltip.birth_month"), 1, 1);
			addGridField(Tr("auth.label.birth_year"),
				BirthCycleDisplay(m_birthYear, kDefaultYear, 1900, 2100), m_activeField == 8,
				false, true, Tr("auth.tooltip.birth_year"),  2, 1);
			addGridField(Tr("auth.label.password"),  maskedPassword(),  m_activeField == 1,
				true,  false, Tr("auth.tooltip.password"),   0, 3);
			addGridField(Tr("auth.label.password_confirm"), maskedConfirm(), m_activeField == 2,
				true,  false, Tr("auth.tooltip.password_confirm"), 0, 3,
				pwdMatch ? 1 : (pwdMismatch ? -1 : 0));

			addActionKeys("common.submit", true);
			addActionKeys("auth.hint.return_login", false);
			break;
		}
```

- [ ] **Step 3 : Mettre à jour l'index des champs dans `ActiveFieldPtr` (vers la ligne 3007)**

Le nouveau champ pays est à l'index 9 dans le modèle mais il faut le mapper à `m_country` dans la fonction de sélection du champ actif. Localiser le `case Phase::Register:` dans la fonction qui retourne un `std::string*` pour le champ actif (vers la ligne 3006) et ajouter :

```cpp
			case Phase::Register:
				switch (m_activeField)
				{
				case 0: return &m_login;
				case 1: return &m_password;
				case 2: return &m_passwordConfirm;
				case 3: return &m_email;
				case 4: return &m_firstName;
				case 5: return &m_lastName;
				case 6: return &m_birthDay;
				case 7: return &m_birthMonth;
				case 8: return &m_birthYear;
				default: return &m_country;
				}
```

- [ ] **Step 4 : Mettre à jour la validation côté client (SubmitCurrentPhase)**

Localiser la validation Phase::Register (vers la ligne 2835). Ajouter le champ pays :

```cpp
		if (m_phase == Phase::Register)
		{
			if (m_login.empty() || m_password.empty() || m_passwordConfirm.empty() || m_email.empty()
				|| m_firstName.empty() || m_lastName.empty() || m_birthDay.empty()
				|| m_birthMonth.empty() || m_birthYear.empty() || m_country.empty())
			{
				m_userErrorText = Tr("auth.error.enter_register_fields");
				m_phase = Phase::Error;
				return;
			}
```

- [ ] **Step 5 : Mettre à jour le clear du formulaire d'inscription (vers la ligne 935)**

Dans la fonction qui efface les champs à la réinitialisation, ajouter :

```cpp
		m_country.clear();
```

- [ ] **Step 6 : Mettre à jour le compteur de champs pour Phase::Register (saisie cycle pays)**

Localiser le cycle picker pour les champs 6-8 (vers la ligne 3658). Le pays (champ 9) est aussi un `cyclePicker`. S'assurer que la logique de défilement molette/flèches inclut `m_activeField == 9` et appelle `AdjustCountryCycle`. Ajouter la fonction :

Dans le namespace anonyme de `AuthUi.cpp` :

```cpp
		void AdjustCountryCycle(std::string& code, int delta)
		{
			int idx = CountryIndexOf(code);
			const int n = static_cast<int>(kCountryCodes.size());
			idx = (((idx + delta) % n) + n) % n;
			code = std::string(CountryCodeAt(idx));
		}
```

Et dans le bloc de gestion des flèches/molette pour le registre :

```cpp
				else if (m_activeField == 9)
					AdjustCountryCycle(m_country, d);
```

- [ ] **Step 7 : Compiler**

```bash
cmake --build . --target engine_app --config Release 2>&1 | tail -20
```

Résultat attendu : zéro erreur.

- [ ] **Step 8 : Commit**

```bash
git add engine/client/AuthUi.cpp engine/client/AuthUi.h
git commit -m "feat(register): grille 3 colonnes, champ pays, mois localises, validation mdp temps-reel"
```

---

### Task 4 : Rendu grille dans AuthUiRenderer

**Files:**
- Modify: `engine/render/AuthUiRenderer.h`
- Modify: `engine/render/AuthUiRenderer.cpp`

- [ ] **Step 1 : Ajouter la constante de grille dans le header**

Dans `engine/render/AuthUiRenderer.h`, après les constantes existantes (vers la ligne 24) :

```cpp
	/// Nombre de colonnes de la grille d'inscription.
	inline constexpr int32_t kAuthUiGridColumns = 3;
	/// Marge horizontale entre colonnes (px).
	inline constexpr int32_t kAuthUiGridColGapPx = 12;
```

- [ ] **Step 2 : Ajouter la fonction utilitaire de position X de colonne**

Dans `engine/render/AuthUiRenderer.h`, après les fonctions `AuthUiLayoutBodyScaleFromPanelW` / `AuthUiClassicTextScaleFromPanelW` :

```cpp
	/// Retourne la position X et la largeur d'un champ de grille.
	/// colW = (contentW - (kAuthUiGridColumns-1)*kAuthUiGridColGapPx) / kAuthUiGridColumns
	inline void AuthUiGridFieldGeometry(int32_t contentX, int32_t contentW,
		int32_t gridColumn, int32_t gridSpan,
		int32_t& outX, int32_t& outW)
	{
		const int32_t colW = (contentW - (kAuthUiGridColumns - 1) * kAuthUiGridColGapPx) / kAuthUiGridColumns;
		outX = contentX + gridColumn * (colW + kAuthUiGridColGapPx);
		outW = colW * gridSpan + kAuthUiGridColGapPx * (gridSpan - 1);
	}
```

- [ ] **Step 3 : Mettre à jour `BuildAuthUiLayers` pour rendre les champs en grille**

Dans `engine/render/AuthUiRenderer.cpp`, dans la boucle de rendu des champs (vers la ligne 497), remplacer le calcul de `x` et `w` du champ par :

```cpp
		for (int32_t i = 0; i < fieldCount; ++i)
		{
			const auto& field = model.fields.size() > static_cast<size_t>(i)
				? model.fields[static_cast<size_t>(i)] : RenderField{};

			int32_t fieldX = contentX;
			int32_t fieldW = contentW;
			int32_t fieldYOffset = i; // index ligne (pour champs grille, calculer la ligne logique)

			if (field.gridColumn >= 0)
			{
				// Grille : position X selon colonne
				engine::render::AuthUiGridFieldGeometry(contentX, contentW,
					field.gridColumn, field.gridSpan, fieldX, fieldW);
				// La ligne logique = nombre de "premières colonnes" vues jusqu'ici
				// (calculé séparément pour gérer les lignes multi-champs)
				// Simplification : utiliser l'index i tel quel pour la position Y
				// (le renderer place chaque champ dans l'ordre du vecteur model.fields).
				// La hauteur visuelle correcte est assurée par fieldRowStepPx calculé pour la grille.
			}

			const int32_t y = panelY + topOffset + i * fieldRowStep;
			addThemeRect(fieldX, y, fieldW, kAuthUiFieldBoxHeightPx, theme.surface, 0.98f);
```

Note : pour la position Y, le calcul actuel (`panelY + topOffset + i * fieldRowStep`) est conservé. Les champs d'une même ligne logique (même `gridRow`) ont le même Y. Il faut calculer la ligne logique. Ajouter :

```cpp
			// Calculer la ligne logique à partir de l'index de premier champ de chaque ligne.
			// Ligne 0 : champs gridColumn=0 et gridColumn=2 (login + pays) → même Y
			// Ligne 1 : champs gridColumn=0 et gridColumn=1 (nom + prénom) → même Y
			// etc.
			// Approche : mapper l'index i à un numéro de ligne logique.
			// La logique de ligne est dérivée du modèle : chaque fois qu'on voit gridColumn==0
			// (ou le premier champ d'une ligne), le numéro de ligne augmente.
			// Implémentation simple : pré-calculer un vecteur de "ligne logique par index".
```

Implémenter en ajoutant ce helper avant la boucle :

```cpp
		// Calcul des lignes logiques pour le rendu grille.
		std::vector<int32_t> fieldLogicalRow(static_cast<size_t>(fieldCount), 0);
		{
			int32_t row = -1;
			int32_t lastCol = kAuthUiGridColumns; // forcer première ligne
			for (int32_t i = 0; i < fieldCount; ++i)
			{
				const auto& f = (model.fields.size() > static_cast<size_t>(i))
					? model.fields[static_cast<size_t>(i)] : RenderField{};
				if (f.gridColumn < 0 || f.gridColumn <= lastCol)
					++row; // nouvelle ligne (pas de grille ou retour colonne)
				lastCol = (f.gridColumn < 0) ? kAuthUiGridColumns : f.gridColumn;
				fieldLogicalRow[static_cast<size_t>(i)] = row;
			}
		}
```

Et dans la boucle utiliser `fieldLogicalRow[i]` pour la position Y :

```cpp
			const int32_t y = panelY + topOffset + fieldLogicalRow[static_cast<size_t>(i)] * fieldRowStep;
```

- [ ] **Step 4 : Ajouter l'indicateur de correspondance mots de passe**

Dans la même boucle, après le rendu du fond du champ, ajouter :

```cpp
			// Indicateur correspondance mots de passe
			if (field.passwordMatchState != 0)
			{
				const float r = (field.passwordMatchState > 0) ? 0.3f : 0.85f;
				const float g = (field.passwordMatchState > 0) ? 0.72f : 0.25f;
				const float b = (field.passwordMatchState > 0) ? 0.42f : 0.25f;
				addRect(fieldX, y + kAuthUiFieldBoxHeightPx - 2, fieldW, 2, r, g, b, 1.0f);
			}
```

- [ ] **Step 5 : Compiler**

```bash
cmake --build . --target engine_app --config Release 2>&1 | tail -20
```

- [ ] **Step 6 : Commit**

```bash
git add engine/render/AuthUiRenderer.h engine/render/AuthUiRenderer.cpp
git commit -m "feat(render): rendu grille 3 colonnes pour formulaire inscription + indicateur correspondance mdp"
```

---

### Task 5 : Rendu texte grille dans AuthGlyphPass

**Files:**
- Modify: `engine/render/AuthGlyphPass.cpp`

- [ ] **Step 1 : Mettre à jour `RecordModel` pour positionner le texte en grille**

Dans `engine/render/AuthGlyphPass.cpp`, dans la boucle champs de `RecordModel` (vers la ligne 1474), remplacer le calcul de position X par :

```cpp
		// Pré-calcul lignes logiques (même logique que AuthUiRenderer)
		const int32_t fieldCount = static_cast<int32_t>(model.fields.size());
		std::vector<int32_t> fieldLogicalRow(static_cast<size_t>(fieldCount), 0);
		{
			int32_t row = -1;
			int32_t lastCol = kAuthUiGridColumns;
			for (int32_t i = 0; i < fieldCount; ++i)
			{
				const auto& f = model.fields[static_cast<size_t>(i)];
				if (f.gridColumn < 0 || f.gridColumn <= lastCol)
					++row;
				lastCol = (f.gridColumn < 0) ? kAuthUiGridColumns : f.gridColumn;
				fieldLogicalRow[static_cast<size_t>(i)] = row;
			}
		}

		const int32_t labelAboveFieldPx = smallScale * 11 + 6;
		const int32_t valueBelowTopPx = 12;
		for (int32_t i = 0; i < fieldCount; ++i)
		{
			const auto& field = model.fields[static_cast<size_t>(i)];
			const int32_t y = panelY + topOffset + fieldLogicalRow[static_cast<size_t>(i)] * fieldRowStep;

			int32_t fx = contentX;
			int32_t fw = contentW;
			if (field.gridColumn >= 0)
			{
				AuthUiGridFieldGeometry(contentX, contentW, field.gridColumn, field.gridSpan, fx, fw);
			}

			AppendText(vertices, field.label, fx + 10, y - labelAboveFieldPx, fw / 2, smallScale, mutedColor);
			const float* valueTint = field.active ? titleColor : (field.hovered ? primaryColor : bodyColor);
			AppendText(vertices, field.value, fx + 12, y + valueBelowTopPx, fw - 24, bodyScale, valueTint, /*useValueFont=*/true);
```

Note : `AppendText` reçoit un nouveau paramètre `useValueFont` (à ajouter dans Task 6).

- [ ] **Step 2 : Compiler (attendu : erreur sur useValueFont non défini — normal, Task 6 le résout)**

```bash
cmake --build . --target engine_app --config Release 2>&1 | tail -5
```

---

### Task 6 : Second atlas TTF (Morpheus) dans AuthGlyphPass

**Files:**
- Modify: `engine/render/AuthGlyphPass.h`
- Modify: `engine/render/AuthGlyphPass.cpp`
- Modify: `engine/Engine.cpp`
- Modify: `config.json`

- [ ] **Step 1 : Ajouter les membres du second atlas dans `AuthGlyphPass.h`**

Dans `engine/render/AuthGlyphPass.h`, après `m_fontGpuReady` (vers la ligne 95), ajouter :

```cpp
		/// Second atlas TTF (police valeurs de champs — ex. Morpheus.ttf).
		FontAtlasTtf m_valueFont{};
		bool m_valueFontGpuReady = false;
		VkDescriptorSetLayout m_valueFontDescriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool      m_valueFontDescriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet       m_valueFontDescriptorSet = VK_NULL_HANDLE;
		VkSampler             m_valueFontSampler = VK_NULL_HANDLE;
		VkPipelineLayout      m_valueFontPipelineLayout = VK_NULL_HANDLE;
		VkPipeline            m_valueFontPipeline = VK_NULL_HANDLE;
		VkImage               m_valueFontImage = VK_NULL_HANDLE;
		VkDeviceMemory        m_valueFontImageMemory = VK_NULL_HANDLE;
		VkImageView           m_valueFontImageView = VK_NULL_HANDLE;
```

Dans la section public, ajouter :

```cpp
		/// Charge la police valeurs (Morpheus.ttf). Nécessite Init(..., fragTtf).
		bool UploadValueFontFromTtf(VkDevice device, VkPhysicalDevice physicalDevice,
			VkQueue graphicsQueue, uint32_t queueFamilyIndex,
			const uint8_t* ttfBytes, size_t ttfSize, float pixelHeight);

		bool HasValueFont() const { return m_valueFontGpuReady; }
```

Mettre à jour la signature de `AppendText` (version privée) :

```cpp
		void AppendText(std::vector<GlyphVertex>& vertices,
			std::string_view text,
			int32_t originX, int32_t originY,
			int32_t maxWidthPx,
			int32_t scale,
			const float color[4],
			bool useValueFont = false) const;
```

- [ ] **Step 2 : Implémenter `UploadValueFontFromTtf` dans `AuthGlyphPass.cpp`**

Ajouter après `UploadUiFontFromTtf` (vers la ligne 991) :

```cpp
	bool AuthGlyphPass::UploadValueFontFromTtf(VkDevice device, VkPhysicalDevice physicalDevice,
		VkQueue graphicsQueue, uint32_t queueFamilyIndex,
		const uint8_t* ttfBytes, size_t ttfSize, float pixelHeight)
	{
		if (!ttfBytes || ttfSize == 0 || m_fontPipeline == VK_NULL_HANDLE)
			return false;
		// Même logique que UploadUiFontFromTtf mais pour m_valueFont.
		// Réutiliser les mêmes shaders SPIR-V (m_fontPipeline utilise auth_ttf.frag.spv).
		// Copier la logique d'atlas, upload GPU, descripteur, pipeline depuis UploadUiFontFromTtf
		// en remplaçant m_uiFont → m_valueFont, m_fontImage → m_valueFontImage, etc.
		// Le pipeline Vulkan peut être le même (mêmes shaders) ; seul l'atlas change.
		// NOTE : pour simplifier, appeler CreateFontPipeline en réutilisant les shaders
		// déjà compilés dans m_fontPipeline (récupérer les paramètres SPIR-V stockés ou
		// appeler Init avec les mêmes shaders).
		// Implémentation recommandée : refactoriser UploadUiFontFromTtf en méthode générique
		// UploadFontAtlas(FontAtlasTtf&, VkImage&, ...) appelée par les deux méthodes publiques.
		m_valueFontGpuReady = false;
		if (!m_valueFont.Build(ttfBytes, ttfSize, pixelHeight))
		{
			LOG_WARN(Render, "[AuthGlyphPass] Echec construction atlas TTF valeur");
			return false;
		}
		// Upload GPU identique à UploadUiFontFromTtf — voir implémentation de référence
		// dans la même fonction (lignes ~760-990).
		// Après upload, créer descripteur et pipeline dédiés pour m_valueFont.
		m_valueFontGpuReady = true;
		LOG_INFO(Render, "[AuthGlyphPass] Police valeur TTF upload OK");
		return true;
	}
```

Note : pour éviter la duplication, refactoriser `UploadUiFontFromTtf` en extrayant la logique GPU dans une méthode privée `UploadFontAtlasToGpu(FontAtlasTtf&, VkImage&, VkDeviceMemory&, VkImageView&, VkSampler&, VkDescriptorPool&, VkDescriptorSetLayout&, VkDescriptorSet&, ...)` réutilisable par les deux méthodes publiques.

- [ ] **Step 3 : Mettre à jour `AppendText` pour router vers la police valeur**

Dans `AuthGlyphPass.cpp`, fonction `AppendText` (vers la ligne 1078) :

```cpp
	void AuthGlyphPass::AppendText(std::vector<GlyphVertex>& vertices,
		std::string_view text, int32_t originX, int32_t originY,
		int32_t maxWidthPx, int32_t scale, const float color[4], bool useValueFont) const
	{
		if (text.empty()) return;
		if (useValueFont && m_valueFontGpuReady && m_valueFont.IsValid())
		{
			AppendTextWithFont(vertices, text, originX, originY, maxWidthPx, scale, color, m_valueFont);
			return;
		}
		if (m_fontGpuReady && m_uiFont.IsValid())
		{
			AppendTextWithFont(vertices, text, originX, originY, maxWidthPx, scale, color, m_uiFont);
			return;
		}
		// Fallback bitmap
		AppendTextBitmap(vertices, text, originX, originY, maxWidthPx, scale, color);
	}
```

Note : extraire `AppendTextTtf` en `AppendTextWithFont(vertices, text, ..., const FontAtlasTtf& font)` pour éviter la duplication.

- [ ] **Step 4 : Charger Morpheus dans `Engine.cpp`**

Après le bloc de chargement de Windlass.ttf (vers la ligne 1159), ajouter :

```cpp
							const std::string valueFontPath = m_cfg.GetString("render.auth_ui.value_font_path", "");
							if (!valueFontPath.empty() && ttfFragPtr != nullptr)
							{
								std::vector<uint8_t> valueFontBytes = engine::platform::FileSystem::ReadAllBytesContent(m_cfg, valueFontPath);
								if (!valueFontBytes.empty())
								{
									const float valueFontPx = static_cast<float>(std::clamp<int64_t>(
										m_cfg.GetInt("render.auth_ui.value_font_pixel_height", 24), 12, 96));
									if (m_authGlyphPass.UploadValueFontFromTtf(
											m_vkDeviceContext.GetDevice(),
											m_vkDeviceContext.GetPhysicalDevice(),
											m_vkDeviceContext.GetGraphicsQueue(),
											m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
											valueFontBytes.data(), valueFontBytes.size(), valueFontPx))
									{
										LOG_INFO(Render, "[Boot] Auth UI value font loaded: {}", valueFontPath);
									}
									else
									{
										LOG_WARN(Render, "[Boot] Auth UI value font upload failed: {}", valueFontPath);
									}
								}
							}
```

- [ ] **Step 5 : Mettre à jour `config.json`**

Dans la section `render.auth_ui`, après `"font_pixel_height": 28` :

```json
            "value_font_path": "fonts/Morpheus.ttf",
            "value_font_pixel_height": 24
```

Vérifier que le fichier `game/data/fonts/Morpheus.ttf` est présent.

- [ ] **Step 6 : Mettre à jour `Destroy` pour libérer les ressources du second atlas**

Dans `AuthGlyphPass::Destroy` (vers la ligne 1652), ajouter les destructions des membres `m_valueFont*` selon le même pattern que les membres `m_font*`.

- [ ] **Step 7 : Compiler**

```bash
cmake --build . --target engine_app --config Release 2>&1 | tail -20
```

Résultat attendu : zéro erreur.

- [ ] **Step 8 : Test visuel**

Lancer le jeu. Sur la page d'inscription, les valeurs saisies dans les champs doivent s'afficher avec Morpheus.ttf et les libellés avec Windlass.ttf. La différence visuelle doit être claire.

- [ ] **Step 9 : Commit**

```bash
git add engine/render/AuthGlyphPass.h engine/render/AuthGlyphPass.cpp engine/Engine.cpp config.json
git commit -m "feat(render): second atlas TTF Morpheus pour valeurs de champs inscription"
```

---

### Task 7 : Vérification visuelle finale et nettoyage

- [ ] **Step 1 : Tester le flux complet d'inscription**

1. Lancer le jeu
2. Cliquer sur "Inscription"
3. Vérifier la grille 3 colonnes (login + pays en ligne 0, nom + prénom en ligne 1, etc.)
4. Saisir deux mots de passe identiques → barre verte sous "Confirmation"
5. Saisir deux mots de passe différents → barre rouge
6. Naviguer entre les mois avec les flèches → noms de mois affichés ("Janvier", "Février"…)
7. Cycler le pays avec les flèches → codes pays + noms affichés
8. Soumettre sans remplir tous les champs → message d'erreur, aucun champ effacé

- [ ] **Step 2 : Commit final si ajustements mineurs**

```bash
git add -p  # ajouter uniquement les corrections visuelles mineures
git commit -m "fix(register): ajustements visuels grille inscription apres test"
```
