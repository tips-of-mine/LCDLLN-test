# SERVER-CORE.42_Weather_markov_zones_authoritative

## Objectif

Mettre en place un **système météo par zone** côté shard LCDLLN,
inspiré de `src/game/Weather` server-core. Quatre piliers :

1. **Markov chain par zone** : table `game_weather` (zone, season, type,
   chance) → chaque tick rotation aléatoire pondérée. Très peu de code,
   beaucoup d'ambiance.
2. **Server-authoritative** : le serveur décide, broadcast à tous les
   clients de la zone → cohérence multi-joueurs ("regarde la pluie
   là-bas"). Crucial vs météo client-locale.
3. **Update lazy** : pas de tick haute fréquence, juste un timer 10min
   par zone occupée, désactivé si zone vide.
4. **Transition douce** : `grade` de 0.0 à 1.0 envoyé au client pour
   fade in/out, pas de cuts brutaux.

C'est un **P3 shard**.

## Dépendances

- M00.1 (build base)
- SERVER-CORE.13 (Database)
- SERVER-CORE.03 (Grids — connaitre quelles zones sont occupées)

## Livrables

### Côté shard (`engine/server/shard/weather/`)

- `WeatherType.h` — enum :
  ```cpp
  enum class WeatherType : uint8 {
    Fine,         // beau temps
    LightRain,
    HeavyRain,
    Snow,
    Storm,
    Fog,
    BlackSnow,    // contenu thématique LCDLLN
  };
  ```
- `WeatherTransition.h` :
  ```cpp
  struct WeatherTransition {
    uint32_t zoneId;
    WeatherType from;
    WeatherType to;
    float chance;
  };
  ```
- `WeatherMgr.{h,cpp}` :
  - `Load(ConnectionPool&)` — charge les transitions.
  - `Update(int64_t nowMs)` — pour chaque zone occupée, ticke (10min) et tire la prochaine météo.
  - `WeatherType GetCurrent(uint32_t zoneId)`
  - `void OnZoneEnter(Player&, uint32_t zoneId)` — push état actuel au joueur.

### Migration DB

```sql
CREATE TABLE game_weather (
  zone_id     INT UNSIGNED NOT NULL,
  season      TINYINT UNSIGNED NOT NULL DEFAULT 0,    -- 0=any, 1=spring, 2=summer, 3=autumn, 4=winter
  from_type   TINYINT UNSIGNED NOT NULL,
  to_type     TINYINT UNSIGNED NOT NULL,
  chance      FLOAT NOT NULL,
  PRIMARY KEY (zone_id, season, from_type, to_type)
);
```

### Configuration (`config.json`)

```json
"weather": {
  "enabled": true,
  "tick_interval_min": 10,
  "transition_grade_seconds": 30,
  "default_season": 0,
  "skip_unoccupied_zones": true
}
```

### Tests

- `WeatherMgrTests.cpp` — markov : 1000 tirages avec table simple → fréquences ~ chances config.
- `WeatherTransitionTests.cpp` — joueur entre zone X → reçoit weather actuel ; tick → broadcast nouveau weather.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. Markov chain

Pour chaque zone, transitions probabilistes :

```
zone Stormwind, summer:
  from Fine → to LightRain  (chance 30%)
  from Fine → to Fine       (chance 70%)
  from LightRain → to Fine  (chance 50%)
  from LightRain → to HeavyRain (chance 20%)
  from LightRain → to LightRain (chance 30%)
```

Tick : chaque 10min, on regarde l'état actuel + on tire selon
`game_weather` filtrée par `from_type`.

### 2. Server-authoritative broadcast

```cpp
// changement de weather dans zone X
auto* msg = BuildSMSGWeather(zoneId, newWeather, grade);
g_grids.MessageDistDeliverer(msg, /*for all players in zone*/);
```

### 3. Transition douce

À chaque changement, `grade` part de 0.0 et augmente vers 1.0 sur
`transition_grade_seconds`. Le client interpole l'effet visuel
(densité de pluie, opacité brouillard).

```cpp
// Tick :
float grade = std::min(1.0f, (now - transitionStartTs) / transitionGradeSec);
if (grade > lastSentGrade + 0.1f) {
  Broadcast(grade);   // pas trop de paquets
  lastSentGrade = grade;
}
```

## Étapes d'implémentation

1. Créer `engine/server/shard/weather/`.
2. Migration DB.
3. Implémenter `WeatherMgr::Load` + tick.
4. Allouer opcode `kOpcodeWeatherUpdate`.
5. Câbler `Player::OnZoneEnter` → push current state.
6. Implémenter transition douce.
7. Tests : 2 fichiers.
8. Doc : section « Weather shard » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard)
- [ ] Tests passent
- [ ] Smoke test : Stormwind avec table `Fine 70% / LightRain 30%` → après 100 ticks, ratio observé proche
- [ ] Joueur entre zone → reçoit weather actuel
- [ ] Transition de Fine → LightRain envoyée avec grade animé sur 30s
- [ ] Migrations idempotentes
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Zones inoccupées** : `skip_unoccupied_zones = true` → pas de tick si personne dedans. Le state est figé. À l'entrée d'un joueur, soit on accepte le state vieux, soit on retick une fois.
- **Cohérence cross-shard** : même zone sur 2 shards → météo différente possible. Pas grave (zones distinctes par instance map).
- **Pas trop de variétés** : 5-7 types de weather suffit. Au-delà, table complexe à équilibrer.
- **Saisons** : `season` permet du contenu saisonnier (neige en hiver). Si LCDLLN n'a pas de cycle saisonnier, ignorer (toujours `season=0`).

## Références

- `SERVER-CORE_ANALYSIS.md` § Weather (P3 shard)
- server-core `src/game/Weather/Weather.cpp`, `WeatherMgr.cpp`
