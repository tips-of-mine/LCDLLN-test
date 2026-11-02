# ADM.7 — Quêtes et événements dynamiques

Aide en jeu : `/help quests`. Rôle : **maître de jeu**.

| Commande | Alias | Effet |
|---|---|---|
| `/gmquest add <quête> [joueur]` | `/questadmin` | quête en cours, étapes à zéro |
| `/gmquest ready <quête> [joueur]` | | étapes remplies, à rendre au PNJ |
| `/gmquest complete <quête> [joueur]` | | terminée, récompense versée (XP, or, objets) |
| `/gmquest remove\|reset <quête> [joueur]` | | état oublié (peut être reproposée) |
| `/gmquest status <quête> [joueur]` | | état et avancement des étapes |
| `/gmquest list [joueur]` | | quêtes proposées, en cours, terminées (30 lignes) |
| `/questsearch [mot]` | `/lookupquest` | quêtes chargées : identifiant, PNJ, étapes, XP |
| `/event list` | `/gmevent` | événements du shard : état, phase, participants |
| `/event start <id>` | | démarre (première phase) |
| `/event stop <id>` | | arrête **sans** récompense (mobs retirés, recharge normale) |
| `/event complete <id>` | | termine **avec** récompense des participants |
| `/instance …` | | **prévue** (instances pas encore simulées) |

Le nom `/quest` reste celui de la fenêtre du journal de quêtes (client) : la
gestion par un MJ s'appelle donc `/gmquest`.

## Cohérence

Après chaque changement, `QuestRuntime::SyncQuestStates` recalcule les quêtes
débloquées ou verrouillées (prérequis, exclusions) ; toutes les variations
partent au client (QuestDelta) et le personnage est sauvegardé.
`/gmquest complete` appelle `GrantQuestReward`, comme un rendu au PNJ.
`QuestRuntime::Definitions()` est ajouté pour la recherche.

## Fichiers

`src/shared/server_bootstrap/ServerAppGmQuests.cpp`,
`src/shardd/gameplay/quest/QuestRuntime.h` (`Definitions()`).

## Déploiement

**Master + shard**. Client inchangé, pas de migration.
