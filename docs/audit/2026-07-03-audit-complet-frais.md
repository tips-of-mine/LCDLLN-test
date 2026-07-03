# Audit complet frais — 2026-07-03

Audit statique de l'ensemble du code du projet (hors `legacy/`), à la recherche
d'anomalies, de problèmes de sécurité et d'optimisations. Audit *frais* : il ne
s'appuie pas sur les audits de juin (`docs/audit/2026-06-*`).

## Méthode

Workflow multi-agents (53 agents au total) en 4 phases :

1. **Audit** — 15 cellules sous-système × dimension (sécurité / bug / perf), un
   agent auditeur chacune, lecture du code réel.
2. **Dédup** — fusion des doublons inter-agents (38 findings bruts → 38 uniques).
3. **Vérification adversariale** — chaque finding P0/P1 soumis à un agent
   sceptique chargé de le **réfuter** en relisant le code ; P2/P3 en vérification
   simple. **2 findings réfutés**, 36 retenus.
4. **Synthèse** — ce rapport.

**Limites** : analyse **statique** uniquement (pas de toolchain de build ni de
profiling local). Les findings de catégorie **perf** sont des **candidats à
mesurer**, pas des certitudes. `legacy/` et `game/data` (hors
`slash_commands.json`) sont exclus.

## Résumé exécutif

| Sévérité | Nombre | Catégories |
|----------|--------|------------|
| **P0** — critique | 4 | 3 sécurité, 1 bug |
| **P1** — sérieux | 5 | 4 sécurité, 1 bug |
| **P2** — mineur / perf | 21 | 6 sécurité, 8 perf, 7 bug |
| **P3** — qualité | 6 | 1 sécurité, 5 bug |
| **Total** | **36** | 14 sécurité, 7 perf, 15 bug |

**Aucune cellule d'audit n'a échoué** — les 15 cellules ont produit un résultat.

Les points les plus importants :

1. **Le canal shard↔master n'est pas authentifié** (F2, F3). N'importe quel pair
   pouvant atteindre le port TCP public 3840 peut s'enregistrer comme un shard
   légitime et forger des heartbeats, jusqu'à intercepter des données de session
   de personnages. C'est la faille la plus grave du lot.
2. **Deux connexions « HTTPS » ne vérifient pas le certificat TLS** : la
   vérification CAPTCHA côté serveur (F4, fuite du `secret_key` + bypass anti-bot
   total sous MITM) et le pinning TLS client désactivé par défaut (F7).
3. **Le répertoire de migrations packagé dans l'image Docker master est tronqué**
   (F1) : un `docker build` manuel sur un checkout git saute 22 migrations
   (0050–0071). ⚠️ Nuance importante ci-dessous — le pipeline CI officiel
   régénère ce dossier et n'est donc pas affecté.
4. **Trois vecteurs de brute-force / injection sur l'auth** : code de vérification
   e-mail brute-forçable (F8), injection SMTP via l'adresse e-mail (F9), absence
   de rate-limiting sur le login du portail web (F28).

---

## P0 — Sécurité critique

### F1 · Migrations 0050–0071 absentes de l'image Docker master — bug

[deploy/docker/Dockerfile.master:30](deploy/docker/Dockerfile.master:30) · catégorie **bug**

