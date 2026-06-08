# Fondation theming runtime de l'UI — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rendre la palette `LnTheme` commutable à l'exécution avec deux thèmes (`or_royal`, `sylve_emeraude`), un sélecteur dans les Options in-game, et une persistance locale — sans modifier les ~249 sites qui lisent déjà `LnTheme::kAccent` & co.

**Architecture :** `LnTheme.h` devient un registre de palettes header-only. L'état du thème actif est un `Palette` statique muté **en place** ; les anciennes constantes `kXxx` deviennent des **références vivantes** vers les membres de ce `Palette`, donc tous les call sites existants suivent automatiquement le thème courant sans aucune édition. Le sélecteur appelle `SetActive()` (aperçu live) et écrit un fichier dédié `ui_theme.json`, relu au boot.

**Tech Stack :** C++17 (inline variables, function-local statics), Dear ImGui (combo), CMake/ctest (test header-only sous Linux).

---

## Refinement vs spec

Le spec ([2026-06-08-theming-runtime-ui-design.md](../specs/2026-06-08-theming-runtime-ui-design.md))
envisageait de **supprimer** les constantes `kXxx` et de migrer explicitement les
249 call sites vers `Active().xxx`. Ce plan adopte une approche équivalente en
résultat mais **sans toucher les call sites** : les `kXxx` sont conservés comme
**références `const Rgba&` vers le `Palette` actif muté en place**. Conséquence :
toutes les couleurs suivent le thème (objectif tenu), zéro régression mécanique
sur 30 fichiers, diff minimal. C'est le seul écart au spec.

Le sélecteur dans les **Options auth** (marqué « idéalement » au spec) est
**différé** (voir « Extension différée » en fin de plan) : le thème s'applique
déjà aux écrans auth puisqu'ils lisent `LnTheme`, seul le *contrôle* de sélection
sur l'écran de login attend un second passage.

---

## Structure des fichiers

- **Modifier** `src/client/render/LnTheme.h` — `Palette`, registre, `Active()` /
  `SetActive()` / `Names()` / `ActiveName()`, alias-références `kXxx`, helpers
  dérivés (`PanelBg`/`AccentDim`/`BorderActive`) basés sur le thème actif.
- **Créer** `src/client/render/tests/LnThemeTests.cpp` — tests unitaires
  header-only (pas d'ImGui, pas d'`assert`).
- **Modifier** `src/CMakeLists.txt` — cible de test `ln_theme_tests`.
- **Modifier** `src/client/app/Engine.cpp` — (a) include `LnTheme.h` + chargement
  `ui_theme.json` au boot ; (b) combo « Thème de l'interface » dans le panneau
  Options in-game + écriture `ui_theme.json`.

---

## Task 1 : LnTheme runtime + tests

**Files:**
- Create: `src/client/render/tests/LnThemeTests.cpp`
- Modify: `src/client/render/LnTheme.h` (remplacement intégral)
- Modify: `src/CMakeLists.txt` (ajout cible test)

- [ ] **Step 1 : Écrire le test (échoue à la compilation — API absente)**

Créer `src/client/render/tests/LnThemeTests.cpp` :

