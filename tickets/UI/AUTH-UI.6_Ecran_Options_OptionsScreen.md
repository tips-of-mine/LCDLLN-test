# AUTH-UI.6 — Écran Options · OptionsScreen (split + redesign visuel)

## Dépendances
- AUTH-UI.1 (socle commun — `AuthUiPresenterSettings.cpp` déjà extrait)
- AUTH-UI.2 (Login — le bouton "⚙ Options" y est affiché)

## Objectif

1. **Split** : déplacer dans `AuthScreenOptions.cpp` les méthodes relatives à `Phase::LanguageOptions`.
2. **Split renderer** : implémenter `AuthImGuiOptions.cpp` aligné sur la maquette `OptionsScreen` (écran 6 de `Screens5to7.jsx`).
3. **Redesign visuel** : layout sidebar + panneau principal, 7 onglets (Graphismes/Son/Contrôles/Langue/Interface/Réseau/Compte), Slider, Toggle, Dropdown, KeybindRow, footer Appliquer/Annuler.

---

## Périmètre fonctionnel (Phase::LanguageOptions)

### Méthodes presenter → `engine/client/auth/screens/AuthScreenOptions.cpp`

| Méthode |
|---|
| `ImGuiOpenLanguageOptionsMenu()` |
| `BuildModel_Options()` **(nouvelle méthode privée)** |
| `Update_Options()` **(nouvelle méthode privée)** |

> `BuildLanguageOptionsImGuiMirror()`, `ImGuiApplyLanguageOptionsMenu()`, `ImGuiCloseLanguageOptionsWithoutApply()`, `CommitLanguageOptionsMenuApply()`, `OpenLanguageOptions()`, `OptionsSubmenuLineCount()`, `EnterOptionsSubmenuFromRoot()` sont déjà dans `AuthUiPresenterSettings.cpp` (AUTH-UI.1).

---

## Cible visuelle (OptionsScreen — Screens5to7.jsx)

### Structure globale — layout sidebar

L'écran Options abandonne le conteneur `ln-auth-panel` au profit d'un layout **plein écran** à deux colonnes :

```
ln-stage
  ln-options                          ← flex-row, plein écran
    ln-options-sidebar                ← colonne gauche fixe (~220px)
      ln-options-sidebar-title "Options"
      [7 onglets verticaux]
        onglet actif : fond semi-opaque, bordure gauche ln-accent
        icône  +  libellé
      [spacer flex-1]
      [note bas italic "Les changements prennent effet après « Appliquer »."]
    ln-options-main                   ← colonne droite, flex-1
      ln-options-main-header
        catégorie label UPPERCASE muted
        h2 nom catégorie (display font, uppercase)
        [Banner warning "Modifications non enregistrées" si dirty]
      ln-options-main-body            ← contenu de l'onglet actif
        [section title ln-options-section-title]
        [controls…]
      ln-options-footer
        Button ghost/md "Retour"  keycap="Échap"
        Button text/sm "Annuler"  (disabled si !dirty)
        Button primary/md "Appliquer"  (disabled si !dirty)
```

### 7 onglets et leurs contenus

#### Graphismes
- `DrawAuthDropdown()` "Résolution" : 1280x720, 1600x900, 1920x1080, 2560x1440, 3840x2160
- `DrawAuthDropdown()` "Qualité graphique" : Basse/Moyenne/Haute/Ultra + tooltip
- `DrawAuthSlider()` "Champ de vision" : 60→120, unité `°`
- Section "Modes"
- `DrawAuthToggle()` "Plein écran" + hint "Alt+Entrée…"
- `DrawAuthToggle()` "Synchronisation verticale" + hint

#### Son
- `DrawAuthSlider()` "Volume maître" 0→100 %
- `DrawAuthSlider()` "Musique" 0→100 %
- `DrawAuthSlider()` "Effets" 0→100 %
- `DrawAuthSlider()` "Voix" 0→100 %

#### Contrôles
- Section "Souris"
- `DrawAuthSlider()` "Sensibilité" 10→100 %
- `DrawAuthToggle()` "Inverser l'axe Y"
- `DrawAuthToggle()` "Disposition ZQSD (AZERTY)"
- Section "Raccourcis clavier"
- `DrawAuthKeybind()` — lignes `[Action] [Touche]` (9 entrées : Avancer/S/Q/D/Interagir/Sort1/Sort2/Inventaire/Carte)

#### Langue
- `DrawAuthDropdown()` "Interface et textes" : Français / English + tooltip "Redémarrage requis"

#### Interface
- `DrawAuthSlider()` "Taille de l'interface" 80→140 %
- `DrawAuthSlider()` "Opacité des panneaux" 40→100 %
- `DrawAuthToggle()` "Afficher les infobulles"

#### Réseau
- `DrawAuthDropdown()` "Serveur préféré" : Europe (Morneplaine) / Europe (Korvath) / Automatique
- Row latence courante : label "Latence actuelle" | valeur mono ln-success
- `DrawAuthToggle()` "Mode gameplay UDP" + hint "Protocole expérimental"

