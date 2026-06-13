# Audit global indépendant — 4 parties (2026-06-13)

> Audit **en lecture seule** (aucun fichier de code modifié), conduit par 5 agents
> parallèles **indépendants** : chacun a analysé son périmètre **sans consulter**
> l'audit précédent (`2026-06-10-audit-global-4-parties.md`), pour fournir une
> **confirmation croisée**. `legacy/` et `external/` exclus.
> Méthode par agent : croisement `CMakeLists.txt` ↔ disque, grep de preuve pour
> chaque « jamais utilisé », lecture intégrale des fichiers centraux.

## Légende

- Sévérité : 🔴 haute / 🟠 moyenne / 🟡 basse
- Confiance : **V** = vérifié (preuve grep/lecture) / **S** = suspecté
- État : 🆕 nouveau vs audit 06-10 / ✅ corrigé depuis (B1/B2/B3) / ⏳ toujours ouvert

---

## 0. Résultat du croisement avec l'audit du 2026-06-10

L'audit indépendant **confirme** que les 3 lots de correctifs critiques mergés
(`#887` B1, `#888` B2, `#889` B3) sont **réellement câblés**, pas seulement définis :

| Lot | Constat 06-10 | Vérification indépendante 06-13 |
|---|---|---|
| **B2** (client) | UB destroy-framebuffer-avant-submit ×11 passes | ✅ Le chemin de resize fait `vkDeviceWaitIdle` + invalidation explicite de tous les caches framebuffer par passe avant `frameGraph.destroy`. Aucune anomalie de cycle de vie GPU restante détectée. |
| **B1** (master) | races multi-worker sans mutex (Lfg, GmTicket, Mail, Quest) | ✅ `LfgQueue`, `GmTicketSystem` (commentaire « Audit Lot B1 »), `QuestStateTracker`, `WeatherHandler`, Guild/Auction/Arena/BG/OutdoorPvp tous verrouillés. Purge anti-rejeu tickets OK. **⚠️ MAIS un système a été oublié — voir 2.1.** |
| **B3** (éditeur) | corruption inter-cartes, perte eau/mesh au save, undo trans-cartes, chunks non namespacés | ✅ `ResetForZoneChange` (clear undo + reset documents) câblé au new/load ; `SaveZoneDocuments`/`LoadZoneDocuments` câblés ; namespacing `chunks/zone_<id>/` + fallback legacy. Les 4 points 🔴 de la section 4.2 du 06-10 sont résorbés. |

**Valeur ajoutée de la passe indépendante** : 1 nouvelle race critique côté serveur
que B1 n'a pas couverte, 1 nouveau risque de perte de données éditeur, et plusieurs
constats raffinés/corrigés (câblage outils éditeur, état du module combat client).

---

## 1. Client de jeu (`src/client/`)

### 1.1 Code orphelin (compilé, jamais instancié — preuve grep)

| Symbole | Fichier | Sév. | Conf. | État |
|---|---|---|---|---|
| `ZonePreloadHook` | `world/ZonePreloadHook.{h,cpp}` | 🟠 | V | ⏳ |
| `FXManager` + chaîne `ParticleSystem`/`ParticleBillboardPass` | `fx/`, `render/` | 🟠 | V | ⏳ |
| `AoEPreviewSystem` | `combat/AoEPreviewSystem.cpp` | 🟠 | V | ⏳ |
| `ClientPredictionSystem` | `gameplay/ClientPrediction.{h,cpp}` | 🟠 | V | ⏳ (cohérent : prédiction = travail à venir) |
| `AutoFitProxy` | `world/collision/AutoFitProxy.{h,cpp}` | 🟡 | V | ⏳ (test-only) |

> **Correction vs 06-10** : le module combat n'est **plus** entièrement orphelin —
> les chantiers Combat SP1-4 (2026-06-10/11) ont câblé l'essentiel. Seul
> `AoEPreviewSystem` reste isolé. Redondance fonctionnelle à arbitrer : **deux UI
> d'enchères** coexistent et sont toutes deux instanciées (`m_auctionUi` +
> `m_auctionHouseUi`).

