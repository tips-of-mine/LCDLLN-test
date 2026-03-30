# STAB.13 — Client: écran d'identification/inscription absent (UI non visible)

**Status:** Closed (voir `tickets/issues/STAB.13_UI_Login_Register_Client_Missing_Issue.md`)

## Constat
La chaîne réseau d'authentification est présente côté client (flow AUTH headless), mais l'interface de connexion/inscription n'est pas visible dans le client jouable.

## Impact
- Bloquant pour les tests utilisateur réels (pas de saisie login/mot de passe/email dans l'UI).
- Empêche la validation UX du tunnel d'entrée (login → register → authentification).
- Freine la suite du projet (onboarding joueur, tests QA, démonstration).

## Éléments observés
- Le binaire client de flow est explicitement **headless** (`ClientFlowMain.cpp`).
- Les assets UI `login/` et `register/` existent, mais aucun écran interactif n'est branché dans le run client actuel.

## Objectif
Ajouter un écran d'identification visible dans le client, avec:
1. Formulaire login (identifiant + mot de passe/hash client selon protocole en place).
2. Bascule vers écran d'inscription.
3. Formulaire inscription (login, email, mot de passe/hash client).
4. Retours d'erreur utilisateur (identifiants invalides, compte existant, timeout réseau).
5. Liaison au flow réseau M20.5/M22.6 déjà implémenté.

## Périmètre
- Client UI uniquement (pas de refonte protocole).
- Réutiliser les assets déjà présents sous `engine/assets/ui/login` et `engine/assets/ui/register`.
- Conserver le flow réseau actuel; exposer son état à l'UI.

## Hors périmètre
- Social login, MFA, captcha production.
- Refonte visuelle avancée/animations complexes.
- Changement du modèle de session côté serveur.

## Tâches proposées
1. Créer un `AuthUiPresenter` (ou équivalent) côté `engine/client`.
2. Définir un mini state machine UI: `Login`, `Register`, `Submitting`, `Error`, `Success`.
3. Connecter les actions UI aux requêtes `REGISTER_REQUEST` et `AUTH_REQUEST`.
4. Afficher messages d'état et erreurs mappées depuis `ERROR`/responses.
5. Intégrer un point d'entrée visuel au boot client.
6. Ajouter logs ciblés + test manuel de bout en bout.

## Critères d'acceptation (DoD)
- [ ] Au lancement client, l'écran de login est affiché.
- [ ] L'utilisateur peut ouvrir l'écran d'inscription et revenir au login.
- [ ] Une inscription valide crée un compte puis permet auth.
- [ ] Une auth valide mène au flux serveur-liste/ticket/shard existant.
- [ ] Les erreurs réseau et validation sont visibles côté UI.
- [ ] Aucun chemin absolu d'assets; chargement via chemins relatifs existants.

## Risques / points d'attention
- Ne pas dupliquer la logique réseau: l'UI doit appeler le flow existant.
- Garantir thread-safety entre rendu UI et callbacks réseau.
- Clarifier la transformation mot de passe → `client_hash` dans le pipeline UI.

## Suggestion de priorité
**Haute (P1)** — demandé pour débloquer une étape clé du projet.
