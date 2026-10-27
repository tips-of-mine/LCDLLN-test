# Étape 4 — une simulation qui ne coûte que là où l'on joue (shard et cellules)

Suite des étapes 1 (`etape1_master_shard.md`), 2 (`etape2_persistance.md`) et 3
(`etape3_reseau.md`). L'étape 4 reprend le bloc 2 du rapport d'analyse City of Heroes
(idées reprises, aucun code) : une zone sans joueur ne doit presque rien coûter au shard,
et le monde doit pourtant continuer de vivre.

| Ticket | Sujet | Statut |
|---|---|---|
| 4.1 (chantiers 2.2, 2.3) | Sommeil des mobs loin des joueurs, classes de fréquence de l'IA | livré |
| 4.2 (chantier 2.6) | Travaux périodiques étalés par identifiant | livré |
| 4.3 (chantiers 2.1, 2.4) | Cellules réellement froides | livré |
| 4.4 (chantier 2.7) | Agenda déterministe des PNJ | livré |
| 4.5 (chantier 2.5) | Délestage avec hystérésis | cette PR |

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

## 4.2 — Travaux périodiques étalés par identifiant (chantier 2.6)

### Avant

À chaque tick (20 par seconde), le shard évaluait **tous** les spawners : activation par
les joueurs (parcours de tous les joueurs), retraits, apparitions. Il recalculait aussi la
carte découverte de chaque joueur et parcourait toutes les annonces de l'hôtel des ventes.

### Principe

`src/shardd/world/TickSlicing.h` (en-tête seul, testé seul) : l'objet d'identifiant `id`
est traité au tick `t` si `(t + id) % période == 0`. Chaque objet est traité **exactement
une fois par période**, et la charge se répartit sur les ticks de la période au lieu d'un
pic.

| Travail | Identifiant | Fréquence (défaut) | Clé |
|---|---|---|---|
| Évaluation d'un spawner (activation, retraits, apparitions) | indice du spawner | 2 Hz | `server.spawners.evaluate_hz` |
| Carte découverte d'un joueur | `client_id` | 4 Hz | `server.map_discovery_hz` |
| Échéances des enchères | fixe | 1 Hz | — |
| Voisinage d'un mob (sommeil, 4.1) | identifiant d'entité | 1 Hz | — |
| Sauvegarde automatique (2.3) | clé du personnage | 1 / 30 s | `server.character_autosave_seconds` |

Conséquences, voulues et bornées :

- Une apparition, un retrait ou une réapparition de mob arrive **au plus 0,5 s** plus tard
  (une période de spawner).
- La carte se découvre par pas de 3 m au plus (13 m/s × 0,25 s), bien moins qu'une case.
- Une enchère se règle au plus 1 s après son échéance ; entre-temps, une annonce échue
  refuse toute offre et tout achat immédiat (« That auction has ended. »).
- Une réapparition de mob arrive au plus ≈ 1 s après `respawnSeconds` (retrait du mob
  mort puis apparition, chacun à son tour de spawner).

Un fréquence ≥ celle du tick (ou ≤ 0) revient à traiter chaque objet à chaque tick.

### Fichiers

| Fichier | Rôle |
|---|---|
| `src/shardd/world/TickSlicing.h` | `PeriodTicks`, `IsSliceTurn` |
| `src/shared/server_bootstrap/ServerApp` | `UpdateSpawners`, `UpdateMapDiscovery`, `ProcessAuctionHouseTick` ; périodes lues dans `InitSpawners` |
| `src/shardd/world/MobActivity.cpp` | La vérification de voisinage (4.1) passe par le même étalement |

### Tests

`tick_slicing_tests` : périodes (1, 2, 4, 0,1 Hz, bornes, tick à 0 Hz), chaque objet une
fois par période quel que soit le tick de départ, 1 000 objets sur 10 ticks = 100 par
tick.

### Déploiement

Shard seul. Pas de migration. Nouvelles clés optionnelles : `server.spawners.evaluate_hz`,
`server.map_discovery_hz`.

## 4.3 — Cellules réellement froides (chantiers 2.1, 2.4)

### Avant

Une région (cellule de 10 km) inactive n'était qu'une étiquette : `UpdateSpawners`
parcourait **tous** les spawners du monde, et pour chacun d'une région inactive vérifiait
qu'il n'avait plus de mob vivant. `GridStateTracker` (états Loaded / Active / Idle /
Removal) existait mais n'était utilisé que par ses tests. Un mob disparaissait dès que
son spawner n'avait plus de joueur à portée, et réapparaissait au retour du joueur : un
joueur qui oscille en bordure faisait clignoter les mobs.