`Dockerfile.master` fait `COPY sql/migrations /app/sql/migrations` avec pour
contexte de build `deploy/docker/`. Or `deploy/docker/sql/migrations/` contient
0001..0049 puis saute directement à `0072_factions_v2.sql` : les 22 fichiers 0050
à 0071 (auction_listings, game_events, arena_teams, character_skills,
outdoor_pvp_state, guilds_master, bg_history, loot_tables, groups, spell_aura,
creature_movement, dungeon_instances + les correctifs d'index/colonnes 0064-0071)
sont absents de cette copie, bien que présents dans `sql/migrations/` à la racine.
`MigrationRunner` ([MigrationRunner.cpp:99](src/masterd/migrations/MigrationRunner.cpp:99))
applique les fichiers présents dans l'ordre de leur nom sans détecter les « trous »
de version : un déploiement Docker frais applique 0072 directement après 0049,
sans créer les tables correspondantes.

> ⚠️ **Nuance (vérification)** : `deploy/docker/sql/migrations/` est un **artefact
> régénéré** par `scripts/sync-db-to-docker-deploy.sh`, exécuté automatiquement en
> CI (`build-linux.yml`, via `pack-linux-docker-bundle.sh`) avant l'assemblage du
> bundle de déploiement officiel. Un déploiement via le pipeline CI **n'est donc
> pas affecté** — la sync écrase la copie périmée. Le risque ne se matérialise que
> pour un `docker compose build` / `docker build` lancé **manuellement** sur un
> checkout git sans exécuter d'abord le script de sync. Ce dossier committé
> obsolète a déjà été signalé par les audits de juin.

**Recommandation** : ne pas committer un artefact généré divergent — soit générer
`deploy/docker/sql/migrations/` au packaging sans le versionner, soit faire pointer
le `COPY` vers `../../sql/migrations`. Ajouter un test CI qui diffe les deux
répertoires et échoue en cas de divergence.

### F2 · Canal shard→master : heartbeat spoofable (shard_id non lié à la connexion) — sécurité

[ShardRegisterHandler.cpp:127](src/masterd/handlers/shard/ShardRegisterHandler.cpp:127) · catégorie **sécurité**

`HandleHeartbeat` appelle `m_registry->UpdateHeartbeat(parsed->shard_id, …)` et
`m_presenceCache->Update(parsed->shard_id, parsed->players)` en utilisant
directement le `shard_id` du **payload client**, sans vérifier que la `connId`
courante est bien celle enregistrée pour ce `shard_id` (le paramètre `connId` est
explicitement ignoré, `uint32_t /*connId*/`). Combiné à F3, un pair réseau peut
forger des heartbeats pour n'importe quel `shard_id` actif (falsifiant sa charge
et sa liste de joueurs) ou faire un ré-REGISTER avec le même `name` pour détourner
`SetShardConnection` et **intercepter les push `AdmitCharacter`** (contenant
`account_id`/`character_id`) destinés au vrai shard.

**Recommandation** : lier chaque `shard_id` à la `connId` ayant réalisé le REGISTER
(déjà mémorisée via `SetShardConnection`) et rejeter tout heartbeat dont la `connId`
ne correspond pas à `GetShardConnection(shard_id)`. À combiner avec F3.

### F3 · SHARD_REGISTER / SHARD_HEARTBEAT sans aucune authentification — sécurité

[ShardRegisterHandler.cpp:49](src/masterd/handlers/shard/ShardRegisterHandler.cpp:49) · catégorie **sécurité**

`HandlePacket` route `kOpcodeShardRegister` et `kOpcodeShardHeartbeat` sans
vérifier ni session, ni secret partagé, ni IP source. Ces opcodes sont dispatchés
par le **même `NetServer` que les opcodes joueurs**, sur le port TCP public 3840
([main_linux.cpp:1091](src/masterd/main_linux.cpp:1091)). Le pattern HMAC existe
pourtant dans la codebase (`ShardTicketHandler` utilise `shard.ticket_hmac_secret`)
mais n'est **pas** appliqué à l'enregistrement de shard. N'importe quel client TCP
capable de forger un payload `SHARD_REGISTER` valide peut s'enregistrer comme un
shard légitime dans le `ShardRegistry` avec un endpoint arbitraire.

**Recommandation** : exiger un secret partagé (HMAC ou mTLS) dans `SHARD_REGISTER`,
ou déplacer ces opcodes sur un listener réseau séparé restreint à l'infrastructure
interne (allowlist IP / VPN), avant tout traitement.

### F4 · CAPTCHA : certificat TLS non vérifié (MITM + fuite du secret) — sécurité

[CaptchaVerifier.cpp:215](src/shared/security/CaptchaVerifier.cpp:215) · catégorie **sécurité**

