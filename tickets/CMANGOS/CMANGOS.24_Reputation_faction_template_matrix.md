# CMANGOS.24_Reputation_faction_template_matrix

## Objectif

Mettre en place le **système de réputation joueur ↔ factions** côté
shard LCDLLN, inspiré de `src/game/Reputation` cmangos. Cinq piliers :

1. **Faction template DB** : matrice (faction_a × faction_b) →
   `at_war` / `can_attack` / `can_assist`. Lookup O(1). Élégant pour
   gérer hostilités sans coder par paire.
2. **Bitmask flags par faction** : `AT_WAR`, `VISIBLE`, `INACTIVE`,
   `HIDDEN` cohabitent sur un seul `uint8`.
3. **Paliers calculés, pas stockés** : seul le « rep total » est
   persisté ; le rang (Hostile/Unfriendly/Neutral/Friendly/Honored/
   Revered/Exalted) est dérivé via `ReputationToRank()`. Évite désync
   DB.
4. **Spillover via faction parent** : gain dans une faction enfant
   remonte au parent via `ParentFactionId` + ratio.
5. **Réplication delta** : seules les factions modifiées sont envoyées
   au client, pas la table entière.

C'est un **P2 shard** avec sync master pour persistance, à activer
quand on a des factions PvE/PvP.

## Dépendances

- M00.1 (build base)
- CMANGOS.13 (Database — SQLStorage)
- CMANGOS.02 (Entities — Unit a `factionId`)
- CMANGOS.11 (Combat — `CanAttack(other)` consulte les flags)

## Livrables

### Côté shard (`engine/server/shard/reputation/`)

- `Faction.h` — struct row de faction :
  ```cpp
  struct Faction {
    uint32_t factionId;
    std::string name;
    int32_t parentFactionId;        // 0 = root
    float parentSpilloverRatio;     // ex. 0.5 = 50% du gain transmis
    int32_t reputationCap;          // ex. Exalted = +42999
    int32_t reputationFloor;        // ex. Hated = -42000
  };
  ```
- `FactionTemplate.h` — relations faction × faction :
  ```cpp
  struct FactionTemplate {
    uint32_t templateId;
    uint32_t factionId;
    uint32_t friendsFactions[4];     // 0 = unused
    uint32_t enemiesFactions[4];
    uint8_t  flags;                   // AT_WAR | etc.
  };
  ```
- `ReputationManager.{h,cpp}` :
  - `Load(ConnectionPool&)` — charge factions + templates.
  - `bool CanAttack(Unit const& a, Unit const& b)` — consulte templates.
  - `bool CanAssist(Unit const& a, Unit const& b)`
- `PlayerReputation.{h,cpp}` — état per-player :
  ```cpp
  class PlayerReputation {
  public:
    int32_t GetReputation(uint32_t factionId) const;
    Rank GetRank(uint32_t factionId) const;
    void GainReputation(uint32_t factionId, int32_t amount, bool spillover = true);
    bool IsAtWar(uint32_t factionId) const;
    void SetFlag(uint32_t factionId, ReputationFlag flag, bool value);
    std::vector<ReputationDelta> CollectChanged() const;    // pour réplication delta
  private:
    std::unordered_map<uint32_t, FactionState> m_states;
    std::unordered_map<uint32_t, FactionState> m_dirty;
  };
  ```
- `ReputationToRank.h` — fonction pure de conversion :
  ```cpp
  enum class Rank : uint8 { Hated, Hostile, Unfriendly, Neutral, Friendly, Honored, Revered, Exalted };
  Rank ReputationToRank(int32_t rep);
  ```

### Migration DB

```sql
CREATE TABLE faction (
  faction_id              INT UNSIGNED NOT NULL,
  name                    VARCHAR(64) NOT NULL,
  parent_faction_id       INT UNSIGNED NOT NULL DEFAULT 0,
  parent_spillover_ratio  FLOAT NOT NULL DEFAULT 0,
  cap                     INT NOT NULL DEFAULT 42999,
  floor                   INT NOT NULL DEFAULT -42000,
  PRIMARY KEY (faction_id)
);

CREATE TABLE faction_template (
  template_id     INT UNSIGNED NOT NULL,
  faction_id      INT UNSIGNED NOT NULL,
  friend_1        INT UNSIGNED NOT NULL DEFAULT 0,
  friend_2        INT UNSIGNED NOT NULL DEFAULT 0,
  friend_3        INT UNSIGNED NOT NULL DEFAULT 0,
  friend_4        INT UNSIGNED NOT NULL DEFAULT 0,
  enemy_1         INT UNSIGNED NOT NULL DEFAULT 0,
  enemy_2         INT UNSIGNED NOT NULL DEFAULT 0,
  enemy_3         INT UNSIGNED NOT NULL DEFAULT 0,
  enemy_4         INT UNSIGNED NOT NULL DEFAULT 0,
  flags           TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (template_id)
);

CREATE TABLE character_reputation (
  character_id    BIGINT UNSIGNED NOT NULL,
  faction_id      INT UNSIGNED NOT NULL,
  reputation      INT NOT NULL DEFAULT 0,
  flags           TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (character_id, faction_id)
);
```