### 1.2 Anomalies

| Constat | Emplacement | Sév. | Conf. |
|---|---|---|---|
| **Collisions de raccourcis clavier** : même touche = toggle panneau **+** action gameplay-net dans la même frame. `H`→toggle Hôtel des Ventes + `SendTalkRequest` ; `G`→refresh enchère + toggle BattleGround ; `B`→bid + toggle SkillBook ; `L`→list + toggle LootRoll. Gating incohérent entre le bloc toggles (`inGameNoMenu`) et le bloc net (`!chatBlocks` seul). | `Engine.cpp` 7587-7718 vs 13461-13613 | 🟠 | V |
| **Ring SSBO bones partagé** (`kFrameSlots=32`, 2 frames en vol) : au-delà de ~16 draws skinnés/frame, un slot encore lu est réécrit → pose corrompue. Aucun garde-fou (limite documentée en commentaire). | `SkinnedRenderer.cpp:565-595`, `.h:171` | 🟠 | V |
| Logs de diagnostic latchés à vie (`static bool`) — silence trompeur après le 1er event. | `Engine.cpp:12807,12834` | 🟡 | V |

### 1.3 Optimisations

| Constat | Emplacement | Sév. |
|---|---|---|
| 3 `std::vector<Mat4>` alloués **par avatar par frame** (Sample→ComputeGlobal→ComputeFinal par valeur). | `Engine.cpp:12933`, `AnimationSampler.h:39` | 🟠 |
| `vkMapMemory`/`vkUnmapMemory` **par draw** au lieu d'un mapping persistant. | `SkinnedRenderer.cpp:580-595` | 🟠 |
| `GetRaceMesh` : concat de string allouante + lookup map par avatar par frame. | `Engine.cpp:12877` | 🟡 |

**Négatifs sains** : resize/shutdown GPU rigoureux (LIFO documenté, caches invalidés) ;
`vkQueueWaitIdle` cantonné à l'init/upload one-shot, aucun stall en boucle de rendu ;
UDP client propre (Goodbye anti-fantôme) ; RenderState double-bufferisé anti-race.

---

## 2. Serveurs (`masterd` / `shardd` / `shared`)

### 2.1 🆕 Anomalie critique — race oubliée par B1

> 🟠 **V — Data race sur `TradeSessionRegistry`.** `src/masterd/trade/TradeSessionRegistry.h:60-65`
> mute `m_sessions` + `m_byAccount` **sans mutex**, alors que le pool NetServer
> dispatche sur 4 workers. Le header affirme à tort « appel synchrone thread
> principal ». `TradeHandler` appelle `Begin/GetByAccount/End` depuis `HandlePacket`
> (opcodes 83/86/88/91/93 câblés). Deux `TRADE_BEGIN` concurrents corrompent les
> `unordered_map`. **B1 a verrouillé Lfg/GmTicket/Quest/Weather mais a manqué Trade.**
> → Ajouter un `std::mutex` couvrant toutes les opérations (même pattern que B1).

Même classe de risque, moindre criticité : `ReputationManager` (sans verrou, mais
seul l'opcode lecture est câblé aujourd'hui — piège futur), TOCTOU bénin sur
`IgnoreListManager::Ignore`.

### 2.2 Anomalies confirmées

| Constat | Emplacement | Sév. | Conf. |
|---|---|---|---|
| **Secrets en dur** : `ticket_hmac_secret = "dev_secret_change_in_production"` **identique** dans master+shard → forge de tickets d'admission shard (usurpation `character_id`). `password = "lcdlln_pass_dev"` (BDD). | `deploy/docker/config/{master,shard}.config.json` | 🔴 | V |
| `DELETE`+`INSERT` sans transaction (régénérable). Constat large : **0 transaction SQL** dans tout le serveur. | `MysqlAccountStore.cpp:373-389` | 🟡 | V |
| Tables livrées en avance sans lecteur/écrivain runtime : `character_auras`, `spell_proc_template`, `creature_movement`, `groups`/`group_members`/`group_loot_rolls`, `pool_pool`/`pool_member_state`, `conditions`/`graveyards`/`locale_strings`, `phase_1a_test_storage`. | `sql/migrations/` | 🟡 | V |

