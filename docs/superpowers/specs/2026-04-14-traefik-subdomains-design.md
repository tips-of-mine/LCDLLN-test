# Design : Exposition des services via Traefik + sous-domaines tips-of-mine.com

**Date :** 2026-04-14
**Statut :** Approuvé

---

## Contexte

Le serveur LCDLLN tourne sur un hôte (`10.0.4.133`) qui héberge également d'autres applications exposées via Traefik + tunnel Cloudflare. Actuellement :

- `web-portal` et `phpmyadmin` sont déjà correctement exposés via Traefik avec leurs sous-domaines.
- `master` a `traefik.enable=false` et ses ports sont publiés directement sur l'hôte.
- Le client C++ référence en dur l'IP `10.0.4.133` dans `external/external_links.json` et `engine/core/DefaultClientEndpoints.h`.

## Objectif

- Exposer tous les services HTTP via Traefik avec un sous-domaine `*.tips-of-mine.com`.
- Remplacer les références à `10.0.4.133` dans le code client par des hostnames DNS.
- Ne pas modifier la config statique de Traefik sur l'hôte.

---

## Architecture retenue (Option A — Hybride)

### Flux réseau

```
# TCP jeu (client C++)
Client → DNS Cloudflare (nuage gris, DNS-only) → IP publique → Router/Firewall
       → port forward vers 10.0.4.133:3840 ou :3843 → container master/shard

# HTTP (status, web-portal, phpmyadmin)
Browser/Client → Cloudflare Tunnel (nuage orange) → Traefik → container (port interne)
```

### Tableau des services

| Service | Protocole | Publication hôte | Via Traefik | Sous-domaine |
|---|---|---|---|---|
| `master` TCP jeu | TCP brut | Port 3840 publié | Non | `lcdlln-master.tips-of-mine.com:3840` |
| `master` HTTP status | HTTP | Supprimé (interne) | Oui → HTTPS | `lcdlln-master-status.tips-of-mine.com` |
| `shard` TCP jeu | TCP brut | Port 3843 publié | Non | `lcdlln-shard.tips-of-mine.com:3843` |
| `web-portal` | HTTP | Interne | Oui → HTTPS | `lcdlln-portal.tips-of-mine.com` *(inchangé)* |
| `phpmyadmin` | HTTP | Interne | Oui → HTTPS | `lcdlln-phpmyadmin.tips-of-mine.com` *(inchangé)* |
| `mysql` | TCP | Localhost seulement | Non | *(inchangé)* |

### Justification du choix hybride

Le tunnel Cloudflare standard ne supporte que HTTP/HTTPS. Faire passer le TCP jeu via Traefik nécessiterait de modifier la config statique Traefik (ajout d'entrypoints 3840/3843) — complexité injustifiée. La solution hybride résout le vrai problème (remplacer l'IP hardcodée) sans surcoût.

---

## Changements à effectuer

### 1. `deploy/docker/docker-compose.yml` — service `master`

**Supprimer** la publication du port 3842 sur l'hôte :
```yaml
# Avant
- "${MASTER_PUBLISH_ADDR:-0.0.0.0}:${MASTER_HEALTH_PORT:-3842}:3842"

# Après : ligne supprimée (Traefik accède au port 3842 en interne)
```

**Remplacer** `traefik.enable=false` et son commentaire par les labels Traefik :
```yaml
labels:
  - "traefik.enable=true"
  - "traefik.docker.network=traefik_front_network"
  # HTTP
  - "traefik.http.routers.lcdlln-master-status-http.rule=Host(`lcdlln-master-status.tips-of-mine.com`)"
  - "traefik.http.routers.lcdlln-master-status-http.entrypoints=${TRAEFIK_ENTRYPOINT_HTTP:-http}"
  - "traefik.http.routers.lcdlln-master-status-http.service=lcdlln-master-status-service"
  # HTTPS
  - "traefik.http.routers.lcdlln-master-status-https.rule=Host(`lcdlln-master-status.tips-of-mine.com`)"
  - "traefik.http.routers.lcdlln-master-status-https.entrypoints=${TRAEFIK_ENTRYPOINT_HTTPS:-https}"
  - "traefik.http.routers.lcdlln-master-status-https.tls=true"
  - "traefik.http.routers.lcdlln-master-status-https.service=lcdlln-master-status-service"
  # Service
  - "traefik.http.services.lcdlln-master-status-service.loadbalancer.server.port=3842"
```

Le port TCP 3840 reste publié directement sur l'hôte (inchangé).

### 2. `external/external_links.json`

```json
{
  "status_api_url": "https://lcdlln-master-status.tips-of-mine.com/status",
  "master_tcp_host": "lcdlln-master.tips-of-mine.com",
  "master_https_host": "lcdlln-master-status.tips-of-mine.com",
  "master_embedded_http_origin": "https://lcdlln-master-status.tips-of-mine.com"
}
```

### 3. `engine/core/DefaultClientEndpoints.h` (ligne 8)

```cpp
// Avant
inline constexpr std::string_view kStatusApiUrl = "http://10.0.4.133:3842/status";

// Après
inline constexpr std::string_view kStatusApiUrl = "https://lcdlln-master-status.tips-of-mine.com/status";
```

### 4. `deploy/docker/.env.example`

Ajouter la documentation des sous-domaines :
```bash
# Sous-domaines Traefik / DNS
MASTER_STATUS_SUBDOMAIN=lcdlln-master-status.tips-of-mine.com
MASTER_TCP_SUBDOMAIN=lcdlln-master.tips-of-mine.com
SHARD_TCP_SUBDOMAIN=lcdlln-shard.tips-of-mine.com
```

### 5. `deploy/docker/README.md`

Mettre à jour la mention de `10.0.4.133` (ligne 94) pour refléter les sous-domaines.

### 6. `deploy/docker/traefik-master.labels.reference.yml`

Supprimer ce fichier de référence devenu caduque (son contenu sera intégré directement dans le docker-compose).

---

## Configuration DNS Cloudflare requise (hors code)

| Entrée DNS | Type | Proxied | Cible |
|---|---|---|---|
| `lcdlln-master.tips-of-mine.com` | A | Non (nuage gris) | IP publique |
| `lcdlln-master-status.tips-of-mine.com` | CNAME/A | Oui (nuage orange) | Tunnel Cloudflare |
| `lcdlln-shard.tips-of-mine.com` | A | Non (nuage gris) | IP publique |

## Configuration réseau requise (hors code)

- Router/firewall : forward port TCP 3840 → `10.0.4.133:3840`
- Router/firewall : forward port TCP 3843 → `10.0.4.133:3843`

---

## Ce qui ne change pas

- Services `web-portal` et `phpmyadmin` : aucune modification
- Service `mysql` : aucune modification
- Config statique Traefik sur l'hôte : aucune modification
- Port TCP 3840 (master) et 3843 (shard) : toujours publiés directement