```cpp
// Tests de la fondation theming runtime (sous-projet 1).
// Header-only, sans ImGui ni assert (NDEBUG strip les assert en CI Release).
#include "src/client/render/LnTheme.h"

#include <cmath>
#include <cstdio>
#include <string_view>
#include <vector>

namespace
{
    int g_failures = 0;

    void Expect(bool cond, const char* what)
    {
        if (!cond)
        {
            std::printf("[FAIL] %s\n", what);
            ++g_failures;
        }
    }

    bool Near(float a, float b) { return std::fabs(a - b) < 0.002f; }
}

int main()
{
    using namespace LnTheme;

    // 1. Le registre expose les deux thèmes attendus.
    const std::vector<std::string_view> names = Names();
    bool hasOr = false, hasSylve = false;
    for (std::string_view n : names)
    {
        if (n == "or_royal") hasOr = true;
        if (n == "sylve_emeraude") hasSylve = true;
    }
    Expect(hasOr, "Names() contient or_royal");
    Expect(hasSylve, "Names() contient sylve_emeraude");

    // 2. Défaut = or_royal, accent doré ~ #E8C56E.
    Expect(SetActive("or_royal"), "SetActive(or_royal) renvoie true");
    Expect(ActiveName() == "or_royal", "ActiveName == or_royal");
    Expect(Near(Active().accent.r, 0.910f) && Near(Active().accent.g, 0.773f)
        && Near(Active().accent.b, 0.431f), "or_royal accent dore");

    // 3. Bascule vers sylve_emeraude : l'accent change.
    Expect(SetActive("sylve_emeraude"), "SetActive(sylve) renvoie true");
    Expect(ActiveName() == "sylve_emeraude", "ActiveName == sylve_emeraude");
    Expect(!(Near(Active().accent.r, 0.910f) && Near(Active().accent.g, 0.773f)),
        "sylve accent != or_royal accent");

    // 4. Les alias-références suivent le thème actif (point clé du refactor).
    Expect(Near(kAccent.r, Active().accent.r) && Near(kAccent.g, Active().accent.g)
        && Near(kAccent.b, Active().accent.b), "kAccent suit Active() apres SetActive");

    // 5. Nom inconnu : pas de changement, renvoie false.
    Expect(!SetActive("inconnu"), "SetActive(inconnu) renvoie false");
    Expect(ActiveName() == "sylve_emeraude", "theme inchange apres nom invalide");

    // 6. Invariants palette : danger reste un rouge distinct de l'accent, alpha=1.
    for (std::string_view n : names)
    {
        SetActive(n);
        const Palette& p = Active();
        Expect(p.accent.a == 1.f && p.errorCol.a == 1.f, "alpha opaque accent/error");
        const bool distinct = std::fabs(p.errorCol.r - p.accent.r) > 0.1f
            || std::fabs(p.errorCol.g - p.accent.g) > 0.1f
            || std::fabs(p.errorCol.b - p.accent.b) > 0.1f;
        Expect(distinct, "errorCol distinct de accent");
    }

    if (g_failures == 0) std::printf("[OK] LnThemeTests\n");
    return g_failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2 : Ajouter la cible de test à `src/CMakeLists.txt`**

Insérer près des autres `add_executable` de tests (ex. après le bloc
`world_clock_tests` autour de la ligne 591), **dans le même bloc conditionnel de
tests** :

```cmake
  # Theming runtime (sous-projet 1) : registre de palettes LnTheme (header-only).
  # engine_core fournit l'include path ${CMAKE_SOURCE_DIR} pour resoudre "src/...".
  add_executable(ln_theme_tests
    ${CMAKE_SOURCE_DIR}/src/client/render/tests/LnThemeTests.cpp)
  target_link_libraries(ln_theme_tests PRIVATE engine_core)
  add_test(NAME ln_theme_tests COMMAND ln_theme_tests)
```

- [ ] **Step 3 : Vérifier que ça échoue (API absente)**

La compilation de `ln_theme_tests` doit échouer : `Names`, `SetActive`,
`Active`, `Palette`, `ActiveName` n'existent pas encore dans `LnTheme.h`.
Attendu : erreurs `is not a member of 'LnTheme'`.

- [ ] **Step 4 : Réécrire `src/client/render/LnTheme.h`**

Remplacer **tout** le contenu du fichier par :

```cpp
#pragma once

#include <array>
#include <string_view>
#include <vector>

namespace LnTheme
{
	/// Couleur RGBA 0–1, sans dépendance Dear ImGui (utilisable partout).
	struct Rgba
	{
		float r;
		float g;
		float b;
		float a;
	};

	/// Palette complète d'un thème : les 12 rôles de couleur de l'UI.
	struct Palette
	{
		Rgba primary;
		Rgba secondary;
		Rgba accent;
		Rgba background;
		Rgba surface;
		Rgba panel;
		Rgba text;
		Rgba muted;
		Rgba border;
		Rgba success;
		Rgba warning;
		Rgba errorCol;
	};

	namespace detail
	{
		// --- Définition des thèmes (data-driven : ajouter un thème = +1 entrée). ---

		// or_royal : palette historique (dérivée de colors_and_type.css — spec auth).
		inline constexpr Palette kOrRoyal{
			/*primary   */ {0.290f, 0.482f, 0.722f, 1.f}, // #4A7BB8
			/*secondary */ {0.361f, 0.420f, 0.549f, 1.f}, // #5C6B8C
			/*accent    */ {0.910f, 0.773f, 0.431f, 1.f}, // #E8C56E
			/*background */ {0.039f, 0.051f, 0.071f, 1.f}, // #0A0D12
			/*surface   */ {0.071f, 0.094f, 0.133f, 1.f}, // #121822
			/*panel     */ {0.078f, 0.110f, 0.157f, 1.f}, // #141C28
			/*text      */ {0.949f, 0.957f, 0.973f, 1.f}, // #F2F4F8
			/*muted     */ {0.608f, 0.659f, 0.722f, 1.f}, // #9BA8B8
			/*border    */ {0.239f, 0.310f, 0.400f, 1.f}, // #3D4F66
			/*success   */ {0.373f, 0.722f, 0.431f, 1.f}, // #5FB86E
			/*warning   */ {0.910f, 0.647f, 0.361f, 1.f}, // #E8A55C
			/*errorCol  */ {0.769f, 0.251f, 0.251f, 1.f}, // #C44040
		};