### Configuration (`config.json`)

```json
"reputation": {
  "spillover_enabled": true,
  "rep_event_log": false,
  "default_starting_rep": 0
}
```

### Tests

- `ReputationManagerTests.cpp` — `CanAttack` consulte la matrice.
- `PlayerReputationTests.cpp` — `GainReputation(+100)` met à jour rank dynamiquement.
- `RanksTests.cpp` — `ReputationToRank` retourne le bon rank pour les 7 paliers.
- `SpilloverTests.cpp` — gain dans faction enfant transmet `× ratio` au parent.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. CanAttack via FactionTemplate

```cpp
bool ReputationManager::CanAttack(Unit const& a, Unit const& b) const {
  auto* tplA = GetTemplate(a.GetFactionTemplateId());
  auto* tplB = GetTemplate(b.GetFactionTemplateId());
  // Si A déclare B comme ennemi (dans enemies_*) → true
  for (auto enemyId : tplA->enemies) {
    if (b.GetFactionId() == enemyId) return true;
  }
  // Sinon : flag AT_WAR (joueur a explicitement déclaré la guerre)
  if (a.IsPlayer() && a.AsPlayer()->Reputation().IsAtWar(b.GetFactionId())) return true;
  return false;
}
```

### 2. Paliers calculés

```cpp
Rank ReputationToRank(int32_t rep) {
  if (rep <= -42000) return Rank::Hated;
  if (rep <= -6000)  return Rank::Hostile;
  if (rep <= -3000)  return Rank::Unfriendly;
  if (rep < 3000)    return Rank::Neutral;
  if (rep < 9000)    return Rank::Friendly;
  if (rep < 21000)   return Rank::Honored;
  if (rep < 42000)   return Rank::Revered;
  return Rank::Exalted;
}
```

Ne **jamais** stocker le rank en DB. Recompute on read.

### 3. Spillover

```cpp
void GainReputation(uint32_t factionId, int32_t amount, bool spillover) {
  m_states[factionId].rep += amount;
  m_dirty[factionId] = m_states[factionId];

  if (spillover) {
    auto* faction = g_reputationMgr.GetFaction(factionId);
    if (faction->parentFactionId != 0) {
      int32_t spilloverAmount = amount * faction->parentSpilloverRatio;
      GainReputation(faction->parentFactionId, spilloverAmount, true);  // récursif
    }
  }
}
```

### 4. Réplication delta

Au tick (ou à chaque action significant), envoyer au client uniquement
`m_dirty` (les changements depuis le dernier sync). Reset après envoi.

```cpp
auto changed = playerRep.CollectChanged();
if (!changed.empty()) {
  Send(kOpcodeReputationUpdate, changed);
}
```

## Étapes d'implémentation

1. Créer `engine/server/shard/reputation/`.
2. Migrations DB (3 tables).
3. Implémenter `Faction` + `FactionTemplate` + `ReputationManager::Load`.
4. Implémenter `CanAttack` / `CanAssist`.
5. Implémenter `PlayerReputation` (per-player).
6. Implémenter `ReputationToRank`.
7. Implémenter spillover.
8. Implémenter réplication delta + opcode `kOpcodeReputationUpdate`.
9. Tests : 4 fichiers.
10. Doc : section « Reputation shard » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard)
- [ ] Tests passent
- [ ] Smoke test : kill PNJ faction X → rep +50 → rank Friendly
- [ ] Spillover : gain faction "Stormwind" → parent "Alliance" gagne 50%
- [ ] CanAttack : 2 unit factions ennemies → true
- [ ] Réplication delta : seul le delta envoyé, pas la table complète
- [ ] Migrations idempotentes
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Pas de stockage du rank** : si un patch rééquilibre les paliers, tous les joueurs voient leur rank changer instantanément (car derived). Stocker le rank casse ça.
- **Spillover récursif** : si parent → grandparent → ... → boucle (mauvaise data). Limiter récursion à 5 niveaux.
- **Cap reputation** : ne **jamais** dépasser `cap`. Clamp avant écriture.
- **Réplication initiale** : au login, envoyer **toutes** les factions visibles (pas juste delta). Le delta arrive après.
- **Faction Hidden** : `Hidden` flag = pas affiché côté client. Encore en RAM/DB, mais le client ne reçoit pas les updates.
- **CanAttack et flag AT_WAR** : le joueur peut déclarer guerre à une faction Friendly (cf. WoW). Le AT_WAR override la matrice.
- **Performance CanAttack** : appelé à chaque dégât / sort hostile. **Pas de virtual call**, retourner bool inlinable. Cache template pointers.

## Références

- `CMANGOS_ANALYSIS.md` § Reputation (P2 shard)
- cmangos `src/game/Reputation/ReputationMgr.cpp`,
  `Faction.h`, `Player.cpp` (intégration)
