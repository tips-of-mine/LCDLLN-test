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

## FU-1 — Injection `TimeProvider` dans `SessionManager` ✅ RÉSOLU (PR #742, 2026-05-28)

**Test bloqué :** ~~`session_manager_tests` — actuellement exclu en CI.~~
→ Test **réintégré** au pattern `ctest -E` du job `build-linux`.

**Symptôme initial :** `std::this_thread::sleep_for(1100ms)` pour
attendre l'expiration d'une session de 1s, fragile sur runner CI lent.

**Fix appliqué (PR #742) :**
1. Nouvelle interface `engine::core::IClock` + `SteadyClock` (default
   runtime) + `FakeClock` (tests) dans `src/shared/core/Clock.{h,cpp}`.
2. `SessionManager::SetClock(IClock*)` (setter, default = singleton
   `SteadyClock::Instance()`).
3. Tous les `Clock::now()` internes remplacés par `clock().Now()`.
4. `SessionManagerTests.cpp` : `FakeClock` + `AdvanceMs(1100)` au lieu
   de `sleep_for`.
5. Retiré du `ctest -E` dans `.github/workflows/build-linux.yml`.

**Bug latent corrigé en bonus :** `TestDuplicateLoginKickExisting`
appelait `Validate(sid2)` AVANT le `SetState(Active)` — assertion
buggée pré-existante masquée par l'exclusion CI. Le test attendait
`true` mais `Created` n'est pas un état valide. Assertion corrigée
en `!Validate(sid2)` + `Validate(sid2)` post-`SetState`.

**Déploiement :** ✅ pas de redéploiement requis (comportement runtime
identique car aucun appelant ne câble `SetClock` — tout tombe sur
`SteadyClock::Instance()` par défaut).

---

## FU-2 — Injection `TimeProvider` dans `RateLimitAndBan` ✅ RÉSOLU (PR #742, 2026-05-28)

**Test bloqué :** ~~`security_tests` — actuellement exclu en CI.~~
→ Test **réintégré** au pattern `ctest -E`.

**Fix appliqué (PR #742) :** identique à FU-1 (`SetClock(IClock*)` +
remplacement de `Clock::now()` par `clock().Now()`). Le module
partage l'abstraction `engine::core::IClock` avec `SessionManager`.

**Bug critique réel corrigé en bonus :** la réintégration CI a révélé
que `TokenBucket` démarrait `tokens=0` + `last_refill=epoch`. Au 1er
`TryConsume`, `elapsed = now() - epoch = uptime du process`. Conséquence
en prod : sur un master master fraîchement (re)démarré, le 1er
`/register` d'une nouvelle IP était **silencieusement refusé pendant
~20 minutes** (`register_per_hour=3` → refill ≈ 0.000833 tok/s → besoin
de ~1200s d'uptime pour accumuler 1 token).

**Fix warm-start :** `RateLimitAndBan::getOrCreateState()` initialise
`tokens=capacity` + `last_refill=now()` à la 1ère insertion d'une IP
dans `m_by_ip`. Pattern aligné avec `UserRateLimiter.cpp:55-60` qui
faisait déjà ce warm-start (convention pré-existante du codebase, le
seul oubli était `RateLimitAndBan`).

**Déploiement :** ⚠️ redéploiement serveur master recommandé pour
bénéficier du fix warm-start (sinon le bug `/register` muet pendant
~20 min après chaque restart persiste). Pas urgent (le contournement
est "attendre 20 min après restart") mais utile.

---

## FU-3 — Audit bug `GridStateTracker::Tick()` ✅ RÉSOLU (PR #744, 2026-05-28)

**Test bloqué :** ~~`grid_state_tests` — actuellement exclu en CI.~~
→ Test **réintégré** au pattern `ctest -E`.

**Symptôme initial :** le test faisait `t.Tick(t0 + 70s)` (timing
logique, pas wall-clock) et le grid restait `Loaded` au lieu de
passer à `Idle`. Message d'erreur "expected Idle after 70s".

**Source :** [`src/shardd/world/GridStateTests.cpp:93`](../../../src/shardd/world/GridStateTests.cpp) et
[`src/shardd/world/GridState.h`/`.cpp`](../../../src/shardd/world/GridState.cpp).

**Fix appliqué (PR #744 \"FU-6\", 2026-05-28) :** Cause racine identifiée
par analyse statique pure : `Entry::lastEmptySince` utilisait `TimePoint{}`
(epoch) comme sentinel \"non initialisé\" alors que `TimePoint{}` est aussi
une valeur LÉGITIME. Quand un appelant passait `t0=TimePoint{}` (typique
des tests reproductibles), `OnPlayerLeave` assignait `lastEmptySince=epoch`,
indistinguable du sentinel → `Tick` suivant réinitialisait au lieu de
mesurer elapsed depuis Leave → `elapsed=40s < idleTimeout(60s)` → restait
Loaded au lieu de passer Idle.

**Fix :** `lastEmptySince` passé à `std::optional<TimePoint>`. `nullopt`
= vraiment non init, `optional<epoch>` = valeur légitime distinguable.
3 sites adaptés (Enter/Leave/Tick).

**Déploiement :** ⚠️ shard recommandé (logique cellule, runtime prod
inchangé en pratique car `steady_clock::now()` n'est jamais epoch dans
la vraie vie — fix défensif).

**Note numérotation :** le titre de la PR utilise \"FU-6\" car les fixes
ont été poussés dans l'ordre où ils ont été attaqués, pas dans l'ordre
de ce rapport. Mapping : Audit FU-3 = PR FU-6 (cette fiche).

---

## FU-4 — Audit bug refcount/releaseDelay `VMapStreamer` ✅ RÉSOLU (PR #745, 2026-05-28)

**Test bloqué :** ~~`vmap_streamer_tests` — actuellement exclu en CI.~~
→ Test **réintégré** au pattern `ctest -E`.

**Symptôme initial :** fail \"expected 1 freed\" sur timing logique
(TP t0, t0+3s, t0+10s).

**Fix appliqué (PR #745 \"FU-7\", 2026-05-28) :** **Pattern strictement
identique à FU-3** (d'où la rapidité du diagnostic). `ManagedModel::m_zeroRefSince`
utilisait `TimePoint{}` comme sentinel \"refcount > 0 ou jamais Release\".
Quand un test/callsite faisait `Release(epoch)`, `m_zeroRefSince = epoch`
était indistinguable du sentinel → `ShouldRelease` retournait éternellement
false → tile jamais déchargé.

**Fix :** `m_zeroRefSince` → `std::optional<TimePoint>`, même remède
qu'en FU-3. 3 sites adaptés (IncRef/DecRef/ShouldRelease).

**Insight transverse :** le pattern \"TimePoint{} sentinel\" est apparu
deux fois dans `src/shardd/` (`GridState` + `VMapStreamer`), origine
probable d'un copy-paste lors de l'implémentation du tick logique.
Recherche transverse confirme aucune autre occurrence active dans le
codebase.

**Déploiement :** ⚠️ shard recommandé (idem FU-3, logique VMap streaming).

**Note numérotation :** PR titrée \"FU-7\". Mapping : Audit FU-4 = PR FU-7.

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

## FU-6 — Bug Linux `LocalizationService::Init()` ✅ RÉSOLU (PR #746, 2026-05-28)

**Test bloqué :** ~~`localization_service_tests` — re-exclu en CI Linux
après tentative de réintégration (PR #721).~~ → Test **réintégré**.

**Symptôme initial :** `LocalizationService::Init()` retournait `false`
même après que `WriteCatalog()` (qui appelle `FileSystem::WriteAllText`)
ait réussi.

**Cause racine identifiée par analyse statique :** le diagnostic
initial était une **fausse piste** vers un bug Linux. En réalité, le
helper de test `WriteCatalog` écrivait dans `<root>/localization/text/<locale>.json`
alors que `LocalizationService::Init` (l. 100-113) scanne
`<root>/localization/` à la recherche de sous-dossiers nommés par tag
de locale, puis cherche `<entry>/<entry>.json`. `LoadCatalog`
(l. 322-342) reconstruit la même structure. Le sous-dossier `text/`
ne matchant aucun tag, Init retournait toujours `false`. Sur **les
deux plateformes** en réalité — la CI Windows ne lance pas ctest
(cf. `build-windows.yml`), donc le bug n'était jamais détecté côté
Win. Probable origine : la refactor [#541](https://github.com/tips-of-mine/LCDLLN-test/pull/541)
("regroup flat files into domain sub-folders") a renommé la structure
de localization, mais le helper de test n'a pas suivi.

**Confirmation :** vraies données = `game/data/localization/{en,fr}/<locale>.json`
→ format attendu = `<locale>/<locale>.json`.

**Fix appliqué (PR #746 \"FU-3\", 2026-05-28) :** helper `WriteCatalog`
corrigé pour utiliser la structure attendue. Zéro changement de code
prod (`LocalizationService.cpp` non modifié).

**Déploiement :** ✅ test-only, pas de redéploiement.

**Note numérotation :** PR titrée \"FU-3\". Mapping : Audit FU-6 = PR FU-3.

---

## FU-7 — Bug Linux `zone_builder_roundtrip_tests` ✅ RÉSOLU (PR #747, 2026-05-28)

**Test bloqué :** ~~`zone_builder_roundtrip_tests` — re-exclu en CI Linux
après tentative de réintégration (PR #721).~~ → Test **réintégré**.

**Symptôme initial :** deux types d'échec : `ReadFileBytes(...).empty()`
retourne true ; `rebuilt.positionX == source.positionX` échoue (précision
float).

**Cause racine identifiée par analyse statique :** comme FU-6, le
diagnostic initial était une **fausse piste Linux**. En réalité,
**deux bugs purement test-side**, plateforme-indépendants, invisibles
jusqu'ici car la CI Windows ne lance pas ctest.

**Bug 1 — `Test_WriteChunkPackage_DeterministicBytes`** : le test bouclait
sur 6 fichiers et exigeait `REQUIRE(!a.empty())` pour TOUS. Or
`WriteChunkPackage` appelle `WriteEmptyBin` pour `instances.bin`/
`navmesh.bin`/`probes.bin` — ces fichiers sont produits **vides par
design** (0 octet, juste `ofstream` ouvert puis fermé sans write).
La REQUIRE échouait au 1er empty file rencontré.

**Bug 2 — `Test_LayoutDocument_JsonRoundtrip`** : sérialisait des doubles
via `oss << inst.positionX` sans `setprecision`. Précision par défaut
d'un `ostream` = 6 chiffres significatifs → `9000.125` (7 chiffres)
était écrit "9000.12" → re-parsé en 9000.12 → assertion bit-à-bit
échouait.

**Fix appliqué (PR #747 "FU-4", 2026-05-28) :**
- Bug 1 : `REQUIRE(std::filesystem::exists(...))` au lieu de `!a.empty()`.
  Conserve l'égalité de taille + hash FNV-1a pour le déterminisme.
- Bug 2 : `oss << std::setprecision(std::numeric_limits<double>::max_digits10)`
  (= 17 chiffres garantis bit-à-bit pour double IEEE-754).

Zéro changement de code prod.

**Déploiement :** ✅ test-only, pas de redéploiement.

**Note numérotation :** PR titrée "FU-4". Mapping : Audit FU-7 = PR FU-4.

---

## Synthèse

| Follow-up | Effort initial | Statut final |
|---|---|---|
| FU-1 + FU-2 | 4-6h | ✅ **Résolu** (PR #742, 2026-05-28) — IClock + warm-start bonus |
| FU-3 | 2-3h | ✅ **Résolu** (PR #744 \"FU-6\", 2026-05-28) — sentinel TimePoint{} → optional |
| FU-4 | 3-5h | ✅ **Résolu** (PR #745 \"FU-7\", 2026-05-28) — même pattern sentinel que FU-3 |
| FU-5 | 30 min | ✅ Résolu (tolérance ±1 byte) |
| FU-6 | 2-4h | ✅ **Résolu** (PR #746 \"FU-3\", 2026-05-28) — bug path dans le test |
| FU-7 | 3-5h | ✅ **Résolu** (PR #747 \"FU-4\", 2026-05-28) — empty check + setprecision |

**🎯 État final au 2026-05-28 : 7/7 follow-ups résolus.**

**Insights consolidés :**

1. **L'analyse statique a suffi pour 4/4 FU initialement classés
   \"bloqués par toolchain locale\"** (FU-3, FU-4, FU-6, FU-7). Sur ces
   4, **2 étaient des bugs purement test-side** (FU-6, FU-7) et **2
   étaient un pattern sentinel TimePoint{} défensif** (FU-3, FU-4 —
   bug latent mais inactif en prod car `steady_clock::now()` n'est
   jamais epoch dans la vraie vie).

2. **La CI Windows ne lance pas ctest** (cf. `build-windows.yml` l. 112-125),
   donc tout test exclu de `ctest -E` sur Linux était de facto exclu
   partout. Le diagnostic \"bug Linux\" était une fausse piste pour
   FU-6 et FU-7 : les tests étaient cassés partout, juste non détectés
   côté Windows.

3. **Le pattern `TimePoint{}` comme sentinel est apparu deux fois
   dans `src/shardd/`** (`GridState::Entry::lastEmptySince` +
   `ManagedModel::m_zeroRefSince`). Probable copy-paste lors de
   l'implémentation des timers logiques. Recherche transverse codebase
   confirme aucune autre occurrence active (PasswordResetStore et
   AuthUi utilisent aussi le pattern mais sont **inactifs** : le
   premier utilise `system_clock` qui n'est jamais epoch en prod, le
   second est UI client sans test unitaire le déclenchant).

4. **Bug réel prod corrigé en bonus** (PR #742) : `RateLimitAndBan`
   warm-start manquant. Premier `/register` d'une nouvelle IP refusé
   pendant ~20 min après chaque restart serveur. Pattern aligné avec
   `UserRateLimiter` qui faisait déjà ce warm-start (le seul oubli
   était `RateLimitAndBan`).

**Leçon clé pour les audits futurs :** avant de présumer un bug
plateforme-spécifique, **vérifier où le test est vraiment exécuté**.
Si la CI d'une plateforme ne lance pas ce test, le bug peut être
plateforme-indépendant et juste invisible côté \"passant\".
