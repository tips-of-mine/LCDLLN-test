# SERVER-CORE.10_BattleGround_framework_instances_score

> **Etat : PARTIEL** (verifie 2026-06-03)
> - Fait / preuves : src/shardd/battleground/BattleGroundQueue.h
> - Manque : BG abstract/score/manager absents
> - Resume : BG partiel (queue seule)

## Objectif

Mettre en place le **framework PvP instancié** côté shard LCDLLN,
inspiré de `src/game/BattleGround` server-core. Cinq piliers :

1. **Hiérarchie de classe** : `BattleGround` virtuelle + 1 classe par
   variante (`BattleGroundColosseum`, `BattleGroundCaptureFlag`, etc.).
   Pattern « moteur générique + contenu spécifique ».
2. **`BattleGroundQueue` séparé du `BattleGroundMgr`** : la file vit en
   dehors de l'instance, peut être interrogée sans avoir d'instance
   active. Utilisable pour LCDLLN même sans cross-realm.
3. **Score per-player via `BattleGroundScore`** sous-classé par BG
   (kills, flags, captures) — généralisation du concept "stats de
   match".
4. **`EventPlayerLoggedIn` / `EventPlayerLoggedOut`** sur l'instance
   pour gérer reconnexion en plein match — critique pour MMORPG.
5. **Reset automatique** des instances vides plutôt que persistance —
   simplifie la gestion mémoire shard-side.

C'est un **P2 shard**, pré-requis pour SERVER-CORE.08 (Arena/Colisée) et
tout futur PvP instancié.

## Dépendances

- M00.1 (build base)
- SERVER-CORE.19 (Maps — `BattleGroundMap` est une sous-classe de Map)
- SERVER-CORE.08 (Arena) — utilise BattleGround

## Livrables

### Côté shard (`engine/server/shard/battleground/`)

- `BattleGround.{h,cpp}` (abstract) :
  ```cpp
  class BattleGround {
  public:
    virtual void OnStart() = 0;
    virtual void OnTick(int32_t dtMs) = 0;
    virtual void OnEnd(BgWinner winner) = 0;
    virtual void OnPlayerJoin(Player&) = 0;
    virtual void OnPlayerLeave(Player&) = 0;
    virtual void OnPlayerLoggedIn(Player&) = 0;       // reconnexion
    virtual void OnPlayerLoggedOut(Player&) = 0;      // déconnexion temporaire
    virtual void OnPlayerKilled(Player& victim, Unit* killer) = 0;
    virtual void OnFlagCaptured(Player& capturer, FlagId flag) {}    // optional
    virtual BattleGroundScore CreateScore(Player&) = 0;
    BgState GetState() const;
  protected:
    BgState m_state;       // WaitingForPlayers, InProgress, Ended
    int64_t m_startTs;
    std::unordered_map<uint64_t, std::unique_ptr<BattleGroundScore>> m_scores;
  };
  ```
- `BattleGroundScore.{h,cpp}` — stats per-player ; sous-classé par BG.
- `BattleGroundQueue.{h,cpp}` :
  - `Enqueue(uint32_t accountId, BgType, GroupId? group)`
  - `Dequeue(...)` (cancel)
  - `std::optional<BgMatch> TryFormMatch()` — appelé périodiquement.
- `BattleGroundManager.{h,cpp}` :
  - `BattleGround* CreateInstance(BgType, BgMatchInfo)`
  - `void DestroyInstance(uint64_t instanceId)`
  - `BattleGround* GetInstance(uint64_t instanceId)`
- `BattleGroundColosseum.{h,cpp}` (premier impl) — sous-classe minimale
  pour le colisée LCDLLN.

### Configuration (`config.json`)

```json
"battleground": {
  "wait_for_players_timeout_sec": 120,
  "match_max_duration_sec": 1800,
  "reconnect_grace_sec": 60,
  "auto_kick_offline_at_match_end": true
}
```

### Tests

- `BattleGroundTests.cpp` — start → tick → end states.
- `BattleGroundQueueTests.cpp` — match formation 5v5 avec 10 joueurs en queue.
- `BattleGroundScoreTests.cpp` — sous-classe avec stats spécifiques (kills + flags).
- `BattleGroundReconnectTests.cpp` — joueur logout/login pendant match → reprend sa place.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. State machine BG

