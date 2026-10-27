# Étape 3 — le réseau de jeu (UDP client ↔ shard)

Suite des étapes 1 (`etape1_master_shard.md`) et 2 (`etape2_persistance.md`).
L'étape 3 reprend le bloc réseau du rapport d'analyse City of Heroes : sécuriser le
canal UDP, réduire la bande passante, puis donner au shard le dernier mot sur le
mouvement.

| Ticket | Sujet | Statut |
|---|---|---|
| 3.1 (chantier 4.3) | Chiffrement et authentification des datagrammes UDP | livré |
| 3.2 (chantier 4.2, + 4.5 / 4.6) | Snapshots en delta sur base acquittée, quantification, plafond d'entités | cette PR |
| 3.3 (chantier 4.1) | Mouvement autoritaire serveur, prédiction et recalage client | à venir |
| 3.4 (chantier 4.4) | Canal fiable minimal pour les messages importants | à venir |

## 3.1 — Chiffrer et authentifier l'UDP

### Avant

Les datagrammes de jeu circulaient en clair. Seul le couple adresse + `clientId`
protégeait une session : n'importe qui sur le chemin pouvait lire la position, le
chat de proximité ou l'inventaire d'un joueur, et un tiers capable d'usurper l'adresse
source pouvait injecter des entrées.

### Principe

```
Client                         Master (TLS)                      Shard (TLS vers le master)
  │ tire K (32 octets)            │                                  │
  │── EnterWorld(id, nom, K) ───▶│                                  │
  │                               │── Admit(id, …, K) ─────────────▶│ registre : id → K
  │ ouvre la socket, tire un sel S│                                  │
  │── Poignée de main [id, S, Hello chiffré] ──────────────────────▶│ clé de travail = HKDF(K, S)
  │◀──────────────────────────── Welcome, snapshots… (chiffrés) ────│
  │── Entrées, sorts, chat… (chiffrés) ────────────────────────────▶│
```

- **Clé de session K** : tirée par le client à chaque EnterWorld (`GenerateUdpSessionKey`,
  OpenSSL `RAND_bytes`). Elle part au master en queue de la requête EnterWorld, puis au
  shard en queue du push d'admission. Les deux liens sont en TLS : K ne circule jamais
  en clair. Le master ne la garde pas et ne la journalise pas ; le journal des paquets
  (`PacketLog`) ne garde aucun aperçu de ces deux opcodes.
- **Sel S** : 16 octets tirés à chaque ouverture de socket (`GameplayUdpClient::Init`).
  Client et shard dérivent la **clé de travail** `HKDF-SHA256(K, S, "LCDLLN UDP v1")`.
  Une nouvelle socket donne une nouvelle clé de travail : les compteurs repartent de 1
  sans jamais réutiliser un nonce.
- **Chiffrement** : ChaCha20-Poly1305 (AEAD) sur chaque datagramme. L'en-tête est
  authentifié (données associées), le contenu chiffré.
- **Nonce** (12 octets) = sens (`C2S\0` ou `S2C\0`) + compteur 64 bits. Le sens sépare
  les deux flux qui partagent la clé de travail : un datagramme renvoyé à son émetteur
  ne s'ouvre pas.
- **Anti-rejeu** : chaque extrémité refuse un compteur déjà vu ou trop ancien (fenêtre
  de 64 paquets, `UdpReplayWindow`). Le désordre léger d'UDP est toléré.
- **Rejeu du Hello** : le shard mémorise les sels déjà utilisés avec chaque clé, dans
  l'entrée du registre d'admission qui porte la clé (`ConsumeUdpSalt`, 256 par clé au
  plus) : tant que la clé peut ouvrir une poignée de main, ses sels restent refusés. Un
  Hello capturé puis renvoyé est refusé ; le même Hello reçu de nouveau depuis la même
  adresse (répétition du client) renvoie seulement le Welcome.
- **Une clé n'est admise qu'une fois** : la reconnexion automatique au master renvoie
  l'EnterWorld avec une clé NEUVE (`ConsumeRefreshedUdpKey`) ; la session UDP en cours
  garde sa clé de travail, les sockets suivantes prennent la nouvelle clé. Un vieux Hello
  ne peut donc jamais être rouvert après l'expiration de ses sels.
- **Adresse liée à un personnage** : une adresse qui a une session pour un personnage
  n'est jamais reprise par un autre (`EndpointBusy`, et contrôle dans `HandleHello`). Un
  joueur qui usurpe l'adresse source d'un autre, avec sa propre clé valide, ne détourne
  pas sa session. L'adresse se libère à la déconnexion ou après 30 s d'inactivité.
- **Pas de repli vers le clair** : un personnage dont le shard connaît la clé ne peut
  ouvrir sa session qu'avec une poignée de main chiffrée (`HandleHello`,
  `authenticatedBySession`). Un datagramme en clair venant d'une adresse qui a une
  session chiffrée est refusé.

Le contenu chiffré est un paquet du protocole de jeu habituel (`ServerProtocol`) : le
chiffrement est une enveloppe, les paquets ne changent pas.

### Formats (petit-boutiste)

