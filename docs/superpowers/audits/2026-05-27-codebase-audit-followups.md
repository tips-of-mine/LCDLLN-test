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

## FU-5 — Arithmétique floating-point `netserver_bandwidth_tests` — ✅ RÉSOLU

**Statut :** ✅ Résolu dans une PR ultérieure (option A — assertion avec
tolérance ±1 byte). Le test est réintégré dans `ctest`, plus exclu.

**Test (anciennement) bloqué :** `netserver_bandwidth_tests`.

**Symptôme :** assertion `Assert(totalSent == 1000u, ...)` ligne 88 du
test échoue sur Linux GCC après 5 itérations de `now += 0.2`. La somme
de 5 × 0.2 en IEEE 754 double ne donne pas exactement 1.0, et le
`static_cast<size_t>(b.tokensBytes)` tronque vers le bas — sur Linux,
au moins une itération donne 199 au lieu de 200, totalSent = 999.

**Source :** [src/shared/network/NetServerBandwidthThrottleTests.cpp:88](../../../src/shared/network/NetServerBandwidthThrottleTests.cpp).

**Diagnostic clé :** le rapport d'audit initial a classé ce test comme
"déterministe donc ne peut pas être flaky" — vrai au sein d'une seule
plateforme, mais **cross-platform GCC vs MSVC ne produit pas la même
représentation flottante**. Le test passe sur Windows MSVC, échoue
sur Linux GCC.

**Fix proposé :**
1. Soit changer l'assertion en tolérance : `Assert(totalSent >= 999u && totalSent <= 1000u, ...)`.
2. Soit recalculer en entiers : `now_us` en `uint64_t` au lieu de `double`, `step_us = 200000`. Plus robuste.

**Effort estimé :** ~30 min (modif test seul, pas de code prod touché).

**Déploiement :** ✅ CI uniquement (test-only).

---

## FU-6 — Bug Linux `LocalizationService::Init()`

**Test bloqué :** `localization_service_tests` — re-exclu en CI Linux
après tentative de réintégration (PR #721).

**Symptôme :** sur Linux, `LocalizationService::Init()` retourne `false`
même après que `WriteCatalog()` (qui appelle `FileSystem::WriteAllText`)
ait réussi (les assertions "write en/fr catalog" passent). Les 8
assertions suivantes échouent en cascade car le service n'est pas
initialisé.

**Source :** [src/client/localization/LocalizationServiceTests.cpp](../../../src/client/localization/LocalizationServiceTests.cpp)
+ [src/client/localization/LocalizationService.cpp](../../../src/client/localization/LocalizationService.cpp).

**Diagnostic clé :** vrai bug Linux à diagnostiquer. Pistes :
- `FileSystem::ReadAllText` se comporte différemment sur Linux ?
- Path traversal `temp_directory_path() / "localization" / "text" / "en.json"`
  fonctionne différemment ?
- Encoding UTF-8 / BOM Linux vs Windows ?
- Parsing JSON qui plante sur les caractères accentués Linux ?
- Permissions de `/tmp/` ?

**Fix proposé :** demande un build Linux local (ou ajouter du
log debug temporaire au test pour comprendre où Init() échoue) puis
diagnostic ciblé.

**Effort estimé :** ~2-4h (~1h diag + 1-3h fix selon la cause racine).

**Déploiement :** dépend de la cause — si bug `FileSystem`, lock-step
client + serveur (composant shared). Si bug `LocalizationService`,
client uniquement.

---

## FU-7 — Bug Linux `zone_builder_roundtrip_tests`

**Test bloqué :** `zone_builder_roundtrip_tests` — re-exclu en CI Linux
après tentative de réintégration (PR #721). Le test **existe bien**
(l'agent d'exploration initial s'était trompé) à
[tools/zone_builder/lib/tests/RoundtripTests.cpp](../../../tools/zone_builder/lib/tests/RoundtripTests.cpp)
et est enregistré dans [tools/zone_builder/lib/CMakeLists.txt:65](../../../tools/zone_builder/lib/CMakeLists.txt).

**Symptôme :** deux types d'échec :
1. `ReadFileBytes(dirA / "probes.bin").empty()` retourne `true` sur
   Linux → les fichiers binaires écrits puis relus sont vides.
2. `rebuilt.instances[i].positionX == source.instances[i].positionX`
   échoue à la ligne 295 → précision float dans le serialize/parse
   round-trip.

**Source :** [tools/zone_builder/lib/tests/RoundtripTests.cpp:170-180,290-300](../../../tools/zone_builder/lib/tests/RoundtripTests.cpp).

**Diagnostic clé :** au moins 2 bugs distincts :
- **Bug 1 (binaires vides)** : la sérialisation écrit-elle bien le
  binaire sur Linux ? `FileSystem::WriteAllBytes` vs `WriteAllText` ?
  Ou problème de path absolu vs relatif ?
- **Bug 2 (précision float)** : roundtrip float JSON peut perdre des
  ULP. À comparer avec `std::abs(a - b) < epsilon` plutôt que `==`.

**Fix proposé :**
1. Diagnostiquer Bug 1 d'abord (ajouter log dans le test pour voir
   les paths utilisés, vérifier que `WriteAllBytes` écrit bien).
2. Bug 2 : changer l'assertion exacte par une tolérance epsilon.

**Effort estimé :** ~3-5h (lifecycle complexe — serializer +
deserializer + comparaisons).

**Déploiement :** ✅ tooling éditeur uniquement, pas de redéploiement
serveur.

---

## Synthèse

| Follow-up | Effort | Priorité | Statut |
|---|---|---|---|
| FU-1 + FU-2 | 4-6h | Moyenne | À faire |
| FU-3 | 2-4h | Moyenne | À faire |
| FU-4 | 3-5h | Moyenne | À faire |
| FU-5 | 30 min | — | ✅ **Résolu** (tolérance ±1 byte) |
| FU-6 | 2-4h | Moyenne | À faire |
| FU-7 | 3-5h | Moyenne | À faire |

**Recommandation :** FU-5 fixé (quick win consommé). Reste 5 follow-ups
à programmer en PRs séparées :
- FU-1+FU-2 combinables (abstraction `Clock` commune)
- FU-3 et FU-4 séparées (zones distinctes)
- FU-6 + FU-7 demandent un build Linux pour diagnostiquer

Total restant : ~12-15h spread sur 4-5 PRs. Pas urgent : ces tests sont
des protections contre régressions, leur absence ne casse rien
fonctionnellement.
