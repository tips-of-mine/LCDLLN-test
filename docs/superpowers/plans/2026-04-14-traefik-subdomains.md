# Traefik Subdomains — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Exposer tous les services HTTP du stack LCDLLN via Traefik avec des sous-domaines `*.tips-of-mine.com`, et remplacer toutes les références à l'IP `10.0.4.133` dans le code client par des hostnames DNS.

**Architecture:** Le service `master` passe de `traefik.enable=false` + ports publiés sur l'hôte à `traefik.enable=true` pour le port HTTP 3842 uniquement ; le port TCP 3840 reste publié directement. Les fichiers client (`external_links.json` et `DefaultClientEndpoints.h`) utilisent désormais les hostnames DNS à la place de l'IP hardcodée.

**Tech Stack:** Docker Compose labels Traefik, JSON, C++ header

---

## File Map

| Fichier | Action | Responsabilité |
|---|---|---|
| `deploy/docker/docker-compose.yml` | Modifier | Activer Traefik sur master (port 3842), supprimer publication port 3842 sur l'hôte |
| `external/external_links.json` | Modifier | Remplacer 10.0.4.133 par les hostnames DNS |
| `engine/core/DefaultClientEndpoints.h` | Modifier | Remplacer l'URL hardcodée par le hostname DNS |
| `deploy/docker/.env.example` | Modifier | Mettre à jour les commentaires, documenter les sous-domaines |
| `deploy/docker/README.md` | Modifier | Mettre à jour les références à 10.0.4.133 et l'ancienne config Traefik master |
| `deploy/docker/traefik-master.labels.reference.yml` | Supprimer | Fichier de référence devenu caduque |

---

### Task 1 : docker-compose.yml — labels Traefik sur master + suppression publication port 3842

**Files:**
- Modify: `deploy/docker/docker-compose.yml:85-92`

- [ ] **Step 1 : Valider que le YAML actuel est valide (baseline)**

```bash
cd deploy/docker && docker compose config --quiet
```

Expected : aucune erreur, sortie vide ou liste de services.

- [ ] **Step 2 : Supprimer la publication du port 3842 et remplacer les labels master**

Dans `deploy/docker/docker-compose.yml`, remplacer le bloc `ports` + `labels` du service `master` (lignes 85-92) :

```yaml
# AVANT
    ports:
      # Accès LAN typique : http://<ip-hôte>:3842/status , TCP jeu <ip-hôte>:3840
      - "${MASTER_PUBLISH_ADDR:-0.0.0.0}:${MASTER_PORT:-3840}:3840"
      - "${MASTER_PUBLISH_ADDR:-0.0.0.0}:${MASTER_HEALTH_PORT:-3842}:3842"
    labels:
      # Pas de Traefik devant le master pour l'instant (serveur joignable uniquement en IP locale, ex. 10.0.4.133).
      # Réactiver traefik.enable=true + labels HTTP/TCP : voir traefik-master.labels.reference.yml
      - "traefik.enable=false"
```

```yaml
# APRÈS
    ports:
      # TCP jeu : publié directement sur l'hôte (DNS-only Cloudflare → lcdlln-master.tips-of-mine.com:3840)
      - "${MASTER_PUBLISH_ADDR:-0.0.0.0}:${MASTER_PORT:-3840}:3840"
      # Port 3842 (HTTP status) : non publié sur l'hôte — routé en interne par Traefik
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

- [ ] **Step 3 : Valider que le YAML modifié est valide**

```bash
cd deploy/docker && docker compose config --quiet
```

Expected : aucune erreur.

- [ ] **Step 4 : Vérifier qu'il ne reste plus de `traefik.enable=false` sur le master**

```bash
grep -n "traefik.enable=false" deploy/docker/docker-compose.yml
```

Expected : aucune sortie.

- [ ] **Step 5 : Commit**

```bash
git add deploy/docker/docker-compose.yml
git commit -m "feat(docker): activer Traefik sur master (port 3842) via lcdlln-master-status.tips-of-mine.com"
```

---

### Task 2 : external/external_links.json — remplacer 10.0.4.133

**Files:**
- Modify: `external/external_links.json`

- [ ] **Step 1 : Remplacer les 4 valeurs contenant l'IP**

Contenu complet du fichier après modification :

```json
{
  "client": {
    "web_portal_reset_url": "https://lcdlln-portal.tips-of-mine.com/password-recovery",
    "status_api_url": "https://lcdlln-master-status.tips-of-mine.com/status",
    "master_tcp_host": "lcdlln-master.tips-of-mine.com",
    "master_https_host": "lcdlln-master-status.tips-of-mine.com",
    "master_embedded_http_origin": "https://lcdlln-master-status.tips-of-mine.com"
  }
}
```

- [ ] **Step 2 : Vérifier qu'il ne reste plus d'IP**

```bash
grep "10.0.4.133" external/external_links.json
```

Expected : aucune sortie.

- [ ] **Step 3 : Commit**

```bash
git add external/external_links.json
git commit -m "fix(client): remplacer IP 10.0.4.133 par sous-domaines DNS dans external_links.json"
```

---

### Task 3 : DefaultClientEndpoints.h — remplacer l'URL hardcodée

**Files:**
- Modify: `engine/core/DefaultClientEndpoints.h:8`

- [ ] **Step 1 : Remplacer l'URL dans le header C++**

Contenu complet du fichier après modification :

```cpp
#pragma once

