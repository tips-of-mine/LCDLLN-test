# CMANGOS.36_OutdoorPvP_zone_plugins_objectives

## Objectif

Mettre en place un framework **PvP en zones ouvertes** côté shard
LCDLLN, inspiré de `src/game/OutdoorPvP` cmangos. Quatre piliers :

1. **Pattern Manager + plugin polymorphe par zone** : `OutdoorPvPMgr`
   enregistre des `OutdoorPvP*` polymorphes au démarrage et dispatch
   les events `OnPlayerEnterZone`, `OnGameObjectCreate`, `OnPlayerKill`.
2. **State machine par objectif** : chaque tour/banner = mini-FSM
   (neutre → capture en cours → capturé) avec timer et progression.
3. **WorldState broadcast** : compteurs de score diffusés via variables
   nommées (CMANGOS.28 World) à tous les clients de la zone.
4. **Découplage triggers/règles** : déclencheurs (entrée AreaTrigger
   géographique) séparés de la logique de scoring — data-driven via
   DB.

C'est un **P3 shard**. Pas pour le MVP, mais pattern réutilisable pour
des futurs events scriptés (festivals, invasions saisonnières).

## Dépendances

- M00.1 (build base)
- CMANGOS.28 (World) — WorldState broadcast
- CMANGOS.07 (AI) — réactions des PNJ aux changements
- CMANGOS.16 (Globals/Conditions)

## Livrables

### Côté shard (`engine/server/shard/outdoorpvp/`)

- `OutdoorPvP.{h,cpp}` (abstract) :
  ```cpp
  class OutdoorPvP {
  public:
    virtual ~OutdoorPvP() = default;
    virtual void OnPlayerEnterZone(Player&) {}
    virtual void OnPlayerLeaveZone(Player&) {}
    virtual void OnPlayerKilledByPlayer(Player& victim, Player& killer) {}
    virtual void OnGameObjectInteract(GameObject&, Player&) {}
    virtual void Update(int32_t dtMs) {}
    uint32_t GetZoneId() const;
    uint32_t GetMapId() const;
  };
  ```
- `OutdoorPvPMgr.{h,cpp}` :
  - `Register(std::unique_ptr<OutdoorPvP>)` — un par zone supportée.
  - `void OnPlayerEnterZone(Player&, uint32_t newZoneId)` — dispatch.
  - `void Update(int32_t dtMs)` — tick tous les zones actives.
- `OutdoorPvPObjective.{h,cpp}` (utilitaire) :
  ```cpp
  class OutdoorPvPObjective {
  public:
    enum class State { Neutral, Capturing, Captured };
    void OnPlayerProgressTick(Player&);
    State GetState() const;
    uint32_t GetWorldStateId() const;     // pour broadcast
  };
  ```

### Configuration (`config.json`)

```json
"outdoor_pvp": {
  "enabled": false,
  "capture_default_duration_sec": 60,
  "capture_radius_m": 30.0,
  "scoreboard_broadcast_interval_sec": 10
}
```

### Tests

- `OutdoorPvPMgrTests.cpp` — register + OnPlayerEnterZone dispatch.
- `OutdoorPvPObjectiveTests.cpp` — state machine Neutral → Capturing → Captured.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. Plugin par zone

```cpp
class HellfireOutdoorPvP : public OutdoorPvP {
public:
  HellfireOutdoorPvP() : OutdoorPvP(/*mapId*/100, /*zoneId*/200) {}
  void OnPlayerEnterZone(Player& p) override {
    BroadcastCurrentScore(p);
  }
  void OnPlayerKilledByPlayer(Player& v, Player& k) override {
    if (k.GetFaction() != v.GetFaction()) {
      m_score[k.GetFaction()] += 1;
      BroadcastScoreUpdate();
    }
  }
private:
  std::array<int32_t, 2> m_score = {};
};
```

### 2. State machine objectif

```
Neutral ----enemy faction enters----> Capturing
Capturing ----timer expires + faction holds----> Captured (faction X)
Capturing ----enemy faction enters----> Capturing (autre faction, reset timer)
Captured ----enemy faction starts capture----> Capturing
```

### 3. WorldState broadcast

Chaque objectif a un `worldStateId`. Quand state change :

```cpp
g_worldState.Set(myWsId, encodedState);
// Tous les clients dans la zone reçoivent automatiquement (CMANGOS.28).
```

UI client : la zone affiche les scores en haut écran (similaire BG).

## Étapes d'implémentation

1. Créer `engine/server/shard/outdoorpvp/`.
2. Implémenter `OutdoorPvP` interface.
3. Implémenter `OutdoorPvPMgr`.
4. Implémenter `OutdoorPvPObjective` (state machine).
5. Câbler `Player::OnZoneChange` → mgr dispatch.
6. Tests : 2 fichiers.
7. Doc : section « OutdoorPvP framework » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard)
- [ ] Tests passent
- [ ] Smoke test : un OutdoorPvP custom (test) enregistré, joueur entre zone → callback déclenché
- [ ] State machine objectif : 60s de capture continue → state = Captured + worldstate broadcast
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Pas pour le MVP** : ce ticket pose le **framework**. L'implémentation d'une zone PvP réelle vient plus tard, par module dédié.
- **Conflits avec OnPlayerKill** : un PvP zone hook + un BattleGround hook peuvent traiter le même kill. Documenter l'ordre de dispatch (OutdoorPvPMgr d'abord, BG ensuite, par exemple).
- **Performance** : le framework lui-même est gratuit (vide tant qu'aucune zone enregistrée). Activer module par module.

## Références

- `CMANGOS_ANALYSIS.md` § OutdoorPvP (P3 shard)
- cmangos `src/game/OutdoorPvP/OutdoorPvP.cpp`,
  `OutdoorPvPMgr.cpp`, exemples zones `OutdoorPvPHP.cpp` etc.
