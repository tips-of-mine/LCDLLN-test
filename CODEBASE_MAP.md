# CODEBASE MAP — Lune Noire (LCDLLN-test)

> Référence rapide à inclure dans un prompt pour éviter la ré-analyse complète.
> Dernière mise à jour : 2026-04-25 — auth & navigation web-portal (sous-projet A).

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
| `db/migrations/000N_*.sql` | Migrations numérotées (0001 → 0019). Appliquées en ordre par MigrationRunner. |
| `engine/server/MigrationRunner.h/.cpp` | Applique les migrations au démarrage du serveur. |
| `engine/server/db/ConnectionPool.h/.cpp` | Pool de connexions MySQL réutilisables. |
| `engine/server/db/DbHelpers.h/.cpp` | Helpers requêtes SQL (bind params, lecture résultats). |

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
| `web-portal/app/bugs/page.tsx` | Signalement bugs : étapes `wp-grid-3`, `wp-tiers` (paliers 5–100). |
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
| `web-portal/app/player/privacy/page.tsx` | Vie privée : CGU (accepter), visibilité du profil. |
| `web-portal/app/player/cgu/page.tsx` | CGU joueur (legacy) : `wp-table` acceptations. |
| `web-portal/app/player/exploits/page.tsx` | Exploits : délègue à `ExploitsProfile`. |
| `web-portal/app/player/recovery-profile/page.tsx` | Profil récupération : `wp-alert warning` si pas de compte. |
| `web-portal/app/password-recovery/page.tsx` | Récupération mot de passe : `wp-card` info. |
| `web-portal/components/ExploitsProfile.tsx` | Exploits : progress bar, cartes visibles/masquées, stats. |
| `web-portal/components/AccountForm.tsx` | Formulaire compte joueur (Client Component) — infos perso, email, adresse. |
| `web-portal/components/CharacterDeleteButton.tsx` | Suppression personnage en 2 confirmations (Client Component). |
| `web-portal/components/PasswordChangeForm.tsx` | Changement mot de passe (Client Component). |
| `web-portal/components/PrivacyForm.tsx` | Visibilité profil radio buttons (Client Component). |
| `web-portal/components/CguAcceptButton.tsx` | Bouton acceptation CGU (Client Component). |
| `web-portal/components/admin/PlayerActions.tsx` | Actions joueur admin : email, statut, désactivation motif, personnages. |
| `web-portal/components/admin/CguManager.tsx` | Gestion CGU admin : create/edit/publish/retire (Client Component). |
| `web-portal/components/admin/FaqAdmin.tsx` | CRUD FAQ admin (Client Component). |
| `web-portal/components/admin/BugAdmin.tsx` | Gestion bugs admin : statut, commentaire, exploit award (Client Component). |
| `web-portal/middleware.ts` | Protection routes `/player/*` et `/admin/*` via cookies (Edge Runtime). |
| `web-portal/lib/session.ts` | `getSession()` — lit cookie `lcdlln_portal_account`, retourne `Session \| null`. |
| `web-portal/lib/email.ts` | Module email centralisé — 7 fonctions d'envoi, templates HTML Lune Noire. Lit la config SMTP depuis `smtp.local.json` (racine dépôt) en fallback si les variables d'environnement `SMTP_HOST` etc. sont absentes. |
| `web-portal/lib/db.ts` | Pool MySQL partagé, `query<T>()`. |
| `web-portal/lib/portalLogin.ts` | `verifyPortalCredentials()` — double Argon2id + legacy scrypt. |
| `web-portal/lib/gamePasswordHash.ts` | Hash/verify double Argon2id (`@node-rs/argon2`). |
| `web-portal/app/api/auth/login/route.ts` | POST login — set cookies `lcdlln_portal_account` + `lcdlln_portal_role`. |
| `web-portal/app/api/auth/logout/route.ts` | POST logout — supprime les deux cookies session. |
| `web-portal/app/api/player/` | APIs joueur : account PATCH, email change, password, parental, cgu accept, privacy, characters delete. |
| `web-portal/app/api/admin/` | APIs admin : players (verify-email, activate, disable), characters (force-rename), roadmap CRUD, faq CRUD, cgu CRUD+publish+retire, bugs PATCH. |
| `design/lune-noire-design-system/ui_kits/email/` | 7 templates HTML email (welcome, verification, password-reset, account-confirmed, account-disabled, parental-validation, email-change). |

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
Créer `smtp.local.json` **à la racine du dépôt** (même niveau que `web-portal/`).
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

## Aide-mémoire : comment trouver un écran

1. **Je veux changer le visuel d'un écran** → `engine/render/auth/screens/AuthImGuiXxx.cpp`
2. **Je veux changer la logique / les données affichées** → `engine/client/auth/screens/AuthScreenXxx.cpp`
3. **Je veux changer un style global (couleur, police, bouton)** → `engine/render/auth/AuthImGuiCommon.h/.cpp`
4. **Je veux changer un message / traduction** → `game/data/localization/fr/fr.json`
5. **Je veux changer la logique réseau d'un écran** → `StartXxxWorker()` dans le fichier presenter + payload dans `engine/network/AuthRegisterPayloads.h`
6. **Je veux changer ce que le serveur fait à la réception** → `engine/server/XxxHandler.cpp`
