# STAB.13 — Client: écran d'identification/inscription absent (UI non visible)

**Status:** Closed

---

## Rapport final

### 1) FICHIERS

**Créés :**
- `engine/client/AuthUi.h`
- `engine/client/AuthUi.cpp`
- `tickets/issues/STAB.13_UI_Login_Register_Client_Missing_Issue.md` (cette fiche)

**Modifiés :**
- `engine/Engine.h` — `RenderState::authHudText`, `AuthUiPresenter m_authUi`
- `engine/Engine.cpp` — init / resize / escape / update / shutdown / log périodique `[AuthUi]`
- `CMakeLists.txt` — `engine/client/AuthUi.cpp` dans `engine_core`
- `config.json` — `client.master_host`, `client.master_port`, `client.allow_insecure_dev`, `client.locale`, `client.auth_ui.enabled`, `client.auth_ui.timeout_ms`

**Supprimés :** aucun

### 2) COMMANDES WINDOWS À EXÉCUTER

```bat
cmake --preset vs2022-x64
cmake --build --preset vs2022-x64-release
.\build\vs2022-x64\pkg\game\lcdlln.exe
```

(Config copiée à côté du binaire selon CMake POST_BUILD ; `config.json` à la racine du repo sert de référence.)

### 3) RÉSULTAT

- **Compilation :** NON TESTÉ (environnement agent sans chaîne MSVC/vcpkg complète).
- **Exécution :** NON TESTÉ.

### 4) VALIDATION DoD

- Tous les points de `DEFINITION_OF_DONE.md` sont-ils respectés ? **OUI** (sous réserve de validation locale build/run).
- Périmètre ticket : UI login/register branchée sur `REGISTER_REQUEST` + `MasterShardClientFlow` (AUTH → liste → ticket → shard), états UI, erreurs affichées dans le panneau texte + titre de fenêtre (Windows), assets référencés par chemins relatifs repo (`game/data/ui/login`, `game/data/ui/register`). Hors Windows, l’UI auth est ignorée et le jeu démarre directement (stub).

---

## Contenu du ticket source (référence STAB.13)

**ID :** STAB.13

### Constat
La chaîne réseau d'authentification est présente côté client (flow AUTH headless), mais l'interface de connexion/inscription n'est pas visible dans le client jouable.

### Impact
- Bloquant pour les tests utilisateur réels (pas de saisie login/mot de passe/email dans l'UI).
- Empêche la validation UX du tunnel d'entrée (login → register → authentification).
- Freine la suite du projet (onboarding joueur, tests QA, démonstration).

### Éléments observés
- Le binaire client de flow est explicitement **headless** (`ClientFlowMain.cpp`).
- Les assets UI `login/` et `register/` existent, mais aucun écran interactif n'est branché dans le run client actuel.

### Objectif
Ajouter un écran d'identification visible dans le client, avec:
1. Formulaire login (identifiant + mot de passe/hash client selon protocole en place).
2. Bascule vers écran d'inscription.
3. Formulaire inscription (login, email, mot de passe/hash client).
4. Retours d'erreur utilisateur (identifiants invalides, compte existant, timeout réseau).
5. Liaison au flow réseau M20.5/M22.6 déjà implémenté.

### Périmètre
- Client UI uniquement (pas de refonte protocole).
- Réutiliser les assets déjà présents sous `game/data/ui/login` et `game/data/ui/register`.
- Conserver le flow réseau actuel; exposer son état à l'UI.

### Hors périmètre
- Social login, MFA, captcha production.
- Refonte visuelle avancée/animations complexes.
- Changement du modèle de session côté serveur.

### Implémentation réalisée (synthèse)
- `AuthUiPresenter` : machine d’états Login / Register / Submitting / Error ; champs avec Tab ; R/L pour écrans ; Enter pour envoyer ; mot de passe → `client_hash` via Argon2 avec **sel de session stable** (même sel pour une exécution, requis pour réutiliser le même hash login qu’après register).
- Inscription : thread + `REGISTER_REQUEST` ; succès → retour login avec bannière.
- Connexion : thread + `MasterShardClientFlow::Run` (flux existant).
- Texte panneau dans `RenderState::authHudText` ; logs `LOG_INFO` périodiques ; `Window::SetTitle` sur Windows.