		// sylve_emeraude : ambiance nature/elfique. Accent vert-or pâle, surfaces
		// vert-gris sombre. text/muted restent lisibles ; success/warning/errorCol
		// conservés sémantiquement (le danger reste un rouge distinct de l'accent).
		inline constexpr Palette kSylveEmeraude{
			/*primary   */ {0.180f, 0.431f, 0.310f, 1.f}, // #2E6E4F
			/*secondary */ {0.290f, 0.420f, 0.341f, 1.f}, // #4A6B57
			/*accent    */ {0.796f, 0.851f, 0.561f, 1.f}, // #CBD98F
			/*background */ {0.031f, 0.067f, 0.047f, 1.f}, // #08110C
			/*surface   */ {0.067f, 0.110f, 0.086f, 1.f}, // #111C16
			/*panel     */ {0.086f, 0.141f, 0.106f, 1.f}, // #16241B
			/*text      */ {0.937f, 0.957f, 0.925f, 1.f}, // #EFF4EC
			/*muted     */ {0.576f, 0.659f, 0.608f, 1.f}, // #93A89B
			/*border    */ {0.239f, 0.373f, 0.286f, 1.f}, // #3D5F49
			/*success   */ {0.373f, 0.722f, 0.431f, 1.f}, // #5FB86E
			/*warning   */ {0.910f, 0.647f, 0.361f, 1.f}, // #E8A55C
			/*errorCol  */ {0.769f, 0.251f, 0.251f, 1.f}, // #C44040
		};

		/// Une entrée du registre : nom interne (clé config) + palette.
		struct Entry
		{
			std::string_view name;
			Palette palette;
		};

		/// Registre ordonné des thèmes disponibles (ordre d'affichage UI).
		inline const std::array<Entry, 2>& Registry()
		{
			static const std::array<Entry, 2> kRegistry{{
				{"or_royal", kOrRoyal},
				{"sylve_emeraude", kSylveEmeraude},
			}};
			return kRegistry;
		}

		/// État du thème actif : copie MUTÉE EN PLACE par SetActive. Les alias
		/// kXxx référencent les membres de CETTE instance ; muter en place (et non
		/// rebinder) garde les références valides et à jour. Initialisé via Meyers
		/// singleton, donc construit avant tout usage (pas de SIOF : tous les call
		/// sites lisent à l'exécution, jamais en initialisation statique).
		inline Palette& ActiveStorage()
		{
			static Palette s = kOrRoyal;
			return s;
		}

		/// Nom du thème actif (string_view vers un littéral du registre).
		inline std::string_view& ActiveNameStorage()
		{
			static std::string_view n = "or_royal";
			return n;
		}
	} // namespace detail

	/// Palette du thème actuellement actif (jamais nulle ; défaut or_royal).
	inline const Palette& Active()
	{
		return detail::ActiveStorage();
	}

	/// Nom interne du thème actif (ex. "or_royal") — pour persistance et UI.
	inline std::string_view ActiveName()
	{
		return detail::ActiveNameStorage();
	}

	/// Noms des thèmes disponibles, dans l'ordre d'affichage.
	inline std::vector<std::string_view> Names()
	{
		std::vector<std::string_view> out;
		out.reserve(detail::Registry().size());
		for (const auto& e : detail::Registry())
		{
			out.push_back(e.name);
		}
		return out;
	}

	/// Bascule le thème actif. Renvoie false (et ne change rien) si name inconnu.
	/// Effet de bord : recolore tout l'UI à la frame suivante (via les alias kXxx).
	inline bool SetActive(std::string_view name)
	{
		for (const auto& e : detail::Registry())
		{
			if (e.name == name)
			{
				detail::ActiveStorage() = e.palette; // copie EN PLACE -> alias suivent
				detail::ActiveNameStorage() = e.name;
				return true;
			}
		}
		return false;
	}

