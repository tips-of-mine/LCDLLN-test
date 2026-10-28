# FIX.5 — Hôtel des ventes, cycle lunaire, ré-authentifications du flux client

Origine : logs serveur du 28/10/2026 (anomalies 5, 6 et 7 de l'analyse).

## Hôtel des ventes : erreur sans cause

Symptôme (shard) : `Auction listings load FAILED (migration 0081 appliquée ?) : hôtel des
ventes vide`, alors que le master est au schéma 82.

La requête fonctionne sur une base de test (MariaDB 10.11, migration 0081) : la cause en
production n'est pas visible dans les logs. Correction : le message contient désormais la
cause MySQL exacte (`SqlPreparedStatement::LastError()`, ou `mysql_error` si le statement
n'a pas pu être préparé) et le `shard_id`. Une lecture réussie est journalisée
(`Auction listings : lecture en base OK`). Le WARN `Load produced empty store` du fichier
historique `auction/listings.ini` passe en INFO : avec la base, son absence est normale.

Vérification côté serveur (n'affiche que les tables, aucun secret) :

```bash
docker exec lcdlln-mysql sh -c 'mysql -N -u"$MYSQL_USER" -p"$MYSQL_PASSWORD" "$MYSQL_DATABASE" -e "SHOW TABLES LIKE \"auction%\"" 2>/dev/null'
```

## Cycle lunaire (master)

- Le premier calcul de phase au démarrage se faisait **avant** le branchement de l'horloge
  monde : phase en temps réel (7, pleine lune) puis, quelques minutes plus tard, en temps de
  jeu (1) ⇒ faux « changement de phase » diffusé aux clients. Le calcul de démarrage est
  maintenant fait après `SetWorldClock`.
- `nextChangeTs` valait toujours 0 avec l'horloge monde : il est calculé à partir de la fin
  de la phase (temps de jeu) convertie au rythme courant de l'horloge (0 si l'horloge est en
  pause ; approximatif si `/timescale` change ensuite, le push 194 reste la référence).
- Le message de démarrage « cycle 14j » est corrigé : `world.lunar.cycle_duration_ms` ne
  sert qu'au repli temps réel ; avec l'horloge monde, la période est
  `game.worldclock.lunar_period_game_days` (16 jours de jeu par défaut).

## Ré-authentifications du flux client (master)

Le client s'authentifie trois fois par connexion (écran de connexion, liste des serveurs,
entrée en jeu) ; chaque authentification remplace la session précédente, dont la connexion
TCP est déjà fermée. Le master journalisait `KickedByDuplicateLogin` et « cascading close »,
comme si un joueur avait été déconnecté.

Correction : nouvelle raison `SessionCloseReason::ReplacedByReauth`, choisie quand la
connexion de l'ancienne session est déjà fermée (`SetSessionConnectionOpenHook`, alimenté
par le `ConnectionClosedHandler` du master). Le remplacement lui-même ne change pas (même
politique, même protection du joueur en jeu) ; seul le nettoyage est silencieux (DEBUG).
Une session dont la connexion est encore ouverte reste `KickedByDuplicateLogin`.

Reste à faire (6.8) : réutiliser la session du premier écran dans `MasterShardClientFlow`
pour n'avoir qu'une authentification (changement client + protocole de reprise).
