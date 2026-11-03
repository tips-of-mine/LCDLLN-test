# ADM.0 — Socle des commandes de jeu

Premier ticket du chantier « commandes de jeu » (ADM.0 à ADM.9) : il pose
l'aide paginée, le relais des commandes vers les shards et l'audit. Les tickets
suivants ajoutent les commandes elles-mêmes (déplacement, personnage, objets,
monde, quêtes, modération, serveur).

## Ce que voit le joueur

| Commande | Rôle | Effet |
|---|---|---|
| `/help` (alias `/commands`, `/aide`, `/?`) | joueur | page 1 de la liste des commandes accessibles à son rôle, **15 par page** |
| `/help 3` | joueur | page 3 |
| `/help <commande>` | joueur | fiche : syntaxe, explication, exemple, rôle minimum, alias (`/help tp` = `/help /tp`) |
| `/help <catégorie> [page]` | joueur | liste d'une seule catégorie (`/help movement`, `/help moderation 2`…) |
| `/search <mot>` (alias `/apropos`) | joueur | commandes dont le nom, un alias, la description ou l'aide contient le mot |
| `/gmaudit [n] [login\|cible]` | administrateur | dernières commandes réservées et refus (table `gm_command_audit`) |
| `/shardinfo` | maître de jeu | état du shard de son personnage (joueurs, mobs, charge) |

Les noms de commandes sont en **anglais** ; les textes d'aide et les réponses
sont en français. Une commande inconnue répond « Commande inconnue : /x — tapez
/help pour la liste ou /search <mot> ». Un rôle insuffisant répond « Vous
n'avez pas le rôle requis pour /x (voir /help /x) ».

## Source unique : `game/data/config/slash_commands.json`

Chaque commande y a une entrée. Champs utilisés par l'aide :

| Champ | Rôle |
|---|---|
| `command` | nom canonique, sans paramètres (`"/tp"`) |
| `aliases` | autres noms acceptés |
| `description` | phrase courte affichée dans les listes |
| `usage` | syntaxe complète (`"/tp <x> <z> \| /tp <lieu>"`) |
| `help` | explication détaillée (paramètres, unités, effets) |
| `example` | exemple prêt à taper |
| `category` | `core`, `movement`, `information`, `character`, `items`, `appearance`, `world`, `quests`, `moderation`, `server` (+ catégories historiques) |
| `minRole` | `player` < `moderator` < `game_master` < `administrator` |
| `status` | `server_route` (exécutée par le master), `shard_command` (exécutée par le shard), `planned` (prévue : masquée des listes, fiche consultable), statuts historiques |

Le fichier est lu au démarrage du master (`server.slash_commands_path`).
La recherche d'une commande est désormais **sans casse** et le `/` est
facultatif. Une forme déclarée avec un paramètre facultatif (`"/help [commande]"`)
est correctement indexée sous `/help` (elle ne l'était pas avant ADM.0).

## Chemin d'une commande exécutée par le shard

```
Client ── chat « /tp 100 200 » ──► Master (ChatRelayHandler)
                                     │ ChatCommandRouter : rôle ≥ minRole ?
                                     │ MasterGmCommands::ForwardToShard
                                     │ GmShardRelay : requestId, attente
                                     ▼
                                   opcode 216 ──► Shard (ShardToMasterClient)
                                                    │ ServerApp::EnqueueGmCommand
                                                    │ tick : ProcessGmCommands
                                                    │ GmCommandExecutor (ServerAppGm.cpp)
                                   opcode 217 ◄────┘ lignes de réponse
                                     │ GmShardRelay::OnResult
Client ◄── lignes « system » ──────┘
```

- Le master **vérifie le rôle** ; le shard le **revérifie** (catalogue
  `src/shardd/gameplay/gm/GmCommandCatalog.h`).
- La réponse 217 n'est acceptée que sur la connexion d'un shard enregistré, pour
  une demande en attente adressée à **ce** shard et à **ce** compte (pas
  d'écriture dans le chat d'un autre joueur).
- Sans réponse au bout de `master.gm_commands.shard_timeout_sec` secondes
  (défaut 8), l'appelant est prévenu (« Le shard n'a pas répondu… »).
