# Spec — Validation temps réel de l'inscription (comblement des manques)

**Date :** 2026-06-19
**Sous-système :** #2 du lot 2026-06-18 (validation inscription)
**Statut :** Design validé (approche A), en attente de plan d'implémentation
**Déploiement :** ✅ client uniquement (comportement) — **pas** de redéploiement serveur

---

## 1. Contexte : l'essentiel existe déjà

L'écran d'inscription (`Phase::Register`) possède **déjà** une validation temps réel
partielle (ne pas reconstruire — cf. `feedback_dedup_before_delivery`) :

| Contrôle | État | Où |
|----------|------|----|
| **(a) Identifiant déjà pris** | ✅ **fait** | Opcodes 35/36, handler master (`AccountStore::ExistsLogin`), debounce 800 ms + n° de séquence anti-réponse-obsolète ; indicateur `RenderField::usernameCheckState` (0 Idle / 1 Pending / 2 Available / 3 Taken) — [AuthScreenRegister.cpp:256](../../../src/client/auth/screens/AuthScreenRegister.cpp), :602-662 |
| **(c) Double saisie identique** | ✅ **fait** | `RenderField::passwordMatchState` (0 neutre / 1 match / -1 mismatch), `canSubmit` via `strcmp` — [AuthScreenRegister.cpp:279](../../../src/client/auth/screens/AuthScreenRegister.cpp) |
| **(b) Mot de passe** | ⚠️ **incohérent** | Le renderer calcule un mètre de force client — [AuthImGuiRegister.cpp:238-274](../../../src/client/render/auth/screens/AuthImGuiRegister.cpp) |
| **(d) E-mail bien formé** | ❌ **manquant** (temps réel) | Validé seulement au submit côté serveur (`ValidateEmail`) |

### Les deux vrais manques

1. **(d) E-mail** : aucun indicateur de format **en temps réel** côté client (le champ
   e-mail est ajouté avec un état neutre, [AuthScreenRegister.cpp:268](../../../src/client/auth/screens/AuthScreenRegister.cpp)).
2. **(b) Incohérence mot de passe** : le mètre de force **client** récompense
   « ≥8 + **majuscule** + chiffre + **symbole** », alors que la **politique serveur**
   `ValidatePassword` exige « ≥8 + **1 chiffre** + **1 lettre** » (≤256). Le client peut
   donc afficher « faible » pour un mot de passe **accepté** par le serveur (et inversement)
   → retour trompeur.

---

## 2. Décision de conception (approche A — validateurs partagés)

**Cause racine de l'incohérence (b)** : la règle de mot de passe est **dupliquée** (le
client a réimplémenté des critères différents du serveur). On supprime la duplication en
**partageant** les validateurs.

`AccountValidation` ne dépend que de `src/shared/network/NetErrorCode.h` (déjà partagé) et
ne contient que des **fonctions pures** → déplaçable en `src/shared/`. Le client et le
serveur utilisent alors **la même** source de vérité.

Approches écartées :
- **B (miroir client)** : réintroduit le risque de dérive — exactement le bug corrigé.
- **C (inline minimal)** : duplique les règles, peu maintenable.

---

## 3. Architecture & composants

### 3.1 Validateurs partagés (déplacement + extension)

**`src/masterd/account/AccountValidation.{h,cpp}` → `src/shared/account/AccountValidation.{h,cpp}`**
- Espace de noms : `engine::server` → **`engine::account`**.
- Mettre à jour les call-sites serveur (`AuthRegisterHandler`, `PasswordResetHandler`,
  `AdminCommandHandler`, `MysqlAccountStore`, `InMemoryAccountStore` — qualifier
  `engine::account::`).
- CMake : ajouter le `.cpp` à **`engine_core`** (client) **et** à la liste **`server_app`**
  (cf. `reference_server_app_sources` — un .cpp partagé doit être dans les deux).
- **Extension** (pour la checklist live des règles MdP) :
  ```cpp
  /// Détail des règles de mot de passe (politique v1), pour un retour UI règle par règle.
  struct PasswordRuleStatus
  {
      bool lengthOk = false;   ///< longueur dans [kAccountPasswordMinLength, kAccountPasswordMaxLength]
      bool hasLetter = false;  ///< au moins une lettre a-z/A-Z
      bool hasDigit = false;   ///< au moins un chiffre 0-9
  };
  /// Évalue chaque règle indépendamment (sans court-circuit), pour affichage live.
  PasswordRuleStatus EvaluatePasswordRules(std::string_view password);
  ```
  `ValidatePassword` est **réécrit** pour déléguer à `EvaluatePasswordRules` (zéro
  duplication ; comportement strictement identique : OK ssi les 3 booléens sont vrais).

