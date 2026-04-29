# CODEBASE MAP — Lune Noire (LCDLLN-test)

> Référence rapide à inclure dans un prompt pour éviter la ré-analyse complète.
> Dernière mise à jour : 2026-04-29 — **Hotfix `Send CHARACTER_CREATE failed` (réutilisation connexion master post-flow)** sur la branche `claude/fix-master-flow-reuse-connection`. Symptôme utilisateur : sur l'écran CharacterCreate, clic Créer → écran d'erreur « Send CHARACTER_CREATE failed ». Cause : `StartMasterFlowWorker` créait un `NetClient` **local** sur la stack du worker thread. Conséquences en cascade : (1) ce local NetClient ouvre une 2e connexion master, dont l'AUTH kick par duplicate-login la session AuthOnly de `m_masterClient` ; (2) à la fin du worker, le local NetClient est détruit → connexion fermée ; (3) `m_masterClient` (AuthOnly) ferme aussi (heartbeat timeout après kick session). Quand l'utilisateur clique Créer, `m_masterClient` est dans l'état `Disconnected` → `RequestResponseDispatcher::SendRequest` retourne `false` → message d'erreur. **Fix** : `MasterShardClientFlow::Run` populate désormais `result.session_id` avec la session créée à l'AUTH. `AuthUiPresenter::AsyncResult::sessionId` propagé en main thread → `m_masterSessionId = copy.sessionId` quand le flow réussit (les futurs `RequestResponseDispatcher::SetSessionId` reçoivent ainsi la session post-Flow, pas l'AuthOnly kickée). `StartMasterFlowWorker` rewrité : ferme proprement l'ancien `m_masterClient` puis ré-alloue + bind TLS en main thread AVANT de lancer le worker, qui ne capte qu'un raw pointer. La connexion **survit** au worker et reste utilisable post-flow par CharacterCreate, CharacterDelete, SAVE_POSITION, EnterWorld. **Phase 3.7.5 (uint64 character_id end-to-end via Hello)** sur la branche `claude/hello-uint64-character-id`. Bump `kProtocolVersion` 1 → 2 (wire-breaking UDP gameplay) : `HelloMessage::clientNonce` passé de `uint32` à `uint64` ; `EncodeHello` / `DecodeHello` utilisent `WriteU64` / `ReadU64`, payload 8 → 12 octets. `GameplayUdpClient::SendHello` signature widened. `Engine.cpp` ne tronque plus à `& 0xFFFFFFFF` lors de la consommation `EnterWorldCommand` ; lecture config `client.gameplay_udp.character_key` reinterpret-cast int64→uint64 pour préserver les valeurs bit 63 set. Côté shard : `ServerApp::HandleHello(uint64)`, `ConnectedClient::{helloNonce, persistenceCharacterKey}` widened, `m_bannedCharacterKeys: unordered_set<uint64>`, ban file parser/writer alignés. `CharacterPersistence::{LoadCharacter, BuildCharacterStateRelativePath}` + `PersistedCharacterState::characterKey` widened ; le path filename `character_<key>.ini` accepte `std::to_string(uint64)` directement. `AuctionHouse` widened en cohérence (`{seller,buyer,highBidder}CharacterKey: uint64`) pour éviter les troncations à la liste/bid/buyout. `RefundGoldToCharacter`, `DepositMailboxDelivery`, `FindConnectedClientByCharacterKey` signatures uint64. **Phase 3.11.1 (panneau chat Dear ImGui scrollable)** sur la branche `claude/chat-imgui-panel`. Remplace l'overlay Win32 mono-couleur de Phase 3.11 par un vrai panneau ImGui ancré en bas-gauche (520×220 px par défaut, configurable via `render.chat_imgui.{width_px, height_px, anchor_margin_px, enabled}`). Nouveau renderer `engine/render/ChatImGuiRenderer.h/.cpp` (Windows uniquement, partage le contexte ImGui de `m_worldEditorImGui` exactement comme `m_authImGui`). Couleurs par canal (10) lues depuis `engine::net::ChannelColorArgb` et converties ARGB→ImVec4 ; auto-scroll bottom uniquement quand `ChatUiPresenter::ScrollLinesFromEnd()==0` ; ligne d'invite bas écho l'`InputLine` quand le focus est actif (caret `_`), sinon hint `[/] tchatter`. `ChatUiPresenter` expose 4 nouveaux accesseurs const (`History()`, `InputLine()`, `ChannelFilterMask()`, `ScrollLinesFromEnd()`) — aucune mutation, le renderer est lecture seule. `Engine.h` ajoute `m_chatImGui` (unique_ptr, créé alongside `m_authImGui` après init `m_worldEditorImGui`). `Engine.cpp` étend la condition `NewFrame` ImGui post-auth + nouvelle branche `else if (chat ImGui actif)` qui appelle `m_chatImGui->Render()` puis `ImGui::Render()` ; `RecordToBackbuffer` est étendu pareil. La logique d'input (`/` focus, Enter submit, 1-0 filtres, scroll PageUp/Dn) reste dans `ChatUiPresenter::Update()` — pas d'`ImGui::InputText` actif donc pas de capture clavier. Quand le panneau ImGui est actif, le path legacy `m_window.SetOverlayText(BuildHudPanelText())` est volontairement skippé pour éviter le double affichage ; le fallback texte legacy reste utilisé sur Linux ou si `render.chat_imgui.enabled=false`. Limites encore ouvertes : pas encore de toggles cliquables sur les chips canaux (l'utilisateur passe par 1-0 clavier), pas d'`ImGui::InputText` natif (l'écho de saisie est statique avec caret `_`), pas de scrollbar ImGui visuelle (utilise `SetScrollHereY` pour le sticky-bottom). **Phase 3.6.6 (CHARACTER_SAVE_POSITION client trigger)** sur la branche `claude/character-save-position-client` (stack au-dessus de Phase 3.6.5). `AuthUiPresenter` expose `SavePositionAsync(characterId, x, y, z, yawDeg, pitchDeg)` qui sérialise via `BuildCharacterSavePositionRequestPayload` et envoie un paquet opcode 43 (requestId=0, fire-and-forget) sur `m_masterClient` toujours vivant grâce au fix Phase 2/3 (suppression des `ResetMasterSession()` avant MasterFlow). Méthode `PumpPostAuthEvents()` drain `m_masterClient->PollEvents()` chaque frame post-auth : responses CHARACTER_SAVE_POSITION_RESPONSE loggées en debug (pas de matching), `Disconnected` → `m_masterSessionId=0` + `m_masterClient.reset()` pour échec propre des futurs Save. Côté `Engine` : 4 nouveaux membres (`m_currentCharacterId`, `m_nextSavePositionTime`, `m_savePositionIntervalSec` configurable `client.save_position.interval_sec` défaut 30 s plancher 5 s, `m_shutdownPositionSaved`). Dans la consommation `EnterWorldCommand` : capture `enterCmd.characterId` + arme `m_nextSavePositionTime = now + interval`. Branche `else` du gate auth (chaque frame post-auth) : `m_authUi.PumpPostAuthEvents()` puis tick périodique qui appelle `SavePositionAsync` avec `out.camera.{position,yaw,pitch}` (rad→deg). `Engine::Shutdown` : sauvegarde finale fire-and-forget juste avant `m_authUi.Shutdown()`, lit la caméra depuis `m_renderStates[m_renderReadIndex.load()&1].camera` (master encore vivant à ce point). **Phase 3.6.5 (CHARACTER_SAVE_POSITION protocol, server-side)** sur la branche `claude/character-save-position`. Nouveaux opcodes 43/44 (41/42 utilisés Phase 3.9 — déjà mergée). Payloads `CharacterSavePositionRequestPayload { uint64 characterId, float x, y, z, yawDeg, pitchDeg }` (28 octets) + `CharacterSavePositionResponsePayload { uint8 success }`. Handler `engine/server/CharacterSavePositionHandler.h/.cpp` côté master : résout `connId → sessionId → accountId`, rejette NaN/Inf, `UPDATE characters SET spawn_x=?, spawn_y=?, spawn_z=?, spawn_yaw_deg=?, spawn_pitch_deg=? WHERE id=? AND account_id=? AND deleted_at IS NULL` (gating par account_id empêche cross-compte ; deleted_at gating empêche save sur perso supprimé). Câblé dans `main_server_linux.cpp` + CMake. Aucune migration. Tests `character_save_position_payloads_tests` (round-trip request/response, rejet short/null, position zero edge case). Le **déclenchement client** (envoi périodique + au shutdown) est en Phase 3.6.6 (PR #396, stack au-dessus). **Phase 3.11 (premier rendu visuel du chat HUD)** sur la branche `claude/chat-hud-overlay`. Ajoute `ChatUiPresenter::BuildHudPanelText()` : version utilisateur du panneau (pas de header debug, pas de code couleur ARGB, pas de filter mask hex) — N lignes "[hh:mm TAG] Sender: text" filtrées par `m_channelFilterMask` + une ligne d'invite ("> _input_" en focus, "[/] pour tchatter — [1..0] filtres canal" sinon). Engine.cpp post-auth else branch : priorité d'affichage = welcome banner (Phase 3.5) > chat HUD (Phase 3.11) > vide. Le chat est désormais visible à l'écran via `m_window.SetOverlayText` (overlay Win32 natif). Pas de nouvelle infrastructure ImGui (un vrai panneau ImGui scrollable sera une Phase 3.11.1 ulterieure si besoin). L'input clavier (`/` pour focus, Enter pour submit, 1-0 toggle filters, scroll history) est déjà câblé dans `ChatUiPresenter::Update()` appelé chaque frame post-auth (`Engine.cpp` ~3027). **Phase 3.8 (race / class string identifiers)** sur la branche `claude/character-race-class-strings` (stack au-dessus de Phase 3.7). Migration 0033 ajoute `characters.race_str` + `class_str` (VARCHAR(32) NOT NULL DEFAULT ''). `CharacterCreateHandler` persiste désormais les strings reçues du payload (auparavant ignorées et hardcodées à 0). `CharacterListHandler` SELECT inclut les 2 colonnes ; `CharacterListEntry` payload étendu avec `race_str` + `class_str` (length-prefixed UTF-8). Renderer `AuthImGuiCharacterSelect` humanise via clé localisation `auth.character_select.race.<id>` / `class.<id>` (FR/EN ajoutées pour 8 races + 8 classes), fallback = id capitalisé. Si row pré-migration (champs vides), affichage retombe sur l'ancien subline (`Slot N - Niveau X`). **Phase 3.7 (character_id propagated to shard)** sur la branche `claude/character-id-to-shard` (stack au-dessus de Phase 3.6). Découverte clé : `ServerApp::HandleHello` traite déjà `clientNonce` comme `tentativeCharacterKey`. Phase 3.7 = simple branchement client : à la consommation d'`EnterWorldCommand`, on `m_cfg.SetValue("client.gameplay_udp.character_key", enterCmd.characterId & 0xFFFFFFFF)` AVANT `InitGameplayNet()`. `SendHello` envoie alors le `character_id` réel comme `clientNonce`. Aucun changement de wire / crypto ticket. Limite : uint32 suffit pour < 4G persos (cas de test largement dépassable). **Phase 3.6 (per-character spawn from DB)** sur la branche `claude/character-spawn-from-db` (stack au-dessus de Phase 3.5). Migration 0032 : ajoute `characters.spawn_x/y/z/yaw_deg/pitch_deg` (FLOAT, défauts `0/100/0/0/-10`). `CharacterCreateHandler` initialise les colonnes depuis la config serveur `character_creation.default_spawn.*` à l'INSERT. `CharacterListHandler` SELECT inclut les 5 colonnes. Wire format CHARACTER_LIST_RESPONSE étendu : 5 floats (20 octets) ajoutés à la fin de chaque entry — wire-breaking, master + client doivent se déployer ensemble. `CharacterListEntry` côté client expose les champs ; `EnterWorldCommand` les porte (`spawnX/Y/Z`, `spawnYawDeg`, `spawnPitchDeg`, `hasSpawn`). `Engine.cpp` priorité 1 = perso si `hasSpawn`, fallback `client.world.default_spawn.*` config. Tests `character_list_payloads_tests` mis à jour (round-trip avec spawns). **Note chat global** : `ChatUiPresenter::BuildPanelText` est appelé chaque frame mais la chaîne n'est utilisée que pour `LOG_DEBUG` (Engine.cpp:3287). Aucun rendu visuel n'existe — feature jamais implémentée, pas une régression. Phase à part entière (UI ImGui chat panel) à planifier. **Phase 3.5 (post-auth spawn + welcome HUD)** sur la branche `claude/post-auth-spawn-hud`. À la consommation de `EnterWorldCommand` dans `Engine.cpp` (branche `else` du gate auth) : (1) la caméra est téléportée à la position lue dans `client.world.default_spawn.{x,y,z,yaw_deg,pitch_deg}` (`config.json`) ; (2) une bannière « Bienvenue, {name} ! » est postée via `m_window.SetOverlayText` et expire après 5 s (`m_enterWorldBannerText` + `m_enterWorldBannerExpiry` dans `Engine.h`) ; (3) la connexion UDP gameplay reste câblée comme en Phase 3. Clé localisation `auth.enter_world.welcome` ajoutée FR/EN. Limites encore ouvertes : pas de `characters.spawn_x/y/z` en DB (Phase 3.6) ; `character_id` non propagé via le ticket shard (Phase 3.7). **Phases 2 + 3 du flux post-auth** déjà mergées (PR #386). **Phase 2** : `MasterShardClientFlow.Run()` envoie un `CHARACTER_LIST_REQUEST` sur la connexion master (toujours active après `TICKET_ACCEPTED`, échec non-fatal) et remplit `MasterShardFlowResult::character_list`. `AuthUiPresenter` introduit `Phase::CharacterSelect` + membres `m_characterList` / `m_selectedCharacterIndex`. Dans la branche succès du flow, `m_flowComplete = true` est remplacé par : `m_postRegistrationCharacterCreatePending` ou liste vide → `Phase::CharacterCreate` ; sinon → `Phase::CharacterSelect`. Nouveaux fichiers `engine/client/auth/screens/AuthScreenCharacterSelect.cpp` (presenter) et `engine/render/auth/screens/AuthImGuiCharacterSelect.cpp` (renderer : liste cliquable + boutons Retour / Créer / Jouer). **Phase 3** : nouveau struct `AuthUiPresenter::EnterWorldCommand` (one-shot, mirror de `VideoSettingsCommand`) émis sur clic « Jouer », consommé par `Engine.cpp` dans la branche `else` du gate auth (~ligne 2853) ; `MasterShardFlowResult::shard_endpoint` ajouté + persisté dans `m_chosenShardEndpoint` côté presenter. Le consume-side override les clés `client.gameplay_udp.{host,port,enabled}` puis appelle `ShutdownGameplayNet()` (si déjà init au boot) + `InitGameplayNet()` pour câbler la connexion UDP au shard accepté. Clés de localisation `auth.character_select.*` ajoutées en FR/EN. Phase 1 (protocole CHARACTER_LIST) déjà mergée : opcodes 39/40, payloads, handler master, tests `character_list_payloads_tests`. Itération 7 du login : gap CONNEXION→IDENTIFIANT resserré à ~9 px avec trait centré (ImGui::Spacing après Separator retiré dans BeginPanel — affecte tous les écrans), champs Identifiant et Mot de passe plus hauts (FramePadding y 3 → 8 dans DrawAuthGoldField, hauteur ≈ 19 → 29 px), descente accrue des 4 boutons (Dummy 18 → 32 avant Récupération/Portail, 14 → 28 avant Créer/Se connecter). Itération 6 : aération formulaire (extraSpacingPx 6, Dummy 12 entre les deux champs). Itération 5 : cadre +30 px hauteur, Tweaks 218 → 160 px collé en bas. Itération 4 : recentrage via `BeginPanel(stageW, titleZoneW, ...)`. Itération 3 : titre stage 96 %, sous-titre 2.5x, cadre +10 px, Tweaks sans header. Itération 2 : titre 5.0x + marge sup., persistance suppression infoBanner langue, retrait cédilles. Itération 1 : cadre 570 px, chips Tab/Entrée masquées, tooltip « Se souvenir de moi », badge éphémère, Tweaks 0.85x + boutons interactifs. Plus en amont : corrections migrations 0017-0031, ajout passes auth Vulkan, templates email déplacés vers `web-portal/email-templates/` et `game/data/email/`.

---

## 1. Vue d'ensemble architecturale

```
┌─────────────────────────────────────────────────────────────────┐
│                        CLIENT (Windows)                         │
│  engine/client/auth/   ←→   engine/render/auth/                │
│  Presenter (logique)         Renderer (affichage ImGui/Vulkan)  │
│         ↕ RenderModel (struct de données UI)                    │
│  engine/client/        ←→   engine/render/                      │
│  HUD, inventaire, chat       Passes Vulkan, terrain, particules │
└──────────────────────────────────┬──────────────────────────────┘
                                   │ UDP / TCP (engine/network/)
┌──────────────────────────────────▼──────────────────────────────┐
│                     SERVEUR MASTER (Linux)                      │
│  engine/server/   →  handlers auth, register, shards, terms     │
│  engine/server/db/→  pool MySQL, migrations                     │
└──────────────────────────────────┬──────────────────────────────┘
                                   │ shard tickets (ShardToMasterClient)
┌──────────────────────────────────▼──────────────────────────────┐
│                     SERVEUR SHARD (Linux)                       │
│  engine/server/   →  gameplay : quêtes, craft, guildes, combat  │
└─────────────────────────────────────────────────────────────────┘
```

**Technologies :** C++20, Vulkan, ImGui, MySQL, UDP maison, CMake + vcpkg.

---

## 2. Flux d'authentification — interaction complète

Ce flux est le plus important pour comprendre les écrans d'auth.

```
Utilisateur → AuthUiPresenter (logique) → RenderModel → AuthImGuiRenderer (affichage)
                    ↓
             StartXxxWorker()  (thread background)
                    ↓
             NetClient → RequestResponseDispatcher → Serveur Master
                    ↓
             BuildModel_Xxx() → RenderModel mis à jour → re-render
```

### Ordre des écrans (état actuel après réordonnancement 2026-04-27)

```
Premier lancement (compte inexistant) :
  LanguageSelectionFirstRun → Login → Register → EmailConfirmationPending →
  VerifyEmail → Login (re-saisie credentials) → Terms (si CGU à accepter) →
  ShardPick (forcé, même avec un seul royaume) →
  MasterFlow (TICKET_ACCEPTED + CHARACTER_LIST) →
  CharacterCreate (post-Register forcé) → Game

Connexion utilisateur existant :
  Login → Terms (si CGU mises à jour, sinon sauté) →
  ShardPick → MasterFlow (TICKET_ACCEPTED + CHARACTER_LIST) →
  CharacterSelect (≥1 perso) ou CharacterCreate (0 perso) →
  [clic « Jouer » → EnterWorldCommand → Engine consume → InitGameplayNet shard host:port] →
  Game (scène 3D rendue par m_world.Update inconditionnel)
```

Drapeaux clés (`engine/client/AuthUi.h`) :
- `m_postRegistrationCharacterCreatePending` (bool) — armé sur `Register` succès, désarmé sur
  `CharacterCreate` succès / annulation / Escape / `flowComplete`. Quand vrai, `ShardPick` redirige
  vers `Phase::CharacterCreate` au lieu de `MasterFlow`.
- `m_chosenShardId` (uint32_t) — royaume sélectionné par l'utilisateur sur `ShardPick`. Persiste à
  travers `Phase::CharacterCreate` et sert d'override `m_shardFlowOverrideId` pour le `MasterFlow`
  final qui connecte le client au shard.
- `MasterShardClientFlow::SetShardPickWhenMultiple(true)` (appelé par AuthUi) force le retour
  `shard_choice_required` même quand un seul shard est en ligne. Le défaut de la classe est `false`
  pour que le client headless `ClientFlowMain` continue d'auto-sélectionner.

### Fichiers impliqués par phase d'auth

| Phase UI | Presenter (logique) | Renderer (affichage) |
|---|---|---|
| Sélection langue | `auth/screens/AuthScreenLanguageSelect.cpp` | `render/auth/screens/AuthImGuiLanguageSelect.cpp` |
| Connexion | `auth/screens/AuthScreenLogin.cpp` | `render/auth/screens/AuthImGuiLogin.cpp` |
| Inscription | `auth/screens/AuthScreenRegister.cpp` | `render/auth/screens/AuthImGuiRegister.cpp` |
| Vérif email | `auth/screens/AuthScreenVerifyEmail.cpp` | `render/auth/screens/AuthImGuiVerifyEmail.cpp` |
| Mot de passe oublié | `auth/screens/AuthScreenForgotPassword.cpp` | `render/auth/screens/AuthImGuiForgotPassword.cpp` |
| Choix shard | `auth/screens/AuthScreenShardPick.cpp` | `render/auth/screens/AuthImGuiShardPick.cpp` |
| Création personnage | `auth/screens/AuthScreenCharacterCreate.cpp` | `render/auth/screens/AuthImGuiCharacterCreate.cpp` |
| Sélection personnage | `auth/screens/AuthScreenCharacterSelect.cpp` | `render/auth/screens/AuthImGuiCharacterSelect.cpp` |
| Options | `auth/screens/AuthScreenOptions.cpp` | `render/auth/screens/AuthImGuiOptions.cpp` |
| CGU | `auth/screens/AuthScreenTerms.cpp` | `render/auth/screens/AuthImGuiTerms.cpp` |
| Erreur | `auth/screens/AuthScreenError.cpp` | `render/auth/screens/AuthImGuiError.cpp` |

---

## 3. Comment lire un écran d'auth (règle de lecture)

Chaque écran est découpé en **deux fichiers** :

### Fichier Presenter — `engine/client/auth/screens/AuthScreenXxx.cpp`
- `BuildModel_Xxx(RenderModel& model)` → remplit la struct `RenderModel` avec les données à afficher (textes, champs, boutons, états actif/survolé).
- `Update_Xxx(Input, Config, Window, ...)` → gère la navigation clavier hors ImGui.
- `ImGuiXxx(...)` → méthodes appelées par le renderer quand l'utilisateur clique/tape.
- `StartXxxWorker(cfg)` → lance le thread réseau pour envoyer la requête.

### Fichier Renderer — `engine/render/auth/screens/AuthImGuiXxx.cpp`
- Lit le `RenderModel` fourni par le presenter.
- Dessine avec ImGui (panneaux, champs de saisie, boutons, couleurs).
- Appelle les méthodes `ImGuiXxx()` du presenter en réponse aux interactions utilisateur.
- **C'est ICI qu'on modifie le visuel** : couleurs, polices, disposition, animations.

### Fichiers communs
| Fichier | Rôle |
|---|---|
| `engine/client/AuthUi.h` | Déclaration complète d'`AuthUiPresenter` : toutes les phases, membres, méthodes. **Lire en premier pour comprendre la structure.** |
| `engine/client/auth/AuthUiPresenterCore.cpp` | Init, `Update()` global, dispatch des phases, `SubmitCurrentPhase()`, gestion async. |
| `engine/client/auth/AuthUiPresenterSettings.cpp` | Persistance locale : remember-me, locale, settings JSON. |
| `engine/client/auth/AuthUiPresenterNative.cpp` | Auth native Windows (hors ImGui). |
| `engine/render/AuthUiRenderer.h` | Interface du renderer (méthode `Render(RenderModel)`). |
| `engine/render/AuthImGuiRenderer.h/.cpp` | Implémentation ImGui du renderer. Dispatch vers les sous-renderers par phase. |
| `engine/render/auth/AuthImGuiCommon.h/.cpp` | Helpers partagés : couleurs, polices, style, boutons communs, champs de saisie. |

### Struct centrale : `RenderModel` (dans `engine/client/AuthUi.h`)
```
RenderModel
├── sectionTitle          : titre du panneau
├── fields[]              : champs de saisie (label, valeur, secret, actif, survolé)
├── bodyLines[]           : lignes de texte (liens, hints, CGU...)
├── actions[]             : boutons (label, primaire, actif, survolé)
├── languageFirstRunCards[]: cartes langue (premier lancement)
├── infoBanner / errorText: messages info/erreur
└── ... (flags de layout)
```

---

## 4. Couche réseau — fichiers clés

| Fichier | Rôle |
|---|---|
| `engine/network/NetClient.h/.cpp` | Socket TCP bas niveau : connexion, envoi, réception. Thread IO interne. |
| `engine/network/NetClient_linux.cpp` | Implémentation Linux de NetClient. |
| `engine/network/RequestResponseDispatcher.h/.cpp` | Associe requêtes et réponses via `request_id`. Pump() = boucle principale. Gère les timeouts. |
| `engine/network/PacketBuilder.h/.cpp` | Construit un paquet binaire v1 (en-tête + payload). |
| `engine/network/PacketView.h/.cpp` | Vue lecture sur un paquet reçu (sans copie). |
| `engine/network/ByteReader.h/.cpp` | Désérialisation séquentielle (ReadU32, ReadString…). |
| `engine/network/ByteWriter.h/.cpp` | Sérialisation séquentielle (WriteU32, WriteString…). |
| `engine/network/ProtocolV1Constants.h` | Opcodes, tailles max, constantes du protocole. |
| `engine/network/NetErrorCode.h` | Enum des codes d'erreur réseau. |
| `engine/network/ErrorPacket.h/.cpp` | Paquet ERROR : build (serveur→client) + parse (client). |
| `engine/network/AuthRegisterPayloads.h/.cpp` | Payloads auth, register, reset password, vérif email, disponibilité pseudo. |
| `engine/network/CharacterPayloads.h/.cpp` | Payloads création/liste de personnages. |
| `engine/network/ShardPayloads.h/.cpp` | Payloads liste shards. |
| `engine/network/ShardTicketPayloads.h/.cpp` | Payloads tickets de connexion shard. |
| `engine/network/ServerListPayloads.h/.cpp` | Payloads liste serveurs. |
| `engine/network/TermsPayloads.h/.cpp` | Payloads CGU. |
| `engine/network/ShardToMasterClient.h/.cpp` | Connexion shard→master (enregistrement, heartbeat). |
| `engine/network/MasterShardClientFlow.cpp` | Flux complet master→shard (flow d'auth). |

### Format paquet v1
```
[ uint16 opcode ][ uint16 flags ][ uint32 request_id ][ uint64 session_id ][ uint32 payload_size ][ payload... ]
```
- `request_id == 0` → push serveur (pas de requête associée).
- `request_id > 0` → réponse à une requête client.

---

## 5. Couche serveur — fichiers clés

### Handlers principaux
| Fichier | Rôle |
|---|---|
| `engine/server/AuthRegisterHandler.h/.cpp` | Traite AUTH_REQUEST et REGISTER_REQUEST. Valide, hashe, crée compte. |
| `engine/server/PasswordResetHandler.h/.cpp` | Reset mot de passe par email. |
| `engine/server/CharacterCreateHandler.h/.cpp` | Création de personnage. |
| `engine/server/ShardRegisterHandler.h/.cpp` | Enregistrement d'un shard auprès du master. |
| `engine/server/ShardTicketHandler.h/.cpp` | Génération de ticket de connexion shard. |
| `engine/server/ServerListHandler.h/.cpp` | Retourne la liste des shards disponibles. |
| `engine/server/TermsHandler.h/.cpp` | Acceptation des CGU. |

### Comptes et validation
| Fichier | Rôle |
|---|---|
| `engine/server/AccountRecord.h` | Struct d'un compte (id, login, email, hash, état…). |
| `engine/server/AccountStore.h` | Interface abstraite du store de comptes. |
| `engine/server/MysqlAccountStore.h/.cpp` | Implémentation MySQL du store. |
| `engine/server/InMemoryAccountStore.h/.cpp` | Implémentation mémoire (tests). |
| `engine/server/AccountValidation.h/.cpp` | Règles de validation (login, email, password, nom perso…). |

### Infrastructure serveur
| Fichier | Rôle |
|---|---|
| `engine/server/NetServer.h/.cpp` | Serveur TCP/UDP : accept, dispatch paquets entrants. |
| `engine/server/ServerApp.h/.cpp` | Application serveur principale (init, boucle, shutdown). |
| `engine/server/SessionManager.h/.cpp` | Gestion des sessions actives (session_id ↔ account). |
| `engine/server/RateLimitAndBan.h/.cpp` | Rate limiting par IP + bannissements. |
| `engine/server/ShardRegistry.h/.cpp` | Registre des shards connectés au master. |
| `engine/server/db/ConnectionPool.h/.cpp` | Pool de connexions MySQL. |

---

## 6. Couche rendu Vulkan — fichiers clés

> Pour les modifications visuelles d'auth, seul `engine/render/auth/` est pertinent.
> Le reste concerne le rendu 3D du jeu.

### Auth rendering (le plus utile pour toi)
| Fichier | Rôle |
|---|---|
| `engine/render/AuthImGuiRenderer.h/.cpp` | Point d'entrée rendu auth ImGui. Dispatch vers les sous-renderers. |
| `engine/render/AuthUiRenderer.h` | Interface abstraite du renderer (ancienne piste non-ImGui — toujours présente). |
| `engine/render/auth/AuthUiRendererCore.cpp` | Cœur partagé du renderer auth (init, transitions de phase). |
| `engine/render/AuthLogoPass.h/.cpp` | Passe Vulkan dédiée au logo « Lune Noire » de l'écran d'auth. |
| `engine/render/AuthGlyphPass.h/.cpp` | Passe Vulkan pour le rendu typographique haut de gamme (glyphes Windlass/Morpheus) en complément d'ImGui. |
| `engine/render/FontAtlasTtf.h/.cpp` | Construction d'un atlas TTF (Windlass.ttf) chargé dans ImGui — voir commit `00ad2b5` (2026-04-27). |
| `engine/render/auth/AuthImGuiCommon.h/.cpp` | **Styles partagés : couleurs, polices, boutons, champs.** Modifier ici impacte tous les écrans. |
| `engine/render/auth/screens/AuthImGuiLogin.cpp` | Rendu écran connexion. |
| `engine/render/auth/screens/AuthImGuiRegister.cpp` | Rendu écran inscription. |
| `engine/render/auth/screens/AuthImGuiVerifyEmail.cpp` | Rendu écran vérification email. |
| `engine/render/auth/screens/AuthImGuiForgotPassword.cpp` | Rendu écran mot de passe oublié. |
| `engine/render/auth/screens/AuthImGuiShardPick.cpp` | Rendu écran choix de shard. |
| `engine/render/auth/screens/AuthImGuiCharacterCreate.cpp` | Rendu écran création personnage. |
| `engine/render/auth/screens/AuthImGuiOptions.cpp` | Rendu écran options. |
| `engine/render/auth/screens/AuthImGuiTerms.cpp` | Rendu écran CGU. |
| `engine/render/auth/screens/AuthImGuiLanguageSelect.cpp` | Rendu écran sélection langue. |
| `engine/render/auth/screens/AuthImGuiError.cpp` | Rendu écran erreur. |

### Passes Vulkan (rendu 3D jeu)
| Fichier | Rôle |
|---|---|
| `engine/render/DeferredPipeline.h/.cpp` | Pipeline déferred principal (orchestration des passes). |
| `engine/render/GeometryPass.h/.cpp` | GBuffer (albedo, normales, roughness…). |
| `engine/render/LightingPass.h/.cpp` | Calcul éclairage PBR. |
| `engine/render/BloomPass.h/.cpp` | Post-process bloom. |
| `engine/render/TaaPass.h/.cpp` | Anti-aliasing temporel. |
| `engine/render/TonemapPass.h/.cpp` | Tonemapping final. |
| `engine/render/CascadedShadowMaps.h/.cpp` | Ombres en cascade. |
| `engine/render/ParticleBillboardPass.h/.cpp` | Rendu particules. |
| `engine/render/terrain/TerrainRenderer.h/.cpp` | Rendu terrain. |
| `engine/render/vk/VkDeviceContext.h/.cpp` | Device Vulkan (GPU, queues, allocateur). |
| `engine/render/vk/VkSwapchain.h/.cpp` | Swapchain (frames présentées à l'écran). |

---

## 7. Localisation

| Fichier | Rôle |
|---|---|
| `engine/client/LocalizationService.h/.cpp` | Charge les fichiers JSON de traduction, expose `Tr("clé")`. |
| `game/data/localization/fr/fr.json` | Traductions françaises. |
| `game/data/localization/en/en.json` | Traductions anglaises. |

Les clés utilisées dans les écrans auth commencent par `auth.`, `common.`, `language.`.

---

## 8. Configuration et build

| Fichier | Rôle |
|---|---|
| `CMakeLists.txt` | Config build racine (C++20, cibles, liens). |
| `CMakePresets.json` | Presets : `vs2022-x64` (Windows), `linux-x64` (serveur). |
| `vcpkg.json` | Dépendances vcpkg (Vulkan, ImGui, MySQL connector…). |
| `engine/core/Config.h/.cpp` | Lecture du fichier de config JSON au runtime (`GetInt`, `GetString`…). |
| `config.json` | Config runtime par défaut (logging, endpoints, timeouts). |
| `deploy/docker/config/master.config.json` | Config serveur master en production. |
| `deploy/docker/config/shard.config.json` | Config serveur shard en production. |

---

## 9. Base de données

| Dossier/Fichier | Rôle |
|---|---|
| `db/schema.sql` | Schéma complet (référence). |
| `db/migrations/000N_*.sql` | Migrations numérotées (0001 → 0031). Appliquées en ordre par lcdlln-master au démarrage. |
| `db/migrations/0007_terms_cgu.sql` | Système CGU initial : `terms_editions`, `terms_localizations`, `account_terms_acceptances`. |
| `db/migrations/0008_bug_reports_exploit.sql` | Table `bug_reports` initiale (catégorie, titre, corps, exploit accordé). |
| `db/migrations/0017_game_servers.sql` | Registre des serveurs de jeu (`game_servers`) — le master s'y enregistre/désenregistre. |
| `db/migrations/0018_player_trade_log.sql` | Audit des échanges joueur-à-joueur (anti-scam). |
| `db/migrations/0019_player_professions.sql` | Tracking professions/skill levels par personnage (Linux/MySQL). |
| `db/migrations/0022_character_stats.sql` | Table `character_stats` — `total_play_seconds` + `server_id` (FK → `game_servers`). |
| `db/migrations/0023_accounts_profile.sql` | Profil joueur sur `accounts` : adresse complète, `email_pending` flow, `disabled_reason`, `role`, colonnes parental_*. Idempotent. |
| `db/migrations/0023_accounts_profile_fields.sql` | Champs de profil ajoutés à `accounts` : `first_name`, `last_name`, `birth_date` (corrige les champs ignorés à l'inscription). |
| `db/migrations/0024_characters_soft_delete.sql` | Colonnes `deleted_at` (soft delete) + `force_rename` sur `characters`. |
| `db/migrations/0025_privacy_settings.sql` | Nouvelle table `account_privacy_settings` (visibilité profil : public/friends/none). FK INT UNSIGNED imprécise — corrigée par 0031. |
| `db/migrations/0026_roadmap_items.sql` | Nouvelle table `roadmap_items` pour la roadmap publique gérée depuis le portail web. |
| `db/migrations/0027_faq_items.sql` | Nouvelle table `faq_items` (FAQ portail web avec contrôle de publication). |
| `db/migrations/0028_bug_reports_admin.sql` | Extension `bug_reports` : `admin_status`, `admin_comment`, `exploit_awarded`. |
| `db/migrations/0029_terms_retired_reason.sql` | Colonne `retired_reason` sur `terms_editions` (raison de retrait d'une édition CGU). |
| `db/migrations/0030_terms_editions_nullable_published_at.sql` | `terms_editions.published_at` passe en `TIMESTAMP NULL DEFAULT NULL` (les brouillons n'ont pas de date). |
| `db/migrations/0031_privacy_settings_ensure.sql` | Recrée `account_privacy_settings` avec `BIGINT UNSIGNED` correct — idempotente (`IF NOT EXISTS`). |
| `engine/server/MigrationRunner.h/.cpp` | Applique les migrations au démarrage du serveur. |
| `engine/server/db/ConnectionPool.h/.cpp` | Pool de connexions MySQL réutilisables. |
| `engine/server/db/DbHelpers.h/.cpp` | Helpers requêtes SQL (bind params, lecture résultats). |

**Migrations notables sur `accounts` :**
| Migration | Colonnes ajoutées |
|---|---|
| 0006 | `email_locale`, `email_verified` |
| 0016 | `country_code`, `tag_id` |
| 0023 (`accounts_profile`) | adresse postale complète, `email_pending` + token, `disabled_reason`, `role`, colonnes contrôle parental |
| 0023 (`accounts_profile_fields`) | `first_name`, `last_name`, `birth_date` (fix : champs ignorés à l'inscription) |

---

## 10. Outils et CI

| Fichier | Rôle |
|---|---|
| `.github/workflows/build-windows.yml` | CI build Windows (MSVC). |
| `.github/workflows/build-linux.yml` | CI build Linux (GCC/Clang). |
| `.gitea/workflows/build-test-linux.yml` | Tests Linux sur Gitea. |
| `tools/hlod_builder/` | Génère les niveaux de détail (HLOD) pour les zones 3D. |
| `tools/zone_builder/` | Construit les packages de zones (chunks, GLTF → PAK). |
| `tools/load_tester/` | Simule des connexions massives pour tester la charge serveur. |
| `tools/migration_checksum/` | Vérifie l'intégrité des migrations SQL. |

---

## 11. Tickets / Documentation des milestones

Le dossier `tickets/` contient 405 fichiers Markdown documentant chaque milestone :

- `M00` : Core (logging, mémoire, job system, platform, game loop)
- `M01` : Vulkan (instance, device, swapchain, shaders)
- `M02` : Frame graph, compilation, barriers, assets
- `M03` : Deferred rendering (GBuffer, lighting, matériaux, tonemap)
- `M04` : Shadows (CSM, shadow pass, PCF)
- `M05–M10` : Éclairage avancé, terrain, eau, météo
- `M11+` : Gameplay (skills, craft, guildes, combat, quêtes)
- `M20` : Authentification Argon2
- `M33` : Auth UI complète (inscription, reset pwd, vérif email)
- `M44` : Milestones finaux

Les plans d'implémentation récents sont dans `docs/superpowers/plans/`.

---

## 12. Web portal (Next.js)

**Design system :** Lune Noire (thème médiéval dark fantasy).
**Branche de référence :** `claude/update-web-portal-design-tcKMO`
**Polices :** Cinzel (≈ Windlass) + EB Garamond (≈ Morpheus) via Google Fonts.

### Fichiers de style

| Fichier | Rôle |
|---|---|
| `web-portal/app/globals.css` | **Source unique de vérité CSS.** Tokens `--ln-*`, overlays race, reset, toutes les classes `wp-*`, boutons `.btn`, formulaires `.field`. |
| `design/lune-noire-design-system/colors_and_type.css` | Référence officielle tokens couleurs + typo (Windlass/Morpheus). Ne pas modifier. |
| `design/lune-noire-design-system/ui_kits/web_portal/portal.css` | Source des classes `wp-*` (layout, cards, timeline, accordéon…). |

### Système de classes CSS (`wp-*`)

| Classe | Rôle |
|---|---|
| `wp-main` / `.narrow` / `.wide` | Conteneur de page (max-width 1100 / 700 / 1200 px). |
| `wp-page-header` | Titre + sous-titre de page avec séparateur bas. |
| `wp-hero` | Section héro centrée avec gradient de fond. |
| `wp-stats` / `wp-stat` / `wp-stat-value` / `wp-stat-label` | Grille de statistiques. |
| `wp-section-title` / `wp-section-sub` | Titres de section intermédiaires. |
| `wp-grid` / `wp-grid-2/3/4` | Grilles responsive auto-fit. |
| `wp-card` / `.interactive` | Carte fond semi-transparent ; `.interactive` ajoute hover doré. |
| `wp-badge` / `.done` / `.active` / `.planned` | Pastilles de statut (vert / or / gris). |
| `wp-alert` / `.info` / `.success` / `.warning` / `.error` | Bannières d'alerte colorées. |
| `wp-accordion` / `wp-acc-item` / `wp-acc-trigger` / `wp-acc-body` | FAQ accordéon. |
| `wp-timeline` / `wp-tl-item` / `.done` / `.active` | Timeline roadmap avec ligne verticale et point coloré (`::before`). |
| `wp-tiers` / `wp-tier` / `wp-tier-num` / `wp-tier-label` | Grille de paliers (exploits). |
| `wp-table-wrap` / `wp-table` | Tableau responsive avec header stylisé. |
| `wp-progress` / `wp-progress-fill` | Barre de progression. |
| `wp-divider` | Séparateur horizontal. |
| `wp-header` / `wp-logo` / `wp-nav` | Topbar sticky (voir `SiteHeader.tsx`). |
| `wp-footer` / `wp-footer-links` | Pied de page. |

### Boutons et formulaires

| Classe | Rôle |
|---|---|
| `btn btn-primary` | Bouton principal bleu dégradé. |
| `btn btn-ghost` | Bouton secondaire transparent bordure. |
| `btn btn-accent` | Bouton doré contour. |
| `btn btn-danger` | Bouton rouge destruction. |
| `field` | Wrapper label + input/textarea/select stylisé. |
| `form-stack` | Colonne de champs avec gap. |
| `error-box` / `success-box` | Boîtes de feedback formulaire (composants existants). |

### Overlays race

Appliquer `data-race="elfes|orcs|nains|morts_vivants|corrompus|divins|demons|humains"` sur un ancêtre pour rebrancher toutes les variables `--ln-*` automatiquement.

### Pages et composants

| Fichier | Rôle |
|---|---|
| `web-portal/app/layout.tsx` | Layout racine : `SiteHeader` + `<main>` + `wp-footer`. |
| `web-portal/components/SiteHeader.tsx` | **Server Component** — appelle `getSession()`, délègue nav interactive à `HeaderActions`. |
| `web-portal/components/HeaderActions.tsx` | **Client Component** — menu mobile, liens conditionnels (TAG-ID, Espace joueur, Admin, Déconnexion). |
| `web-portal/app/page.tsx` | Page d'accueil : hero, stats, grille fonctionnalités, accès rapide. |
| `web-portal/app/login/page.tsx` | Connexion : logo lune, `wp-card`, champs `.field`, `wp-alert error`. |
| `web-portal/app/roadmap/page.tsx` | Roadmap dynamique : lecture depuis `roadmap_items` DB, `wp-timeline`. |
| `web-portal/app/bugs/page.tsx` | Signalement bugs : affiche `BugReportForm` si authentifié, sinon message "Connexion requise". |
| `web-portal/app/support/page.tsx` | FAQ dynamique : lecture depuis `faq_items` DB (published=1), accordéon. |
| `web-portal/app/contact/page.tsx` | Contact : infos + formulaire dans `wp-grid-2`. |
| `web-portal/app/admin/page.tsx` | Hub admin : 6 modules (CGU, acceptations, joueurs, roadmap, FAQ, bugs). |
| `web-portal/app/admin/cgu/page.tsx` | CGU admin CRUD : draft/published/retired avec règles métier strictes. |
| `web-portal/app/admin/acceptances/page.tsx` | Suivi acceptations CGU (lecture seule). |
| `web-portal/app/admin/players/page.tsx` | Gestion joueurs : liste paginée, filtres, actions (email, statut, personnages). |
| `web-portal/app/admin/roadmap/page.tsx` | Roadmap admin CRUD : ajouter/modifier/supprimer items. |
| `web-portal/app/admin/faq/page.tsx` | FAQ admin CRUD : questions/réponses publiées ou archivées. |
| `web-portal/app/admin/bugs/page.tsx` | Suivi bugs : changement statut, commentaire admin, attribution exploits. |
| `web-portal/app/player/page.tsx` | Hub espace joueur : nav vers 5 sections + sections existantes. |
| `web-portal/app/player/account/page.tsx` | Détail du compte : profil, email (avec re-validation), adresse postale. |
| `web-portal/app/player/chronicles/page.tsx` | Mes Chroniques : temps de jeu par serveur, exploits, personnages + suppression. |
| `web-portal/app/player/parental/page.tsx` | Contrôle parental : validation tuteur légal pour joueurs mineurs. |
| `web-portal/app/player/security/page.tsx` | Sécurité : changement mot de passe, placeholder MFA. |
| `web-portal/app/player/privacy/page.tsx` | Vie privée : liste CGU (lire + accepter), visibilité du profil. |
| `web-portal/app/player/cgu/page.tsx` | Mes CGU : bannière statut, section "À accepter", historique complet des acceptations. |
| `web-portal/app/cgu/[id]/page.tsx` | Lecture publique d'une CGU publiée (FR/EN), langue via `?lang=` — `notFound()` si non publié. |
| `web-portal/app/player/exploits/page.tsx` | Exploits : délègue à `ExploitsProfile`. |
| `web-portal/app/player/recovery-profile/page.tsx` | Profil récupération : `wp-alert warning` si pas de compte. |
| `web-portal/app/password-recovery/page.tsx` | Récupération mot de passe : `wp-card` info. |
| `web-portal/components/ExploitsProfile.tsx` | Exploits : progress bar, cartes visibles/masquées, stats. |
| `web-portal/components/AccountForm.tsx` | Formulaire compte joueur (Client Component) — infos perso, email, adresse. |
| `web-portal/components/CharacterDeleteButton.tsx` | Suppression personnage en 2 confirmations (Client Component). |
| `web-portal/components/PasswordChangeForm.tsx` | Changement mot de passe (Client Component). |
| `web-portal/components/PrivacyForm.tsx` | Visibilité profil radio buttons (Client Component). |
| `web-portal/components/CguAcceptButton.tsx` | Bouton acceptation CGU (Client Component). |
| `web-portal/components/BugReportForm.tsx` | Formulaire signalement bug : catégorie, titre, corps — POST `/api/bugs` (Client Component). |
| `web-portal/components/admin/PlayerActions.tsx` | Actions joueur admin : email, statut, désactivation motif, personnages. |
| `web-portal/components/admin/CguManager.tsx` | Gestion CGU admin : create/edit/publish/retire (Client Component). |
| `web-portal/components/admin/FaqAdmin.tsx` | CRUD FAQ admin (Client Component). |
| `web-portal/components/admin/BugAdmin.tsx` | Gestion bugs admin : statut, commentaire, exploit award (Client Component). |
| `web-portal/middleware.ts` | Protection routes `/player/*` et `/admin/*` via cookies (Edge Runtime). |
| `web-portal/lib/session.ts` | `getSession()` — lit cookie `lcdlln_portal_account`, retourne `Session \| null`. |
| `web-portal/lib/email.ts` | Module email centralisé — 7 fonctions d'envoi, templates HTML Lune Noire. Lit la config SMTP depuis `config/smtp.local.json` (racine dépôt) en fallback si les variables d'environnement `SMTP_HOST` etc. sont absentes. |
| `web-portal/lib/db.ts` | Pool MySQL partagé, `query<T>()`. |
| `web-portal/lib/portalLogin.ts` | `verifyPortalCredentials()` — double Argon2id + legacy scrypt. |
| `web-portal/lib/gamePasswordHash.ts` | Hash/verify double Argon2id (`@node-rs/argon2`). |
| `web-portal/app/api/auth/login/route.ts` | POST login — set cookies `lcdlln_portal_account` + `lcdlln_portal_role`. |
| `web-portal/app/api/auth/logout/route.ts` | POST logout — supprime les deux cookies session. |
| `web-portal/app/api/player/` | APIs joueur : account PATCH, email change, password, parental, cgu accept, privacy, characters delete. |
| `web-portal/app/api/bugs/route.ts` | POST bug report — insère dans `bug_reports`, auth via cookie session, valide catégorie. |
| `web-portal/app/api/admin/` | APIs admin : players (verify-email, activate, disable), characters (force-rename), roadmap CRUD, faq CRUD, cgu CRUD+publish+retire, bugs PATCH. |
| `web-portal/email-templates/` | Templates HTML email FR/EN actifs lus par `web-portal/lib/email.ts` (welcome, verification, password-reset, account-confirmed, account-disabled, parental-validation, email-change — 14 fichiers). |
| `game/data/email/` | Mêmes templates HTML FR/EN consommés côté C++ par `SmtpMailer` (commit `d7b490b`). |
| `design/lune-noire-design-system/ui_kits/email/` | Source design d'origine — référence visuelle, pas chargée à l'exécution. |

---

## Configuration SMTP (envoi d'e-mail)

`web-portal/lib/email.ts` supporte deux modes de configuration, par ordre de priorité :

### Mode 1 — Variables d'environnement (production recommandée)
| Variable | Description |
|----------|-------------|
| `SMTP_HOST` | Hôte SMTP (ex. `10.0.4.52`) |
| `SMTP_PORT` | Port (défaut : `587`) |
| `SMTP_SECURE` | `"true"` pour SSL/TLS port 465, sinon laisser vide (STARTTLS sur 587) |
| `SMTP_USER` | Identifiant de connexion SMTP |
| `SMTP_PASS` | Mot de passe SMTP |
| `SMTP_FROM` | Adresse expéditeur (ex. `"Lune Noire" <noreply@lune-noire.fr>`) |

### Mode 2 — Fichier local (développement / serveur sans gestionnaire d'env)
Créer `config/smtp.local.json` **dans le dossier `config/`** à la racine du dépôt.
Ce fichier est ignoré par git (`.gitignore`). Un exemple est disponible dans `smtp.local.json.example`.

```json
{
  "smtp": {
    "host": "10.0.4.52",
    "port": 587,
    "user": "user@domain.fr",
    "password": "mot-de-passe",
    "from": "user@domain.fr",
    "starttls": 1
  }
}
```

> **Priorité** : les variables d'environnement priment toujours sur `smtp.local.json`.
> Si `SMTP_HOST` est défini en env, le fichier JSON n'est pas lu du tout.

---

## 13. Tweaks et badges éphémères de l'écran de connexion

### 13.1 Panneau « Tweaks » (bas-droite)

Source unique : `AuthImGuiRenderer::DrawAuthTweaksPanel` dans `engine/render/AuthImGuiRenderer.cpp`.
État maintenu par l'instance du renderer (`m_langTweakRace`, `m_langTweakAnimBg`).
La police du panneau est volontairement plus petite que celle du cadre principal
(`SetWindowFontScale(0.85f)`), et la sélection courante d'une race ou du toggle
ACTIVE / DESACTIVE est signalée à la fois par la **bordure** et la **couleur du texte**
en accent (`LnTheme::kAccent`).

Le titre « TWEAKS » et son bouton de réduction (- / +) ont été retirés en itération 3 :
le panneau est désormais toujours affiché expansé, sans header.

**Hauteur calibrée serrée** (itération 5) : `winH = 160 px` (auparavant 218) pour que le
contenu — « THEME DE RACE » + grille 3×3 + « FOND ANIME » + ACTIVE/DESACTIVE à
`SetWindowFontScale(0.85f)` ≈ 152 px — touche naturellement le bas du cadre. Aucun Dummy
au-dessus du premier label : la position est dictée uniquement par `WindowPadding(12, 12)`.
**Ancrage bas-droite** : `pos = (vpW - winW - 22, vpH - winH - 10)` pour conserver la même
marge externe (10 px du bord bas, 22 px du bord droit) quelle que soit la résolution.
`m_authTweakPanelMinimized` reste comme placeholder mais n'est plus relu — gardé pour
réintroduire la fonctionnalité minimize sans réécrire la struct si besoin.

### 13.2 Toggle « FOND ANIME » → animation décorative du fond auth

`m_langTweakAnimBg` (`bool`) est la source de vérité du toggle ACTIVE / DESACTIVE.
Contrat futur : quand la passe Vulkan d'animation de fond auth sera ajoutée (probablement à
côté de `engine/render/AuthLogoPass` / `engine/render/AuthGlyphPass`), elle devra observer ce
flag et activer/désactiver son émission de commandes en conséquence. Tant que la passe
n'existe pas, le toggle ne fait que conserver l'état visuellement (sélection de bordure +
texte) — voir le commentaire `// Le toggle ACTIVE / DESACTIVE pilote le futur fond animé`
dans `DrawAuthTweaksPanel`.

### 13.3 Badge éphémère « Langue : … »

Lors de la transition `LanguageSelectionFirstRun → Login`, le renderer capture
`rm.infoBanner` (posé par `AuthUiPresenter::ApplyLocaleSelection`) puis l'affiche au-dessus
du cadre central pendant `kLoginLangBadgeDurationSec` secondes (4 s par défaut, dont
`kLoginLangBadgeFadeOutSec` = 1 s de fade-out final).

**Suppression permanente de la double affichage** : tant que `rm.infoBanner` est égal à
`m_loginLangBadgeText` (le texte capturé), le panneau de connexion n'affiche **plus jamais**
ce bandeau à l'intérieur — pas seulement pendant la fenêtre éphémère. Cette persistance évite
que le bandeau « saute » dans le cadre après le fade-out (effet visuel signalé en revue UX).
À l'expiration du timer, `m_loginLangBadgeStartTime` repasse à `-1.0` (plus de fenêtre
flottante), mais `m_loginLangBadgeText` reste mémorisé jusqu'au prochain `Reset()` ou jusqu'à
ce qu'une nouvelle transition LangSel → Login le remplace.

État : `m_loginLangBadgeText`, `m_loginLangBadgeStartTime`, `m_prevPhaseToken` dans
`AuthImGuiRenderer.h`.
Détection : `SyncTransientFromModel` compare `m_prevPhaseToken` au nouveau token (bit 0 =
language sel, bit 1 = login, bit 31 = active) avec un masque `0x7FFFFFFFu` pour ignorer le
bit "active". Rendu : `DrawLoginLanguageBadge(vpW, vpH)`.

### 13.4 Titre login agrandi + marge supérieure

`RenderLoginScreen` applique `SetWindowFontScale(5.0f)` au titre principal (Windlass 13 px →
≈ 65 px), précédé d'un `SetCursorPosY(max(24, vpH * 0.05f))` pour ajouter une bande d'air
au-dessus. Objectif maquette : que le titre remplisse plus de la moitié de l'espace vide
entre le bord supérieur de l'écran et le panneau (qui démarre à `vpH * 0.28f`).

**Stage englobante élargie** : à 5.0x, « LES CHRONIQUES » mesure ~720 px et était clipée
quand le BeginChild faisait 570 px de large. La stage est désormais à `vpW * 0.96f` pour
englober titre + sous-titre + cadre central sans clipping. Le cadre central (panneau de
connexion) reste fixé à 580 px et **doit être centré dans la stage**, pas dans le viewport :
on appelle donc `BeginPanel(stageW=580, titleZoneW, vpH, ...)` avec `titleZoneW` (la largeur
réelle du child englobant) comme 2e argument, sinon `panelX = (vpW - vpW) / 2 = 0` aligne
le panneau contre le bord gauche du child (bug observé en itération 3 → corrigé en it. 4).

**Sous-titre `de la Lune Noire`** : `SetWindowFontScale(2.5f)` (auparavant 1.5f) précédé
d'un `Dummy(0, 8)` pour le descendre légèrement sous la baseline du titre principal.

### 13.6 Aération formulaire login (itération 6 + 7)

`DrawAuthGoldField` accepte maintenant `extraSpacingPx` (défaut 0 → comportement Register
inchangé). Le login passe 6 px → un `Dummy` est inséré entre le libellé en majuscules
accent et le `InputText`. Entre les deux champs, un `Dummy(0, 12)` au call site sépare
« IDENTIFIANT » et « MOT DE PASSE ».

**Hauteur des champs (it. 7)** : `DrawAuthGoldField` pousse également
`FramePadding(10, 8)` autour de l'`InputText` (auparavant le défaut ImGui de
`(4, 3)`). Hauteur effective des champs ≈ 29 px (au lieu de 19 px par défaut).

**Boutons descendus (it. 7)** : au-dessus des liens secondaires (Récupération du mot de
passe, Portail web) : `Dummy(0, 32)` (auparavant 18). Au-dessus des actions principales
(Créer un compte, Se connecter) : `Dummy(0, 28)` (auparavant 14).

**Gap CONNEXION→IDENTIFIANT (it. 7)** : retrait du `ImGui::Spacing()` après le `Separator`
dans `BeginPanel` → gap visuel ≈ 9 px (ItemSpacing.y * 2 + Separator) avec le trait
naturellement centré. Affecte tous les écrans qui utilisent `BeginPanel`.

### 13.5 Limitations Windlass.ttf (police principale)

`game/data/fonts/Windlass.ttf` ne contient pas tous les glyphes Latin-1 supplément :
- Majuscules accentuées : É (0xC9), À (0xC0), Ô (0xD4)…
- Minuscules accentuées spécifiques : ç (0xE7) a notamment été observé manquant.

ImGui les rend en `?` (ou un placeholder visuel). Tant qu'aucune fallback font n'est
fusionnée dans l'atlas (cf. `WorldEditorImGui::Init` — la solution propre serait
`AddFontFromMemoryTTF` en `MergeMode = true` avec une police qui couvre 0x0080-0x00FF), les
libellés français de l'UI auth doivent rester en ASCII pur. Liste des clés concernées :
- `auth.login.maquette_create` (« CREER UN COMPTE »)
- `auth.login.remember_detail` (« CONSERVE L'IDENTIFIANT A LA PROCHAINE OUVERTURE »)
- `language.native_line.fr`, `language.name.fr` (« Francais » au lieu de « Français »)
- `language.apply_success` (« Langue appliquee immediatement : … »)

---

## Aide-mémoire : comment trouver un écran

1. **Je veux changer le visuel d'un écran** → `engine/render/auth/screens/AuthImGuiXxx.cpp`
2. **Je veux changer la logique / les données affichées** → `engine/client/auth/screens/AuthScreenXxx.cpp`
3. **Je veux changer un style global (couleur, police, bouton)** → `engine/render/auth/AuthImGuiCommon.h/.cpp`
4. **Je veux changer un message / traduction** → `game/data/localization/fr/fr.json`
5. **Je veux changer la logique réseau d'un écran** → `StartXxxWorker()` dans le fichier presenter + payload dans `engine/network/AuthRegisterPayloads.h`
6. **Je veux changer ce que le serveur fait à la réception** → `engine/server/XxxHandler.cpp`
7. **Je veux changer l'ordre d'enchaînement des écrans d'auth** → `PollAsyncResult()` et `SubmitCurrentPhase()` dans `engine/client/auth/AuthUiPresenterCore.cpp`. Pour l'auto/forcé du choix de shard côté flux : `engine/network/MasterShardClientFlow.cpp` (variable `m_shardPickWhenMultiple`).

### Limitations connues (à compléter par fonctionnalités futures)

- Pas encore de requête « liste des personnages d'un compte sur un shard ». Après `ShardPick`, le client ne sait pas si le compte a déjà des personnages ; le drapeau `m_postRegistrationCharacterCreatePending` est utilisé en proxy pour décider `ShardPick → CharacterCreate` vs `ShardPick → MasterFlow`. La sélection « jouer avec un personnage existant ou en créer un autre (max 5/shard) » n'a pas encore de protocole ni d'écran dédié.
- Le drapeau `m_postRegistrationCharacterCreatePending` est en mémoire processus ; si l'utilisateur s'inscrit puis ferme l'application avant la création de personnage, il devra (au prochain lancement) s'authentifier puis le serveur devra fournir l'information « pas de personnage sur ce shard » pour rejouer la création (non implémenté).
