# EXT-2 — Quêtes répétables / quotidiennes / hebdo + auto-complete

**Date** : 2026-07-03
**Statut** : design (à relire)
**Doc parent** : `2026-07-02-quest-system-overview-design.md` ; stackée sur EXT-1 (`excludes`).
**Portée** : rendre une quête re-réalisable selon un **mode par quête** (`repeatable`/`daily`/`weekly`/`cooldown`) + drapeau **`autoComplete`** (fin automatique sans retour PNJ). **Shard + persistance + éditeur — aucun changement de wire.**

> **Décisions produit validées (user, 2026-07-03)** : modèle **par quête** (chaque quête choisit son mode dans le JSON) ; inclure les **3 drapeaux** (Repeatable sans limite, Daily/Weekly, AutoComplete).

---

## 1. Modèle de données

### 1.1 Schéma JSON (champs optionnels, rétro-compatibles)
```jsonc
{
  "id": "...", "giver": "...", "turnIn": "...", "steps": [...], "rewards": {...},
  "repeat": "none",        // ∈ {none, repeatable, daily, weekly, cooldown} — défaut "none" (absent = none)
  "cooldownHours": 24,     // entier > 0 — requis/utilisé UNIQUEMENT si repeat == "cooldown"
  "autoComplete": false    // bool — défaut false
}
```

### 1.2 `QuestDefinition` (`QuestRuntime.h`)
```cpp
enum class QuestRepeatMode : uint8_t { None = 0, Repeatable = 1, Daily = 2, Weekly = 3, Cooldown = 4 };
// nouveaux champs :
QuestRepeatMode repeatMode = QuestRepeatMode::None;
uint32_t        cooldownHours = 0;      // pertinent si repeatMode == Cooldown
bool            autoComplete  = false;
```

### 1.3 `QuestState` (`QuestRuntime.h:65`) — état par personnage
```cpp
uint64_t completedAtEpochMs = 0;   // ms UTC de la dernière complétion ; 0 = jamais
```

## 2. Sémantique de reset

Une quête `Completed` dont `repeatMode != None` **redevient disponible** quand sa borne de reset est franchie : `Completed → Locked` (puis la ré-évaluation prérequis/exclusion la repromeut à `Offered`), `stepProgressCounts` remis à zéro, delta émis.

