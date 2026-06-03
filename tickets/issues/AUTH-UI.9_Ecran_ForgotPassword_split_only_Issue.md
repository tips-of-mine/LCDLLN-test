# Issue: AUTH-UI.9

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- src/client/auth/screens/AuthScreenForgotPassword.cpp
- src/client/render/auth/screens/AuthImGuiForgotPassword.cpp

## Note
ForgotPassword

---

## Contenu du ticket (AUTH-UI.9)

# AUTH-UI.9 — Écran Mot de passe oublié · ForgotPassword (split uniquement)

## Dépendances
- AUTH-UI.1 (socle commun)
- AUTH-UI.2 (Login — c'est depuis Login que ForgotPassword est déclenché)

## Objectif

**Split uniquement** : déplacer dans `AuthScreenForgotPassword.cpp` et `AuthImGuiForgotPassword.cpp` les méthodes relatives à `Phase::ForgotPassword`.

Il n'existe **pas de maquette HTML correspondante** dans le design system actuel. Le rendu ImGui existant (hérité de STAB.13) est conservé sans modification visuelle. Un redesign visuel sera ajouté dans un ticket ultérieur lorsqu'une maquette sera disponible.

---

## Périmètre fonctionnel (Phase::ForgotPassword)

### Méthodes presenter → `engine/client/auth/screens/AuthScreenForgotPassword.cpp`

| Méthode |
|---|
| `StartForgotPasswordWorker()` |
| `ImGuiNavigateToForgotFromLogin()` |
| `ImGuiSubmitForgotPassword()` |
| `ImGuiBackFromForgotToLogin()` |
| `BuildModel_ForgotPassword()` **(nouvelle méthode privée)** |
| `Update_ForgotPassword()` **(nouvelle méthode privée)** |

### Renderer → `engine/render/auth/screens/AuthImGuiForgotPassword.cpp`

Extraction de la logique ImGui ForgotPassword depuis `AuthUiRenderer.cpp` (si présente) vers ce fichier dédié.

---

## Rendu ImGui (état existant STAB.13, non modifié)

Le rendu actuel de `Phase::ForgotPassword` affiche :
- Panel avec champ "Adresse courriel"
- Bouton "Envoyer le lien"
- Bouton "Retour"
- Banner success/error selon résultat worker

Ce rendu est **conservé tel quel** après le split. Seul l'emplacement du code change.

---

## Note de conception (pour future maquette)

Quand une maquette `ForgotPasswordScreen` sera disponible dans `design/lune-noire-design-system/project/ui_kits/auth_flow/`, un ticket dédié `AUTH-UI.9b` couvrira le redesign visuel. Le split opéré ici le rendra trivial à mettre en œuvre.

Éléments prévisibles pour la maquette (cohérence avec les autres écrans) :
- Hero 2 lignes ou breadcrumb
- Panel avec Field "Adresse courriel" + tooltip "Lien envoyé si le compte existe"
- Banner success "Lien envoyé — vérifiez votre courriel"
- Banner error si échec réseau
- Button ghost "Retour" keycap="Échap"
- Button primary "Envoyer le lien" keycap="↵"

---

## Livrables

**Créés / Complétés :**
- `engine/client/auth/screens/AuthScreenForgotPassword.cpp`
- `engine/render/auth/screens/AuthImGuiForgotPassword.cpp`

**Modifiés :**
- `engine/client/AuthUi.h` — `BuildModel_ForgotPassword()`, `Update_ForgotPassword()`
- `engine/client/auth/AuthUiPresenterCore.cpp` — dispatch
- `CMakeLists.txt` — ajout des deux nouveaux fichiers si non déjà listés (AUTH-UI.1)

---

## Definition of Done

- [ ] Build Windows OK
- [ ] Écran ForgotPassword s'affiche identiquement à l'état STAB.13 (pas de régression visuelle)
- [ ] Transition Login → ForgotPassword (bouton "Mot de passe oublié ?")
- [ ] Transition ForgotPassword → Login (Retour / Escape)
- [ ] Submit → worker lancé → Banner success ou error
- [ ] Aucune régression Login / Register
- [ ] Rapport final