### 2.3 Code orphelin

- 🟡 V — `src/shared/server_bootstrap/main.cpp` : entrypoint Windows obsolète, hors
  de toute cible CMake (le boot réel = `shardd/main_win.cpp`). → supprimer.
- 🟡 V — `ConditionMgr`/`GraveyardManager`/`LocaleStrings` compilés **uniquement**
  dans les cibles de test, jamais linkés en prod.

### 2.4 Optimisations

- 🟡 V — `/online` portail : N+1 (`SELECT login … WHERE id=?` par compte connecté). → `IN (…)`.
- 🟡 V — `ChatRelayHandler.cpp:490` : ré-encodage du paquet **par destinataire** en broadcast. → encoder le corps une fois, ne réécrire que l'en-tête.
- 🟡 S — `TradeHandler::FindConnIdForAccount` O(n) par transition de trade.

**Négatifs sains** : SQL 100 % paramétré (aucune injection) ; thread-safety correcte
partout sauf Trade ; admission UDP solide (`AdmittedCharacterRegistry` verrouillé,
TTL pending) ; rate-limit/anti-DDoS câblés en profondeur ; ownership personnage gaté.

---

## 3. Éditeur de carte (`world_editor` + parties éditeur d'`Engine`)

### 3.1 Code orphelin — **le stock reste élevé** (12 sous-systèmes testés mais jamais instanciés)

`TutorialSystem`, `OverlayGuidanceSystem`, `DiagnosticSystem`, `QuickStartWizard`,
`ZoneValidator`, `InteractivePanel`+`PlacementTool`, `ForestTool`/`FieldTool`/`FoliagePaintTool`,
`HazardTool`/`WindZoneTool`/`ZoneTool`, `BridgeTool`/`WallTool`/`HamletGeneratorTool`,
`SelectionTool`, `PlaytestMode` (header seul, 0 référence). **Tous ont un fichier de
test mais ne sont jamais câblés au Shell** (Phase 12 accessibilité + tools livrés
« cœur pur » sans passe de câblage). Le menu Help affiche encore un stub bootstrap.

### 3.2 Anomalies — intégrité des données

