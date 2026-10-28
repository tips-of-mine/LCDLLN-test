# FIX.6 — Bruit des logs serveur et avertissements de configuration

Origine : logs serveur du 28/10/2026 (anomalies 8 et 9 de l'analyse) — le log du shard
faisait 7,8 Mo pour 6 minutes, dont 56 716 lignes au seul démarrage.

| Message | Avant | Après |
|---|---|---|
| `[ServerApp] Spawner ready (…)` | INFO, une ligne par spawner | DEBUG ; résumé INFO unique : `Spawners prets : N spawner(s), M emplacement(s) de mob, K avec agenda` |
| `[ServerApp] Tick stats (…)` | INFO chaque seconde | INFO toutes les `server.tick_stats_log_interval_sec` (60 s par défaut, 0 = seulement sur changement) **et** dès que le nombre de clients ou le délestage change. Les métriques `/metrics` restent à 1 Hz. |
| `[ServerApp] Mob state changed (…)` | INFO | DEBUG |
| `[CellGrid] Entity removed (…)` | INFO | DEBUG |
| `WARNING: MYSQL_OPT_RECONNECT is deprecated` | à chaque connexion MySQL (master, shard, migrations) | supprimé : l'option était passée à `false`, qui est déjà le défaut de libmysqlclient ; la reconnexion reste assurée par le pool (`mysql_ping`) |
| `max_bandwidth_per_player missing` | WARN (master et shard) | clé `server.bandwidth.max_bandwidth_per_player = 3200` (Ko/s, valeur identique au défaut dérivé) dans `deploy/docker/config/*.config.json` ; repli journalisé en INFO |

Pour retrouver le détail : `"log": { "level": "debug" }` dans la configuration du serveur.

Nouvelle clé facultative (shard) : `server.tick_stats_log_interval_sec` (défaut 60).