	// --- Alias de compatibilité : références VIVANTES vers le thème actif. ---
	// Conservent la syntaxe LnTheme::kAccent sur ~249 call sites existants, mais
	// reflètent désormais le thème courant (références vers les membres de
	// ActiveStorage(), muté en place par SetActive). Aucune édition de call site.
	inline const Rgba& kPrimary = detail::ActiveStorage().primary;
	inline const Rgba& kSecondary = detail::ActiveStorage().secondary;
	inline const Rgba& kAccent = detail::ActiveStorage().accent;
	inline const Rgba& kBackground = detail::ActiveStorage().background;
	inline const Rgba& kSurface = detail::ActiveStorage().surface;
	inline const Rgba& kPanel = detail::ActiveStorage().panel;
	inline const Rgba& kText = detail::ActiveStorage().text;
	inline const Rgba& kMuted = detail::ActiveStorage().muted;
	inline const Rgba& kBorder = detail::ActiveStorage().border;
	inline const Rgba& kSuccess = detail::ActiveStorage().success;
	inline const Rgba& kWarning = detail::ActiveStorage().warning;
	inline const Rgba& kErrorCol = detail::ActiveStorage().errorCol;

	/// Fond de panneau semi-transparent dérivé du panel du thème actif.
	inline Rgba PanelBg(float alpha = 0.72f)
	{
		const Palette& p = Active();
		return Rgba{p.panel.r, p.panel.g, p.panel.b, alpha};
	}

	/// Variante atténuée de l'accent du thème actif (overlays de survol).
	inline Rgba AccentDim(float alpha = 0.10f)
	{
		const Palette& p = Active();
		return Rgba{p.accent.r, p.accent.g, p.accent.b, alpha};
	}

	/// Couleur de bordure « active » : l'accent du thème courant.
	inline Rgba BorderActive()
	{
		return Active().accent;
	}
} // namespace LnTheme
```

- [ ] **Step 5 : Compiler et lancer le test**

Run (CI Linux / ctest) : `ctest -R ln_theme_tests --output-on-failure`
Expected : `[OK] LnThemeTests`, test PASS.

- [ ] **Step 6 : Vérifier la non-régression des consommateurs (build complet)**

La compilation de la cible client doit rester verte : les ~249 `LnTheme::kXxx`
et les 73 `PanelBg`/`AccentDim`/`BorderActive` continuent de compiler sans
édition (les premiers via références, les seconds via corps mis à jour).
Si un site utilisait un `kXxx` en contexte `constexpr` (improbable), le corriger
ponctuellement — sinon ne rien toucher.

- [ ] **Step 7 : Commit**

```bash
git add src/client/render/LnTheme.h src/client/render/tests/LnThemeTests.cpp src/CMakeLists.txt
git commit -m "feat(ui): LnTheme runtime — registre de palettes commutable (or_royal + sylve_emeraude)"
```

---

## Task 2 : Chargement du thème au boot

**Files:**
- Modify: `src/client/app/Engine.cpp` (include + bloc boot ~ligne 798)

- [ ] **Step 1 : S'assurer que `LnTheme.h` est inclus dans Engine.cpp**

En tête de `src/client/app/Engine.cpp`, ajouter (s'il n'y est pas déjà) :

```cpp
#include "src/client/render/LnTheme.h"
```

- [ ] **Step 2 : Charger `ui_theme.json` et appliquer le thème**

Dans le bloc de boot, juste après le chargement de `keybinds.json`
(autour de la ligne 798, après le `if (cfg.LoadFromFile("keybinds.json"))`),
ajouter :

```cpp
			// ui_theme.json : préférence de thème UI (fichier dédié écrit par le
			// panneau Options, comme keybinds.json). Merge dans cfg puis applique.
			if (cfg.LoadFromFile("ui_theme.json"))
				LOG_INFO(Core, "[Boot] ui_theme.json applique");
			// Applique le thème lu ; défaut or_royal si absent ou nom invalide
			// (SetActive renvoie false et conserve or_royal dans ce cas).
			if (!LnTheme::SetActive(cfg.GetString("ui.theme", "or_royal")))
				LOG_WARN(Core, "[Boot] theme '{}' inconnu -> or_royal", cfg.GetString("ui.theme", "or_royal"));