### Principe

- **Index par région** : à l'initialisation, chaque spawner du monde en cellules est
  rangé dans sa région (`m_spawnersByRegion`) ; ceux des zones historiques restent dans
  une liste à part, toujours évaluée.
- **Cycle de vie des régions** (`src/shardd/world/RegionLifecycle`, module pur, qui
  s'appuie sur `GridStateTracker`) mis à jour une fois par seconde avec les régions
  actives de `RegionActivity` :

  | État | Quand | Spawners |
  |---|---|---|
  | Active | un joueur dans la région ou à moins de 1 000 m de son bord | évalués |
  | Loaded | plus active depuis moins de `world.region_idle_s` (60 s) | évalués (retraits, retour rapide) |
  | Idle (froide) | plus active depuis 60 s | **plus parcourus du tout** |
  | Removal | plus active depuis `world.region_unload_s` (300 s) | région oubliée du suivi |

  Une région jamais visitée est froide d'office. Une région froide qui a encore un mob
  vivant (combat qui se prolonge) reste parcourue jusqu'à son retrait : aucun mob n'est
  abandonné.
- **Liste des spawners évalués** (`m_evaluatedSpawners`) recalculée chaque seconde :
  hors cellules + régions chaudes + régions froides avec un mob vivant. `UpdateSpawners`
  ne parcourt plus qu'elle, toujours étalée (4.2). Le coût suit les zones jouées, pas la
  taille du monde.
- **Délai de grâce** (chantier 2.4) : un spawner qui n'a plus de joueur à portée garde ses
  mobs `server.spawners.despawn_grace_s` (20 s) avant de les retirer. Il ne fait pas
  réapparaître de mob pendant ce délai. Un joueur qui oscille en bordure ne fait plus
  clignoter les mobs. Les mobs n'apparaissent toujours qu'avec un joueur à portée du
  spawner (± 3 cellules de 100 m).
- **Déchargement** : à 300 s, la région sort du suivi (ses mobs sont déjà retirés). Les
  délais de réapparition en cours **continuent** : les remettre à zéro offrirait un
  raccourci pour « farmer » un mob rare (s'éloigner quelques minutes puis revenir).
- **Marge des régions** : `world.region_activation_margin_m` ne descend jamais sous la
  portée de l'intérêt (400 m) : un spawner visible d'un joueur est toujours dans une région
  chaude.

Journal du shard : `Region froide : N003_W009 (plus parcourue)`, `Region dechargee : …`.

### Fichiers

| Fichier | Rôle |
|---|---|
| `src/shardd/world/RegionLifecycle.h/.cpp` | États des régions, régions devenues froides, déchargement |
| `src/shardd/world/RegionActivity` | `ActiveCells()` : liste des régions actives |
| `src/shared/server_bootstrap/ServerApp` | Index par région (`InitSpawners`), `RebuildEvaluatedSpawners`, `RefreshRegionActivity` (cycle de vie, déchargement), `UpdateSpawners` (liste évaluée, délai de grâce) |

### Tests

`region_lifecycle_tests` : région jamais visitée froide, chaude 59 s après le départ et
froide à 60 s (signalée une seule fois), retour d'un joueur (délai qui repart de zéro),
déchargement à 300 s puis oubli, plusieurs régions et doublons, délais incohérents
corrigés. `region_activity_tests` : liste des régions actives, coordonnées négatives.

### Déploiement

Shard seul. Pas de migration. Nouvelles clés optionnelles : `world.region_idle_s`,
`world.region_unload_s`, `server.spawners.despawn_grace_s`.

## 4.4 — Agenda déterministe des PNJ (chantier 2.7)

### Avant

Les PNJ d'ambiance suivaient une routine (tournée, ronde, poste, errance) avec un
horaire grossier (jour, nuit, toujours) et des pauses tirées au hasard pendant la
simulation. Rien ne disait où se trouvait un PNJ à une heure donnée sans le simuler.
Avec 4.1 à 4.3, un PNJ loin des joueurs n'existe plus : il fallait pouvoir le recréer
« au bon endroit ».

### Principe

`src/shardd/gameplay/ambient/DailyAgenda` (module pur, testé seul) :

- **Routines de métier** : une suite de créneaux (heure de début, lieu, activité).

  | Métier | Journée type |
  |---|---|
  | `fermier` | dort → 5 h 30 travail → 12 h auberge → 13 h travail → 19 h devant chez lui → 21 h dort |
  | `artisan` | dort → 6 h 30 atelier → 12 h auberge → 13 h atelier → 18 h 30 auberge → 21 h dort |
  | `marchand` | dort → 7 h étal → 12 h 30 auberge → 13 h 30 étal → 19 h flânerie sur la place → 21 h dort |
  | `aubergiste` | dort → 8 h auberge → 15 h place → 16 h auberge → 23 h 30 dort |
  | `pretre` | dort → 6 h temple → 12 h chez lui → 13 h temple → 18 h place → 20 h temple → 22 h dort |
  | `flaneur` | dort → 8 h place → 10 h marché → 12 h auberge → 14 h place → 17 h marché → 19 h auberge → 22 h dort |

- **Variante par graine** : graine = identifiant du spawner + emplacement. Pour chaque
  jour de jeu :
  - horaires décalés jusqu'à `jitterMinutes` (20 min), sans jamais inverser deux
    créneaux ;
  - auberge, place, marché et temple choisis parmi les points du lieu (domicile et
    travail restent fixes) ;
  - petit point d'arrêt propre à chaque PNJ (≤ 1,5 m) : deux PNJ ne se tiennent pas au
    même point.
- **Position à toute heure, sans simulation** : `EvaluateAgenda(agenda, graine,
  secondes de jeu)` donne le créneau, le lieu, l'activité et la position. La position est
  en route si le PNJ n'a pas fini son trajet depuis le lieu précédent (vitesse de marche et
  rythme de l'horloge monde). La nuit, une fois arrivé chez lui, il est **à l'intérieur** :
  aucune entité.
- **Flânerie** (`StrollPoint`) : point qui change toutes les 2 min de jeu dans
  `strollRadiusMeters` (4 m) autour du lieu, lui aussi déterministe.

Côté shard (`ServerApp`) :

- un PNJ à agenda **apparaît là où sa journée le place à l'instant** (au travail, à
  l'auberge, en chemin), et non plus à sa porte ;
- il n'apparaît pas tant qu'il dort chez lui (`IsAgendaSlotIndoors`) ;
- éveillé, il va au point de son créneau (`UpdateAgendaMob`), s'arrête pour un joueur
  tout proche comme avant, rentre le soir puis est retiré. Il réapparaît au matin, en
  sortant de chez lui ;
- l'agenda remplace la routine **et son horaire** (jour, nuit) quand les deux sont
  présents ; il ne s'applique qu'aux archétypes d'ambiance (`behavior.kind = ambient`) :
  sur une créature, il est ignoré et signalé au démarrage ;
- l'heure du coucher vaut aussi pour un PNJ endormi (4.1) : rentrer le rend « occupé »,
  il se réveille et rentre. Il est retiré à moins de 4 m de sa porte (plusieurs PNJ la
  partagent souvent), ou de toute façon une fois l'heure d'arrivée passée ;
- un trajet tient toujours dans son créneau (95 % au plus) : le créneau suivant part du
  bon lieu, même pour un lieu lointain. Vitesse de marche = celle de l'archétype, la même
  qu'à l'apparition.

Avec 4.1 à 4.3, un quartier sans joueur ne coûte rien. Pourtant, au retour d'un joueur,
le forgeron est à sa forge à 10 h et à l'auberge à midi : le monde a continué de vivre.

### Données

`spawners.json`, à côté de `routine` (un shard plus ancien ignore `agenda` et garde la
routine) :

```json
"agenda": {
  "profession": "artisan",
  "jitterMinutes": 20, "strollRadiusMeters": 4,
  "places": {
    "home":   [[-90019.8, -30019.1]],
    "work":   [[-90002.1, -29989.4]],
    "tavern": [[-90010.6, -30002.1], [-89995.0, -30020.0]],
    "square": [[-89989.4, -29997.9]],
    "market": [[-89994.0, -29991.0]]
  }
}
```

Politique stricte, comme pour `routine` : métier inconnu, lieu inconnu, point mal formé ou
domicile absent font échouer le chargement des spawners. Un lieu sans point utilise le
domicile.

`tools/world_gen` en produit désormais pour :

- les villageois de jour (fermier, artisan ou flâneur ; les noctambules gardent leur
  tournée) ;
- les marchands (leur étal) ;
- l'aubergiste (il dort à l'auberge).

Ses tirages se font sur un générateur à part : villes, tournées et faune restent
identiques. **Relancer `tools/world_gen`** pour profiter des agendas ; sans
régénération, les PNJ gardent leurs routines.

### Limite connue

Les mobs des camps (garde, chasse, repos) n'ont pas encore d'agenda : le même module
s'y prêtera avec des métiers dédiés.

### Fichiers

| Fichier | Rôle |
|---|---|
| `src/shardd/gameplay/ambient/DailyAgenda.h/.cpp` | Métiers, variante par graine, `EvaluateAgenda`, `StrollPoint` |
| `src/shardd/gameplay/spawner/SpawnerRuntime` | `agenda` du JSON (`ParseAgenda`) |
| `src/shared/server_bootstrap/ServerApp` | Apparition à la place de l'agenda, `IsAgendaSlotIndoors`, `UpdateAgendaMob`, horloge de jeu |
| `tools/world_gen/lib/Population.cpp`, `Output.cpp` | Agendas des villageois, marchands et aubergistes |

### Tests

`daily_agenda_tests` : routines intégrées cohérentes, nuit chez soi et invisible, au
travail le matin et en route au début du créneau, déterminisme et variantes, décalage
borné (1 000 journées), auberge variée et atelier fixe, retour du soir visible puis
invisible, lieux sans point et agenda désactivé, flânerie bornée et déterministe, trajet
plus long que son créneau.
`world_gen_tests` : agendas générés (métier connu, domicile présent).

### Déploiement

Shard seul (puis régénération du monde par `tools/world_gen` pour les agendas). Pas de
migration, pas de nouvelle clé de configuration.

## 4.5 — Délestage avec hystérésis (chantier 2.5)

### Avant

Un shard surchargé (foule, événement, base lente) prenait du retard tick après tick,
sans rien réduire : tout le monde subissait la lenteur.

### Principe

`src/shardd/world/LoadShedding` (module pur, horloge passée en argument) mesure la
**durée réelle de chaque tick** (réception, simulation, envois) rapportée au budget
(`1 / tickHz`, 50 ms à 20 Hz), lissée (poids 0,1 par tick ; un tick exceptionnel, comme
le premier après le démarrage, compte au plus pour 4 budgets). Le déclencheur est donc la
charge réelle, pas un nombre de joueurs. Délestage désactivé : la charge reste mesurée et
affichée.

| Bascule | Condition (défaut) | Clé |
|---|---|---|
| Entrée | charge lissée ≥ 85 % pendant 2 s | `server.load_shedding.enter_load`, `enter_hold_s` |
| Sortie | charge lissée ≤ 60 % pendant 10 s | `server.load_shedding.exit_load`, `exit_hold_s` |
| Délai minimal | 15 s entre deux bascules | `server.load_shedding.min_dwell_s` |

Deux seuils distincts, des durées de maintien et un délai minimal : un tick lent isolé est
absorbé par le lissage, et une charge entre 60 et 85 % ne fait jamais osciller l'état.

Pendant le délestage (`ServerApp::ApplyLoadShedding`) :

- **plafond d'entités par client** × `server.load_shedding.view_cap_factor` (0,5 : 100 →
  50, jamais sous 20 ni au-dessus du plafond configuré). Les plus proches restent, groupe et cible toujours compris (3.2) :
  le rayon de vue se réduit de lui-même ;
