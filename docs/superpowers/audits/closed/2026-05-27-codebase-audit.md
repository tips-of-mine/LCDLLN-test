# Audit codebase LCDLLN — 2026-05-27

> Audit transverse des 4 sous-systèmes (client, serveur, éditeur monde, web portal)
> + revue du code orphelin / legacy / dette docs.
> Branche : `claude/tg3-scope-doc`. Méthode : 5 agents `Explore` en parallèle + vérifications ciblées.

---

## TL;DR

| Sous-système | Verdict | Note |
|---|---|---|
| Client / jeu (`src/client/`, `src/shared/`) | Sain | Aucun code mort majeur ; conventions repo respectées ; Engine.cpp couplé mais pas urgent. |
| Serveur (`src/masterd/`, `src/shardd/`) | Architecturalement sain, hygiène SQL fragile | Sessions/RBAC corrects ; aucun opcode orphelin. Mais : zéro prepared statement, ~50% d'index FK manquants en migration 0001. |
| Éditeur monde (`src/world_editor/`) | Sain | 15 outils tous câblés et dispatchés. 2 ops zone presets sur 14 retournent `Unsupported`. Boilerplate répétitif sur les 15 outils. |
| Web portal (`web-portal/`) | Sain, **1 bug sécu confirmé** | Architecture Next.js 14 propre. Fallback secret HMAC en dur si `AUTH_SECRET` absent. |
| Transverse / legacy | Dette docs | `legacy/`, `CMANGOS_ANALYSIS.md`, `BUILD_CHECK.md`, `INVESTIGATION_terrain_invisible.md` candidats nettoyage. **Décision : laissé tel quel (utile comme repère de suivi).** |

**Risque critique unique** : F1 (fallback secret en dur, web-portal). Tout le reste est de la dette à amortir.

---

## 1. Périmètre et méthode

### Périmètre
- **Client / jeu** : `src/client/` + parties de `src/shared/` (math, network protocol côté client).
- **Serveur** : `src/masterd/` (master TCP Linux), `src/shardd/` (shard UDP gameplay), `sql/migrations/`, `src/shared/server_bootstrap/`, handlers `src/shared/network/`.
- **Éditeur monde** : `src/world_editor/`, `src/client/render/terrain/TerrainEditingTools.*`, `tools/zone_builder/`, `game/data/editor/`.
- **Web portal** : `web-portal/` (Next.js 14 App Router).
- **Transverse** : `legacy/`, `tickets/`, `docs/`, `scripts/`, `tools/`, fichiers racine.

### Méthode
5 agents `Explore` lancés en parallèle, un par périmètre. Chaque agent capé à ~500-600 mots, avec consignes strictes (chemins précis, niveau de confiance, pas de proposition de solution). 3 findings critiques vérifiés directement par lecture du code après synthèse — 1 finding éditeur (Apply jamais appelé) infirmé, 1 finding serveur (SQL injection) requalifié en "absence prepared statements", 1 finding web-portal (secret en dur) confirmé.

---

## 2. Client / jeu

**Volume** : ~228 fichiers `.cpp`, ~404 avec headers, ~84K LOC hors tests. Organisé en ~15 sous-domaines (`app/`, `render/`, `auth/`, `ui/`, `gameplay/`, `world/`, `combat/`, `crafting/`, `inventory/`, `social/`, `quest/`, `mail/`, `auction/`, `lfg/`, `arena/`).

### Constats
- **Engine.cpp ≈ 10.6K LOC** + **Engine.h** avec 90 includes directs de sous-systèmes UI/render. Pattern hub-and-spoke. Pas illégal mais fragilise toute évolution touchant l'init/teardown.
- **15 `*ImGuiRenderer.cpp`** sans hiérarchie commune (ChatImGuiRenderer, MailImGuiRenderer, etc.) — boilerplate ImGui répété mais pas de duplication 1:1.
- **VMA désactivé intentionnellement** ([`Engine.cpp:62`](../../../src/client/app/Engine.cpp)) — choix tracé par STAB.7.
- **Convention winding terrain respectée** ([`TerrainRenderer.cpp`](../../../src/client/render/terrain/TerrainRenderer.cpp)) — garde anti-régression CLAUDE.md tient.
- **AuthUiPresenterCore.cpp ≈ 4.5K LOC** avec zéro commentaire interne — maintenable mais long à reprendre.
- **Aucun TODO/FIXME accumulé, aucun `#if 0`**.