| Datagramme | Octets |
|---|---|
| Poignée de main (client → shard, porte le Hello) | `magic "LCDE"` (4) · `type = 1` (1) · `character_id` (8) · `sel` (16) · chiffré · étiquette (16) — compteur implicite 0, surcoût 45 octets |
| Données (deux sens) | `magic "LCDE"` (4) · `type = 2` (1) · `compteur` (8) · chiffré · étiquette (16) — compteur ≥ 1, surcoût 29 octets |

`kProtocolVersion` passe de 15 à 16. Les tampons de réception du shard passent de 512
à 1472 octets (`Datagram`) : un datagramme tronqué serait refusé à l'authentification.

### Arrivée du Hello avant l'admission

Le Hello UDP arrive souvent au shard avant le push d'admission du master (cas déjà
géré en TA.3). Deux mécanismes se complètent :

- **Le client répète son Hello** (mêmes octets, même sel) toutes les secondes tant qu'il
  n'a pas de Welcome, 15 fois au plus (`GameplayUdpClient::MaybeResendHello`). C'est le
  mécanisme principal : un Hello reçu après l'admission passe directement, et un Hello
  refusé parce que la nouvelle clé n'était pas encore arrivée (nouvel EnterWorld moins de
  5 minutes après le précédent) passe à la répétition suivante.
- **Le shard garde la poignée de main** reçue avant l'admission (`PendingHello::sealedHello`)
  et la rouvre quand l'admission, et donc la clé, arrive. Rien n'étant encore
  authentifié, cette file est bornée sans jamais évincer une entrée : 4 poignées de main
  par personnage, 8 par adresse IP, 1024 au total. Pleine, elle refuse les nouvelles
  entrées ; la répétition du client prend le relais. Un Hello en clair ne modifie jamais
  une poignée de main chiffrée en attente.

### Configuration