#### Compte
- Encart compte : [identifiant display accent] + [TAG-ID mono muted] + Button ghost "Copier"
- Section "Actions"
- `DrawAuthButton(kind=ghost)` "Changer le mot de passe"
- `DrawAuthButton(kind=ghost)` "Changer le courriel"
- `DrawAuthButton(kind=danger)` "Se déconnecter"

### Mapping mirror → ImGui

L'écran Options utilise `LanguageOptionsImGuiMirror` (construit via `BuildLanguageOptionsImGuiMirror()`) comme état local ImGui, puis applique via `ImGuiApplyLanguageOptionsMenu()` :

| Mirror field | Onglet | Control |
|---|---|---|
| `videoResWidth / videoResHeight` | Graphismes | Dropdown résolution |
| `videoQualityPreset` | Graphismes | Dropdown qualité (0=Basse…3=Ultra) |
| `videoFovDegrees` | Graphismes | Slider FOV |
| `videoFullscreen` | Graphismes | Toggle plein écran |
| `videoVsync` | Graphismes | Toggle vsync |
| `audioMaster01…audioUi01` | Son | Sliders volume (0.0–1.0 → 0–100%) |
| `mouseSensitivity` | Contrôles | Slider sensibilité (0.002=min…) |
| `invertY` | Contrôles | Toggle inverser Y |
| `useZqsd` | Contrôles | Toggle ZQSD |
| `languageSelectionIndex` | Langue | Dropdown langue |
| `gameplayUdpEnabled` | Réseau | Toggle UDP |
| `allowInsecureDev` | Réseau | Toggle (dev uniquement) |
| `authTimeoutMs` | Réseau | Slider timeout (2000→10000 ms) |

### Implémentation ImGui du layout sidebar

Le layout sidebar n'a **pas d'équivalent direct en ImGui**. Implémentation via :

```cpp
// Sidebar fixe à gauche
ImGui::BeginChild("opt_sidebar", ImVec2(220, -1), false);
  // Titre, onglets...
ImGui::EndChild();

ImGui::SameLine();

// Panneau principal
ImGui::BeginChild("opt_main", ImVec2(-1, -1), false);
  // Header, body, footer
ImGui::EndChild();
```

Les onglets de la sidebar utilisent `ImGui::Selectable()` avec un style personnalisé (bordure gauche via `ImDrawList::AddRectFilled` sur l'onglet actif).

### KeybindRow (raccourcis clavier)

Nouvelle primitive à ajouter dans `AuthImGuiCommon.cpp` :

```cpp
void DrawAuthKeybind(const char* actionName, const char* keyLabel);
// Affiche : [actionName ........ [keyLabel]]
// fond transparent, séparateur bas, police UI
```

### Indicateur "dirty" (modifications non enregistrées)

- `dirty = true` dès qu'un contrôle est modifié localement par rapport au mirror initial
- Banner warning visible dans le header du panneau principal
- Bouton "Annuler" et "Appliquer" activés si dirty
- "Appliquer" → `ImGuiApplyLanguageOptionsMenu()` puis `dirty = false`
- "Annuler" → recharger mirror depuis `BuildLanguageOptionsImGuiMirror()` + `dirty = false`

---

## Livrables

**Créés / Complétés :**
- `engine/client/auth/screens/AuthScreenOptions.cpp`
- `engine/render/auth/screens/AuthImGuiOptions.cpp`
- `engine/render/auth/AuthImGuiCommon.cpp` — ajout `DrawAuthKeybind()`, `DrawAuthSlider()`

**Modifiés :**
- `engine/client/AuthUi.h` — `BuildModel_Options()`, `Update_Options()`
- `engine/client/auth/AuthUiPresenterCore.cpp` — dispatch

---

## Definition of Done

- [ ] Build Windows OK
- [ ] Layout sidebar + panneau principal affiché plein écran (pas de ln-auth-panel)
- [ ] 7 onglets navigables ; onglet actif : fond + bordure gauche accent
- [ ] Graphismes : dropdown résolution + qualité + slider FOV + 2 toggles
- [ ] Son : 4 sliders volume
- [ ] Contrôles : 2 toggles + slider sensibilité + 9 lignes keybind
- [ ] Langue : dropdown avec 2 locales
- [ ] Interface : 2 sliders + 1 toggle
- [ ] Réseau : dropdown serveur + latence + toggle UDP
- [ ] Compte : encart TAG-ID + 3 boutons d'action
- [ ] Banner "Modifications non enregistrées" visible si dirty
- [ ] Appliquer → settings propagés via `ImGuiApplyLanguageOptionsMenu()`
- [ ] Annuler → reset mirror
- [ ] Retour → `ImGuiCloseLanguageOptionsWithoutApply()`
- [ ] Aucune régression Login / Register
- [ ] Rapport final
