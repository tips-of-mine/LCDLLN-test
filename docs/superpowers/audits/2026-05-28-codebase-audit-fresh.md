# Audit codebase LCDLLN — 2026-05-28 (fresh)

> Deuxième passage transverse des 4 sous-systèmes (client, serveur, éditeur monde, web portal),
> exécuté indépendamment de l'audit du 2026-05-27 (cf. [audits/closed/2026-05-27-codebase-audit.md](closed/2026-05-27-codebase-audit.md))
> pour identifier régressions, nouvelles anomalies, code orphelin et opportunités d'optimisation.
>
> Méthode : 4 agents `Explore` en parallèle, un par sous-système. Briefs incluant la
> liste des findings 2026-05-27 à vérifier comme "toujours résolus" (toute redécouverte = régression).

---

## TL;DR

| Sous-système | Verdict | Critique |
|---|---|---|
| **Client** (`src/client/`, parts shared client-only) | ✅ Sain, stable | 3 TODOs mineurs (vs "aucun" 2026-05-27), pas de régression fonctionnelle |
| **Serveur** (`src/masterd/`, `src/shardd/`, shared server, SQL) | 🟡 Dette SQL non amortie | **F2 jamais shippé** (prepared statements absents), F3 partiellement résolu |
| **Éditeur monde** (`src/world_editor/`, `tools/`, `game/data/editor/`) | 🟡 4 anomalies actives | Validator par frame, écriture LCMI non atomique, validation `Place()` laxe |
| **Web portal** (`web-portal/`) | 🔴 Régression logging | **31 catch silencieux sur 32 routes API** — un seul site utilise `logError()` |

**Constat 1** : F2 (« prepared statements + migration 0070 ») de l'audit 2026-05-27 n'apparaît dans aucune PR mergée. La PR #722 a uniquement ajouté `ix_characters_server_id` (un seul index simple).

**Constat 2** : la PR #720 a migré 9 sites `console.error/warn` vers `lib/log.ts`, mais les **catch blocs préexistants des routes API n'ont jamais été audités**. Sur 32 routes API, **31 ont un `catch { return NextResponse.json(...) }` sans aucun appel à `logError`**.

---

## 1. Périmètre et méthode

### Périmètre
- **Client** : `src/client/` + parties de `src/shared/` (math, network protocol côté client, core/Config, ServerEndpoints, security).
- **Serveur** : `src/masterd/`, `src/shardd/`, `src/shared/server_bootstrap/`, parties serveur de `src/shared/network/`, `src/shared/security/`, `sql/migrations/`, `deploy/docker/` (parts serveur).
- **Éditeur monde** : `src/world_editor/`, `src/client/render/terrain/TerrainEditingTools.*`, `tools/zone_builder/`, `tools/asset_pipeline/`, `game/data/editor/`, parties éditeur de `src/client/app/Engine.cpp`.
- **Web portal** : `web-portal/` (Next.js 14 App Router), `deploy/docker/.env.example`.

### Méthode
4 agents `Explore` lancés en parallèle, briefs distincts avec :
- 3 axes obligatoires : orphelin / anomalies / optimisations
- Citation `file:line` + niveau de confiance (haut / moyen / hypothèse)
- Liste des findings 2026-05-27 à vérifier (régression = signal critique)
- Pas de proposition de solution (audit pur)

---

## 2. Client / jeu

### Orphelin
- **C-O-1** [moyen] [`src/client/auth/AuthUiPresenterCore.cpp:160`](../../../src/client/auth/AuthUiPresenterCore.cpp) — commentaire « si le repop » suggère logique TODO non finalisée

### Anomalies
- **C-A-1** [moyen] [`src/client/render/FrameGraph.cpp:653`](../../../src/client/render/FrameGraph.cpp) — TODO STAB.7 sur restauration VMA (intentionnel mais long-running)
- **C-A-2** [moyen] [`src/client/app/Engine.cpp:5268`](../../../src/client/app/Engine.cpp) — TODO source de temps wall-clock perte précision (Profiler)

### Optimisations
- **C-P-1** [hypothèse] `src/client/render/terrain_chunk/SplatMapGpuCache.cpp` — `std::map` détecté côté descripteurs GPU, à vérifier si lookup hot path

