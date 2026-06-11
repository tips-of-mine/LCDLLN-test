# Audit global du dépôt — 4 parties (2026-06-10)

> Audit en lecture seule (aucun fichier de code modifié). Couverture : client de jeu,
> serveurs masterd/shardd, éditeur de carte, web-portal, plus une passe transversale
> « code orphelin » au niveau build & données. `legacy/` et `external/` exclus.
> Méthode : croisement CMakeLists ↔ disque, greps de preuve pour chaque « jamais
> utilisé », lecture intégrale des fichiers centraux (Engine, NetServer, mains des
> daemons, routes API du portail, WorldEditorSession/TerrainDocument).

## Légende

- Sévérité : 🔴 haute / 🟠 moyenne / 🟡 basse
- Confiance : **V** = vérifié (preuve grep/lecture) / **S** = suspecté

---

## 1. Constats transversaux (tout le dépôt)

### 1.1 Doublons massifs

| | Constat | Détail |
|---|---|---|
| 🔴 V | `src/client/assets/` (111 fichiers) est un arbre mort | Duplique `game/data/` ; le content root runtime est `paths.content = "game/data"` (config.json:576) ; zéro référence dans src/, CMake, scripts, CI. Reliquat de la réorg 2026-05-09 ; a déjà coûté du travail fantôme (PR #862). → **Supprimer l'arbre entier.** |
| 🟠 V | Templates e-mail dupliqués et déjà divergents | `game/data/email/` (lu par masterd : verification, password-reset, cgu-accepted) vs `web-portal/email-templates/` (lu par le portail). `password-reset.{fr,en}.html` divergent déjà (diff non vide). → Choisir une source canonique ou répartir (ne garder dans game/data que les 3 templates masterd). |
| 🟠 V | `deploy/docker/sql/migrations/` committé et périmé | Il manque 0050→0071 (mais 0072 présent) alors que la CI régénère ce dossier via `sync-db-to-docker-deploy.sh`. → Gitignorer ou resynchroniser. |
| 🟠 S | Deux UIs d'hôtel des ventes compilées ET instanciées | `src/client/auction/AuctionUi.cpp` (`AuctionHousePresenter`, membre `m_auctionHouseUi`) et `src/client/economy/AuctionUi.cpp` (`AuctionUiPresenter`, membre `m_auctionUi`), toutes deux dans Engine.h:1209/1250. → Arbitrer laquelle fait foi. |

### 1.2 Bugs latents de configuration (mismatch de chemins)

| | Constat | Détail |
|---|---|---|
| 🟠 V | `world.default_spawn.{x,y,z,yaw_deg,pitch_deg}` (config.json:561) jamais lu | Le code lit `client.world.default_spawn.*` (Engine.cpp:8417-8421), chemin absent de config.json → les défauts code s'appliquent (identiques par coïncidence). → Harmoniser. |
| 🟠 V | `client.character_creation.default_server_id` (config.json:609) jamais lu | Le code lit `character_creation.default_server_id` sans préfixe (CharacterCreateHandler.cpp:93). → Harmoniser. |
| 🟡 V | 14 autres clés config mortes | `db.delay_thread_*`, `db.prepared_statement_cache_size_per_conn`, `db.sql_storage_log_load_durations`, `globals.default_locale`, `globals.fallback_locale`, `globals.graveyard_default_faction_neutral_radius_m`, `accounts.default_new_account_role`, `accounts.role_change_audit`, `editor.world.camera.fpsSpeed`, `editor.world.terrain.lodWorkers`, `gi.ddgi.intensity` (le commentaire de config.json l.677 dit lui-même qu'elle n'est plus lue). Note : `terms.fallback_locale` est lue par le code mais absente de config.json. |

### 1.3 Tables SQL sans aucun lecteur/écrivain (grep `-w` src/ + web-portal/)

- **V — candidates à DROP** : `reset_tokens` (0005, supplantée par `account_password_reset_tokens`), `auction_listings` (0013, supplantée par `auction_listings_v2`), `bounties`, `character_history`, `character_reputation`, `npc_memory`, `player_legacy`, `prison_records` (0011), `player_currency_log` (0012), `player_professions` (0019).
- **V — scaffolds de phases serveur non câblés au runtime** : `phase_1a_test_storage` (0041), `pool_pool`/`pool_member_state` (0059), `groups`/`group_members`/`group_loot_rolls` (0060), `creature_movement` (0062), `locale_strings` (0042, touchée seulement par des tests). → Garder si les phases arrivent, sinon élaguer ; documenter le pré-provisionnement.
- **S** : `character_auras`, `spell_proc_template` (0061, récente) — probablement du server-first dont le code shardd n'est pas encore mergé. Garder, vérifier que le code suit.

### 1.4 Données / assets orphelins (V sauf mention)

- `game/data/shaders/luminance_reduce.comp.spv` — artefact compilé sans source ni référence ; le ticket M08.6 demandait sa suppression. Idem `src/client/render/shaders/water.{vert,frag}` (copies octet-pour-octet de `game/data/shaders/water.*`, seul le .spv est chargé).
- `game/data/themes/` (4 fichiers) — les vrais thèmes vivent dans `game/data/ui/themes/` ; ce dossier top-level n'est lu par personne.
- `game/data/items/tabard.json` (le tabard est en constantes code), `game/data/particles/weather_rain.json` + `weather_snow.json` (WeatherSystem a des pools internes), `game/data/ui/server_list/server_online.txt`, `game/data/zones/cell_n000_e000/`.
- `game/data/configuration/animations/animation_sets.json`, `equipment/armor_sets.json`, `equipment/sockets_attachments.json` — données anticipées, aucun loader. Garder si le chantier personnages les câblera.
- Localization : `banner.png` ×7 locales + `drapeau.png` de/es/it/pl/pt — seuls fr/en sont chargés (Engine.cpp:3909-3917).
- `game/data/editor/tutorials/first_launch.json` — jamais lu ; contenu dupliqué en dur dans `TutorialIo.cpp`.

### 1.5 Scripts / outils

- **V mort** : `tools/Migrate-CellNaming.ps1` (one-shot déjà joué), `tools/asset_pipeline/detect_fbx_fps.py` (zéro réf).
- **À documenter** : `tools/world_gen/raise_hill.py`, `tools/asset_pipeline/texture_nature.py` (récents, manuels, non documentés).
- Tout le reste (scripts/*.sh, 6 outils CMake) est vivant et référencé par la CI/docs.

---

## 2. Client de jeu (`src/client/`)

### 2.1 Code orphelin

| | Constat |
|---|---|
| 🔴 V | **Module combat entier compilé mais jamais instancié** : `src/client/combat/` (AdvancedCombatUi, AoEPreviewSystem, AuraFXSystem, BuffBarPresenter, CombatHud) + `src/client/social/PartyHud.cpp`. Grep exhaustif : seules auto-références. |
| 🟠 V | Idem : `fx/FXManager.cpp`, `crafting/HarvestCastBar.cpp`, `crafting/CraftingUi.cpp`, `world/ZonePreloadHook.cpp`, `world/PakReader.cpp`. |
| 🟠 V | `settings/SettingsMenuUi.cpp` jamais instancié (le menu Options réel = AuthScreenOptions) + include mort dans AuthUi.h:87. |
| 🟠 V | `ui_common/ThemeManager.cpp/h` — ancien système de thème mort, doublon conceptuel de LnTheme.h (utilisé, lui, par ~20 renderers). |
| 🟠 V | Compilés mais référencés uniquement par leurs tests : `gameplay/ClientPrediction.cpp`, `world/terrain/TerrainLodWorker.cpp`, HazardSimulator, ShadeMapBuilder, HamletKitLibrary, FootstepAudioSurfaceHook. |
| 🟡 V | `world/routine/RoutineSegmentReader.h` — seul header de src/client jamais inclus (cohérent : M101.8/9 différés). |
| 🟡 V | Hot-reload shaders mort : `Engine::WatchShader` (Engine.cpp:12033) sans aucun appelant ; `Poll()`/`ApplyPending()` tournent chaque frame pour rien. |

### 2.2 Anomalies

| | Constat |
|---|---|
| 🔴 V | **Pattern systémique Vulkan** : ~11 passes de post-process (`LightingPass.cpp:720`, `TonemapPass.cpp:435`, `SsaoPass.cpp:337`, `TaaPass.cpp:313`, `BloomPass.cpp:357/703/1053`, `CloudPass.cpp:340`, `UnderwaterPass.cpp:456`, `VolumetricFogPass.cpp:451`, `DepthOfFieldPass.cpp:449`, SsaoBlurPass, `GeometryPass.cpp:1075`) créent un framebuffer temporaire dans Record() puis le **détruisent avant le vkQueueSubmit** (Engine.cpp:11079) → command buffer invalide, UB toléré par le driver actuel. Correctif : router vers `m_deferredDestroyQueue` (existe) ou cache framebuffer (comme ShadowMapPass/WaterPass/DecalPass). |
| 🔴 V | **Collisions de touches in-game** (Engine.cpp:7600/7622/7651 vs 480/12281/12383) : **A** (strafe) toggle aussi le panneau Arena + envoie RequestTeams ; **Y** confirme la vente shop ET toggle Weather ; **G** rafraîchit l'enchère ET toggle BattleGround. Pas de registre central de binds. |
| 🟠 S | `SkinnedRenderer` (h:171, kFrameSlots=32) : ring SSBO de bones partagé entre tous les draws skinnés ; au-delà de ~16 draws/frame avec 2 frames en vol → réécriture d'un slot encore lu (pose corrompue). Aucun assert. |
| 🟠 V | Logout volontaire (Engine.cpp:12014-12031) ne fait **pas** de `SavePositionAsync`, contrairement à la fermeture (7466-7481) → position perdue depuis la dernière sauvegarde périodique. |
| 🟠 V | FrameGraph.cpp:653 (STAB.7) : VMA désactivé, un `vkAllocateMemory` par image → risque `maxMemoryAllocationCount` (souvent 4096) + fragmentation. |
| 🟡 V | Engine.cpp:12132-12138 : commentaire promet un bit-cast préservant le bit 63, le code remplace par 1. |
| 🟡 V | AuthUiPresenterCore.cpp:1683 : `m_asyncResult.ready` lu hors mutex (data race bénigne, UB formel). |
| 🟡 V | Engine.cpp:4111-4123 : log « DeferredPipeline init OK » inconditionnel même si Init a échoué. |
| 🟡 V | Engine.cpp:10895/11061 : échec Begin/EndCommandBuffer → return avec sémaphore `imageAvailable` signalé non consommé. |
| 🟡 V | `WaitConnected` + `ApplyMasterTlsConfig` dupliqués à l'identique ×3 (AuthScreenRegister, AuthScreenForgotPassword, AuthUiPresenterCore). |

### 2.3 Optimisations

- 🟠 V — Config string-keyed : `ToOwnedKey` (Config.cpp:734) alloue un std::string par lookup × ~41 `Get*` par frame dans Update(). → lookup hétérogène ou cache des valeurs chaudes.
- 🟠 V — Chat : `BuildPanelText()`/`BuildHudPanelText()` reconstruisent tout le texte **chaque frame** (Engine.cpp:8244, ChatUi.cpp:443). → reconstruire sur changement (compteur de révision).
- 🟠 V — RecordRemoteAvatars (Engine.cpp:11860-11932) : 3 `vector<Mat4>` par valeur + string + clés concaténées **par avatar et par frame** ; `vkMap/UnmapMemory` par draw (SkinnedRenderer.cpp:580/593). → buffers de pose réutilisables + mapping persistant.
- 🟡 V — Purge `m_remoteSmoothed`/`m_remoteAnims` en O(n·m) par frame (Engine.cpp:12253-12269).
- 🟡 V — `GameplayUdpClient::PollIncoming` (389-424) : vector de vectors alloué par frame.
- 🟡 V — Accumulateur d'envoi input remis à 0 au lieu de soustraire la période (Engine.cpp:12219-12221) → cadence effective < request_tick_hz.

Négatif sain : aucune nouvelle incohérence winding/cullMode (tout est conforme à la doc anti-régression) ; caches framebuffer/descriptors présents là où ça compte ; conventions français/PascalCase respectées dans le code récent.

---

## 3. Serveurs (`src/masterd/`, `src/shardd/`, `src/shared/`)

### 3.1 Anomalies

| | Constat |
|---|---|
| 🔴 V | **Race de concurrence** : `NetServer` (NetServer.cpp:866-890) dispatche sur un pool de N workers (défaut 4, masterd main_linux.cpp:210) mais plusieurs handlers master mutent un état partagé **sans mutex** : `LfgQueue`, `GmTicketSystem`, `MailManager`, `QuestStateTracker` (alors que Weather/Guild/Auction ont leurs mutex). Deux paquets concurrents = data race sur unordered_map. → mutex sur ces structures ou `worker_threads=1` documenté. |
| 🟠 S | Même classe de risque : `RateLimitAndBan` (RateLimitAndBan.h:91) et `PasswordResetStore` (PasswordResetStore.h:88-92) sans verrou, appelés depuis les workers. |
| 🟠 V | `GuildSystem.cpp` (shardd, :564-730) : seul îlot SQL **non converti en prepared statements** (~10 `mysql_query` concaténés, échappés mais fragiles) ; `DbDeleteGuild` (:589-606) supprime 3 tables **sans ScopedTransaction**. Nuance : compilé en mode fichier côté shard (sans ENGINE_HAS_MYSQL) → code DB mort à l'exécution actuelle. → convertir ou retirer (l'autorité semble être MysqlGuildStore côté master). |
| 🟠 V | `ShardTicketValidator.cpp:64-72` : l'ensemble anti-rejeu `m_used_ticket_ids` n'est **jamais purgé** → fuite mémoire lente (les tickets expirent en 60 s, la purge est facile). |
| 🟡 V | `ServerRegistry.cpp:127` : `SELECT id FROM game_servers` — la colonne s'appelle `server_id` (migration 0017) → échec silencieux du chemin ON DUPLICATE KEY (actif seulement si `server.self_register=true`). |
| 🟡 V | `ticket_hmac_secret = "dev_secret_change_in_production"` livré dans les 3 configs `deploy/docker/config/*.json` → si déployé tel quel, forge de tickets d'admission shard possible. → refuser le boot si secret == valeur dev connue. |
| 🟡 V | `SessionManager` documenté « v1 single-threaded » mais son hook `SetOnSessionClosed` appelle `FindConnIdForSession` O(N) ; si `Close` venait d'un worker, race non protégée. → clarifier/verrouiller. |

### 3.2 Code orphelin

- 🟡 V — `src/shared/server_bootstrap/main.cpp` : seul .cpp serveur hors de toute cible CMake (main Windows supplanté par `src/shardd/main_win.cpp`). → supprimer.
- 🟡 S — Opcode `kOpcodeEnterDungeonRequest` (197) + `EnterDungeonHandler` câblés côté serveur, **aucun émetteur côté client** → handler qui répond dans le vide (client différé ?).

### 3.3 Optimisations

- 🟠 V — `/status` portail (masterd main_linux.cpp:1529-1535) : lookup login en **N+1** (une requête par compte en ligne). → `WHERE id IN (...)` ou cache TTL.
- 🟠 V — `ChatRelayHandler.cpp:340-360` : sous-requête guilde exécutée **à chaque message guilde** (idem friends :433), sans cache. → cache d'appartenance invalidé sur join/leave.
- 🟡 V — `ServerApp.cpp:2486-2503` (`MaybeSendSnapshots`) : ré-encodage complet des entités **par destinataire** (O(clients × entités)). → mutualiser par cellule.

Négatifs sains : ownership personnage gaté partout (`WHERE id=? AND account_id=?`), gate UDP `HandleHello` solide (AdmittedCharacterRegistry, TTL pending), pas d'opcode dupliqué, chantier prepared statements quasi complet (sauf l'îlot GuildSystem ci-dessus).

---

## 4. Éditeur de carte (`src/world_editor/` + parties éditeur d'Engine)

### 4.1 État du rapport d'audit 2026-06-05 : **aucun point corrigé depuis**

Toujours ouverts (re-vérifiés) : P0 3.1 `reserve(count)` non plafonné (MeshInsertIo.cpp:149, DungeonPortalIo.cpp:147, VMapBridge.cpp:265) ; P0 6.1 Zone Presets sans rollback ; P0 4.1 érosion thermale non conservative ; 6.4 presets d'outils ignorés (OperationParams.cpp:135 / OperationDispatcher.cpp:515) ; 3.3 atmosphère écrite imbriquée mais relue en clé plate (WorldMapIo.cpp:1310 vs :1471) → jamais relue ; 1.1-1.4, 5.1, 5.2, 5.6, bloc `#if 0` de ~245 lignes (WorldEditorImGui.cpp:1363).

### 4.2 Anomalies nouvelles — intégrité des données ⚠️

| | Constat |
|---|---|
| 🔴 V | **Charger une carte ne réinitialise pas `TerrainDocument`** (WorldEditorSession.cpp:482-508, 615-661) : les chunks de la carte A restent en mémoire ; la première commande moderne sur la carte B déclenche `SyncWorldEditorHeightmapFromDocument` (Engine.cpp:12793) qui **réécrit la heightmap de B avec les hauteurs de A**. |
| 🔴 V | **Undo trans-cartes** : `InitNewZoneTerrain` (WorldEditorShell.cpp:752) vide les chunks mais jamais `m_commandStack` (`CommandStack::Clear()` existe — et n'est appelé nulle part). Ctrl+Z après « Nouvelle carte » rejoue les deltas inverses de l'ancienne carte. |
| 🔴 V | **Chemins chunks non namespacés par zone** (TerrainDocument.cpp:62-65, 138-139, 254-256) : `game/data/chunks/chunk_X_Z/terrain.bin` sans zoneId → deux cartes s'écrasent mutuellement au save. |
| 🔴 V | **Perte silencieuse au save** : `WaterDocument`, `MeshInsertDocument`, `DungeonPortalDocument` n'ont **aucun** appel `SaveToDisk` hors tests (seul SaveTerrainChunks persiste terrain+splat, WorldEditorShell.cpp:760). Lacs, rivières, grottes, arches et portails placés sont perdus à la fermeture. |
| 🔴 V | **Câblage réel des outils : 4/15, pas 15/15** (Engine.cpp:9904-9965) : seuls Sculpt, Stamp, MountainRange, ValleyChain reçoivent les inputs viewport. `SplatPaintTool::OnMouseDown/Move/Up` n'est appelé nulle part ; pire, pour les 11 autres outils le **pinceau legacy Raise** sculpte le terrain quand on clique. |
| 🔴 V | **Double vérité heightmap** (Engine.cpp:10021-10057 vs 12834-12861) : le pinceau legacy édite la heightmap seule ; la sync document→heightmap réécrit ensuite chaque cellule couverte → coups de pinceau legacy silencieusement effacés. |
| 🟠 V | Pas de pont SplatMap chunks → GPU (SplatPaintCommand.cpp:27-74) : même câblé, le paint moderne ne s'affichera pas (aucun observer symétrique à la sync heightmap). |
| 🟠 V | `terrain.bin` corrompu → chunk plat silencieux non-dirty (TerrainDocument.cpp:93-101) ; la première édition + save **écrase l'original**. |

### 4.3 Code orphelin nouveau (le stock croît au lieu de se résorber)

- 🔴 V — **Tutoriel interactif M100.49 (PR #818) non câblé** : `tutorial/` + `help/OverlayGuidanceSystem` + `WidgetTargetRegistry` référencés uniquement par leurs tests ; les cibles du tutoriel (`toolbar.button.validate`, `panel.validation`, `menubar.file.new_from_preset` — TutorialIo.cpp:46-70) **n'existent nulle part** dans le shell.
- 🟠 V — `diagnostic/` (PR #819) : DiagnosticSystem + 10 règles jamais instanciés hors tests.
- 🟠 V — `wizard/` (PR #820) : QuickStartWizard/AutoGenerators jamais instanciés hors tests ; dépendra de ZonePresetExecutor dont le P0 rollback est ouvert.
- 🟠 V — `ui/EditorAudioPanel.{h,cpp}` : **hors CMake**, référencé par personne (M43.5 inachevé).
- 🟠 V — `InteractivePanel.{h,cpp}` : compilé, référencé par personne, pas même un test.
- 🟠 V — `WorldEditorExporter.{h,cpp}` : utilisé seulement par un test ; l'export réel passe par `WorldMapIo::ExportRuntimeBundle` → **deux pipelines d'export parallèles**.
- 🟡 V — Headers jamais inclus (non compilés, donc même pas validés par le compilateur) : `BridgeTool.h`, `FoliagePaintCommand.h`, `HamletGeneratorTool.h`, `HazardDocument.h`, `SplineCommand.h`, `WallTool.h`, `ui/IToolPropertiesPanelContent.h`, `PlaytestMode.h`.
- 🟡 S — `routine/RoutineGraphDocument` sans aucune sérialisation : tout graphe construit est perdu à la fermeture (cohérent M101.8/9 différés — dette à tracer).

### 4.4 Optimisations

- 🔴 V — Pinceau splat/grass legacy : `FlushSplatMap`/`FlushGrassMask` **à chaque frame du drag** = re-upload complet + `vkQueueSubmit` + **`vkQueueWaitIdle`** (stall GPU par frame) + command pool/staging recréés + LOG_INFO par frame (Engine.cpp:10033/10041, TerrainEditingTools.cpp:255-260, 806). → flush au mouse-up / throttle + staging du frame.
- 🟠 V — `OnChunkChanged` jette la coordonnée (flag global, Engine.cpp:12692-12695) : chaque commit/undo re-parcourt **tous** les chunks et re-upload la heightmap entière (~10-30 ms admis). → set de coords dirty.
- 🟠 V — `SplatPaintCommand::TryMerge` (:102-117) : le commentaire promet une map (x,z), le code fait un scan linéaire par cellule → O(N²) par tick de stroke.
- 🟡 V — LOG_INFO à chaque sync document→heightmap (Engine.cpp:12862).

### 4.5 Documentation

🟡 V — Règle « toute fonction éditeur en `///` » globalement respectée, sauf `WorldEditorImGui.cpp` (1934 lignes, 0 `///`) et `InteractivePanel.cpp`.

---

## 5. Web-portal (`web-portal/`)

### 5.1 Anomalies (sécurité d'abord)

| | Constat |
|---|---|
| 🔴 V | **Aucun rate-limiting sur le login** (app/api/auth/login/route.ts) ; combiné à Argon2id 64 Mo par tentative → brute-force ET vecteur DoS. |
| 🟠 V | **Oracle temporel d'énumération** (lib/auth/portalLogin.ts:52-54) : identifiant inconnu → retour immédiat sans Argon2 ; compte existant → 2 hashes. → dummy verify. |
| 🟠 V | **Aucun header de sécurité** (ni next.config.mjs, ni nginx, ni middleware) : pas de CSP, HSTS, X-Frame-Options, X-Content-Type-Options, Referrer-Policy. |
| 🟠 S | **Pas de défense CSRF** au-delà de `SameSite=lax` sur toutes les mutations player/admin. → vérifier `Origin` ou token CSRF. |
| 🟠 V | **Endpoints GET mutants** : confirm-email (route.ts:1130) et parental/validate (:1417) modifient la base sur un GET (prefetch/CSRF par image) ; leurs tokens sont stockés **en clair** dans `accounts`, contrairement au reset password (haché SHA-256). → hacher + POST. |
| 🟡 V | request-email-change renvoie 409 « déjà utilisée » → énumération d'e-mails (auth requise, mineur). |
| 🟡 V | Pas de rate-limit sur request-email-change et parental/request (envoi d'e-mails) — seul le recovery plafonne (3/h). |
| 🟡 V | PATCH FAQ : `display_order` non coercé en nombre (faq/[id]/route.ts:474). |
| 🟡 V | Fallback `verifyLegacyScryptPassword` toujours actif (portalLogin.ts:10-24) — vérifier qu'aucun hash legacy ne subsiste puis retirer. |
| 🟡 V | Calcul d'âge parental par 365.25 jours (parental/request/route.ts:1387) au lieu du calcul calendaire (`computeAgeYears` existe déjà) — enjeu légal aux bornes d'anniversaire. |

Négatifs sains : SQL 100 % paramétré, Argon2id bien paramétré, sessions opaques 256 bits avec autorité re-lue en DB, IDOR corrigés, pas de secret en dur, reset password exemplaire (haché, 10 min, usage unique, anti-énumération).

### 5.2 Code orphelin

- 🟡 V — `lib/exploits/exploitTier.ts` entier (réimplémenté en SQL inline dans admin/bugs/[id]/route.ts:95).
- 🟡 V — 3 senders e-mail jamais appelés (sendVerificationEmail, sendWelcome, sendAccountConfirmed — ces e-mails partent du serveur de jeu) + **8 templates morts** (verification/welcome/account-confirmed ×2 langues, et `cgu-accepted.{fr,en}` sans aucun sender côté portail).
- 🟡 V — Hiérarchie de rôles jamais exploitée : `isPlayer`, `hasAtLeast`, `roleLevel` (l'autorisation est binaire staff/player).
- 🟡 V — CSS mort : `wp-stat-val`, `wp-grid-4`, `wp-table-wrap`, `wp-table` + descendants (tout le style de table admin).

### 5.3 Optimisations

- 🟠 V — `ensurePasswordRecoveryTables()` : 3 `CREATE TABLE IF NOT EXISTS` **à chaque requête** recovery (passwordRecovery.ts:117-162 et appels). → migration SQL au boot, retirer du chemin requête.
- 🟡 V — Pool mysql2 sans `connectionLimit` explicite ; transporter nodemailer recréé à chaque e-mail (mémoïser + pool SMTP).

---

## 6. Synthèse des thèmes dominants

1. **Le pattern « noyau livré non câblé » est systémique** : module combat client, tutoriel/diagnostic/wizard éditeur, ClientPrediction, TerrainLodWorker, EnterDungeon serveur, hiérarchie de rôles portail… Le code est testé unitairement mais aucune passe de câblage UI/runtime ne suit. Le stock **augmente** (3 sous-systèmes éditeur ajoutés depuis l'audit du 05-06).
2. **Intégrité des données de l'éditeur** : c'est le risque le plus concret pour le travail des mappeurs (corruption inter-cartes, perte eau/mesh/portails au save, chunks non namespacés, double vérité heightmap).
3. **Deux bugs runtime sérieux côté infra** : la race multi-worker des handlers master sans mutex, et la destruction de framebuffers avant submit dans ~11 passes Vulkan.
4. **Sécurité portail** : fondamentaux sains, mais le périmètre transverse (rate-limit login, headers, CSRF, GET mutants) reste à faire.
5. **Ménage rentable** : ~50 fichiers orphelins vérifiés + l'arbre `src/client/assets/` (111 fichiers) + 10 tables SQL DROP-candidates + 16 clés config mortes (dont 2 mismatches = bugs latents).

## 7. Propositions de lots (à discuter)

- **Lot A — Ménage sans risque** (suppressions pures, zéro impact runtime) : `src/client/assets/`, `game/data/themes/`, shaders morts, scripts one-shot, templates e-mail morts, `server_bootstrap/main.cpp`, CSS mort, `exploitTier.ts` ; + migration DROP des tables supplantées (`reset_tokens`, `auction_listings`).
- **Lot B — Corrections critiques** : B1 mutex handlers master (serveur) ; B2 framebuffers → DeferredDestroyQueue (client) ; B3 intégrité éditeur (reset document au load, Clear() du command stack, SaveToDisk eau/mesh/portails, namespacing chunks par zone) ; B4 rate-limit login + headers sécurité (portail) ; B5 collisions de touches A/Y/G (client) ; B6 save position au logout.
- **Lot C — Câbler ou supprimer (décision par feature)** : combat UI client, tutoriel/diagnostic/wizard éditeur, EditorAudioPanel, doublon AuctionUi, EnterDungeon, ClientPrediction/TerrainLodWorker, hot-reload shaders.
- **Lot D — Optimisations** : flush splat au mouse-up (éditeur, le plus gros gain perçu), sync heightmap par chunk dirty, cache config/chat client, N+1 `/status` et cache guilde (serveur), CREATE TABLE hors chemin requête (portail).
- **Lot E — Process** : règle « pas de merge d'un sous-système sans sa passe de câblage » + registre central des binds clavier in-game.

---

*Rapports d'agents source : 5 audits parallèles (client, serveur, éditeur, portail, transversal), 2026-06-10. Aucune modification de code effectuée.*
