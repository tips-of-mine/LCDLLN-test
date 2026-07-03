# Corrections P0/P1 de l'audit 2026-07-03 — design

## Objectif

Corriger les findings **P0 et P1** du rapport
[docs/audit/2026-07-03-audit-complet-frais.md](../../audit/2026-07-03-audit-complet-frais.md).
8 findings corrigés (F1-F6, F8, F9) ; **F7 différé** (dépend d'une valeur
opérationnelle absente).

## Périmètre & livraison

- **Aucun changement du binaire client de jeu** : le REGISTER shard part de
  `shardd` (serveur), pas du client. F7 (seul finding client) est différé.
- Livraison sur la branche courante `claude/funny-bardeen-f6f803`, **server-first**,
  un commit par finding (ou par groupe cohérent).
- Tout est serveur + deploy.

## Décisions de cadrage (validées avec l'utilisateur)

1. **F7 différé** — passer `allow_insecure_dev` à `false` avec
   `server_fingerprint` vide bloquerait tous les clients
   ([NetClient.cpp:147](../../../src/shared/network/NetClient.cpp:147) :
   `serverFp != expected && !allowInsecure` → `return false`). Nécessite le vrai
   SHA-256 du cert master, non disponible. Documenté, non implémenté.
2. **F6 fail-fast** — le boot échoue si le secret HMAC est faible/vide, sauf flag
   dev explicite `LCDLLN_ALLOW_DEV_SECRET=1`. La valeur faible est retirée des
   configs livrées.
3. **F3 réutilise `shard.ticket_hmac_secret`** comme clé d'authentification (ancre
   de confiance shard↔master existante, protégée par F6).
4. **Une branche, commits groupés, server-first.**

---

## Findings & corrections

### Groupe 1 — serveur sûr, non wire-breaking

#### F5 — router `kOpcodeResendVerificationRequest`
- **Fichier** : `src/masterd/main_linux.cpp` (~ligne 1097).
- **Correction** : ajouter `|| opcode == kOpcodeResendVerificationRequest` à la
  condition de dispatch qui route vers `passwordResetHandler.HandlePacket`.
- **Test** : dispatch d'intégration (couvert par revue ; pas de test unitaire
  isolé du `main`).

#### F8 — rate-limit sur `HandleVerifyEmail`
- **Fichier** : `src/masterd/handlers/password/PasswordResetHandler.cpp`
  (`HandleVerifyEmail`, ~230-291).
- **Correction** : utiliser `m_rateLimit` (déjà injecté via `SetRateLimitAndBan`,
  actuellement inutilisé) pour compter les échecs de vérification par
  `account_id` (et IP si disponible dans le contexte du handler). Après N échecs
  consécutifs (constante nommée, ex. `kMaxVerifyAttempts = 5`), rejeter les
  tentatives suivantes pendant une fenêtre de verrouillage, en **plus** du TTL de
  15 min existant. Un succès réinitialise le compteur.