| Clé (`config.json` du shard) | Valeurs | Défaut |
|---|---|---|
| `gameplay_udp.encryption` | `required` : seuls les datagrammes chiffrés sont acceptés · `optional` : le clair reste accepté des adresses sans session chiffrée | `required` sur le shard Linux (registre d'admission branché), `optional` sur le build Windows sans master |

Rien à renseigner pour le fonctionnement normal.

### Fichiers

| Fichier | Rôle |
|---|---|
| `src/shared/network/UdpSecureChannel.h/.cpp` | Enveloppe : dérivation HKDF, scellement/ouverture ChaCha20-Poly1305, fenêtre anti-rejeu |
| `src/shardd/world/UdpSessionTable.h/.cpp` | Sessions du shard par adresse (une adresse = un personnage), purge |
| `src/shardd/world/AdmittedCharacterRegistry` | Garde la clé de session reçue avec l'admission et ses sels déjà utilisés |
| `src/shared/network/CharacterPayloads` / `ShardPayloads` | Clé en queue de l'EnterWorld (47) et de l'admission (199) |
| `src/client/net/GameplayUdpClient` | Sel par socket, scelle tout ce qui part, refuse ce qui ne s'ouvre pas, répète le Hello |
| `src/client/app/Engine.cpp`, `src/client/auth/AuthUi` | Tire la clé à chaque EnterWorld, une clé neuve à la reconnexion master, l'efface à la déconnexion |
| `src/shared/server_bootstrap/ServerApp` | `ProcessPacket` (ouverture), `SendDatagram` (scellement, remplace `m_transport.Send`), `ProcessSecureHandshake` |

### Tests

`udp_secure_channel_tests` (aller-retour, falsification, réflexion, rejeu, fenêtre,
sel ou clé différents, compteur 0 jamais réutilisé), `udp_session_table_tests` (doublon,
rejeu depuis une autre adresse, faux Hello qui ne consomme pas le sel, adresse d'un autre
personnage refusée, nouvelle socket, purge), `shard_payloads_tests` (clé en queue,
absence, clé tronquée refusée), `admitted_character_registry_tests` (clé avec
l'admission, sels par clé, plafond), `packet_log_tests` (aucun aperçu pour 47 et 199).

### Déploiement

Lock-step **master + shard + client**. Un client antérieur (en clair) ne se connecte
plus à un shard neuf ; un master antérieur ne relaie pas la clé (le shard refuse alors
le Hello chiffré : « aucune clé de session »).

## 3.2 — Snapshots en delta sur base acquittée (chantiers 4.2, 4.5, 4.6)

### Avant

Dix fois par seconde, chaque client recevait l'état **complet** de toutes les entités
de son ensemble d'intérêt : 61 octets au moins par entité (flottants 32 bits, nom et
genre des joueurs à chaque fois), découpés par paquets de 18 entités. Un morceau perdu
faisait perdre tout le snapshot. Des messages Spawn / Despawn partaient en plus, ignorés
par le client.

### Principe

```
Shard                                             Client
 n°7 complet (base 0) ─────────────────────────▶ reconstruit n°7
                      ◀───────────── Input(ack = 7)
 n°8 delta / base 7 ───────────────X (perdu)
 n°9 delta / base 7 ───────────────────────────▶ reconstruit n°9 (a la base 7)
                      ◀───────────── Input(ack = 9)
 n°10 delta / base 9 ──────────────────────────▶ …
```

- Chaque snapshot porte son numéro et celui de sa **base** (0 = complet). Le client
  acquitte le dernier snapshot reconstruit en entier dans chaque Input
  (`ackSnapshotSeq`, 20 Hz). Le shard écrit le suivant par rapport à ce snapshot
  acquitté (`SnapshotSender`, 16 derniers gardés ; plus ancien = snapshot complet).
- **Rien n'est renvoyé** : un snapshot perdu est remplacé par le suivant, écrit sur une
  base que le client possède (il l'a acquittée). C'est l'inverse de City of Heroes, qui
  envoyait chaque snapshot en mode fiable.
- Une entité **inchangée** n'est pas envoyée ; une entité modifiée n'envoie que les
  champs qui ont changé (masque d'un octet) ; une entité sortie de l'intérêt est listée
  dans les **retraits** ; une entité nouvelle arrive complète. Spawn / Despawn ne sont
  plus émis.
- Le client (`SnapshotReceiver`) rassemble les morceaux (désordre et doublons tolérés),
  applique le delta à la base, garde les 32 derniers snapshots reconstruits. Un morceau
  manquant : le snapshot est abandonné et remplacé par le suivant. Base inconnue : le
  snapshot est ignoré, jamais appliqué à moitié.

### Quantification (4.6)

| Champ | Fil | Précision |
|---|---|---|
| Position | entier en centimètres ; delta zigzag varint par rapport à la base (1 à 2 octets par axe pour un déplacement courant), absolu à l'apparition | 1 cm |
| Orientation | 16 bits | 0,0055° |
| Vitesse | cm/s, zigzag varint | 1 cm/s |
| PV, PV max, drapeaux | varint | exact |
| Identité (client, archétype, nom ≤ 64 o, genre ≤ 8 o) | seulement à l'apparition ou au changement | exact |

Le delta est calculé sur les valeurs **quantifiées**, identiques des deux côtés : la base
du shard et celle du client ne dérivent jamais. Une entité qui bouge de 50 cm coûte
6 octets au plus ; un snapshot sans changement, 42 octets (en-tête).

### Plafond et fréquence (4.5)

- **Plafond d'entités par client** : `server.replication.view_cap_entities`, désormais
  **100** par défaut, 512 au plus (plafond du codec ; 0 = ce maximum). Au-delà, les plus
  proches sont gardées : le rayon de vue se réduit de lui-même dans la foule.
- **Jamais coupés par le plafond** (parmi les entités des cellules d'intérêt) : les
  membres du groupe, la dernière cible attaquée ou visée par un sort, la cible du sort en
  cours. Un membre du groupe hors des cellules d'intérêt n'est pas ajouté.
- **PNJ d'ambiance** (`MobEntity::ambient`) : mis à jour un snapshot sur deux (5 Hz). Les
  snapshots impairs reprennent leur état du snapshot précédent (jamais d'un plus ancien,
  même si l'acquittement est en retard).

### Format d'un morceau (kind 4, petit-boutiste)

En-tête 34 octets : `clientId u32 · serverTick u32 · snapshotSeq u32 · baselineSeq u32 ·
connectedClients u16 · chunkIndex u16 · chunkCount u16 · receivedPackets u32 ·
sentPackets u32 · removedCount u16 · entryCount u16`, puis les identifiants retirés
(varint), puis les entrées `entityId varint · masque u8 · champs du masque`. Budget
~1100 octets de charge utile par morceau (1200 o de datagramme moins l'enveloppe
chiffrée et l'en-tête du protocole). `kProtocolVersion` passe de 16 à 17 ; l'Input passe
de 25 à 29 octets.

### Fichiers

| Fichier | Rôle |
|---|---|
| `src/shared/network/SnapshotDelta.h/.cpp` | Quantification, codec delta, `SnapshotSender` (shard), `SnapshotReceiver` (client) |
| `src/shared/server_bootstrap/ServerApp` | `SendSnapshot` (état quantifié, PNJ d'ambiance à mi-fréquence), acquittement dans l'Input, plafond avec priorités, fin de Spawn / Despawn |
| `src/client/ui_common/UIModel` | `ApplySnapshot` via `SnapshotReceiver`, `LatestSnapshotSeq` |
| `src/client/net/GameplayUdpClient`, `src/client/app/Engine.cpp` | `ackSnapshotSeq` dans chaque Input |

### Tests

`snapshot_delta_tests` : quantification sans dérive, complet puis delta, perte du
snapshot intermédiaire, morceaux dans le désordre et en double, morceau manquant puis
snapshot suivant, base inconnue, acquittements invalides, PNJ à mi-fréquence, paquets
mal formés, et une simulation de 600 pas (apparitions, retraits, déplacements, 30 % de
perte, acquittements retardés) où chaque snapshot reconstruit doit être exact.

### Déploiement

Lock-step **master + shard + client** (`kProtocolVersion` 17). Pas de migration. Nouvelle
valeur par défaut `server.replication.view_cap_entities = 100` (à mettre à 0 dans la
config du shard pour garder l'ancien comportement sans plafond).
