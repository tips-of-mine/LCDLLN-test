# Design — Questions secrètes en listes déroulantes + pastille « en ligne » (web-portal)

Date : 2026-05-31
Branche : `feat/portal-secret-questions-online-pastille`

## Contexte

Deux demandes sur le **web-portal** (Next.js, lit MySQL `lcdlln_master`) :

1. **Profil de récupération** — remplacer les 3 champs texte libre « Question secrète N »
   par des **listes déroulantes** alimentées par une liste figée de 20 questions, avec
   **exclusion mutuelle** : une question choisie dans une liste ne doit plus être
   sélectionnable dans les deux autres.
2. **Admin → Gestion des joueurs** — afficher une **pastille verte** à droite du nom
   indiquant si le joueur est **en ligne**.

État actuel pertinent :

- `web-portal/components/player/RecoveryProfileForm.tsx` : les questions sont des
  `<input>` texte ; le modèle stocke `secretQuestions: [{question: string, answer: string}]`
  (×3). API `POST /api/password-recovery/profile`.
- La présence des joueurs **n'existe pas en base**. Elle vit en mémoire dans le master :
  - `SessionManager::ListActiveAccountIds()` → comptes **authentifiés** (session active master).
  - `SessionCharacterMap` → connexions **en jeu** (post-EnterWorld), mais indexé par
    `connId` et stocke `characterId`, **pas** `accountId`.
- Le master expose déjà un serveur HTTP `HealthEndpoint` (port `server.health.port`,
  défaut 3842) : routes `/healthz`, `/readyz`, `/metrics`, `/status`, `/web-portal/status`.
- Le portail **ne contacte pas encore** le master en HTTP (il ne lit que MySQL).

Décision retenue (utilisateur) : **API live** interrogée par le portail (et non une
persistance DB), pastille basée sur **les deux** signaux (connecté master + en jeu),
rafraîchissement **au chargement de page** uniquement.

## Feature 1 — Listes déroulantes des questions secrètes (100 % front)

**Fichier :** `web-portal/components/player/RecoveryProfileForm.tsx` (seul).

- Constante en tête de fichier `SECRET_QUESTIONS: string[]` = les 20 questions fournies.
- Chaque slot (3) devient un `<select>` :
  - 1ère option placeholder `value=""` : « — Choisir une question — ».
  - Options = `SECRET_QUESTIONS` **moins** les questions sélectionnées dans les **autres**
    slots (la sélection du slot courant reste visible et sélectionnée).
  - **Compat** : si la valeur chargée d'un slot n'est pas dans `SECRET_QUESTIONS`
    (ancien texte libre), on l'ajoute comme option de **ce** slot uniquement, pour ne pas
    écraser la donnée existante. Elle reste exclue des autres slots comme une question normale.
- Le champ **réponse** reste `<input type="password">` inchangé.
- Le modèle de données (`question` = string) et l'API **ne changent pas**.

