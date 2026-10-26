# Étape 2 — fiabiliser la persistance des personnages

Suite de l'étape 1 (`etape1_master_shard.md`) : depuis 1.2, un seul shard est
propriétaire d'un personnage. L'étape 2 fait de la base la seule source de vérité
et retire le travail SQL du tick de jeu.

| Ticket | Sujet | Statut |
|---|---|---|
| 2.1 (chantier 3.1) | Base = seule source de vérité (fin des `.ini`, fin de l'écriture de position par le master) | livré |
| 2.2 (chantier 3.2) | SQL hors du tick (file de sauvegarde par personnage) | livré |
| 2.3 (chantier 3.3) | Sauvegardes automatiques étalées | livré |
| 2.4 (chantier 3.4) | Échanges et transferts : les deux personnages dans une transaction | livré |
| 2.5 | Carte découverte écrite par le shard | livré |
| 2.6 (chantier 3.6) | Instantanés restaurables | cette PR |
| 2.7 | Lectures du Hello hors du tick | PR suivante |
| 2.8 | Enchères en base, dans des transactions | PR suivante |

## 2.1 — La base, seule source de vérité

Avant : l'état d'un personnage était réparti entre la base (bourse, sac, équipement,
position) et un fichier `persistence/characters/character_<id>.ini` sur le disque
du shard (quêtes, XP, zone, métiers, barres, ceinture, courrier…). Perdre le
répertoire du shard, ou changer de shard, perdait une partie du personnage. Le
master écrivait en plus la position envoyée par le client.

### Où est quoi

| Donnée | Table | Écrite par |
|---|---|---|
| Position de réapparition, niveau | `characters` (`spawn_*`, `level`) | shard propriétaire |
| Zone, XP, PV, rôle modérateur | `character_progress` (colonnes) | shard propriétaire |
| Quêtes, joueurs ignorés, barre d'action, ceinture, gâteaux, compétences, métiers | `character_progress.progress_payload` | shard propriétaire |
| Bourse | `player_wallet` | shard propriétaire |
| Sac | `character_inventory` | shard propriétaire |
| Équipement | `character_equipment` | shard propriétaire |
| Courrier en attente (or, objets) | `character_mailbox` | n'importe quel shard (ajout seul) |

Migration `0078_character_progress_mailbox.sql` (copiée dans
`deploy/docker/sql/migrations/`).

`progress_payload` garde le format `clé=valeur` du fichier `.ini` pour les listes
(quêtes et leurs étapes, métiers…). Le code de lecture et d'écriture est commun au
fichier et à la base (`CharacterPersistence.cpp` : `SerializeCharacterProgressPayload`,
`ApplyCharacterProgressPayload`, testés par `character_progress_payload_tests`).

### Sauvegarde

`ServerApp::SaveCharacterToDb` écrit tout dans **une** transaction, après le
contrôle de propriété de 1.2 (`SELECT … FOR UPDATE`) :
position et niveau → progression → courrier fusionné supprimé → bourse → sac →
équipement → `COMMIT`.

Avec une base, **aucun fichier `.ini` n'est écrit**. Si la base est injoignable,
la sauvegarde échoue (log `ERROR`) et la suivante réessaie : écrire un fichier à la
place créerait une deuxième vérité qui ne serait jamais relue.

**Base configurée mais injoignable** :
- au démarrage (`db.host` renseigné, aucune connexion) : le shard **refuse de
  démarrer**. Sinon il servirait des `.ini` périmés, et ce qui serait joué pendant
  la panne serait perdu au retour de la base ;
- à la connexion d'un joueur (lecture en erreur, table absente) : le Hello est
  **refusé** (`LoadCharacterFromDb` → `Error`), le client réessaie. Le shard ne
  repart jamais d'un `.ini` ou de valeurs par défaut que la sauvegarde suivante
  recopierait en base par-dessus la vraie progression.

Sans base (développement sans MySQL, shard Windows), le fichier `.ini` reste le
seul stockage, comme avant.

### Chargement (Hello)

1. La base d'abord, avant même de créer le joueur. Erreur de lecture → Hello
   refusé (voir plus haut). Si `character_progress` a une ligne, la base contient
   tout : le fichier est ignoré.
2. Sinon (personnage créé avant cette version), le fichier `.ini` est lu une
   fois. Si la base avait déjà bourse, sac et équipement (version Roadmap-10),
   ces trois domaines restent ceux de la base.
3. La sauvegarde suivante écrit tout en base : le personnage est migré, le
   fichier ne sera plus relu.

### Courrier

`DepositMailboxDelivery` (remboursement d'enchère, objet gagné, vente expirée)
ajoute une ligne `character_mailbox` sans lire l'état du destinataire, qui peut être
en jeu sur un autre shard. À la connexion, le shard propriétaire lit les lignes,
les ajoute à la bourse et au sac, et retient les `id` lus (`mailboxClaimedIds`).
Chaque sauvegarde supprime exactement ces lignes dans la même transaction que la
bourse et le sac : le courrier n'est ni perdu ni compté deux fois, et celui
arrivé après la connexion (ou pas encore validé à ce moment) reste pour la
suivante. La sauvegarde qui suit la fusion part une fois la génération du verrou
1.2 et la position de la base connues.

### Master

`CharacterSavePositionHandler` n'écrit plus rien : il vérifie la session et
acquitte la requête, pour que les clients existants ne restent pas en attente.
La position est écrite par le shard propriétaire.

### Limites

- La carte découverte : voir 2.5.
- Les anciens fichiers `.ini` restent sur le disque (jamais supprimés
  automatiquement) ; ils peuvent être archivés une fois tous les personnages migrés.
- Instantanés restaurables : voir 2.6.

### Déploiement

Migration 0078 + shard + master. Client inchangé. **Ordre** : master d'abord (il
applique la migration au démarrage), puis le shard — un shard démarré sans la
migration refuse les connexions (table `character_progress` absente).

## 2.2 — SQL hors du tick

Avant : chaque sauvegarde (ramassage, équipement, quête, autosave…) exécutait sa
transaction SQL dans le tick de jeu. Une base lente ralentissait tout le shard.

### La file de sauvegarde

`src/shardd/gameplay/character/CharacterSaveQueue.{h,cpp}`, testée par
`character_save_queue_tests` (aussi passée sous ThreadSanitizer).

- `SaveConnectedClient` copie l'état du personnage (instantané) et le dépose dans
  la file ; le tick repart aussitôt. L'écriture (`PersistCharacterState` →
  `SaveCharacterToDb`) se fait sur un thread de la file.
