# Spec — Validation temps réel de l'inscription (rendu ImGui des retours + socle partagé)

**Date :** 2026-06-19
**Sous-système :** #2 du lot 2026-06-18 (validation inscription)
**Statut :** Design validé (approche A + périmètre corrigé), en attente de plan d'implémentation
**Déploiement :** ✅ client uniquement (comportement) — **pas** de redéploiement serveur

---

## 1. Contexte : la logique existe, le RENDU ImGui est absent

L'écran d'inscription (`Phase::Register`) a une logique de validation partielle dans le
**modèle / réseau**, mais le **renderer ImGui actif n'affiche AUCUN retour** (même pattern
que #4/#6 : feature présente côté modèle, rendu ImGui manquant). Constat vérifié :

| Contrôle | Logique modèle/réseau | Rendu ImGui (écran réel) |
|----------|------------------------|--------------------------|
| **(a) Identifiant déjà pris** | ✅ Opcodes 35/36, handler master (`AccountStore::ExistsLogin`), debounce 800 ms + n° de séquence ; `RenderField::usernameCheckState` (0/1/2/3) peuplé — [AuthScreenRegister.cpp:256](../../../src/client/auth/screens/AuthScreenRegister.cpp), :602-662 | ❌ **non affiché** (lu seulement par le renderer natif `AuthUiRendererCore.cpp:828`, pas par ImGui) |
| **(c) Double saisie identique** | ✅ `RenderField::passwordMatchState` (0/1/-1) peuplé — [AuthScreenRegister.cpp:279](../../../src/client/auth/screens/AuthScreenRegister.cpp) | ❌ **non affiché** en ImGui |
| **(b) Mot de passe** | ⚠️ `strength` calculé dans le renderer mais **jamais affiché** (sert seulement à `canSubmit`, [AuthImGuiRegister.cpp:299](../../../src/client/render/auth/screens/AuthImGuiRegister.cpp)) ; critères **divergents** du serveur | ❌ **non affiché** |
| **(d) E-mail bien formé** | ❌ aucun état (validé seulement au submit serveur via `ValidateEmail`) | ❌ **non affiché** |

`DrawAuthGoldField` ([AuthImGuiRenderer.cpp:697](../../../src/client/render/AuthImGuiRenderer.cpp)) — le champ ImGui — ne dessine
**que** le label + `InputText`, **aucun** indicateur d'état.

### Le vrai périmètre

**Faire afficher par le renderer ImGui les 4 retours temps réel**, en réutilisant ce qui
existe déjà dans le modèle et en s'appuyant sur des validateurs **partagés** :
1. **(a) identifiant** : lire `field.usernameCheckState` (déjà peuplé) → pastille verte/rouge/grise ;
2. **(b) mot de passe** : checklist `≥8 · une lettre · un chiffre` (**politique serveur**, via validateurs partagés) — remplace le `strength` divergent inutilisé ;
3. **(c) correspondance** : lire `field.passwordMatchState` (déjà peuplé) → ✓/✗ ;
4. **(d) e-mail** : nouvel `field.emailFormatState` → ✓/✗.

**Incohérence (b) à corriger** : le `strength` client récompense « ≥8 + **majuscule** + chiffre
+ **symbole** » alors que la politique serveur `ValidatePassword` exige « ≥8 + **1 chiffre** +
**1 lettre** » (≤256) → un mot de passe accepté par le serveur peut paraître « faible ». On
aligne en partageant les règles.

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
- **Namespace conservé `engine::server`** (choix de robustesse) : on déplace seulement le
  fichier vers `src/shared/`, sans renommer le namespace — ainsi **aucun call-site serveur
  n'a à changer sa qualification** (les appels `engine::server::…` et non qualifiés restent
  valides). Seul le **chemin d'include** change (`src/masterd/account/…` →
  `src/shared/account/…`) dans les 6 fichiers qui l'incluent. L'objectif (source unique
  client+serveur) est atteint ; un éventuel renommage `engine::account` est un nettoyage
  ultérieur facultatif.
- CMake : déplacer les 3 références `src/masterd/account/AccountValidation.cpp` →
  `src/shared/account/AccountValidation.cpp` dans `src/CMakeLists.txt` (lignes 148/323/1022),
  **et** ajouter le `.cpp` à **`engine_core`** (client, `CMakeLists.txt` racine) — un .cpp
  partagé doit être dans les listes serveur **et** client (cf. `reference_server_app_sources`).
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

### 3.4 Rendu ImGui (cœur du correctif — aujourd'hui ABSENT)

**3.4.a — `DrawAuthGoldField` ([AuthImGuiRenderer.cpp:697](../../../src/client/render/AuthImGuiRenderer.cpp))** : après
l'`InputText`, dessiner une **barre/pastille d'état** sous le champ quand l'un des états du
`spec` est non-neutre, en miroir du schéma de couleurs du renderer natif
([AuthUiRendererCore.cpp:819-837](../../../src/client/render/auth/AuthUiRendererCore.cpp)) :
- `spec.usernameCheckState` : 1 Pending → gris, 2 Available → vert, 3 Taken → rouge (champ identifiant) ;
- `spec.passwordMatchState` : 1 → vert, -1 → rouge (champ confirmation) ;
- `spec.emailFormatState` : 1 → vert, -1 → rouge (champ e-mail).
Un seul de ces états est non-nul par champ → une barre `ImGui::GetWindowDrawList()->AddRectFilled`
de 2 px en bas de la cellule (couleurs ASCII-safe, pas de glyphe spécial requis). C'est ce qui
rend **(a)**, **(c)** et **(d)** enfin **visibles** en jeu.

**3.4.b — `AuthImGuiRegister.cpp`** : **supprimer** le bloc `strength` divergent et inutilisé
(l.238-274) ; à la place, sous le champ mot de passe, afficher la **checklist live** alignée
serveur lue depuis `rm.fields[<pw>].pwdRuleLength/Letter/Digit` :
`[v/x] Au moins 8 caracteres   [v/x] Une lettre   [v/x] Un chiffre`
(préfixe ASCII `[v]`/`[x]` + couleur verte/rouge ; rien si `-1`/non évalué). `canSubmit`
s'appuie désormais sur `ValidatePassword`/`pwdRule*` (mêmes règles que le serveur) au lieu
de `strength >= 3`.

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

- La **logique modèle/réseau** de (a) identifiant et (c) double-saisie : **inchangée**
  (déjà correcte) — on ne touche qu'au **rendu ImGui** (3.4.a) pour les rendre visibles.
- Refonte visuelle globale des 4 contrôles (écartée — on comble, on n'embellit pas tout).
- Changement de la **politique** de mot de passe/e-mail (on s'aligne sur la politique
  serveur existante, on ne la durcit pas).
- Validation DNS/MX de l'e-mail (le serveur ne fait pas de résolution ; le client non plus).