### Code orphelin
**Aucun fichier `.cpp` substantiel non-référencé**. L'écart de ~10 fichiers entre filesystem et CMakeLists s'explique par les tests dans `src/client/*/tests/` exclus de la cible binaire (comportement attendu).

### Verdict
Sain. Seul vrai sujet de fond : couplage Engine.cpp — **hors scope** (refactor risqué, pas de douleur opérationnelle).

---

## 3. Serveur (master + shard)

**Architecture** : master TCP single-thread + shard UDP thread tick. 39 handlers dans `src/masterd/handlers/`. Sessions via `SessionManager` + `ConnectionSessionMap` + `SessionCharacterMap`. RBAC pour commandes admin via `AdminCommandHandler`.

### Constats

| Aspect | État | Détail |
|---|---|---|
| Hygiène SQL | 🟡 Fragile | Toutes les queries en concaténation `"... WHERE col = '" + escapedX + "'"`. `mysql_real_escape_string` utilisé partout, valeurs numériques typées (`uint64_t` → `std::to_string`) → **pas d'injection exploitable actuellement**, mais zéro prepared statement, pattern fragile à l'évolution. |
| Index DB | 🟡 Lacunaire | Migration 0001 crée 5 index sur 9 tables. `characters.server_id` et plusieurs FK sans index. Colonnes ajoutées plus tard (`spawn_x/y/z` migration 0032, etc.) sans index pour range queries. |
| N+1 queries | 🟡 Potentielles | 2-3 cas suspects : [`ChatRelayHandler.cpp:210-220`](../../../src/masterd/handlers/chat/ChatRelayHandler.cpp) (SELECT `guild_id` puis SELECT `player_id` en 2 queries), [`CharacterEnterWorldHandler`](../../../src/masterd/handlers/character/CharacterEnterWorldHandler.cpp) (character lookup + role lookup) — joinables. |
| Threading / races | ✅ OK | Master single-thread strict ; locks brefs là où nécessaire. Shard : compteurs UDP désormais `std::atomic` (TG.3). |
| Opcodes / handlers | ✅ OK | 195 paires request/response, toutes câblées. Opcodes 197/198 (EnterDungeon) câblés depuis M100.44. |
| RBAC admin | ✅ OK | `AdminCommandHandler` valide `minRole > userRole` avant dispatch. |
| Migrations | ✅ OK | 68 fichiers `sql/migrations/`. Toutes colonnes créées sont lues. Pas de migration orpheline. Le runner ([`MigrationRunner.cpp`](../../../src/masterd/migrations/MigrationRunner.cpp)) parse strict `NNNN_name.sql` + checksum SHA-256 par fichier en DB. **Renommer ou déplacer un fichier `.sql` existant fait planter le master au boot.** |
| TG.3 split-receive | ✅ Implémenté | Opt-in (cf. `claude/tg3-scope-doc`). Reste : split gameplay multi-thread, multi-shard. Ticketé. |

### Code orphelin
Aucun handler/opcode/migration orpheline détectée côté serveur. 129 opcodes "réponse/notification" sont par construction côté client (pas de handler serveur — comportement attendu).

### Verdict
Architecturalement solide. Dette principale : **conversion progressive en prepared statements** + **revue d'index**. Pas urgent (pas d'injection actuelle), mais à acter avant que de nouveaux handlers s'empilent.

---

## 4. Éditeur monde

**Volume** : ~256 fichiers `src/world_editor/` + `TerrainEditingTools.*` (côté `src/client/render/terrain/`) + `tools/zone_builder/`.