#include <string_view>

namespace engine::core::defaults
{
	/// URL de la sonde `/status` — à garder alignée avec `external/external_links.json` (`client.status_api_url`).
	inline constexpr std::string_view kStatusApiUrl = "https://lcdlln-master-status.tips-of-mine.com/status";
}
```

- [ ] **Step 2 : Vérifier qu'il ne reste plus d'IP**

```bash
grep "10.0.4.133" engine/core/DefaultClientEndpoints.h
```

Expected : aucune sortie.

- [ ] **Step 3 : Vérifier la cohérence avec external_links.json**

```bash
grep "status_api_url" external/external_links.json
grep "kStatusApiUrl" engine/core/DefaultClientEndpoints.h
```

Expected : les deux lignes pointent vers `https://lcdlln-master-status.tips-of-mine.com/status`.

- [ ] **Step 4 : Commit**

```bash
git add engine/core/DefaultClientEndpoints.h
git commit -m "fix(client): remplacer IP 10.0.4.133 par sous-domaine DNS dans DefaultClientEndpoints.h"
```

---

### Task 4 : .env.example — mettre à jour commentaires et documenter les sous-domaines

**Files:**
- Modify: `deploy/docker/.env.example`

- [ ] **Step 1 : Remplacer le contenu**

Contenu complet du fichier après modification :

```bash
# ============================================================
# LCDLLN Docker — Variables d'environnement
# ============================================================
# Copier ce fichier en .env et adapter les valeurs.
# Ne jamais committer .env en production.
# ============================================================

# --- MySQL ---------------------------------------------------
MYSQL_ROOT_PASSWORD=lcdlln_root_dev
MYSQL_DATABASE=lcdlln_master
MYSQL_USER=lcdlln_user
MYSQL_PASSWORD=lcdlln_pass_dev

# --- Serveur -------------------------------------------------
SERVER_PORT=3840

# --- Sécurité ------------------------------------------------
# IMPORTANT : changer ce secret en production (min 32 caractères)
LCDLLN_HMAC_SECRET=dev_secret_change_in_production_32chars

# --- Master (ports sur l'hôte Docker) ------------------------
# TCP jeu (3840) : publié directement sur l'hôte. DNS-only Cloudflare → lcdlln-master.tips-of-mine.com:3840
# HTTP status (3842) : routé en interne par Traefik, non publié sur l'hôte.
# MASTER_PUBLISH_ADDR=0.0.0.0
# MASTER_PORT=3840
# MASTER_HEALTH_PORT=3842

# --- Traefik -------------------------------------------------
# Noms des entrypoints dans la config statique Traefik (ex. web / websecure).
# TRAEFIK_ENTRYPOINT_HTTP=http
# TRAEFIK_ENTRYPOINT_HTTPS=https
# TRAEFIK_CERT_RESOLVER=letsencrypt

# --- Sous-domaines (référence) -------------------------------
# Ces valeurs sont câblées dans les labels Traefik du docker-compose.
# Les modifier nécessite aussi de mettre à jour external/external_links.json
# et engine/core/DefaultClientEndpoints.h.
# MASTER_STATUS_SUBDOMAIN=lcdlln-master-status.tips-of-mine.com   (HTTP status, via Traefik)
# MASTER_TCP_SUBDOMAIN=lcdlln-master.tips-of-mine.com              (TCP jeu, DNS-only Cloudflare)
# SHARD_TCP_SUBDOMAIN=lcdlln-shard.tips-of-mine.com                (TCP jeu shard, DNS-only Cloudflare)
```

- [ ] **Step 2 : Vérifier qu'il ne reste plus d'IP ni de référence à traefik.enable=false**

```bash
grep "10.0.4.133\|traefik.enable=false\|traefik-master.labels.reference" deploy/docker/.env.example
```

Expected : aucune sortie.

- [ ] **Step 3 : Commit**

```bash
git add deploy/docker/.env.example
git commit -m "docs(docker): mettre à jour .env.example — sous-domaines Traefik, supprimer références IP"
```

