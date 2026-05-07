# CMANGOS.33_LFG_queue_role_matchmaking

## Objectif

Mettre en place un **système Looking-For-Group / matchmaking** côté
master LCDLLN, inspiré de `src/game/LFG` cmangos. Quatre piliers :

1. **`LFGQueue` séparée du `LFGMgr`** : la file (structure de données +
   algo de matching) est isolée du manager (état joueur, téléport,
   récompenses) — testable à part.
2. **Matching par rôles requis** : chaque slot d'un dungeon demande un
   set de rôles, l'algo cherche la plus petite combinaison qui remplit.
3. **State machine joueur** : Idle / Queued / Proposal / Boot / InDungeon
   — transitions explicites évitent les bugs de "joueur fantôme dans
   la file".
4. **Timeout proposals** : si un joueur ne confirme pas en N secondes,
   sa place est rendue à la file.

C'est un **P3 master**.

## Dépendances

- M00.1 (build base)
- CMANGOS.13 (Database)
- CMANGOS.15 (Groups — création de groupe au match formé)

## Livrables

### Côté master (`engine/server/lfg/`)

- `LFGRole.h` — enum `Tank, Heal, Dps, Filler` + flags (un joueur peut être multi-rôle).
- `LFGEntry.h` :
  ```cpp
  struct LFGEntry {
    uint32_t accountId;
    uint64_t characterId;
    LFGRoleFlags rolesAvailable;
    std::vector<uint32_t> dungeonsRequested;
    int64_t enqueuedTs;
  };
  ```
- `LFGQueue.{h,cpp}` :
  - `void Enqueue(LFGEntry)`
  - `void Dequeue(uint32_t accountId)`
  - `std::optional<LFGMatch> TryFormMatch(uint32_t dungeonId)` — algo bin-packing
- `LFGMgr.{h,cpp}` (state machine) :
  - `OnPlayerJoinQueue`, `OnPlayerLeaveQueue`
  - `OnMatchProposal(LFGMatch)` — envoie proposal aux 5 joueurs
  - `OnProposalAccepted(accountId)` ou `OnProposalDeclined`
  - `OnAllAccepted` → crée Group + tp dans dungeon
- `LFGHandler.{h,cpp}` — opcodes.

### Migration DB

```sql
CREATE TABLE lfg_dungeon (
  dungeon_id          INT UNSIGNED NOT NULL,
  name                VARCHAR(64) NOT NULL,
  required_tanks      TINYINT UNSIGNED NOT NULL DEFAULT 1,
  required_heals      TINYINT UNSIGNED NOT NULL DEFAULT 1,
  required_dps        TINYINT UNSIGNED NOT NULL DEFAULT 3,
  min_level           INT NOT NULL DEFAULT 1,
  max_level           INT NOT NULL DEFAULT 60,
  PRIMARY KEY (dungeon_id)
);
```

### Configuration (`config.json`)

```json
"lfg": {
  "proposal_timeout_sec": 60,
  "boot_grace_sec": 90,
  "match_check_interval_sec": 5
}
```

### Tests

- `LFGQueueTests.cpp` — match avec 5 joueurs (1T/1H/3D) → match formé.
- `LFGMgrStateMachineTests.cpp` — Queued → Proposal → si timeout → retour Queued.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Étapes d'implémentation

1. Créer `engine/server/lfg/`.
2. Migration DB.
3. Implémenter `LFGQueue` (algo matching).
4. Implémenter `LFGMgr` (state machine).
5. Implémenter handler + opcodes.
6. Tests : 2 fichiers.
7. Doc : section « LFG » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (master)
- [ ] Tests passent
- [ ] Smoke test : 5 joueurs en queue (rôles complémentaires) → proposal envoyée → tous acceptent → Group créé + tp dungeon
- [ ] Timeout proposal : 1 joueur ne répond pas → autres rendus à la queue
- [ ] Migrations idempotentes
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Algo bin-packing** : pas trivial à scaler à 1000s de joueurs en queue. Démarrer simple (greedy par rôle), optimiser plus tard.
- **Boot vote** : un joueur en groupe peut être kické par vote majoritaire. State machine `Boot` couvre ça.
- **Cross-shard** : oui, naturellement master-side. Une queue unique pour tous les shards.
- **Penalty leaver** : un joueur qui quitte un dungeon en cours = debuff "Deserter" 30 min. Géré côté master à la sortie inattendue.
- **Reactive matchmaking** : `match_check_interval_sec = 5` → délai max 5s entre rôle complète et proposition. Acceptable. Pas besoin du temps réel.

## Références

- `CMANGOS_ANALYSIS.md` § LFG (P3 master)
- cmangos `src/game/LFG/LFGMgr.cpp`, `LFGQueue.cpp`,
  `LFGHandler.cpp`