**Outils** : 15 outils dans `enum ActiveTool` ([`WorldEditorShell.h:40-58`](../../../src/world_editor/core/WorldEditorShell.h)) — Sculpt, Stamp, SplatPaint, Lake, River, MountainRange, ValleyChain, RiverNetwork, Coastline, HydraulicErosion, ThermalWindErosion, Cave, Overhang, Arch, DungeonPortal.

### Constats vérifiés (correction d'un faux finding)

L'agent d'exploration avait initialement classé Coastline / ThermalWind / Cave / Overhang / Arch / DungeonPortal comme "Apply() jamais appelé". **Faux**. Vérification ([`ToolPropertiesPanel.cpp`](../../../src/world_editor/panels/ToolPropertiesPanel.cpp)) :
- Les 15 outils ont leur bloc `Render*Params` (lignes 777, 813, 905, 917, 1012, 1101, 1193, 1352, etc.) et leur dispatch (lignes 2130-2172).
- Les 4 outils volumes (Cave/Overhang/Arch/DungeonPortal) utilisent `Place()` qui pousse une commande, là où les autres utilisent `Apply()`. Pattern intentionnel selon la nature du tool.

### Constats réels

- **Zone presets M100.46 incrément 1** : 14 types d'opération, **12 câblées** dans [`OperationDispatcher.cpp`](../../../src/world_editor/zone_presets/OperationDispatcher.cpp). 2 retournent `DispatchResult::Unsupported` : `sculpt_brush` et `splat_paint`. Les 8 presets livrés ne les utilisent pas.
- **Catalogues volumes chargés au boot mais pas exposés en dropdown UI** : CaveCatalog, OverhangCatalog, ArchCatalog, DungeonCatalog. Sélection par texte/id manuel. État MVP, paper cut UX sur 4 outils.
- **Boilerplate répété sur 15 outils** : chacun a `Init(commandStack, document, cfg)` → membres `m_stack / m_doc / m_cfg` → `Place()` / `Apply()` qui push une commande. 62 accesseurs `Mutable*/Get*` au total dans `WorldEditorShell`.
- **Doc Doxygen `///`** : règle CLAUDE.md respectée sur Phase 11 (M100.40-44). Pas de violation détectée.
- **Hot-reload `Ctrl+Shift+R`** des presets compile en mode release (cosmétique).

### Verdict
Très propre pour un éditeur de cette taille. Sujets éditeur : (a) finir les 2 ops zone presets, (b) factoriser le boilerplate des 15 outils. **Reportés en chantier dédié** (hors scope du présent plan).

---

## 5. Web portal

**Stack** : Next.js 14.2.18 App Router, MySQL2 3.11.0 (pas de Prisma), Nodemailer 6.10.1, Argon2id double-hash. 32 routes API, auth par cookies + middleware. TypeScript strict.

### Findings

#### 🔴 F1 — Secret HMAC fallback en dur (confirmé)

[`web-portal/lib/auth/passwordRecovery.ts:61`](../../../web-portal/lib/auth/passwordRecovery.ts):

```ts
function getRecoverySecret(): string {
  return process.env.AUTH_SECRET || "lcdlln-dev-recovery-secret";
}
```

Si l'env var n'est pas définie en prod, le secret HMAC de `hashAnswer` devient `"lcdlln-dev-recovery-secret"` — secret de dev en dur dans le binaire. Le hash des réponses de récupération de mot de passe devient devinable. **Sévérité haute, effort très faible.**

#### 🟡 Autres
- Stats admin sans cache ([`web-portal/app/admin/page.tsx`](../../../web-portal/app/admin/page.tsx)) : 4× `COUNT(*)` par render server-side, pas de `revalidatePath/revalidateTag`.
- 7 `console.error/warn` en code de prod ; pas de service de logs structuré.

