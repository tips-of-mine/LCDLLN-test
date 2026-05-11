# CMANGOS.34 — Metric (InfluxDB / Measurement RAII)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.34_Metric_influxdb_measurement_raii.md](../../../../tickets/CMANGOS/CMANGOS.34_Metric_influxdb_measurement_raii.md)
> **Priorité** : P3 — ajout à valeur (observabilité prod)
> **Cible** : cross (master + shard)

## 1. Statut implémentation

❌ **Absent** — pas de collecteur métriques InfluxDB / Prometheus,
pas de `Measurement` RAII, pas de flush asynchrone batché.

## 2. Preuves dans le code

**Manquant :**
- ❌ `engine/core/metric/` ou équivalent
- ❌ Modèle InfluxDB line-protocol
- ❌ `Measurement` RAII auto-publish duration
- ❌ Flush async batché vers endpoint distant
- ❌ Config `metric.endpoint`, `metric.batch_size`, `metric.flush_period`

## 3. Recouvrement milestones existantes

❌ **Non couvert** — `LogFilter` PR #468 couvre le **logging** mais pas
les métriques structurées (durations, counters, gauges) consommables
par Grafana/Telegraf.

## 4. Écart par rapport à la spec CMANGOS

100% absent. Pas d'observabilité prod (latence handlers, taille queues,
ticks/sec, mémoire).

## 5. Effort estimé

**M** (2 PR) :
- PR 1 : `Measurement` RAII + line-protocol formatter + tests
- PR 2 : flush async batché + config + intégration handlers chauds

Pas wire-breaking, pas migration DB, pas dépendance externe (HTTP std lib
ou socket UDP).

## 6. Valeur joueur/serveur

**Moyenne (pré-launch) → Élevée (post-launch)** — invisible joueur,
critique pour ops. Sans métriques prod, debug à l'aveugle quand un
incident arrive.

## 7. Dépendances bloquantes

Aucune — peut être livré tôt comme outil ops.

## 8. Risque / piège ⚠️

- ⚠️ **Endpoint distant** — InfluxDB / Telegraf à provisionner côté
  infra. À coordonner avec les ops.
- ⚠️ **Volumétrie** — 1000 measurements/s × 100 chars = 100KB/s. Batch
  + compression OK.
- ⚠️ **Failure mode** — si endpoint tombe, ne pas bloquer le shard.
  Drop oldest + warning log.
- ⚠️ **PII / sécurité** — pas de PII dans les tags/fields (account
  email/IP). Convention claire.
- ⚠️ **Cardinality** — un tag `accountId` = explosion cardinalité
  InfluxDB. Tags = labels low-cardinality (handler name, status code).
  Fields = values.

## 9. Recommandation finale

🔧 **Adapter et faire**, **avant launch** — déblocant ops :

1. **Étape 0** : valider stack ops (InfluxDB ? Prometheus ? autre ?).
   InfluxDB cmangos pattern OK, Prometheus + line-protocol = autre
   approche.
2. **Étape 1** : `Measurement` RAII basique (durée scope) + tests.
3. **Étape 2** : flush async batché + intégration `LoginHandler`,
   `ChatRelayHandler`, `Tick` shard.

Effort raisonnable, ROI ops élevé pré-launch. Peut être livré tôt en
parallèle des autres P3.

---

*Audit du 2026-05-08. Mises à jour : —*
