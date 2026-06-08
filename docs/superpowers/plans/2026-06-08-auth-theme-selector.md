# Sélecteur de thème dans les Options auth — Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use `- [ ]`.

**Goal:** Permettre de changer le thème UI **avant d'entrer en jeu**, depuis l'onglet « Interface » de l'écran Options d'authentification (complète le sous-projet 1, où le sélecteur n'existait qu'en in-game).

**Architecture :** Un combo « Thème de l'interface » dans `AuthImGuiOptions.cpp` (onglet `m_optionsTab == 4`). Application **live** (`LnTheme::SetActive`) + persistance `ui_theme.json` — identique au sélecteur in-game, auto-contenu (hors du modèle staged Appliquer/Annuler du panneau, car un thème se prévisualise en direct). Libellés i18n (fr/en).

**Tech Stack :** Dear ImGui, `LnTheme` runtime, localisation JSON. Client/Windows uniquement (`AuthImGuiOptions.cpp` est sous `#if defined(_WIN32)`).

**Déploiement :** ✅ client uniquement, pas de redéploiement serveur.

---

## Task 1 : Combo de thème dans l'onglet Interface des Options auth

**Files:**
- Modify: `src/client/render/auth/screens/AuthImGuiOptions.cpp` (bloc `m_optionsTab == 4`)
- Modify: `game/data/localization/fr/fr.json` (3 clés)
- Modify: `game/data/localization/en/en.json` (3 clés)

- [ ] **Step 1 : Clés de localisation (fr)**

Dans `game/data/localization/fr/fr.json`, après la ligne
`"options.interface.tooltips_hint": "...",` (vers la ligne 431), ajouter (sans
accents, comme les clés voisines) :

```json
  "options.interface.theme": "Theme de l'interface",
  "options.interface.theme.or_royal": "Or royal",
  "options.interface.theme.sylve_emeraude": "Sylve emeraude",
```

- [ ] **Step 2 : Clés de localisation (en)**

Dans `game/data/localization/en/en.json`, après
`"options.interface.tooltips_hint": "...",` (vers la ligne 431), ajouter :

```json
  "options.interface.theme": "Interface theme",
  "options.interface.theme.or_royal": "Royal Gold",
  "options.interface.theme.sylve_emeraude": "Emerald Sylvan",
```

Vérifier que la virgule JSON précédente/suivante laisse le fichier valide.

- [ ] **Step 3 : Include FileSystem (si absent)**

En tête de `src/client/render/auth/screens/AuthImGuiOptions.cpp`, s'assurer que
`#include "src/shared/platform/FileSystem.h"` est présent (l'ajouter avec les
autres includes `src/...` sinon). `LnTheme.h` est déjà inclus.

- [ ] **Step 4 : Insérer le combo dans l'onglet Interface**

Dans le bloc `else if (m_optionsTab == 4)`, juste après la ligne
`toggleRow("options.interface.tooltips", &m_optShowTooltipsUi, "options.interface.tooltips_hint");`
et avant la `}` qui ferme le bloc, insérer (tabs) :

```cpp
			// Thème de l'interface : application live (aperçu immédiat sur l'écran
			// auth) + persistance ui_theme.json, comme le sélecteur in-game. Hors du
			// modèle staged (Appliquer/Annuler) : un thème se prévisualise en direct.
			{
				auto themeLabel = [&](std::string_view id) -> std::string {
					if (id == "or_royal") return tr("options.interface.theme.or_royal");
					if (id == "sylve_emeraude") return tr("options.interface.theme.sylve_emeraude");
					return std::string(id);
				};
				const std::string_view curTheme = LnTheme::ActiveName();
				if (ImGui::BeginCombo(tr("options.interface.theme").c_str(), themeLabel(curTheme).c_str()))
				{
					for (std::string_view id : LnTheme::Names())
					{
						const bool selected = (id == curTheme);
						if (ImGui::Selectable(themeLabel(id).c_str(), selected) && !selected)
						{
							LnTheme::SetActive(id);
							const std::string js =
								std::string("{\n  \"ui\": { \"theme\": \"")
								+ std::string(LnTheme::ActiveName()) + "\" }\n}\n";
							if (!engine::platform::FileSystem::WriteAllText("ui_theme.json", js))
							{
								// Persistance best-effort ; l'aperçu live reste appliqué même si l'écriture échoue.
							}
						}
						if (selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			}
```

Note : si une macro de log (`LOG_WARN`) est déjà disponible/incluse dans ce TU,
remplacer le commentaire d'échec d'écriture par
`LOG_WARN(Core, "[AuthOptions] ui_theme.json non ecrit (theme non persiste)");`.
Sinon laisser le commentaire (ne pas ajouter d'include de log juste pour ça).

- [ ] **Step 5 : Build (CI uniquement)**

Pas de toolchain locale → vérification CI (`build-windows` compile ce fichier ;
`build-linux` l'ignore car `#if defined(_WIN32)`).

- [ ] **Step 6 : Commit**

```bash
git add src/client/render/auth/screens/AuthImGuiOptions.cpp game/data/localization/fr/fr.json game/data/localization/en/en.json
git commit -m "feat(ui): selecteur de theme dans les Options auth (onglet Interface)"
```
Terminer le corps par :
Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>

---

## Self-review (rédaction)

- Couverture : combo thème dans Options auth ✓ ; live apply + persist ✓ ;
  libellés i18n fr/en ✓ ; même clé `ui.theme`/fichier que le boot et l'in-game ✓.
- Cohérence : `BeginCombo`/`EndCombo` équilibrés ; `themeLabel` fallback = id brut ;
  pas de toucher au pipeline staged.
- Risque : JSON localisation — bien gérer les virgules. Combo Windows-only (OK,
  l'auth UI l'est déjà).