### Verdict
Globalement sain. Pas de code mort. Validation d'inputs robuste. Pas de catch silencieux. Pas de CVE évidente dans les deps. **Fix prioritaire : F1**.

---

## 6. Transverse — code orphelin, legacy, docs, tests CI

### Candidats nettoyage (laissés tels quels par décision utilisateur)

| Élément | Statut | Décision |
|---|---|---|
| `legacy/design/lune-noire-design-system/` | Archive d'assets design, non compilé | **Conservé** — repère de suivi |
| `CMANGOS_ANALYSIS.md` (61 Ko) | Analyse comparative, nom viole `feedback_no_cmangos_term.md` | **Conservé** — repère de suivi |
| `BUILD_CHECK.md` | Checklist Vulkan obsolète pré-#427 | **Conservé** — repère de suivi |
| `smtp.local.json.example` | Aucune référence dans le code | **Conservé** — repère de suivi |
| `docs/INVESTIGATION_terrain_invisible.md` | Bug résolu 2026-05-14 | **Conservé** — référencé par CLAUDE.md |
| `build_hlod.bat` | Script HLOD offline | **Conservé** |

### Tests exclus de ctest

[`.github/workflows/build-linux.yml:76`](../../../.github/workflows/build-linux.yml) — 8 tests exclus :

**Flaky (timing-sensitive)** :
1. `session_manager_tests` — wall-clock 1.20s
2. `security_tests` — rate limit timing
3. `netserver_bandwidth_tests` — token bucket exact timing
4. `grid_state_tests` — wall-clock 70s
5. `vmap_streamer_tests` — refcount/lifecycle

**Environnement (fixtures absentes)** :
6. `localization_service_tests` — JSON localization manquants
7. `zone_builder_roundtrip_tests` — fixtures absentes

**Infrastructure** :
8. `network_integration_tests` — DB MySQL live non provisionnée

→ Cible chantier D : réintégrer 5 + 2 = 7 tests, marquer le dernier exclusion définitive.

### Doublons
Aucun doublon classe/handler détecté. Migrations uniques. Cross-refs `tickets/M100/` ↔ `docs/superpowers/specs/` cohérentes.

### Scripts / outils
`scripts/` (5 fichiers) tous appelés. `tools/` (`zone_builder`, `load_tester`, etc.) intégrés CMake.

---

## 7. Findings cross-cutting

| # | Finding | Sous-systèmes | Sévérité | Décision |
|---|---|---|---|---|
| F1 | Secret HMAC fallback en dur (`"lcdlln-dev-recovery-secret"`) si `AUTH_SECRET` manquant | Web portal | 🔴 Haute | **GO — PR 1** |
| F2 | SQL en concaténation partout, zéro prepared statement | Serveur (master) | 🟡 Moyenne | **GO — PR 2** (5 handlers prioritaires) |
| F3 | Migration 0001 manque ~50% des index FK + range queries non indexées | Serveur (DB) | 🟡 Moyenne | **GO — PR 2** (migration 0070) |
| F4 | 5 tests flaky exclus + 2 tests env (fixtures) + 1 test infra (DB live) | Serveur / shared | 🟡 Moyenne | **GO — PR 3** |
| F5 | 2 opérations zone presets non câblées (`sculpt_brush`, `splat_paint`) | Éditeur | 🟡 Basse | **Reporté** (chantier éditeur dédié) |
| F6 | Dette docs/legacy | Transverse | 🟡 Basse | **Ignoré** — repère de suivi utilisateur |
| F7 | Présence du terme "CMANGOS" dans repo | Transverse | 🟡 Basse | **Ignoré** — couvert par F6 |
| F8 | Boilerplate identique répété sur 15 outils éditeur | Éditeur | 🟢 Cosmétique | **Reporté** (chantier éditeur dédié) |
| F9 | Engine.cpp/Engine.h monolithiques | Client | 🟢 Cosmétique | **Hors scope** — refactor risqué, pas de douleur |
| F10 | `console.log/error` en prod + stats admin sans cache (web-portal) | Web portal | 🟢 Cosmétique | **GO — PR 4** |

