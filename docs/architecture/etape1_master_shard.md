# Étape 1 — remettre chaque responsabilité à sa place (master / shard)

Plan d'amélioration issu de l'analyse de l'architecture serveur de City of Heroes
(idées reprises, aucun code). L'étape 1 regroupe trois chantiers, chacun livré dans
sa propre PR :

| Chantier | Sujet | Statut |
|---|---|---|
| 1.1 | `masterd` relais pur : plus aucune simulation de gameplay dans le master | cette PR |
| 1.2 | Un seul propriétaire par personnage (verrou attribué par le master) | PR suivante |
| 5.1 | Chat de proximité (Dire, Crier) au shard, canaux globaux au master | PR suivante |

Règle d'architecture visée : **`masterd` est un relais pur, sans simulation de
gameplay ; toute la simulation vit dans `shardd`.**

## 1.1 — `masterd` relais pur

### Ce qui a été retiré du master

Ces handlers simulaient du gameplay « V1 » dans le master, souvent avec des
données factices, et doublonnaient le shard pour une partie d'entre eux :

| Fonction | Ancien handler | Ce qu'il faisait | Côté shard |
|---|---|---|---|
| Échange | `TradeHandler` + `TradeSessionRegistry` | Machine à états, aucun transfert réel | Échange complet en UDP (`/trade`, `HandleTrade*`) |
| Hôtel des ventes | `AuctionHandler` | 8 annonces codées en dur, économie cosmétique | Hôtel des ventes UDP (`HandleAuction*`, MessageKind 47-51) |
| Guildes V1 | `GuildHandler` | 2 guildes codées en dur, lecture seule | `GuildSystem` (à brancher en UDP) |
| Jets de loot | `LootHandler` | Dés aléatoires, 5 objets codés en dur | Ramassage + mode de butin (`/loot`) |
| Compétences | `SkillHandler` | Lot de départ codé en dur, usage simulé | Métiers, artisanat, récolte (`HandleLearnProfession`, `HandleCraft*`) |
| Arène | `ArenaHandler` | Match contre une IA fictive, 50 % de victoire | à construire |
| Champs de bataille | `BattleGroundHandler` | Match contre un bot, score simulé | à construire |
| JcJ extérieur | `OutdoorPvpHandler` | Capture instantanée simulée | à construire |
| Météo | `WeatherHandler` | Tirage aléatoire à chaque abonnement | à construire (le client a son système local) |
| Événements | `GameEventHandler` | 4 événements saisonniers codés en dur | `DynamicEvents` du shard |
| Recherche de groupe | `LfgHandler` | File sans appariement réel | à construire |
| Objets interactifs | `InteractiveHandler` | Relais sans validation, jamais appelé par le client | à construire |

Supprimés aussi : les stores MySQL qui ne servaient qu'à ces handlers
(`MysqlAuctionStore`, `MysqlArenaStore`, `MysqlGameEventStore`, `MysqlSkillStore`,
`MysqlOutdoorPvpStore`, `MysqlGuildStore`, `MysqlBattleGroundStore`, `MysqlLootStore`).

**Les tables SQL sont conservées** (aucune migration de suppression) : `guild_members_v2`
est encore lue à l'entrée en jeu, et les autres serviront de base quand ces fonctions
seront reconstruites côté shard. Les en-têtes de logique pure `src/masterd/lfg/LfgQueue.h`
et `src/masterd/events/GameEventManager.h` restent en place (tests existants), sans être
branchés.

### Réponse aux anciens opcodes

`src/masterd/handlers/retired/RetiredGameplayHandler.{h,cpp}` et
`RetiredGameplayOpcodes.cpp` : les 41 opcodes de requête de ces fonctions reçoivent un
paquet `ERROR` (`UNKNOWN_OPCODE`, message « fonction … retirée du master ») au lieu
d'être ignorés en silence. Un client ancien ne reste donc pas bloqué jusqu'au timeout.
Test : `retired_gameplay_handler_tests`.

### Ce que le master garde, et pourquoi

| Fonction | Raison |
|---|---|
| Authentification, sessions, CGU, mot de passe | Compte |
| Création, liste, suppression, entrée en jeu des personnages | Persistance et routage vers le shard |
| Enregistrement des shards, tickets, liste des serveurs | Relais |
| Chat, courrier, liste d'ignorés, tickets MJ, commandes d'administration | Relais et persistance par compte |
| Réputation (`ReputationHandler`) | Lecture de `account_reputation` (persistance du compte) |
| Cinématiques vues (`CinematicHandler`) | Persistance du compte (`cinematic_seen`) |
| Exploits, carte découverte | Persistance |
| Horloge du monde, phase lunaire | Autorité de temps partagée, calcul déterministe (pas de simulation) |

### Côté client

`Engine.cpp` : les presenters des fonctions retirées restent initialisés, mais leur
callback d'envoi n'appelle plus le master (`retiredMasterFeature`). Une requête échoue
proprement (message d'erreur du presenter, une ligne de log) jusqu'au branchement de
chaque fonction sur le shard en UDP.

### Déploiement

Master et client à déployer ensemble : le master ne répond plus à ces opcodes et le
nouveau client ne les envoie plus. Aucun changement de protocole UDP, aucune migration.

### CI

Les workflows `build-linux`, `build-windows` et `trivy` tournent maintenant aussi sur
`develop` (push et PR), pour valider chaque PR avant la promotion vers `main`.
