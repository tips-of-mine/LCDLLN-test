# B2 — Unification du menu d'options (réutiliser l'écran auth en jeu) — Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps en cases à cocher.

**Goal :** Le bouton « Options » du menu Pause en jeu ouvre le **même** écran d'options que l'auth (source unique). Le mini-panneau in-game est supprimé. Le menu Pause (ESC) lui-même reste inchangé.

**Architecture (Approche B — pipeline réutilisable) :** garder le presenter auth comme source unique de vérité, en levant 3 verrous qui ferment l'écran options dès `m_flowComplete` (entrée en jeu) :
1. **Consume découplé** : l'application réelle des réglages (aujourd'hui sous `authGateActive`) tourne chaque frame.
2. **Contexte d'options** (`m_optionsOpenInGame`) : assouplit les gardes `Phase` de l'apply/close, en neutralisant le `SetPhase` que déclenche `ApplyLocaleSelection` (sinon corruption session).
3. **Wrapper de rendu** `RenderOptionsOverlay()` : rend l'écran options sans dépendre de `vs.active`, sans le décor « THÈME DE RACE », appelé en jeu à la place du mini-panneau.

**Décisions produit validées (2026-06-14) :**
- En jeu : **masquer les onglets Network et Account** (risque : déconnexion gameplay / actions de compte destructrices).
- **Remap des touches éditable partout** (auth + en jeu) : porter le rebind interactif du mini-panneau dans l'onglet Controls unifié.

