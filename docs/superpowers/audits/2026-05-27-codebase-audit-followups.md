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
