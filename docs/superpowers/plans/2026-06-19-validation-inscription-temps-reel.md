# Validation inscription temps réel — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Faire afficher par le renderer ImGui de l'écran d'inscription les 4 retours de validation temps réel (identifiant pris, règles mot de passe, double-saisie, format e-mail), sur un socle de validateurs **partagés** client+serveur (source unique).

**Architecture:** On déplace `AccountValidation` (fonctions pures) vers `src/shared/` pour que le client utilise EXACTEMENT les règles du serveur (fin de la divergence). Le presenter peuple de nouveaux états sur `RenderField` ; le renderer ImGui (`DrawAuthGoldField` + `AuthImGuiRegister`) les **affiche** (barres colorées + checklist mot de passe). La logique réseau (a) et modèle (c) existe déjà — on ne fait que la rendre visible.

**Tech Stack:** C++17/20, validateurs purs partagés, ImGui (Vulkan), tests `int main()` + `Assert`, CMake/MSVC. Déploiement **client uniquement** — pas de redéploiement serveur.

**Spec :** [docs/superpowers/specs/2026-06-19-validation-inscription-temps-reel-design.md](../specs/2026-06-19-validation-inscription-temps-reel-design.md)

**Rappels projet :** commentaires **français** ; **PascalCase** pour tout nouveau type/fichier ; pas de toolchain locale (build/ctest via **CI**, les étapes « Run » sont la cible CI) ; i18n **ASCII-only** (atlas police). Branche : `claude/register-realtime-validation`.

---

## Structure des fichiers

| Fichier | Action | Rôle |
|---------|--------|------|
| `src/shared/account/AccountValidation.{h,cpp}` | **Déplacé** depuis `src/masterd/account/` (namespace `engine::server` conservé) | Validateurs purs partagés + `EvaluatePasswordRules` |
| `src/shared/account/AccountValidationTests.cpp` | Créer | Tests unitaires des validateurs + `EvaluatePasswordRules` |
| 6 fichiers serveur incluant l'ancien chemin | Modifier (include path) | `AuthRegisterHandler.cpp`, `PasswordResetHandler.cpp`, `AdminCommandHandler.cpp`, `MysqlAccountStore.cpp`, `InMemoryAccountStore.cpp`, `AccountValidation.cpp` (self) |
| `src/CMakeLists.txt` | Modifier (3 chemins) | Lignes 148/323/1022 : nouveau chemin |
| `CMakeLists.txt` (racine) | Modifier | Ajouter le .cpp à `engine_core` + enregistrer `account_validation_tests` |
| `src/client/auth/AuthUi.h` | Modifier | `RenderField` : `emailFormatState`, `pwdRuleLength/Letter/Digit` |
| `src/client/auth/screens/AuthScreenRegister.cpp` | Modifier | `BuildModel_Register` peuple les nouveaux états (via validateurs partagés) |
| `src/client/render/AuthImGuiRenderer.cpp` | Modifier | `DrawAuthGoldField` dessine les barres d'état |
| `src/client/render/auth/screens/AuthImGuiRegister.cpp` | Modifier | Supprime `strength`, ajoute la checklist MdP, `canSubmit` aligné serveur |
| `game/data/localization/{en,fr,es,de,it,pl,pt}/*.json` | Modifier | 4 clés i18n |

---

## Task 1 : Déplacer `AccountValidation` vers shared + `EvaluatePasswordRules` + tests

**Files:**
- Move: `src/masterd/account/AccountValidation.{h,cpp}` → `src/shared/account/AccountValidation.{h,cpp}`
- Modify: includes dans 6 fichiers, `src/CMakeLists.txt`, `CMakeLists.txt`
- Test: `src/shared/account/AccountValidationTests.cpp`

- [ ] **Step 1 : Déplacer les fichiers (git mv) + créer le dossier**

```bash
mkdir -p src/shared/account
git mv src/masterd/account/AccountValidation.h src/shared/account/AccountValidation.h
git mv src/masterd/account/AccountValidation.cpp src/shared/account/AccountValidation.cpp
```

