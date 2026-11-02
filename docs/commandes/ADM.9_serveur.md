# ADM.9 — Administration du serveur

Aide en jeu : `/help server`.

| Commande | Rôle | Exécutée par | Effet |
|---|---|---|---|
| `/serverinfo` | modérateur | master | démarrage, durée de fonctionnement, connexions, joueurs, shards, accès, message du jour |
| `/motd` | joueur | master | affiche le message du jour |
| `/setmotd <texte>\|clear` | administrateur | master | change et diffuse le message du jour (envoyé ensuite à chaque entrée en jeu) |
| `/closed [on\|off] [motif]` | administrateur | master | ferme / rouvre le serveur aux joueurs |
| `/saveall` | administrateur | shard | sauvegarde tous les personnages du shard |
| `/reload …` | administrateur | — | **prévue** |
| `/restart …` | administrateur | — | **prévue** (en attendant : `/shutdown` puis relance) |

Commandes historiques conservées : `/shutdown <secondes|cancel> [motif]`,
`/promote`, `/settime`, `/pausetime`, `/settimescale`, `/uptime`, `/snapshots`,
`/restore`.

## Message du jour

Valeur de départ : clé **`master.motd`** de `config.json` (vide = aucun).
`/setmotd` la remplace jusqu'au redémarrage du master. Le message est envoyé à
chaque entrée en jeu validée (`CharacterEnterWorldHandler::SetEnteredWorldCallback`).

## Serveur fermé

`/closed on` : `CharacterEnterWorldHandler` et `ShardTicketHandler` refusent les
comptes sans rôle de **modérateur ou plus** (nouveau `SetAccountAdmissionGate`) ;
le joueur reçoit « Le serveur est temporairement fermé aux joueurs
(maintenance) ». Les joueurs déjà en jeu restent connectés ; le personnel est
rappelé à l'ordre à son entrée. L'état n'est pas conservé au redémarrage du
master.

## Fichiers

| Fichier | Rôle |
|---|---|
| `src/masterd/admin/MasterGmServer.cpp` | /serverinfo, /motd, /setmotd, /closed |
| `src/shared/server_bootstrap/ServerAppGm.cpp` | /saveall |
| `src/masterd/handlers/character/CharacterEnterWorldHandler.{h,cpp}` | refus par compte, rappel d'entrée en jeu |
| `src/masterd/handlers/shard/ShardTicketHandler.{h,cpp}` | refus de ticket par compte |

## Déploiement

**Master + shard** (gating de l'entrée en jeu modifié, nouvelle clé de
configuration facultative `master.motd`). Client inchangé, pas de migration.