- Au plus 8 commandes en vol par compte ; au plus 16 commandes exécutées par
  tick du shard (le reste attend le tick suivant).
- Les lignes de réponse sont découpées à 250 octets (limite d'une ligne de chat).

## Audit

Migration `sql/migrations/0085_gm_command_audit.sql` : table `gm_command_audit`.
Une ligne est écrite pour chaque commande dont le rôle minimum est modérateur ou
plus (exécutée par le master ou relayée au shard), et pour chaque refus faute de
rôle, quelle que soit la commande. La cible nommée (premier argument non
numérique, `@nom` compris) est mémorisée pour filtrer. Les deux journaux
techniques gardent aussi une trace : `[GmAudit]` (master) et `[GmCommand]`
(shard).

## Ajouter une commande

1. **Commande exécutée par le shard** :
   - une ligne dans `kGmShardCommands` (`GmCommandCatalog.h`) avec le rôle minimum ;
   - l'entrée JSON avec `"status": "shard_command"`, le **même** `minRole`, une
     description, un `usage`, une aide (`help`) et un exemple ;
   - la méthode `Cmd…()` de `GmCommandExecutor` et sa ligne dans `kGmHandlers`
     (`src/shared/server_bootstrap/ServerAppGm.cpp`).
   Le relais master est automatique (aucun code master à écrire).
2. **Commande exécutée par le master** : l'entrée JSON `"status": "server_route"`
   et l'enregistrement dans `MasterGmCommands` (`RegisterEx`).

Le test `command_help_tests` casse la CI si le JSON et le catalogue du shard ne
concordent pas (nom ou rôle), si un nom ou un alias est en double, ou si une
entrée ADM n'a pas d'aide complète.

## Fichiers

| Fichier | Rôle |
|---|---|
| `src/shared/admin/GmCommandText.h` | découpe des arguments (guillemets), nombres (`,` ou `.`), on/off, durées, lignes de chat |
| `src/masterd/chat/CommandHelp.{h,cpp}` | réponses de `/help` et `/search` |
| `src/masterd/chat/ChatCommandRouter.{h,cpp}` | `RegisterEx` (contexte : connexion, session, rôle), rappel d'audit |
| `src/masterd/admin/SlashCommandRegistry.{h,cpp}` | champs `usage`, `help`, `example` ; recherche sans casse |
| `src/masterd/admin/MasterGmCommands.{h,cpp}` | enregistrement des commandes master, relais, audit |
| `src/masterd/admin/GmShardRelay.{h,cpp}` | corrélation 216/217, délais, anti-rafale |
| `src/masterd/admin/GmAuditStore.{h,cpp}` | lecture / écriture de `gm_command_audit` |
| `src/shared/network/ShardPayloads.{h,cpp}` | paquets 216 / 217 |
| `src/shardd/gameplay/gm/GmCommandCatalog.h` | commandes connues du shard + rôle |
| `src/shared/server_bootstrap/ServerAppGm.cpp` | exécuteur des commandes au tick du shard |

## Tests

- `gm_command_text_tests` — outils texte.
- `command_help_tests` — pagination par 15, filtrage par rôle, fiches, recherche,
  cohérence JSON ↔ catalogue du shard.
- `gm_shard_relay_tests` — paquets 216/217 (aller-retour, bornes), relais
  (corrélation, réponse d'un autre shard ou d'un autre compte ignorée, délai,
  anti-rafale).
- `chat_command_router_tests` — `RegisterEx`, contexte et audit.

## Déploiement

Lock-step **master + shard** : nouveaux opcodes 216/217, nouveaux handlers,
migration 0085. Le client n'est pas modifié (les commandes passent par le chat).
