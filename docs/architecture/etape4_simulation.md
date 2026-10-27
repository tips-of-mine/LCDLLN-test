# Étape 4 — une simulation qui ne coûte que là où l'on joue (shard et cellules)

Suite des étapes 1 (`etape1_master_shard.md`), 2 (`etape2_persistance.md`) et 3
(`etape3_reseau.md`). L'étape 4 reprend le bloc 2 du rapport d'analyse City of Heroes
(idées reprises, aucun code) : une zone sans joueur ne doit presque rien coûter au shard,
et le monde doit pourtant continuer de vivre.

| Ticket | Sujet | Statut |
|---|---|---|
| 4.1 (chantiers 2.2, 2.3) | Sommeil des mobs loin des joueurs, classes de fréquence de l'IA | cette PR |
| 4.2 (chantier 2.6) | Travaux périodiques étalés par identifiant | à venir |
| 4.3 (chantiers 2.1, 2.4) | Cellules réellement froides | à venir |
| 4.4 (chantier 2.7) | Agenda déterministe des PNJ | à venir |
| 4.5 (chantier 2.5) | Délestage avec hystérésis | à venir |

Aucun ticket de l'étape 4 ne change le protocole : **seul le shard est à redéployer**,
le client et le master ne changent pas.

## 4.1 — Sommeil des mobs et classes de fréquence (chantiers 2.2, 2.3)

### Avant

Chaque mob vivant exécutait son IA à `server.mob_ai_hz` (10 Hz) : recherche d'une cible,
errance, routine des PNJ d'ambiance. C'était vrai même sans aucun joueur à proximité, et
même pour un mob qui attend au repos.

### Principe

`src/shardd/world/MobActivity` (module pur, testé seul) décide, `ServerApp::UpdateMobAi`
applique.

- **Classes de fréquence** :

  | Classe | Mobs concernés | Fréquence (défaut) | Clé |
  |---|---|---|---|
  | Combat | cible d'aggro, menace, retour au point d'attache | 10 Hz | `server.mob_ai_hz` |
  | Repos | mob hostile qui attend ou erre | 5 Hz | `server.mob_ai.calm_hz` |
  | Ambiance | PNJ d'ambiance (routine) | 5 Hz | `server.mob_ai.ambient_hz` |

  Le pas de déplacement couvre le temps **réellement écoulé** depuis la décision
  précédente (1 s au plus) : un mob traité 2 fois moins souvent fait des pas 2 fois plus
  longs, sa vitesse au sol ne change pas. Les fréquences de repos et d'ambiance ne
  dépassent jamais celle du combat.
- **Sommeil** : un mob sans joueur à moins de `server.mob_ai.wake_radius_m` (600 m)
  depuis `server.mob_ai.sleep_delay_s` (3 s), et qui n'est pas occupé, s'endort. Il
  n'exécute plus ni IA ni déplacement, et sa vitesse est remise à zéro pour les clients.
  - Le voisinage est vérifié **une fois par seconde**, étalé par identifiant d'entité (1/20
    des mobs à chaque tick à 20 Hz).
  - Un mob **occupé** (dégâts, menace, provocation, retour en cours, PNJ qui rentre chez
    lui à la fin de son horaire) ne s'endort jamais et se réveille sur-le-champ : un sort
    tiré de très loin lui donne de la menace.
  - Au réveil, aucun rattrapage du temps dormi : il reprend là où il s'était arrêté.
  - `wake_radius_m = 0` désactive le sommeil.

Le rayon n'est **jamais en deçà de la portée de l'intérêt** (cellule du joueur ± 3
cellules de 100 m, soit ≈ 566 m en diagonale) : le shard relève une valeur plus petite.
Un mob encore répliqué vers un joueur reste donc éveillé, pas de « statue » sous ses yeux.
Comme un spawner ne garde ses mobs que dans cette même portée, le sommeil vise surtout
les mobs qui se sont éloignés (errance, routine) et ceux des spawners en attente de
retrait (mob en combat récent). Les mobs n'ont pas d'aggro de proximité : un mob n'attaque
qu'après avoir reçu de la menace, et la menace le réveille sur-le-champ.

**Correction au passage** : `MoveMobTowards` renvoyait « arrivé » après chaque pas (le
résultat de la grille, presque toujours vrai). Une errance s'arrêtait après un seul pas
puis marquait sa pause de 2 à 6 s, et un retour au point d'attache repassait au repos loin
de chez lui. Elle ne renvoie plus « arrivé » qu'à l'arrivée : les mobs marchent vraiment
jusqu'à leur cible.

### Exploitation

La ligne `Tick stats` du shard (1 fois par seconde) donne `mobs=` et `mobs_endormis=`.
Loin des joueurs, les mobs endormis doivent dominer.

### Fichiers

| Fichier | Rôle |
|---|---|
| `src/shardd/world/MobActivity.h/.cpp` | Classes de fréquence, rattrapage du temps, sommeil, étalement de la vérification |
| `src/shared/server_bootstrap/ServerApp` | `UpdateMobAi` (boucle), `IsPlayerNearMob`, `MoveMobTowards` (pas = temps couvert), lecture des réglages dans `InitSpawners` |

### Tests

`mob_activity_tests` : classes et intervalles, bornes des réglages, rattrapage (même
distance par seconde à 10 Hz et à 5 Hz), endormissement après 3 s, réveil par un joueur,
mob occupé jamais endormi et réveillé sur-le-champ, sommeil désactivé, vérification
étalée (200 entités : 10 vérifications par tick).

### Déploiement

Shard seul. Pas de migration. Nouvelles clés optionnelles : `server.mob_ai.calm_hz`,
`server.mob_ai.ambient_hz`, `server.mob_ai.wake_radius_m`, `server.mob_ai.sleep_delay_s`.