```

- [ ] **Step 3 : Vérifier le build**

La cible client compile. Au lancement, sans `ui_theme.json`, le thème reste
`or_royal` (aucun warning). Aucun test automatisé ici (chemin d'I/O boot).

- [ ] **Step 4 : Commit**

```bash
git add src/client/app/Engine.cpp
git commit -m "feat(ui): applique le theme UI persiste (ui_theme.json) au boot"
```

---

## Task 3 : Sélecteur de thème dans les Options in-game

**Files:**
- Modify: `src/client/app/Engine.cpp` (panneau Options, ~ligne 10261)

- [ ] **Step 1 : Ajouter le combo « Thème de l'interface »**

Dans le bloc `if (m_inGameOptionsPanelVisible)`, juste après le slider
« Sensibilite souris » (autour de la ligne 10261) et **avant** le
`ImGui::Spacing(); ImGui::Separator(); ImGui::TextUnformatted("Controles");`,
insérer :

```cpp
					// --- Thème de l'interface (recolore tout l'UI in-game) ---
					// Libellés ASCII volontaires : la police in-game (Windlass) n'a
					// pas tous les glyphes accentués (cf. "Se deconnecter" du menu pause).
					auto prettyTheme = [](std::string_view n) -> const char* {
						if (n == "or_royal") return "Or royal";
						if (n == "sylve_emeraude") return "Sylve emeraude";
						return "?";
					};
					const std::string_view curTheme = LnTheme::ActiveName();
					if (ImGui::BeginCombo("Theme de l'interface", prettyTheme(curTheme)))
					{
						for (std::string_view n : LnTheme::Names())
						{
							const bool selected = (n == curTheme);
							if (ImGui::Selectable(prettyTheme(n), selected) && !selected)
							{
								LnTheme::SetActive(n);
								// Persistance : fichier dédié mergé au boot (comme keybinds.json).
								const std::string js =
									std::string("{\n  \"ui\": { \"theme\": \"")
									+ std::string(LnTheme::ActiveName()) + "\" }\n}\n";
								if (!engine::platform::FileSystem::WriteAllText("ui_theme.json", js))
									LOG_WARN(Core, "[Options] ui_theme.json non ecrit (theme non persiste)");
							}
							if (selected)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
```

- [ ] **Step 2 : Vérifier le build**

La cible client compile (`engine::platform::FileSystem::WriteAllText` et
`LnTheme::*` déjà disponibles dans ce TU — cf. usages keybinds.json et Task 2).

- [ ] **Step 3 : Validation manuelle (en jeu)**

Lancer le client, ouvrir Pause → Options. Le combo « Theme de l'interface »
liste « Or royal » et « Sylve emeraude ». Sélectionner Sylve émeraude : l'UI
(panneau Options, chat, HUD, écrans) se recolore immédiatement en vert. Fermer
et relancer le client : le thème Sylve est conservé (`ui_theme.json` présent).

- [ ] **Step 4 : Commit**

```bash
git add src/client/app/Engine.cpp
git commit -m "feat(ui): selecteur de theme dans les Options in-game + persistance"
```

---

## Extension différée (hors de ce plan)

- **Sélecteur dans les Options auth** (`AuthImGuiOptions.cpp`, onglet `tab.ui`) —
  le spec le note « idéalement ». Différé : la structure de l'overlay Options
  auth (modèle `RenderModel`, callbacks presenter, sémantique Appliquer/Annuler,
  traductions) demande un passage dédié. Le thème **s'applique déjà** aux écrans
  auth (ils lisent `LnTheme`) ; seul le *contrôle* de sélection sur l'écran de
  login attend. Petit follow-up une fois la structure de l'onglet UI relue.
- **Thèmes Azur arcane / Sang & pourpre** — ajout d'une entrée dans
  `detail::Registry()` + le libellé dans `prettyTheme` (et `std::array<Entry, N>`
  redimensionné). Trivial, data-driven.

---

## Déploiement

> **Déploiement** : ✅ client uniquement, pas de redéploiement serveur — thème =
> préférence locale (`ui_theme.json`), aucun opcode, handler ni migration DB.

---

## Self-review (rédaction)

- **Couverture spec** : modèle `Palette` ✓ (Task 1), registre runtime + API ✓
  (Task 1), set initial or_royal + sylve_emeraude ✓ (Task 1), migration des
  consommateurs ✓ (Task 1, via références — sans édition), persistance locale
  `ui.theme` ✓ (Task 2 boot + Task 3 écriture), sélecteur in-game ✓ (Task 3),
  tests ✓ (Task 1), note déploiement client-only ✓. Sélecteur auth = différé
  (assumé, voir ci-dessus).
- **Placeholders** : aucun — chaque step a son code/commande complet.
- **Cohérence des types** : `Palette`, `Rgba`, `Active()`, `SetActive()`,
  `ActiveName()`, `Names()` utilisés de façon identique entre Task 1, 2, 3 et le
  test.
- **Note CI** : test sans `assert` (NDEBUG-safe) ; cible `ln_theme_tests` liée à
  `engine_core` (include path) ; client-only, jamais `server_app`.
