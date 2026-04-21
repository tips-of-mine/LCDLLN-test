# AUTH-UI.4 — Écran Erreurs d'inscription · RegisterErrorScreen (split + redesign visuel)

## Dépendances
- AUTH-UI.1 (socle commun)
- AUTH-UI.3 (Register opérationnel — transition Register↔Error)

## Objectif

1. **Split** : déplacer dans `AuthScreenError.cpp` les méthodes relatives à `Phase::Error`.
2. **Split renderer** : implémenter `AuthImGuiError.cpp` aligné sur la maquette `RegisterErrorScreen` (écran 4 de `Screens1to4.jsx`).
3. **Redesign visuel** : pastilles de type d'erreur, banner error/warning, encart "Champ à corriger", encart "Comment corriger", bouton "Réessayer" conditionnel.

---

## Périmètre fonctionnel (Phase::Error)

### Méthodes presenter → `engine/client/auth/screens/AuthScreenError.cpp`

| Méthode |
|---|
| `ImGuiAcknowledgeErrorScreen()` — retour au formulaire depuis l'écran d'erreur |
| `BuildModel_Error()` **(nouvelle méthode privée)** |
| *(pas d'Update_Error : l'écran error est navigué uniquement via ImGui callbacks)* |

`Phase::Error` est entré via `EnterAuthErrorPhase()` (dans Core) — cette méthode n'est pas déplacée.

---

## Cible visuelle (RegisterErrorScreen — Screens1to4.jsx)

### Structure globale

```
ln-stage
  ln-stage-col (max 640px)
    Panel
      header: "Inscription impossible"  |  badge "Erreur"  |  icône "i"
      body:
        [Sélecteur pastilles — démo uniquement, production: non affiché]
        Banner (error ou warning selon type)
          title: e.title    ex. "Identifiant déjà pris"
          body:  e.msg      ex. "Un aventurier porte déjà ce nom."
        [si e.field]
          encart "Champ à corriger"
            label UPPERCASE muted : "Champ à corriger"
            valeur accent display : e.field  ex. "Identifiant"
        encart "Comment corriger" (bordure gauche ln-accent)
          label UPPERCASE accent : "Comment corriger"
          texte italic body : e.fix
        ln-actions:
          Button ghost/md "Retour au formulaire"  keycap="Échap"
          [si network] Button primary/md "Réessayer"
```

### Mapping RenderModel → ImGui

| RenderModel | ImGui |
|---|---|
| `authErrorRichRegisterLayout` | true → afficher la mise en page riche |
| `authErrorPanelTitle` | Titre panel |
| `authErrorVersionBadge` | Badge ("Erreur") |
| `authErrorPanelSubtitle` | Sous-titre panel |
| `authRegisterErrorVariants` | Liste des variantes d'erreur |
| `authRegisterErrorClassifiedIndex` | Index de la variante active |
| `authErrorBannerBodyFromUserMessage` | Si true → corps banner = `errorText` (message serveur) |
| `authErrorHideFieldBox` | Si true → masquer l'encart "Champ à corriger" |
| `authErrorFieldSectionLabel` | Label de section "Champ à corriger" |
| `authErrorFixSectionLabel` | Label de section "Comment corriger" |
| `authErrorBackButtonLabel` | Libellé bouton retour |
| `authErrorBackKeycap` | Badge keycap retour |
| `authErrorRetryButtonLabel` | Libellé bouton réessayer |
| `authErrorShowRetryButton` | Afficher/masquer bouton réessayer |
| `errorText` | Message d'erreur brut (utilisé si `authErrorBannerBodyFromUserMessage`) |

### 4 types d'erreur (AuthRegisterErrorVariantRow)

| Cas | pillLabel | warningBanner | fieldLabel | fix |
|---|---|---|---|---|
| Identifiant pris | "Identifiant déjà pris" | false | "Identifiant" | Variante / suffixe |
| Mot de passe faible | "Mot de passe trop faible" | false | "Mot de passe" | Règles de complexité |
| Courriel invalide | "Courriel invalide" | false | "Adresse courriel" | Format attendu |
| Réseau | "Serveur injoignable" | **true** | *(vide → encart masqué)* | Réessayer / status |

### Implémentation ImGui des encarts

**Encart "Champ à corriger"** :
```cpp
// Fond sombre semi-opaque, bordure ln-border, radius-md
ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(10,13,18,128));
// Label : font UI, uppercase, tracking, ln-muted
// Valeur : font display, bold, uppercase, ln-accent
```

**Encart "Comment corriger"** (bordure gauche dorée) :
```cpp
// Barre verticale 3px ln-accent à gauche via ImDrawList::AddRectFilled
// Fond : rgba(232,197,110, 0.04)
// Label : font UI, uppercase, ln-accent
// Texte : font body, italic, ln-text
```

**Banner** (via `DrawAuthBanner()` de AuthImGuiCommon) :
- `warningBanner=true` → `kind=warning` (icône `!`, fond ambre)
- `warningBanner=false` → `kind=error` (icône `✕`, fond rouge sombre)

---

## Livrables

**Créés / Complétés :**
- `engine/client/auth/screens/AuthScreenError.cpp`
- `engine/render/auth/screens/AuthImGuiError.cpp`

**Modifiés :**
- `engine/client/AuthUi.h` — `BuildModel_Error()`
- `engine/client/auth/AuthUiPresenterCore.cpp` — dispatch

---

## Definition of Done

- [ ] Build Windows OK
- [ ] Écran d'erreur s'affiche depuis Register (bouton "Voir les erreurs") ET depuis toute transition `EnterAuthErrorPhase`
- [ ] Banner error/warning selon `warningBanner`
- [ ] Encart "Champ à corriger" visible si `fieldLabel` non vide, masqué sinon
- [ ] Encart "Comment corriger" avec bordure gauche accent
- [ ] Bouton "Réessayer" visible uniquement si `authErrorShowRetryButton`
- [ ] Bouton "Retour au formulaire" → retour à `m_errorReturnPhase`
- [ ] Aucune régression Register / Login
- [ ] Rapport final