---

### Task 5 : README.md — mettre à jour les références IP et Traefik master

**Files:**
- Modify: `deploy/docker/README.md:92-96`

- [ ] **Step 1 : Remplacer le paragraphe "Master — accès par IP (défaut actuel)" (ligne 94)**

Localiser et remplacer le bloc suivant :

```markdown
**Master — accès par IP (défaut actuel)** : le service `master` publie **3840** (jeu) et **3842** (HTTP embarqué : `/status`, etc.) sur **`MASTER_PUBLISH_ADDR`** (défaut `0.0.0.0`), joignable depuis le LAN (ex. `http://10.0.4.133:3842/status`). **`traefik.enable=false`** sur le master : pas de route Traefik tant que vous n'avez pas de DNS / reverse proxy devant. Côté client, adaptez **`external/external_links.json`** (copié à côté de l'exe) si l'IP du serveur change.

**Master derrière Traefik (plus tard)** : copier les labels depuis **`traefik-master.labels.reference.yml`** (HTTP vers 3842, TCP jeu vers 3840, TLS passthrough). Il faut alors des **entrypoints** Traefik adaptés et `traefik.enable=true` sur le master ; voir aussi [doc Traefik TCP](https://doc.traefik.io/traefik/routing/routers/#tcp).
```

Par :

```markdown
**Master — HTTP status via Traefik** : le port 3842 (HTTP : `/status`, `/healthz`) est routé par Traefik via `https://lcdlln-master-status.tips-of-mine.com`. Le port 3840 (TCP jeu) est publié directement sur l'hôte et accessible via `lcdlln-master.tips-of-mine.com:3840` (DNS-only Cloudflare, nuage gris). Le port 3843 (TCP shard) est accessible via `lcdlln-shard.tips-of-mine.com:3843` (même principe). Côté client, les endpoints sont définis dans **`external/external_links.json`** (copié à côté de l'exe) et **`engine/core/DefaultClientEndpoints.h`** (valeur de compilation par défaut).
```

- [ ] **Step 2 : Mettre à jour la référence à `traefik-master.labels.reference.yml` dans la même section (ligne 92)**

Localiser dans la ligne 92 la mention de `traefik-master.labels.reference.yml` :

```markdown
Copies des labels : **`traefik-web-portal.labels.reference.yml`**, **`traefik-master.labels.reference.yml`**.
```

Remplacer par :

```markdown
Copie des labels web-portal : **`traefik-web-portal.labels.reference.yml`**.
```

- [ ] **Step 3 : Vérifier qu'il ne reste plus de références obsolètes**

```bash
grep "10.0.4.133\|traefik-master.labels.reference\|traefik.enable=false" deploy/docker/README.md
```

Expected : aucune sortie.

- [ ] **Step 4 : Commit**

```bash
git add deploy/docker/README.md
git commit -m "docs(docker): mettre à jour README — sous-domaines master, supprimer références IP et fichier référence"
```

---

### Task 6 : Supprimer traefik-master.labels.reference.yml

**Files:**
- Delete: `deploy/docker/traefik-master.labels.reference.yml`

- [ ] **Step 1 : Vérifier que le fichier existe bien**

```bash
ls deploy/docker/traefik-master.labels.reference.yml
```

Expected : le fichier est listé.

- [ ] **Step 2 : Supprimer le fichier**

```bash
git rm deploy/docker/traefik-master.labels.reference.yml
```

- [ ] **Step 3 : Vérifier la suppression**

```bash
ls deploy/docker/traefik-master.labels.reference.yml 2>&1
```

Expected : `No such file or directory`.

- [ ] **Step 4 : Commit**

```bash
git commit -m "chore(docker): supprimer traefik-master.labels.reference.yml (intégré dans docker-compose)"
```

---

### Task 7 : Vérification finale — aucune référence à 10.0.4.133

- [ ] **Step 1 : Scanner tout le projet (hors commentaires docker-compose)**

```bash
grep -rn "10.0.4.133" \
  --include="*.json" \
  --include="*.h" \
  --include="*.cpp" \
  --include="*.yml" \
  --include="*.yaml" \
  --include="*.env*" \
  --include="*.md" \
  .
```

Expected : seules les lignes dans `docs/superpowers/specs/` (le design doc) peuvent subsister — aucune dans le code source, headers ou config Docker.

- [ ] **Step 2 : Valider le docker-compose final**

```bash
cd deploy/docker && docker compose config --quiet
```

Expected : aucune erreur.

- [ ] **Step 3 : Commit de clôture si des fichiers non commités subsistent**

```bash
git status
```

Expected : `nothing to commit, working tree clean`.
