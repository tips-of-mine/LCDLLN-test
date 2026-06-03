# SERVER-CORE.08_Arena_team_mmr_weekly_colisee

## Objectif

Mettre en place le **système d'arène** côté master+shard LCDLLN, adapté
de `src/game/Arena` server-core pour le concept LCDLLN spécifique :
**colisée dans une ville**, pas de file d'attente cross-realm. Quatre
piliers :

1. **Séparation `ArenaTeam` (entité persistante) / `BattleGround`
   (instance de match)** : l'équipe survit aux matches, le match est
   jeté. Applicable au colisée local LCDLLN.
2. **MMR + rating glicko-like par équipe** stocké en DB, recalculé
   après chaque match — réutilisable tel quel pour le ladder du
   colisée.
3. **`ArenaTeamHandler` traite les opcodes spécifiques** (invite, kick,
   disband, query stats) séparément de la logique métier.
4. **Distribution hebdo via `WeeklyMaintenance`** : les points sont
   calculés en cron serveur, pas à chaque match — pattern propre pour
   les récompenses différées.

C'est un **P2 master+shard**, pré-requis pour ton concept de colisée.

## Dépendances

- M00.1 (build base)
- SERVER-CORE.06 (Accounts)
- SERVER-CORE.10 (BattleGround) — instance de match
- SERVER-CORE.13 (Database)
- SERVER-CORE.18 (Mails) — distribution récompenses

## Livrables

### Côté master (`engine/server/arena/`)

- `ArenaTeam.{h,cpp}` :
  ```cpp
  class ArenaTeam {
  public:
    bool AddMember(uint32_t accountId, uint64_t charId);
    bool RemoveMember(uint32_t accountId);
    void Disband();
    void OnMatchResult(bool win, int32_t teamMmrDelta);
    int32_t GetMMR() const;
    int32_t GetRating() const;     // visible vs invisible MMR
    int32_t GetWeeklyPoints() const;
  private:
    uint64_t m_teamId;
    ArenaTeamSize m_size;          // 2v2, 3v3, 5v5
    std::string m_name;
    uint32_t m_captainAccountId;
    std::vector<ArenaTeamMember> m_members;
    int32_t m_mmr = 1500;          // hidden
    int32_t m_rating = 0;          // visible
    int32_t m_weeklyPoints = 0;
    int m_seasonGames = 0;
    int m_seasonWins = 0;
  };
  ```
- `ArenaTeamManager.{h,cpp}` :
  - `Load(ConnectionPool&)`
  - `ArenaTeam* CreateTeam(captainId, size, name)`
  - `ArenaTeam* GetTeamForAccount(accountId, size)` — un account peut être dans 3 équipes (1 par taille)
- `ArenaTeamHandler.{h,cpp}` — opcodes :
  - `kOpcodeArenaTeamCreate`, `kOpcodeArenaTeamInvite`, etc.
  - `kOpcodeArenaTeamQueryStats`
  - `kOpcodeArenaTeamDisband`
- `ArenaMmrEngine.{h,cpp}` — calcul Glicko-2 ou Elo simplifié :
  - `int32_t ComputeMmrDelta(int32_t teamMmr, int32_t opponentMmr, bool win)`
  - `int32_t ComputeRatingDelta(int32_t mmr, int32_t rating, bool win)`
- `WeeklyMaintenance.{h,cpp}` — cron weekly :
  - `Run()` — distribue les arena points aux teams selon `weeklyPoints`, livre via mail.

### Côté shard (`engine/server/shard/colosseum/`) — pour le colisée local

- `ColosseumArena.{h,cpp}` (sous-classe de `BattleGround`, SERVER-CORE.10) :
  spawn point, zone bornée, 2v2/3v3/5v5, win conditions (last team standing OU score).
- `ColosseumEntrance.{h,cpp}` — interaction PNJ portier dans la ville :
  vérifie team formed + opposing team présente → démarre match.

### Migration DB

```sql
CREATE TABLE arena_team (
  team_id           BIGINT UNSIGNED NOT NULL,
  size              TINYINT UNSIGNED NOT NULL,    -- 2, 3, 5
  name              VARCHAR(64) NOT NULL UNIQUE,
  captain_account_id INT UNSIGNED NOT NULL,
  mmr               INT NOT NULL DEFAULT 1500,
  rating            INT NOT NULL DEFAULT 0,
  weekly_points     INT NOT NULL DEFAULT 0,
  season_games      INT NOT NULL DEFAULT 0,
  season_wins       INT NOT NULL DEFAULT 0,
  PRIMARY KEY (team_id)
);

CREATE TABLE arena_team_member (
  team_id           BIGINT UNSIGNED NOT NULL,
  account_id        INT UNSIGNED NOT NULL,
  character_id      BIGINT UNSIGNED NOT NULL,
  joined_ts         BIGINT NOT NULL,
  personal_rating   INT NOT NULL DEFAULT 0,
  PRIMARY KEY (team_id, account_id)
);

CREATE TABLE arena_match_log (
  match_id          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  team_a_id         BIGINT UNSIGNED NOT NULL,
  team_b_id         BIGINT UNSIGNED NOT NULL,
  winner_team_id    BIGINT UNSIGNED,
  duration_sec      INT UNSIGNED NOT NULL,
  team_a_mmr_before INT NOT NULL,
  team_b_mmr_before INT NOT NULL,
  team_a_mmr_delta  INT NOT NULL,
  team_b_mmr_delta  INT NOT NULL,
  ts                BIGINT NOT NULL,
  PRIMARY KEY (match_id)
);
```

