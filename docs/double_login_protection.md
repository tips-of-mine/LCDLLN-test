# Protection double-login — design

**Statut** : ✅ Livré dans la PR #616.
**PRs liées** : #615 (compteur joueurs fiable + heartbeat client master — prérequis pour la fiabilité de la détection « en jeu »).

---

## 1. Symptôme observé

Un même compte pouvait être « connecté » deux fois en parallèle : deux clients
ouvraient le même personnage et jouaient simultanément. La barre de joueurs
oscillait, et l'éviction `KickedByDuplicateLogin` semblait sans effet.

## 2. Cause racine — le ping-pong de reconnexion

Le master appliquait déjà la policy `KickExisting` : sur une nouvelle auth pour
un compte déjà en session, l'ancienne session était fermée. Mais :

1. La fermeture côté master = simple `close()` TCP du connId visé. Aucune
   notification protocole envoyée.
2. Côté client kické, `NetClient` ne voit qu'un `peer closed`
   (`NetClient.cpp:611`) — indistinguable d'une coupure réseau.
3. `PumpPostAuthEvents`, en post-EnterWorld, déclenche la reconnexion auto
   (`m_reconnectInProgress = true`).
4. La reconnexion ré-authentifie → kicke le client B → B reconnecte → kicke A →
   **boucle infinie**.

Résultat : les deux clients flappaient sans cesse, donnant l'illusion d'une
double connexion simultanée.

## 3. Décision de design

Trois options envisagées :