- **Comportement après verrouillage** : renvoyer la même réponse d'échec générique
  que pour un code invalide (pas de divulgation « compte verrouillé » qui
  aiderait l'énumération).
- **Test** : `PasswordResetHandler` — N échecs → tentative suivante rejetée même
  avec le bon code pendant la fenêtre.

#### F9 — rejeter les caractères de contrôle dans l'e-mail
- **Fichiers** : `src/shared/account/AccountValidation.cpp` (`ValidateEmail`) ;
  défense en profondeur dans `src/masterd/email/SmtpMailer.cpp` (`Send`).
- **Correction** : `ValidateEmail` rejette tout octet `< 0x20` ou `== 0x7F`
  **n'importe où** dans la chaîne. `SmtpMailer::Send` refuse (ou strip) tout CR/LF
  dans `to`/`subject`/`from_address` avant construction des commandes/headers SMTP.
- **Test** : `AccountValidation` — `a@b.com\r\nRCPT TO:<x>` rejeté ; e-mail valide
  accepté.

#### F2 — lier le heartbeat shard à la connexion enregistrée
- **Fichier** : `src/masterd/handlers/shard/ShardRegisterHandler.cpp`
  (`HandleHeartbeat`, ~127-148).
- **Correction** : récupérer `GetShardConnection(parsed->shard_id)` et rejeter le
  heartbeat (log WARN + drop) si la `connId` courante ne correspond pas. Master
  seul, pas de changement wire.
- **Test** : couvert par la revue + les tests de F3 (le heartbeat authentifié
  valide la `connId`).

#### F4 — vérifier le certificat TLS dans `CaptchaVerifier`
- **Fichiers** : `src/shared/security/CaptchaVerifier.cpp` (`VerifyHttp`) ;
  `deploy/docker/Dockerfile.master` (paquet `ca-certificates`).
- **Correction** : après `SSL_CTX_new`, appeler
  `SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr)` +
  `SSL_CTX_set_default_verify_paths(ctx)` ; avant `SSL_connect`,
  `SSL_set1_host(ssl, host)` ; après le handshake, échouer si
  `SSL_get_verify_result(ssl) != X509_V_OK`. Vérifier que l'image Docker master
  installe `ca-certificates`.
- **Hors périmètre** : le bypass Windows (`Verify()` renvoie `true` sans réseau)
  est un stub de dev ; le serveur tourne sous Linux.
- **Test** : difficile en unitaire (réseau) — au minimum vérifier que le code
  compile et que les appels de validation sont présents (garde de non-régression).

### Groupe 2 — wire-breaking + gating boot (lock-step master+shard)

#### F3 — authentifier `SHARD_REGISTER` / `SHARD_HEARTBEAT`
- **Fichiers** : `src/shared/network/ShardPayloads.{h,cpp}` (payloads register +
  heartbeat) ; `src/masterd/handlers/shard/ShardRegisterHandler.cpp` (validation) ;
  côté émission `shardd` (`ShardToMasterClient` / `MasterShardClientFlow`) ;
  helper HMAC `src/masterd/handlers/shard/ShardTicketCrypto.*` (ou équivalent
  partagé).
- **Correction** : ajouter un champ **tag HMAC-SHA256 (32 octets)** en fin des
  payloads `ShardRegisterPayload` et `ShardHeartbeatPayload`, calculé sur les
  champs sérialisés (name/endpoint/shard_id/… selon le payload) avec la clé
  `shard.ticket_hmac_secret`. `shardd` calcule et joint le tag ; le master
  recalcule et **rejette** (drop + log) si le tag est absent ou invalide, avant
  tout `RegisterShard`/`UpdateHeartbeat`. Comparaison à **temps constant**
  (`CRYPTO_memcmp`) — corrige aussi F27 (P2) au passage.
- **Wire-breaking** : taille de payload modifiée → client (shard) neuf
  incompatible avec master ancien et inverse → **redéploiement lock-step**.
- **Test** : `ShardPayloadsTests` — round-trip build/parse avec tag ; tag
  falsifié rejeté ; secret différent rejeté.

#### F6 — refuser le secret HMAC par défaut au boot
- **Fichiers** : nouveau helper partagé (ex.
  `src/shared/security/SharedSecretPolicy.{h,cpp}`, `IsWeakSharedSecret`) ;
  `src/masterd/main_linux.cpp` et `src/shardd/main_linux.cpp` (contrôle au boot) ;
  configs livrées `deploy/docker/config/master.config.json`,
  `deploy/docker/config/shard.config.json`, `server-config/config.json`.
- **Correction** : après lecture de `shard.ticket_hmac_secret`, si le secret est
  vide **ou** appartient au jeu de valeurs de dev connues (au minimum
  `"dev_secret_change_in_production"`) et que `LCDLLN_ALLOW_DEV_SECRET` != `"1"`,
  logguer FATAL et `exit(non-zéro)` avant d'ouvrir le port. Retirer la valeur
  faible des 3 configs livrées (valeur vide, à renseigner par l'opérateur).
- **CMake** : le nouveau `.cpp` partagé doit être ajouté **aussi** à la liste
  `server_app` dans `src/CMakeLists.txt` (server_app ne linke pas `engine_core`).
- **Test** : `IsWeakSharedSecret` — vide/valeur dev → true ; secret fort → false.

### Groupe 3 — deploy/config

#### F1 (+ F10) — synchroniser les migrations Docker + garde CI
- **Fichiers** : `deploy/docker/sql/migrations/` (resync) ; garde CI dans
  `.github/workflows/` ; script de sync existant
  (`scripts/sync-db-to-docker-deploy.sh` si présent).
- **Correction** : resynchroniser `deploy/docker/sql/migrations/` depuis
  `sql/migrations/` (ajoute 0050-0071, corrige la copie obsolète de 0043 = F10).
  Ajouter une étape CI qui diffe les deux répertoires et **échoue** en cas de
  divergence, pour empêcher toute nouvelle dérive.
- **Test** : la garde CI elle-même (échoue si divergence).

### F7 — différé (non implémenté)
- Documenté ici et dans l'entête de suivi de l'audit. Fermeture ultérieure :
  fournir le SHA-256 du cert master dans `client.server_fingerprint`, puis passer
  `client.allow_insecure_dev` à `false` (tous les call sites +
  `BuildUserSettingsJson` + `config.json`).

---

## Stratégie de test

TDD là où le repo le permet (la CI Linux `build-linux.yml` lance `ctest`). Tests
neufs/étendus : F9 (`AccountValidation`), F3 (`ShardPayloadsTests`), F6
(`IsWeakSharedSecret`), F8 (`PasswordResetHandler`). F5/F2/F4 couverts par revue +
compilation (dispatch/réseau, non isolables en unitaire simple). Attention au piège
`assert`+`NDEBUG` : ne pas mettre la logique de validation dans un `assert`.

## Déploiement

⚠️ **REDÉPLOIEMENT SERVEUR REQUIS — master ET shard, en lock-step.**
- F3 est wire-breaking (taille de payload register/heartbeat modifiée).
- F6 change le comportement de boot (échec si secret faible).
- **Action opérateur obligatoire** : définir un vrai `shard.ticket_hmac_secret`
  (identique côté master et shard) **avant** de démarrer, sinon boot en échec
  (par design). En dev, poser `LCDLLN_ALLOW_DEV_SECRET=1`.
- F1 est deploy-only (rejouer le build de l'image master).
- Groupe 1 seul exigerait déjà un redéploiement master (F5/F8/F9/F2/F4).

## Critères de succès

- F1-F6, F8, F9 corrigés avec leurs tests verts en CI.
- F7 documenté comme différé, sans changement de code client.
- Aucune régression wire hors du changement F3 assumé.
- Ligne de déploiement lock-step claire dans la PR.
