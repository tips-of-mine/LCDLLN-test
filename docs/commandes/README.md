# Référence des commandes de jeu

Générée depuis `game/data/config/slash_commands.json` (source unique, lue par le master).
En jeu : `/help` (15 commandes par page), `/help <commande>` pour la fiche complète, `/search <mot>`.

Rôles : joueur < modérateur < MJ (maître de jeu) < admin. Les commandes « prévues » ne sont pas encore disponibles.

Chantier : [ADM.0](ADM.0_socle_commandes.md) (socle)

## Aide et outils

| Commande | Alias | Rôle | Syntaxe | Description |
|---|---|---|---|---|
| `/help` | `/commands` `/aide` `/?` | joueur | `/help [page] \| /help <commande> \| /help <catégorie> [page]` | Liste paginée des commandes accessibles à votre rôle (15 par page). |
| `/search` | `/apropos` | joueur | `/search <mot>` | Cherche une commande par mot-clé (nom, alias, description). |
| `/gmaudit` |  | admin | `/gmaudit [nombre] [login\|cible]` | Journal des dernières commandes réservées (audit). |
| `/shardinfo` |  | MJ | `/shardinfo` | État du shard de votre personnage (joueurs, mobs, charge). |

## Modération

| Commande | Alias | Rôle | Syntaxe | Description |
|---|---|---|---|---|
| `/kick <player>` |  | modérateur | `/kick <player>` | Force-disconnect a player from the server. |
| `/ban <player> <reason>` |  | MJ | `/ban <player> <reason>` | Ban a player account from the server. |
| `/mute <player> <duration>` |  | modérateur | `/mute <player> <duration>` | Mute a player in chat for a duration. |
| `/announce <message>` |  | MJ | `/announce <message>` | Broadcast a message to all players on the shard. |
| `/uptime` |  | modérateur | `/uptime` | Temps de fonctionnement du serveur master (jours/heures/minutes/secondes). |
| `/snapshots` |  | MJ | `/snapshots` | Liste les 10 derniers instantanés d'un personnage (copies datées prises à chaque déconnexion). |
| `/restore` |  | MJ | `/restore` | Restaure un personnage depuis un instantané (/restore <personnage> <id>) : appliqué par le shard à sa prochaine connexion. |

## Fenêtres

| Commande | Alias | Rôle | Syntaxe | Description |
|---|---|---|---|---|
| `/mail` |  | joueur | `/mail` | Toggle the Mail panel (read inbox, compose, attach items). |
| `/quest` | `/quests` | joueur | `/quest` | Toggle the Quest log panel. |
| `/rep` | `/reputation` | joueur | `/rep` | Toggle the Reputation panel (factions standing). |
| `/skills` | `/skill` | joueur | `/skills` | Toggle the Skill book panel (cooking, herbalism, etc.). |
| `/lfg` |  | joueur | `/lfg` | Toggle the Looking For Group panel. |
| `/arena` |  | joueur | `/arena` | Toggle the Arena panel (team list, queue, match results). |
| `/bg` |  | joueur | `/bg` | Toggle the BattleGround panel (queue, scoreboard). |
| `/pvp` |  | joueur | `/pvp` | Toggle the OutdoorPvP panel (zones, objectives, captures). |
| `/weather` |  | joueur | `/weather` | Toggle the Weather panel (zones, current state). |
| `/events` |  | joueur | `/events` | Toggle the Game Events panel (seasonal events, lunar gates). |
| `/guild` | `/guilds` | joueur | `/guild` | Toggle the Guild panel (members, ranks, bank). |
| `/ah` | `/auction` | joueur | `/ah` | Toggle the Auction House panel (browse, bid, post). |
| `/time` |  | joueur | `/time` | Affiche la date et l'heure du serveur (UTC). |

## Social

| Commande | Alias | Rôle | Syntaxe | Description |
|---|---|---|---|---|
| `/ticket` | `/gmticket` | joueur | `/ticket` | Toggle the GM Ticket panel (submit support ticket, read replies). |
| `/ignore <account_id>` |  | joueur | `/ignore <account_id>` | Add a player to the ignore list (or refresh list when no arg). |
| `/unignore <account_id>` |  | joueur | `/unignore <account_id>` | Remove a player from the ignore list. |
| `/who` |  | joueur | `/who` | List online players matching a filter. |
| `/report <player>` |  | joueur | `/report <player>` | Report a player for misconduct (creates GM ticket). |
| `/friend <player>` |  | joueur | `/friend <player>` | Add a player to friends list. |

## Jeu

| Commande | Alias | Rôle | Syntaxe | Description |
|---|---|---|---|---|
| `/trade` |  | joueur | `/trade` | Toggle the Trade window with the targeted player. |
| `/invite <player>` |  | joueur | `/invite <player>` | Invite a player to your party. |
| `/leave` |  | joueur | `/leave` | Leave the current party. |
| `/pkick <player>` |  | joueur | `/pkick <player>` | Kick a player from your party (party leader only). |

## Débogage

| Commande | Alias | Rôle | Syntaxe | Description |
|---|---|---|---|---|
| `/loot` |  | admin | `/loot` | Toggle the Loot Roll panel + Simulate button (debug). |
| `/sky info` | `/sky` | joueur | `/sky info` | Print current sky state (timeOfDay, sunDir, moonPhase, illumination) to client log. |
| `/sky time <hours>` |  | admin | `/sky time <hours>` | Override local time of day (debug preview, master state inchange). |
| `/sky moon <phase 0..15>` |  | admin | `/sky moon <phase 0..15>` | Override local lunar phase (debug preview, master state inchange). |
| `/packetlog status` |  | admin | `/packetlog status` | Affiche l'etat du PacketLog (active/desactive, capacite, taille courante). |
| `/packetlog dump <connId> <n>` |  | admin | `/packetlog dump <connId> <n>` | Dump on-demand des n derniers paquets RX/TX d'une connexion donnee (defaut 20). |
| `/packetlog dump_all <n>` |  | admin | `/packetlog dump_all <n>` | Dump on-demand des n derniers paquets RX/TX toutes connexions confondues (defaut 20). |

## Administration

| Commande | Alias | Rôle | Syntaxe | Description |
|---|---|---|---|---|
| `/shutdown <seconds|cancel> [reason]` |  | admin | `/shutdown <seconds\|cancel> [reason]` | Ordered cluster shutdown: announce, block new logins, ask every shard to save and stop, then stop the master. /shutdown cancel aborts the countdown. |
| `/promote <account_id> <role>` |  | admin | `/promote <account_id> <role>` | Promote/demote a target account to a given role. |
| `/settime <HH:MM>` |  | admin | `/settime <HH:MM>` | Fixe l'heure du jour (horloge monde serveur, broadcast a tous les clients). |
| `/pausetime <on|off>` |  | admin | `/pausetime <on\|off>` | Gele ou reprend l'horloge monde serveur. |
| `/setlevel <player> <level>` |  | admin | `/setlevel <player> <level>` | Fixe le niveau d'un joueur en ligne, recalcule ses stats (soin complet), re-pousse la fiche et persiste. Commande de test du level-up. |
| `/settimescale <minutes reelles par jour>` |  | admin | `/settimescale <minutes reelles par jour>` | Vitesse du cycle jour/nuit (1..1440 ; 60 = 1 jour de jeu par heure reelle). |
