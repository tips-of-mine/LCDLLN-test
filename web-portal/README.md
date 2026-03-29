# LCDLLN — Portail web (joueurs & administration)

Application **séparée** du moteur (`/engine`, `/game`). Elle fournit :

- **Public** : présentation, roadmap, contact, support, signalement de bugs.
- **Joueur** (après auth) : profil, serveurs, personnages, amis/guilde en ligne, CGU (historique, relire, accepter les nouvelles).
- **Admin** : gestion des CGU en base, **lecture seule** des profils, suivi des acceptations CGU.

Règles de confidentialité : un joueur **ne doit jamais** voir les données d’un autre compte ; toutes les API doivent filtrer sur `account_id` issu du JWT.

## Stack

- **Next.js 14** (App Router), build **standalone** pour Docker.
- Auth / API métier : **à implémenter** (JWT, MySQL `lcdlln_master`, alignement avec le master).

## Scripts

```bash
cd web-portal
npm install
npm run dev
```

Build production : `npm run build` puis `npm start` ou image Docker.

## Base de données

- CGU : `terms_editions`, `terms_localizations`, `account_terms_acceptances` (migration `0007`).
- Exploits unifiés : `exploits`, `account_exploit_unlocks`, `character_exploit_unlocks` (migration `0008`).
- Bugs : `bug_reports` ; la progression alimente les exploits dont `metric_source = 'bug_reports'` (seuils en base).
- Nettoyage ancienne 0008 : migration `0009` (`DROP` de `account_bug_exploit_stats` si elle existait).

## Docker & Traefik

L’image écoute le port **3000**. Dans `deploy/docker/docker-compose.yml`, le service `web-portal` expose ce port et inclut des **labels Traefik** d’exemple (à adapter : host, entrypoints, TLS).

Traefik est supposé **externe** au repo : il suffit que le réseau Docker soit attaché au même réseau que Traefik ou d’utiliser `traefik.docker.network`.

## NGINX

Fichier d’exemple : `nginx/default.conf` (reverse proxy vers Next). Inutile si Traefik termine déjà le trafic vers le conteneur.