| Constat | Sév. | Conf. | État |
|---|---|---|---|
| **Câblage réel des outils : 4/16 via viewport** (Sculpt, Stamp, MountainRange, ValleyChain). Nuance : les outils **eau** passent par un canvas 2D top-down (utilisables) ; érosion/cave/arch/portal ont un bouton Apply. **Seul `SplatPaint` est vraiment cassé** : `OnMouseDown/Move/Up` conçus pour le viewport mais **jamais appelés**, et son bouton panel est un no-op explicite (« needs Config plumbing — TODO »). | 🔴 | V | ⏳ |
| **« Double vérité » heightmap** : le pinceau **legacy** (`TerrainEditingTools::ApplyBrush`) écrit directement la heightmap **sans** document/undo → coups **non undoables, non chunk-backed, non persistés** par `SaveTerrainChunks`. | 🟠 | V | ⏳ (non couvert par B3) |
| 🆕 **Save inconditionnel après échec de load** : un `.bin` corrompu → document vidé + `LOG_WARN`, puis `SaveZoneDocuments` réécrit **sans condition** → le fichier réparable est **écrasé par un fichier vide**. Aucun garde-fou « ne pas sauver si le dernier load a échoué ». | 🟠 | V | ⏳ |
| `reserve(instanceCount)` non borné (jusqu'à 4 G) sur `.bin` corrompu → DoS mémoire mineur. | 🟡 | V | ⏳ |
| Reset TerrainDocument + CommandStack au load/new | ✅ | V | **corrigé B3** |
| Save eau/mesh/portails hors tests | ✅ | V | **corrigé B3** |
| Namespacing chunks par zone | ✅ | V | **corrigé B3** |

### 3.3 Optimisations

| Constat | Sév. |
|---|---|
| 🔴 **Stall GPU par frame de drag (splat/grass)** : `UploadToImage` fait `vkCreateCommandPool`→submit→**`vkQueueWaitIdle`**→`vkDestroyCommandPool` **à chaque frame** où le bouton est maintenu (`FlushSplatMap`/`FlushGrassMask`). → réutiliser un pool long-lived + staging ring + barrière (comme le pinceau hauteur, qui lui est déjà correct). | 🔴 |
| `LOG_INFO("FlushSplatMap OK")` **par frame de drag**. → LOG_TRACE/supprimer. | 🟠 |
| `TerrainSculptCommand::TryMerge` concatène `cells` **sans dédupliquer** → enflure mémoire/coût undo (correctness OK car deltas relatifs). | 🟡 |
| `SyncWorldEditorHeightmapFromDocument` re-parcourt tous les chunks (atténué : flag one-shot, pas par-frame). | 🟡 |

**Négatifs sains** : B3 réellement câblé ; désérialisation bornée (magic+version+readers
bornés) ; coalescing pile undo O(1) ; pinceau hauteur via staging dirty-flag sans stall ;
pool eau réutilisé (pattern à répliquer pour splat/grass).

**Doc** : règle `///` respectée sur le Shell/ZonePaths/B3, mais **violée** sur les
fichiers orphelins (tools racine, InteractivePanel, SelectionTool).

---

## 4. Web-portal (`web-portal/`)

### 4.1 Sécurité (priorité — portail exposé)

| Constat | Emplacement | Sév. | Conf. |
|---|---|---|---|
| **Aucun rate-limiting** (grep `rate.?limit\|429` = 0). Login brute-forçable ; `request-email-change` et `parental/request` permettent un envoi SMTP vers adresse arbitraire sans plafond (spam/relais). Seul `passwordRecovery` plafonne (3/h). | login/route.ts, player/* | 🔴 | V |
| **Oracle temporel d'énumération** : identifiant inconnu → retour immédiat sans Argon2 ; compte connu → hash coûteux. | `lib/auth/portalLogin.ts:51` | 🔴 | V |
| **Aucun en-tête de sécurité** (CSP/HSTS/X-Frame-Options/X-Content-Type-Options/Referrer-Policy) — ni next.config, ni middleware, ni nginx. | — | 🔴 | V |
| **Endpoints GET mutants** : `confirm-email` et `parental/validate` font `UPDATE` sur GET (prefetch/CSRF par `<img>`). | player/account, player/parental | 🟠 | V |
| Défense CSRF limitée à `SameSite=lax` (aucune vérif Origin/token sur les mutations). | — | 🟠 | V |
| Tokens e-mail (`email_pending_token`, `parental_token`) stockés/comparés **en clair** — alors que le reset password, lui, hache en SHA-256. | request-email-change, parental/request | 🟡 | V |
| Incohérence `NEXT_PUBLIC_PORTAL_URL` (recovery) vs `PORTAL_URL` (autres senders) — `requireEnv` jette → moitié des flux e-mail casse si une seule est définie. | passwordRecovery.ts:289 vs sender.ts:110 | 🟡 | V |

### 4.2 Code orphelin

- 🟡 V — `lib/exploits/exploitTier.ts` entier (jamais importé ; logique réimplémentée en SQL inline).
- 🟡 V — 3 senders jamais appelés (`sendVerificationEmail`/`sendWelcome`/`sendAccountConfirmed`) + **8 templates morts** (verification/welcome/account-confirmed ×2 langues + `cgu-accepted.{fr,en}` sans aucun sender).
- 🟡 V — Hiérarchie de rôles non exploitée (`isPlayer`/`roleLevel`/`hasAtLeast` jamais importés ; autorisation binaire staff/player).

### 4.3 Optimisations

- 🟠 V — `ensurePasswordRecoveryTables()` : **3 `CREATE TABLE IF NOT EXISTS` par requête** sur les 3 routes recovery. → migration SQL au boot.
- 🟠 V — Pool mysql2 sans `connectionLimit`/`queueLimit` explicites.
- 🟠 V — Transporter nodemailer recréé à chaque e-mail (pas de pool SMTP). → singleton `pool:true`.

**Négatifs sains** : SQL 100 % paramétré (SET dynamiques via fieldMap codé en dur) ;
`requireAdmin()` sur 14/14 routes admin, rôle relu en DB ; sessions opaques 256 bits ;
IDOR scoppés par session ; Argon2id double + comparaisons timing-safe ; aucun secret en dur.

---

## 5. Transversal (build / données / config)

| # | Constat | Sév. | Conf. |
|---|---|---|---|
| 5.1 | **`deploy/docker/sql/migrations/` périmé** : 49 fichiers committés, **0050→0071 manquants** (22 migrations). Artefact régénéré par `sync-db-to-docker-deploy.sh` mais suivi git ; un `docker build` manuel **sans** la sync embarque un schéma amputé. | 🔴 | V |
| 5.2 | **Templates `password-reset` divergents** : « 30 minutes » (game/data/email, lu par masterd) vs « 10 minutes » (web-portal). Au moins un texte utilisateur est faux. | 🔴 | V |
| 5.3 | **`client.character_creation.default_server_id` jamais lu** : le code lit `character_creation.default_server_id` (sans préfixe `client.`) → valeur admin **silencieusement ignorée**, retombe sur le défaut codé `1`. | 🔴 | V |
| 5.4 | **`game/data/shaders/luminance_reduce.comp.spv`** : `.spv` suivi par git **sans source ni référence** (le pipeline charge `luminance_histogram*`). | 🔴 | V |
| 5.5 | `game/data/themes/` : arbre de thèmes orphelin (le live = `game/data/ui/themes/`). | 🟠 | V |
| 5.6 | `deploy/docker/server-config/` + `Dockerfile.server{,.build}` : topologie de déploiement **legacy** (binaire unique) non utilisée par compose/CI. | 🟠 | V |
| 5.7 | 38 `.spv` suivis par git alors que `.gitignore` les ignore (`git rm --cached`). | 🟠 | V |
| 5.8 | `configuration/equipment/*.json` + `animation_sets.json` : aucun loader (pré-provisionné CHAR-MODEL.25). | 🟠 | V |
| 5.9 | Locales `de/es/it/pl/pt` : pas de catalogue `<locale>.json` → non enregistrées au runtime ; 10 PNG orphelins. | 🟡 | V |
| 5.10 | `src/world_editor/ui/EditorAudioPanel.{h,cpp}` : sur disque, **hors de toute cible**, jamais référencé. | 🟠 | V |
| 5.11 | `tools/Migrate-CellNaming.ps1` : one-shot déjà joué. | 🟡 | V |
| 5.12 | ~14 clés config mortes (jamais lues) : `gi.ddgi.{origin_m,spacing_m,counts,intensity}`, `db.delay_thread_*`, `db.prepared_statement_cache_size_per_conn`, `globals.{default_locale,fallback_locale,...}`, `accounts.{default_new_account_role,role_change_audit}`, `editor.world.camera.fpsSpeed`, `editor.world.terrain.lodWorkers`. | 🟡 | V |

> ⚠️ **Point hors-périmètre relevé par l'agent serveur** : le code et les commentaires
> contiennent encore **massivement** la référence à l'autre serveur de jeu (préfixes
> `CMANGOS.xx` dans `main_linux.cpp`, `src/CMakeLists.txt`, headers), en contradiction
> directe avec la règle projet « ne pas utiliser ce terme ». Candidat à un nettoyage
> transverse dédié.

---

## 6. Synthèse — thèmes dominants (post B1/B2/B3)

1. **B1/B2/B3 confirmés câblés et efficaces** — mais B1 a laissé **`TradeSessionRegistry`
   sans mutex** (même classe de bug, oublié) : c'est le constat 🆕 le plus important.
2. **Le pattern « noyau livré non câblé » reste systémique côté éditeur** : 12 sous-systèmes
   testés mais jamais instanciés (tutoriel/diagnostic/wizard/validation/tools). Le stock ne
   se résorbe pas. Côté client il a régressé positivement (combat câblé par SP1-4).
3. **Intégrité éditeur** : B3 a réglé l'inter-cartes, mais restent ⏳ le SplatPaint inutilisable,
   la double vérité heightmap (legacy non-undoable), et le 🆕 save-écrase-après-échec-de-load.
4. **Sécurité portail** : fondamentaux sains, périmètre transverse toujours à faire
   (rate-limit login, en-têtes, CSRF, GET mutants).
5. **Secrets de prod en dur** (ticket HMAC + mot de passe DB) : risque concret si déployé tel quel.
6. **Ménage rentable** : ~50 fichiers orphelins, 4 🔴 transversaux (migrations Docker périmées,
   templates divergents, mismatch config server_id, shader .spv mort), 14 clés config mortes.

## 7. Propositions de lots (à discuter)

- **Lot B7 — race Trade** (serveur, 🆕) : `std::mutex` sur `TradeSessionRegistry`, sur le modèle exact de B1. **Petit, critique, à faire en priorité.** → **redéploiement master requis.**
- **Lot F — sécurité portail** : rate-limit login + sends, en-têtes de sécurité, dummy-verify anti-timing, GET→POST + tokens hachés. → redéploiement portail.
- **Lot G — secrets & déploiement** : externaliser `ticket_hmac_secret`+password, refus de boot si valeur dev ; gitignorer `deploy/docker/sql/` ; harmoniser le délai des templates password-reset. → redéploiement master.
- **Lot A' — ménage sans risque** : `server_bootstrap/main.cpp`, `EditorAudioPanel`, `game/data/themes/`, `luminance_reduce.comp.spv`, `.spv` `git rm --cached`, `exploitTier.ts`, CSS/templates/senders morts, `Migrate-CellNaming.ps1`, clés config mortes + **fix mismatch `default_server_id`**.
- **Lot D' — optimisations** : flush splat/grass au mouse-up (éditeur, plus gros gain perçu) ; buffers de pose réutilisables + mapping persistant (client) ; N+1 `/online` + ré-encodage chat (serveur) ; CREATE TABLE hors chemin requête + pool SMTP (portail).
- **Lot C' — câbler ou supprimer (décision par feature)** : 12 sous-systèmes éditeur, doublon AuctionUi client, AoEPreviewSystem, FXManager, ClientPrediction, ZonePreloadHook.
- **Lot H — terminologie** : nettoyage transverse des références `CMANGOS` (code/commentaires).
- **Lot E — process** : « pas de merge d'un sous-système sans sa passe de câblage » + registre central des binds clavier in-game.

---

*Source : 5 audits parallèles indépendants (client, serveur, éditeur, portail, transversal),
2026-06-13. Aucune modification de code. À lire en complément de
`2026-06-10-audit-global-4-parties.md` (les deux convergent ; ce rapport ajoute la race
Trade, le save-après-échec éditeur, et la confirmation post-B1/B2/B3).*

> **Déploiement** : ✅ Audit en lecture seule — aucun redéploiement. Les lots de
> correction, eux, impacteront : master (B7, G), portail (F), client/éditeur (A'/C'/D').