**Contraintes :** pas de toolchain locale (build via CI ; items in-game non couverts par ctest → validation manuelle en jeu **indispensable** par l'utilisateur après chaque étape risquée). Réponses/commits FR, code EN. Conventions repo (PascalCase nouveau code, commentaires FR).

**Déploiement :** ✅ 100 % client (UI/options/input). Pas de redéploiement serveur.

**Branche :** `feat/b2-menu-options-unifie` (depuis `main`). Coordination merge : indépendante de la PR #901 (B1) ; régions différentes mais fichiers communs (`Engine.cpp`, `AuthImGuiRenderer.{h,cpp}`) → si #901 merge avant, rebaser B2.

---

## Carte de référence (architecte — fichier:ligne)

- Entrée options auth : `AuthUiPresenterSettings.cpp:125-165` `OpenLanguageOptions()` (sauve `m_phaseBeforeOptions`, `SetPhase(LanguageOptions)`, init des `*Pending` depuis les valeurs live `:136-155`).
- Mirrors UI : `AuthImGuiRenderer::PullLanguageOptionsFromPresenter()` `AuthImGuiRenderer.cpp:224-262`, source `BuildLanguageOptionsImGuiMirror()` `AuthUiPresenterSettings.cpp:54-79`.
- Apply : `AuthImGuiOptions.cpp:605-608` → `submitOptionsMirror()` `AuthImGuiOptions.cpp:257-287` → `ImGuiApplyLanguageOptionsMenu()` `AuthUiPresenterSettings.cpp:81-114` (**garde `:83`**) → `CommitLanguageOptionsMenuApply()` `AuthUiPresenterSettings.cpp:213-257` (remplit les 4 commandes pending `:236-255`, puis `ApplyLocaleSelection(false)` `:256`).
- `ApplyLocaleSelection(false)` : `AuthScreenLanguageSelect.cpp:40-80` — **`:78 SetPhase(m_phaseBeforeOptions)` ⚠️ ferme l'écran / change la phase**.
- Consume (application réelle) : `Engine.cpp:8356-8439`, sous `authGateActive = IsInitialized() && !IsFlowComplete()` `:8356`. Consomme `ConsumePendingVideo/Audio/Control/GameSettings` (`AuthUiPresenterSettings.cpp:260-265,656-675`, idempotents : `{}` si pas `applyRequested`).
- Close sans appliquer : `AuthUiPresenterSettings.cpp:116-124` (**garde `:118`**, `:122 m_phase = m_phaseBeforeOptions`).
- Overlay rendu : `AuthImGuiRenderer::Render()` `:409` (`:411 if (!vs.active) return;`), `BeginFullscreenOverlay` `:499-518`, dispatch options `:461-464`, `End` `:496`. `vs.active = m_initialized && !m_flowComplete && m_authEnabled` `AuthUiPresenterCore.cpp:4426`.
- `RenderOptionsScreen(const RenderModel&, ...)` : `AuthImGuiOptions.cpp:39`. N'utilise du RenderModel que `rm.authOptionsAccountLogin` `:501` et `rm.authOptionsAccountTagId` `:505` (remplis `AuthScreenOptions.cpp:98-99`). Tout le reste via `m_authPresenter` (toujours vivant en jeu). Appelle `DrawAuthTweaksPanel` en fin `:621` (décor à NE PAS afficher en jeu).
- Onglets : Graphics `:294-332`, Audio `:333-340`, Controls `:341-366` (keybinds **lecture seule** `:357-365`), Language `:367-398`, UI `:399-443` (thème live `:416-442`), Network `:444-489`, Account `:490-532`.
- Mini-panneau in-game (à supprimer) : `Engine.cpp` `if (m_inGameOptionsPanelVisible)` ~`11978-12194` ; rebind interactif `~12057-12143` (`m_rebindingAction`, `kRebindableKeys`, `m_keybindWarning`) ; effets live (`m_audioEngine.SetMasterVolume` `~12011`).
- Menu Pause (NE PAS modifier) : `Engine.cpp` `if (m_inGamePauseMenuVisible)` ~`11888-11977` ; bouton « Options » `~11959-11963` (pose `m_inGameOptionsPanelVisible=true`).
- NewFrame ImGui in-game inclut déjà `|| m_inGameOptionsPanelVisible` `Engine.cpp:~8898-8904`.

---

## ST1 — Découpler le consume des réglages (additif, faible risque)

**Files:** `src/client/app/Engine.cpp`

**But :** l'application réelle des réglages stagés doit tourner **chaque frame**, pas seulement sous `authGateActive`, pour fonctionner aussi en jeu. Les `ConsumePending*` sont idempotents (retournent `{}` sans `applyRequested`), donc l'appel inconditionnel est sûr.

- [ ] **Step 1 — Lire** `Engine.cpp:8356-8439` (bloc `if (authGateActive) { ConsumePending... + application }`) en entier.
- [ ] **Step 2 — Extraire** le corps (consume des 4 commandes + leur application : vidéo `:8366-8401`, audio `:8402-8415`, contrôles `:8416-8423`, jeu `:8424-8439`) dans une méthode privée `Engine::ApplyConsumedSettingsCommands()` (déclarée dans `Engine.h`, documentée `///`).
- [ ] **Step 3 — Appel inconditionnel** : remplacer le bloc `if (authGateActive) {…}` par un appel `ApplyConsumedSettingsCommands();` **chaque frame** (en dehors de la garde `authGateActive`). Vérifier qu'aucune variable locale du bloc ne dépendait de `authGateActive`.
- [ ] **Step 4 — Garde anti-déconnexion** : dans la partie « jeu » du consume (`InitGameplayNet/ShutdownGameplayNet` `:8432-8435`), n'exécuter ces (ré)initialisations réseau **que si on n'est pas déjà en session** (ou ne pas les déclencher quand l'onglet Network est masqué en jeu — cf. ST6). Documenter le garde-fou.
- [ ] **Step 5 — Commit** `refactor(client): consume des réglages options exécuté chaque frame (réutilisable en jeu)`.

**Vérif (CI + manuelle) :** à l'auth, changer un réglage + Appliquer fonctionne comme avant (aucune régression). En jeu (avec le mini-panneau encore présent), rien ne casse.

**Risque :** moyen. Bien vérifier qu'aucun réglage ne s'applique deux fois (live + staged).

---

## ST2 — Contexte d'options (lève le verrou Phase)

**Files:** `src/client/auth/AuthUiPresenterSettings.cpp`, `src/client/auth/screens/AuthScreenLanguageSelect.cpp`, header presenter (`AuthUiPresenter.h`).

- [ ] **Step 1 — Membre** : ajouter `bool m_optionsOpenInGame = false;` au presenter (header), documenté.
- [ ] **Step 2 — Assouplir les gardes** : dans `ImGuiApplyLanguageOptionsMenu` (`:83`) et `ImGuiCloseLanguageOptionsWithoutApply` (`:118`), remplacer `if (m_phase != Phase::LanguageOptions) return;` par `if (m_phase != Phase::LanguageOptions && !m_optionsOpenInGame) return;`.
- [ ] **Step 3 — Neutraliser le `SetPhase`** : dans `CommitLanguageOptionsMenuApply` (`:256`) et le close (`:122`), **ne pas** restaurer la phase quand `m_optionsOpenInGame` (sinon corruption de l'état auth post-monde). Pour l'apply : extraire l'application de locale de `ApplyLocaleSelection(false)` du `SetPhase` (`AuthScreenLanguageSelect.cpp:78`) — n'appeler `SetPhase(m_phaseBeforeOptions)` que si `!m_optionsOpenInGame`.
- [ ] **Step 4 — Commit** `feat(client): contexte options in-game (gardes Phase assouplies sans toucher la phase auth)`.

**Risque :** ⚠️ ÉLEVÉ (zone session/phase). Ne JAMAIS modifier `m_phase`/`m_flowComplete` quand `m_optionsOpenInGame`. Validation en jeu obligatoire (ouvrir/fermer options ne doit pas faire réapparaître l'écran auth ni déconnecter).

---

## ST3 — Ouverture/fermeture des options in-game (presenter)

**Files:** `src/client/auth/AuthUiPresenterSettings.cpp` (+ header).

- [ ] **Step 1 — `OpenLanguageOptionsInGame()`** : nouvelle méthode publique qui pose `m_optionsOpenInGame = true` et **initialise les `*Pending` depuis les valeurs live** (réutiliser le corps `OpenLanguageOptions():136-155`) **sans** `SetPhase` ni toucher `m_phaseBeforeOptions`. Documenter.
- [ ] **Step 2 — `CloseLanguageOptionsInGame()`** : pose `m_optionsOpenInGame = false` (sans toucher la phase).
- [ ] **Step 3 — Accès mirrors** : s'assurer que `BuildLanguageOptionsImGuiMirror()` fonctionne dans ce contexte (il lit les `*Pending`, déjà initialisés au Step 1).
- [ ] **Step 4 — Commit** `feat(client): ouverture/fermeture des options en jeu sans transition de phase`.

**Risque :** moyen.

---

## ST4 — Wrapper de rendu `RenderOptionsOverlay` (lève le verrou rendu)

**Files:** `src/client/render/AuthImGuiRenderer.{h,cpp}`, `src/client/render/auth/screens/AuthImGuiOptions.cpp`.

- [ ] **Step 1 — Méthode publique** `bool AuthImGuiRenderer::RenderOptionsOverlay(float vpW, float vpH)` documentée (`///` : rôle, effet de bord ImGui, thread main). Elle :
  - ouvre une fenêtre overlay propre (réutiliser `BeginFullscreenOverlay` ou une fenêtre centrée modale, **indépendante de `vs.active`**),
  - appelle `PullLanguageOptionsFromPresenter()` à la première frame d'ouverture (init mirrors),
  - appelle `RenderOptionsScreen(rmMinimal, vpW, vpH)` avec un **RenderModel minimal** (au moins `authOptionsAccountLogin`/`TagId` vides — l'onglet Account étant masqué en jeu, cf. ST6),
  - **ne** rend **pas** `DrawAuthTweaksPanel`,
  - renvoie `true` tant qu'ouvert, `false` quand l'utilisateur ferme (Retour/Échap) — via le close in-game (ST3) ou un retour de `RenderOptionsScreen`.
- [ ] **Step 2 — Paramétrer `RenderOptionsScreen`** pour : (a) sauter `DrawAuthTweaksPanel` (`:621`) en contexte in-game, (b) signaler la fermeture au caller. Ajouter un paramètre `bool inGame` (ou un membre `m_optionsInGame`).
- [ ] **Step 3 — Commit** `feat(client): RenderOptionsOverlay réutilisable (écran options sans décor auth)`.

**Risque :** moyen.

---

## ST5 — Câblage Engine (remplacer le mini-panneau)

**Files:** `src/client/app/Engine.cpp` (+ `Engine.h`).

- [ ] **Step 1 — Ouverture** : quand le bouton « Options » du menu Pause est cliqué (`~11959-11963`, **inchangé** : pose `m_inGameOptionsPanelVisible=true`), appeler `m_authUi.OpenLanguageOptionsInGame()` (au passage `false→true` du flag).
- [ ] **Step 2 — Rendu** : remplacer TOUT le bloc mini-panneau `if (m_inGameOptionsPanelVisible) { … }` (`~11978-12194`) par :
```cpp
if (m_inGameOptionsPanelVisible)
{
    const bool stillOpen = m_authImGui->RenderOptionsOverlay(dw, dh);
    if (!stillOpen)
    {
        m_inGameOptionsPanelVisible = false;
        m_authUi.CloseLanguageOptionsInGame();
        m_inGamePauseMenuVisible = true; // retour au menu Pause
    }
}
```
- [ ] **Step 3 — Suspension gameplay** : conserver le comportement existant (le gameplay est déjà suspendu quand le panneau est ouvert ; vérifier que les conditions référençant `m_inGameOptionsPanelVisible` restent valides, ex. NewFrame `~8898`).
- [ ] **Step 4 — Commit** `refactor(client): menu Pause Options ouvre l'écran d'options unifié (mini-panneau supprimé)`.

**Risque :** moyen-élevé. Validation en jeu : ESC → Pause inchangé ; Options → écran complet ; Retour → Pause ; réglages appliqués ; gameplay bien suspendu pendant l'ouverture.

---

## ST6 — Onglets sensibles + keybinds éditables

**Files:** `src/client/render/auth/screens/AuthImGuiOptions.cpp`.

- [ ] **Step 1 — Masquer Network + Account en jeu** : conditionner l'affichage des onglets Network (`:444-489`) et Account (`:490-532`) à `!inGame` (paramètre/membre du ST4). En jeu, n'afficher que Graphics/Audio/Controls/Language/UI. Adapter la barre d'onglets pour ne pas laisser d'onglet vide/sélectionnable.
- [ ] **Step 2 — Keybinds éditables (partout)** : remplacer l'affichage **lecture seule** des keybinds dans l'onglet Controls (`:357-365`) par le **rebind interactif** porté du mini-panneau (logique `Engine.cpp:~12057-12143` : capture touche, détection conflit, persistance `keybinds.json`). L'exposer à l'auth ET en jeu. Réutiliser/centraliser la liste `kRebindableKeys` (la déplacer dans un en-tête partagé si nécessaire ; éviter la duplication).
- [ ] **Step 3 — Commit** `feat(client): options unifiées — Network/Account masqués en jeu, remap touches éditable`.

**Risque :** moyen. Attention à la persistance keybinds (format `keybinds.json` existant).

---

## ST7 — Nettoyage du code mort du mini-panneau

**Files:** `src/client/app/Engine.cpp`, `Engine.h`.

- [ ] **Step 1 — Vérifier** par `git grep` les symboles devenus inutilisés après ST5/ST6 : `m_rebindingAction`, `m_keybindWarning` (`Engine.h`). NE PAS retirer `kRebindableKeys` s'il reste utilisé (`Engine.cpp:~436/444` hors mini-panneau) ni `m_inGameOptionsPanelVisible` (toujours déclencheur).
- [ ] **Step 2 — Retirer** uniquement les symboles sans plus aucun usage.
- [ ] **Step 3 — Commit** `chore(client): retrait du code mort du mini-panneau options in-game`.

---

## ST8 — Intégration, CI, PR

- [ ] Revue locale (`git diff main..HEAD --stat`), push, CI verte (build-windows ~30 min).
- [ ] PR `feat: menu d'options unifié (écran auth réutilisé en jeu)`. Déploiement : ✅ client uniquement.
- [ ] Indiquer à l'utilisateur l'ordre de merge vs PR #901 (régions distinctes ; rebaser si #901 merge en premier).

## Self-review (couverture)
- Verrou Phase → ST2 ✅ ; Verrou consume → ST1 ✅ ; Verrou rendu → ST4 ✅ ; ouverture/fermeture in-game → ST3 ✅ ; câblage → ST5 ✅ ; onglets masqués + keybinds éditables → ST6 ✅ ; nettoyage → ST7 ✅.
- **Validation runtime en jeu indispensable** (pas de build local) : surtout après ST2 (phase/session) et ST5 (câblage).