`VerifyHttp()` crée un `SSL_CTX` avec `SSL_CTX_new(TLS_client_method())` et fait
`SSL_connect()` **sans jamais** appeler `SSL_CTX_set_verify(…, SSL_VERIFY_PEER, …)`,
sans charger de trust store, et sans vérifier `SSL_get_verify_result()` ni le
CN/SAN du certificat. Le mode par défaut d'OpenSSL étant `SSL_VERIFY_NONE`, la
connexion réussit face à un certificat auto-signé, expiré ou d'un autre domaine.
Un attaquant en position réseau (Wi-Fi malveillant, DNS spoofing, proxy d'egress)
peut MITM cet appel « HTTPS » vers hcaptcha/google : il **reçoit le `secret_key`
CAPTCHA en clair** (concaténé dans le body, ligne 165-166) et peut renvoyer un
`{"success":true}` forgé, faisant passer toute vérification CAPTCHA — bypass total
du contrôle anti-bot serveur (inscriptions de masse / credential stuffing).

> Note connexe : sur Windows, `Verify()` bypasse entièrement l'appel réseau et
> retourne `true` sans vérifier — problème distinct mais tout aussi grave.

**Recommandation** : activer la validation complète — `SSL_CTX_set_verify(ctx,
SSL_VERIFY_PEER, nullptr)`, `SSL_CTX_set_default_verify_paths(ctx)`,
`SSL_set1_host(ssl, host)` avant `SSL_connect`, et contrôler
`SSL_get_verify_result() == X509_V_OK` après le handshake.

---

## P1 — Bug sérieux / sécurité modérée

### F5 · Renvoi de code de vérification totalement inopérant (opcode non routé) — bug

[main_linux.cpp:1097](src/masterd/main_linux.cpp:1097) · catégorie **bug**

Le client envoie `kOpcodeResendVerificationRequest`
([AuthUiPresenterCore.cpp:2617](src/client/auth/AuthUiPresenterCore.cpp:2617)) et
`PasswordResetHandler` sait le traiter, mais la table de dispatch du master ne
route que `kOpcodeForgotPasswordRequest`, `kOpcodeResetPasswordRequest` et
`kOpcodeVerifyEmailRequest` vers `passwordResetHandler`. `kOpcodeResendVerification­Request`
(=37) est absent de la condition : le paquet tombe dans le `else` final et
`HandleResendVerification` n'est jamais atteint. Les utilisateurs qui n'ont pas
reçu ou ont laissé expirer leur code ne peuvent **jamais** en obtenir un nouveau.

**Recommandation** : ajouter `|| opcode == kOpcodeResendVerificationRequest` à la
condition de dispatch (lignes 1097-1099).
**Déploiement** : ⚠️ ce fix nécessite un redéploiement du master.

### F6 · Secret HMAC de ticket shard faible et committé en dur — sécurité

[master.config.json:10](deploy/docker/config/master.config.json:10) · catégorie **sécurité**

`master.config.json:10`, `shard.config.json:9` et `server-config/config.json:21`
définissent tous la **même** valeur `"ticket_hmac_secret":
"dev_secret_change_in_production"`, committée dans le repo. Ce secret signe en
HMAC-SHA256 les tickets de connexion client→shard. Le seul garde-fou côté code est
`if (m_secret.empty())` — qui ne rejette que la chaîne **vide**, pas cette valeur
par défaut connue. Ces fichiers sont bind-montés tels quels dans les conteneurs
(le compose référence un vrai domaine de prod). Un opérateur qui oublie de changer
la valeur laisse un attaquant connaissant ce secret public forger des tickets
shard valides et usurper des sessions client→shard.

**Recommandation** : refuser au boot toute valeur égale à un jeu de valeurs de dev
connues (pas seulement vide), ou générer un secret aléatoire au premier démarrage,
et faire échouer le boot si le secret par défaut est détecté hors environnement dev.

### F7 · Pinning TLS client désactivé par défaut (`allow_insecure_dev=true`) — sécurité

[AuthUiPresenterCore.cpp:2217](src/client/auth/AuthUiPresenterCore.cpp:2217) · catégorie **sécurité**

`client.allow_insecure_dev` est lu avec un défaut **`true`** codé en dur à tous les
points de connexion (Login/Register/VerifyEmail/ForgotPassword) et transmis à
`NetClient::SetAllowInsecureDev()`. Quand ce flag est vrai, un mismatch de
fingerprint de certificat est accepté silencieusement (simple `LOG_WARN`) au lieu
de refuser la connexion ([NetClient.cpp:141](src/shared/network/NetClient.cpp:141)).
Le même défaut `true` est écrit dans le JSON généré pour tout nouvel utilisateur et
dans le `config.json` livré (ligne 222). Le ticket source M19.6 exigeait pourtant
« ne pas fallback silencieusement en insecure » — c'est une régression par rapport
à l'intention documentée. Exploitation conditionnée à une position MITM active
(d'où P1 et non P0). Même pattern côté shard→master.

**Recommandation** : passer le défaut à `false` (tous les call sites +
`BuildUserSettingsJson` + `config.json`) ; n'activer le mode insecure que via une
macro de build de développement, pas une clé de config modifiable par l'utilisateur.

> **Statut (2026-07-03)** : DIFFÉRÉ. Passer `allow_insecure_dev` à `false` sans
> `client.server_fingerprint` renseigné bloquerait tous les clients
> (`TlsVerifyFingerprint` refuse la connexion sur mismatch). Fermeture : fournir
> le SHA-256 du certificat du master de production dans `client.server_fingerprint`,
> puis passer `allow_insecure_dev` à `false` (tous les call sites +
> `BuildUserSettingsJson` + `config.json`).

### F8 · Code de vérification e-mail brute-forçable (pré-auth, sans throttle) — sécurité

[PasswordResetHandler.cpp:230](src/masterd/handlers/password/PasswordResetHandler.cpp:230) · catégorie **sécurité**

`HandleVerifyEmail` lit un `account_id` **arbitraire** depuis le payload client
(opcode traité avant toute authentification de session) et valide un code à 6
chiffres via `ValidateVerificationCode`, qui ne vérifie qu'un TTL de 15 minutes,
**sans aucune limite de tentatives**. Le membre `m_rateLimit` est câblé
(`SetRateLimitAndBan`) mais **jamais utilisé** dans ce fichier — seul l'envoi
d'e-mail est limité, pas la vérification. Un attaquant connaissant/énumérant un
`account_id` peut soumettre jusqu'à 10⁶ codes en 15 minutes sans blocage réseau.

**Recommandation** : appliquer `m_rateLimit` (déjà disponible) sur les tentatives
`HandleVerifyEmail` par `account_id`/IP, avec verrouillage après N échecs, en plus
du TTL.

### F9 · Injection SMTP via l'adresse e-mail (CR/LF non filtrés) — sécurité

[AccountValidation.cpp:36](src/shared/account/AccountValidation.cpp:36) · catégorie **sécurité**

`ValidateEmail()` ne vérifie qu'un `@` non initial, la longueur et un `.` dans le
domaine — **aucun caractère de contrôle interdit**. `NormaliseEmail()` ne trim que
les extrémités : un CR/LF au **milieu** de la chaîne (ex.
`a@b.com\r\nRCPT TO:<victim@evil.tld>\r\nDATA\r\n…`) survit. Cette valeur, lue
brute du payload réseau, est persistée puis passée telle quelle à
`SmtpMailer::Send()` comme `to`, insérée sans échappement dans les commandes SMTP
brutes `RCPT TO:<to>` ([SmtpMailer.cpp:439](src/masterd/email/SmtpMailer.cpp:439))
et l'en-tête `To:` — permettant l'injection de commandes/headers SMTP arbitraires.
Le prepared-statement protège du SQL, pas du SMTP.

**Recommandation** : rejeter dans `ValidateEmail()`/`NormaliseEmail()` tout octet
de contrôle (0x00-0x1F, 0x7F) **n'importe où** dans la chaîne, et en défense en
profondeur sanitiser `to`/`subject`/`from` dans `SmtpMailer::Send()`.

---

## P2 — Bug mineur / optimisation probable

### Sécurité

**F25 · /setlevel sans RBAC réel côté shard** —
[slash_commands.json:457](game/data/config/slash_commands.json:457).
La commande de triche `/setlevel` (minRole `administrator`) est routée shard et son
gating effectif repose sur un simple booléen `ConnectedClient.chatModeratorRole`
qui ne distingue pas moderator/game_master/administrator. Un compte `moderator`
pourrait invoquer une commande censée être réservée aux administrateurs.
*Reco* : propager le rôle réel du compte au shard et exiger `administrator` dans
`ChatCommandParser` / `HandleChatSlashCommand`.

**F26 · Position serveur appliquée sans validation NaN/Inf côté client** —
[ClientPrediction.cpp:268](src/client/gameplay/ClientPrediction.cpp:268).
`OnServerSnapshot` copie `snap.position`/`velocity` sans `std::isfinite` ni bornes.
Le canal gameplay UDP n'authentifie pas fortement chaque champ : un paquet corrompu
NaN/Inf propage indéfiniment via le lerp de `UpdateSmoothing`, corrompant l'état
local sans récupération. *Reco* : valider `isfinite` + bornes de map, sinon
ignorer/loguer et garder le dernier état valide.

**F27 · Comparaison HMAC de ticket shard non à temps constant** —
[ShardTicketCrypto.cpp:43](src/masterd/handlers/shard/ShardTicketCrypto.cpp:43).
`std::memcmp` court-circuite au premier octet différent. Le `ticket_id` aléatoire à
usage unique et TTL court limite fortement l'exploitabilité, mais c'est un écart
aux bonnes pratiques. *Reco* : `CRYPTO_memcmp` d'OpenSSL (déjà lié).

**F28 · Aucun rate-limiting sur le login du portail web** —
[login/route.ts:16](web-portal/app/api/auth/login/route.ts:16).
`/api/auth/login` et `/api/password-recovery/reset` n'ont aucun throttling/lockout
(`verifyPortalCredentials` sans compteur d'échecs). Argon2id ralentit mais
n'empêche pas un brute-force ciblé à faible volume. `requestPasswordRecovery` a bien
un plafond de 3 tokens/h, mais pas le reset ni le login. *Reco* : rate limiting par
IP/compte (DB ou Redis) avec verrouillage temporaire.

**F29 · Token de reset écrit en clair dans les logs (fallback sans SMTP)** —
[passwordRecovery.ts:292](web-portal/lib/auth/passwordRecovery.ts:292).
Quand `SMTP_HOST`/`SMTP_FROM` sont absents, `logWarn` journalise le `resetUrl`
complet (token valide 10 min). Si les logs sont centralisés/persistés et plus
largement accessibles que le SMTP, cela offre une prise de contrôle de compte.
*Reco* : ne jamais loguer le token/lien complet ; identifiant tronqué ou
`account_id` uniquement, et s'assurer que ce chemin n'est pas actif en prod.

**F30 · Injection HTML dans les e-mails via `{{var}}` non échappé** —
[sender.ts:51](web-portal/lib/email/sender.ts:51).
`loadTemplate()` fait `html.replaceAll("{{key}}", value)` sans échapper `< > & "`.
`sendEmailChange()` injecte `newEmail` — validé seulement par `.includes('@')` —
dans le template : une adresse `x"><script>…</script>@evil.com` passe la validation
et le HTML/JS injecté finit dans l'e-mail envoyé. *Reco* : échapper les valeurs
interpolées en entités HTML, ou valider `newEmail` avec un vrai regex strict.

### Bug

**F10 · Copie déployée de la migration 0043 obsolète (sans garde de colonne)** —
[0043_phase_1c_account_roles.sql:1](deploy/docker/sql/migrations/0043_phase_1c_account_roles.sql:1).
La version racine de 0043 contient une étape 0 qui garantit l'existence de
`accounts.role` via `information_schema` (à cause du doublon de version 0023) ; la
copie sous `deploy/docker/` ne l'a pas. Même mitigation CI que F1 (dossier
régénéré). *Reco* : synchroniser depuis la version canonique.

**F11 · Anti-flood chat contournable une fois `maxTrackedAccounts` atteint** —
[ChatGate.cpp:158](src/masterd/chat/ChatGate.cpp:158).
`m_windows` n'évince jamais d'entrée individuelle (`PurgeWindow` vide le deque mais
pas la map ; seuls `ResetState`/`Reconfigure` la vident). Une fois 4096 comptes
distincts trackés, tout **nouveau** compte reçoit `Allowed` sans passer par
l'anti-flood — indéfiniment sur un serveur qui tourne des semaines. Le commentaire
du header prétend à tort qu'une éviction LRU existe. *Reco* : éviction LRU réelle
ou balayage périodique des entrées à deque vide.

**F12 · Objet interactif : ni portée ni droit avant broadcast global** —
[InteractiveHandler.cpp:52](src/masterd/handlers/interactive/InteractiveHandler.cpp:52).
`HandlePacket` ne valide que l'existence d'une session : n'importe quel joueur
authentifié peut changer l'état de n'importe quel objet interactif (porte, coffre)
sans vérification de distance ni de droit, changement répercuté à toutes les
connexions. Absence assumée dans le commentaire (M100.32). *Reco* : contrôle de
portée/droits une fois la position serveur disponible côté master.

**F13 · Craft : destruction définitive d'ingrédients sans output ni remboursement** —
[CraftingSystem.cpp:413](src/shardd/gameplay/crafting/CraftingSystem.cpp:413).
Les ingrédients sont validés une seule fois à `TryStartCraft`, pas réservés. À la
complétion, la boucle appelle `RemoveItemFromInventory` par ingrédient ; cette
fonction retire toujours ce qui est présent puis renvoie `false` si insuffisant, et
la boucle `break` au premier manquant — mais les ingrédients déjà traités et la
quantité partielle sont perdus, **sans output ni rollback**. Reproductible en
vendant/tradant un ingrédient entre le start et le tick. *Reco* : re-valider la
présence complète de **tous** les ingrédients avant d'en retirer un seul.

**F14 · Parseur JSON de Config récursif sans limite de profondeur (stack overflow)** —
[Config.cpp:125](src/shared/core/Config.cpp:125).
`ParseObject`/`ParseArray` s'appellent mutuellement via `ParseValue` sans compteur
de profondeur. Un fichier `[[[[…]]]]` de quelques Ko fortement imbriqué provoque un
dépassement de pile au chargement de `config.json`, `server.config.json`,
`zone.json` ou `scenery.json` (dont du contenu de zone potentiellement externe via
`LoadActiveZone`). Même défaut dans `RoutineSerialization.cpp`. *Reco* : compteur de
profondeur (seuil 64-128) renvoyant une erreur propre.

**F15 · `DecodeRoutinesBin` : `reserve(count)` non borné depuis le fichier** —
[RoutineSegmentCodec.cpp:68](src/shared/routine/RoutineSegmentCodec.cpp:68).
`count` (u32) est lu du fichier et passé à `graphs.reserve(count)` sans validation
vs `bytes.size()`. Un `routines.bin` corrompu avec `count=0xFFFFFFFF` déclenche un
`std::bad_alloc`/`length_error` non catché → crash, au lieu du retour d'erreur
propre appliqué aux autres champs. *Reco* : borner `count` vs la taille restante
avant `reserve`.

**F16 · `LoadDungeonPortalsBin` : `reserve()` non borné (crash éditeur)** —
[DungeonPortalIo.cpp:147](src/world_editor/volumes/dungeons/DungeonPortalIo.cpp:147).
Même pattern : `instanceCount` (u32) du header passé à `reserve()` avant validation
par item. Un `.bin` corrompu/forgé fait tenter une réservation de centaines de Go →
crash de l'éditeur à l'ouverture. *Reco* : borner `instanceCount` vs octets
restants.

**F17 · `LoadMeshInsertsBin` : même `reserve()` non borné** —
[MeshInsertIo.cpp:149](src/world_editor/volumes/MeshInsertIo.cpp:149).
Identique à F16 pour les instances de mesh insert. *Reco* : idem.

### Perf *(candidats non mesurés)*

**F18 · Purge O(n²) de `m_remoteSmoothed`/`m_remoteAnims` deux fois par frame** —
[Engine.cpp:15417](src/client/app/Engine.cpp:15417).
Deux boucles de purge scannent chacune tout le vecteur `remotes` par entrée de map,
en O(n·m) répété deux fois par frame à ~60 FPS. En zone dense (AoI large) le coût
croît quadratiquement. *Reco* : construire un `unordered_set<EntityId>` une fois par
frame et purger les deux maps en O(n+m).

**F19 · `emitBarriersBeforePass` alloue plusieurs conteneurs par passe et par frame** —
[FrameGraph.cpp:291](src/client/render/FrameGraph.cpp:291).
À chaque passe compilée de chaque frame, la fonction alloue sur le tas un
`unordered_map` + plusieurs `vector` (barrières). Avec N passes (shadow, geometry,
lighting, bloom, TAA, SSAO, water…), 3-5 alloc/désalloc par passe par frame. *Reco*
: conteneurs membres pré-alloués (`clear()`) ou frame arena existant.

**F20 · `vkDeviceWaitIdle` sur le thread de rendu à chaque éviction de chunk terrain** —
[TerrainChunkRenderer.cpp:1150](src/client/render/terrain_chunk/TerrainChunkRenderer.cpp:1150).
`Tick()` appelle `vkDeviceWaitIdle` pour libérer sûrement les buffers/images de
chunks évincés — ce qui stall **toute la frame** (perte du parallélisme CPU/GPU) à
chaque éviction pendant le streaming (déplacement joueur, changement de zone). Le
`DeferredDestroyQueue` prévu existe et est déjà collecté par frame, mais son
`PushBuffer`/`PushImage` n'est câblé nulle part. *Reco* : enfiler les ressources
évincées dans `m_deferredDestroyQueue` au lieu du wait synchrone.

**F21 · `QuestRuntime::ApplyEvent` : cascade O(quêtes²) à chaque événement gameplay** —
[QuestRuntime.cpp:730](src/shardd/gameplay/quest/QuestRuntime.cpp:730).
Appelé par kill/loot/talk/zone-enter, `ApplyEvent` appelle deux fois
`SyncQuestStates`, qui itère toutes les définitions avec des scans linéaires
`FindQuestStateIndex` (comparaison de chaînes) et une boucle imbriquée d'exclusion
→ O(Q²)–O(Q³) par événement par joueur. OK pour un petit catalogue, ne scalera pas.
*Reco* : `unordered_map<questId,index>` + graphe d'exclusion précalculé.

**F22 · `FindMobByEntityId` reste un scan linéaire (index O(1) non appliqué aux mobs)** —
[ServerApp.cpp:2452](src/shared/server_bootstrap/ServerApp.cpp:2452).
`UpdateSpawners` appelle `FindMobByEntityId` par slot par spawner à chaque tick, en
O(spawners·slots·mobs). Le même antipattern a déjà été corrigé pour les clients
(`m_clientIndexByEntityId`, commenté « TG.2 — O(1) ») mais pas pour les mobs, malgré
15+ appels dont plusieurs en boucle chaude. *Reco* : index
`unordered_map<EntityId,size_t>` analogue.

**F23 · Rasterisation polyline macro O(cellules × segments)** —
[PolylineMacroCore.cpp:189](src/world_editor/terrain/PolylineMacroCore.cpp:189).
`RasterizeMacroPolyline` teste, pour chaque cellule de la bbox, **tous** les
segments de la polyline. Pour une longue polyline (chaîne de montagnes/vallée) ou
un preset de zone automatisé, O(aire_bbox × nb_sommets). *Reco* : structure
d'accélération spatiale (buckets par chunk).

**F24 · Copie complète du heightmap à chaque tick de brosse Smooth** —
[TerrainBrush.cpp:135](src/world_editor/terrain/TerrainBrush.cpp:135).
En mode Smooth, `ApplyBrushKernel` fait `snapshot = chunk.heights` (257×257 =
~258 Ko) à chaque tick de déplacement souris, alors que seule la bbox de la brosse
est lue. Churn heap et memcpy inutiles à cadence interactive, × chunks touchés.
*Reco* : ne copier que la sous-région de la bbox (+ halo de 1 cellule).

---

## P3 — Qualité / suggestions

**F31 · Fuite latente d'image VMA dans `FrameGraph::destroy()`** —
[FrameGraph.cpp:502](src/client/render/FrameGraph.cpp:502).
Si `allocatedWithVma` est vrai mais l'allocateur nul, la destruction est sautée et
la VkImage/VmaAllocation reste orpheline. Chemin **actuellement mort**
(`ensureImageResources` force `allocatedWithVma=false`, bypass STAB.7) mais piège
latent si le bypass est levé. *Reco* : voie de secours (`vkDestroyImage` direct) ou
`LOG_ERROR` explicite.

**F32 · Reset password envoyé à l'adresse fournie par le client, pas celle en base** —
[PasswordResetHandler.cpp:130](src/masterd/handlers/password/PasswordResetHandler.cpp:130).
`HandleForgotPassword` envoie à `email` (entrée réseau normalisée) plutôt qu'à
`optAccount->email` (valeur DB canonique). Impact nul en nominal (FindByEmail matche
exact), mais dérive possible si la normalisation applicative diverge un jour de la
collation SQL. Incohérent avec `TermsHandler`/`AuthRegisterHandler`. *Reco* :
utiliser `optAccount->email`.

**F33 · `UpdateField` stocke un pointeur brut vers le mask de l'objet propriétaire** —
[UpdateField.h:67](src/shardd/entities/UpdateField.h:67).
`Unit`/`Creature`/`Player`/`WorldObject` construisent chaque `UpdateField<T>` avec
`&Mask()` mais ne déclarent aucun constructeur/opérateur de copie supprimé : une
copie par valeur (ex. `std::vector<Player>` qui réalloue) laisserait les pointeurs
de mask pointer vers l'objet **original** → corruption de réplication ou
use-after-free. Aucun call site ne copie ces types aujourd'hui — footgun latent.
*Reco* : `= delete` sur la copie de `Object` tant qu'une deep-copy correcte n'existe
pas.

**F34 · Statement caché reste cassé après échec de `Reset()` sur un hit** —
[SqlPreparedStatement.cpp:377](src/shared/db/SqlPreparedStatement.cpp:377).
Sur un cache hit, si `stmt->Reset()` échoue, `Acquire()` renvoie `nullptr` mais
l'entrée reste dans `m_lru`/`m_index` : les `Acquire()` suivants pour le même SQL
échoueront en boucle. Impact faible (le `ConnectionPool` reset tout le cache sur
échec de connexion) mais l'auto-guérison au niveau statement manque. *Reco* :
évincer l'entrée sur échec de `Reset()`.

**F35 · Même `reserve()` non borné pour les proxies de collision de volume** —
[VMapBridge.cpp:265](src/world_editor/volumes/bridge/VMapBridge.cpp:265).
`outProxies.reserve(count)` depuis un champ header non validé. Sévérité moindre que
F16/F17 (POD sans strings, ~128 Go au pire) mais vecteur de crash sur fichier
corrompu. *Reco* : valider `count` vs octets restants, cohérent avec les autres
loaders.

**F36 · Géolocalisation IP en clair (HTTP, pas HTTPS)** —
[IpApiGeoProvider.cpp:65](src/client/localization/IpApiGeoProvider.cpp:65).
`FetchCountryCode` utilise `WinHttpOpenRequest(…, 0)` (pas `WINHTTP_FLAG_SECURE`) et
le port HTTP par défaut. Un attaquant réseau observe le lancement du client
(fingerprinting) et peut falsifier la réponse JSON pour influencer la langue
suggérée (impact fonctionnel mineur). *Reco* : passer en HTTPS (port 443,
`WINHTTP_FLAG_SECURE`) comme les autres appels WinHTTP du module auth.

---

## Couverture & limites

**Cellules auditées (15/15, aucune échouée)** : masterd (handlers/wire, auth,
autres), shardd (net/anticheat, gameplay/entities, autres), shared
(net/messager, security/auth/db, core/util/formulas), client (render, app/net,
gameplay/UI), world_editor, web-portal, sql + tools + deploy/config.

**Exclusions** : `legacy/` (consigne permanente), `game/data` hors
`slash_commands.json`, `tickets/`, `docs/`, assets binaires.

**Rappels méthodologiques** :
- Analyse **statique** — aucun build ni profiling local. Les 7 findings **perf**
  sont des **candidats à mesurer** avant optimisation.
- Chaque finding P0/P1 a été soumis à une réfutation adversariale ; 2 findings
  (sur 38 bruts) ont été réfutés et écartés.
- F1 et F10 concernent un dossier (`deploy/docker/sql/migrations/`) **régénéré en
  CI** : le chemin de déploiement officiel n'est pas affecté, seul un build Docker
  manuel l'est. Déjà signalé par les audits de juin.

## Priorisation suggérée

1. **F2 + F3** (auth du canal shard) — traiter ensemble, plus grave impact.
2. **F4** (TLS CAPTCHA) et **F7** (pinning client) — corrections TLS localisées.
3. **F5** (renvoi de code, une ligne) et **F8/F9** (brute-force/injection auth).
4. **F1/F10** — décider du sort du dossier `deploy/docker/sql/migrations` committé.

**Déploiement** : ✅ docs uniquement, pas de redéploiement serveur. *(Les
corrections issues de ce rapport, elles, seront pour la plupart wire/serveur —
F1–F6, F8–F13, F21, F22, F25 touchent le master ou le shard et exigeront un
redéploiement lock-step quand elles seront livrées.)*