### Vérification findings 2026-05-27
| Finding | État |
|---|---|
| F9 (Engine.cpp 10.6K LOC) | ✅ Stable à 10782 LOC |
| Aucun .cpp substantiel orphelin | ✅ OK |
| Convention winding terrain CCW | ✅ OK ([`TerrainRenderer.cpp:528-542`](../../../src/client/render/terrain/TerrainRenderer.cpp)) |
| VMA désactivé intentionnellement | ✅ OK ([`Engine.cpp:62`](../../../src/client/app/Engine.cpp)) |
| AuthUiPresenterCore 4.5K LOC | ✅ Accepté |
| "Aucun TODO/FIXME" | 🟡 3 TODOs trouvés (drift mineur) |

**Verdict client** : sain, pas de régression critique. Hors scope PR isolée.

---

## 3. Serveur (master + shard)

### Orphelin
- Aucun. Tous les opcodes dispatchés (vérifié `main_linux.cpp` lambda + ShardTicketHandler explicite ligne 1031), tous les payloads `Build*/Parse*` utilisés, migrations 0001-0069 toutes idempotentes et lues.

### Anomalies
- **S-A-1** [haut] [`src/masterd/handlers/character/CharacterCreateHandler.cpp:44`](../../../src/masterd/handlers/character/CharacterCreateHandler.cpp) — SQL injection mitigée par `mysql_real_escape_string` mais **zéro prepared statement** sur les 5 hot handlers. **F2 non résolu.**
- **S-A-2** [moyen] [`src/masterd/handlers/admin/AdminCommandHandler.cpp`](../../../src/masterd/handlers/admin/AdminCommandHandler.cpp) lignes 196, 235, 309, 702, 999, 1007, 1033 — **8 catch(...) silencieux** sans log. Traçabilité nulle si parse d'argument admin échoue.
- **S-A-3** [moyen] [`src/masterd/handlers/character/CharacterCreateHandler.cpp:67`](../../../src/masterd/handlers/character/CharacterCreateHandler.cpp) — WHERE composite `server_id AND account_id AND deleted_at IS NULL` sans index compound. La migration 0069 ajoute juste `server_id` simple.

### Optimisations
- **S-P-1** [moyen] — Aucun compound index sur les paires `(server_id, account_id)` utilisées massivement par `CharacterListHandler`, `CharacterEnterWorldHandler`. → migration 0070 toujours pertinente.

