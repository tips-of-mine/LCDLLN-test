# PR 3 — Stabilisation CI tests (quick wins) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Réintégrer dans `ctest` 2 tests faussement exclus (`localization_service_tests`, `netserver_bandwidth_tests`) + 1 référence orpheline (`zone_builder_roundtrip_tests` qui n'existe pas), restructurer la commande `ctest -E` avec un commentaire structuré, et documenter les 4 tests réellement bloqués comme follow-ups.

**Architecture:** Pure modification de configuration CI (`.github/workflows/build-linux.yml`) + documentation. Aucun code C++ touché. La PR est intentionnellement minimaliste : on retire les exclusions "fausses alertes" et on laisse la CI valider ; si un de ces 2 tests échoue effectivement sur Linux, on aura un diagnostic clair (logs CI) pour un fix-up dédié.

**Tech Stack:** YAML (GitHub Actions), Markdown.

**Spec source :** [docs/superpowers/audits/2026-05-27-codebase-audit.md](../audits/closed/2026-05-27-codebase-audit.md) section 8 PR 3, scope réduit "quick wins" validé par utilisateur.

**Branche:** depuis `origin/main` (contient PR #719 mergée). Indépendante de PR #720 (en attente de merge). Pas de conflit possible — fichiers non touchés par les autres PRs.

**Déploiement:** ✅ CI uniquement, aucun redéploiement serveur/client.

---

## Justification du retrait des 3 exclusions

| Test exclu actuellement | Diagnostic | Action |
|---|---|---|
| `localization_service_tests` | Le test crée ses propres fixtures via `WriteCatalog()` → `FileSystem::WriteAllText()` ([src/client/localization/LocalizationServiceTests.cpp:28-32](../../../src/client/localization/LocalizationServiceTests.cpp)). Pas de fixture externe requise. Le rapport d'audit a faussement classé "JSON localization absents". | Retirer de `-E`. Si la CI échoue, on aura un diagnostic réel (bug FileSystem Linux ? Encoding ? Permissions ?) → fix-up dédié. |
| `netserver_bandwidth_tests` | Test purement logique sur des `double` ([src/shared/network/NetServerBandwidthThrottleTests.cpp](../../../src/shared/network/NetServerBandwidthThrottleTests.cpp)). Aucun `sleep_for`, aucun wall-clock. `now = 0.0; now += 0.2`. Ne peut pas être "flaky" — c'est déterministe. | Retirer de `-E`. |
| `zone_builder_roundtrip_tests` | **N'existe pas** comme cible CTest. Référence orpheline dans le `-E` qui ne filtre rien. | Retirer de `-E` ET du commentaire au-dessus. |

## Justification des exclusions maintenues

| Test | Statut | Raison |
|---|---|---|
| `network_integration_tests` | **Définitif** | Exige une DB MySQL live (fork serveur + handshake TLS). Aucun mock viable. CI gratuit GitHub Actions sans service MySQL provisionné. |
| `session_manager_tests` | **Temporaire** | `std::this_thread::sleep_for(1100ms)` pour tester expiry 1s (timing-sensitive). Fix : refactor `SessionManager` pour injection `Clock`/`TimeProvider`. Follow-up PR dédiée. |
| `security_tests` | **Temporaire** | Même pattern : `sleep_for(1100ms)` pour `ban_duration_sec=1`. Fix : refactor `RateLimitAndBan` pour `TimeProvider`. Follow-up PR dédiée. |
| `grid_state_tests` | **Temporaire** | Wall-clock simulé via `Tick(t0 + 70s)` mais fail "expected Idle after 70s" suggère bug dans `GridStateTracker::Tick()`. Follow-up : audit. |
| `vmap_streamer_tests` | **Temporaire** | Fail "expected 1 freed" suggère bug dans refcount/releaseDelay de `VMapStreamer`. Follow-up : audit. |

---

## File structure

| Fichier | Action | Responsabilité |
|---|---|---|
| `.github/workflows/build-linux.yml` | Modify (lignes 47-76) | Retirer 3 exclusions de `-E`, mettre à jour commentaire structuré au-dessus avec statut DEFINITIF/TEMPORAIRE par test. |
| `docs/superpowers/audits/2026-05-27-codebase-audit-followups.md` | **Create** | Document de suivi pour les 4 tests bloqués qui méritent une PR dédiée chacun (refactor TimeProvider, audits bugs). |

---

## Task 1 : Préparer la branche

**Files:** Aucun — opération git pure.

- [ ] **Step 1 : Vérifier l'état initial**

```bash
git status
git branch --show-current
```

Le working tree devrait être propre (PR #720 déjà pushée, conflits résolus). La branche actuelle est `claude/pr4-web-portal-polish` (ou autre, peu importe).

- [ ] **Step 2 : Fetch + créer la branche depuis origin/main**

```bash
git fetch origin
git switch -c claude/pr3-ci-tests-stabilization origin/main
git status
git log --oneline -1
```

Expected : branch `claude/pr3-ci-tests-stabilization`, working tree clean, HEAD = dernier commit `origin/main` (incluant PR #719 mergée).

- [ ] **Step 3 : Porter le plan PR 3 sur la nouvelle branche**

Le fichier `docs/superpowers/plans/2026-05-27-pr3-ci-tests-stabilization.md` existe sur l'ancienne branche `claude/pr4-...` (untracked). Il devrait suivre naturellement au switch s'il est untracked. Si non, le récupérer :

```bash
ls docs/superpowers/plans/2026-05-27-pr3-ci-tests-stabilization.md 2>&1
```

Si présent : passer au Step 4. Sinon, le récupérer depuis l'autre branche :

```bash
git checkout claude/pr4-web-portal-polish -- docs/superpowers/plans/2026-05-27-pr3-ci-tests-stabilization.md
```

- [ ] **Step 4 : Premier commit (plan)**

```bash
git add docs/superpowers/plans/2026-05-27-pr3-ci-tests-stabilization.md
git status --short
git commit -m "$(cat <<'EOF'
docs(plan): plan d'implémentation PR 3 — stabilisation CI tests

Issu du rapport d'audit codebase 2026-05-27 (chantier D). Scope réduit
"quick wins" validé : retirer 3 exclusions ctest (localization,
netserver_bandwidth, zone_builder_roundtrip orphelin) sans toucher
au code C++. Les 4 tests timing/refactor restent exclus, tracés
comme follow-ups dans un doc dédié.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
git log --oneline -2
```

---

## Task 2 : Modifier `.github/workflows/build-linux.yml`

**Files:** Modify `.github/workflows/build-linux.yml:47-76`.

- [ ] **Step 1 : Relire la section actuelle**

Lis les lignes 47-76 de `.github/workflows/build-linux.yml`. Tu dois voir un commentaire structuré (lignes 47-70) puis la commande ctest (ligne 76) :

```yaml
      # Exécute les tests CTest compilés (entities, mail, anticheat, etc.).
      # Exclusions (pattern -E) :
      #
      # - network_integration_tests : exige une DB MySQL live (pas provisionnée en CI).
      #
      # Les tests suivants sont *pré-existants cassés sur main* au 2026-05-11
      # (révélés par l'introduction de ce step ctest sur PR #592). Ils sont
      # exclus temporairement pour débloquer le gate, et tracés pour fix
      # ultérieur. Chacun a une catégorie :
      #
      #   Flaky / timing-sensitive (CI runners plus lents que dev) :
      #   - session_manager_tests        : "New session valid after SetState" (1.20s)
      #   - security_tests               : "Register N allowed" (rate limit timing)
      #   - netserver_bandwidth_tests    : token bucket attend rate*1s exact
      #   - grid_state_tests             : wall-clock "expected Idle after 70s"
      #   - vmap_streamer_tests          : refcount/lifecycle "expected 1 freed"
      #
      #   Environnement (fichiers de test manquants dans le PATH CI) :
      #   - localization_service_tests   : JSON localization absents
      #   - zone_builder_roundtrip_tests : fixtures absentes (!a.empty())
      #
      # Les 2 vrais bugs déterministes (chat_sanitizer_tests UTF-8 truncation
      # et vmap_format_tests check buffer size) ont été fixés dans cette
      # même PR #592 — ils ne sont PAS exclus.
      - name: Run tests (ctest)
        env:
          VCPKG_ROOT: ${{ github.workspace }}/vcpkg
        working-directory: ${{ github.workspace }}/build/linux-x64-release
        run: |
          ctest --output-on-failure --no-compress-output -E "network_integration_tests|session_manager_tests|security_tests|netserver_bandwidth_tests|grid_state_tests|vmap_streamer_tests|localization_service_tests|zone_builder_roundtrip_tests"
```

- [ ] **Step 2 : Remplacer le bloc complet**

Utilise Edit. old_string = tout le bloc ci-dessus (lignes 47-76 EXACTEMENT comme dans le fichier).

new_string :
```yaml
      # Exécute les tests CTest compilés (entities, mail, anticheat, etc.).
      #
      # Exclusions (pattern -E) — chacune a une raison documentée et un statut.
      # Voir docs/superpowers/audits/2026-05-27-codebase-audit-followups.md
      # pour le plan de réintégration des exclusions TEMPORAIRES.
      #
      # ─── DÉFINITIF ────────────────────────────────────────────────────────
      # - network_integration_tests
      #     Exige une DB MySQL live + fork serveur. Non mockable, non
      #     provisionnable sur runner GitHub Actions gratuit. Ne sera
      #     jamais réintégré sauf provisioning service MySQL au job.
      #
      # ─── TEMPORAIRE — refactor TimeProvider requis ─────────────────────────
      # - session_manager_tests
      #     Fait `std::this_thread::sleep_for(1100ms)` pour tester un expiry
      #     1s. Fix attendu : injecter un Clock/TimeProvider dans
      #     SessionManager pour rendre le test déterministe sans wall-clock.
      # - security_tests
      #     Même pattern : `sleep_for(1100ms)` pour `ban_duration_sec=1`.
      #     Fix attendu : injection TimeProvider dans RateLimitAndBan.
      #
      # ─── TEMPORAIRE — bug suspect à auditer ───────────────────────────────
      # - grid_state_tests
      #     Wall-clock simulé via Tick(t0 + 70s), fail "expected Idle after
      #     70s" suggère un bug de logique idle dans GridStateTracker::Tick().
      # - vmap_streamer_tests
      #     Fail "expected 1 freed" suggère un bug refcount + releaseDelay
      #     dans VMapStreamer. Audit nécessaire.
      #
      # ─── RÉINTÉGRÉS dans cette PR (audit 2026-05-27) ──────────────────────
      # - localization_service_tests : test crée ses propres fixtures via
      #     WriteCatalog → FileSystem::WriteAllText. Pas de fixture externe
      #     requise. Exclusion "JSON absent" était une fausse alerte.
      # - netserver_bandwidth_tests : test purement logique sur double
      #     (now += 0.2). Pas de wall-clock. Ne peut pas être flaky.
      # - zone_builder_roundtrip_tests : référence orpheline — aucune cible
      #     CTest de ce nom n'existe. Filtre inutile.
      - name: Run tests (ctest)
        env:
          VCPKG_ROOT: ${{ github.workspace }}/vcpkg
        working-directory: ${{ github.workspace }}/build/linux-x64-release
        run: |
          ctest --output-on-failure --no-compress-output -E "network_integration_tests|session_manager_tests|security_tests|grid_state_tests|vmap_streamer_tests"
```

Note clé : la liste `-E` passe de 8 entrées à **5 entrées** :
- Retirées : `localization_service_tests`, `netserver_bandwidth_tests`, `zone_builder_roundtrip_tests`
- Conservées : `network_integration_tests`, `session_manager_tests`, `security_tests`, `grid_state_tests`, `vmap_streamer_tests`

- [ ] **Step 3 : Vérifier le diff**

```bash
git diff .github/workflows/build-linux.yml
```

Expected :
- Le commentaire structuré au-dessus du step est réécrit (large diff visible).
- La commande `ctest ... -E "..."` passe de 8 entrées à 5 entrées.
- Aucun autre step du workflow n'est touché (les steps "Build", "world_editor_app", "Vérifie présence", etc. doivent rester identiques).

```bash
grep -c '|' .github/workflows/build-linux.yml
```

(Pas une vérif sémantique mais juste pour confirmer que la ligne ctest a maintenant 4 `|` séparateurs au lieu de 7 dans le bloc concerné.)

- [ ] **Step 4 : Stage + commit**

```bash
git add .github/workflows/build-linux.yml
git commit -m "$(cat <<'EOF'
ci: réintégrer localization + netserver_bandwidth + zone_builder en ctest

Audit codebase 2026-05-27 : 3 des 8 exclusions ctest étaient injustifiées.

- localization_service_tests : le test crée ses propres fixtures via
  WriteCatalog → FileSystem::WriteAllText. Aucun JSON externe requis.
  L'exclusion "JSON absent" était une fausse alerte du rapport initial.
- netserver_bandwidth_tests : test purement logique sur double (now
  += 0.2). Pas de wall-clock, pas de sleep. Ne peut pas être flaky.
- zone_builder_roundtrip_tests : référence orpheline — aucune cible
  CTest de ce nom n'existe dans le repo. Filtre inutile.

Si l'un de ces 3 tests échoue effectivement sur Linux après cette PR,
la CI fournira un diagnostic clair (logs ctest --output-on-failure)
pour un fix-up dédié.

Commentaire au-dessus du step restructuré pour distinguer clairement :
- DÉFINITIF (network_integration_tests)
- TEMPORAIRE refactor TimeProvider (session_manager, security)
- TEMPORAIRE bug à auditer (grid_state, vmap_streamer)
- RÉINTÉGRÉS dans cette PR (les 3 ci-dessus)

Suivi des 4 follow-ups dans docs/superpowers/audits/
2026-05-27-codebase-audit-followups.md (créé dans le commit suivant).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3 : Créer le doc follow-ups

**Files:** Create `docs/superpowers/audits/2026-05-27-codebase-audit-followups.md`.

- [ ] **Step 1 : Créer le doc**

Utilise Write avec ce contenu :

```markdown
# Follow-ups audit codebase 2026-05-27

> Tickets à traiter en PRs séparées suite à l'audit du 2026-05-27.
> Référence : [2026-05-27-codebase-audit.md](2026-05-27-codebase-audit.md)

## Origine

L'audit du 2026-05-27 identifiait F4 (tests CI exclus). La PR 3
([plan](../plans/2026-05-27-pr3-ci-tests-stabilization.md)) a traité les
"quick wins" : 3 exclusions ctest retirées sans modifier le code C++
(2 fausses alertes + 1 référence orpheline).

Les 4 follow-ups ci-dessous demandent du refactor de code de production
et restent à programmer en PRs dédiées (1 par follow-up suggéré).

---

## FU-1 — Injection `TimeProvider` dans `SessionManager`

**Test bloqué :** `session_manager_tests` — actuellement exclu en CI.

**Symptôme :** le test fait `std::this_thread::sleep_for(1100ms)` pour
attendre l'expiration d'une session de 1s. Sur runner CI lent, le
sleep peut prendre 1.2s ou plus → assertion timing-sensitive échoue
de façon intermittente.

**Source :** `src/masterd/session/SessionManagerTests.cpp:99` et
`src/masterd/session/SessionManager.h`.

**Fix proposé :**
1. Introduire une interface `Clock` (ou réutiliser un type existant
   du shared) injectable dans `SessionManager` au constructeur.
2. En prod, `SessionManager` utilise un `SteadyClock` qui appelle
   `std::chrono::steady_clock::now()`.
3. En test, `SessionManager` reçoit un `FakeClock` mockable où le
   test peut avancer le temps via `clock.Advance(1100ms)` sans sleep.
4. Mettre à jour `SessionManagerTests.cpp` pour utiliser le FakeClock.
5. Retirer `session_manager_tests` de l'exclusion `ctest -E` dans
   `.github/workflows/build-linux.yml`.

**Effort estimé :** 2-4h. Risque : refactor de code de prod
(SessionManager), peut nécessiter ajustement des call sites
(`master_app`, handlers qui instancient un SessionManager).

**Déploiement :** ⚠️ master + lock-step (refactor de production code).

---

## FU-2 — Injection `TimeProvider` dans `RateLimitAndBan`

**Test bloqué :** `security_tests` — actuellement exclu en CI.

**Symptôme :** identique à FU-1 — `sleep_for(1100ms)` pour
`ban_duration_sec=1`.

**Source :** `src/shared/security/SecurityTests.cpp:99` et
`src/shared/security/RateLimitAndBan.h`.

**Fix proposé :** même pattern que FU-1 (injection `Clock`/
`TimeProvider`). Pourrait partager une abstraction commune avec FU-1
(même interface Clock dans `src/shared/core/`).

**Effort estimé :** 2-4h (peut être combiné avec FU-1 dans 1 PR
"refactor: injection Clock dans SessionManager + RateLimitAndBan"
si l'abstraction est commune).

**Déploiement :** ⚠️ master + lock-step.

---

## FU-3 — Audit bug `GridStateTracker::Tick()`

**Test bloqué :** `grid_state_tests` — actuellement exclu en CI.

**Symptôme :** le test fait `t.Tick(t0 + 70s)` (timing logique, pas
wall-clock) et attend que le grid passe en état Idle après 70s. Le
message d'erreur "expected Idle after 70s" suggère un bug réel dans
la logique de timeout d'idle, pas un problème de timing CI.

**Source :** `src/shardd/world/GridStateTests.cpp:93` et
`src/shardd/world/GridState.h`/`.cpp`.

**Fix proposé :**
1. Lire le code de `GridStateTracker::Tick()` et identifier la
   condition qui transitionne vers Idle.
2. Comparer avec le test : quelle valeur exacte est attendue après
   `Tick(t0 + 70s)` ?
3. Soit fixer le bug d'implémentation, soit ajuster le test si
   l'attente est incorrecte.
4. Retirer `grid_state_tests` de l'exclusion.

**Effort estimé :** 2-3h d'audit puis 30-60min de fix.

**Déploiement :** ⚠️ shard + lock-step (refactor code de prod).

---

## FU-4 — Audit bug refcount/releaseDelay `VMapStreamer`

**Test bloqué :** `vmap_streamer_tests` — actuellement exclu en CI.

**Symptôme :** fail "expected 1 freed" sur timing logique
(TP t0, t0+3s, t0+10s). Suggère un bug dans la logique de refcount +
`releaseDelay` qui libère les ressources VMap.

**Source :** `src/shardd/internals/vmap/VMapStreamerTests.cpp:128` et
`src/shardd/internals/vmap/VMapStreamer.h`/`.cpp`.

**Fix proposé :** audit du décompte de référence + de la séquence
de libération. Si bug → fixer. Sinon ajuster le test.

**Effort estimé :** 3-5h (lifecycle complexe, attention aux races).

**Déploiement :** ⚠️ shard + lock-step.

---

## Synthèse

| Follow-up | Effort | Priorité | Peut être combiné ? |
|---|---|---|---|
| FU-1 + FU-2 | 4-6h | Moyenne | **Oui** — abstraction Clock commune dans 1 PR |
| FU-3 | 2-4h | Moyenne | Non — zone shard distincte |
| FU-4 | 3-5h | Moyenne | Non — lifecycle distinct |

**Recommandation :** 3 PRs (FU-1+FU-2 combinées, FU-3 seule, FU-4
seule). Total ~12-15h spread sur 3 PRs. Pas urgent : ces tests sont
des protections contre régressions, leur absence ne casse rien
fonctionnellement, juste un trou de couverture.
```

- [ ] **Step 2 : Stage + commit**

```bash
git add docs/superpowers/audits/2026-05-27-codebase-audit-followups.md
git commit -m "$(cat <<'EOF'
docs(audit): document follow-ups CI tests bloqués (FU-1 à FU-4)

Trace les 4 tests qui restent exclus de ctest après la PR 3 (quick
wins) avec une stratégie de fix concrète pour chacun.

FU-1 + FU-2 (session_manager_tests + security_tests) : refactor
TimeProvider, combinables en 1 PR. ~4-6h.

FU-3 (grid_state_tests) : audit bug GridStateTracker::Tick() idle
70s. ~2-4h.

FU-4 (vmap_streamer_tests) : audit bug refcount + releaseDelay
dans VMapStreamer. ~3-5h.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 3 : Vérifier**

```bash
git log --oneline -4
git status
```

Expected : 3 commits sur la branche, working tree clean.

---

## Task 4 : Push + créer PR

**Files:** Aucun — opérations git/gh.

- [ ] **Step 1 : État final**

```bash
git log --oneline origin/main..HEAD
git status
git branch --show-current
```

Expected : branch `claude/pr3-ci-tests-stabilization`, working tree clean, 3 commits ahead de main :
1. `docs(plan): plan d'implémentation PR 3`
2. `ci: réintégrer localization + netserver_bandwidth + zone_builder en ctest`
3. `docs(audit): document follow-ups CI tests bloqués (FU-1 à FU-4)`

- [ ] **Step 2 : Push**

```bash
git push -u origin claude/pr3-ci-tests-stabilization
```

- [ ] **Step 3 : Créer la PR via `gh`**

```bash
gh pr create --title "ci: réintégrer 3 tests faussement exclus de ctest" --body "$(cat <<'EOF'
## Summary

Issue de l'audit codebase 2026-05-27 ([rapport](docs/superpowers/audits/2026-05-27-codebase-audit.md), chantier D, scope réduit "quick wins").

L'audit a identifié 8 tests exclus dans `ctest -E`. Investigation approfondie : **3 exclusions sur 8 étaient injustifiées**.

- **`localization_service_tests`** : le test crée ses propres fixtures via `WriteCatalog()` → `FileSystem::WriteAllText()`. Aucun fichier JSON externe requis. L'exclusion "JSON absent" était une fausse alerte.
- **`netserver_bandwidth_tests`** : test purement logique sur `double` (`now += 0.2`). Aucun `sleep_for`, aucun wall-clock. Ne peut pas être "flaky" — c'est déterministe.
- **`zone_builder_roundtrip_tests`** : référence orpheline dans `-E` — aucune cible CTest de ce nom n'existe dans le repo. Filtre inutile.

Cette PR :
1. Retire ces 3 exclusions de `.github/workflows/build-linux.yml`.
2. Restructure le commentaire au-dessus du step `ctest` pour distinguer clairement DÉFINITIF / TEMPORAIRE-refactor / TEMPORAIRE-audit / RÉINTÉGRÉS.
3. Documente les 4 follow-ups restants (`session_manager_tests`, `security_tests`, `grid_state_tests`, `vmap_streamer_tests`) dans un nouveau doc avec stratégie de fix concrète.

## Test plan

- [ ] CI Linux verte (build + ctest).
- [ ] Si l'un des 3 tests réintégrés échoue effectivement sur Linux, on aura un diagnostic clair via les logs `ctest --output-on-failure` → fix-up dédié dans une PR de suivi (5-30 min).
- [ ] Vérifier que les 5 tests restants exclus le sont toujours (la liste `-E` passe de 8 à 5 entrées).

## Déploiement

✅ **CI uniquement** — aucun redéploiement serveur ni client. Aucun code de production touché.

## Suivi

PR 3 sur 4 du plan d'audit. Reste : PR 2 (hygiène SQL serveur — lock-step master + DB).

Les 4 follow-ups CI documentés dans `docs/superpowers/audits/2026-05-27-codebase-audit-followups.md` peuvent être ouverts en parallèle / hors mois selon priorité.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

- [ ] **Step 4 : Vérifier**

```bash
gh pr view --json url,number,state,title
```

Expected : JSON avec URL de la PR, état OPEN.

---

## Self-review

**Spec coverage** (vs spec de cette PR) :
- ✅ Retirer `localization_service_tests` de `-E` → Task 2 Step 2 (liste passe de 8 à 5)
- ✅ Retirer `netserver_bandwidth_tests` de `-E` → Task 2 Step 2
- ✅ Retirer `zone_builder_roundtrip_tests` de `-E` (référence orpheline) → Task 2 Step 2
- ✅ Restructurer la commande + commentaire avec rationale par exclusion → Task 2 Step 2
- ✅ Documenter les 4 follow-ups → Task 3 (nouveau fichier dédié)
- ✅ Aucun code C++ touché → vérifiable via `git diff --stat`
- ✅ Branche depuis origin/main, indépendante de PR #719 mergée et PR #720 en attente → Task 1 Step 2

**Placeholder scan :** RAS. Tous les chemins, lignes, commandes git et code blocks sont concrets.

**Type consistency :** N/A (pas de code, juste YAML + Markdown).

**Risques / edge cases :**
- ✅ Si la CI échoue effectivement sur localization ou netserver après la réintégration, on aura un diagnostic réel — c'est l'objectif du "quick win" (préférable à laisser une fausse exclusion en place).
- ✅ Pas de conflict possible avec PR #720 (en attente) car cette PR ne touche pas `.github/workflows/` ni le nouveau doc d'audit.
- ✅ Le doc `2026-05-27-codebase-audit-followups.md` référence le rapport principal `2026-05-27-codebase-audit.md` via lien relatif standard (vérifiable au rendu GitHub).