- [ ] **Step 2 : Corriger l'include self dans le .cpp**

Dans `src/shared/account/AccountValidation.cpp`, remplacer la 1re ligne :
```cpp
#include "src/masterd/account/AccountValidation.h"
```
par :
```cpp
#include "src/shared/account/AccountValidation.h"
```

- [ ] **Step 3 : Ajouter `PasswordRuleStatus` + `EvaluatePasswordRules` au header**

Dans `src/shared/account/AccountValidation.h`, juste AVANT la déclaration de `ValidatePassword` (la ligne `engine::network::NetErrorCode ValidatePassword(std::string_view password);`), insérer :
```cpp
	/// Détail règle-par-règle de la politique de mot de passe v1, pour un retour UI live
	/// (checklist). Chaque booléen est évalué indépendamment (sans court-circuit).
	struct PasswordRuleStatus
	{
		bool lengthOk = false;  ///< longueur dans [kAccountPasswordMinLength, kAccountPasswordMaxLength]
		bool hasLetter = false; ///< au moins une lettre ASCII a-z / A-Z
		bool hasDigit = false;  ///< au moins un chiffre 0-9
	};

	/// Évalue chaque règle de mot de passe séparément (pour affichage live).
	/// ValidatePassword() == OK si et seulement si les trois booléens sont vrais.
	PasswordRuleStatus EvaluatePasswordRules(std::string_view password);

```

- [ ] **Step 4 : Implémenter `EvaluatePasswordRules` + réécrire `ValidatePassword` pour déléguer**