### 3.2 Modèle UI (`RenderField`, client)

Ajouter un champ d'état e-mail (même esprit que `usernameCheckState`/`passwordMatchState`) :
```cpp
/// Indicateur de format e-mail (champ e-mail uniquement) :
/// 0 = neutre (vide), 1 = format valide, -1 = format invalide.
int32_t emailFormatState = 0;
```
Pour le mot de passe : porter les 3 booléens de règles pour la checklist live :
```cpp
/// Règles de mot de passe live (champ mot de passe uniquement) : -1 = non évalué,
/// 0 = échoue, 1 = respectée. Ordre : [longueur, lettre, chiffre].
int32_t pwdRuleLength = -1;
int32_t pwdRuleLetter = -1;
int32_t pwdRuleDigit  = -1;
```

### 3.3 Presenter (`BuildModel_Register`)

- **E-mail** : `emailFormatState` =
  - `0` si `m_email` vide ;
  - sinon `ValidateEmail(NormaliseEmail(m_email)) == NetErrorCode::OK ? 1 : -1`.
- **Mot de passe** : `const auto r = EvaluatePasswordRules(m_password);` →
  remplir `pwdRuleLength/Letter/Digit` (`-1` si `m_password` vide, sinon `0/1`).

### 3.4 Renderer (`AuthImGuiRegister.cpp`)

- **E-mail** : afficher l'indicateur de format (réutiliser le motif barre/pastille colorée
  de `usernameCheckState` : vert = valide, rouge = invalide, rien si neutre).
- **Mot de passe** : **remplacer** le mètre de force divergent (l.238-274) par une
  **checklist live** alignée serveur : `[✓/✗] ≥ 8 caractères · [✓/✗] une lettre · [✓/✗] un chiffre`,
  lue depuis `pwdRuleLength/Letter/Digit`.

### 3.5 i18n (ASCII-only — contrainte atlas police, cf. `reference_imgui_font_atlas`)

Nouvelles clés (en+fr, puis miroir es/de/it/pl/pt avec la parité garantie par
`catalog_parity_tests`) :
- `auth.register.email_valid` / `auth.register.email_invalid`
- `auth.register.pwd_rule_length` (« Au moins 8 caracteres »)
- `auth.register.pwd_rule_letter` (« Au moins une lettre »)
- `auth.register.pwd_rule_digit` (« Au moins un chiffre »)

---

## 4. Flux & gestion d'erreurs

- **Saisie vide → état neutre** (pas de rouge tant que l'utilisateur n'a pas tapé).
- Le retour temps réel est **best-effort UI** ; la validation **autoritaire** reste au
  submit côté **serveur** (`ValidateEmail`/`ValidatePassword`, inchangée).
- L'indicateur (a) identifiant et (c) double-saisie **restent tels quels**.

---

## 5. Tests

- **Unitaires CI** (validateurs partagés, purs) : `EvaluatePasswordRules` (longueur limite
  7/8/256/257, présence lettre/chiffre, combinaisons), et non-régression `ValidatePassword`
  (résultat identique à l'ancien). `ValidateEmail` cas limites (sans `@`, `@` en tête,
  domaine sans `.`, trop long). Réutiliser/compléter les tests d'`AccountValidation`
  existants après déplacement.
- **Parité catalogues** : `catalog_parity_tests` garde les 4 nouvelles clés sur les 7 langues.
- **Rendu / feel** : validation **manuelle en jeu** (indicateurs visibles, cohérents avec
  l'acceptation serveur).

---

## 6. Déploiement

> ✅ **Client uniquement (comportement).** L'extraction de `AccountValidation` vers
> `src/shared/` **recompile** le serveur mais il **valide à l'identique** (mêmes règles,
> aucun changement wire ; opcodes 35/36 déjà déployés). **Aucun redéploiement serveur
> requis** pour que la feature (retour temps réel client) fonctionne.

---

## 7. Hors périmètre

- (a) identifiant déjà pris et (c) double-saisie : **inchangés** (déjà fonctionnels).
- Refonte visuelle globale des 4 contrôles (écartée — on comble, on n'embellit pas tout).
- Changement de la **politique** de mot de passe/e-mail (on s'aligne sur la politique
  serveur existante, on ne la durcit pas).
- Validation DNS/MX de l'e-mail (le serveur ne fait pas de résolution ; le client non plus).