- **IA de repos et d'ambiance deux fois moins souvent** (5 → 2 Hz). Le combat ne change
  pas ;
- **plus de nouveau PNJ d'ambiance** : ceux qui sont là restent, les créatures et les mobs
  de quête apparaissent toujours. À la sortie, les PNJ en attente réapparaissent au fil de
  l'évaluation étalée des spawners (4.2), pas d'un coup. Un joueur qui arrive dans une
  ville pendant le délestage la trouve moins peuplée : c'est le prix de la dégradation.

Tout revient à la normale à la sortie. Désactivable : `server.load_shedding.enabled =
false`.

### Exploitation

- Bascules journalisées en avertissement : `Délestage ACTIVÉ (charge lissée 92 % du tick,
  plafond 50 entités/client…)`, puis `Délestage levé`.
- La ligne `Tick stats` donne `charge_tick=` et `delestage=oui|non` chaque seconde.

### Fichiers

| Fichier | Rôle |
|---|---|
| `src/shardd/world/LoadShedding.h/.cpp` | Charge lissée, seuils, maintien, délai minimal |
| `src/shared/server_bootstrap/ServerApp` | Mesure du tick dans `Run`, `ApplyLoadShedding`, apparitions d'ambiance suspendues dans `UpdateSpawners`, réglages lus dans `InitSpawners` |

### Tests

`load_shedding_tests` : charge normale sans bascule, entrée après le maintien, pic isolé
absorbé, hystérésis (75 % entre les seuils : aucun changement dans un sens comme dans
l'autre), sortie après 10 s, délai minimal entre bascules (sortie exactement à 15 s),
désactivation (charge toujours mesurée), entrées invalides, réglages absurdes corrigés,
long premier tick absorbé.

### Déploiement

Shard seul. Pas de migration. Nouvelles clés optionnelles `server.load_shedding.*`.
