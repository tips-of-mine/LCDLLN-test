# SERVER-CORE.34_Metric_influxdb_measurement_raii

## Objectif

Mettre en place un **collecteur de métriques** prod LCDLLN inspiré de
`src/shared/Metric` server-core. Trois piliers :

1. **Modèle InfluxDB line-protocol** (tags + fields + timestamp) :
   standard de facto, lisible par Grafana/Telegraf out-of-the-box.
2. **`Measurement` RAII** : `{ Measurement m("LoginHandler"); ... }` →
   durée poussée automatiquement à la sortie de scope. Pattern
   ergonomique pour profiler du code chaud sans intrusion.
3. **Flush asynchrone batché** : queue protégée par mutex, scheduled
   timer qui envoie en lot — évite N petits packets HTTP à chaque
   measurement.

C'est un **P3 cross master+shard**, observabilité prod (latence
handlers, taille queues, ticks/sec).

## Dépendances

- M00.1 (build base)
- Logger (PR #468) — pour debug si la connexion InfluxDB tombe

## Livrables

### Couche partagée (`engine/core/metric/`)

- `MetricPoint.h` :
  ```cpp
  struct MetricPoint {
    std::string measurement;        // "handler_latency"
    std::vector<std::pair<std::string, std::string>> tags;
    std::vector<std::pair<std::string, double>> fields;
    int64_t timestampNs;
  };
  ```
- `MetricCollector.{h,cpp}` :
  - `void Push(MetricPoint p)` — non bloquant (push dans queue).
  - `void Start(MetricConnectionInfo info)`
  - `void Stop()`
- `Measurement.h` (RAII) :
  ```cpp
  class Measurement {
  public:
    explicit Measurement(std::string_view name);
    Measurement& Tag(std::string_view key, std::string_view value);
    Measurement& Field(std::string_view key, double value);
    ~Measurement();
  private:
    MetricPoint m_point;
  };
  ```
  À la destruction, calcule la durée et push automatiquement.

### Configuration (`config.json`)

```json
"metric": {
  "enabled": false,
  "influxdb_host": "127.0.0.1",
  "influxdb_port": 8086,
  "influxdb_database": "lcdlln",
  "flush_interval_ms": 5000,
  "max_queue_size": 10000
}
```

### Tests

- `MeasurementTests.cpp` — sortie de scope produit un point avec durée.
- `MetricCollectorTests.cpp` — push 1000 points, flush bat 1 batch.
- `LineProtocolTests.cpp` — sérialise un point au format InfluxDB ligne.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. Format InfluxDB line-protocol

```
handler_latency,handler=Auth,server=master latency_ms=12.5 1715067600000000000
```

Format : `<measurement>,<tag1>=<v1>,<tag2>=<v2> <field1>=<v1>,<field2>=<v2> <timestamp_ns>`

### 2. Measurement usage

```cpp
void HandleAuthRequest(...) {
  Measurement m("auth_handler");
  m.Tag("type", "register");
  // ... travail ...
  m.Field("custom_metric", 42.0);
  // à la sortie : ajout automatique du field "duration_ns"
}
```

### 3. Flush async

Worker thread dédié qui dort `flush_interval_ms` ms, puis :
1. Swap la queue locale avec une nouvelle.
2. Sérialise tous les points en line-protocol.
3. POST HTTP vers `{host}:{port}/write?db={database}`.
4. En cas d'échec : retry 3× avec backoff, puis log error et drop.

## Étapes d'implémentation

1. Créer `engine/core/metric/`.
2. Implémenter `MetricPoint` + line-protocol serializer.
3. Implémenter `MetricCollector` (queue + flush thread).
4. Implémenter `Measurement` RAII.
5. Câbler dans 5-10 hot paths existants (auth handler, NetServer pump, DB query, Map tick).
6. Tests : 3 fichiers.
7. Doc : section « Metric » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK
- [ ] Tests passent
- [ ] Smoke test : InfluxDB local lancé, le flush envoie les points, Grafana les voit
- [ ] `metric.enabled = false` → pas d'overhead (push devient no-op)
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Disabled = no-op total** : si `metric.enabled = false`, `Measurement::~Measurement` ne fait absolument rien. Sinon overhead inutile.
- **Cardinality** : ne **jamais** mettre des values explosives en tag (ex. `accountId=...`). Tags sont indexés côté InfluxDB, cardinality élevée = explosion mémoire DB. Utiliser fields à la place pour les IDs.
- **Buffer overflow** : si la queue dépasse `max_queue_size`, drop les plus anciens (FIFO). Log warn.
- **Network failures** : InfluxDB indisponible → garder en queue, retry. Si la queue overflow, drop. Pas de blocage du process serveur pour cause de monitoring HS.

## Références

- `SERVER-CORE_ANALYSIS.md` § Metric (P3 cross)
- server-core `src/shared/Metric/Metric.cpp`
- InfluxDB line protocol : https://docs.influxdata.com/influxdb/v2/reference/syntax/line-protocol/
