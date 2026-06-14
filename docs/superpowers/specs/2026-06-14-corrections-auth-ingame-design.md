# Spec 1 — Corrections auth / in-game + mail CGU (Lots A, B, C)

Date : 2026-06-14
Statut : design validé (sections A, B, C)
Périmètre : client (Vulkan/ImGui) + serveur master (mail CGU)

## Contexte

Séance de correction de bugs et d'ajustements regroupant 7 items répartis en
trois lots. Le Lot D (prefab cimetière/auberge dans l'éditeur) fait l'objet
d'une spec séparée (`2026-06-14-prefab-cimetiere-auberge-editeur-design.md`).

Chaque item ci-dessous indique son impact de déploiement selon les règles
`CLAUDE.md` (client-only vs redéploiement serveur).

---

## Lot A — Corrections rapides

### A1. Suppression des raccourcis Ctrl+R / Ctrl+O / Ctrl+F (écran login)

**Problème** : depuis l'écran login, les raccourcis Ctrl+R (register), Ctrl+F
(mot de passe oublié) et Ctrl+O (options) sont actifs et indésirables.

**Localisation** : `Update_LoginShortcuts`,
`src/client/auth/screens/AuthScreenLogin.cpp:130-154`.
- Ctrl+R → `ImGuiNavigateToRegisterFromLogin` (ligne 142-144)
- Ctrl+F → `ImGuiOpenForgotPasswordPortal` (ligne 146-148)
- Ctrl+O → `OpenLanguageOptions` (ligne 150-152)

**Solution** : retirer les trois branches de raccourci. Les actions restent
accessibles via les boutons à l'écran (Register, lien mot-de-passe oublié,
bouton OPTIONS ligne 201). Si la fonction `Update_LoginShortcuts` devient vide,
la supprimer ainsi que son site d'appel.

**Critère de réussite** : sur l'écran login, Ctrl+R/O/F ne déclenchent plus
aucune action ; les boutons équivalents fonctionnent toujours.

**Déploiement** : ✅ client uniquement.

### A2. Touche Enter sur l'écran de login

**Problème** : Enter ne soumet pas le formulaire de login (aucun flag
`EnterReturnsTrue`, aucun handler `IsKeyPressed(Enter)`).

**Localisation** :
- Champs : `DrawAuthGoldField`, `src/client/render/AuthImGuiRenderer.cpp:728-752`
  (pas de `ImGuiInputTextFlags_EnterReturnsTrue`).
- Écran login ImGui : `src/client/render/auth/screens/AuthImGuiLogin.cpp`.
- Pattern de référence (déjà correct) :
  `src/client/render/auth/screens/AuthImGuiVerifyEmail.cpp:276`.

**Solution** : ajouter dans l'écran login un handler
`ImGui::IsKeyPressed(ImGuiKey_Enter, false)` qui déclenche la **même** action que
le bouton « Se connecter », sous conditions :
- champs identifiant + mot de passe non vides,
- pas de soumission déjà en cours.

Réutiliser le pattern de `AuthImGuiVerifyEmail.cpp:276`. Ne pas réactiver les
chips de légende masquées (ligne 163-165) — comportement clavier seulement.

**Critère de réussite** : sur le login, Enter soumet le formulaire comme un clic
sur « Se connecter ».

**Déploiement** : ✅ client uniquement.

### A3. Ciblage TAB — filtre frustum caméra + correction de la sélection

**Problème** : la touche TAB ne sélectionne pas les ennemis ; et même corrigée,
elle ne doit pas pouvoir cibler un ennemi non visible (dans le dos du joueur).

**Localisation** : `src/client/app/Engine.cpp:10685-10717`.
- TAB : `m_input.WasPressed(Key::Tab)` (ligne 10686).
- Candidats collectés sur `uiModel.remoteEntities` (ligne 10690), filtrés par
  archétype/état (lignes 10692-10693).
- Sélection par distance XZ au carré + `std::sort` (lignes 10697-10701).
- **Aucun filtre de champ de vision.**