### Configuration (`config.json`)

```json
"arena": {
  "starting_mmr": 1500,
  "starting_rating": 0,
  "match_durations_sec": 600,
  "weekly_points_per_win": 10,
  "weekly_maintenance_day": "Tuesday",
  "weekly_maintenance_hour_utc": 7,
  "colosseum_zone_id": 1234,
  "max_teams_per_account_per_size": 1
}
```

### Tests

- `ArenaTeamTests.cpp` — create + add + kick + disband.
- `ArenaMmrTests.cpp` — Elo : équipe 1500 bat équipe 1500 → +K, perd → -K.
- `WeeklyMaintenanceTests.cpp` — distribution points proportionnelle aux victoires.
- `ColosseumArenaTests.cpp` — spawn 2 teams, last team standing wins.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. MMR vs Rating

- **MMR (matchmaking)** : invisible, utilisé pour matchmaking.
- **Rating** : visible joueur. Diverge du MMR pour incentiver les
  joueurs sous-classés à jouer (gain rating > gain MMR si on gagne
  contre + fort).

### 2. Spécificité LCDLLN — pas de queue cross-realm

server-core utilise un `BattleGroundQueue` pour matchmaker à travers tous
les joueurs online. LCDLLN simplifié :

- Joueur A et son équipe entrent dans le **colisée** (zone physique
  d'une ville).
- Le portier (PNJ) propose : « il y a une équipe en attente, lancer le
  match ? »
- Si oui → instance de `ColosseumArena` créée sur le shard, transfert
  des 2 équipes dedans.

Pas de queue, pas de matchmaking à grande échelle. Plus simple, plus
local, RP-friendly.

### 3. Distribution récompenses

À la fin d'un match :
- Update `weekly_points` côté DB (juste un += 10 si victoire).
- Pas de gold immédiat — c'est `WeeklyMaintenance` qui livre les
  récompenses.

`WeeklyMaintenance` (cron) :
- Calcule pour chaque team : `points_to_award = weeklyPoints × multiplier`.
- Envoie à chaque membre actif via Mail (SERVER-CORE.18) : « Récompense
  hebdomadaire arène : 100 jetons ».
- Reset `weekly_points = 0`.

### 4. Cohérence avec BattleGround (SERVER-CORE.10)

`ColosseumArena` hérite de `BattleGround` (framework générique). Les
spécificités arène : zone bornée, win condition "last team standing",
durée max 10 min sinon match nul.

## Étapes d'implémentation

1. Créer `engine/server/arena/` (master) et `engine/server/shard/colosseum/` (shard).
2. Migrations DB.
3. Implémenter `ArenaTeam` + `ArenaTeamManager`.
4. Implémenter `ArenaMmrEngine` (Glicko-2 ou Elo simplifié).
5. Implémenter `ArenaTeamHandler` + opcodes.
6. Implémenter `WeeklyMaintenance` (cron).
7. Implémenter `ColosseumArena` (sous-classe de BG).
8. Implémenter `ColosseumEntrance` (PNJ portier).
9. Tests : 4 fichiers.
10. Doc : section « Arena (colisée LCDLLN) » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (master + shard)
- [ ] Tests passent
- [ ] Smoke test : team A 2v2 entre colisée, match contre team B, victoire → MMR/rating updated, weekly_points +10
- [ ] Cron WeeklyMaintenance distribue récompenses via mail
- [ ] Migrations idempotentes
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Personal rating** : par membre, pas seulement par team. Permet "tu as joué que 5 matches → ton rating personnel restreint à 80% du team rating" (anti-carry).
- **Disband** : team avec membres actifs → demander confirmation captaine ; mark disbanded mais conserver match_log pour l'historique.
- **Match nul** : timer 10min expire avec les 2 équipes vivantes → MMR ne change pas (ou change peu). Évite stalling.
- **Pas de queue automatique** : LCDLLN spec ; si un jour tu veux ajouter cross-realm, c'est un autre ticket.
- **Replay** : le `arena_match_log` permet de stocker le replay (paquets PacketLog → SERVER-CORE.12 Server) pour rejouer les matches en spectateur. Reportable.
- **Spectator mode** : pas dans ce ticket, mais le design doit permettre l'ajout (laisser la zone du colisée publique en read-only).

## Références

- `SERVER-CORE_ANALYSIS.md` § Arena (P2 master+shard)
- server-core `src/game/Arena/ArenaTeam.cpp`, `ArenaTeamHandler.cpp`,
  `ArenaTeamMgr.cpp`
- Glicko-2 : http://www.glicko.net/glicko/glicko2.pdf
