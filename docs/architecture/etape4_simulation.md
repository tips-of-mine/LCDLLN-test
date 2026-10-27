# Étape 4 — une simulation qui ne coûte que là où l'on joue (shard et cellules)

Suite des étapes 1 (`etape1_master_shard.md`), 2 (`etape2_persistance.md`) et 3
(`etape3_reseau.md`). L'étape 4 reprend le bloc 2 du rapport d'analyse City of Heroes
(idées reprises, aucun code) : une zone sans joueur ne doit presque rien coûter au shard,
et le monde doit pourtant continuer de vivre.

| Ticket | Sujet | Statut |
|---|---|---|
| 4.1 (chantiers 2.2, 2.3) | Sommeil des mobs loin des joueurs, classes de fréquence de l'IA | livré |
| 4.2 (chantier 2.6) | Travaux périodiques étalés par identifiant | cette PR |
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
- **Sommeil** : un mob sans joueur à moins de `server.mob_ai.wake_radius_m` (250 m)
  depuis `server.mob_ai.sleep_delay_s` (3 s), et qui n'est pas occupé, s'endort. Il
  n'exécute plus ni IA ni déplacement, et sa vitesse est remise à zéro pour les clients.
  - Le voisinage est vérifié **une fois par seconde**, étalé par identifiant d'entité (1/20
    des mobs à chaque tick à 20 Hz).
  - Un mob **occupé** (dégâts, menace, provocation, retour en cours) ne s'endort jamais et
    se réveille sur-le-champ : un sort tiré de très loin lui donne de la menace.
  - Au réveil, aucun rattrapage du temps dormi : il reprend là où il s'était arrêté.
  - `wake_radius_m = 0` désactive le sommeil.

Le rayon de 250 m reste en deçà de la portée des snapshots (± 350 m) : un mob endormi
encore visible est immobile, ce qui passe pour une pause à cette distance. Les mobs
n'ont pas d'aggro de proximité : un mob n'attaque qu'après avoir reçu de la menace, et
la menace le réveille sur-le-champ.

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
- Une enchère se règle au plus 1 s après son échéance.

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
