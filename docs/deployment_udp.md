# Déploiement — port UDP gameplay (replication joueurs)

Depuis la replication joueurs (TA → TD), le shard expose **un nouveau port UDP**
pour la boucle gameplay (Hello / Input / Snapshot via `ServerApp` +
`UdpTransport`). Ce port s'ajoute aux ports TCP préexistants.

## Récapitulatif des ports shard

| Port           | Proto | Rôle                                            | Source config                                      |
|----------------|-------|-------------------------------------------------|----------------------------------------------------|
| 3843           | TCP   | Ticket / handshake auth (NetServer)             | `server.tcp.port` (ou fallback `shard.port`)       |
| 3844           | TCP   | Health / metrics HTTP (loopback usuel)          | `server.health.port`                               |
| **27015**      | **UDP** | **Gameplay (Hello / Input / Snapshot)**       | **`server.listen_port`**                           |

Le port UDP est défini par `server.listen_port` dans `shard.config.json`
(défaut **27015**). Il **doit être joignable depuis chaque client** :
le client envoie ses paquets vers `<ip-shard>:<port-UDP>` annoncé par le master.

## 1. Configurer le shard

### Choisir le port UDP (optionnel)

Le défaut `27015` (héritage Source Engine) fonctionne. Pour changer :

- Éditer `deploy/docker/config/shard.config.json` :
  ```json
  "server": { "listen_port": 30000 }
  ```
- Aligner la variable Docker `SHARD_UDP_PORT=30000` dans `.env`.
- Aligner le mapping dans `docker-compose.yml` (cf. section 2).
- Aligner `SHARD_REGISTER_UDP_ENDPOINT` (cf. section 4).

### Annoncer l'endpoint UDP au master

Le master ne propage l'endpoint UDP au client **que si** le shard le déclare.
Sinon, le client lit une chaîne vide et ne peut pas envoyer de gameplay.

Dans `shard.config.json` :
```json
"shard": {
  "register": {
    "udp_endpoint": "10.0.4.133:27015"
  }
}
```

Ou via variable d'environnement Docker (préférable, alignée avec
`SHARD_REGISTER_ENDPOINT`) :
```env
SHARD_REGISTER_UDP_ENDPOINT=10.0.4.133:27015
```

**Format** : `ip-publique-joignable:port`. Doit pointer vers la même socket
que celle exposée par le mapping Docker du service `shard`.

## 2. Mapping Docker

Le `docker-compose.yml` versionné publie déjà le port UDP :

```yaml
services:
  shard:
    ports:
      - "${SHARD_PORT:-3843}:3843"
      - "${SHARD_UDP_PORT:-27015}:27015/udp"
```

Le `Dockerfile.shard` déclare aussi `EXPOSE 27015/udp` (documentaire).
Aucune action si l'on garde les défauts ; sinon ajuster `SHARD_UDP_PORT` dans
`.env`.

## 3. Firewall hôte Linux

### UFW (Ubuntu / Debian)

```bash
sudo ufw allow 27015/udp comment 'LCDLLN shard gameplay'
sudo ufw status numbered   # verification
```

### iptables (manuel)

```bash
sudo iptables -A INPUT -p udp --dport 27015 -j ACCEPT
sudo netfilter-persistent save   # selon distribution
```

### nftables

```bash
sudo nft add rule inet filter input udp dport 27015 accept
```

### firewalld (CentOS / RHEL / Fedora)

```bash
sudo firewall-cmd --permanent --add-port=27015/udp
sudo firewall-cmd --reload
```

## 4. Pare-feu cloud / NAT

À ouvrir **en plus** du firewall hôte si le serveur est derrière un cloud
provider :

| Cloud      | Où ouvrir                                                                  |
|------------|----------------------------------------------------------------------------|
| OVH        | Manager → IP → Firewall réseau → règle UDP 27015 entrante                  |
| AWS        | Security Group EC2 → Inbound rules → UDP 27015 (source 0.0.0.0/0 ou plus restrictif) |
| Hetzner    | Cloud Console → Firewalls → règle entrante UDP 27015                       |
| GCP        | VPC Network → Firewall rules → règle ingress UDP 27015                     |
| Azure      | NSG → Inbound security rules → UDP 27015                                   |
| Scaleway   | Console → Security Group → règle entrante UDP 27015                        |

NAT domestique (auto-hébergement) : redirection UDP 27015 → IP LAN du serveur.

## 5. Reverse proxy : ne pas mettre Traefik devant l'UDP

Traefik route TCP/HTTP par défaut, **pas l'UDP du gameplay**. Le client doit
joindre directement la socket UDP exposée par Docker. Garder le shard joint
**en IP directe** pour ce port, exactement comme le TCP `3843` actuel
(`traefik.enable=false` sur le service shard pour ce port).

## 6. Vérification post-déploiement

### Côté serveur

```bash
# Sur l'hote : le port UDP est-il en ecoute ?
sudo ss -ulpn | grep 27015
#  UNCONN  0  0  0.0.0.0:27015  0.0.0.0:*  users:(("lcdlln_shard", pid=...))

# Logs shard : ServerApp doit avoir initialise le transport
docker compose logs shard | grep -E "Gameplay UDP loop|ServerApp Init|listen_port"
# Attendu :
#   [Core] [ServerApp] Init OK (port=27015, tick_hz=20, snapshot_hz=10)
#   [Net]  [ShardMain] Gameplay UDP loop demarree (ServerApp sur thread dedie)
```

### Côté client (depuis une autre machine)

```bash
# Le port est-il joignable depuis le reseau du client ?
nc -uvz <ip-shard> 27015
# Connection to <ip-shard> 27015 port [udp/*] succeeded!

# Ou avec nmap :
nmap -sU -p 27015 <ip-shard>
# 27015/udp open|filtered  (UDP -> "open|filtered" est attendu sans handshake)
```

### Bout-en-bout

Connecter 2 clients ; chaque client doit voir l'autre se déplacer
(mesh placeholder, voir `Engine::RecordRemoteAvatars`). Si le client A reste
seul à l'écran alors que le client B est connecté :

1. Vérifier dans les logs serveur que **les 2 ont passé HandleHello**
   (`[ServerApp] Hello accepted ...`).
2. Vérifier que `SHARD_REGISTER_UDP_ENDPOINT` est bien renseigné côté shard
   (sinon client ne sait pas où envoyer).
3. Vérifier le firewall : `nc -uvz` doit réussir depuis la machine cliente.

## 7. Wire-breaking : redéploiement lock-step

Tout changement du protocole UDP gameplay (bump `kProtocolVersion`) exige un
redéploiement **simultané master + shard + client**. Référence :
[PLAN_replication_joueurs.md](PLAN_replication_joueurs.md).
