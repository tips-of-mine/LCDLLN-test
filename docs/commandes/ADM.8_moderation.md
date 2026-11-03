# ADM.8 — Modération

Aide en jeu : `/help moderation`. Toutes ces commandes sont **exécutées par le
master** et tracées dans le journal d'audit (`gm_command_audit`, ADM.0).

## Nouvelles commandes

| Commande | Alias | Rôle | Effet |
|---|---|---|---|
| `/unmute <joueur\|login>` | | modérateur | lève la sanction de chat (table `chat_mutes`) ; le joueur est prévenu |
| `/mutes` | | modérateur | sanctions de chat en cours (temps restant, motif), 30 lignes |
| `/unban <login>` | | administrateur | compte remis actif (`account_status = 0`) |
| `/bans [mot]` | | modérateur | logins bannis ou verrouillés (statut 1 ou 2), 50 au plus |
| `/warn <joueur> <message>` | | modérateur | « [Avertissement de la modération] … » au joueur, consigné |
| `/history <joueur\|login> [n]` | | modérateur | commandes réservées visant ce joueur ou tapées par ce compte |
| `/notify <message>` | | maître de jeu | « [Annonce] … » à toutes les connexions |
| `/gmw <joueur> <message>` | `/gmwhisper` | modérateur | « [Équipe] votre nom : … » au joueur |
| `/csr <joueur> <commande> [args]` | | administrateur | exécute une commande **du shard** pour le compte du joueur (`/csr Hubert tp lhynn`) |

## Commandes historiques (inchangées)

`/kick`, `/mute`, `/ban`, `/announce`, `/who`, `/report`, `/promote`,
`/shutdown` restent interceptées par le client et traitées par
`AdminCommandHandler` (opcode 195). Elles apparaissent dans `/help` avec leur
rôle d'origine.

## `/csr`

Reprise de City of Heroes (« exécuter une commande comme si on était le
joueur ») : le master relaie la commande au shard du joueur visé en le désignant
comme acteur (`actorCharacterId`), avec le **rôle de l'administrateur**. Seules
les commandes exécutées par le shard sont acceptées. Les réponses reviennent à
l'administrateur.

## Fichiers

| Fichier | Rôle |
|---|---|
| `src/masterd/admin/MasterGmModeration.cpp` | les 9 commandes |
| `src/masterd/admin/GmModerationStore.{h,cpp}` | `chat_mutes` et comptes bannis |

## Déploiement

**Master** (et shard pour `/csr`, déjà prêt depuis ADM.0). Client inchangé, pas de
migration (tables existantes).