Fonction utilitaire interne (pure) :
`availableQuestions(allQuestions, selected, currentIndex) → string[]`
renvoie les questions choisissables pour le slot `currentIndex` (= toutes sauf celles
prises par les autres slots, + l'éventuelle valeur libre du slot courant). Testable isolément.

→ **Aucun impact serveur, aucune migration. Client only.**

## Feature 2 — Pastille « en ligne » (API live)

### Côté master (C++)

1. `SessionCharacterMap` (`src/masterd/session/SessionCharacterMap.{h,cpp}`) :
   - Ajouter `uint64_t accountId` à `CharacterInfo`.
   - Étendre `Set(connId, accountId, characterId, name, normalized, role)` — l'appelant
     `CharacterEnterWorldHandler` dispose déjà de `*accountId`.
   - Ajouter `std::vector<uint64_t> ListInWorldAccountIds() const` (snapshot sous mutex,
     dédupliqué — un compte = au plus un personnage en jeu en pratique).
   - Mettre à jour `SessionCharacterMapTests.cpp` (nouvelle signature `Set`, nouveau getter)
     et tous les autres appelants de `Set` (ChatRelayHandler le cas échéant).

2. `HealthEndpoint` (`src/masterd/metrics/HealthEndpoint.{h,cpp}`) :
   - Nouveau provider optionnel `std::function<std::string()> onlineAccountsProvider`
     (paramètre supplémentaire de `Init`, après `webPortalStatusHtmlProvider`).
   - Détecter le chemin `GET /online-accounts` dans `ParseRequestLine` (ajout d'un flag).
   - Répondre `200 application/json` avec le corps du provider (ou `404` si non câblé).

3. `main_linux.cpp` :
   - Lambda provider capturant `sessionManager` et `sessionCharMap`, construisant
     `{"authenticated":[...],"inWorld":[...]}` (entiers, pas d'échappement nécessaire).
   - Passer la lambda en dernier argument de `healthEndpoint.Init(...)`.

Source « en ligne » = `authenticated` (login jeu) ; « en jeu » = `inWorld` (EnterWorld validé).

### Côté portail (Next.js)

1. Variable d'env **optionnelle** `MASTER_STATUS_URL` (ex. `http://127.0.0.1:3842`).
   - Helper `lib/serverStatus.ts` : `fetchOnlineAccounts(): Promise<{authenticated:Set<number>, inWorld:Set<number>}>`.
   - `fetch(`${base}/online-accounts`, { cache: 'no-store' })`, `try/catch`, timeout court
     (AbortController ~1,5 s). En cas d'absence d'env / d'échec / de timeout → **ensembles vides**
     (pas d'erreur propagée). Aucune pastille affichée dans ce cas.

2. `app/admin/players/page.tsx` (server component) :
   - Appelle `fetchOnlineAccounts()` une fois, calcule par joueur :
     `inWorld.has(id)` → état « en jeu » ; sinon `authenticated.has(id)` → « connecté » ; sinon hors ligne.
   - Affiche la pastille à droite du `displayName` (avant le `(login)` / `#id`).

3. Composant pastille (présentation pure) à côté du nom, 3 états :
   - **En jeu** : disque plein `var(--ln-success)`, `title="En jeu"`.
   - **Connecté (menu)** : disque contour (`border: 1px solid var(--ln-success)`, fond transparent),
     `title="Connecté (menu)"`.
   - **Hors ligne** : rien.

→ **Déploiement : ⚠️ redéploiement master requis** (nouvelle route HTTP + champ `accountId`
dans `SessionCharacterMap`). Côté portail : nouvelle var d'env `MASTER_STATUS_URL` à renseigner
(sinon dégradation gracieuse, pas de pastille). Feature 1 livrable indépendamment (client only).

## Découpage PR proposé

- **PR A — Feature 1** (client only) : `RecoveryProfileForm.tsx`. Mergeable seule, aucun redéploiement.
- **PR B — Feature 2** (master + portail) : changements C++ + portail, à déployer en lock-step
  (le portail dégrade gracieusement si le master n'a pas encore la route, donc pas de casse,
  mais la pastille n'apparaît qu'une fois le master redéployé et `MASTER_STATUS_URL` renseignée).

## Tests

- Feature 1 : test unitaire de `availableQuestions(...)` (exclusion mutuelle + compat valeur libre).
- Feature 2 master : test `SessionCharacterMap::ListInWorldAccountIds()` (ajout/suppression, dédup).
- Feature 2 portail : `fetchOnlineAccounts` dégrade en ensembles vides si fetch échoue.

## Non-objectifs (YAGNI)

- Pas de polling temps réel (rafraîchissement au chargement de page seulement).
- Pas de persistance DB de la présence.
- Pas d'authentification ajoutée sur la route `/online-accounts` (cohérent avec les routes
  `HealthEndpoint` existantes ; le binding réseau `server.health.bind` reste la frontière).
- Pas de filtre / tri par statut en ligne dans l'admin (affichage seulement).
