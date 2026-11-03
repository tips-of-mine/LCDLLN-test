# ADM.4 — Objets et argent (maître de jeu)

Aide en jeu : `/help items`. Sans nom de joueur, la commande vise l'appelant.

| Commande | Alias | Effet |
|---|---|---|
| `/additem <objet\|n°> [qté] [joueur]` | `/give` | ajoute au sac (1 à 10 000 ; refus si 64 piles) |
| `/removeitem <objet\|n°> [qté] [joueur]` | | retire du sac (au plus ce qui est possédé) |
| `/money <±montant> [gold\|honor\|badges\|premium] [joueur]` | `/gold` | ajoute ou retire (plafonds de la bourse, jamais négatif) |
| `/inventory [joueur]` | `/inv` | piles du sac (40 lignes) + or, honneur, insignes |
| `/equipment [joueur]` | `/equip` | objets portés par emplacement |
| `/clearbag [joueur] confirm` | | vide le sac (« confirm » obligatoire) ; l'équipement porté reste |
| `/itemsearch <mot\|n°>` | `/lookupitem`, `/finditem` | 25 objets du catalogue au plus : numéro, nom, emplacement |

## Désigner un objet

Par son **numéro** (catalogue du shard, identique au client) ou par son **nom**,
sans majuscules ni accents : nom exact, puis début, puis partie du nom. Un nom de
plusieurs mots s'écrit entre guillemets (`/additem "potion de soin" 5`). Si
plusieurs objets correspondent sans nom exact, la réponse les liste avec leur
numéro.

## Effets

- Le sac est renvoyé **en entier** au client (`InventoryDelta` est un instantané
  complet) ; la bourse par `WalletUpdate` (via `TryAddCurrency` /
  `TrySubtractCurrency`, qui appliquent les plafonds).
- Chaque modification est sauvegardée (`SaveConnectedClient`, motifs `gm_additem`,
  `gm_removeitem`, `gm_clearbag`, `wallet_add`…).
- Le joueur visé est prévenu (don, retrait d'argent, sac vidé).

## Fichiers

`src/shared/server_bootstrap/ServerAppGmItems.cpp` (7 commandes).

## Déploiement

**Master + shard** en lock-step. Client inchangé, pas de migration.