Dans `src/shared/account/AccountValidation.cpp`, remplacer la fonction `ValidatePassword` existante :
```cpp
	engine::network::NetErrorCode ValidatePassword(std::string_view password)
	{
		if (password.size() < kAccountPasswordMinLength || password.size() > kAccountPasswordMaxLength)
			return engine::network::NetErrorCode::WEAK_PASSWORD;
		bool hasDigit = false;
		bool hasLetter = false;
		for (unsigned char c : password)
		{
			if (std::isdigit(c))
				hasDigit = true;
			if (std::isalpha(static_cast<unsigned char>(c)))
				hasLetter = true;
		}
		if (!hasDigit || !hasLetter)
			return engine::network::NetErrorCode::WEAK_PASSWORD;
		return engine::network::NetErrorCode::OK;
	}
```
par :
```cpp
	PasswordRuleStatus EvaluatePasswordRules(std::string_view password)
	{
		PasswordRuleStatus s;
		s.lengthOk = (password.size() >= kAccountPasswordMinLength
			&& password.size() <= kAccountPasswordMaxLength);
		for (unsigned char c : password)
		{
			if (std::isdigit(c))
				s.hasDigit = true;
			if (std::isalpha(c))
				s.hasLetter = true;
		}
		return s;
	}

	engine::network::NetErrorCode ValidatePassword(std::string_view password)
	{
		const PasswordRuleStatus s = EvaluatePasswordRules(password);
		if (!s.lengthOk || !s.hasDigit || !s.hasLetter)
			return engine::network::NetErrorCode::WEAK_PASSWORD;
		return engine::network::NetErrorCode::OK;
	}
```
(Comportement de `ValidatePassword` strictement identique à l'avant ; `#include <cctype>` est déjà présent.)

- [ ] **Step 5 : Mettre à jour le chemin d'include dans les 5 fichiers serveur**

Dans chacun de ces fichiers, remplacer `#include "src/masterd/account/AccountValidation.h"` par `#include "src/shared/account/AccountValidation.h"` :
- `src/masterd/handlers/auth/AuthRegisterHandler.cpp` (ligne 8)
- `src/masterd/handlers/password/PasswordResetHandler.cpp` (ligne 10)
- `src/masterd/handlers/admin/AdminCommandHandler.cpp` (ligne 11)
- `src/masterd/account/MysqlAccountStore.cpp` (ligne 12)
- `src/masterd/account/InMemoryAccountStore.cpp` (ligne 2)

Vérifier qu'aucun autre fichier ne référence l'ancien chemin :
```bash
grep -rn "masterd/account/AccountValidation" src/ ; echo "(doit être vide)"
```

- [ ] **Step 6 : CMake — déplacer les 3 chemins + ajouter à engine_core**

Dans `src/CMakeLists.txt`, aux lignes 148, 323 et 1022, remplacer
`${CMAKE_SOURCE_DIR}/src/masterd/account/AccountValidation.cpp`
par
`${CMAKE_SOURCE_DIR}/src/shared/account/AccountValidation.cpp`.

Dans le `CMakeLists.txt` **racine**, dans la liste de sources de `engine_core` (près de
`src/client/localization/LocalizationService.cpp`, ~ligne 249), ajouter :
```cmake
  src/shared/account/AccountValidation.cpp
```

- [ ] **Step 7 : Écrire le test des validateurs**

Create `src/shared/account/AccountValidationTests.cpp` :
```cpp
#include "src/shared/account/AccountValidation.h"

#include <iostream>
#include <string>

namespace
{
	int s_fail = 0;
	void Assert(bool cond, const char* msg)
	{
		if (!cond) { std::cerr << "[FAIL] " << msg << std::endl; ++s_fail; }
	}
	using engine::network::NetErrorCode;

	void TestPasswordRules()
	{
		// 7 caractères -> longueur KO.
		auto a = engine::server::EvaluatePasswordRules("abc123z");
		Assert(!a.lengthOk, "len 7 -> lengthOk false");
		Assert(a.hasLetter && a.hasDigit, "abc123z -> lettre+chiffre");
		// 8 caractères, lettre+chiffre -> tout OK.
		auto b = engine::server::EvaluatePasswordRules("abcd1234");
		Assert(b.lengthOk && b.hasLetter && b.hasDigit, "abcd1234 -> 3 règles OK");
		// Que des chiffres -> pas de lettre.
		auto c = engine::server::EvaluatePasswordRules("12345678");
		Assert(c.lengthOk && c.hasDigit && !c.hasLetter, "12345678 -> sans lettre");
		// Que des lettres -> pas de chiffre.
		auto d = engine::server::EvaluatePasswordRules("abcdefgh");
		Assert(d.lengthOk && d.hasLetter && !d.hasDigit, "abcdefgh -> sans chiffre");
	}

	void TestValidatePasswordEquivalence()
	{
		Assert(engine::server::ValidatePassword("abcd1234") == NetErrorCode::OK, "valid pw OK");
		Assert(engine::server::ValidatePassword("abc123z") == NetErrorCode::WEAK_PASSWORD, "len 7 weak");
		Assert(engine::server::ValidatePassword("12345678") == NetErrorCode::WEAK_PASSWORD, "no letter weak");
		Assert(engine::server::ValidatePassword("abcdefgh") == NetErrorCode::WEAK_PASSWORD, "no digit weak");
	}

	void TestValidateEmail()
	{
		using engine::server::ValidateEmail;
		using engine::server::NormaliseEmail;
		Assert(ValidateEmail(NormaliseEmail("a@b.com")) == NetErrorCode::OK, "a@b.com OK");
		Assert(ValidateEmail(NormaliseEmail("  A@B.COM ")) == NetErrorCode::OK, "trim+lower OK");
		Assert(ValidateEmail(NormaliseEmail("noat")) == NetErrorCode::INVALID_EMAIL, "no @");
		Assert(ValidateEmail(NormaliseEmail("@b.com")) == NetErrorCode::INVALID_EMAIL, "@ en tete");
		Assert(ValidateEmail(NormaliseEmail("a@bcom")) == NetErrorCode::INVALID_EMAIL, "domaine sans .");
		Assert(ValidateEmail(NormaliseEmail("")) == NetErrorCode::INVALID_EMAIL, "vide");
	}
}

int main()
{
	TestPasswordRules();
	TestValidatePasswordEquivalence();
	TestValidateEmail();
	if (s_fail != 0) return 1;
	std::cout << "AccountValidation tests: all passed." << std::endl;
	return 0;
}
```

- [ ] **Step 8 : CMake — enregistrer le test**

Dans `CMakeLists.txt` racine, après le bloc `localization_service_tests` (ou un autre test client existant), ajouter :
```cmake
# Validateurs de compte partagés (login/email/password) — Plan #2
add_executable(account_validation_tests src/shared/account/AccountValidationTests.cpp)
target_link_libraries(account_validation_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(account_validation_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME account_validation_tests COMMAND account_validation_tests)
```
(`engine_core` contient maintenant `AccountValidation.cpp` (Step 6) → le test y accède.)

- [ ] **Step 9 : (build/ctest — SKIP, pas de toolchain locale ; validé en CI).**

- [ ] **Step 10 : Commit**
```bash
git add src/shared/account CMakeLists.txt src/CMakeLists.txt \
        src/masterd/handlers/auth/AuthRegisterHandler.cpp \
        src/masterd/handlers/password/PasswordResetHandler.cpp \
        src/masterd/handlers/admin/AdminCommandHandler.cpp \
        src/masterd/account/MysqlAccountStore.cpp \
        src/masterd/account/InMemoryAccountStore.cpp
git commit -m "refactor(account): valideurs partagés src/shared + EvaluatePasswordRules + tests"
```
(Terminer le message par `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.)

---

## Task 2 : États de validation sur `RenderField` + calcul presenter

**Files:**
- Modify: `src/client/auth/AuthUi.h` (struct `RenderField`)
- Modify: `src/client/auth/screens/AuthScreenRegister.cpp` (`BuildModel_Register`)

- [ ] **Step 1 : Ajouter les états à `RenderField`**

Dans `src/client/auth/AuthUi.h`, dans `struct RenderField` (juste après `int32_t usernameCheckState = 0;`, avant la `};` de fin), ajouter :
```cpp
			/// Indicateur de format e-mail (champ e-mail uniquement) :
			/// 0 = neutre (vide), 1 = format valide, -1 = format invalide.
			int32_t emailFormatState = 0;
			/// Règles de mot de passe live (champ mot de passe uniquement) :
			/// -1 = non évalué (vide), 0 = règle non respectée, 1 = règle respectée.
			int32_t pwdRuleLength = -1;
			int32_t pwdRuleLetter = -1;
			int32_t pwdRuleDigit  = -1;
```

- [ ] **Step 2 : Inclure les validateurs partagés dans le presenter**

En tête de `src/client/auth/screens/AuthScreenRegister.cpp`, dans le bloc d'includes (près de `#include "src/shared/network/AuthRegisterPayloads.h"`), ajouter :
```cpp
#	include "src/shared/account/AccountValidation.h"
```
(garder le même style `#\t` si les includes du fichier sont sous un `#if defined(_WIN32)` indenté — vérifier l'indentation locale des includes existants.)

- [ ] **Step 3 : Peupler l'état e-mail dans `BuildModel_Register`**

Dans `BuildModel_Register`, juste après l'ajout du champ e-mail (la ligne
`addGridField(Tr("common.email"), m_email, m_activeField == 3, ... Tr("auth.placeholder.register_email"));`,
vers la ligne 268), insérer :
```cpp
		model.fields.back().emailFormatState = m_email.empty()
			? 0
			: (engine::server::ValidateEmail(engine::server::NormaliseEmail(m_email))
				== engine::network::NetErrorCode::OK ? 1 : -1);
```

- [ ] **Step 4 : Peupler les règles MdP dans `BuildModel_Register`**

Juste après l'ajout du champ mot de passe (la ligne `addGridField(Tr("auth.label.password"), maskedPassword(), ... Tr("auth.placeholder.register_password"));`, vers la ligne 276), insérer :
```cpp
		{
			RenderField& pwField = model.fields.back();
			if (m_password.empty())
			{
				pwField.pwdRuleLength = -1;
				pwField.pwdRuleLetter = -1;
				pwField.pwdRuleDigit  = -1;
			}
			else
			{
				const engine::server::PasswordRuleStatus rules =
					engine::server::EvaluatePasswordRules(m_password);
				pwField.pwdRuleLength = rules.lengthOk ? 1 : 0;
				pwField.pwdRuleLetter = rules.hasLetter ? 1 : 0;
				pwField.pwdRuleDigit  = rules.hasDigit ? 1 : 0;
			}
		}
```
(`m_password`/`m_email` sont les buffers presenter ; `engine::network::NetErrorCode` est déjà visible via les payloads inclus.)

- [ ] **Step 5 : (build — SKIP, CI).**

- [ ] **Step 6 : Commit**
```bash
git add src/client/auth/AuthUi.h src/client/auth/screens/AuthScreenRegister.cpp
git commit -m "feat(auth): états e-mail/règles MdP sur RenderField (via valideurs partagés)"
```

---

## Task 3 : Rendu ImGui des barres d'état dans `DrawAuthGoldField`

**Files:**
- Modify: `src/client/render/AuthImGuiRenderer.cpp` (`DrawAuthGoldField`, ~ligne 697-762)

- [ ] **Step 1 : Dessiner la barre d'état sous le champ**

Dans `DrawAuthGoldField`, juste AVANT le `ImGui::Spacing();` final (la dernière instruction avant la `}` de fermeture, ~ligne 761), insérer :
```cpp
		// Barre d'état 2 px sous la cellule (retour validation temps réel). Un seul des
		// trois états est non-neutre selon le champ : identifiant (usernameCheckState),
		// confirmation (passwordMatchState), e-mail (emailFormatState). Couleurs alignées
		// sur le renderer natif AuthUiRendererCore. ASCII-safe (pas de glyphe).
		{
			float br = 0.f, bg = 0.f, bb = 0.f;
			bool drawBar = false;
			if (spec.usernameCheckState == 1) { br = 0.55f; bg = 0.55f; bb = 0.55f; drawBar = true; } // Pending gris
			else if (spec.usernameCheckState == 2) { br = 0.13f; bg = 0.80f; bb = 0.27f; drawBar = true; } // Available vert
			else if (spec.usernameCheckState == 3) { br = 0.80f; bg = 0.13f; bb = 0.13f; drawBar = true; } // Taken rouge
			else if (spec.passwordMatchState == 1 || spec.emailFormatState == 1) { br = 0.13f; bg = 0.80f; bb = 0.27f; drawBar = true; }
			else if (spec.passwordMatchState == -1 || spec.emailFormatState == -1) { br = 0.80f; bg = 0.13f; bb = 0.13f; drawBar = true; }
			if (drawBar)
			{
				const ImVec2 mn = ImGui::GetItemRectMin();   // dernière InputText
				const ImVec2 mx = ImGui::GetItemRectMax();
				ImDrawList* dl = ImGui::GetWindowDrawList();
				dl->AddRectFilled(ImVec2(mn.x, mx.y - 2.f), ImVec2(mx.x, mx.y),
					ImGui::ColorConvertFloat4ToU32(ImVec4(br, bg, bb, 1.f)));
			}
		}
```
(`ImGui::GetItemRect*` renvoie le rect du dernier widget = l'`InputText` qui précède ; `ImDrawList`/`ImVec2` sont déjà disponibles via `imgui.h` inclus en tête du fichier.)

- [ ] **Step 2 : (build — SKIP, CI).**

- [ ] **Step 3 : Commit**
```bash
git add src/client/render/AuthImGuiRenderer.cpp
git commit -m "feat(auth/ui): DrawAuthGoldField affiche la barre d'état (identifiant/match/e-mail)"
```

---

## Task 4 : Checklist mot de passe + `canSubmit` aligné serveur

**Files:**
- Modify: `src/client/render/auth/screens/AuthImGuiRegister.cpp` (~lignes 238-299)

> ⚠️ **Attention** : entre le bloc `strength` (l.238-274) et le calcul de `canSubmit`
> (l.297-299) se trouve le bloc `dayStr`/`monStr`/`yrStr` (l.276-295) qui est **nécessaire**
> au form-build des boutons — **NE PAS le supprimer**. On remplace donc séparément (a) le bloc
> `strength` et (b) la ligne `canSubmit`.

- [ ] **Step 1 : Remplacer UNIQUEMENT le bloc `strength` (l.238-274) par la checklist**

Lire le fichier pour repérer le bloc exact : il commence à `int strength = 0;` et se termine
au dernier `if (hasSym)\n\t\t{\n\t\t\t++strength;\n\t\t}` (juste avant le commentaire/bloc
`std::string dayStr = "01";`). Remplacer **ce seul bloc** (qui calcule `strength`, `pwLen`,
`hasUpper`, `hasDigit`, `hasSym`) par :
```cpp
		// Checklist mot de passe LIVE, alignée sur la politique serveur (ValidatePassword) :
		// >= 8 caractères, au moins une lettre, au moins un chiffre. États lus depuis le
		// modèle (rm.fields[8].pwdRule*), peuplés par BuildModel_Register via les valideurs
		// PARTAGÉS — donc strictement cohérents avec l'acceptation serveur.
		const engine::client::AuthUiPresenter::RenderField& pwSpec = rm.fields[8];
		auto drawRule = [this, &tr](int32_t state, const char* key, const char* fallback) {
			if (state < 0) return;                       // non évalué (champ vide)
			const bool ok = (state == 1);
			ImGui::PushStyleColor(ImGuiCol_Text, ok ? IV(LnTheme::kAccent) : IV(LnTheme::kErrorCol));
			ImGui::Text("%s %s", ok ? "[v]" : "[x]", tr(key, fallback).c_str());
			ImGui::PopStyleColor();
		};
		drawRule(pwSpec.pwdRuleLength, "auth.register.pwd_rule_length", "Au moins 8 caracteres");
		drawRule(pwSpec.pwdRuleLetter, "auth.register.pwd_rule_letter", "Au moins une lettre");
		drawRule(pwSpec.pwdRuleDigit,  "auth.register.pwd_rule_digit",  "Au moins un chiffre");
		ImGui::Spacing();
		const bool pwdRulesOk = (pwSpec.pwdRuleLength == 1)
			&& (pwSpec.pwdRuleLetter == 1) && (pwSpec.pwdRuleDigit == 1);
```
> `rm.fields[8]` = champ mot de passe (ordre `BuildModel_Register` : login=0, country=1,
> last=2, first=3, email=4, day=5, month=6, year=7, **password=8**, confirm=9). `tr`, `IV`,
> `LnTheme::kAccent`/`kErrorCol` sont déjà définis/utilisés dans ce fichier. **Le bloc
> `dayStr`/`monStr`/`yrStr` qui suit reste intact.**

- [ ] **Step 1bis : Aligner `canSubmit` (l.297-299)**

Toujours dans `RenderRegisterScreen`, remplacer la ligne (vers 299) :
```cpp
		const bool canSubmit = fieldsOk && (strength >= 3) && (std::strlen(m_regPw) > 0) && (std::strcmp(m_regPw, m_regPw2) == 0);
```
par :
```cpp
		const bool canSubmit = fieldsOk && pwdRulesOk && (std::strcmp(m_regPw, m_regPw2) == 0);
```
(La ligne `const bool fieldsOk = ...` juste au-dessus reste inchangée. `pwdRulesOk` est
défini au Step 1 ; `strength` n'existe plus.)

- [ ] **Step 2 : Vérifier qu'`std::isalnum`/`<cctype>` n'est plus requis si devenu inutilisé**

Le bloc supprimé utilisait `std::isalnum` (via `<cctype>`). Vérifier si `<cctype>` reste utilisé ailleurs dans le fichier :
```bash
grep -n "std::is" src/client/render/auth/screens/AuthImGuiRegister.cpp
```
S'il n'y a plus aucune occurrence, retirer `#include <cctype>` (ligne 7) pour éviter un include mort ; sinon le laisser.

- [ ] **Step 3 : (build — SKIP, CI).**

- [ ] **Step 4 : Commit**
```bash
git add src/client/render/auth/screens/AuthImGuiRegister.cpp
git commit -m "feat(auth/ui): checklist MdP alignée serveur + canSubmit cohérent (remplace strength)"
```

---

## Task 5 : Clés i18n (7 catalogues)

**Files:**
- Modify: `game/data/localization/{en,fr,es,de,it,pl,pt}/<tag>.json`

- [ ] **Step 1 : Ajouter les 4 clés à `en.json` (référence)**

Dans `game/data/localization/en/en.json`, près des autres clés `auth.register.*`, ajouter :
```json
  "auth.register.email_valid": "Email format looks valid",
  "auth.register.email_invalid": "Invalid email format",
  "auth.register.pwd_rule_length": "At least 8 characters",
  "auth.register.pwd_rule_letter": "At least one letter",
  "auth.register.pwd_rule_digit": "At least one digit",
```

- [ ] **Step 2 : Ajouter les MÊMES clés aux 6 autres catalogues**

Ajouter les 5 mêmes clés (valeurs traduites, **ASCII-only**, pas d'accent) dans chacun de
`fr/fr.json`, `es/es.json`, `de/de.json`, `it/it.json`, `pl/pl.json`, `pt/pt.json`. Exemple `fr` :
```json
  "auth.register.email_valid": "Format d'e-mail valide",
  "auth.register.email_invalid": "Format d'e-mail invalide",
  "auth.register.pwd_rule_length": "Au moins 8 caracteres",
  "auth.register.pwd_rule_letter": "Au moins une lettre",
  "auth.register.pwd_rule_digit": "Au moins un chiffre",
```
(es : "Formato de correo valido"… ; de : "E-Mail-Format gueltig"… ; etc. — traductions ASCII-only.)

- [ ] **Step 3 : Vérifier la parité (les 7 catalogues ont le même jeu de clés)**

Run (PowerShell) :
```powershell
$b="game/data/localization"; $en=(Get-Content -Raw -Encoding UTF8 "$b/en/en.json"|ConvertFrom-Json).PSObject.Properties.Name
foreach($t in 'fr','es','de','it','pl','pt'){ $k=(Get-Content -Raw -Encoding UTF8 "$b/$t/$t.json"|ConvertFrom-Json).PSObject.Properties.Name; $d=Compare-Object $en $k; if($d){Write-Output "$t DIFF:"; $d}else{Write-Output "$t OK"} }
```
Expected: `fr OK … pt OK` (le test CI `catalog_parity_tests` valide aussi).

- [ ] **Step 4 : Commit**
```bash
git add game/data/localization
git commit -m "feat(i18n): clés validation inscription (e-mail + règles MdP) sur 7 catalogues"
```

---

## Validation finale (en jeu — hors CI)

Le rendu ImGui n'est pas testable en CI. À valider en jeu sur l'écran d'inscription :
- [ ] Taper un identifiant existant → barre **rouge** ; un libre → **verte** (après ~800 ms).
- [ ] Mot de passe : la checklist `[v]/[x]` reflète `≥8 · lettre · chiffre` en direct, et un mot de passe accepté par le serveur n'affiche jamais `[x]`.
- [ ] Confirmation : barre **verte** si identique, **rouge** sinon.
- [ ] E-mail : barre **verte** si format valide (`a@b.com`), **rouge** sinon.
- [ ] Le bouton « Créer le compte » / Entrée ne submit que si `canSubmit` (cohérent avec l'acceptation serveur).

---

## Déploiement

> ✅ **Client uniquement (comportement).** `AccountValidation` déplacé vers `src/shared/`
> (le serveur recompile mais valide à l'identique) ; rendu ImGui ajouté côté client ;
> opcodes 35/36 déjà déployés. **Aucun redéploiement serveur requis.**