- **Ordre** : un personnage est toujours traité par le même thread
  (`id % nombre de threads`). Ses écritures restent dans l'ordre.
- **Fusion** : une sauvegarde pas encore commencée est remplacée par la suivante
  du même personnage. Dix sauvegardes rapprochées (ramassage d'objets en série)
  deviennent une seule écriture.
- **File pleine** : la sauvegarde est écrite tout de suite, dans le tick (log
  `WARN`), après avoir laissé finir l'écriture éventuellement en cours du même
  personnage (sinon un instantané plus ancien passerait après). Rien n'est perdu ;
  le tick ralentit au lieu de jeter des données.
- **Relecture** : au Hello, le shard attend que les écritures en cours du
  personnage soient terminées (`Flush`, 2 s au plus) avant de le relire. Au-delà,
  le Hello est refusé (le client réessaie) plutôt que de charger un état ancien.
- **Déconnexion** : la sauvegarde de départ est attendue (2 s au plus) : si le
  joueur rejoint aussitôt un autre shard, le master déplace le verrou de
  propriété (1.2) et une écriture tardive serait refusée.
- **Interblocages** : chaque transaction tourne en `READ COMMITTED` (pas de
  verrous d'intervalle entre personnages voisins) et est rejouée jusqu'à 3 fois
  si elle échoue alors que la connexion a été obtenue.
- **Arrêt** : `Shutdown` sauve tous les joueurs, puis `Stop()` écrit tout ce qui
  reste avant de rendre la main. `main_linux` appelle maintenant
  `gameplayApp.Shutdown()` **avant** de fermer le pool : avant, les sauvegardes de
  fin partaient après la fermeture de la base et échouaient.

### Réglages (optionnels)

| Clé | Défaut | Rôle |
|---|---|---|
| `shard.save_workers` | 2 | Threads d'écriture (borné aux connexions réellement ouvertes moins une, laissée au tick ; moins de 2 connexions = pas de file) |
| `shard.save_queue_max` | 4096 | Travaux en attente avant écriture immédiate |

Sans base (développement, shard Windows), pas de file : le fichier `.ini` est
écrit dans le tick, comme avant.

### Limites

- Les lectures du Hello (`LoadCharacterFromDb`, `LoadSpawnFromDb`) restent dans le
  tick : il faut l'état pour faire apparaître le personnage.
- Le dépôt de courrier (`DepositMailboxDelivery`, rare) reste immédiat.
- Sur un crash du processus, ce qui est encore dans la file est perdu (au plus
  quelques secondes d'activité). Les échanges et transferts (2.4) sont écrits
  immédiatement, dans une transaction, pour ne jamais dupliquer d'objets.

### Déploiement

Shard uniquement. Pas de migration, pas de changement réseau.

## 2.3 — Sauvegardes automatiques étalées

Avant : `MaybeAutosaveCharacters` sauvait **tous** les joueurs au même tick,
toutes les 30 s (`server.character_autosave_seconds`). Avec beaucoup de joueurs,
un pic d'écritures toutes les 30 s.

Maintenant, à chaque tick, seuls les personnages dont c'est le tour sont sauvés :
`id % période == tick % période` (période = 30 s × fréquence du tick). Chaque
personnage est toujours sauvé une fois par période, mais la charge est répartie :
avec 6 000 joueurs à 20 Hz, 10 sauvegardes par tick au lieu de 6 000 d'un coup.

Règle pure : `src/shardd/gameplay/character/AutosaveSchedule.h`
(`IsCharacterAutosaveDue`), testée par `autosave_schedule_tests`.

Les sauvegardes déclenchées par un événement (ramassage, quête, déconnexion…)
ne changent pas. Un bilan est journalisé une fois par période (nombre de joueurs,
travaux en attente dans la file 2.2).

### Déploiement

Shard uniquement. Pas de migration, pas de changement réseau, même clé de
configuration.

## 2.4 — Transactions à deux personnages

Avant : un échange entre joueurs modifiait les deux personnages en mémoire, sans
sauvegarde ; l'autosave les écrivait ensuite **séparément**. Un arrêt du shard
entre les deux écritures pouvait dupliquer un objet (écrit chez l'acheteur, encore
chez le vendeur) ou le perdre. Le transfert de monnaie faisait deux sauvegardes
indépendantes.

### Fonctionnement

`ServerApp::SaveCharacterPair` (appelé à la fin d'un échange validé et d'un
transfert de monnaie) :

1. laisse finir les sauvegardes en file des deux personnages (2.2), plus
   anciennes que l'opération ;
2. construit un identifiant unique `<type>-s<shard>-<ms UTC>-<compteur>`
   (`CharacterOperationId.h`, testé par `character_operation_id_tests`) ;
3. `SaveCharacterPairToDb` ouvre **une** transaction :
   - insère l'opération dans `character_operations` (`INSERT IGNORE`) ; si
     l'identifiant existe déjà, l'opération a déjà été écrite → annulation, rien
     n'est appliqué deux fois ;
   - écrit les deux personnages complets (`WriteCharacterStateInTx`, avec le
     contrôle de propriété de 1.2), dans l'ordre croissant des identifiants pour
     éviter les interblocages ;
   - `COMMIT`, ou `ROLLBACK` au moindre échec (y compris si l'un des deux
     personnages n'appartient plus à ce shard).

La transaction est rejouée jusqu'à 3 fois avec le **même** identifiant : si un
essai a en fait été validé (réponse du `COMMIT` perdue), le suivant reçoit
« déjà appliquée » et ne réécrit rien. L'opération est donc écrite entièrement ou
pas du tout, et jamais deux fois.

Si les sauvegardes en file des deux joueurs ne finissent pas en 2 s (base très
lente), pas de transaction : chacun reçoit une sauvegarde en file, dans l'ordre
(log `ERROR`), pour ne jamais écrire un état ancien après l'opération.
`character_operations` sert aussi de journal (qui a échangé avec qui, quand).

Migration `0079_character_operations.sql` (copiée dans `deploy/docker/sql/migrations/`).

Sans base, les deux fichiers `.ini` sont écrits l'un après l'autre, comme avant.

### Code commun

`SaveCharacterToDb` (une sauvegarde) et `SaveCharacterPairToDb` (deux) utilisent
le même corps, `WriteCharacterStateInTx`, qui écrit un personnage dans une
transaction ouverte par l'appelant. `BuildPersistedCharacterState` fabrique
l'instantané d'un joueur pour les deux chemins.

### Limites

- Les ventes aux enchères ne passent pas par ce mécanisme : l'hôtel des ventes
  du shard est encore stocké en fichier, il ne peut pas entrer dans une
  transaction SQL. L'objet et l'or y sont tenus en séquestre par l'hôtel, pas
  par un joueur, ce qui limite le risque de duplication.
- Échec d'écriture (base injoignable) : rien n'est écrit, l'incident est
  journalisé en `ERROR` ; les sauvegardes suivantes réécrivent l'état complet de
  chaque joueur séparément.

### Déploiement

Migration 0079 + shard. Pas de changement réseau, client inchangé.

## 2.5 — Carte découverte écrite par le shard

Avant : le client calculait les cases découvertes (cases de 125 m dans un rayon
de 300 m) et les envoyait au master (opcode 210), qui les fusionnait en base. Deux
écrivains pour un personnage, et une donnée fournie par le client.

Maintenant :

- le shard révèle lui-même les cases autour de la **position autoritaire** du
  joueur (`MapDiscoveryTracker`, `src/shardd/world/`, mêmes règles que le client :
  `src/shared/world/MapDiscovery.h`), dans le monde en cellules uniquement ;
- les cases révélées pendant la session partent dans la transaction de sauvegarde
  du personnage : fusion OU avec `character_map_discovery` (`SELECT … FOR UPDATE`,
  écriture seulement si le masque grandit). Une case découverte ne redevient
  jamais cachée ;
- le master ne fait plus que **lire** la carte pour le client (opcodes 208/209,
  inchangés). Les mises à jour 210 envoyées par le client sont ignorées.

Le client n'est pas modifié : il révèle toujours la carte localement pour
l'affichage immédiat et continue d'envoyer 210 (sans effet). Une prochaine version
du client pourra cesser cet envoi.

Test : `map_discovery_tracker_tests`.

### Déploiement

Shard + master. Pas de migration, pas de changement réseau, client inchangé.

## 2.6 — Instantanés restaurables

Pour le support joueur : à chaque déconnexion, une copie complète et datée du
personnage ; un MJ peut la consulter et la restaurer.

Migration `0080_character_snapshots.sql` (copiée dans `deploy/docker/sql/migrations/`).

### Enregistrement (shard)

`ServerApp::RecordCharacterSnapshot`, appelé après la sauvegarde de déconnexion
(et à l'arrêt du shard) : l'état complet est écrit dans `character_snapshots`
au format clé=valeur (`SerializeCharacterState` : position, bourse, sac,
équipement, quêtes, métiers, barres…). Le travail part sur le même thread que les
sauvegardes du personnage (2.2), donc toujours après la sauvegarde de départ. Les
**20** instantanés les plus récents sont gardés par personnage.

### Consultation et restauration (master, MJ)

| Commande | Rôle | Effet |
|---|---|---|
| `/snapshots <personnage>` | maître de jeu | Les 10 derniers instantanés : numéro, date UTC, motif, shard |
| `/restore <personnage> <numéro>` | maître de jeu | Pose une demande dans `character_restore_requests` |

Le master **n'écrit pas** l'état du personnage (un seul écrivain, 1.2/2.1). La
demande est appliquée par le shard propriétaire à la **prochaine connexion** du
personnage :

1. au chargement, une demande en attente remplace l'état courant par celui de
   l'instantané (position comprise) ; le courrier en attente reste dû ;
2. une sauvegarde immédiate écrit l'état restauré et marque la demande appliquée
   (`applied_at`), dans la même transaction. Une demande posée pendant la session
   suivante attend la connexion d'après.

Pour un personnage en jeu : le déconnecter (`/kick`), la restauration s'applique
à sa reconnexion. Une nouvelle demande remplace celle en attente.

### Limites

- Le niveau est restauré (la sauvegarde d'un état restauré écrit le niveau tel
  quel ; les autres sauvegardes ne le font que monter). L'orientation du
  personnage n'est pas dans l'instantané.
- Un instantané illisible fait refuser la connexion (journalisé) plutôt que de
  repartir de valeurs par défaut.
- Seul le shard propriétaire enregistre un instantané (contrôle 1.2) ; les
  instantanés visés par une demande en attente ne sont jamais purgés.
- La carte découverte n'est pas dans l'instantané (elle ne fait que grandir).
- Commandes ajoutées à `game/data/config/slash_commands.json`.

### Déploiement

Migration 0080 + master + shard. Client inchangé (les commandes passent par le
chat existant).
