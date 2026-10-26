# Étape 2 — fiabiliser la persistance des personnages

Suite de l'étape 1 (`etape1_master_shard.md`) : depuis 1.2, un seul shard est
propriétaire d'un personnage. L'étape 2 fait de la base la seule source de vérité
et retire le travail SQL du tick de jeu.

| Ticket | Sujet | Statut |
|---|---|---|
| 2.1 (chantier 3.1) | Base = seule source de vérité (fin des `.ini`, fin de l'écriture de position par le master) | cette PR |
| 2.2 (chantier 3.2) | SQL hors du tick (file de sauvegarde par personnage) | PR suivante |
| 2.3 (chantier 3.3) | Sauvegardes automatiques étalées | PR suivante |
| 2.4 (chantier 3.4) | Échanges et transferts : les deux personnages dans une transaction | PR suivante |

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

Sans base (développement sans MySQL, shard Windows), le fichier `.ini` reste le
seul stockage, comme avant.

### Chargement (Hello)

1. La base d'abord. Si `character_progress` a une ligne, la base contient tout :
   le fichier est ignoré.
2. Sinon (personnage créé avant cette version), le fichier `.ini` est lu une
   fois. Si la base avait déjà bourse, sac et équipement (version Roadmap-10),
   ces trois domaines restent ceux de la base.
3. La sauvegarde suivante écrit tout en base : le personnage est migré, le
   fichier ne sera plus relu.

### Courrier

`DepositMailboxDelivery` (remboursement d'enchère, objet gagné, vente expirée)
ajoute une ligne `character_mailbox` sans lire l'état du destinataire, qui peut être
en jeu sur un autre shard. À la connexion, le shard propriétaire lit les lignes,
les ajoute à la bourse et au sac, et retient le plus grand `id` lu
(`mailboxClaimUpToId`). Chaque sauvegarde supprime `id <=` cette valeur dans la
même transaction que la bourse et le sac : le courrier n'est ni perdu ni compté
deux fois, et celui arrivé après la connexion reste pour la suivante.

### Master

`CharacterSavePositionHandler` n'écrit plus rien : il vérifie la session et
acquitte la requête, pour que les clients existants ne restent pas en attente.
La position est écrite par le shard propriétaire.

### Limites

- La carte découverte (`character_map_discovery`) reste écrite par le master à la
  demande du client.
- Les anciens fichiers `.ini` restent sur le disque (jamais supprimés
  automatiquement) ; ils peuvent être archivés une fois tous les personnages migrés.
- Instantanés restaurables (chantier 3.6) : pas encore.

### Déploiement

Migration 0078 + shard + master. Client inchangé.
