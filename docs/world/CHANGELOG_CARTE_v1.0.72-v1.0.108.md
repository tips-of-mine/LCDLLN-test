# Synthèse de la branche `carte/corrections-v1.0.72` (lore v1.0.72 → v1.0.112)

> Base : `main` au commit `8a565ba` (« Carte : région de départ des Enfers (v1.0.71) (#1126) »).
> 39 commits (v1.0.72 à v1.0.107, puis v1.0.108 : documentation, v1.0.109 : vérification finale, v1.0.110 : lieux secrets). Détail de chaque lot :
> [`LORE_BD.md`](LORE_BD.md) §16 (table des matières en tête de section). État du monde :
> [`ROYAUMES.md`](ROYAUMES.md) §11 et [`ENFERS.md`](ENFERS.md) §7. Outillage :
> [`EDITEUR_LORE.md`](EDITEUR_LORE.md), [`OUTILS_LORE.md`](OUTILS_LORE.md). Index : [`README.md`](README.md).

## Sommaire

1. [Ce que la branche change, en bref](#1-ce-que-la-branche-change-en-bref)
2. [Tableau par version](#2-tableau-par-version)
3. [Impact sur le jeu et déploiement](#3-impact-sur-le-jeu-et-déploiement)
4. [Points ouverts et à valider (liste consolidée)](#4-points-ouverts-et-à-valider-liste-consolidée)
5. [Description de PR proposée](#5-description-de-pr-proposée)

## 1. Ce que la branche change, en bref

| Domaine | Avant (v1.0.71) | Après (v1.0.108) |
|---|---|---|
| Cellules jouables (master + corridors) | 191 | **481** (412 dans `generated_cells`) |
| Routes en jeu | réseau calculé par le générateur entre les villes | **routes du lore** suivies case par case (165 routes, `lore_roads`) + 10 routes du générateur gardées pour les corridors de départ |
| Régions | régions du lore | **régions du hors-série** + **provinces** ; niveau de référence et niveaux par faction structurés |
| Nouveaux contenus | — | Île de la Trêve, 3 villes moyennes, Mine de la Traverse, Enfers des tomes 15-22, dragons de Xygga, 12 peuples lointains, Ophidiens, lieux `[GD]` des cases ouvertes, gouffre caché de Thurvhal |
| Villes et PNJ de service | 114 villes, 1127 PNJ | 122 villes (dont Thurvhal), **1264 PNJ** de service |
| Lieux, quêtes, raids (lore) | 88 lieux, 146 quêtes, 33 raids | **408 lieux**, 198 quêtes, 38 raids |
| Éditeur de lore | export avec pertes | export **sans perte** octet pour octet, mode Déplacer, mode Tracer, calques (dont secrets), champs structurés, 5 tests Playwright |
| Outils | `generate_city_services.py`, `validate_city_services.py` | + `sync_positions.py`, `sync_routes.py`, `test_sync_routes.py`, `check_references.py` (tous en CI) |
| Générateur (`tools/world_gen`) | — | reliefs `colline` / `crevasse` / `massif` / `gouffre`, positions manuelles, routes du lore, villes secrètes, palette des Enfers, données de lumières et d'ambiance |
| Code du jeu (`src/`) | — | **inchangé** (aucun opcode, aucune migration, `kProtocolVersion` inchangé) |

## 2. Tableau par version

Légende de l'impact jeu : **R** = données du monde régénérées par la CI (client + master +
shard **en lock-step** avec le `game/data` du même commit) ; **R=** = générateur modifié mais
monde prouvé identique ; **—** = lore, éditeur ou documentation seulement (aucun effet en jeu).

| Version | Commit | Résumé | Impact jeu | À valider par le propriétaire |
|---|---|---|---|---|
| v1.0.72 | `5892bac` | 11 corrections : villes déplacées, colline de Vaerdhun, crevasse et mine de Vharholdt, Sanctuaire des Brisants, routes 000198-000206 ; reliefs locaux au générateur | R | noms (Mine de Vharholdt, Sanctuaire, Veillée des Feux) ; hauteur de la colline et de la crevasse ; Ombhral et Gredhaim hors du 3×3 de Haeldorf |
| v1.0.73 | `4c86a67` | 3 villes moyennes (Fhelvakt, Hazhaëlin, Kholvard), Mine de la Traverse et son raid | R | noms, races, factions ; galerie sous N000_W008 ; niveau et boss du raid |
| v1.0.74 | `451b979` | Île de la Trêve : 54 cases, 4 villes, ports, ligne du Nord, 4 raids, quêtes entre factions | R | noms, tailles, contrôle ; « la Légion » ; escales ; port d'Ostrevan ; équipes mêlées |
| v1.0.75 | `7b0ef00` | la cité redevient « Syzygie » | R (noms) | — |
| v1.0.76 | `aa18cff` | tracés de routes (waypoints, jonctions) et corrections de terrain | R | lectures des points 4 et 8 ; Bhridrak sur les neiges ; « Porte du Sud » |
| v1.0.77 | `b075b48` | relief de l'Île (11 massifs) et temples incas des Hwakhari | R | peuple, culte, noms, hauteurs des massifs |
| v1.0.78 | `cc4de85` | cellules de Vaerdhun et des Brisants ; décisions du 11/11 (balisage, niveaux par faction) | R | forme du balisage en jeu et seuil |
| v1.0.79 | `0945436` | 3 cases pour relier Vaerdhun | R | — (décidé) |
| v1.0.80 | `df141aa` | route Syzygie – Vaerdhun continue, Treuvhal jouable | R | rivière au nord de Treuvhal (N012_W014) fermée |
| v1.0.81 | `0a6af5f` | Enfers des tomes 15-22 (hiérarchie, 7 lieux, plans) et 11 dragons de Xygga | R (deux cartes) | combat contre Xygga ; noms des dragons ; Seuil des Plans ; genre de Xygga |
| v1.0.82 | `f52de5e` | 12 peuples lointains et Ophidiens | R (POI) | noms `[GD]`, directions, peuple turc / persan, Cité-Nid, butin |
| v1.0.83 | `4ca052c` | palette infernale du générateur | R (Enfers) | lave émissive côté client |
| v1.0.84 | `2126cf5` | route 000074 par le Deathtrap Canyon | — (suivie en jeu depuis v1.0.105) | — |
| v1.0.85 | `7c13248` | balisage des zones dangereuses, ancrages manquants | R (POI) | noms des balises ; poche 1-15 de Whispers Hill |
| v1.0.86 | `ebcd12e` | lieux et quêtes de Moork, Tsaroth, Désert de l'Oracle | R (POI) | noms ; Tsaroth 20-40, Oracle 10-25 ; contre-quêtes |
| v1.0.87 | `bdda151` | lieux et quêtes des Gigantomorphes, Via Magna, Marche de l'Est | R (POI) | noms ; repères de niveau |
| v1.0.88 | `210b04a` | ouverture de la Marche de l'Est (45 cases) | R | noms ; gradient 15-28 ; marges |
| v1.0.89 | `40f5c85` | quêtes et niveaux du Royaume des nains de l'est | — | noms et étapes des 5 quêtes ; gradient |
| v1.0.90 | `a0c54d3` | 222 cases rattachées à une région ; sud-centre et Confins du Midi | — | noms et limites des régions ; Faervosk |
| v1.0.91 | `3f997cf` | ouverture d'Ysenkhal – Vozmarkh et de 4 routes de la Marche | R | noms ; côte orientale 24-30 ; Vozmarkh (Lune Noire) comme suite du départ nain |
| v1.0.92 | `62cd0e0` | régions du hors-série et provinces | — | Marche de l'Est éclatée ; tracé au sud ; Désert des Sels ; renommage des noms HS ; boîtes du master |
| v1.0.93 | `55244e4` | renommage des 88 villes aux noms anglais | R (noms) | — (décidé) |
| v1.0.94 | `40c4bf4` | noms à vérifier (Kendrhir, Heusgly, hameaux nains), Harrodh | R (noms) | faction d'Harrodh |
| v1.0.95 | `511b63a` | placement manuel (éditeur, `sync_positions.py`, générateur) | R= | — |
| v1.0.96 | `d927df3` | export de l'éditeur sans perte | — | — |
| v1.0.97 | `a833988` | mode Tracer, routes qui suivent les villes | — | — |
| v1.0.98 | `8159b38` | sauvegarde complète de l'éditeur vérifiée | — | — |
| v1.0.99 | `dbe5988` | calques de la carte de l'éditeur | — | champ `niveau_ref` (fait en v1.0.100) |
| v1.0.100 | `da1b244` | niveaux, étapes de quête situées, type mine, formulaires, suppression | — | `niveau_par_faction` (rempli en v1.0.103) |
| v1.0.101 | `b39fc31` | îlots de la Via Magna aux Territoires orques | — | — (décidé) |
| v1.0.102 | `8fef334` | derniers îlots de régions et de provinces | — | — (décidé) |
| v1.0.103 | `7b765e3` | niveaux `[GD]`, niveaux par faction, ancrages, noms de fiches, `check_references.py` | — (seule la `source` d'Harrodh change au master) | **tous les niveaux** ; règle +5 / seuil 20 ; raids de Kendrhir ; Harrodh ; orphelines |
| v1.0.104 | `467b80c` | ouverture des routes de Vozmarkh et de Rhivkhar (55 cases) | R | noms ; niveaux 12-15 au nord de Farvhenn ; routes encore coupées |
| v1.0.105 | `c4f9941` | le générateur suit les routes du lore (`sync_routes.py`) | R | routes maritimes et pontons |
| v1.0.106 | `f6982df` | 11 routes créées, 4 tracés corrigés, balises replacées, 68 cases ouvertes | R | tracés `[GD]` ; Piste des Morts ; baie de l'Île sans route |
| v1.0.107 | `2c1aba0` | gouffre caché de Deathtrap Canyon et Thurvhal (réapparition secrète) | R (dont `respawn_points.txt` du shard) | noms `[GD]` ; rendu des torches et du fond sombre ; mécanisme de découverte |
| v1.0.108 | `bb469af` | documentation complète : guide de l'éditeur, outils, monde, synthèse ; commentaires de code | — | — |
| v1.0.109 | `f939b0b` | vérification finale : champ `territory.ville_id` au formulaire de faction (deux éditeurs), `test_editor_fiches.py` calculé depuis les données (point 57) | — | lieux secrets dans `places.json` (point 65) |
| v1.0.110 | `1b72856` | **lieux secrets cachés aux joueurs** (point 65, option a) : `secret` des POI au master (11 grottes des dragons, Puits des Lianes), `secret_places.json` réservé au serveur et retiré du paquet client, annuaire MJ du shard, case « Secret » de l'éditeur, `check_secrets.py` en CI ([LORE_BD §16.37](LORE_BD.md)) | — | — |
| v1.0.111 | `83aa9fc` | **666 cimetières** : nombre fixe (contrôle bloquant de la CI build-linux), exploit [GD] « Pèlerin des 666 tombes » ([LORE_BD §16.38](LORE_BD.md)) | — | répartition des cimetières (81 cases sans), mécanique de l'exploit |
| v1.0.112 | (ce commit) | **répartition des 666 cimetières** : un cimetière au moins par cellule à terre (71 isolés ; 10 cellules de mer sans), 71 hameaux sans cimetière, total vérifié par le générateur ([LORE_BD §16.39](LORE_BD.md)) | — | — |

## 3. Impact sur le jeu et déploiement

- **Code du shard** : seul l'annuaire des MJ (`src/shardd/gameplay/gm/GmPlaceDirectory`, v1.0.110)
  change — il lit en plus `world/<monde>/secret_places.json` (lieux secrets pour `/tp` et `/places`).
  Pas de nouvel opcode, pas de migration, pas de nouvelle clé de `config.json`, `kProtocolVersion` inchangé.
- **Paquet client** : `secret_places.json` en est retiré par `.github/scripts/package-windows.ps1`
  (`Copy-GameData -StripServerOnly`, v1.0.110) ; il reste dans le paquet serveur (deploy/docker).
- Mais les **données** changent : `game/data/world/royaumes/world_master.json` (cases, villes,
  POI, reliefs, routes du lore), `game/data/world/enfers/world_master.json` (POI), et le
  **générateur** (`tools/world_gen`). La CI régénère le monde : `start_locations.json`
  (`generated_cells`, lu par le **master** et le **shard**), `respawn_points.txt` et
  `spawners.json` des cellules (lus par le **shard** au démarrage), cellules de rendu, `places.json`,
  `map_features.bin`, `world_map.png` (lus par le **client**).
- Conséquence : **client, master et shard en lock-step**, avec le `game/data` généré par la CI
  du même commit ; **régénération du monde requise** (faite par les builds). Un client ancien
  verrait des murs invisibles et un relief différents du serveur ; un shard ancien n'aurait pas le
  cimetière de Thurvhal ni les points de réapparition des nouvelles cellules.

## 4. Points ouverts et à valider (liste consolidée)

Tout ce qui est marqué `[GD]` est une proposition à valider. Les numéros renvoient aux sections de
[`LORE_BD.md`](LORE_BD.md) ; « R » = référence de la section 17 (questions ouvertes antérieures).

### 4.1 Noms `[GD]` à valider

1. Mine de Vharholdt, Sanctuaire des Brisants, Veillée des Feux des Brisants (16.1).
2. Les 3 villes moyennes (Fhelvakt, Hazhaëlin, Kholvard), leurs races et factions ; Elfes des bois neutres (16.2).
3. Île de la Trêve : région, villes, lieux, raids, créatures, tailles et contrôle des villes (16.3).
4. Hwakhari : peuple, culte, temples, PNJ, quêtes, tailles ; rameau humain plutôt que race existante (16.6).
5. Noms des dragons de Xygga (« pas de noms pour l'instant ») et genre de Xygga (16.10).
6. Peuples lointains et leurs dirigeants ; directions exactes ; peuple turc ou persan unique ou double (16.11).
7. Noms des balises de danger (16.14).
8. Lieux et quêtes `[GD]` des lots d'ouverture : 36 lieux et 14 quêtes (16.15), 11 et 10 (16.16), 40 et 2 (16.17), 5 quêtes (16.18), 52 et 3 (16.20), 48 et 3 (16.33), 64 lieux (16.35).
9. Noms des régions et provinces du sud-centre (Forêt des Lutins, Val de Suthvarn, Désert des Sels, Confins du Midi) (16.19).
10. Noms du lore différents des noms du hors-série (« Territoires orcques », « Baronnie de Moorks », « Royaumes des nains du sud ») : renommage à décider (`nom_hs`) (16.21).
11. Gouffre : Gouffre des Revenus, Fente, Thurvhal, Ghaurdes, Mhaldra, Kerreth, 3 quêtes (16.36).
12. Noms des fleuves, rivières et lacs (R).
13. Calendrier : noms des lunes, année de jeu à fixer (§0).

### 4.2 Niveaux

14. Tous les niveaux de référence `[GD]` des régions et provinces, en particulier Hishtarland 25-40, Grande Plaine 45-55, Nains du sud 10-30, 1ère Province 10-22, Northind 12-24 (16.32).
15. **Raids de Kendrhir 38-56** dans les Terres des Gigantomorphes (sac 38-43, grande bataille 51-56, défense 38-43, contre-bataille 51-56) alors que l'histoire du T1 est un début (16.32).
16. Planchers des raids de fin d'histoire (Héroïque 45, Légendaire 50, Mythique 55, Apocalyptique 60), dont les raids Légendaires de Lhynn à 50-55 (16.32).
17. Règle des niveaux par faction : décalage +5 et seuil 20 en territoire ennemi ; Marches, Hishtarland, Via Magna sans maître (16.32).
18. 2 raids de l'Empire du Jade et 3 régions d'extension sans niveau (16.32).
19. Repères des lots : Tsaroth 20-40, Oracle 10-25 (16.15) ; lisière des Gigantomorphes 28-36 (16.16) ; gradient 15-28 de la Marche de l'Est (16.17) ; côte orientale 24-30 (16.20) ; nord de Farvhenn 12-15 contre 20-26 sur la route de Rhivkhar (16.33) ; Fhelvakt – Hazhaëlin 30-35 contre 20-26 côté elfe (16.35).
20. Poche 1-15 de Whispers Hill accessible seulement par des cases 30-56 (16.14).
21. Niveau des abords des Portes des enfers du continent (16.19).
22. Forme du balisage en jeu (panneaux, avertissements de PNJ, couleur du nom de zone) et seuil (16.7).
23. Niveaux, niveaux par faction et niveaux de raid ne sont lus par **aucun** code du jeu : intégration à décider (16.32).

### 4.3 Factions, régions, quêtes

24. **Faction d'Harrodh** : Empire (`[GD]`, master `empire_hynn`) (16.23, 16.32).
25. Régions HS : Marche de l'Est éclatée (18 cases dans la province), tracés au sud (Tsaroth à la Baronnie, Der Hem Shelbem aux Nains du Sud, Harndrecht et Zaltheim au Drach), Hishtarland exclu des candidats du sud, Désert des Sels coupé en deux, cœur des Nains de l'Est étendu (16.21).
26. Boîtes `regions` du master non recalées sur les régions HS (16.19, 16.21).
27. Limites du sud-centre (S001-S002 de la forêt, colonne E007, côte sud de Lhynn, côtes S015-S017) ; Faervosk garde `region_id = royaume_drach` (16.19).
28. Quêtes ouvertes à toutes les factions dans des zones tenues par la Lune Noire : contre-quêtes à définir (16.15 à 16.20, 16.33).
29. « La Légion » de l'Île de la Trêve (Légion infernale ou Légions impériales) ; règle de formation des équipes mêlées (16.3).
30. Ombhral et Gredhaim quittent la région 3 × 3 de Haeldorf (16.1).
31. Vozmarkh, ville de la Lune Noire au cœur du royaume nain, comme suite du départ nain (16.20).

### 4.4 Données incomplètes et références

32. **89 références orphelines** (36 identifiants) : `faction_demons` ×30, `race_menthat` ×11, `race_half_dark_elf` ×6, `battlefield_poi` / `forest_poi`, classes inexistantes, 9 butins, `citadelle_legion`… (16.32).
33. 59 quêtes non plaçables (étapes à situer) et 6 raids sans case (16.32).
34. 32 dialogues `dial_t10_NN` vides à écrire ; 45 dialogues sans réplique (16.32).
35. Krak de Jhenner, citadelle impériale et Passe de Heniak sans case (16.15, R).
36. Palais des Maîtres des Dragons et Tsaroth sans entrée au master (routes finies au point praticable de la case) (16.34, 16.35).

### 4.5 Routes et monde jouable

37. **Routes encore coupées** au mur invisible : Farvhenn – Vestmhor (000106), Shaddrek – Klehrwath (000071) ; 43 routes du lore qui s'arrêtent à une zone fermée (16.33, 16.35).
38. Tracés `[GD]` des 11 routes créées en v1.0.106 (dont Orkh, Marn) ; Piste des Morts (000236) en double de 000074 (16.35).
39. Baie de l'Île de la Trêve sans route (5 cases de haut-fond ; une chaussée demanderait un changement de terrain) (16.35).
40. Routes maritimes et pontons non tracés en jeu ; lignes maritimes non jouées (16.3, 16.34).
41. Ouverture de 55 + 68 cases par méthode, sans demande case par case ; marges en haute montagne autour de Shaddrek et de Khrozrad (16.33, 16.35).
42. Hishtarland et Dyprokh sans route terrestre (îles) (16.1, ROYAUMES §10).
43. Port d'Ostrevan à 300 m de l'eau dans le générateur (16.3).
44. Rivière nord de Vaerdhun au nord de Treuvhal (N012_W014) derrière le mur invisible (16.9).
45. Éditeur : cases de raccord en ligne droite (route qui coupe la mer ou une pointe de terre) ; ponton déplacé vers la case navigable la plus proche, pas forcément la plus logique (16.26).

### 4.6 Rendu client et mécanismes à faire

46. **Lumière des torches** du gouffre (`world.point_lights` du zone.json) non lue par le client (un seul fichier global de 64 lumières) (16.36).
47. **Fond sombre** du gouffre (`world.ambiance_zones`) : aucun réglage d'ambiance par zone au client (16.36).
48. **Lave** des Enfers non émissive, sans dégâts (4 couches de sol à couleur fixe) (16.12).
49. Ville et lieux secrets : absents de `places.json` (v1.0.110 : bannière, radar, son de ville et carte du jeu sans eux) ; mécanisme de « lieu découvert » (révéler un secret au joueur qui l'a trouvé) à définir (16.36, 16.37).
50. Gouffre : galerie à ciel ouvert (pas de tunnel dans la heightmap), écurie générée au fond, part de S008_E011 renvoyée au cimetière du gouffre (16.36).
51. Modèles 3D absents : temples incas, palais de Lucifer, viaduc, grottes, croix et brasier du sanctuaire ; le générateur ne modèle pas de falaises côtières (16.1, 16.6, 16.10).
52. Instances, boss et récompenses absents : raids de l'Île, Mine de la Traverse, Cité-Nid ophidienne, plans infernaux (Seuil des Plans sans transfert), combat contre Xygga endormi (16.2, 16.3, 16.10, 16.11).
53. Événements de monde (Veillée des Feux des Brisants) : aucun système serveur (16.1).

### 4.7 Outillage

54. Tests Playwright de l'éditeur hors CI (à lancer à la main) (16.25 à 16.29).
55. `check_references.py` non bloquant : passer à `--strict` quand les orphelines seront tranchées (16.32).
56. Éditeur : carte des Enfers absente ; `description` / `notes` d'une route non éditables en mode Tracer ; fusion de fiches et import « BD » peu testés ; `secret` et `entree_cachee` sans champ de formulaire (16.26 à 16.29, 16.36).
57. ~~`test_editor_fiches.py` : 3 échecs déjà présents au v1.0.107 (attentes figées au v1.0.103 : 5 lieux `mine_poi` ajoutés depuis, 96 quêtes ancrées sans `etapes_struct` au lieu de 90, champ `territory.ville_id` sans champ de formulaire).~~ **Corrigé en v1.0.109** : attentes calculées depuis les données, champ « Ville du territoire » ajouté au formulaire de faction des deux éditeurs.

### 4.8 Questions de lore antérieures encore ouvertes (section 17)

58. Albums hors série : points de 10.5.
59. Identité de « l'amant » vengé par la succube ; sort de Feidreiva ; nom de la fille de Wismerhill (T12).
60. `race_half_dark_elf` absente de `races` (voir aussi point 32).
61. Escaliers de l'Oracle (BD-T2 contre BD-T3, lacs du destin, colline à sanctuaire, antre souterrain).
62. Palais du Maître des Dragons « au-delà des sept régions » mais visible depuis le pont de la Passe.
63. Ghulvathan : N009_E011 en jeu contre N012_E012 au lore.
64. Forteresse naine d'Heusgly dite « proche de Moork » sans appui BD.

### 4.9 Vérification finale (v1.0.109)

65. ~~**Lieux secrets dans la sortie publique**~~ — **Corrigé en v1.0.110 (option a)** : drapeau `secret` des POI, `secret_places.json` réservé au serveur, retiré du paquet client (LORE_BD §16.37). Constat d'origine : les 11 grottes des dragons de Xygga (`poi_grotte_xygga`, `poi_grotte_premier_ne_*`) et l'entrée ophidienne `poi_ophidiens_puits_des_lianes` sont des POI du master ; le générateur recopie **tous** les POI du master (`g.pois`, Terrain.cpp) dans `places.json` `pois`, fichier livré au client, avec position exacte et description (« grotte cachée », Xygga…). Trois sont sur des cellules générées (S008_E012, N010_E012, N010_E010) et la carte du jeu les affiche une fois la zone découverte. Seules les villes `secret: true` sont écartées de la liste publique (v1.0.107), mais `secret_cities` (Thurvhal, description du Gouffre des Revenus) est écrit dans ce même `places.json` livré au client (non lu, mais lisible). Vérifié sur une génération complète (481 cellules, 354 POI dont ces 12). À trancher : drapeau `secret` des POI (écartés de `pois` comme `secret_cities`, et POI hors cellules générées non publiés) ou découverte par la carte assumée.

**Total : 65 points ouverts consolidés (64 + 1, dont le point 57 corrigé).**

## 5. Description de PR proposée

**Titre** : `Carte : corrections et ajouts du propriétaire, éditeur sans perte, routes du lore en jeu (lore v1.0.72 → v1.0.112)`

**Corps** :

```markdown
## Résumé

Branche `carte/corrections-v1.0.72` : 39 commits, lore v1.0.72 → v1.0.112 (un lot du propriétaire par commit).

- **Monde jouable** : 191 → 481 cellules jouables (Marche de l'Est, routes de Vozmarkh et de Rhivkhar, passages
  des routes du lore, Vaerdhun, Île de la Trêve) ; lieux du lore 88 → 408 (dont les lieux [GD] des cases ouvertes, avec POI réels) ; 3 villes moyennes ; gouffre
  caché de Deathtrap Canyon avec Thurvhal, point de réapparition secret ; lieux secrets (gouffre, grottes des
  dragons, Puits des Lianes) hors de la carte du jeu et du paquet client (`secret_places.json`, serveur seulement).
- **Routes** : le générateur suit désormais les routes tracées dans le lore (`lore_roads`, recopiées par
  `tools/lore/sync_routes.py`) ; 165 routes tracées, chaque ville jouable reliée à pied aux départs.
- **Lore** : Enfers et dragons de Xygga (T15-22), 12 peuples lointains, Ophidiens, régions du hors-série et
  provinces, niveaux de référence et par faction, renommage des 88 villes, étapes de quête situées.
- **Éditeur de lore** : export sans perte (octet pour octet), modes Déplacer et Tracer, calques (dont secrets),
  champs structurés ; 5 tests Playwright.
- **Outils et CI** : `sync_positions.py`, `sync_routes.py`, `test_sync_routes.py`, `check_secrets.py`, `check_references.py`
  ajoutés au workflow « Lore — services des villes » ; le paquet client Windows retire `secret_places.json`.
- **Shard** : l'annuaire des MJ (`/tp`, `/places`) lit aussi `secret_places.json`.
- **Générateur** : reliefs colline / crevasse / massif / gouffre, positions manuelles, villes et lieux secrets, palette des
  Enfers, lumières et ambiance en données.
- **Documentation** : `docs/world/README.md` (index), `EDITEUR_LORE.md`, `OUTILS_LORE.md`,
  `CHANGELOG_CARTE_v1.0.72-v1.0.108.md`, ROYAUMES.md §11, ENFERS.md §7, table des matières de LORE_BD.md §16.

## Contrôles

- `validate_city_services.py` (122 villes, 1264 PNJ), `generate_city_services.py --check`,
  `sync_positions.py --check`, `sync_routes.py --check`, `test_sync_routes.py`, `check_secrets.py`, `check_references.py` (89 orphelines, rapport)
- `world_gen_tests` (un test par lot), `world_places_tests`, `start_locations_tests`, `respawn_rules_tests`, `gm_place_directory_tests`
- Tests de l'éditeur (Playwright, à la main) : roundtrip, routes, forms, layers et fiches, tous verts sur les deux copies
  (v1.0.110)

## Points à valider

65 points consolidés (57 et 65 corrigés) : voir `docs/world/CHANGELOG_CARTE_v1.0.72-v1.0.108.md` §4 (noms [GD], niveaux, raids de
Kendrhir 38-56, faction d'Harrodh, 89 références orphelines, routes encore coupées, rendu client des torches, du
fond sombre et de la lave, mécanisme de découverte des lieux secrets).

**Déploiement** : ⚠️ redéploiement serveur (master + shard) requis — la branche change `world_master.json`, le
générateur, l'annuaire des MJ du shard et les données du shard (points de réapparition, spawners, `start_locations.json`,
`secret_places.json`) : client, master et
shard **en lock-step** avec le `game/data` généré par la CI du même commit ; **régénération du monde requise**.
Aucun opcode, aucune migration, `kProtocolVersion` inchangé.
```

**Déploiement** : ⚠️ redéploiement serveur (master + shard) requis — `world_master.json`, générateur et données du
shard (points de réapparition) changent : client et serveur (master + shard) en lock-step, régénération du monde
requise.