```
WaitingForPlayers (timeout 2min) → si min_players atteint → InProgress
InProgress (timeout 30min) → OnEnd(timeout) ou OnEnd(team_winner)
Ended → 1 minute pour récupérer rewards → instance détruite
```

### 2. Score sous-classé

```cpp
struct ColosseumScore : public BattleGroundScore {
  int kills = 0;
  int deaths = 0;
  int damageDealt = 0;
  int healingDone = 0;
};

struct CaptureFlagScore : public BattleGroundScore {
  int flagsCaptured = 0;
  int flagsReturned = 0;
  int kills = 0;
};
```

Le BG sous-classé override `CreateScore(player)` pour retourner le
bon type.

### 3. Reconnexion

```cpp
void BattleGroundColosseum::OnPlayerLoggedOut(Player& p) {
  // Ne pas remove du BG immédiatement.
  // Marquer "absent", démarrer timer reconnect_grace_sec.
  m_absentPlayers[p.GetId()] = now() + reconnectGraceSec * 1000;
}

void BattleGroundColosseum::OnPlayerLoggedIn(Player& p) {
  if (m_absentPlayers.contains(p.GetId())) {
    m_absentPlayers.erase(p.GetId());
    p.TeleportTo(m_spawnPosition);    // re-place dans la zone
  }
}

void BattleGroundColosseum::OnTick(int32_t dtMs) {
  for (auto& [id, deadline] : m_absentPlayers) {
    if (now() > deadline) {
      // Considérer le joueur définitivement parti
      RemoveFromBg(id);
    }
  }
}
```

### 4. BattleGroundQueue séparée

Pas couplée à une instance — vit dans un `BattleGroundManager`. Chaque
tick, `TryFormMatch()` regarde si min_players * 2 (les 2 équipes) sont
en queue → forme un match → crée une instance.

Pour LCDLLN colisée local : queue trivialement résolue par le portier
PNJ (SERVER-CORE.08).

## Étapes d'implémentation

1. Créer `engine/server/shard/battleground/`.
2. Implémenter `BattleGround` abstract.
3. Implémenter `BattleGroundScore` base + une sous-classe.
4. Implémenter `BattleGroundQueue` (file FIFO simple).
5. Implémenter `BattleGroundManager` (create/destroy instances).
6. Implémenter `BattleGroundColosseum` (premier impl concret).
7. Implémenter reconnect/logout handling.
8. Tests : 4 fichiers.
9. Doc : section « BattleGround framework » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard)
- [ ] Tests passent
- [ ] Smoke test : 5 vs 5 entrent en colisée, match start, kills tracked, end → score visible
- [ ] Reconnect : player logout pendant match → login dans 60s → reprend sa place
- [ ] Logout > 60s → kick définitif
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **State machine strict** : transitions `WaitingForPlayers → InProgress → Ended` uniquement. Pas de retour en arrière. Évite bugs.
- **Score persistance** : sauvegarder en DB pour les matches arène (SERVER-CORE.08), volatile pour les autres.
- **min_players_per_team** : exposer en config par BgType. Colisée 2v2 = 2 par équipe ; futur grand BG = 10+.
- **Joueur quitte volontairement** : « `LeaveBg` » → désertion = forfait + pénalité (debuff "Deserter" 15min). Anti-leave abuse.
- **Match crée et personne ne join** : timeout `wait_for_players_timeout_sec` → cancel + retour en queue.
- **Cross-shard BG** : si LCDLLN scale et veut une queue cross-shard, le `BattleGroundManager` devient master-side avec instances créées sur les shards. Pas pour le MVP.
- **`std::unique_ptr<BattleGroundScore>`** : permet sous-classes virtuelles. Coût acceptable (pointer indirection) car peu d'updates de scores par tick.

## Références

- `SERVER-CORE_ANALYSIS.md` § BattleGround (P2 shard)
- server-core `src/game/BattleGround/BattleGround.cpp`, `BattleGroundMgr.cpp`,
  `BattleGroundQueue.cpp`, `BattleGroundWS.cpp` (Warsong example)
