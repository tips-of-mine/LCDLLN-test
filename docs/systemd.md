# systemd — Master et Shard (M24.1)

Ce document décrit l’installation et l’utilisation des unités systemd pour déployer le serveur **Master** (`lcdlln_server`) et les serveurs **Shard** (`lcdlln_shard`) sous Linux (Debian/Ubuntu).

---

## 1. Fichiers fournis

| Fichier | Rôle |
|--------|------|
| `deploy/systemd/master.service` | Unité pour le Master (auth, registre shards, tickets). |
| `deploy/systemd/shard@.service` | Modèle (template) pour plusieurs shards : une instance par `shard@<instance>`. |

**Spécification des unités :**
- Exécution sous un **utilisateur dédié** (non-root), par défaut `lcdlln`.
- **WorkingDirectory** : répertoire de travail (config, logs).
- **Restart=on-failure** : redémarrage automatique en cas d’échec.
- **EnvironmentFile** optionnel : permet de surcharger chemins et variables d’environnement.

---

## 2. Prérequis

- Linux avec systemd (Debian, Ubuntu, etc.).
- Binaires `lcdlln_server` (Master) et `lcdlln_shard` (Shard) compilés (voir build Linux).
- MySQL/MariaDB configuré si le Master utilise la DB (migrations, auth).

---

## 3. Installation

### 3.1 Utilisateur et répertoires

Créez l’utilisateur dédié et les répertoires (à adapter si vous utilisez un autre chemin que `/opt/lcdlln`) :

```bash
sudo useradd --system --no-create-home --shell /usr/sbin/nologin lcdlln

sudo mkdir -p /opt/lcdlln/bin
sudo mkdir -p /opt/lcdlln/shard-1
# Pour d’autres shards : /opt/lcdlln/shard-2, etc.

sudo cp /chemin/vers/lcdlln_server /opt/lcdlln/bin/
sudo cp /chemin/vers/lcdlln_shard  /opt/lcdlln/bin/
sudo cp config.json /opt/lcdlln/
# Optionnel : copier config.json par shard dans /opt/lcdlln/shard-1/, etc.

sudo chown -R lcdlln:lcdlln /opt/lcdlln
sudo chmod 755 /opt/lcdlln/bin/lcdlln_server /opt/lcdlln/bin/lcdlln_shard
```

### 3.2 Copie des unités systemd

```bash
sudo cp deploy/systemd/master.service   /etc/systemd/system/
sudo cp deploy/systemd/shard@.service   /etc/systemd/system/
sudo systemctl daemon-reload
```

### 3.3 Fichier d’environnement optionnel

Pour le Master (chemins, ports, etc.) :

```bash
sudo mkdir -p /etc/lcdlln
sudo tee /etc/lcdlln/master.env << 'EOF'
# Exemple : surcharge du binaire ou du répertoire de travail
# WORKING_DIR=/var/lib/lcdlln
# PATH=/opt/lcdlln/bin:$PATH
EOF
sudo chown root:lcdlln /etc/lcdlln/master.env
sudo chmod 640 /etc/lcdlln/master.env
```

Pour un shard (ex. instance `1`) :

```bash
sudo tee /etc/lcdlln/shard-1.env << 'EOF'
# Exemple : config spécifique shard 1
EOF
```

Le préfixe `-` dans `EnvironmentFile=-/etc/lcdlln/master.env` indique que l’absence du fichier n’est pas une erreur.

---

## 4. Démarrage et contrôle

### Master

```bash
sudo systemctl enable master
sudo systemctl start master
sudo systemctl status master
sudo systemctl stop master
sudo systemctl restart master
```

### Shard (template)

Une instance par identifiant (ex. `1`, `2`, `shard-a`) :

```bash
sudo systemctl enable shard@1
sudo systemctl start shard@1
sudo systemctl status shard@1
sudo systemctl stop shard@1
sudo systemctl restart shard@1
```

Pour plusieurs shards :

```bash
sudo systemctl start shard@1 shard@2
sudo systemctl status shard@1 shard@2
```

---

## 5. Smoke test (Debian/Ubuntu)

1. Installer les unités et créer l’utilisateur / répertoires comme ci-dessus.
2. Vérifier que les binaires sont présents dans `/opt/lcdlln/bin/` et que `config.json` est en place dans `WorkingDirectory`.
3. **Master :**
   - `sudo systemctl start master`
   - `sudo systemctl status master` → actif (running)
   - `sudo systemctl restart master` → OK
   - `sudo systemctl stop master` → OK
4. **Shard :**
   - `sudo systemctl start shard@1`
   - `sudo systemctl status shard@1` → actif (running)
   - `sudo systemctl restart shard@1` → OK
   - `sudo systemctl stop shard@1` → OK
5. Consulter les logs : `journalctl -u master -f` et `journalctl -u shard@1 -f`.

---

## 6. Critères de validation (DoD)

- [x] `systemctl start` / `stop` / `restart` fonctionnent pour `master` et `shard@<instance>`.