### Vérification findings 2026-05-27
| Finding | État |
|---|---|
| F2 (prepared statements) | ⚠️ **RÉOUVERT / JAMAIS SHIPPÉ** — confirmé par lecture des 5 handlers prioritaires + absence de migration 0070 |
| F3 (index FK manquants) | 🟡 **PARTIEL** — `ix_characters_server_id` ajouté (PR #722), compound `(server_id, account_id)` toujours manquant |
| TG.2 (FindClient O(1)) | ✅ OK (unordered_map confirmé `ConnectionSessionMap:47` + SessionManager:138-139) |
| TG.3 (UDP atomic counters) | ✅ OK ([`UdpTransport.h:79`](../../../src/shardd/world/UdpTransport.h)) |

**Verdict serveur** : architecturalement solide. Dette F2 + F3 toujours à amortir.

---

## 4. Éditeur monde

### Orphelin
- **E-O-1** [haut] [`tools/asset_pipeline/`](../../../tools/asset_pipeline/) — `convert_race_meshes.py` + 4 autres scripts Python jamais appelés ; inbox 81+ fichiers FBX jamais consommée
- **E-O-2** [haut] [`game/data/editor/zone_presets/thumbnails/`](../../../game/data/editor/zone_presets/) — répertoire vide, code lit `.thumbnailPath` ([`ZonePresetIo.cpp:182`](../../../src/world_editor/zone_presets/ZonePresetIo.cpp)) mais aucun PNG présent
- **E-O-3** [moyen] [`tools/zone_builder/lib/`](../../../tools/zone_builder/lib/) — GltfImporter, LayoutImporter, ChunkPackageWriter exportés mais semblent non utilisés par le binaire

### Anomalies
- **E-A-1** [haut] [`src/world_editor/volumes/caves/CaveTool.cpp:73`](../../../src/world_editor/volumes/caves/CaveTool.cpp) — `Place()` accepte `uniformScale` sans validation (scale ≤ 0 → gltf invalide), pas de `std::isnan/isinf` sur Vec3
- **E-A-2** [haut] [`src/world_editor/volumes/MeshInsertDocument.cpp:88`](../../../src/world_editor/volumes/MeshInsertDocument.cpp) — **écriture LCMI sans `.tmp` + rename atomique** → crash mid-save = fichier corrompu
- **E-A-3** [haut] [`src/world_editor/zone_presets/OperationDispatcher.cpp:879-886`](../../../src/world_editor/zone_presets/OperationDispatcher.cpp) — `sculpt_brush` + `splat_paint` `Unsupported` (intentionnel M100.46, confirmé)
- **E-A-4** [haut] [`src/world_editor/panels/ToolPropertiesPanel.cpp:1291-1296`](../../../src/world_editor/panels/ToolPropertiesPanel.cpp) — **Phase11Validator créé + exécuté à chaque frame** quand DungeonPortal actif → O(N²) sur 1000+ meshes, lag 60fps

### Optimisations
- **E-P-1** [haut] [`src/world_editor/panels/ToolPropertiesPanel.cpp`](../../../src/world_editor/panels/ToolPropertiesPanel.cpp) lignes 586, 613, 616 — `std::string` allouées dans hot loop Render*Params
- **E-P-2** [moyen] [`src/world_editor/terrain/TexturePreviewCache.h:134`](../../../src/world_editor/terrain/TexturePreviewCache.h) — `m_pool` commenté TODO inutilisé
- **E-P-3** [moyen] [`src/world_editor/zone_presets/OperationDispatcher.cpp:437`](../../../src/world_editor/zone_presets/OperationDispatcher.cpp) — TODO « lookup TerrainDocument quand… » → O(N) linéaire

### Vérification findings 2026-05-27
| Finding | État |
|---|---|
| 15 outils dispatchés | ✅ Confirmé |
| 2 ops Unsupported (`sculpt_brush`, `splat_paint`) | ✅ Confirmé |
| 4 catalogs chargés / pas en dropdown | ✅ Confirmé |
| Boilerplate 62 accesseurs | ✅ Accepté |
| Doxygen Phase 11 | ✅ Confirmé |

**Verdict éditeur** : 4 anomalies actives identifiées. **Hors scope du plan d'attaque 2026-05-28** (décision utilisateur — chantier éditeur dédié plus tard).

---

## 5. Web portal

### Orphelin
- **W-O-1** [haut] [`web-portal/lib/email/sender.ts:31-43`](../../../web-portal/lib/email/sender.ts) — `getTransporter()` jamais appelé ; les routes email réimplémentent leur propre `nodemailer.createTransport()` → code orphelin + duplication
- **W-O-2** [moyen] [`web-portal/app/player/parental/page.tsx`](../../../web-portal/app/player/parental/page.tsx) — aucun `<Link href="/player/parental">` trouvé
- **W-O-3** [moyen] [`web-portal/app/contact/page.tsx`](../../../web-portal/app/contact/page.tsx) — référencée que par footer statique
- **W-O-4** [haut] Aucun `*.test.ts` / jest / vitest — confirmé absent (cohérent avec 2026-05-27)

### Anomalies
- **W-A-1** [haut] [`web-portal/app/api/auth/login/route.ts:40,58`](../../../web-portal/app/api/auth/login/route.ts) — catch silencieux sans log
- **W-A-2** [haut] **31 routes API sur 32 ont `catch { return NextResponse.json(...) }` sans `logError`** → seule [`app/api/admin/cgu/route.ts:65`](../../../web-portal/app/api/admin/cgu/route.ts) log. Audit exhaustif des 32 routes requis.
- **W-A-3** [moyen] [`web-portal/lib/auth/passwordRecovery.ts:435`](../../../web-portal/lib/auth/passwordRecovery.ts) — fallback `Number.parseInt(process.env.SMTP_PORT || "587", 10)` (acceptable RFC 6409 mais asymétrique avec `requireEnv("AUTH_SECRET")`)
- **W-A-4** [moyen] [`web-portal/lib/email/sender.ts:33`](../../../web-portal/lib/email/sender.ts) — même fallback SMTP_PORT codé différemment (duplication)
- **W-A-5** [moyen] 9 routes API publiques (`/api/health`, `/api/bugs`, `/api/password-recovery/*`, `/api/player/parental/*`, `/api/contact`) — `/api/bugs` POST anonyme à clarifier
- **W-A-6** [haut] [`web-portal/lib/email/sender.ts:18-27`](../../../web-portal/lib/email/sender.ts) — `JSON.parse()` en try-catch silencieux (`smtp.local.json` cassé = aucun log)

### Optimisations
- **W-P-1** [moyen] [`web-portal/app/admin/page.tsx:13-34`](../../../web-portal/app/admin/page.tsx) — cache 60s OK (PR #720), mais **aucun `revalidateTag("admin-stats")` sur les mutations admin** → stats peuvent être fausses jusqu'à 60s
- **W-P-2** [bas] pool MySQL OK, pas de timeout/retry
- **W-P-3** [moyen] 25 pages serveur sans `Suspense` boundary → rendu bloquant DB

### Vérification findings 2026-05-27
| Finding | État |
|---|---|
| F1 (secret HMAC fallback) | ✅ **Confirmé résolu** ([`passwordRecovery.ts:62-64`](../../../web-portal/lib/auth/passwordRecovery.ts) utilise `requireEnv("AUTH_SECRET")`) |
| F10a (cache stats admin) | ✅ **Confirmé résolu** (PR #720 `unstable_cache`) |
| F10b (console.error en prod) | ⚠️ **EXTENSION** — 9 sites `console.error` existants migrés, mais 31 catch préexistants restent silencieux |
| Infra de test | 🟡 Confirmé absent |

**Verdict web-portal** : F1 et F10a vraiment résolus. F10b a un goût d'inachevé.

---

## 6. Findings cross-cutting

| # | Finding | Sous-systèmes | Sévérité | Origine | Décision |
|---|---|---|---|---|---|
| **N1** | F2 jamais shippé (prepared statements absents sur 5 handlers) | Serveur | 🔴 Haute | Reconduction 2026-05-27 | **GO — PR 5-6 (POC + bloc)** |
| **N2** | 31/32 routes API web sans `logError` dans catch | Web portal | 🔴 Haute | **Nouveau** — extension F10b | **GO — PR 3** |
| **N3** | F3 partiel — compound index `(server_id, account_id)` manquant | Serveur | 🟡 Moyenne | Reconduction partielle | **GO — PR 4** |
| **N4** | MeshInsertDocument écrit LCMI sans atomic temp+rename | Éditeur | 🟡 Moyenne | **Nouveau** | **Reporté** (chantier éditeur dédié) |
| **N5** | Phase11Validator exécuté à chaque frame UI | Éditeur | 🟡 Moyenne | **Nouveau** | **Reporté** |
| **N6** | 8 catch(...) silencieux AdminCommandHandler | Serveur | 🟡 Moyenne | **Nouveau** | **GO — PR 2** |
| **N7** | `lib/email/sender.ts:getTransporter()` orphelin + duplication routes email | Web portal | 🟡 Moyenne | **Nouveau** | **GO — PR 1** |
| **N8** | CaveTool::Place() sans validation scale/NaN | Éditeur | 🟡 Moyenne | **Nouveau** | **Reporté** |
| **N9** | `asset_pipeline/` inbox + scripts Python orphelins | Éditeur | 🟢 Basse | **Nouveau** | **Reporté** |
| **N10** | zone_presets/thumbnails/ vide alors que JSON les référence | Éditeur | 🟢 Basse | **Nouveau** | **Reporté** |

---

## 7. État des follow-ups FU-1 à FU-7 (audit 2026-05-27)

Seul **FU-5** a été shippé (PR #723). Les 6 autres (FU-1, FU-2, FU-3, FU-4, FU-6, FU-7) ne montrent aucune trace de PR mergée. À planifier dans une session avec build C++ local accessible.

---

## 8. Plan d'attaque — 6 PRs validées

> Décision utilisateur : éditeur (N4/N5/N8/N9/N10) ignoré pour ce cycle. 5 chantiers retenus.
> Ordre de merge : **du moins risqué au plus risqué**.

### PR 1 — N7 (web-only) — sender.ts orphelin
- Soit supprimer `getTransporter()` orphelin, soit refactor les 3 routes email pour l'utiliser
- Décision à trancher après lecture du code
- **Déploiement** : ✅ web portal seul, pas de redéploiement serveur

### PR 2 — N6 (master) — 8 catch admin → log
- Ajouter `LogWarn`/`LogError` sur les 8 catch(...) silencieux dans `AdminCommandHandler.cpp`
- **Déploiement** : ⚠️ master + lock-step (binaire serveur recompilé)

### PR 3 — N2 (web) — 31 catch web → logError
- Passe transverse `logError()` sur les 31 routes API web-portal
- Audit exhaustif des 32 routes pour confirmer le compte
- **Déploiement** : ✅ web portal seul

### PR 4 — N3 (master + DB) — migration 0070 compound indexes
- Migration idempotente `0070_add_compound_indexes.sql` (notamment `characters (server_id, account_id)`)
- Audit transverse des autres compound queries possibles
- **Déploiement** : ⚠️ master + migration DB, lock-step

### PR 5 — N1-A (master) — DbPreparedQuery POC + CharacterCreateHandler
- Helper `DbPreparedQuery` dans `src/shared/db/` (wrapper autour de `mysql_stmt_*`)
- Conversion `CharacterCreateHandler` en prepared statements (POC)
- Tests unitaires
- **Déploiement** : ⚠️ master + lock-step

### PR 6 — N1-B (master) — 4 handlers restants
- Conversion des 4 handlers restants (ChatRelayHandler, AuthHandler, CharacterListHandler, CharacterEnterWorldHandler) après merge N1-A
- **Déploiement** : ⚠️ master + lock-step

### Ordre de merge
PR 0 (housekeeping) → PR 1 → PR 2 → PR 3 → PR 4 → PR 5 → PR 6.
Aucune stack — toutes mergeables indépendamment, sauf PR 6 qui dépend de PR 5.

### Hors scope confirmé
- **Éditeur (N4, N5, N8, N9, N10)** : laissé tel quel (décision utilisateur — chantier éditeur dédié plus tard).
- **Client (C-A-1, C-A-2, C-P-1)** : TODOs intentionnels, hypothèse à vérifier.
- **W-A-3, W-A-4** : fallback SMTP_PORT 587 acceptable RFC 6409.
- **FU-1 à FU-4, FU-6, FU-7** : nécessitent un build C++ local — à programmer hors session.

---

## 9. Annexes

### Archive de l'audit précédent
Rapports clos déplacés dans [`audits/closed/`](closed/) :
- [`closed/2026-05-27-codebase-audit.md`](closed/2026-05-27-codebase-audit.md)
- [`closed/2026-05-27-codebase-audit-followups.md`](closed/2026-05-27-codebase-audit-followups.md)

Convention adoptée : un rapport d'audit est déplacé dans `closed/` quand l'utilisateur confirme que la phase de mise en œuvre est close. Les liens relatifs depuis `docs/superpowers/plans/` sont ajustés (`../audits/X.md` → `../audits/closed/X.md`).

### Vérifications directes effectuées après synthèse agent
- [`web-portal/lib/auth/passwordRecovery.ts:62-64`](../../../web-portal/lib/auth/passwordRecovery.ts) — `getRecoverySecret()` utilise bien `requireEnv("AUTH_SECRET")` → **F1 résolu confirmé**.
- [`web-portal/app/admin/page.tsx:13-34`](../../../web-portal/app/admin/page.tsx) — `unstable_cache` 60s sur stats admin → **F10a résolu confirmé**.
- [`src/masterd/handlers/character/CharacterCreateHandler.cpp:44-47`](../../../src/masterd/handlers/character/CharacterCreateHandler.cpp) — concaténation SQL + `mysql_real_escape_string`, **aucun `mysql_stmt_*`** → **F2 non résolu confirmé**.
- [`sql/migrations/0069_add_characters_server_id_index.sql`](../../../sql/migrations/0069_add_characters_server_id_index.sql) — seul index ajouté est `ix_characters_server_id` (simple) → **F3 partiel confirmé**.
- [`src/shardd/world/UdpTransport.h:79`](../../../src/shardd/world/UdpTransport.h) — `std::atomic<uint64_t>` confirmé → **TG.3 toujours en place**.

### Métrique du plan d'attaque
- **6 PRs** au total (1 housekeeping + 5 chantiers), dont **2 web-only** (N7, N2) et **4 master/serveur** (N6, N3, N1-A, N1-B)
- **4 lock-step master** requis (N6, N3, N1-A, N1-B) — à coordonner avec les déploiements opérationnels
- **1 lock-step DB** (N3 — migration 0070)
- **Aucun wire-breaking** (pas de bump `kProtocolVersion`)
- **Effort estimé total** : ~1.5-2 jours-homme (N1-A + N1-B = la moitié)