| | Avantage | Inconvénient | Verdict |
|---|---|---|---|
| (a) Notifier le client kické pour qu'il ne reconnecte pas | Conserve `KickExisting` | Nécessite flush TX fiable côté NetServer ; le 2ᵉ joueur prend la place du 1ᵉʳ | écarté |
| (b) Policy globale `RefuseNew` | Le joueur en place est protégé | Casse le flux de connexion normal du client (auth#1 puis re-auth dans `MasterShardClientFlow` — la 2ᵉ serait refusée par la 1ʳᵉ) | écarté |
| **(c) Refuser la 2ᵉ auth uniquement si la session existante est en jeu** | Le joueur en place est protégé ; le flux client double-auth reste compatible (la session pré-monde transitoire est toujours kickée) | Un hook supplémentaire à câbler | **retenu** |

## 4. Architecture retenue

### 4.1 Hook `SessionManager::SetSessionInWorldHook`

`SessionManager.h` / `.cpp` exposent un hook optionnel :

```cpp
void SetSessionInWorldHook(std::function<bool(uint64_t existingSessionId)> hook);
```

`CreateSession`, sur détection de doublon pour un compte :

1. Si `hook && hook(existing)` → **retourne `kInvalidSessionId` (refus)**, quelle
   que soit la `DuplicateLoginPolicy`.
2. Sinon → policy configurée (par défaut `KickExisting`, qui kicke la session
   pré-monde transitoire — typiquement le double-auth du flux client).

### 4.2 Câblage `main_linux.cpp`

```cpp
sessionManager.SetSessionInWorldHook(
    [&connSessionMap, &sessionCharMap](uint64_t existingSessionId) -> bool
    {
        auto connId = connSessionMap.FindConnIdForSession(existingSessionId);
        if (!connId) return false;
        return sessionCharMap.GetByConnId(*connId).has_value();
    });
```

Chaîne de résolution : `existingSessionId → connId (ConnectionSessionMap)
→ SessionCharacterMap` — la présence d'un binding character signe l'`EnterWorld`
validé.

### 4.3 Renvoi au handler — aucun changement

`AuthRegisterHandler::HandleAuth` traitait **déjà** `CreateSession() == 0` en
renvoyant `BuildAuthResponseErrorPacket(NetErrorCode::ALREADY_LOGGED_IN, ...)`
au client. Rien à modifier.

### 4.4 Propagation côté client

- `MasterShardClientFlow` : capte `p->error_code` (jusque-là ignoré) et le
  propage via le nouveau champ `MasterShardFlowResult::authErrorCode`.
- `AsyncResult::flowAuthAlreadyLoggedIn` : flag passé du worker au main thread
  (la localisation `Tr(...)` n'est appelée que côté main).
- **Deux workers concernés** : `StartLoginWorker` (auth initiale, c'est ELLE qui
  reçoit le refus pour un 2ᵉ joueur en pratique) et `StartMasterFlowWorker`
  (re-auth du flux complet). Les deux lèvent le flag sur `ALREADY_LOGGED_IN`.
- `PollAsyncResult` (deux branches : `AsyncKind::AuthOnly` et la fin du flux
  Master/Shard) : si le flag est levé, affiche `Tr("auth.error.already_logged_in")`
  au lieu du label réseau brut `NetErrorLabel(...)` (qui renverrait « Already
  logged in » en anglais quelle que soit la locale).

### 4.5 Localisation

Clé `auth.error.already_logged_in` ajoutée dans `fr.json` et `en.json`. Le
message mentionne explicitement la session active et invite à contacter le
support et changer son mot de passe si ce n'est pas l'utilisateur.

## 5. Compatibilité avec le flux de connexion client

Le client s'authentifie **deux fois** dans son cycle de connexion normal :

1. `StartLoginWorker` — auth initiale (validation credentials, terms, etc.).
2. `StartMasterFlowWorker` → `MasterShardClientFlow::Run` — re-auth complet
   du flux master/shard sur une nouvelle connexion.

Au moment de la re-auth (étape 2), la session de l'étape 1 existe encore mais
**n'est pas en jeu** (`sessionCharMap` n'a pas encore reçu d'`EnterWorld`).
Le hook renvoie `false` → policy `KickExisting` s'applique → la session
transitoire est kickée → l'auth #2 passe. Le flux client n'est **pas cassé**.

## 6. Cas couverts et edge cases

| Scénario | Résultat |
|---|---|
| Joueur A en jeu, joueur B s'authentifie avec le compte de A | **Refus** côté master ; B voit le message localisé ; A continue à jouer |
| A se déconnecte proprement (TCP master close) → relogue rapidement | `KickExisting` (1ʳᵉ session pas en jeu pendant la fenêtre de reconnexion ? voir ci-dessous) |
| Flux normal client : auth#1 + auth#2 dans `MasterShardClientFlow` | `KickExisting` (auth#1 pas en jeu) → flux complet OK |
| A en jeu, sa TCP master tombe vraiment (rare grâce au heartbeat #615) | Pendant la reconnexion : `sessionCharMap.Remove(connId)` est appelé par le hook `SetConnectionClosedHandler` (cf. #615), donc A n'est plus « en jeu » côté hook → un 2ᵉ login dans cette fenêtre pourrait le kicker. **Acceptable** : la fenêtre est très courte avec le heartbeat actif. |

## 7. Hors scope

- **Éviction de la session de jeu UDP côté shard** — le shard Linux déployé ne
  fait pas (encore) tourner de session de jeu UDP réelle (`m_clients` de
  `ServerApp` n'est utilisé que sur le binaire Windows). Quand le gameplay UDP
  sera câblé sur le shard Linux, une éviction account-keyed côté
  `ServerApp::HandleHello` deviendra nécessaire (refus de l'arrivée d'un 2ᵉ
  endpoint UDP pour un compte déjà présent). À traiter dans un suivi dédié.
- **Authentification UDP** — `HandleHello` fait confiance au `helloNonce`
  (`character_id`) du client sans signature. À renforcer en même temps que
  l'éviction UDP.

## 8. Fichiers touchés (PR #616)

| Fichier | Changement |
|---|---|
| `src/masterd/session/SessionManager.h/.cpp` | Hook `SetSessionInWorldHook` + appel dans `CreateSession` |
| `src/masterd/main_linux.cpp` | Câblage du hook (sessionId → connId → sessionCharMap) |
| `src/shared/network/MasterShardClientFlow.h/.cpp` | Champ `authErrorCode` propagé depuis `ParseAuthResponsePayload` |
| `src/client/auth/AuthUi.h` | `AsyncResult::flowAuthAlreadyLoggedIn` |
| `src/client/auth/AuthUiPresenterCore.cpp` | Flag levé dans `StartLoginWorker` + `StartMasterFlowWorker` ; lecture localisée dans les deux branches de `PollAsyncResult` |
| `game/data/localization/{fr,en}/{fr,en}.json` | Clé `auth.error.already_logged_in` |

## 9. Test plan

- [x] Compte A en jeu, B tente de s'y connecter → refus, message dédié.
- [x] A continue à jouer sans interruption.
- [x] Flux de connexion normal d'un compte (auth#1 + re-auth flow) → non cassé.
- [x] Locale FR : le message FR s'affiche (pas le label anglais brut).
- [ ] Reconnexion auto après vraie coupure (rare avec le heartbeat) : pas
      de régression du comportement existant.

## 10. Déploiement

⚠️ Redéploiement **master** (binaire) + rebuild **client Windows** en lock-step.
Pas de migration DB, pas de nouvel opcode, pas de changement de protocole.