**Solution** :
1. **Diagnostic** de la raison pour laquelle TAB ne sélectionne rien
   actuellement (filtre archétype trop strict ? liste `remoteEntities` vide à ce
   point ? sélection non propagée à l'UI cible / au modèle de ciblage ?), puis
   correction de la cause racine. À faire via `superpowers:systematic-debugging`
   avant tout correctif.
2. **Filtre frustum** : avant d'ajouter un ennemi aux candidats, projeter sa
   position monde via la matrice view-projection du rendu (même matrice que le
   pipeline de rendu). Garder l'ennemi uniquement si :
   - profondeur positive (devant la caméra),
   - NDC x ∈ [-1, 1] et NDC y ∈ [-1, 1] (dans le frustum / visible à l'écran).
   Réutiliser `Mat4::PerspectiveVulkan` et la matrice vue caméra déjà
   disponibles dans `Engine`.

**Critère de réussite** : TAB sélectionne l'ennemi visible le plus proche ;
cycle entre les ennemis **visibles à l'écran** ; un ennemi hors écran / dans le
dos n'est jamais sélectionné.

**Déploiement** : ✅ client uniquement.

### A4. Saccadement de la course avec ALT

**Problème** : courir avec SHIFT est fluide ; avec ALT (sprint) il y a un
saccadement.

**Cause** : ALT (VK_MENU = 0x12) émet `WM_SYSKEYDOWN` / `WM_SYSKEYUP` au lieu de
`WM_KEYDOWN` / `WM_KEYUP`, et déclenche l'activation du menu système Windows
(boucle modale interne de `DefWindowProc`), d'où des frames perdues.

**Localisation** :
- Touche sprint par défaut « Alt » : `src/client/app/Engine.cpp:9068`.
- `BuildMoveInput` : `run = Shift`, `sprint = sprintKey`,
  `src/client/app/Engine.cpp:470-526` (extraits 516).
- Capture bas niveau : `src/shared/platform/Input.cpp:25-50`
  (`WM_KEYDOWN` / `WM_SYSKEYDOWN`).

**Solution** (ALT conservé comme touche configurable) :
1. Dans la window proc : intercepter `WM_SYSCOMMAND` avec
   `wParam == SC_KEYMENU` (et `WM_SYSKEYDOWN` pour `VK_MENU`) et retourner 0
   pour **bloquer l'activation du menu système** — supprime la boucle modale.
2. Vérifier le traitement **symétrique** du keyup : `WM_SYSKEYUP` doit relâcher
   la touche au même titre que `WM_KEYUP` dans `Input.cpp` (sinon ALT reste
   « collé »).

**Critère de réussite** : courir avec ALT est aussi fluide qu'avec SHIFT ;
aucune frame perdue à l'appui/relâchement d'ALT ; ALT reste rebindable.

**Déploiement** : ✅ client uniquement.

---

## Lot B — Refonte UI

### B1. Thèmes de race sur l'écran d'auth (recolor + persistance)

**Problème** : les boutons de thème de race changent l'état sélectionné
visuellement mais n'appliquent aucune couleur.

**Localisation** : `DrawAuthTweaksPanel`,
`src/client/render/AuthImGuiRenderer.cpp:966-1091`. Le clic ne fait que
`m_langTweakRace = idx` (ligne 1018-1042). Le système de thème runtime est
`LnTheme` (cf. combo Options `AuthImGuiOptions.cpp:414-435`, persistance
`ui_theme.json`).

**Code couleur (source de vérité)** :

| Race | Dominante (hex) | Accent (hex) |
| --- | --- | --- |
| Humains | `#4A6FA5` | `#C0C8D0` |
| Orkhs (Dzorak) | `#5A6B3B` | `#D8CFA8` |
| Démons | `#8B1A1A` | `#FF5722` |
| Nains | `#8C5A2B` | `#D4A017` |
| Elfes | `#5E4B8B` | `#C9BCE0` |

**Solution** :
1. Enregistrer dans le système `LnTheme` **5 thèmes de race** + un « défaut » :
   - dominante → couleur primaire du thème (fond/cadres principaux),
   - accent → couleur d'accent (bordures, surbrillance, sélection).
   - Noms i18n cohérents avec l'existant : `options.interface.theme.<id>`
     (ex. `humains`, `orkhs`, `demons`, `nains`, `elfes`, `defaut`).
2. Câbler les boutons `DrawAuthTweaksPanel` : un clic appelle
   `LnTheme::SetActive(<raceThemeId>)` → recolor **live** de l'écran d'auth.
3. **Persistance** : écrire le choix dans `ui_theme.json` (même pipeline que le
   combo Options), conservé entre sessions.

**Effet de bord assumé** : le thème étant global (`ui_theme.json`), il s'applique
aussi à l'UI en jeu — cohérence voulue.

**Critère de réussite** : cliquer un bouton de race recolore immédiatement l'UI
auth avec les bonnes couleurs ; le choix survit à un redémarrage du client.

**Déploiement** : ✅ client uniquement.

### B2. Unification du menu d'options

**Contrainte explicite** : ne pas toucher au menu Pause ouvert par ESC. Seul son
lien « Options » doit ouvrir le bon écran d'options (celui de l'auth) au lieu du
mini-panneau actuel.

**Localisation** :
- Menu options auth complet : `RenderOptionsScreen`,
  `src/client/render/auth/screens/AuthImGuiOptions.cpp:39+` (7 onglets :
  Graphics, Audio, Controls, Language, UI, Network, Account).
- Bouton « Options » du menu Pause : `src/client/app/Engine.cpp:11959-11962`
  (active `m_inGameOptionsPanelVisible`).
- Mini-panneau in-game à supprimer : `src/client/app/Engine.cpp:11982-12144`.

**Solution** :
1. Extraire `RenderOptionsScreen` en composant réutilisable, appelable depuis
   l'auth **et** depuis `Engine` (in-game), avec persistance config unifiée
   (`ui_theme.json` + config / keybinds, sans double chemin).
2. Rewire le bouton « Options » du menu Pause pour ouvrir cet écran complet
   (tous les onglets), en overlay au-dessus du jeu.
3. Supprimer le mini-panneau in-game (`Engine.cpp:11982-12144`) et l'état
   `m_inGameOptionsPanelVisible` associé.

**Critère de réussite** : en jeu, le bouton « Options » du menu Pause ouvre le
même écran d'options que l'auth ; le menu Pause (ESC) lui-même est inchangé ;
plus aucune trace du mini-panneau.

**Déploiement** : ✅ client uniquement.

---

## Lot C — Mail de validation des CGU (revue de code)

**Problème** : le mail d'inscription fonctionne ; le mail de validation des CGU
déclenché depuis l'interface du jeu ne part toujours pas.

**Flux qui fonctionne (inscription)** :
- `src/masterd/handlers/auth/AuthRegisterHandler.cpp:205-238`
  (envoi après `CreateAccount`, email fraîchement fourni).

**Flux défaillant (CGU)** :
- Client : `AuthScreenTerms.cpp:307` envoie `kOpcodeTermsAcceptRequest`
  (opcode 31) via `StartTermsAcceptWorker` (273-428), payload
  `editionId + acknowledged=1`.
- Dispatch master : `src/masterd/main_linux.cpp:1115-1116`.
- Handler : `src/masterd/handlers/terms/TermsHandler.cpp:104-167` — enregistre
  l'acceptation puis envoie le mail si
  `m_smtp && !m_smtp->host.empty() && m_accounts && ar && !ar->email.empty()`
  (lignes 143-162).

**Méthode** : `superpowers:systematic-debugging` par revue de code (pas de
runtime), hypothèses par ordre de suspicion :
1. **Session / account_id non résolu depuis l'interface du jeu** — en jeu le
   client dialogue principalement avec le shard ; l'opcode CGU va au master qui
   doit disposer d'une **session master avec account_id**
   (`TermsHandler.cpp:123-128`, `SessionManager`). Si cette session n'existe pas
   dans le contexte in-game, `account_id` est absent → envoi silencieusement
   sauté. (Hypothèse la plus probable.)
2. **Dépendances non injectées** dans `TermsHandler` (`m_accounts`, `m_smtp`)
   vs `AuthRegisterHandler` — vérifier le câblage `main_linux.cpp:290` (auth) et
   `:352` (terms) : même instance `&smtpConfig`, et `SetAccountStore`/équivalent
   bien appelé sur `termsHandler`.
3. **Email vide en base** au lookup `FindByAccountId` (`TermsHandler.cpp:145`).

**Livrable** : cause racine identifiée + correctif. Ajout de logs explicites aux
points de décision (`email vide`, `session absente`, `échec SMTP`) si utile au
diagnostic futur.

**Déploiement** : ⚠️ **redéploiement master probable** (le correctif touche
vraisemblablement un handler/câblage serveur). À confirmer une fois la cause
identifiée ; la mention exacte sera donnée dans la PR.

---

## Stratégie de PR et ordre de merge

**Décision utilisateur : une seule grosse PR** regroupant les Lots A, B et C
(les 7 items A1-A4, B1-B2, C), plutôt que plusieurs petites PR.

- La PR contient les changements client (Lots A et B) **et** serveur master
  (Lot C, mail CGU).
- Comme elle inclut un correctif serveur (Lot C), elle est **à déployer en
  lock-step** : client neuf + master neuf ensemble si le correctif touche le
  wire ou un handler (à confirmer une fois la cause C identifiée).
- Implémentation interne ordonnée (B2 = refacto options en premier car la plus
  structurante), mais **tout livré dans la même PR**.
- Le Lot D (prefab éditeur) reste une PR/feature distincte (spec séparée).

## Hors périmètre

- Lot D (prefab cimetière/auberge éditeur) — spec séparée.
- Overrides de thème par écran (le thème reste global).
- Refonte du menu Pause lui-même (interdit explicitement).

## Tests

- A1/A2/A4/B1/B2 : vérification manuelle en jeu (pas de toolchain locale ;
  compilation via CI/VS). A3 et Lot C : revue + tests unitaires si un point de
  logique se prête à un test sans handler runtime.
- CI : `build-windows` (client) ; `build-linux` exécute `ctest` (Lot C serveur).