Fonction **pure et testable** (pas d'horloge interne, `now` injecté) :
```cpp
bool ShouldRepeatReset(QuestRepeatMode mode, uint32_t cooldownHours,
                       uint64_t completedAtMs, uint64_t nowMs);
```
- `None` → `false` (jamais).
- `Repeatable` → `true` (immédiat, aucun délai).
- `Daily` → `UtcDayIndex(nowMs) != UtcDayIndex(completedAtMs)`.
- `Weekly` → `UtcWeekIndex(nowMs) != UtcWeekIndex(completedAtMs)`.
- `Cooldown` → `completedAtMs != 0 && nowMs >= completedAtMs && (nowMs - completedAtMs) >= uint64_t(cooldownHours) * 3600000ULL`.

Helpers entiers purs :
- `UtcDayIndex(ms)  = ms / 86400000ULL` (jour UTC, minuit UTC comme borne).
- `UtcWeekIndex(ms) = (ms / 86400000ULL + 3) / 7` (le jour epoch 0 = jeudi ; `+3` aligne les semaines sur **lundi 00:00 UTC**).

**Limite V1 (assumée)** : reset **évalué au login et au Talk** (pas de tick horaire). Un joueur resté en ligne au passage de minuit verra le reset au prochain Talk/relog. Suffisant pour la V1 ; un tick périodique = amélioration future.

### 2.1 `ApplyRepeatResets` (nouvelle méthode `QuestRuntime`)
```cpp
bool ApplyRepeatResets(std::vector<QuestState>& states, uint64_t nowMs,
                       std::vector<QuestProgressDelta>& outDeltas) const;
```
Pour chaque `state` `Completed` avec `def.repeatMode != None` et `ShouldRepeatReset(...)` : `status = Locked`, `stepProgressCounts` mis à zéro, delta émis. Retourne `true` si au moins un changement.
**Séparée** de `SyncQuestStates` (celle-ci reste inchangée → tests EXT-1 valides). **Ordre d'appel** : `ApplyRepeatResets(states, now)` **AVANT** `SyncQuestStates(states)` (le reset `Locked` est alors repromu `Offered` dans la même passe).

### 2.2 Sites d'appel (`ServerApp.cpp`)
1. **Login** (`ServerApp.cpp:~1525`, où `SyncQuestStates` est déjà appelé) : appeler `ApplyRepeatResets(client.questStates, NowUnixEpochMsUtc(), deltas)` juste avant, envoyer les deltas.
2. **Talk / giver-list** : avant de construire la `QuestGiverList` renvoyée au Talk (localiser le site d'envoi `QuestGiverList`), appeler `ApplyRepeatResets(...)` puis re-`SyncQuestStates(...)` pour que les quêtes `repeatable`/quotidiennes re-`Offered` apparaissent **sans relog**.

## 3. AutoComplete

### 3.1 `QuestRuntime::ApplyEvent` (`QuestRuntime.cpp:740-750`)
Quand `AreAllStepsComplete` : si `definition.autoComplete` → `state.status = QuestStatus::Completed` (au lieu de `ReadyToTurnIn`) ; le delta reflète `Completed`. Sinon comportement actuel (`ReadyToTurnIn`). **La récompense n'est PAS versée dans `QuestRuntime`** (pas d'accès inventaire) — versée par le caller ServerApp (§3.2).

### 3.2 Versement de récompense — refactor `ServerApp`
- **Extraire** le bloc de versement de `HandleTurnInQuest` (`ServerApp.cpp:5437-5454` : `TakeRewardOnTurnIn` + `ApplyLevelUpsAfterXp` + `AddCurrency` + `AddItemToInventory`) dans un helper privé `void ServerApp::GrantQuestReward(ConnectedClient& client, const QuestDefinition& def);`.
- `HandleTurnInQuest` : appelle `GrantQuestReward` ; **stampe** `state.completedAtEpochMs = NowUnixEpochMsUtc()`.
- `ServerApp::ApplyQuestEvent` (`5225`, appelle `m_questRuntime.ApplyEvent` `5233`) : après `ApplyEvent`, pour tout delta dont `status == Completed` (auto-complété), retrouver la `QuestDefinition`, **stamper** `completedAtEpochMs = NowUnixEpochMsUtc()` sur l'état, et appeler `GrantQuestReward`. (Une quête non-autoComplete passe par `ReadyToTurnIn` → pas de delta `Completed` ici → pas de double versement.)

## 4. Persistance (`CharacterPersistence.cpp`) — pas de wire

- **Sérialisation** (`~280-291`) : bumper `quests.format_version` de `1` → `2` ; ajouter `quests.<i>.completed_at=<completedAtEpochMs>`.
- **Désérialisation** (`~150-166`) : lire `quests.<i>.completed_at` avec **défaut 0** (les sauvegardes v1 en sont dépourvues → `completedAtEpochMs = 0`, inoffensif : une quotidienne déjà complétée redeviendra disponible au 1er login, comportement voulu).
- `MapPersistedQuestStatus`/compat statut inchangés. C'est un **bump additif** de format de save (serveur, DB/blob) — **pas** de wire, mais ⚠️ redéploiement shard.

## 5. Éditeur (`QuestEditIo` + `QuestEditorPanel`)

- **`EditedQuest`** : `QuestRepeatMode repeatMode = None; uint32_t cooldownHours = 0; bool autoComplete = false;` (réutiliser l'enum shard ou un miroir éditeur — au choix de l'implémenteur, cohérent).
- **Parse** (`QuestEditIo.cpp`, après rewards) : `"repeat"` (string → enum, valider ∈ set), `"cooldownHours"` (uint, valider `> 0` si `repeat==cooldown`), `"autoComplete"` (bool). Tous optionnels (défaut None/0/false) → rétro-compat.
- **Sérialisation** : émettre `"repeat"`, `"cooldownHours"`, `"autoComplete"` — JSON pur relisible par le shard.
- **Validation** : mode valide ; `cooldownHours > 0` requis si `cooldown` ; sinon pas de contrainte.
- **`QuestEditorPanel`** : `ImGui::Combo` pour le mode (5 entrées), `ImGui::DragInt` "Cooldown (h)" **affiché seulement si** mode==Cooldown, `ImGui::Checkbox` "Auto-complete". Plumbing Load/Save/Reset (miroir des scalaires rewardXp `254-257`). **Doc `///`** obligatoire sur toute fonction ajoutée (règle éditeur, CLAUDE.md).

## 6. Wire / client
- **Aucun changement de wire.** Le client voit le statut résultant via `QuestDelta` : une quête reset repasse `Offered` (ré-affichée), une autoComplete passe `Completed` directement. Pas de bump `kProtocolVersion`.
- **Hors périmètre** : badge client « quotidienne » / minuteur de cooldown affiché (nécessiterait du wire) → future EXT.

## 7. Tests
- **Shard** (`QuestRuntimeTests.cpp` + éventuel nouveau, cible existante) : `ShouldRepeatReset` (none/repeatable/daily borne/weekly borne lundi/cooldown écoulé vs non, avec `now` fixes) ; `ApplyRepeatResets` (Completed→Locked + delta, stepProgress remis à zéro) ; `ApplyEvent` autoComplete → `Completed` (pas `ReadyToTurnIn`) ; parse `repeat`/`cooldownHours`/`autoComplete` (+ rejet mode invalide / cooldown<=0).
- **Persistance** (`CharacterPersistence` tests) : round-trip `completedAtEpochMs` ; save v1 (sans `completed_at`) → charge avec 0 ; `format_version=2`.
- **Éditeur** : round-trip repeat/cooldown/autoComplete ; rejets validation.
- Tests **non-strippables** (Release `-DNDEBUG`) : `std::cerr`+compteur+`return 1`, sauf fichier `#undef NDEBUG`.

## 8. Déploiement
> **⚠️ REDÉPLOIEMENT SHARD requis** — `QuestRuntime` (parse/reset/autoComplete) + `CharacterPersistence` (format v2) + `ServerApp` (reward refactor, call-sites). **Client inchangé** (aucun wire). Éditeur = outillage. **Stackée sur EXT-1** : merger #933 (EXT-1) **avant** cette PR.

## 9. Definition of Done
- [ ] `QuestDefinition` (repeatMode/cooldownHours/autoComplete) + parse ; `QuestState.completedAtEpochMs`.
- [ ] `ShouldRepeatReset` + `ApplyRepeatResets` ; call-sites login + Talk (ordre avant SyncQuestStates).
- [ ] AutoComplete dans `ApplyEvent` + `GrantQuestReward` (refactor) réutilisé (turn-in + auto) + stamping `completedAtEpochMs`.
- [ ] Persistance format v2 (completed_at, compat v1).
- [ ] Éditeur repeat/cooldown/autoComplete + validation + doc `///`.
- [ ] Tests shard + persistance + éditeur, non-strippables. CI verte. Rapport : ⚠️ REDÉPLOIEMENT SHARD, client inchangé, stack sur EXT-1.
