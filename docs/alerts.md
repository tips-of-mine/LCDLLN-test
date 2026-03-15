# Alert conditions — Master / Shard (M23.3)

Ce document définit les conditions d’alerte pour incidents critiques et les hooks logs côté serveur. Utilisable pour configurer Prometheus Alertmanager et la surveillance du cluster.

---

## 1. Shard heartbeat timeout

**Description :** Un shard enregistré ne renvoie plus de heartbeat dans le délai configuré (`shard.heartbeat_timeout_sec`, défaut 90 s). Le Master le marque offline.

**Côté serveur (hooks logs) :**
- Lorsqu’un shard est marqué down par le watchdog :  
  `[ServerMain] Shard down event: shard_id=<id>`  
  (déjà émis par `ShardRegistry::SetShardDownCallback`.)

**Prometheus / Alertmanager :**
- Métrique : `shards_online` (gauge).
- Règle possible : alerter si `shards_online` passe sous un seuil attendu (ex. 0 alors qu’au moins un shard est attendu) pendant une durée donnée.
- Exemple de règle (à adapter au contexte) :
  ```yaml
  - alert: ShardHeartbeatTimeout
    expr: shards_online == 0
    for: 2m
    annotations:
      summary: "Aucun shard online (heartbeat timeout ou arrêt)."
  ```

---

## 2. DB down

**Description :** La base de données est inaccessible (connexion ou ping échoue). Le Master ne peut plus servir auth/sessions/migrations.

**Côté serveur (hooks logs) :**
- L’endpoint `/readyz` retourne **503** quand la DB est inaccessible (readiness = DB ping via `ConnectionPool::Acquire`).
- Un log de transition **not-ready → ready** est émis par le health endpoint lors du retour de la DB.

**Prometheus / Alertmanager :**
- Utiliser un **probe** sur `/readyz` : alerte si le probe renvoie 503 (ou timeout).
- Exemple (Prometheus probe ou Blackbox) :
  - Target : `http://<master>:<server.health.port>/readyz`
  - Alerte si : `probe_success == 0` ou `probe_http_status_code != 200` pendant 1–2 min.

---

## 3. High auth failures rate

**Description :** Taux élevé d’échecs d’authentification (bruteforce, fuite de creds, ou problème applicatif).

**Côté serveur (hooks logs) :**
- Les échecs sont comptés dans la métrique `auth_fail_total` (M23.2).
- Les logs existants par tentative (LOG_WARN / audit) restent ; le résumé périodique (toutes les 60 s) inclut `auth_fail_total` pour visibilité.

**Prometheus / Alertmanager :**
- Métrique : `auth_fail_total` (counter).
- Règle : alerter si le **taux** d’échecs dépasse un seuil sur une fenêtre glissante.
- Exemple :
  ```yaml
  - alert: HighAuthFailureRate
    expr: rate(auth_fail_total[5m]) > 0.5
    for: 2m
    annotations:
      summary: "Taux d'échecs auth élevé (> 0.5/s sur 5 min)."
  ```
- Ajuster le seuil (0.5, 1, …) selon la charge attendue.

---

## 4. High disconnect rate

**Description :** Taux élevé de déconnexions (instabilité réseau, surcharge, ou erreurs côté serveur).

**Côté serveur (hooks logs) :**
- Le résumé périodique (60 s) inclut les compteurs réseau (connexions actives, etc.) pour corrélation.
- Les déconnexions sont déjà loguées au besoin (heartbeat timeout, etc.). Pour un compteur global côté Prometheus, exposer un `disconnect_total` (ou équivalent) dans une évolution ultérieure permet d’appliquer une règle de taux.

**Prometheus / Alertmanager :**
- Option 1 (si métrique `disconnect_total` existe) :  
  `rate(disconnect_total[5m]) > <seuil>`
- Option 2 : alerter sur chute forte de `connections_active` ou sur probe de disponibilité du service.
- Exemple conceptuel :
  ```yaml
  - alert: HighDisconnectRate
    expr: rate(disconnect_total[5m]) > 2
    for: 2m
    annotations:
      summary: "Taux de déconnexions élevé."
  ```
- À brancher lorsque la métrique appropriée est exposée (ex. agrégat des `disconnectByReason` côté Master).

---

## Logs résumés périodiques (état cluster)

Toutes les **60 secondes**, le Master émet un log résumé contenant :
- `connections_active`
- `sessions_active`
- `shards_online`
- `auth_success_total` / `auth_fail_total`
- état DB (ready ou not ready)

Préfixe log : `[ServerMain] cluster summary`. Throttled à 60 s pour ne pas spammer.

Ces lignes permettent de vérifier l’état du cluster en un coup d’œil et de croiser avec les alertes Prometheus/Alertmanager.