---

## 8. Plan PRs — 4 PRs cibles

> Contrainte utilisateur : limite mensuelle de PRs proche, donc packaging compact.

### PR 1 — Sécurité web-portal (chantier A)
- `getRecoverySecret()` → fail-fast si `AUTH_SECRET` absent (throw au lieu de fallback).
- Audit transverse `web-portal/` pour autres fallbacks `process.env.X || "..."` ; correction si trouvés.
- **Déploiement** : ✅ web portal seul, aucun redéploiement serveur.
- **Effort** : très faible, ~30 lignes.

### PR 2 — Hygiène SQL serveur + index manquants (chantier C)
- Helper `DbPreparedQuery` dans `src/shared/db/` (wrapper autour de `mysql_stmt_*`).
- Migration des **5 handlers les plus exposés** en prepared statements :
  - `CharacterCreateHandler`
  - `ChatRelayHandler`
  - `AuthHandler` (login/register)
  - `CharacterListHandler`
  - `CharacterEnterWorldHandler`
- Migration idempotente `0070_add_missing_indexes.sql` couvrant les index FK + range queries manquants.
- **Pas de réorganisation `.sql`** (impact runtime trop élevé : MigrationRunner stocke un checksum SHA-256 par fichier).
- **Déploiement** : ⚠️ master + migration DB, lock-step.
- **Effort** : élevé, ~600-1000 lignes.

### PR 3 — Stabilisation CI tests (chantier D)
- Fix 5 tests flaky (token bucket exact timing, vmap streamer refcount, session/security/grid wall-clock).
- Provisionning fixtures pour `localization_service_tests` + `zone_builder_roundtrip_tests`.
- Commentaire d'exclusion définitive pour `network_integration_tests` dans `build-linux.yml`.
- **Déploiement** : ✅ CI uniquement.
- **Effort** : moyen.

### PR 4 — Polish web-portal (chantier E partiel, F10)
- Cache `revalidatePath`/`revalidateTag` sur stats admin ([`web-portal/app/admin/page.tsx`](../../../web-portal/app/admin/page.tsx)).
- Logger structuré pour remplacer les 7 `console.error/warn`.
- **Déploiement** : ✅ web portal seul.
- **Effort** : faible.

### Ordre de merge suggéré
PR 1 → PR 4 → PR 3 → PR 2 (du moins risqué au plus risqué). Toutes mergeables indépendamment, aucune stack.

### Hors scope confirmé
- **Chantier B (nettoyage docs/legacy)** : laissé tel quel sur demande utilisateur.
- **F5 + F8 (éditeur : 2 ops zone presets + factorisation 15 outils)** : chantier éditeur dédié plus tard.
- **F9 (split Engine.cpp)** : refactor risqué, pas de douleur opérationnelle.
- **Reste des handlers SQL** au-delà des 5 prioritaires : à étaler hors mois en cours.

---

## 9. Annexes

### Fichiers vérifiés directement après synthèse
- [`src/world_editor/panels/ToolPropertiesPanel.cpp`](../../../src/world_editor/panels/ToolPropertiesPanel.cpp) — vérification des `Render*Params` et dispatches → **finding éditeur infirmé**.
- [`src/masterd/handlers/character/CharacterCreateHandler.cpp:30-79`](../../../src/masterd/handlers/character/CharacterCreateHandler.cpp) — vérification de la construction SQL → **finding "SQL injection" requalifié en "absence prepared statements"**.
- [`web-portal/lib/auth/passwordRecovery.ts:50-65`](../../../web-portal/lib/auth/passwordRecovery.ts) — vérification du fallback secret → **finding confirmé F1**.
- [`src/masterd/migrations/MigrationRunner.cpp:50-63`](../../../src/masterd/migrations/MigrationRunner.cpp) — parser strict `NNNN_name.sql` + checksum SHA-256 → **renommage/déplacement des `.sql` interdit**.
