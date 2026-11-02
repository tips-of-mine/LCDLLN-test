# ADM.5 — Monture et apparence

Aide en jeu : `/help appearance`.

| Commande | Rôle | État | Effet |
|---|---|---|---|
| `/mount [on\|off\|<vitesse 1-3>] [joueur]` | maître de jeu | disponible | monté : vitesse ×1,6 (ou la valeur donnée), cumulée avec `/speed` |
| `/dismount [joueur]` | maître de jeu | disponible | = `/mount off` |
| `/scale <0.5-3> [joueur]` | maître de jeu | **prévue** | taille du personnage |
| `/morph <archétype> [joueur]`, `/demorph` | maître de jeu | **prévue** | apparence d'une créature |

## Ce que fait la monture aujourd'hui

C'est la **mécanique** d'une monture : `GmPlayerState::mounted` et
`mountSpeedMultiplier` (ADM.1) multiplient la vitesse autoritaire du shard et la
vitesse locale du client (GmStateUpdate, drapeau `kGmFlagMounted`). Le
personnage garde son apparence et ses animations de course.

## Ce qui reste à faire pour une vraie monture

1. Modèles 3D et animations de montures (contenu).
2. Objet « monture » (catalogue d'objets, apprentissage, invocation par le joueur
   lui-même : `/mount` passerait alors au rôle joueur pour une monture possédée).
3. Réplication de l'état monté et du modèle de monture dans les snapshots, pour
   que les autres joueurs voient le cavalier.

`/scale` et `/morph` demandent le même ajout au format de réplication (échelle et
modèle affiché d'une entité) : elles sont déclarées « prévues » dans le registre
(fiche visible par `/help /scale`, absentes des listes).

## Fichiers

`src/shared/server_bootstrap/ServerAppGmAppearance.cpp`.

## Déploiement

**Master + shard** (client déjà prêt depuis ADM.1). Pas de migration.
