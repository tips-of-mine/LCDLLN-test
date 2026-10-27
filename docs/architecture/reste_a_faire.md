# Reste à faire — suivi des chantiers du rapport City of Heroes

Ce document suit, dans le temps, les chantiers du rapport d'analyse City of Heroes
(idées reprises, aucun code) qui ne sont **pas encore livrés**. Les étapes 1 à 5 sont
décrites dans `etape1_master_shard.md` … `etape5_exploitation.md` ; ce qui suit forme
**l'étape 6**, à lancer quand l'exploitation (étape 5) sera stabilisée.

Mettre ce tableau à jour à chaque PR qui avance l'un de ces sujets (statut, PR, date).

| Réf. | Sujet | Statut | Notes |
|---|---|---|---|
| 6.1 (chantier 1.4) | Transfert de personnage en deux temps, avec retour arrière | à faire | Voir ci-dessous. |
| 6.2 (chantier 5.3) | File d'attente de connexion et protection contre la surcharge | à faire | |
| 6.3 | Agent par hôte et placement des processus | à faire | « deux tirages au hasard, on garde le moins chargé ». |
| 6.4 | Commande sur un joueur hors ligne | à faire | Objets joints au courrier (5.6 ne transfère que l'or), sanction MJ. |
| 6.5 | Archivage des personnages inactifs depuis longtemps | à faire | |
| 6.6 | Historique des positions pour juger les tirs | à faire | `LagCompensation` existe mais n'est pas branché. |
| 6.7 | Sauvegardes : ne plus renvoyer courrier réclamé et carte à chaque fois | à faire | Suite de 5.5 : vider `mailboxClaimedIds` et n'envoyer que les cases de carte nouvelles après un COMMIT confirmé au tick. |

## 6.1 — Transfert de personnage en deux temps (chantier 1.4)

**But** : déplacer un personnage d'un shard à un autre (changement de zone servie par un
autre shard, rééquilibrage) sans jamais avoir deux propriétaires ni zéro.

- Phase 1 : le shard d'origine sauvegarde, gèle le personnage et annonce « prêt à
  céder » ; le master réserve le shard cible (verrou en attente, génération + 1).
- Phase 2 : le shard cible charge depuis la base et confirme ; le master bascule
  `owner_shard_id` et renvoie le client vers la cible. Sans confirmation dans le délai,
  retour arrière : l'origine reprend le personnage, la réservation est annulée.
- Point d'appui existant : verrou de propriété avec génération (ETAPE1.2), sauvegarde
  hors tick (ETAPE2.2), billets HMAC (`ShardTicketHandler`).

## 6.2 — File d'attente de connexion et protection contre la surcharge (chantier 5.3)

**But** : quand le cluster est plein, faire attendre plutôt que refuser ou s'effondrer.

- File d'attente côté master avec position et estimation d'attente envoyées au client.
- Débit d'admission borné par shard (jetons par seconde), en plus de la capacité.
- Point d'appui existant : `ShardRegistry::SelectShard` exclut les shards `Degraded` ;
  ETAPE5.4 (prévue) doit faire remonter la charge réelle du shard au master.

## 6.3 — Agent par hôte et placement des processus

**But** : démarrer et surveiller les shards sur plusieurs machines.

- Un petit agent par hôte annonce ses ressources et lance / arrête les processus shard.
- Placement : tirer deux hôtes au hasard et garder le moins chargé (« power of two
  choices »), simple et robuste.
- Aujourd'hui : chaque shard est lancé à la main ou par systemd/Docker.

## 6.4 — Commande sur un joueur hors ligne

**But** : agir sur un personnage qui n'est connecté nulle part.

- Envoi d'un courrier avec pièce jointe à un hors-ligne : l'or passe déjà en deux phases
  (5.6, `mail_transfers`) ; reste les **objets** (champ à ajouter à l'envoi du client,
  verrou de l'objet chez l'expéditeur au prélèvement, dépôt `character_mailbox` au
  retrait) et l'envoi d'or sans être en jeu. Sanction MJ (gel, réduction, bannissement
  de personnage).
- La commande est écrite en base (opération idempotente) et appliquée au prochain
  chargement par le shard propriétaire, comme `character_mailbox`.

## 6.5 — Archivage des personnages inactifs

**But** : garder les tables chaudes petites.

- Après une longue inactivité, déplacer l'état complet vers une table d'archive
  (instantané ETAPE2.6) et libérer le nom selon une règle à définir.
- Restauration à la demande, depuis le portail ou par un MJ.

## 6.6 — Historique des positions pour juger les tirs

**But** : juger un tir au moment où le client l'a vu, pas au moment où il arrive.

- `src/shardd/world/LagCompensation.{h,cpp}` fournit déjà un historique XZ par entité
  (anneau de 1 s à 60 Hz) et `ValidateHit` (retour arrière borné par
  `maxRewindMs`). **Il n'est pas branché** dans `ServerApp` : aucun appel à
  `RecordEntityPosition` ni à `ValidateHit` dans la boucle de tick.
- Reste : enregistrer les positions au tick (joueurs et mobs), utiliser l'horodatage
  client du snapshot vu (ETAPE3.2) pour le retour arrière, borner la tolérance, et
  journaliser les écarts pour la détection de triche.
