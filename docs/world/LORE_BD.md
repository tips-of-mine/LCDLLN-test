# Lore et bande dessinée — confrontation tome par tome

Ce document trace les corrections apportées au fichier de lore
`legacy/world-builder-html/lune-noire-data-v20.json` (et à sa copie identique
`lune-noire-data.json`) après lecture des albums *Les Chroniques de la Lune Noire*,
ainsi que les décisions de placement reportées dans le monde jouable
(`game/data/world/royaumes/world_master.json`). Il complète
[`ROYAUMES.md`](ROYAUMES.md), qui décrit le générateur du monde.

## 0. Ce qui fait foi

Tout ce qui fait foi est inscrit dans le JSON (`regles_transverses`, catégorie `canon`, ids dans
`meta.canon`, v1.0.57) : `regle_canon_corpus`, `regle_canon_priorite_sources`, `regle_canon_epoque_jeu`,
`regle_canon_calendrier`, `regle_canon_etiquettes`, `regle_canon_registre_decisions` (index de toutes les décisions du propriétaire).
- **Corpus** : Tome 0, Tomes 1 à 14, « L'Empire de la Négation », Methraton 1-3, Arcanes 1-5 ; les tomes 15 et suivants sont exclus (exceptions : Enfers et dragons de Xygga en v1.0.81, peuples lointains et Ophidiens en v1.0.82 ; 16.10, 16.11).
- **Priorité** : [DÉCISION] > Tomes 0-14 > L'Empire de la Négation > Arcanes et Methraton ; [GD] comble les vides sans contredire ; [INFÉRÉ], [À VÉRIFIER] ne font pas foi.
- **Moment du jeu** : après la destruction de Kendrhir Vale (T1, en reconstruction) et avant les destructions du T11 (donc aussi du T14) ; année unique, monde figé ; préquelles = passé ; THS limité à la section 10.3 (Q1).
- **Calendrier** : ère de l'Oblivion (an 0 = effacement de Methraton) ; année lunaire de 13 lunes de 28 jours + la Nuit de la Lune Noire ; noms [GD] à valider, année de jeu à fixer.
- **Nouvelle décision** : note `[DÉCISION] (v1.0.xx)` sur la fiche, puis entrée au registre. Script reproductible : `lore_canon.py` (appliqué au fichier v1.0.56).

## 1. Conventions

| Étiquette | Sens |
|---|---|
| `[BD-Tn]` | Fait lu dans l'album n (texte ou image). |
| `[DÉDUCTION]` | Conclusion tirée de plusieurs indices concordants de la BD, indices cités. |
| `[HYPOTHÈSE]` | Lecture plausible, non confirmée : à vérifier dans les tomes suivants. |
| `[INFÉRÉ]` | Choix de conception (cellule, nom repris de `carte_export.json`, emplacement) faute d'indication dans la BD. |
| `[GD]` / `[GAMEPLAY]` | Ajout de game design, sans source BD. |
| `[À VÉRIFIER BD-Tn]` | Donnée antérieure conservée mais contredite par un autre tome. |

- **Références de page** : numéro de page **imprimée** de l'album ; le numéro de planche
  vaut la page imprimée moins 4 (T2). Au T1 : « Page X (p.Y BD) », X = page du PDF.
- **Aucune suppression sans justification** : une note fausse est retirée seulement si la
  BD la contredit, et une note de remplacement explique la correction.
- **Fusions** : quand deux fiches désignent le même personnage, la fiche conservée reçoit
  les notes de l'autre et une note `[v1.0.xx] Fusion de …` ; les références sont repointées.
- **Validation** : après toute modification, `tools/lore/generate_city_services.py`
  (régénère les PNJ de service : une écurie par route de type `road`) puis
  `validate_city_services.py` et `generate_city_services.py --check` (CI
  `lore-city-services.yml`). Les deux fichiers JSON doivent rester identiques.

## 2. Tome 1 — « Le Signe des Ténèbres » (v1.0.44 à v1.0.49)

| Version | Corrections |
|---|---|
| 1.0.44 | Graphie **Kendrhir** partout ; `char_seigneur_kendrhi` → `char_grand_mage_kendrhir` ; locuteurs des dialogues ; fusions de doublons ; références orphelines (`char_oracle*`, `char_feydriva`). |
| 1.0.45 | Kendrhir déplacée au sud de la Faille de Kashogs (N015_E012, sur une falaise, pas face à la mer) ; Tbarak, Klindor, Hazol dans les Marches ; **Ekvhitorn** (S005_W004) reprend la ligne maritime et la quête de la baie. |
| 1.0.46 | Intérieur de Kendrhir : 19 PNJ de service placés, PNJ d'ambiance. |
| 1.0.47 | Kendrhir **en reconstruction** (après le T1) — état `reconstruction` du générateur. |
| 1.0.48 | Marches étendues au nord de la Faille ; itinéraire complet de la horde (carte p.25) : Tbarak → Klindor → Hazol → **Mhorgad** → **Dhunfal** → Faille → Kendrhir. |
| 1.0.49 | Lieux mineurs : Forêt de la Rencontre (N012_E008), **Hayvekh** (N018_E013, ruine), Camp de l'Étang (N018_E012), Champ de la Première Bataille (N018_E011), **Askhrof** (N017_E012, ruine). |
| 1.0.50 | Restes du T1 : 10 butins orphelins des créatures du T1 créés ; extrémités des routes 000003, 000020, 000058 resynchronisées sur la nouvelle cellule de Kendrhir ; `companion_pilou_ou_face` → `companion_pilou`. |

## 3. Tome 2 — « Le Vent des Dragons » (v1.0.50)

### 3.1 Identités tranchées par la BD

| Question | Réponse | Preuve |
|---|---|---|
| Fey et Feidreiva | **Même personne** : « Fey » est le surnom. `char_fey` et `companion_fey_t2` fusionnés. | « Vas-y, Fey ! » (p.8) ; Wismerhill à sa compagne : « Non, Feidreiva ! » (p.31). |
| Pilou | **Surnom de Pile-ou-Face**. `companion_pilou` renommé « Pile-ou-Face (Pilou) ». | « Ça suffit, Pilou ! » (p.8), « 500 piécettes pour Pilou ! » (p.10). |
| Gunther / Gunthar | **Gunthar**, commandeur de l'Ordre. `char_gunther_t5` fusionné dans `char_maitre_gunthar_t8`. | « Quand est-il parti, commandeur Gunthar ? » (p.34). |
| Fratus / Frater Sinister | Même personnage ; « Frater » est employé comme titre. | p.28 (comte Fratus) et pp.32-34 (Grand Maître Frater). |
| Frère de Nasha | **Goum**, son jumeau de 7 ans transformé par un mage. Murata est le vainqueur de l'arène. | pp.9, 15. Quête renommée `quest_t2_liberation_goum`. |
| Maître des Dragons | **Lord Drakkhurst** (`char_lord_drakkhurst`, nouveau), monté sur Dracchennauknh, tué avec son dragon. | pp.29, 39-42. |
| « Drakkahen » | Premier mot du message draconique (terme d'adresse) ; `char_drakkahen` reste le dragon impérial du T4. | p.29. |
| Greldinard | Lord Greldinard = Baron de Moork [DÉDUCTION]. | « Excellent, Baron ! » (p.38). |
| Desdémone | Démone (`race_demon`) ; sa direction « à l'ouest » est un piège. | p.26. |

### 3.2 Lieux et carte

| Lieu | Cellule | Décision |
|---|---|---|
| Mont des Neuf Pics | **N007_E005** | [INFÉRÉ] seule cellule lacustre libre entre Feyhin et Magistrya ; POI `poi_mont_neuf_pics` ; « hôtel au sommet » lu « autel ». |
| L'Atmosphère | S001_W003 | [DÉDUCTION] = entrée de la Passe des Dragons (paroi en visage, p.27) ; POI `poi_atmosphere`. |
| Palais du Maître des Dragons | S003_W004 | = « Palais abandonné de la Passe » (`zone_palais_abandonne_passe`, POI existant). |
| Feyhin Lokcthat ↔ Magistrya | — | Route 000117 ajoutée (panneau p.19) ; une écurie de plus dans chaque ville. |
| Magistrya | N003_W003 | Ville **tout en hauteur** : prévoir de très nombreux escaliers [GD] ; l'arène est à Feyhin, pas à Magistrya. |
| Passe → Oracle | — | Positions conservées ; l'indication « à l'ouest » de Desdémone est notée non fiable. |

### 3.3 Autres corrections

- Dialogues : locuteurs du défi de la Passe et de la scène de Desdémone ; « prêtre des
  vents » (et non « prince ») dans l'initiation.
- Chronologie : 6 évènements T2 corrigés ; ajouts `event_t2_rapport_urmarcht`,
  `event_t2_conjuration_altenberg`, `event_t2_ambassade_massacree`.
- Nouveau personnage `char_orkhan_t2` (envoyé de Frater Sinister) ; nouvel arc
  `arc_t02_vent_des_dragons`.
- Région Drach : « Grands **Vers** », « sept régions », terme « drachen ».
- Feyhin : pont de **pierre** à arches ; « Dzorak » est un juron, pas un personnage.

## 4. Tome 3 — « La Marque des Démons » (v1.0.51)

Références : `pl.N` = numéro de planche imprimé au T3, qui vaut la page du PDF moins 2.
Les références T3 plus anciennes du lore (v1.0.3, « p.N ») suivent la page du PDF.

### 4.1 Identités tranchées par la BD

| Question | Réponse | Preuve |
|---|---|---|
| Goum | **Colosse humain**, jumeau de Nasha (T2). Notes « petite créature noire », « gobelin », « Discrétion (petite taille) » retirées ; son armure naine porte une **nacelle blindée pour Nasha** (butin `item_goum_armor_naine` créé). | « L'armure faite pour Goum et sa sœur était un bijou » (pl.18). |
| Œuf et dragonnet | L'œuf vert est celui de **Pile-ou-Face** ; le dragonnet **Kruikna** s'attache à **Nasha**. `companion_dragon_pilou` fusionné dans `companion_kruikna`. | « Mon bébé, enfin ! » (Pile-ou-Face), « Moi trop gentille, dragounet joli » (Nasha), pl.27. |
| Dialogue de l'éclosion | Locuteurs rétablis (Feidreiva, Wismerhill, Pile-ou-Face, Kruikna, Nasha) ; la succube n'est pas dans la scène. | pl.27. |
| Haazheel Thorn | **Vieillard au turban vert et rouge**, gemme verte au front, yeux lumineux, griffes ; ancien visuel (cheveux blonds) marqué `[À VÉRIFIER BD-T3]`. | pl.19, 38-39. |
| Succube | **Desdémone** (T2) et la succube du T3 ne font qu'une [DÉDUCTION] ; fiche unique `char_succubus` « **Hellavnnea (la Succube)** », `char_desdemone` fusionnée ; elle appartenait à Haazheel. | Rousse aux yeux verts, boucle verte, « Mon amant est vengé ! » (pl.31-32) ; « Je perds une succube » (pl.39). |
| Empereur | L'Empereur du T1 est **Haghendorf** [DÉDUCTION] : `char_emperor_lhynn_t1` fusionné dans `char_haghendorf`. | Cheveux gris, couronne à gemme, nommé par les conjurés (pl.19-23). |
| Hizin | Alias « **Hizim** » (graphie T3), un Menthat. | pl.4. |
| Baron de Mhoorkh | Graphie T3 du Baron de Moork = **Greldinard**. | pl.21. |
| Prince Draakh | Conjuré du T3 ; lien avec le Prince Brakkh du T4 laissé en [HYPOTHÈSE]. | pl.23. |
| Garundel | Tué avec ses fils dans la charge du groupe ; **la BD ne dit pas qui l'a tué** (« Tué par Ghorghor » retiré). | pl.41-42. |
| Ghorghor Bey | Revient au camp de l'Oracle « n'étant pas parmi les morts » ; la mention d'un anneau est retirée de la quête. | pl.7-10. |

### 4.2 Trajet du T3 et carte

Trajet continu, du nord-ouest au sud-est puis retour vers l'ouest : Camp de l'Oracle
(N000_E000) → **Ghrunke Dash** (S007_E016, inchangée : ville de départ des nains) →
forêt des lutins → **village fantôme** (S003_E006) → **fief de Garundel** (S001_E004).

| Lieu | Cellule | Décision |
|---|---|---|
| Ghrunke Dash | S007_E016 | Cellule reportée dans le lore ; animosité nains/elfes marquée `[À VÉRIFIER BD-T3]`. |
| Village fantôme | **S003_E006** (au lieu de N010_W004) | [INFÉRÉ] Cellule de forêt passée en **marais** (grille `%`, terrain `swamp`) ; les villageois renouvellent leur sang, le piège est celui d'Urmarcht [DÉDUCTION]. |
| Fief de Garundel | S001_E004 | `lieu_garundel_estate` (S002_E007) fusionné : les routes 000012, 000013, 000018 partent du fief. |
| Routes | — | 000118 Camp de l'Oracle → Ghrunke Dash, 000119 Ghrunke Dash → fief de Garundel ; une écurie de plus à chaque bout (921 PNJ de service). |
| Lhynn | N001_W010 | Inchangée ; voir les questions ouvertes. |

### 4.3 Autres corrections

- Chronologie : 8 évènements du T3 (`event_t3_coeur_oracle` à `event_t3_fief_garundel`).
- Nouvel arc `arc_t03_marque_des_demons` reliant les 10 quêtes du T3.
- Quêtes : Iaun Reith est égaré par les lutins (et non immunisé) ; rôle des villageois du
  village fantôme ; retour de Ghorghor sans anneau.
- Notes T3 ajoutées : Fratus Sinister (Lhynn, conjuration), six conjurés (répliques
  pl.23), Feidreiva (disparition), Kruikna, Nasha, Pile-ou-Face, Iaun Reith.

## 5. Carte du monde agrandie et densifiée (v1.0.52)

Choix de conception [INFÉRÉ], sans source BD sauf mention : le continent était coupé
net par les bords nord (N018), sud (S014) et est (E020) de la carte. Il est refermé par
une côte, et l'intérieur reçoit les villes du lore qui n'existaient pas encore dans le
monde jouable.

### 5.1 Grille

| Élément | Avant | Après |
|---|---|---|
| Carte du lore (`WorldCells.h`, `cells` du lore) | N018→S014 × W019→E020 (33 × 40) | **N023→S018 × W019→E026** (42 × 46) |
| Grille du master (`grid.rows`) | 33 × 44 (W021→E022) | **42 × 50** (W021→E028, 2 colonnes de marge de chaque côté) |
| Grille d'intérêt du shard (`kCellWorldHalfExtentMeters`) | ±210 km | **±270 km** |

| Terres ajoutées | Cellules | Contenu |
|---|---|---|
| Extrême nord des **Terres des Gigantomorphes** | N019–N023, E009–E023 | Collines, forêt boréale, monts et sommets enneigés, côte. Le lore situe les Géants de froid à l'extrême nord ([HS], région `terres_des_gigantomorphes`). |
| **Rivage des Géants** | E021–E025, N018→S014 | Côte orientale : plaines, forêts, terres arides, montagnes naines jusqu'à la mer. Le lore repousse les géants vers le nord et l'est ([HS]). |
| **Confins du Midi** | S015–S018 | Plaines, chaîne du désert prolongée, terres arides, côte sud. |

Nouveau code de grille `p` = `plain_with_lake` (déjà dans `terrain_types` du lore),
traité par le générateur comme une plaine qui porte un lac.

### 5.2 Cohérence monde jouable ↔ lore

- **23 cellules** du lore, plus récentes que le master, sont reportées sur la grille :
  16 lisières (v1.0.29), île de Dyprokh en N012_E002-E003 (v1.0.36), côtes
  S006_W004 et S006_W007, rivière de Feyhin en N008_E007.
- **Millway, Stonemark, Ridgeholt** sont retirées du master : elles avaient été
  supprimées du lore (v1.0.29-1.0.30).
- **Ghulvathan** reste en N009_E011 dans le master (ville de départ des orcs), alors que le
  lore la place en N012_E012 (v1.0.40) : voir les questions ouvertes.

### 5.3 Villes

- **44 villes du lore** entrent dans le monde jouable : race déduite de la région, sauf
  factions humaines, ports du royaume Drach et Horkher (comté humain, BD-T6) ; faction
  reprise du lore.
- Le **palais du Maître des Dragons** reste un POI (`poi_palais_passe`). La Cité du Jade,
  la Nécropole du Jade et le Krak de Jhenner n'ont pas de cellule et restent hors carte.
- Noms : Unnamed-64 → **Runhkar**, Unnamed-143 → **Klaerhin** (noms libres de
  `carte_export.json`).
- **Dhemorr** passe de N014_E016 (cellule de Hoengardt) à N014_E017.
- **10 villes** sont créées sur les nouvelles terres : Khaldhol, Vernhild, Dhraegtun,
  Shtenbrak, Ryvaldh (port à l'embouchure de la rivière de Rhivkhar), Grhavelt,
  Dhuzvelt, Ghrondak (nains), Faervosk, Thysskar.
- Le master compte **90 agglomérations** et le lore 93 villes (1 008 PNJ de service).
- Nouveaux POI : Haawling Spines (N002_E006), cratère du volcan (S008_E014), entrée des
  Abysses (S008_W005) ; genres `volcan` et `gouffre` ajoutés au client.

### 5.4 Eau, forêts, champs

- **Rivières** : de Rhivkhar (N010_E019 → Rhivkhar → Ryvaldh, mer orientale) et de
  Suthvarn (S013_E006 → Suthvarn → mer du sud).
- **Lacs de plaine** : Klehrwath (S014_E014), Hilvakh (N010_E017), Mhillenz
  (S002_E002), Nakhtvar (N001_E012), Mhordaal (S011_E021).
- **Forêts** (avec lisières) : Vlakmorh (N007–N005, E016–E018), Harndrecht
  (S005–S006, W010–W008), Melvhord (S011–S012, E001–E003), Landes (N016–N015,
  E019–E020) ; **marais des landes** en N013_E017.
- **Champs cultivés** autour de toutes les agglomérations habitées : 2 à 4 petits champs
  par hameau ou village (comme avant), 4 à 6 grandes parcelles par ville, grande ville,
  forteresse ou capitale.

## 6. Tomes 4 à 6 — « Quand sifflent les serpents », « La Danse écarlate », « La Couronne des ombres » (v1.0.53)

Références : T4 `pl.N` = planche imprimée (page du PDF moins 2) ; T5 et T6 `p.N` = page
imprimée (page du PDF plus 2).

### 6.1 Identités et graphies tranchées par la BD

| Question | Réponse | Preuve |
|---|---|---|
| Succube | Graphie **Hellaynnea** (confirmée sur l'album) ; « Hellavnnea » remplacée dans tous les textes, identifiants inchangés (`char_succubus`, `hellavnnea_la_succube`…). | « Hellaynnea ça suffit ! » (T5 p.9). |
| Esmeralda | **Apparence** prise par Hellaynnea pour vider l'âme d'un homme blond anonyme de Moork ; `char_esmeralda_succubus` fusionnée dans `char_succubus`. La victime n'est **pas** Fratus. | T6 p.22-25 ; Fratus repart vers Altenberg (p.13) et reparaît p.48. |
| « Grand Maître Lion » | C'est **Fratus Sinister** : `char_grand_maitre_lion` fusionné dans `char_fratus_sinister`, dialogue et règle repointés. Les deux « discrets observateurs » sont **Haazheel et Greldinard**. | T6 p.48. |
| Titan de Tsaroth | **Haazheel** se transforme en titan à quatre bras ; Wismerhill n'engage pas la Garde Noire. `creature_titan_wismerhill` renommée (identifiant inchangé), note fausse de Wismerhill retirée. | T5 p.39, 43-46. |
| Conseil de Moork | **Selinporf**, **Pharix**, **Hokhost** (au lieu de Selindorf, Dharix, Hokhosi). | T6 p.30. |
| Dragonnet | Wismerhill l'appelle **« Krukh »** (et non « Kruki »). | T6 p.21. |
| Urmarcht | Graphie « Urmacht » aux T4-T5, notée sans renommage. | T4 pl.15, 17 ; T5 p.11. |
| Pilou au T5 | C'est **Pile-ou-Face** (crinière noire, oreilles pointues), pas un goblinoïde. | T5 p.27. |
| Garundel | Vassal d'Haazheel, « un Dvorak » (sans doute Dzorak). | T4 pl.13. |
| Autres | Sysigie (et non « Sysgié »), Serpent « Hérault », alias « Dis » de Dys, garde Gaspard, Nasha « Pépette », « sire Cédric ». | T4 pl.9, 20, 36 ; T6 p.32, 51. |

### 6.2 Lieux

| Lieu | Décision |
|---|---|
| Tsaroth (`ville_tsaroth`) | **Sans service** (« lieu désolé et loin de tout », T5 p.23) : 6 PNJ de service retirés, cellule S009_E011 reportée. Le camp de départ des Démons reste dans `world_master.json` en attendant son déplacement (voir 9). |
| Faille de Tsaroth | Tenue par la **Lune Noire** (et non la Lumière), cellule S009_E011. |
| Krak de Jhenner | Ne « donne » plus « accès à la Faille » : il tombe avant la citadelle impériale, dernier obstacle (T5 p.22-23). |
| Nouveaux lieux | `lieu_passe_heniak`, `lieu_citadelle_imperiale_tsaroth`, `lieu_palais_lune_noire_reflet` (tour et arche verte au bout de la baronnie de Moork), `lieu_palais_lune_noire_lunaire` (hors carte). |
| Moork | Baronnie de Greldinard puis de Wismerhill ; le palais d'Haazheel est sur la Lune (T6 p.33-34). |
| Sysigie | Biome montagnard, au-dessus d'une mer de nuages (remplacé en v1.0.54 : île fluviale, voir 7.2). |
| Horkher | Notes des cartes du T6 (Horkher au-dessus de Moork, cohérent avec N006_E014) et du titre « duc des Marches ». |

### 6.3 Frise et règles

- 9 évènements corrigés : dénonciation de Fratus par son agent double, massacre orkh (sans
  Fratus), serment de Wismerhill (pas encore baron ; Garde Noire 500 + 3 000), bataille de
  Tsaroth (titan = Haazheel, défaite mise en scène), retour à Moork, succube et âme
  solitaire, palais lunaire, observateurs Haazheel et Greldinard (2 évènements).
- 2 évènements ajoutés : chute du Krak de Jhenner, villes qui se rendent (Arthus, Shamballeau).
- Plan de bataille de Tsaroth réaligné sur la carte de T5 p.34 (le « commandeur roux »
  n'existe pas).
- Quête `quest_esmeralda_succube_t6` : les joueurs découvrent qu'Esmeralda est la succube du baron.
- Tomes des personnages normalisés en `tNN` ; T4-T6 ajoutés aux fiches concernées.

## 7. Tomes 7 à 14 — de « De Vents, de Jade et de Jais » à « La Fin des Temps » (v1.0.54)

Albums : T7 « De Vents, de Jade et de Jais », T8 « Le Glaive de justice », T9 « Les Chants
de la négation », T10 « L'Aigle foudroyé », T11 « Ave Tenebrae », T12 « La Porte des
Enfers », T13 « La Prophétie », T14 « La Fin des Temps ». Références : T7 à T12 `p.N` =
page (planche) imprimée ; T13 et T14 `p.N` = page du PDF (aucun folio fiable). Script
reproductible : `lore_t7_14.py` (appliqué au fichier v1.0.53).

### 7.1 Identités et graphies tranchées par la BD

| Question | Réponse | Preuve |
|---|---|---|
| « Prince des Ténèbres (fils de Haazheel) » | C'est **Lucifer**, le **père** d'Haazheel : `char_prince_tenebres_t11` fusionné dans `char_lucifer_prince_tenebres` ; règle `regle_seuil_infernal_t11` corrigée (le sceptre de commande est remis à Haazheel). | « Voilà, mon père, c'est fait » (T11 p.35) ; « père d'Haazheel Thorn » (T12 p.7). |
| « Le Baron (aspirant Empereur) » | C'est **Fratus** (cache-œil) ; le « Baron » de Moork à qui il promet l'Empire est **Greldinard**. `char_baron_aspirant_t13` fusionné dans `char_fratus_sinister`, mort (`alive` faux). | T13 p.12-14 ; « Je suis un guerrier, pas un politicien ! » (Greldinard, p.12). |
| Pilou | **Surnom de Pile-ou-Face** : `char_pilou` (« sœur gobeline de Goum », morte) fusionné dans `char_pile_ou_face`. | T2 p.8, 10 ; T9 p.17 ; T14 p.36. |
| Serpent | Le « Serpent Hérault de Methrhton » (T4-T5) et le Serpent de Methraton (T9-T14) ne font qu'un [DÉDUCTION] : `char_methrhton_herald` fusionné dans `char_serpent_methraton`. | Même titre, même rôle d'observateur (T5 p.25, T10 p.16, T11 p.42). |
| « Fils de Lucifer » | C'est **Haazheel** qui le dit ; Haazheel ignore « de quel démon » descend Wismerhill, « enfant des Vents ». L'appel « Ton sang ! » vient des **esprits des vents**, pas de l'Oracle. Notes fausses de Wismerhill et de l'Oracle retirées. | T13 p.38-40. |
| Hellaynnea et sa fille | Les notes T13 de la fiche de la fille (`char_hellaynnea_t12`) concernaient la mère : déplacées vers `char_succubus`. | T13 p.7, 13-14, 40. |
| Bataille finale du T13 | Dans la **salle du trône du palais de Lhynn** [DÉDUCTION], pas à Moork (Haazheel y rassemble seulement son armée) : textes, zone `zone_moork_salle_trone_t13` et raid renommés (identifiants inchangés). | T13 p.6, 20-22 ; T14 p.5. |
| Fratus au T11 | Il **accompagne** Haazheel dans l'extermination d'une ville du Sud ; ni « royaume de Fratus » attaqué, ni âmes de Fratus dévorées. | T11 p.44. |
| Statuts | Morts : Haazheel (T13, âme dévorée T14), Fratus (T13), Urmacht (T12), Gunthar (T8). Vivants : le père de Wismerhill (mort simulée par Urmacht), Iaun Reith (« seul survivant », T10 p.30). | — |
| Répliques | « Finalement vous vivrez, Xhan » est de **Fratus** (T10 p.47) ; « Ce combat inepte n'est pas le mien ! » (Greldinard, T13 p.26) ; les démons lacérateurs sont ceux d'**Haazheel** (p.28). | — |
| Graphies | **Grhumpdeth** (lore « Grhulnpdeth » corrigé ; « Grumpedth » au T7), **Hellwelkin** (lore « Hellvekkin »), Lame du **Feu** Solaire (lore « Feli »), Methraton (sans accent). Notées sans renommage : Hellaynea (T7), Dharix (T7, Pharix au T6), Syzigie (T8), Haazeel (T9), HAAGHENDORFF (T10), Urmacht, Heusgly (forteresse naine, T11), Oghan (T12), Feyhin Lockthat et Magistra (T14), Der Hem Shelbem (ville du lore « Der hem Shelsem », alias ajouté). | — |
| Auberge de Dis | « l'Auroch Assoiffé » (lecture retenue) en alias de « l'Auroch Assomé » (ancienne lecture). | T4 pl.17. |

### 7.2 Lieux

| Lieu | Décision |
|---|---|
| Lhynn | **Décision du propriétaire** : au bord de l'océan / la mer de l'ouest, pas d'un lac. Le « lac » des vues T3, T11 p.38 et T14 p.25-33 est la baie en fjord ; « dernier coucher de soleil sur la mer » (T14 p.40). |
| Sysigie (alias Syzygie) | **Décision du propriétaire** : cité sur une **île au milieu d'un grand fleuve** ; le brouillard fréquent sur le fleuve produit la « cité dans les nuages » (T4, T10) ; île à tour-cathédrale (T9 p.39) ; la principauté est bordée de montagnes, accès par une gorge (T11). Biome, topographie et hydrographie corrigés ; projetée dans un autre plan au T11. |
| Horkher | Notes BD (T6) de la fiche `horker` (Harrodh, N004_E009) déplacées vers `city_horkher` (N006_E014). Harrodh reste une ville du jeu (`RENOMMAGES` du générateur). |
| Der Hem Shelbem | Province du Grand Sud, au sud de Moork ; on y affronte le prince mort-vivant archimage (la note « Dragon Rouge » est retirée). |
| Altenberg, Northind, Moork, Feyhin, Magistrya | Notes : Altenberg côtière au nord de Lhynn, démantelée (T11) ; Northind dans les marches nord-ouest ; Moork, Feyhin et Magistrya détruites au T14. |
| Nouveaux lieux | `lieu_temple_methraton_lhynn`, `lieu_port_franc`, `lieu_tharsulatis` (hors carte), `lieu_seuil_lhynn`, `lieu_defile_mordrek`, `lieu_grand_hall_nains`, `lieu_sanctuaire_chevaliers_dragons`, `lieu_ville_moros` — placés en v1.0.55 (section 8), sauf Tharsulatis. |
| « Empire du Jade » | **Aucun appui dans l'album T7** : ni Empire, ni cité, ni temple, ni seigneur « du Jade ». Conservé comme extension de jeu `[GD]` (voir 7.4). Les zones de « voyage mystique » sont les épreuves de l'initiation (p.22-27) ; la « Nécropole du Désert » est celle de Der Hem Shelbem. |

### 7.3 Frise et personnages

- T7 : 5 évènements « Jade » réécrits sur la BD (séjour à Lhynn, temple de Methraton,
  initiation, conseil des Vents, nécropole de Der Hem Shelbem) ; `event_t7_alliance_jade`
  marqué `[À VÉRIFIER]` ; 3 ajoutés (chute de Northind, palais lunaire, Fratus averti).
- T8 : 5 évènements ajoutés ; T9 : 6 évènements nommés et datés, 1 ajouté.
- T10 à T14 : les **85 fiches vides** de la frise (`tl_t10_*`, `timeline_t11_*`,
  `timeline_t12_*`, `tl_t13_*`, `tl_t14_*`) remplies, identifiants inchangés.
- Personnages ajoutés : Snippy (T7), Shenniterrann (dragon d'or de l'Empereur, T8-T10),
  le Haut Elfe et Turuk (T14). Tomes ajoutés aux fiches (115 entrées, format `tNN`) ;
  notes `[BD-Tn]` paginées ajoutées ou corrigées sur 46 fiches.

### 7.4 Décisions du propriétaire (05/11/2026)

| Sujet | Décision |
|---|---|
| Empire du Jade | Extension de jeu `[GD]` (territoire à ouvrir plus tard, ouverture au monde) : contenu conservé, notes passées en `[GD]`. Le T7 n'en montre rien. |
| Ascendances | Haazheel Thorn est le **fils de Lucifer** [BD-T12 p.7 ; T13 p.40 ; T14 p.7]. Wismerhill est le **fils de Pazuzu** (`char_pazuzu`) : règle `regle_wismerhill_fils_lucifer_t13` réécrite et compétence renommée « Forme Démoniaque — Fils de Pazuzu » (identifiants inchangés). |
| Époque du jeu | **Avant** les destructions des T11 et T14 : Altenberg, Sysigie, Moork, Feyhin Lokcthat et Magistrya restent intactes dans le monde jouable. |
| Pile-ou-Face, Goum | Pile-ou-Face est un **elfe** (sous-race à préciser), Goum un humain : tous deux retirés de `rule_gobelins_size_advantage`. |
| Orthographe | « L'hynn » → **Lhynn** et « Der hem Shelsem » → **Der Hem Shelbem** dans le lore, `world_master.json` et `factions.json` (identifiants inchangés). |
| Carte | Placement des nouveaux lieux, du fleuve de Sysigie et d'autres rivières et lacs : PR de carte à part. |

### 7.5 Points ouverts restants

Traités en v1.0.55 (section 8) : placements, fleuve de Sysigie, Haut Tharque = Meldrim, retour de
Parsifal, Kruikna / Cruik / dragon de Pilou, titre de Greldinard. Reste ouvert (section 17) : le nom
de la fille de Wismerhill.

## 8. Placements T7-T14 et fleuve de Sysigie (v1.0.55)

Placements des lieux des tomes 7 à 14 dans le lore (`cell`) et dans le monde jouable
(`world_master.json`). Script reproductible : `lore_v1055.py` (appliqué au fichier v1.0.54).

### 8.1 Lieux placés

| Lieu | Cellule | Monde jouable | Justification |
|---|---|---|---|
| Temple de Methraton (`lieu_temple_methraton_lhynn`) | N001_W010 | POI `poi_temple_methraton` (`temple`) | **Décision du propriétaire** : dans Lhynn. |
| Seuil de Lhynn (`lieu_seuil_lhynn`) | N001_W010 | POI `poi_seuil_lhynn` (`portail`, sans liaison) | **Décision du propriétaire** : près du palais de Lhynn (T14 p.26). |
| Forteresse naine d'Heureux / « Heusgly » (`ville_forteresse_naine_heureux_t11`) | S006_E004 | forteresse naine déplacée de S004_W004 | **Décision du propriétaire** : refuge des neuf mères (T9 p.26) et forteresse « d'Heusgly » (T11 p.36). |
| Sanctuaire des Chevaliers Dragons (`lieu_sanctuaire_chevaliers_dragons`) | S003_W004 | POI `poi_sanctuaire_chevaliers_dragons` (`sanctuaire`) | **Décision du propriétaire** : case du palais du Maître des Dragons. |
| Hoengardt (`ville_hoengardt_t11`) | N014_E016 | cellule inchangée, type `ville` → `forteresse` (comme le lore) | [INFÉRÉ] Plaine au pied de la chaîne, dans la région des Marches du Nord ; l'armée non humaine doit y « remonter » (T11 p.25). |
| Port Franc (`lieu_port_franc`) | S003_W013 | POI `poi_port_franc` (`port`) | [INFÉRÉ] Plage de la côte ouest, ~40 km au sud de Lhynn : la flotte tharque vient de la mer de l'ouest (T10-T11) et le raz-de-marée tharque doit atteindre Lhynn (T14 p.40) ; Altenberg tient la côte au nord. |
| Défilé de Mordrek (`lieu_defile_mordrek`) | N016_E013 | POI `poi_defile_mordrek` (`defile`) | [INFÉRÉ] Chaîne (code `M`) des Marches du Nord, entre les Terres des Gigantomorphes (N019+) et le sud : les géants « descendaient vers Lhynn » (T14 p.28). |
| Moros (`lieu_ville_moros`) | N000_W007 | POI `poi_moros` (`cite`, ville non bâtie) | [INFÉRÉ] Aucune indication : plaine des Terres de l'Empereur (seigneur « félon » de l'Empire, T12 p.22). Intacte à l'époque du jeu. |
| Grand Hall des nains (`lieu_grand_hall_nains`) | S008_E014 | POI `poi_grand_hall_nains` (`salle`) | [INFÉRÉ] Sous le volcan (`poi_volcan_cratere`), à une case de Ghrunke Dash : salle sous un volcan, cité naine au pied du cratère (T14 p.39). |

- Plusieurs POI d'une même case (Lhynn, S003_W004, S008_E014) : le générateur les écarte
  d'un quart de tour autour de la ville de la case (ou de 400 m), sans changer les POI existants.
  Les POI restent hors du plateau urbain : temple et Seuil sont des marqueurs aux abords de Lhynn.
- Nouveaux genres de POI côté client (`PlaceKindLabel`) : `temple`, `sanctuaire`, `port`, `cite`,
  `defile`, `salle`.
- Aucune de ces cellules n'est générée par défaut (`generated_cells` inchangé).

### 8.2 Fleuve de Sysigie

**Décision du propriétaire** : Sysigie (N009_W011) est une île au milieu d'un grand fleuve.
**Mise à jour v1.0.56** (section 10, Q5 ; **annulée en v1.0.75**, voir §16.4) : la cité s'affichait « **Aurore** » (alias Sysigie, Syzygie ;
identifiant `ville_sysigie` inchangé), capitale de la principauté de Syzygie ; le grand fleuve traverse
le **lac de Syzygie**, au milieu duquel se trouve l'île. Tracé et grille ci-dessous inchangés.
Tracé [INFÉRÉ], nom provisoire « [à nommer] » (le propriétaire nommera fleuves, rivières et lacs) :

- source dans la lisière de la forêt occidentale (N006_W010), vers le nord par N007_W010
  (la rivière de N007_W011 s'y joint), N008_W011, **l'île de Sysigie** (N009_W011), N010_W011,
  N011_W011 ; embouchure dans la mer du nord en N012_W011 ;
- grille : N006_W010 et N007_W010 `t` → `R`, N008_W011 à N011_W011 `.` → `r` (lore : cellules
  `forest_with_river` / `plain_with_river`, `has_river`) ; lore : `hydrography.rivers` →
  `riv_fleuve_sysigie` ;
- monde jouable : nouvelle section `river_courses` du master (`fleuve_sysigie`, `width_m` 80,
  `island_city` `ville_sysigie`). Le générateur suit les centres des cases (le centre de la ville
  remplace celui de sa case), contourne les autres villes, ajoute de grands méandres, puis descend
  vers la mer depuis la case d'embouchure. Autour de l'île, le lit se divise en **deux bras**
  (~40 m) écartés d'environ 340 m du centre, hors du plateau urbain ; les routes les franchissent.
  Largeur ~50 m à la source, 80 m à l'embouchure.
- Limite : les lieux procéduraux de la rive opposée aux routes n'ont pas toujours de chemin local
  (avertissement « chemin impossible » du générateur dans N008_W011 à N011_W011).

### 8.3 Notes du propriétaire

| Sujet | Décision |
|---|---|
| Haut Tharque | **Est Meldrim**, Empereur de Tharque : `char_haut_tharque` (T9 p.44) fusionné dans `char_meldrim_t11` (T11 p.16-17) ; alias « le Haut Tharque » dans la description, tomes T9 et T11. |
| Enfants de Wismerhill | Plusieurs enfants (T12) ; « Elle s'appelle Hellaynnea, n'est-ce pas ? » (T12 p.7) reste à préciser. |
| Dragons | Kruikna (T3-T6), « Cruik » (T9 p.36) et le dragon bleu de Pilou (T11) sont **trois dragons différents** ; fiches non fusionnées, note ajoutée sur chacune. |
| Parsifal | Caché dans une cité au T12 (simplement plus montré) : son retour n'est pas une incohérence. |
| Greldinard | « Baron » (T10-T13) est l'appellation de la BD : aucun fief supplémentaire. |

## 9. Plans de référence

Plans vus du dessus (repères pleins = BD, en pointillés = lore placé) publiés comme
documents de conception : Kendrhir (en reconstruction), Feyhin Lokcthat, Magistrya,
puis Mont des Neuf Pics, l'Atmosphère, le palais du Maître des Dragons, Altenberg et
l'Oracle (extérieur) ; pour le T3 : Ghrunke Dash, Lhynn, le village fantôme (village et
marais) et le fief de Garundel (circulaire, douves, tour ronde, logis accolé).

## 10. Albums hors série (Tome 0, L'Empire de la Négation, Methraton 1-3, Arcanes 1-5) (v1.0.56)

Intégration des dix albums hors série. Les **25 propositions par défaut** soumises au propriétaire
sont toutes acceptées, plus un ajout du propriétaire (Ruines de Wismerhill sur la carte jouable).
Script reproductible : `lore_hs.py` (appliqué au fichier v1.0.55).

### 10.1 Albums et références

| Code | Album | Identifiant (`tomes_config`) | Référence | Époque |
|---|---|---|---|---|
| T00 | Tome 0 « En un jeu cruel » | `t00` | `[BD-T00 p.N]`, planche imprimée | avant T1 → seuil du T1 |
| THS | « L'Empire de la Négation » (encyclopédie illustrée) | `t_hs_empire` | `[BD-THS p.N]`, page imprimée | après T11, avant T13 |
| M1 | Methraton 1 « Le Serpent » | `t_hs_methraton1` | `[BD-M1 p.N]`, page du PDF | avant T1 |
| M2 | Methraton 2 « Le Crâne » | `t_hs_methraton2` | `[BD-M2 p.N]`, page du PDF | avant T1 (3000 ans après la chute de Methraton) |
| M3 | Methraton 3 « Pharaon » | `t_hs_methraton3` | `[BD-M3 p.N]`, page du PDF | avant T1 (au plus tard avant T4) |
| A1 | Arcanes 1 « Ghorghor Bey » | `t_hs_arcanes1` | `[BD-A1 p.N]`, folio imprimé | avant T1 → seuil du T1 |
| A2 | Arcanes 2 « Pile-ou-Face » | `t_hs_arcanes2` | `[BD-A2 pl.N]`, planche | avant T1 → seuil du T1 |
| A3 | Arcanes 3 « Parsifal » | `t_hs_arcanes3` | `[BD-A3 pl.N]`, planche | avant T1 |
| A4 | Arcanes 4 « Greldinard, première époque » | `t_hs_arcanes4` | `[BD-A4 p.N]`, page imprimée | avant T1 (longtemps) |
| A5 | Arcanes 5 « Greldinard, deuxième époque » | `t_hs_arcanes5` | `[BD-A5 p.N]`, page imprimée | avant T1 (proche du T1) |

Chaque fiche nouvelle ou complétée porte une note `[ÉPOQUE]` (avant T1, seuil du T1, après T11…).
Frise relative (Q3) : A4 → A5 → M2/M3 → A3, puis T00, A1 et A2, qui convergent au seuil du T1.

### 10.2 Contenu ajouté

- **Personnages (45)** : A1 (Éléonore, Will, Wilf, le Magnifique, Hélène, Minidou, Thenhart, le Capitan
  Thurgen, Helmen d'Horst, le Grand Trésorier du Nord), A2 (Ghrukkkkh, la sœur de Pile-ou-Face, la dragonne
  et le dragonnet, le marchand borgne, le Maître de la guilde de Khurden, le Surin, la Tendresse, Chérubin),
  A3 (Willhem de Syzygie, la dame de Syzygie, Couette, le chef des géants, le démon-sanglier), A4 (Ogunta,
  Grundur, Shaadrak, le commandant et le prêtre du Château de la Mort), A5 (Phocrathus, Hermann, Glurkha, la
  démonesse à la couronne d'épines), Methraton (Hokkham, Bastet, Râ, Ptah, Pharaon, l'archimage d'Erypt,
  Smenethep, Tanith, Hopt, Merweenn, le Grand Vizir, Narankhorhh).
- **Lieux (19, `lieux`)** : Ruines de Wismerhill (placées en N011_E008, fusion de `ville_chateau_ruine_t00`, voir 10.4) ; Erypt, Memphis, Thèbes, Île des Mages,
  tête de mort volante, village de Sekh, plan de Methraton, plan des aiguilles, Montagne de Feu, pays Krell
  (hors carte) ; Khurden, Rériah, Almuria, Château de la Mort, village natal de Ghorghor, ruines de Thenhart,
  sanctuaire des deux fois nés, Passe de Syzygie (sans cellule, placement : PR de carte à part).
- **Régions (2)** : `region_marche_est` (Marche de l'Est), `region_hishtarland` (royaume d'Hishtarland,
  = région `hishtarland` du monde jouable). **Races (3, PNJ)** : `race_half_orc` (demi-orque),
  `race_half_ogre` (demi-ogre, déjà référencée sans fiche), `race_elfe_des_bois`.
- **Objets (5)** : ankh de Methraton, épée de Lumière de Parsifal, Fiat Mortis, heaume de Greldinard,
  anneau d'Ogunta ; notes A1/A2 sur l'anneau de régénération, Bepher et Nepher.
- **Frise (16)** : `tl_hs_*`, de la chute de Methraton au règne de Wismerhill Ier ; règle
  `regle_epoque_hors_serie` (Q1-Q3) ; 8 albums ajoutés à `tomes_config`.
- **Fiches existantes complétées** : Wismerhill, Pazuzu, Ghorghor, Pile-ou-Face, Greldinard, Haazheel,
  Methraton, le Serpent, Parsifal, Haghendorf, Feidreiva, Urmarcht, Okhost, Moork, Lhynn, Syzygie, etc.

### 10.3 Décisions du propriétaire (05/11/2026)

| N° | Sujet | Décision appliquée |
|---|---|---|
| 1 | Époque de THS | « Après T11 ». Entrent dans le monde jouable : Via Magna, Hishtarland, plan de Lhynn (chenal, deux enceintes, port ; ancien palais conservé). Institutions du nouvel empire, porte des Enfers ouverte, bâtons de prière : lore `[HS]` hors époque. |
| 2 | Préquelles | État de fin d'album puis évolution des T1-T10 : lieux détruits en ruines, lieux debout intacts, PNJ morts absents (souvenir, tombe), PNJ vivants sans suite seulement si une quête les utilise (`regle_epoque_hors_serie`). |
| 3 | Frise | A4 → A5 → M2/M3 → A3, puis T00, A1, A2 au seuil du T1 ; positions M2, M3, A3 `[HYPOTHÈSE]` ; seule durée chiffrée : 3000 ans. |
| 4 | Methraton, archimages, Erypt | Methraton caché dans son plan, accessible par son temple de Lhynn ; archimages libérés avant le T9 (`tl_hs_liberation_archimages`) ; panthéon d'Erypt = culte lointain, sans dieu incarné. |
| 5 | Sysigie | Syzygie = principauté (région `syzygie`) ; `ville_sysigie` affichée **« Aurore »** (alias Sysigie, Syzygie) dans le lore et `world_master.json` ; le fleuve traverse le lac de Syzygie, île dans le lac, brouillard conservé ; Passe et muraille = gorge du T11 et Portes d'Aurore ; carte THS = schéma. |
| 6 | Père de Wismerhill | Pazuzu : `char_wismerhill_father` **fusionné** dans `char_pazuzu` (forme elfique ; vivant ; 12 références repointées : quête, dialogues, règles) ; le vieux seigneur du T00 est le père légal. |
| 7 | Nom de Wismerhill | Vient de la colline (T00) ; Pile-ou-Face le redonne au T1 sans le savoir (clin d'œil des Vents) ; événement T1 conservé. |
| 8 | Anneau de Ghorghor | Décision T3 conservée : l'anneau d'A1 (`loot_anneau_regeneration`) est historique, perdu ou brisé avant le T3 ; distinct de l'anneau du T11. |
| 9 | Dvorak / Dzorak | Espèce ancienne des Marais maudits, graphie « Dvorak », alias « Dzorak » ; `creature_dzorak_marais_t11` affichée avec l'alias « Dvorak des Marais » ; faction `dzorak_orcs` à revoir à part ; Ghorghor passe en `race_half_ogre`. |
| 10 | Moork | Greldinard baron jusqu'au T5, Wismerhill du T5 au T11, puis Greldinard (THS, hors époque) ; à l'époque du jeu : baronnie de Wismerhill, Greldinard seigneur de guerre. |
| 11 | Qui Methraton a servi | Le « roi des rois » (pharaon ancien, mort) ; les « maîtres » de M1 sont ses archimages emprisonnés. |
| 12 | Graphies | Formes du lore conservées ; nouvelles : Erypt (érypte), Ptah, Tanith, Menthat(s) ; variantes en alias (Horker, Magestrya, Ishtarland, Feydriva, Haazhell…) ; noms affichés « Hellwelkin » (`loot_hellvelkin_hs`) et « Menthat » (4 compétences et règles), identifiants inchangés. |
| 13 | Okhost / Hokhost | Même personnage `[HYPOTHÈSE]` : `char_okhost` reçoit l'alias Hokhost ; renvoi croisé avec `char_hokhosi_moork` (fiches non fusionnées). |
| 14 | Ogur, Ogun, jumelles | « Ogur » nom de scène canonique, « Ogun » alias ; `char_ogun_jumelles_t11` renommée « Carole et Caroline (les jumelles de Ghorghor) » (humaines, compagnes et non filles) ; aucun lien avec Ogunta. |
| 15 | Marche de l'Est | Région `region_marche_est` (Moork, Horkher, `raid_marches_est_t14`), distincte des Marches du Nord ; Moork ancienne cité impériale conquise. |
| 16 | Erypt | Hors carte (`lieu_erypt`, « Grand Sud lointain ») avec Memphis, Thèbes, Île des Mages et tête de mort volante en sous-lieux ; récit ou quête de mémoire seulement. |
| 17 | Orkland | Nom orque de `territoires_orcques` (alias) ; ses marches = frange frontalière ; Montagne de Feu et pays Krell hors carte. |
| 18 | Carte THS | Hishtarland = royaume (`region_hishtarland`), capitale à la même case ; Via Magna = route principale Lhynn → Moork (tracé : PR de carte) ; Magistrya garde sa case ; « Royaume Drach » retenu. |
| 19 | Lieux des Arcanes | Une seule forêt elfique, enclavée dans l'Empire ; Khurden près de sa lisière orientale, Rériah et Almuria dans les Marches du Nord, Château de la Mort en bordure de l'Orkland — fiches sans cellule, placement dans la PR de carte. |
| 20 | Zones hors carte | Palais lunaire, Enfers, plan de Methraton, plan des aiguilles : hors carte et non accessibles ; le « reflet » du palais reste visible et non franchissable. |
| 21 | Pile-ou-Face | Elfe des bois (sylvain) (`race_elfe_des_bois`), petit enfant, taille normale adulte ; barbe atypique ; alias « Nelfe ». |
| 22 | Greldinard | Race de jeu demi-orque (`race_half_orc`) ; secret conservé ; esprit d'Ogunta lié à son anneau (`loot_anneau_ogunta`). |
| 23 | Visuel d'Haazheel | Turban et gemme verte ; heaume cornu = tenue de guerre ; gemme jaune d'A4 non retenue. |
| 24 | Sekh = le Serpent | `char_serpent_methraton` : passé « Sekh, enfant d'Erypt, élu de Bastet », alias Sekh / Sekhmet ; transformation inexpliquée. |
| 25 | Empereur des préquelles | Haghendorf dans A1, A3, A5 ; prédécesseur non nommé dans A4. |

### 10.4 Ruines de Wismerhill (ajout du propriétaire)

Le château de la colline des murmures (enfance de Wismerhill, incendié au T00) est **en ruine** à
l'époque du jeu et figure sur la carte jouable : lieu `lieu_ruines_wismerhill` (alias colline des
murmures, château de Wismerhill, Wisperhill, Château en Ruine du Vieux Seigneur), région
`region_terres_nord_t00`.

**Décision du propriétaire** : le village vivant `ville_whispers_hill` (N011_E008, ville de départ des
elfes) **reste tel quel** ; le château en ruine est un POI de la **même case N011_E008**, à côté du village.

- Monde jouable : POI `poi_ruines_wismerhill` (genre `ruine`) en N011_E008. Le générateur le pousse hors
  du plateau du village et de son raccord (rayon du village + marge), sans chevauchement. Plus rien en
  N009_E017 (première proposition abandonnée).
- Doublon fusionné : `ville_chateau_ruine_t00` (« Château en Ruine du Vieux Seigneur », ville sans cellule
  dans le lore, hameau en ruine en N016_E015 dans le monde jouable) est fusionné dans
  `lieu_ruines_wismerhill` : nom gardé en alias, 4 dialogues du T00 repointés (`location_ville_id`), ville
  retirée de `world_master.json` (88 villes) et entrée retirée de `TAILLE_VILLES`
  (`tools/lore/generate_city_services.py`).
- `region_terres_nord_t00` et `zone_whispers_hill` renvoient à la case N011_E008 et au lieu fusionné.

### 10.5 Points ouverts restants

- Tracé de la Via Magna : PR de carte à part (Q18), tracé en v1.0.60 (section 13). Khurden, Rériah, Almuria, Château de la Mort,
  Passe de Syzygie et Marche de l'Est : placés en v1.0.59 (section 12).
- Faction `dzorak_orcs` à revoir (Ghorghor demi-ogre, Dvorak créature distincte) (Q9).
- Lecture « Ogun » du T11 à revérifier (Q14).
- Okhost / Hokhost : identité `[HYPOTHÈSE]`, fusion éventuelle des deux fiches.
- L'impératrice du THS (`char_imperatrice_hs`) = Hellaynnea : `[HYPOTHÈSE]` forte, fiches non fusionnées.
- Page imprimée 037 d'A5 manquante (copie de la page 025) : événements entre la mort de Glurkha et Moork inconnus.

## 11. Monde complété en [GD] : bestiaire, religions, magie, économie (v1.0.58)

La BD étant muette sur ces domaines, le lore les complète en `[GD]`, ancré sur ce que les 24 albums
montrent (références `[BD-…]`), sans contredire une `[DÉCISION]` ni une source de rang supérieur
(`regle_canon_priorite_sources`). Tout est dans le JSON :

| Domaine | Où dans le JSON | Contenu |
|---|---|---|
| Bestiaire | `creatures` (champs `habitat`, `zones_chasse`, `groupe`, `raid_id`), `loots`, `cells`, `regle_bestiaire_echelles` | 28 espèces ajoutées, 58 complétées (niveaux, habitat, butin) |
| Religions | `lore.religions`, `regle_calendrier_fetes_religieuses` | 10 cultes, 17 fêtes dans le calendrier lunaire, temples des villes rattachés |
| Magie | `competences`, `lore.magic_system`, `cross_rules.magic_interactions`, `regle_systeme_magie` | 4 sources, 13 écoles, 16 compétences ajoutées, 26 complétées |
| Économie | `regles_transverses` (catégorie `economie`), `game_design.economie.grille_prix` | monnaies, impôts, primes, productions, routes, métiers, prix |

Les choix `[GD]` les plus structurants restent à valider par le propriétaire (liste dans la PR v1.0.58).

## 12. Placements hors série (v1.0.59)

Placement sur la carte des lieux des albums hors série renvoyés à « une PR de carte » (10.3, Q15, Q19 ;
section 11). **Le placement est demandé par le propriétaire** (note `[DÉCISION] (v1.0.59)` sur chaque
fiche, 9 entrées au registre `regle_canon_registre_decisions`) ; **toutes les cellules sont `[INFÉRÉ]`**.
Lore : champ `cell` des lieux, `region_id` et notes des cellules. Monde jouable (`world_master.json`) :
7 POI et 2 régions ; aucune ville ajoutée (les cités non bâties sont des POI `cite`, comme Moros),
`TAILLE_VILLES` inchangé ; aucun lieu existant déplacé, aucune case de ville occupée. Époque du jeu
(après la destruction de Kendrhir, avant le T11) : les lieux détruits dans leur album sont en ruine.
Script reproductible : `lore_v1059.py` (appliqué au fichier v1.0.58).

| Lieu | Cellule | Monde jouable | Justification |
|---|---|---|---|
| Khurden (`lieu_khurden`) | N011_E017 | POI `poi_khurden` (`cite`) | Plaine contre la lisière orientale de la forêt elfique (N011_E016, Q19), adossée à la chaîne de N011_E018 / N012_E017 (« rues en pente », A2 pl.26) ; carte d'A2 pl.14 : la cité est au bout d'une grande forêt de conifères allongée nord-sud, à l'est (nord en haut `[HYPOTHÈSE]`). Intacte. |
| Rériah (`lieu_reriah`) | N018_E015 | POI `poi_reriah` (`cite`) | Marches du Nord (Q19), au pied des forêts et des neiges du nord (N019_E015, N021_E015) ; carte d'A1 p.30 : Rériah à l'ouest d'Almuria (flèche de l'itinéraire), dans les montagnes. |
| Almuria (`lieu_almuria`) | N018_E017 | POI `poi_almuria` (`cite`) | Marches du Nord, sous les montagnes de N019_E017 et les neiges de N020_E018 (grande ville aux tours coniques enneigées, A1 p.30 `[HYPOTHÈSE]`), à l'est de Rériah ; Runhkar (N018_E016) entre les deux. |
| Château de la Mort (`lieu_chateau_de_la_mort`) | N013_E016 | POI `poi_chateau_de_la_mort` (`ruine`) | Chaîne (code `M`) au bord des Territoires orcques (N013_E015, Q19), même chaîne que le défilé de Mordrek aux falaises rouges : forteresse-mine dans une falaise de canyon sous un plateau (A4 p.16-22). **Ruine** (prise par les orques, A4 p.29). |
| Passe de Syzygie et sa muraille (`lieu_passe_syzygie`) | N008_W013 | POI `poi_passe_syzygie` (`defile`) | Au sud-ouest de Syzygie (N009_W011), entre la côte (N008_W014) et la vallée : Passe à l'ouest de la Syzygie, route venue du sud par la côte puis vers l'est (A3 pl.16, pl.41) ; Parsifal part vers Altenberg (N005_W012) par les Portes d'Aurore (T8 p.24-25). Hors du fleuve (N008_W011) et d'Iustarn (N007_W011). |
| Marche de l'Est (`region_marche_est`) | 56 cellules sans région de E013 à E020, S003 à N007 | région `marche_de_lest` (E013-E020, N004-N007) | Extrême est autour de Moork (A5 p.38-44) : comté d'Horkher au nord (N006_E014), confins à l'est de la baronnie. Les cellules de `baronnie_de_moorks` gardent leur région (elle fait partie de la Marche) ; dans le monde jouable, la baronnie garde son rectangle et la Marche couvre la bande au nord (la plus petite région l'emporte, Horkher s'y affiche). |
| Marais maudits de Thenhart (`marais_maudits_t11`) | N010_W005, N010_W004, N010_W003, N011_W005, N011_W004 | région `marais_maudits` (W005-W003, N010-N011) | Cellules de marais du bestiaire v1.0.58 (Dvorak) ; `region_id` `terres_des_gigantomorphes` remplacé. Haevrin (N011_W004) au bord du marais. |
| Ruines de Thenhart (`lieu_ruines_thenhart`) | N009_W004 | POI `poi_ruines_thenhart` (`ruine`) | Forêt traversée par la rivière de la cascade (`riv_cascade_northind`), juste en amont du marais (N010_W004) : château sur un rocher au-dessus d'une rivière qui file vers les marais (A1 p.41-42) ; Wismerhill survole les ruines puis trouve les marais (T11). **Ruine**. |
| Élevages impériaux de béhémoths et léviathans | N002_W008 (pâturages N002_W008 à W010) | POI `poi_elevages_imperiaux` (`elevage`) | Plaines des Terres de l'Empereur du bestiaire v1.0.58 (`[GD]`) ; case commune aux béhémoths et léviathans, hors de Lhynn et du hameau de Marn. |

- Nouveau genre de POI côté client (`PlaceKindLabel`) : `elevage` (« Élevage ») ; `cite`, `ruine` et
  `defile` existaient.
- Cellules générées : seule N002_W008 (voisine du hameau de Marn) l'est ; les autres POI sont hors de
  `generated_cells`.
- Points incertains : orientation des cartes d'A1, A2 et A3 (nord supposé en haut) ; la grille n'a pas
  de chaîne autour de la Syzygie (relief de la gorge à modeler) ; Almuria « plus loin des Marches »
  (A1) alors que Q19 la met dans les Marches du Nord ; Khurden pourrait devenir une vraie ville (fiche
  `villes`, `TAILLE_VILLES`) si une quête l'exige.

## 13. Via Magna (v1.0.60)

Tracé de la Via Magna renvoyé à une PR de carte (10.3, Q18). **Le tracé est demandé par le
propriétaire** (note `[DÉCISION] (v1.0.60)` sur la route et la région `via_magna`, 2 entrées au
registre `regle_canon_registre_decisions`).

- **Route** `000120`, nouveau type `imperial_road` (« Voie impériale », `route_types` ; trait plein
  rouge `#b8322a` ajouté à `ROUTE_TYPES` de l'éditeur) : 34 cellules contiguës (`waypoints`) de Lhynn
  (N001_W010) à Moork (S002_E015).
- **Étapes** : Lhynn, Magistrya (« Magestrya » de la carte), Camp de l'Oracle, fief de Garundel,
  Harrodh, Shezhold, Horkher (« Horker » de la carte, Q12), Nykhfeld, Moork. Sources : carte
  de l'Empire `[BD-THS p.5]` (Lhynn, Magestrya, Horker, Moork ; creux au sud puis remontée à Horker,
  descente au sud-est vers Moork) et texte `[BD-THS p.7]` ; les autres étapes et toutes les cellules
  intermédiaires sont `[INFÉRÉ]` (plus court chemin hors mer, lacs, chaînes, montagnes et volcans).
  Distance de 8 000 km non reproduite (Q1). Aucun fleuve franchi : pas de pont.
- **Routes provisoires** (règle économie v1.0.58) : 000011 (Lhynn → Oracle) et 000012 (Garundel →
  Oracle) supprimées ; 000013 (Garundel → Moork) gardée en raccourci, branche de `axe_via_magna`
  (routes `000120`, branches 000096, 000050, 000013).
- **Écuries** : `grille.ecurie.route_types` = `road` + `imperial_road`. Seules les extrémités d'une
  route comptent : Moork +1 palefrenier, Camp de l'Oracle −2, fief de Garundel −1 (998 PNJ).
- **Monde jouable** : `world_master.json` n'a pas de routes et `tools/world_gen` trace les siennes
  (Kruskal + A* entre les villes du master) sans lire les routes du lore : rien à reporter.
- Points incertains : étapes intermédiaires ; la région `via_magna` ne suit pas le tracé
  sur toute sa longueur (inchangée).

## 14. Départs par faction (v1.0.61)

**Décision du propriétaire (06/11/2026)** : « Les départs ne sont pas en fonction de la race, mais de
la faction. » Notes `[DÉCISION] (v1.0.61)` sur les fiches (factions, hameaux, lieux, anciens départs,
`rule_niveaux_par_faction`) et 26 entrées au registre `regle_canon_registre_decisions`. Les anciens
départs par race (Hameau de Marn, Whispers Hill, Ghulvathan, Ghrunke Dash) redeviennent des villes
normales ; leurs cellules restent générées. « Pas loin de X » : un **nouveau hameau** dédié à la faction,
dans une case voisine de X (libre, habitable, hors de l'eau), relié à X par une route (`routes` 000121
à 000127, type `road`) et doté des services d'un hameau. Noms des hameaux `[GD]`, **à valider** ;
cellules `[INFÉRÉ]`. Script reproductible : `lore_v1061.py` (appliqué au fichier v1.0.60).

| Faction (lore) | Faction de jeu | Départ | Cellule | Justification |
|---|---|---|---|---|
| Chevaliers de la Lumière (`chevaliers_de_la_lumiere`) | `lumiere` | hameau Orhendal (`hameau_dawnmere`), près d'Altenberg | N005_W011 | Plaine de la Première Province à l'est d'Altenberg (N005_W012) ; ouest et sud sont côte et plage. |
| Chevaliers de la Justice (`chevaliers_de_la_justice`) | `justice` | hameau Ordhrann (`hameau_oathford`), près de Syzygie | N009_W010 | Plaine de la Syzygie à l'est de l'île (N009_W011), hors du fleuve ; la route franchit un bras. |
| Dzorak (`dzorak_orcs`) | `dzorak` | hameau Grakhul (`hameau_grakhul`), près de Ghulvathan | N008_E012 | Lisière de forêt au sud-est de Ghulvathan du monde jouable (N009_E011) ; cellule déjà générée. |
| Chevaliers-Dragons (`faction_dragon_knights`) | `dragons` | hameau Drakhenhal (`hameau_wyrmhallow`), près du Palais des Maîtres des Dragons | S004_W004 | Montagne au sud du palais (S003_W004) ; nord, est et ouest sont des neiges éternelles. |
| Nains (`faction_dwarves`) | `naine` | hameau Durnhalde (`hameau_durnhalde`), près de Ghrunke Dash | S007_E017 | Montagne du royaume des nains de l'Est (S007_E016 à l'ouest), hors des cases volcaniques ; cellule déjà générée. |
| Hauts Elfes (`faction_hauts_elfes`) | `elfe` | hameau Syllaërin (`hameau_silverglade`), près de Northind | N007_W006 | Forêt de la région de Northind à l'ouest (N007_W005), hors des cases à rivière et de la case d'Orkh. |
| Légions Impériales (`faction_legions_imperiales`) | `legions_imperiales` (réservé) | hameau Haeldorf (`hameau_eaglewatch`), près de Lhynn | N002_W010 | Pâturages des Terres de l'Empereur au nord de Lhynn (N001_W010) ; cellule déjà générée, voisine de Marn. |
| La Lune Noire (`faction_black_moon`) | `lune_noire` | DANS le palais de la Lune Noire, sur la Lune (`lieu_palais_lune_noire_lunaire`) | hors carte | Lieu existant ; pas de hameau. |
| Les Archimages (`les_archimages`) | aucune | DANS la pyramide de Methraton (`lieu_plan_methraton`, `zone_flying_pyramid`) | hors carte | Lieu existant ; pas de hameau. |
| La Légion infernale (`faction_legion_infernale`, **nouvelle**) | `legion` | Citadelle de la Légion (`citadelle_legion`), carte des Enfers | Enfers N000_E000 | Lieu central de la croix des 5 cellules des Enfers (9 cellules depuis la v1.0.71, §15.8) ; un démon sort et rentre par n'importe laquelle des 5 portes ([`ENFERS.md`](ENFERS.md) §2-3). |
| Maison du Serpent (`maison_du_serpent`) | `serpent` | **à définir** `[À VÉRIFIER]` | — | Aucun départ créé. |
| Hors-La-Loi (`Hors-La-Loi`) | aucune | **aucun** | — | Faction qui se débloque pendant le jeu (statut acquis). |

- **Correspondance lore ↔ jeu** : nouveau champ `game_design.faction_jeu` des factions (identifiant de
  `game/data/races/factions.json`, aussi `characters.faction_str`) et `game_design.depart` (`type`
  `hameau` / `lieu` / `ville` / `a_definir` / `aucun`, `id`, `cell`, `pres_de`). Les identifiants du lore
  sont inchangés.
- **Faction démon** : le lore n'avait aucune faction démon jouable ; `faction_legion_infernale`
  (`playable` true, `selectable` true) est créée et alignée sur `legion`. Elle reste distincte des
  Légions Impériales.
- **Monde jouable** : `world_master.json`, champ `start_for` (nom inchangé) = identifiants de faction de
  jeu (avant : races) ; 7 hameaux ajoutés après le Hameau de Marn ; `citadelle_legion` des Enfers :
  `["legion"]`. Cellules générées : + 47 (chaque hameau et ses voisines hors mer), soit 104 listées et
  180 avec les corridors (`connect_start_cities`, 117 avant la v1.0.61). `world_master.json` n'a pas de routes : `tools/world_gen` relie les
  villes lui-même (Kruskal + A*) ; les routes 000121 à 000127 ne sont que dans le lore.
- **Départs hors carte** (Lune Noire, Archimages) : `start_for` ne vit que sur les villes du master et
  ces lieux n'ont pas de cellule ; le départ est porté par `game_design.depart` (type `lieu`) dans le
  lore, sans entrée dans le master. En jeu, tant que leur carte n'existe pas, ces personnages suivent le
  repli par race. Ces décisions remplacent, pour ces factions, le « non accessible au joueur » de v1.0.56.
- **Code** : `start_locations.json` gagne `start_factions` et `factions` (prioritaires) ; `races` devient
  le repli (première ville de départ du master de cette race : humains → Haeldorf, orcs → Grakhul,
  nains → Durnhalde, elfes → Syllaërin, démons → Citadelle). Le master (création) et le shard
  (relocalisation) résolvent faction puis race (`StartLocationTable::ResolveFor`). La faune des quêtes
  d'accueil suit la race de la ville de départ.
- **Points incertains** : noms des hameaux ; `faction_legions_imperiales` est `playable` mais
  `selectable` false, absente de `factions.json` (seule `empire_hynn`, l'Empire de Lhynn, non
  sélectionnable) et rattachée à `region_enfers_t00` ; Archimages et Hors-La-Loi absents de
  `factions.json` ; les PNJ d'accueil (Iaelith, Rendal…) et les quêtes `<race>_accueil` restent dans les
  anciennes villes.

## 15. Régions de départ

**Décision du propriétaire (06/11/2026)** : chaque départ de faction a une **région de départ de 3×3
cases** (la case du hameau de départ et ses 8 voisines), de niveau **débutant** (1 à 10) : 2 à 3 fermes
ou hameaux, 4 à 6 POI, routes locales, créatures de bas niveau ; **une PR de carte par départ**. Note
`[DÉCISION] (v1.0.62)` sur `rule_niveaux_par_faction` et une entrée au registre
`regle_canon_registre_decisions`. Règles de remplissage : rien sur une case qui porte déjà une ville ou un
lieu (sauf POI à distance raisonnable, comme les Ruines de Wismerhill), terrain respecté, époque du jeu
(après la destruction de Kendrhir, avant le T11) ; tout ajout est `[GD]` (noms à valider), cellules
`[INFÉRÉ]` ; chaque ferme ou hameau est relié par une route (`road`) au hameau de départ ou à la route la
plus proche ; faune prise parmi les espèces existantes de niveau 1 à 10 (`creatures.zones_chasse` et
`cells.creature_ids`), sans nouvelle espèce. Le monde jouable reçoit les villes (`cities`, `kind`
`hameau`) et les POI (`pois`) dans `world_master.json` ; routes, sentiers et faune ne sont que dans le
lore (`tools/world_gen` trace ses routes et peuple les cases lui-même). La région est notée dans
`game_design.depart.region` de la faction (`cells`, `niveaux`).

### 15.1 Haeldorf — Légions Impériales (v1.0.62)

Centre N002_W010 (`hameau_eaglewatch`, v1.0.61). Les 9 cases étaient déjà générées. Lhynn (N001_W010,
capitale, mer à l'ouest) et le hameau de Marn (N003_W009) sont dans la région mais **rien n'y est posé**.
Script reproductible : `lore_v1062.py` (appliqué au fichier v1.0.61).

| Case | Terrain | Contenu | Faune (niveaux) |
|---|---|---|---|
| N003_W011 | plaine (Première Province) | POI **Camp de manœuvres des Légions** (`lieu_camp_manoeuvres_legions`, `poi_camp_manoeuvres_legions`, `camp`) : lices et cibles des recrues `[GD]` ; ancrage : légions et camps impériaux `[BD-THS p.8]` | lapin (1-3), renard (2-5), cheval (3-8) |
| N003_W010 | plaine (Première Province) | Ferme **Ombhral** (`hamlet_ambermead`, `farm_hamlet` / `hameau`) : blé et fourrage de la garnison `[GD]` ; route 000128 vers Haeldorf | poule, coq (1), bœuf (3-6), chien (2-6), lapin |
| N003_W009 | plaine | Hameau de Marn (existant, inchangé) | poule, lapin |
| N002_W011 | plaine | POI **Autel des Aigles** (`lieu_autel_des_aigles`, `poi_autel_des_aigles`, `sanctuaire`) : serment des recrues `[GD]` (description de Haeldorf, v1.0.61) | lapin, corbeau (1-4) |
| N002_W010 | plaine | **Haeldorf** (hameau de départ) ; routes 000121 (Lhynn), 000128, 000129 ; sentiers 000131 à 000134 vers les 4 POI | chien, poule ; molosse impérial (10-20, existant) |
| N002_W009 | plaine (pâturages) | POI **Ruines de la villa tharque** (`lieu_ruines_villa_tharque`, `poi_ruines_villa_tharque`, `ruine`) : domaine de l'ancien Empire tharque `[GD]`, ancré sur la Via Magna « héritée du grand Empire tharque » `[BD-THS p.8]` | araignée (3-8), corbeau, renard |
| N001_W011 | côte | POI **Grotte des contrebandiers** (`lieu_grotte_contrebandiers`, `poi_grotte_contrebandiers`, `salle`) : contrebande du port de guerre de Lhynn `[GD]` (`[BD-T7 p.5-6]`) | crabe, mouette (1), araignée |
| N001_W010 | plaine | Lhynn (capitale, inchangée) | inchangée |
| N001_W009 | plaine | Ferme **Gredhaim** (`hamlet_greyhedge`, `farm_hamlet` / `hameau`) : volailles, chèvres et ânes de bât `[GD]` ; route 000129 vers Haeldorf, route 000130 vers la Via Magna | chèvre (1-3), âne (2-4), chien, renard ; molosse impérial (existant) |

- **Routes** (`routes`) : 000128 Ombhral → Haeldorf, 000129 Haeldorf → Gredhaim et 000130
  Gredhaim → Via Magna (`road`, embranchement sur la route 000120 en N000_W010, première case après
  Lhynn, sans ville à l'arrivée) : de Haeldorf on rejoint la Via Magna sans traverser la capitale.
  Sentiers 000131 à 000134 (`path`, sans écurie) de Haeldorf vers les 4 POI.
- **Services** : 2 fermes (taille `hameau`) ; écuries d'après les routes `road` : Haeldorf 3 (au lieu
  de 1), Ombhral 1, Gredhaim 2 ; 1036 → 1050 PNJ de service.
- **Faune** : les bêtes de guerre des élevages impériaux (rhinocéros cuirassé, golem de roche,
  béhémoth, léviathan impérial, niveaux 18 à 55, v1.0.58) sont retirées de N002_W009 et N002_W010 et
  restent dans les enclos de N002_W008 (`poi_elevages_imperiaux`, hors région).
- **Points incertains** : noms ; le molosse impérial (10-20) reste sur les routes de Lhynn (N001_W009,
  N002_W010), au plafond de la tranche ; crabe et mouette ont un niveau par défaut (1-1) ; le camp de
  manœuvres et l'autel pourraient porter des PNJ ou des quêtes d'accueil des Légions (aucun créé).

> **v1.0.72** (section 16.1) : Ombhral est déplacée en N003_W008 et Gredhaim en N001_W008, hors de la
> région 3×3 ; routes 000128, 000129 et 000130 retracées.
>
> **v1.0.73** (section 16.2) : l'entrée nord de la Mine de la Traverse (raid à une équipe, niveaux 10 à
> 15) s'ouvre au sud de Gredhaim, au pied de la chaîne N000_W008.

> **v1.0.76** (section 16.5) : sentiers 000131 (Camp de manœuvres) et 000133 (Ruines de la villa tharque)
> supprimés, leurs lieux étant sur les routes 000199 et 000128 ; Gredhaim → Ombhral (000201) devient un sentier.

### 15.2 Orhendal — Chevaliers de la Lumière (v1.0.64)

Centre N005_W011 (`hameau_dawnmere`, v1.0.61). Les 9 cases étaient déjà générées. Altenberg (N005_W012,
citadelle de l'Ordre, intacte à l'époque du jeu) est dans la région mais **rien n'y est posé** ; Dys
(N005_W009) et Iustarn (N007_W011) sont hors région. Thème : l'Ordre de la Lumière et l'Église du Dieu
de Lumière (`lore.religions`, v1.0.58) — commanderie, chapelle, pèlerinage ; trois des POI sont ajoutés
aux `lieux_de_culte` de `religion_eglise_lumiere`. Script reproductible : `lore_v1064.py` (appliqué au
fichier v1.0.62).

| Case | Terrain | Contenu | Faune (niveaux) |
|---|---|---|---|
| N006_W012 | plaine (Première Province) | POI **Calvaire des pèlerins** (`lieu_calvaire_des_pelerins`, `poi_calvaire_des_pelerins`, `sanctuaire`) : première station du pèlerinage d'Altenberg à Syzygie pour la Grande Messe de la Lumière `[GD]` (`fete_eglise_grande_messe_lumiere`) ; croix cerclée, bannières rouges à croix blanche `[BD-T8 p.28-33]` `[BD-T2 p.34]` | lapin (1-3), corbeau (1-4), renard (2-5) |
| N006_W011 | plaine | Ferme **Ysselhaim** (`hamlet_whitesheaf`, `farm_hamlet` / `hameau`) : grain des novices et pain d'autel d'Altenberg `[GD]` ; route 000136 vers Orhendal | poule, coq (1), chien (2-6), lapin |
| N006_W010 | forêt avec rivière | POI **Chapelle de l'ermite** (`lieu_chapelle_de_l_ermite`, `poi_chapelle_de_l_ermite`, `sanctuaire`) : chapelle et source d'eau bénite `[GD]` ; prêtres de chapelle `[BD-A3 pl.19-20]`, croix de pierre en forêt `[BD-A3 pl.33]` | biche (2-6), araignée (3-8), renard, lapin |
| N005_W012 | plaine | Altenberg (grande ville, inchangée) | inchangée |
| N005_W011 | plaine | **Orhendal** (hameau de départ) ; routes 000122 (Altenberg), 000135 à 000137 ; sentiers 000138 à 000141 vers les 4 POI | chien, poule |
| N005_W010 | plaine | Commanderie rurale **Lhumbrecht** (`hamlet_brightholm`, `farm_hamlet` / `hameau`) : domaine agricole et haras de l'Ordre tenu par des frères convers (`[BD-T8 p.33-35]`) `[GD]` — distincte de la Grande et de la Haute Commanderie, salles de la citadelle `[BD-T8]` ; route 000135 vers Orhendal | cheval (3-8), bœuf (3-6), chien, poule |
| N004_W012 | plage | POI **Camp des naufrageurs** (`lieu_camp_des_naufrageurs`, `poi_camp_des_naufrageurs`, `camp`) : faux feux sur la longue plage au sud d'Altenberg `[GD]`, citadelle « sur un rocher battu par les vagues » `[BD-T2 p.32-33]` | crabe, mouette (1), corbeau |
| N004_W011 | plaine | Ferme **Lammerhin** (`hamlet_lambsfold`, `farm_hamlet` / `hameau`) : bergerie, laine blanche des habits et capuches de l'Ordre (`[BD-T8 p.31-35]`) `[GD]` ; route 000137 vers Orhendal | mouton (1), bœuf, âne (2-4), chien, renard |
| N004_W010 | plaine | POI **Ruines du prieuré** (`lieu_ruines_du_prieure`, `poi_ruines_du_prieure`, `ruine`) : prieuré abandonné depuis des générations, crypte à vermine `[GD]` (aucune destruction de l'époque du jeu) | araignée, corbeau, renard |

- **Routes** (`routes`) : 000135 Orhendal → Lhumbrecht, 000136 Orhendal → Ysselhaim, 000137 Orhendal →
  Lammerhin (`road`, cases voisines) ; sentiers 000138 à 000141 (`path`, sans écurie) d'Orhendal vers les
  4 POI.
- **Services** : 3 hameaux (taille `hameau`) ; écuries d'après les routes `road` : Orhendal 4 (au lieu
  de 1), Lhumbrecht, Ysselhaim et Lammerhin 1 ; 1050 → 1067 PNJ de service.
- **Faune** : les 8 cases hors Altenberg n'avaient aucune créature ; espèces existantes de niveau 1 à 10
  seulement.
- **Points incertains** : noms ; Fratus Sinister et ses « Fils de la Lumière » (`[BD-T4]`, `[BD-T5]`)
  ne sont pas évoqués : la région reste neutre sur la direction de l'Ordre ; crabe, mouette et mouton ont
  un niveau par défaut (1-1) ; Orhendal porte 4 écuries pour un hameau (règle des routes) ; le calvaire et
  la chapelle pourraient porter des PNJ ou quêtes d'accueil (aucun créé).

> **v1.0.76** (section 16.5) : sentier 000139 (Chapelle de l'ermite, sur la route Dys → Iustarn) supprimé ;
> 000140 remplacé par Lhumbrecht → Ruines du prieuré (000225) ; 000138 (Calvaire des pèlerins) part d'Altenberg et
> 000141 (Camp des naufrageurs) de Lammerhin : ils croisaient une route avant Orhendal.

### 15.3 Ordhrann — Chevaliers de la Justice (v1.0.66)

Centre N009_W010 (`hameau_oathford`, v1.0.61). Les 9 cases étaient déjà générées. Syzygie (N009_W011,
`ville_sysigie`, cité de l'île du lac, intacte à l'époque du jeu) est dans la région mais **rien n'y est
posé** ; le fleuve de Sysigie passe par le centre de N010_W011 et de N008_W011 (section 8.2, le
générateur pose les POI au centre de leur case) : **aucun lieu** dans ces deux cases, seulement de la
faune. La Passe de Syzygie (N008_W013) et Iustarn (N007_W011) sont hors région. Thème : l'Ordre de la
Justice — serments des novices, garnisons, parjures ; la stèle est ajoutée aux `lieux_de_culte` de
`religion_eglise_lumiere`. Routes selon la règle du propriétaire v1.0.65 : chaque nouvelle ferme ou
hameau est relié au voisin le plus proche (ici Ordhrann, case voisine, sans étape) ; routes existantes
inchangées. Script reproductible : `lore_v1066.py` (appliqué au fichier v1.0.65).

| Case | Terrain | Contenu | Faune (niveaux) |
|---|---|---|---|
| N010_W011 | plaine avec fleuve (Syzygie) | aucun lieu (lit du fleuve) | lapin (1-3), corbeau (1-4), loutre (1), renard (2-5) |
| N010_W010 | plaine (Syzygie) | Ferme **Thydmar** (`hamlet_tithemead`, `farm_hamlet` / `hameau`) : ferme de la dîme, blé, foin et chevaux de remonte des garnisons de Syzygie `[GD]` ; route 000166 vers Ordhrann. POI **Lices des novices** (`lieu_lices_des_novices`, `poi_lices_des_novices`, `camp`) à distance de la ferme : exercices des novices `[GD]` | poule, coq (1), chien (2-6), bœuf (3-6), cheval (3-8), lapin |
| N010_W009 | lisière (Syzygie) | POI **Camp des parjures** (`lieu_camp_des_parjures`, `poi_camp_des_parjures`, `camp`) : écuyers chassés de l'Ordre pour parjure et brigands `[GD]` | renard, araignée (3-8), corbeau, biche (2-6) |
| N009_W011 | plaine avec fleuve | Syzygie (grande ville, inchangée) | inchangée |
| N009_W010 | plaine | **Ordhrann** (hameau de départ) ; routes 000123 (Syzygie), 000166, 000167 ; sentiers 000168 à 000171 vers les 4 POI | chien, poule |
| N009_W009 | lisière (Première Province) | Hameau de garnison **Vharholdt** (`hamlet_wardholt`, `farm_hamlet` / `hameau`) : palissade, poste de garde de l'Ordre face à la forêt elfique, bûcherons et charbonniers `[GD]` ; route 000167 vers Ordhrann | chien, âne (2-4), biche, lapin, renard |
| N008_W011 | plaine avec fleuve (Syzygie) | aucun lieu (lit du fleuve ; route 000021 Syzygie → Iustarn inchangée) | lapin, corbeau, loutre, renard |
| N008_W010 | plaine (Première Province) | POI **Stèle des Serments** (`lieu_stele_des_serments`, `poi_stele_des_serments`, `sanctuaire`) : stèle de granit gravée de la devise de l'Ordre, serment des novices devant un frère aîné `[GD]` ; devise et frères aînés `[BD-T4]` | mouton (1), lapin, corbeau, renard |
| N008_W009 | lisière (Première Province) | POI **Ruines de la tour du guet** (`lieu_ruines_tour_du_guet`, `poi_ruines_tour_du_guet`, `ruine`) : tour abandonnée depuis des générations face à la forêt elfique, cave à vermine `[GD]` (aucune destruction de l'époque du jeu) | araignée, corbeau, renard |

- **Routes** (`routes`) : 000166 Ordhrann → Thydmar, 000167 Ordhrann → Vharholdt (`road`, cases
  voisines, sans `waypoints`) ; sentiers 000168 à 000171 (`path`, sans écurie) d'Ordhrann vers les 4 POI.
- **Services** : 2 hameaux (taille `hameau`) ; écuries d'après les routes `road` : Ordhrann 3 (au lieu
  de 1), Thydmar et Vharholdt 1 ; 1067 → 1077 PNJ de service.
- **Corridor des départs** (`connect_start_cities`) : avec Vharholdt, le corridor qui relie les départs
  passe par N008_W008 (forêt elfique, désormais générée) au lieu de N006_W009 et N006_W008, ce qui aurait
  retiré Dys (N005_W009) du monde jouable ; ces trois cellules sont ajoutées explicitement à
  `generated_cells` (dernière ligne) : 180 → 181 cellules générées, aucune perdue.
- **Faune** : les 8 cases hors Syzygie n'avaient aucune créature ; espèces existantes de niveau 1 à 10
  seulement.
- **Points incertains** : noms ; « Stèle » plutôt que « Pierre » des Serments (le mot « Pierre » est un
  nom du monde et ferait refuser des PNJ nains par `validate_city_services.py`) ; les Lices partagent la
  case de Thydmar (5 cases libres pour 6 lieux) ; Parsifal et l'évacuation de l'Ordre vers Syzygie
  (`[BD-T10]`) ne sont pas évoqués ; loutre et mouton ont un niveau par défaut (1-1) ; la stèle et les
  lices pourraient porter des PNJ ou quêtes d'accueil (aucun créé).

> **v1.0.72** (section 16.1) : Vharholdt est au fond d'une crevasse en cul-de-sac, avec une mine ; la
> route Thydmar → Ostrevan (000198) remplace la route 000066 Syzygie → Ostrevan.

> **v1.0.76** (section 16.5) : sentiers 000169 (Lices des novices) et 000170 (Camp des parjures) supprimés,
> leurs lieux étant sur les routes 000166 et 000198.

### 15.4 Grakhul — Dzorak (v1.0.67)

Centre N008_E012 (`hameau_grakhul`, v1.0.61). Les 9 cases étaient déjà générées. Ghulvathan est en
N009_E011 dans le monde jouable (`world_master.json`) mais en N012_E012 dans le lore (section 17) :
**rien n'est posé** dans N009_E011, pas même de la faune. Mhozrak (N006_E011) et Horkher (N006_E014)
sont hors région. Thème : les clans Dzorak et les croyances des esprits des orques (`lore.religions`,
`religion_esprits_orques`, v1.0.58) — kraals, totem chamanique, arène, camp de guerre, chasse ; le totem
est ajouté aux `lieux_de_culte` de `religion_esprits_orques`. Routes selon la règle du propriétaire
v1.0.65 : chaque nouveau kraal est relié au voisin le plus proche (ici Grakhul, case voisine, sans étape) ;
routes existantes inchangées (000125 vers Ghulvathan, 000142 Mhozrak → Grakhul). Les PNJ de
service des kraals sont Dzorak (`RACE_FACTIONS` de `generate_city_services.py` : `dzorak_orcs`). Script
reproductible : `lore_v1067.py` (appliqué au fichier v1.0.66).

| Case | Terrain | Contenu | Faune (niveaux) |
|---|---|---|---|
| N009_E011 | forêt | Ghulvathan du monde jouable (inchangée, rien n'est posé) | inchangée |
| N009_E012 | forêt | POI **Arène des Jeunes-Sangs** (`lieu_arene_des_jeunes_sangs`, `poi_arene_des_jeunes_sangs`, `camp`) : fosse de combat entre Grakhul et Ghulvathan, épreuve de l'arène des jeunes guerriers `[GD]` ; épreuve et festin `[BD-A4 p.30-32]`, trône de défenses `[BD-A5 p.12]` | araignée (3-8), corbeau (1-4), renard (2-5), biche (2-6) |
| N009_E013 | forêt | POI **Camp de guerre du clan Ghazrak** (`lieu_camp_de_guerre_ghazrak`, `poi_camp_de_guerre_ghazrak`, `camp`) : le clan rassemble ses guerriers avant de rejoindre les armées de la Lune Noire `[GD]` ; trophées de têtes `[BD-A4 p.29-30]` | chien (2-6), araignée, corbeau, renard |
| N008_E011 | forêt | POI **Totem d'Ogunta** (`lieu_totem_d_ogunta`, `poi_totem_d_ogunta`, `sanctuaire`) : cercle de pierres et totem de crânes, osselets, runes de puissance, mémoire de la chamane Ogunta `[GD]` ; `[BD-A4 p.8-15]` | corbeau, araignée, biche, lapin (1-3) |
| N008_E012 | lisière | **Grakhul** (hameau de départ) ; routes 000125 (Ghulvathan), 000142 (Mhozrak), 000172, 000173 ; sentiers 000174 à 000178 vers les 5 POI | chien, chèvre (1-3) |
| N008_E013 | lisière | Kraal **Gharzuk** (`hamlet_gharzuk`, `farm_hamlet` / `hameau`) : chasseurs et charbonniers, gibier fumé, peaux, bois des palissades `[GD]` ; route 000173 vers Grakhul | chien, biche, lapin, renard |
| N007_E011 | lisière | POI **Affût de la Grande Chasse** (`lieu_affut_de_la_grande_chasse`, `poi_affut_de_la_grande_chasse`, `camp`) : camp de chasseurs, départ de la Grande Chasse (`fete_orques_grande_chasse`) `[GD]` ; route 000142 inchangée | biche, lapin, renard, corbeau |
| N007_E012 | plaine | Kraal **Thokrul** (`hamlet_thokrul`, `farm_hamlet` / `hameau`) : éleveurs, chèvres, bœufs et ânes de bât, peaux tannées `[GD]` ; route 000172 vers Grakhul | chèvre, bœuf (3-6), âne (2-4), chien, lapin |
| N007_E013 | plaine (Marche de l'Est) | POI **Ruines du fortin des Marches** (`lieu_ruines_fortin_des_marches`, `poi_ruines_fortin_des_marches`, `ruine`) : fortin impérial abandonné depuis la conquête de la Marche de l'Est par la Lune Noire, avant le T1 `[BD-A5 p.38-44]` ; le fortin est `[GD]` | araignée, corbeau, renard, lapin |

- **Routes** (`routes`) : 000172 Grakhul → Thokrul, 000173 Grakhul → Gharzuk (`road`, cases voisines,
  sans `waypoints`) ; sentiers 000174 à 000178 (`path`, sans écurie) de Grakhul vers les 5 POI.
- **Services** : 2 kraals (taille `hameau`) ; écuries d'après les routes `road` : Grakhul 4 (au lieu
  de 2), Thokrul et Gharzuk 1 ; 1077 → 1090 PNJ de service, tous Dzorak dans les kraals.
- **Faune** : les 8 cases hors Ghulvathan n'avaient aucune créature ; espèces existantes de niveau 1 à 10
  seulement. Sanglier (8-15), loup (5-12) et sanglier de guerre (15-25, N007_E014, hors région) dépassent
  la tranche : la Grande Chasse et le festin de sanglier restent un décor.
- **Points incertains** : noms ; le clan Ghazrak est inventé (aucun personnage de la BD) ; le totem ne dit
  rien de l'esprit d'Ogunta lié à l'anneau de Greldinard (Q22) ; Greldinard n'est pas évoqué ; l'arène
  touche la case de Ghulvathan, dont la position reste ouverte (section 17) ; la chèvre (1-3) et le chien
  sont les seuls animaux de Grakhul ; le totem, l'arène et l'affût pourraient porter des PNJ ou quêtes
  d'accueil (aucun créé).

> **v1.0.76** (section 16.5) : sentiers 000175 (Arène des Jeunes-Sangs) et 000177 (Affût de la Grande Chasse)
> supprimés, leurs lieux étant sur les routes 000125 et 000142.

### 15.5 Drakhenhal — Chevaliers-Dragons (v1.0.68)

Centre S004_W004 (`hameau_wyrmhallow`, v1.0.61). Les 9 cases étaient déjà générées. Le Palais des Maîtres
des Dragons et le Sanctuaire des Chevaliers Dragons (S003_W004, `poi_palais_passe` et
`poi_sanctuaire_chevaliers_dragons` dans le monde jouable) et Ekvhitorn (S005_W004, port du Royaume Drach)
sont dans la région mais **rien n'y est posé**, faune comprise. Thème : l'élevage et le dressage des
Chevaliers-Dragons — bergers qui nourrissent les dragons du Palais, haras des montures des écuyers, aire
de dressage, nids abandonnés (les œufs sont gardés dans les sous-sols du Palais, aucun commerce de dragons
ni d'œufs, v1.0.58), cairn du pacte ; le cairn est ajouté aux `lieux_de_culte` de
`religion_croyances_mineures` (culte du pacte). Routes selon la règle du propriétaire v1.0.65 : chaque
nouveau hameau est relié au voisin le plus proche (ici Drakhenhal, case voisine orthogonale, sans étape) ;
routes existantes inchangées (000124 vers le Palais, 000082 Palais → Ekvhitorn, 000017). Population
humaine : `RACE_FACTIONS` de `generate_city_services.py` inchangé. Script reproductible : `lore_v1068.py`
(appliqué au fichier v1.0.67).

| Case | Terrain | Contenu | Faune (niveaux) |
|---|---|---|---|
| S003_W003 | neiges éternelles | POI **Nids abandonnés des Crêtes** (`lieu_nids_abandonnes_des_cretes`, `poi_nids_abandonnes_des_cretes`, `ruine`) : aires de ponte désertées depuis des générations, coquilles brisées et ossements `[GD]` ; œufs dans les sous-sols du Palais (`city_dragon_palace`), vieux dragons et progéniture `[BD-T2]` ; aucun dragon n'y vit | vautour (1), corbeau (1-4), aigle (1), marmotte (1) |
| S003_W004 | neiges éternelles | Palais des Maîtres des Dragons et Sanctuaire des Chevaliers Dragons (inchangés, rien n'est posé) | inchangée |
| S003_W005 | neiges éternelles | POI **Cairn des Ailes tombées** (`lieu_cairn_des_ailes_tombees`, `poi_cairn_des_ailes_tombees`, `sanctuaire`) : chevaliers et dragons tombés en vol, serment des écuyers au pacte `[GD]` ; pacte `[BD-T2]`, rompu au T14 seulement `[BD-T14 p.21-23]` | aigle, corbeau, marmotte, chèvre (1-3) ; Grand Ancien (66-70) retiré |
| S004_W003 | montagne | Hameau **Krakhol** (`hamlet_cragfold`, `farm_hamlet` / `hameau`) : bergers, chèvres, moutons et bœufs dont une part monte au Palais pour nourrir les dragons `[GD]` ; route 000179 vers Drakhenhal | chèvre, mouton (1), bœuf (3-6), chien (2-6), marmotte |
| S004_W004 | montagne | **Drakhenhal** (hameau de départ) ; routes 000124 (Palais), 000179, 000180 ; sentiers 000181 à 000184 vers les 4 POI | chien, chèvre, mulet (1) |
| S004_W005 | montagne | Hameau **Bhridrak** (`hamlet_bridlecrag`, `farm_hamlet` / `hameau`) : élevage de montures (chevaux de montagne, mulets, poneys) des écuyers, selliers `[GD]` ; écuyers qui apprennent à monter (v1.0.61), « J'ai pas le droit de monter un dragon » `[BD-T2]` ; route 000180 vers Drakhenhal | cheval (3-8), mulet, poney (1), âne (2-4), chien |
| S005_W003 | montagne (Royaume Drach) | POI **Ruines du pont des Gorges** (`lieu_ruines_pont_des_gorges`, `poi_ruines_pont_des_gorges`, `ruine`) : pont suspendu et corps de garde abandonnés depuis des générations `[GD]` ; gorges et ponts suspendus du royaume des Chevaliers-Dragons `[BD]` (lieu distinct du pont de la Passe, `[BD-T2]`) | araignée (3-8), vautour, corbeau, marmotte |
| S005_W004 | plaine (Royaume Drach) | Ekvhitorn (ville portuaire, inchangée) | inchangée |
| S005_W005 | montagne (Royaume Drach) | POI **Aire de dressage des écuyers** (`lieu_aire_de_dressage_des_ecuyers`, `poi_aire_de_dressage_des_ecuyers`, `camp`) : selle de dragon sur poutre pivotante, lance, ordres en dracchen `[GD]` ; combat aérien `[BD]`, langue dracchen `[BD-T2]` | cheval, corbeau, aigle, chèvre ; dragon des Princes Dragons (40-48) retiré |

- **Routes** (`routes`) : 000179 Drakhenhal → Krakhol, 000180 Drakhenhal → Bhridrak (`road`, cases
  voisines, sans `waypoints`) ; sentiers 000181 à 000184 (`path`, sans écurie) de Drakhenhal vers les 4 POI.
- **Services** : 2 hameaux (taille `hameau`) ; écuries d'après les routes `road` : Drakhenhal 3 (au lieu
  de 1), Krakhol et Bhridrak 1 ; 1090 → 1102 PNJ de service, tous humains.
- **Faune** : bestiaire v1.0.58, pas de dragon adulte en zone débutant : le Grand Ancien
  (`creature_dragon_grand_ancien`) est retiré de S003_W005 (il reste en S002_W005 et S004_W006, hors
  région) et le dragon des Princes Dragons (`creature_dragon_fratus_t8`) de S005_W005 (il reste en
  S005_W006 et au Palais). Espèces existantes de niveau 1 à 10 seulement ; aucun dragonnet du bestiaire
  n'est dans la tranche (dragonnet-léopard 14-22), aucun n'est créé.
- **Points incertains** : noms ; les dragons du Palais (40-48) restent dans la case du Palais, au cœur de
  la région (lieu de la BD, non modifié) ; la note v1.0.61 de Drakhenhal dit les cases est et ouest en
  neiges éternelles alors que S004_W003 et S004_W005 sont en montagne (les hameaux suivent le terrain des
  cellules) ; le tribut de bétail pour les dragons et l'abandon des nids sont inventés ; aigle, vautour,
  marmotte, mulet, poney et mouton ont un niveau par défaut (1-1) ; l'aire de dressage et le cairn
  pourraient porter des PNJ ou quêtes d'accueil (aucun créé).

> **v1.0.76** (section 16.5) : Bhridrak passe en S004_W006 et Krakhol en S004_W002, hors de la région 3×3
> (`game_design.depart.region` inchangée), dont ils restent le haras et la bergerie ; routes 000180 et 000179
> allongées par leur ancienne case. Leurs animaux domestiques les suivent ; S004_W005 garde marmottes et corbeaux,
> S004_W003 sa marmotte. Le Palais (S003_W004) et Ekvhitorn (S005_W004) ne changent pas.

### 15.6 Durnhalde — Nains (v1.0.69)

Centre S007_E017 (`hameau_durnhalde`, v1.0.61). Les 9 cases étaient déjà générées et toutes en montagne
(aucune case volcanique, aucune rivière). Ghrunke Dash (S007_E016, cité naine, `ville_ghrunke_dash`) est
dans la région mais **rien n'y est posé**, faune comprise ; le cratère du volcan et le Grand Hall des nains
(S008_E014) ainsi que les cases volcaniques de l'ouest (S007_E015, S008_E015) sont hors région. Thème : les
mines et les forges qui font la réputation de Ghrunke Dash (« célèbre dans l'Empire pour la qualité de ses
métaux » `[BD-T3 pl.16]`, charbon `[BD-T9 p.12]`) — hameau minier, houillère, galeries effondrées, puits des
apprentis, convois de minerai, prospecteurs, autel des ancêtres ; l'autel est ajouté aux `lieux_de_culte` de
`religion_nains` (« Le Métal, le Feu et la Lave »). Routes selon la règle du propriétaire v1.0.65 : chaque
nouveau hameau est relié au voisin le plus proche, sans étape : Kohlbrand à Durnhalde (case voisine
orthogonale ; Ghrunke Dash n'est que diagonal), Eisenkehl à Kohlbrand (case voisine orthogonale ; Durnhalde
n'est que diagonal) ; routes existantes inchangées (000126 Durnhalde → Ghrunke Dash, 000148
Ghrunke Dash → hamlet_0196). Les PNJ de service des hameaux sont nains (`RACE_FACTIONS` de
`generate_city_services.py` : `faction_dwarves`). Script reproductible : `lore_v1069.py` (appliqué au
fichier v1.0.68).

| Case | Terrain | Contenu | Faune (niveaux) |
|---|---|---|---|
| S006_E016 | montagne | POI **Autel des Ancêtres** (`lieu_autel_des_ancetres`, `poi_autel_des_ancetres`, `sanctuaire`) : niche aux visages des ancêtres des clans, premier outil forgé déposé en offrande, marteaux frappés au sol `[GD]` ; rythme des marteaux, « nés du métal, du feu et de la lave » `[BD-T14 p.39]` | corbeau (1-4), chèvre (1-3), marmotte (1), aigle (1) |
| S006_E017 | montagne (nains de l'Est) | POI **Puits des Apprentis** (`lieu_puits_des_apprentis`, `poi_puits_des_apprentis`, `salle`) : mine de cuivre des apprentis de Durnhalde, niveaux profonds à vermine `[GD]` ; mines d'apprentissage de Durnhalde (v1.0.61) | rat (1), araignée (3-8), chèvre |
| S006_E018 | montagne (nains de l'Est) | POI **Galeries effondrées de la Vieille Veine** (`lieu_galeries_effondrees_vieille_veine`, `poi_galeries_effondrees_vieille_veine`, `ruine`) : veine de fer épuisée, galeries effondrées depuis des générations `[GD]` (aucune destruction de l'époque du jeu) ; tunnels nains `[BD-T5]` | rat, araignée, corbeau, marmotte |
| S007_E016 | montagne | Ghrunke Dash (grande ville, inchangée, rien n'est posé) | inchangée |
| S007_E017 | montagne (nains de l'Est) | **Durnhalde** (hameau de départ) ; routes 000126 (Ghrunke Dash), 000185 (Kohlbrand) ; sentiers 000187 à 000191 vers les 5 POI | chien, mulet, rat |
| S007_E018 | montagne (nains de l'Est) | POI **Camp des prospecteurs** (`lieu_camp_des_prospecteurs`, `poi_camp_des_prospecteurs`, `camp`) : tentes, tranchées d'essai, recherche de nouvelles veines `[GD]` ; paiement en gemmes `[BD-T3]` | vautour (1), aigle, marmotte, corbeau |
| S008_E016 | montagne | POI **Relais des convois de minerai** (`lieu_relais_des_convois`, `poi_relais_des_convois`, `camp`) : pesée et regroupement des convois de minerai et de charbon sous escorte avant Ghrunke Dash `[GD]` ; chars blindés et mercenaires nains `[BD-T3 pl.17-18]` | mulet, âne (2-4), poney (1), corbeau |
| S008_E017 | montagne (nains de l'Est) | Hameau de houilleurs **Kohlbrand** (`hamlet_kohlbrand`, `farm_hamlet` / `hameau`) : puits de charbon, meules de coke, bas fourneaux `[GD]` ; charbon `[BD-T9 p.12]` ; routes 000185 vers Durnhalde et 000186 vers Eisenkehl | mulet, âne, chien, chèvre, rat |
| S008_E018 | montagne (nains de l'Est) | Hameau minier **Eisenkehl** (`hamlet_eisenkehl`, `farm_hamlet` / `hameau`) : mines de fer étayées, terrils, treuils ; minerai à dos de mulet vers les forges de Ghrunke Dash `[GD]` ; route 000186 vers Kohlbrand (voisin orthogonal ; Durnhalde n'est que diagonal) | mulet (1), chien (2-6), chèvre, rat |

- **Routes** (`routes`) : 000185 Durnhalde → Kohlbrand, 000186 Kohlbrand → Eisenkehl (`road`, cases
  voisines, sans `waypoints`) ; sentiers 000187 à 000191 (`path`, sans écurie) de Durnhalde vers les 5 POI.
- **Services** : 2 hameaux (taille `hameau`) ; écuries d'après les routes `road` : Durnhalde 2 (au lieu
  de 1), Kohlbrand 2, Eisenkehl 1 ; 1102 → 1114 PNJ de service, tous nains dans les nouveaux hameaux.
- **Placement d'Eisenkehl** (contrôlé par `tools/world_gen`) : au nord (S006_E017), le corridor des
  départs (`connect_start_cities`) passait le long de S005_E017 (Baronnie de Moorks, géant brun 32-44,
  dragon noir 45-55), qui devenait jouable au contact de la région ; à l'est (S007_E018), le hameau rendait
  inaccessible un lieu procédural de la case (avertissement « chemin impossible vers …_mine_0 »). Eisenkehl
  est donc au sud-est (S008_E018) : corridor inchangé, 181 → 181 cellules générées, aucune ajoutée ni
  perdue, aucun nouvel avertissement du dry-run.
- **Faune** : les 8 cases hors Ghrunke Dash n'avaient aucune créature, aucune n'est retirée ; espèces
  existantes de niveau 1 à 10 seulement. La créature de lave (S008_E014), le géant de feu (S009_E014), les
  géants bruns et dragons noirs (S005_E015 à S005_E017) restent hors région.
- **Points incertains** : noms (consonance germanique alignée sur Durnhalde) ; le culte des ancêtres est
  inventé (la BD ne donne que le rythme des marteaux) ; l'« eau à l'est » de Ghrunke Dash `[BD-T3 pl.16]`
  n'a pas de rivière dans les cellules ; le Relais et l'Autel touchent la case de Ghrunke Dash, desservis par
  des sentiers depuis Durnhalde ; mulet, poney, rat, marmotte, aigle et vautour ont un niveau par défaut
  (1-1) ; l'autel et le puits pourraient porter des PNJ ou quêtes d'accueil (aucun créé).

### 15.7 Syllaërin — Hauts Elfes (v1.0.70)

Centre N007_W006 (`hameau_silverglade`, v1.0.61). Les 9 cases étaient déjà générées et toutes en forêt.
Northind (N007_W005, grande ville, `ville_northind`), le village Orkh (N008_W006, monde jouable,
`ville_orkh`) et la Porte 5 des enfers (N008_W007, `lieu_portail_enfer_5`, château abandonné « à l'écart
des routes ») sont dans la région mais **rien n'y est posé**, faune comprise ; N006_W005 a une rivière au
centre de la case (code `R` de la grille, le générateur pose les POI au centre) : **aucun lieu**, seulement
de la faune. Les Ruines du château de Thenhart (N009_W004) et les marais maudits plus au nord sont hors
région. Thème : les croyances des peuples des bois (« Mère Nature et les Vents », `religion_peuples_des_bois`,
v1.0.58) et la garde de la forêt elfique — bosquet sacré, autel des Vents, tour de guet, ruines anciennes ;
l'autel et le bosquet sont ajoutés aux `lieux_de_culte` de `religion_peuples_des_bois`. Routes selon la
règle du propriétaire v1.0.65 : chaque nouveau hameau est relié au voisin le plus proche, sans étape :
Ghwenaëth et Faernilh à Syllaërin (case voisine orthogonale ; Northind n'est que diagonal de
Ghwenaëth, le village Orkh que diagonal de Faernilh) ; routes existantes inchangées (000127
Syllaërin → Northind, 000077 et 000085 de Northind). Les PNJ de service des hameaux sont hauts elfes
(`RACE_FACTIONS` de `generate_city_services.py` : `faction_hauts_elfes`). Script reproductible :
`lore_v1070.py` (appliqué au fichier v1.0.69).

| Case | Terrain | Contenu | Faune (niveaux) |
|---|---|---|---|
| N006_W007 | forêt (forêt elfique) | POI **Bosquet des Pavanes** (`lieu_bosquet_des_pavanes`, `poi_bosquet_des_pavanes`, `sanctuaire`) : cercle d'arbres centenaires où les jeunes elfes apprennent les pavanes sacrées `[GD]` ; pavanes en l'honneur de Mère Nature `[BD-T14 p.19]` | biche (2-6), lutin (5-10), hibou (1), écureuil (1) |
| N006_W006 | forêt (Province de Northind) | Hameau de cueilleurs **Ghwenaëth** (`hamlet_greenbough`, `farm_hamlet` / `hameau`) : herboristes, séchoirs à herbes et à champignons `[GD]` ; route 000192 vers Syllaërin. POI **Autel des Vents** (`lieu_autel_des_vents`, `poi_autel_des_vents`, `sanctuaire`) à l'écart du hameau : premier message confié aux Vents `[GD]` ; Vents messagers `[BD-T12 p.6, 10]` | lapin (1-3), écureuil, biche, renard (2-5) |
| N006_W005 | forêt avec rivière (Province de Northind) | aucun lieu (rivière au centre de la case) | biche, loutre (1), renard, lapin |
| N007_W007 | forêt (forêt elfique) | Hameau de facteurs d'arcs **Faernilh** (`hamlet_fernhollow`, `farm_hamlet` / `hameau`) : arcs, bois d'if, replantation `[GD]` (`[BD-A4 p.6]`) ; route 000193 vers Syllaërin. POI **Tour des Flèches blanches** (`lieu_tour_des_fleches_blanches`, `poi_tour_des_fleches_blanches`, `camp`) à l'écart du hameau : tour de guet de bois vivant `[GD]` ; flèche à empennage blanc `[HS]` | biche, renard, corbeau (1-4), lutin |
| N007_W006 | forêt (Province de Northind) | **Syllaërin** (hameau de départ) ; routes 000127 (Northind), 000192, 000193 ; sentiers 000194 à 000197 vers les 4 POI | lapin, écureuil, renard |
| N007_W005 | forêt (Province de Northind) | Northind (grande ville, inchangée, rien n'est posé) | inchangée |
| N008_W007 | forêt (forêt elfique) | Porte 5 des enfers (lieu BD, inchangé, rien n'est posé) | inchangée (démon à tête de sanglier 30-40) |
| N008_W006 | forêt (Province de Northind) | village Orkh (monde jouable, inchangé, rien n'est posé) | inchangée (aucune) |
| N008_W005 | forêt (Province de Northind) | POI **Ruines du Temple sylvestre** (`lieu_ruines_du_temple_sylvestre`, `poi_ruines_du_temple_sylvestre`, `ruine`) : temple elfe abandonné depuis des générations, quand la forêt reculait devant les colons `[GD]` (`[BD-A4 p.4-6]` ; aucune destruction de l'époque du jeu, sans rapport avec l'incendie de Northind `[BD-T8]`) | araignée (3-8), corbeau, chouette (1), renard |

- **Routes** (`routes`) : 000192 Syllaërin → Ghwenaëth, 000193 Syllaërin → Faernilh (`road`, cases
  voisines, sans `waypoints`) ; sentiers 000194 à 000197 (`path`, sans écurie) de Syllaërin vers les 4 POI.
- **Services** : 2 hameaux (taille `hameau`) ; écuries d'après les routes `road` : Syllaërin 3 (au lieu
  de 1), Ghwenaëth et Faernilh 1 ; 1114 → 1127 PNJ de service, tous hauts elfes dans les nouveaux hameaux.
- **Corridor des départs** (`connect_start_cities`, contrôlé par `tools/world_gen`) : avec Ghwenaëth, le
  corridor ne passe plus par N006_W004 (forêt de la Province de Northind), qui est ajoutée explicitement à
  `generated_cells` (dernière ligne) ; avec Faernilh, il passe par N007_W008 (forêt elfique, sans
  créature ni lieu), désormais générée. L'ordre des villes compte (Kruskal) : les deux hameaux sont en fin
  de liste de `cities` (insérés après Syllaërin, ils ajoutaient aussi N004_W005). 181 → 182 cellules
  générées, aucune perdue, aucun nouvel avertissement du dry-run. Aucune case libre de la région ne
  laissait le corridor inchangé avec deux hameaux.
- **Faune** : les 5 cases libres et le centre n'avaient aucune créature, aucune n'est retirée ; espèces
  existantes de niveau 1 à 10 seulement. La fée (12-20) et les lutins de N009_W006 à N009_W008 restent hors
  région.
- **Points incertains** : noms (consonance anglaise alignée sur Syllaërin) ; le **démon à tête de
  sanglier (30-40)** reste dans la case de la Porte 5 des enfers (lieu BD non modifié, comme le Palais de
  Drakhenhal) : à retirer de N008_W007 si le propriétaire veut la région entièrement débutante (il resterait
  à la porte, `zones_chasse` `lieu_portail_enfer_5`) ; la Tour est de genre `camp` (aucun genre « tour »
  côté client) ; l'Autel et la Tour partagent la case d'un hameau (4 cases libres pour 6 lieux) ; le
  village-ambassade elfe `[HS]` n'est pas placé ; écureuil, loutre, hibou et chouette ont un niveau par
  défaut (1-1) ; le bosquet et l'autel pourraient porter des PNJ ou quêtes d'accueil (aucun créé).

> **v1.0.72** (section 16.1) : Syllaërin est déplacée seule en N007_W003 ; la région 3×3 reste centrée
> sur N007_W006. Les sentiers 000194 à 000197 partent de Ghwenaëth ou de Faernilh.

> **v1.0.76** (section 16.5) : sentier 000197 (Ruines du Temple sylvestre) supprimé, le lieu étant sur la route
> Syllaërin → Faernilh (000193).

### 15.8 Citadelle de la Légion — Légion infernale, carte des Enfers (v1.0.71)

Centre N000_E000 de la **carte des Enfers** (`citadelle_legion`, monde jouable seulement : pas de fiche
`villes`, v1.0.58). **Décisions du propriétaire (09/11/2026)**, notes `[DÉCISION] (v1.0.71)` sur
`faction_legion_infernale`, `regle_bestiaire_echelles` et `rule_niveaux_par_faction`, 3 entrées au registre :

1. **Coins ouverts** : la région 3×3 = les 9 cellules N001..S001 × W001..E001, toutes générées. Les coins
   gardent le code `M` (chaîne de montagnes) : `tools/world_gen` génère les cellules `M` comme sur le
   continent (S008_E009 à S008_E011) ; pentes médianes ≈ 20° (≈ 15° sur la croix), 91 % du sol sous 35°,
   donc praticable. La couronne N002/S002, W002/E002 reste `M`, non générée ([`ENFERS.md`](ENFERS.md) §1).
2. **Niveaux** : les Enfers sont une zone de départ de niveau 1 à 10 ; seuls les boss et les abords des
   portails peuvent être plus élevés. `regle_bestiaire_echelles` : « Enfers, volcan, boss mondiaux 55-70 »
   devient « volcan, boss mondiaux 55-70 ; Enfers (départ de la Légion infernale) 1-10, sauf boss et
   abords des portails » (autres tranches inchangées).
3. **Pas de hameau** : la Citadelle reste la seule ville ; services inchangés (1127 PNJ, la Citadelle n'a
   pas de fiche `villes`).

Le lore n'a pas de fiche `cells` pour les Enfers (N000_E000… sont des cases du continent) : les lieux
portent `cell` vide, `carte` `enfers` et `carte_cell` ; les créatures citent la case `enfers:<case>` dans
`zones_chasse` (pas de `creature_ids`). `game_design.depart.region` de la faction : `carte` `enfers`,
9 `cells`, `niveaux` [1, 10]. Toutes les cases de la croix portent un portail (I à IV sur les bras, V au
centre) : les POI sont dans les 4 coins, plus un poste de garde à 400 m du Portail I (placement du
générateur, POI sans ville dans la case), comme les Ruines de Wismerhill. L'Autel est ajouté aux
`lieux_de_culte` de `religion_enfers_princes_demons`. Script reproductible : `lore_v1071.py` (appliqué
au fichier v1.0.70).

| Case | Terrain | Contenu | Faune (niveaux) |
|---|---|---|---|
| N001_W001 | montagne (`M`, ouverte) | POI **Ruines du Bastion des Damnés** (`lieu_ruines_bastion_des_damnes`, `poi_ruines_bastion_des_damnes`, `ruine`) : bastion d'un maître de plan déchu, englouti par les coulées depuis des âges `[GD]` ; un maître par plan `[BD-A5 p.18]` | diablotin errant (1-4), salamandre (1) |
| N001_E000 | volcanique | Portail II (inchangé, rien n'est posé) | chien des cendres (2-6), salamandre ; petit démon (8-14, abords du portail) |
| N001_E001 | montagne (`M`, ouverte) | POI **Camp d'épreuve des Recrues** (`lieu_camp_epreuve_des_recrues`, `poi_camp_epreuve_des_recrues`, `camp`) : arène où la Légion éprouve les démons nés à la Citadelle `[GD]` | chien des cendres, diablotin errant |
| N000_W001 | volcanique | Portail III (inchangé, rien n'est posé) | chien des cendres, salamandre ; petit démon (abords) |
| N000_E000 | volcanique | **Citadelle de la Légion** (départ) et Portail V (inchangés) | diablotin errant, chien des cendres |
| N000_E001 | volcanique | Portail IV (inchangé, rien n'est posé) | chien des cendres, salamandre ; petit démon (abords) |
| S001_W001 | montagne (`M`, ouverte) | POI **Autel des Pactes** (`lieu_autel_des_pactes`, `poi_autel_des_pactes`, `sanctuaire`) : pactes payés en âmes ou en sang, serment des jeunes démons `[GD]` (religion v1.0.58) | diablotin errant, salamandre |
| S001_E000 | volcanique | Portail I (inchangé) ; POI **Poste de garde du Portail I** (`lieu_poste_garde_portail_1`, `poi_poste_garde_portail_1`, `camp`) à 400 m à l'est du portail `[GD]` | chien des cendres, salamandre ; petit démon (abords) |
| S001_E001 | montagne (`M`, ouverte) | POI **Faille des Cohortes** (`lieu_faille_des_cohortes`, `poi_faille_des_cohortes`, `faille`) : crevasse par où descendent les âmes `[GD]` ; « des milliers d'âmes » `[BD-T10 p.34]`, âmes « en cohortes » `[BD-T12 p.38]` | diablotin errant, chien des cendres |

- **Routes** : aucune. Le réseau routier du lore (`routes`) est fait de cases du continent et ne couvre
  pas les Enfers ; `tools/world_gen` ne relie que les villes du master (une seule aux Enfers : 0 route).
  Rien n'est forcé.
- **Faune** : aucune espèce démoniaque existante n'était dans 1-10 ; deux fiches `[GD]` alignées sur les
  archétypes de jeu déjà posés autour de la Citadelle (faune d'accueil des démons) : **Diablotin errant**
  (`creature_diablotin_errant`, 1-4, archétype 150) et **Chien des cendres** (`creature_chien_des_cendres`,
  2-6, archétype 151) ; salamandre (existante, 1). Le petit démon (8-14) est rattaché aux abords des
  portails I à IV (exception de la décision 2). Les espèces de haut niveau dont l'habitat cite les Enfers
  restent aux portes du continent et à la Porte du T12 (`zones_chasse` inchangées).
- **Générateur** : `Places.cpp` donne les mines procédurales des coins à la Légion (race `demons`) au lieu
  des nains (aucune ville démon sur le continent : sortie des Royaumes inchangée) ; tests `world_gen_tests`,
  `world_places_tests`, `start_locations_tests` passés à 9 cellules. 5 → 9 cellules générées.
- **Faune procédurale** : sur la carte des Enfers, `Population.cpp` (`CreatureFor`) ne tire plus par biome
  mais dans la tranche 1-10 : diablotins (150) près de la Citadelle, puis chiens des cendres (151), et
  ogres des collines (161, niveau 10) au plus loin ; plus d'élémentaire de braise (164, niveau 12). Les
  légionnaires (241) des mines des coins sont des PNJ d'ambiance de la Légion (hostiles aux seuls humains).
  Continent inchangé. Test `world_gen_tests` « faune de niveau 1 a 10 ».
- **Points incertains** : noms ; le poste de garde est sur la case du Portail I (à
  400 m) ; la région `region_enfers_t00` (Enfers des préquelles, hors carte) sert faute de région du lore
  pour la carte jouable ; le Bastion et l'Autel pourraient porter des quêtes d'accueil (aucune créée).

## 16. Corrections et ajouts du propriétaire

### 16.1 Corrections v1.0.72

**Corrections du propriétaire (10/11/2026)**. Chaque fiche touchée porte une note `[DÉCISION] (v1.0.72)` ;
11 entrées sont ajoutées au registre `regle_canon_registre_decisions`. Les routes ne sont que dans le lore
(`routes`) : `tools/world_gen` trace ses propres routes entre les villes du master. Les déplacements, les
reliefs, les cours d'eau et le POI vont dans `world_master.json`. Les nouvelles routes sont numérotées de
000198 à 000206. Leur tracé (`waypoints`, quand il fait plus d'une case) est le plus court chemin hors mer et
hors chaînes de montagnes, et il évite les cases des autres villes `[INFÉRÉ]`. Scripts reproductibles :
`master_v1072.py` puis `lore_v1072.py`, appliqués aux fichiers v1.0.71.

| # | Correction | Ce qui change |
|---|---|---|
| 1 | Route 000066 Syzygie → Ostrevan supprimée | Chaîne Syzygie → Ordhrann (000123), Ordhrann → Thydmar (000166), **Thydmar → Ostrevan (000198)**, par N010_W009. |
| 2 | Vharholdt (`hamlet_wardholt`, N009_W009) au fond d'une crevasse, avec une mine | Fiche : topographie et notes. Lieu **Mine de Vharholdt** (`lieu_mine_de_wardholt`, `open_dungeon_poi`) `[GD]`. Monde jouable : relief `crevasse_wardholt` et mine au fond (voir plus bas). La route 000167 depuis Ordhrann reste la seule. |
| 3 | Route 000006 Lhynn → Altenberg supprimée | Lhynn → Haeldorf (000121), **Haeldorf → Lammerhin (000199)**, **Lammerhin → Altenberg (000200)**. |
| 4 | Ombhral (`hamlet_ambermead`) de N003_W010 à N003_W008 | **Gredhaim → Ombhral (000201)**, **Ombhral → Dys (000202)**. La route 000128 vers Haeldorf est gardée, avec un tracé par N002_W009 qui évite la case du hameau de Marn. La basse-cour, les bœufs et les chiens suivent la ferme ; le lapin reste en N003_W010. |
| 5 | Gredhaim (`hamlet_greyhedge`) de N001_W009 à N001_W008 | 000129 depuis Haeldorf passe par N002_W009. Les chèvres, les ânes et les chiens suivent la ferme ; le molosse et le renard restent. Voir 000130 ci-dessous. |
| 6 | Route terrestre 000151 Hishtarland → Zaltheim supprimée | La route maritime 000109 est gardée. Hishtarland (île, code `o`) n'est plus desservie que par les lignes 000108 et 000109. |
| 7 | La route 000085 Dys → Northind passe par Ghwenaëth | **Dys → Ghwenaëth (000203)**, par N005_W008 et N005_W007, et **Ghwenaëth → Northind (000204)**. |
| 8 | Syllaërin (`hameau_silverglade`, départ des Hauts Elfes) seul de N007_W006 à N007_W003 | 000127 vers Northind, 000192 vers Ghwenaëth et 000193 vers Faernilh sont retracées depuis N007_W003. 000193 passe par N007_W006, la case laissée libre, pour éviter Northind et le village Orkh. Les sentiers partent du hameau le plus proche : 000194 Ghwenaëth → Autel des Vents (même case), 000195 Faernilh → Bosquet des Pavanes, 000196 Faernilh → Tour des Flèches blanches (même case), 000197 Ghwenaëth → Ruines du Temple sylvestre. Pour le Bosquet et les Ruines, les deux hameaux sont à égalité : la région du lieu tranche. |
| 9 | Route Ghwenaëth → Magistrya | **Ghwenaëth → Sauken Glude of Magistrya (000205)**, par N005_W005 et N004_W004. |
| 10 | Vaerdhun (`town_0119`, N010_W015) perchée en haut d'une haute colline, deux rivières en partent | Fiche : topographie et hydrographie. Les rivières `riv_verdansur_sud` et `riv_verdansur_nord` existaient : elles naissent maintenant au pied de la colline, pas dans la ville. Monde jouable : relief `colline_verdansur` et deux `river_courses`. |
| 11 | « Événement » tout à gauche de N010_W016, au niveau des falaises, avec une route depuis Vaerdhun | POI **Sanctuaire des Brisants** (`poi_sanctuaire_des_brisants`, `sanctuaire`, fiche `lieu_sanctuaire_des_brisants`, `shrine_poi`) `[GD]` : croix de pierre et brasier au-dessus des brisants. Événement **Veillée des Feux des Brisants** (`fete_eglise_feux_des_brisants`, fête de `religion_eglise_lumiere`, 28e jour de la Lune des Feuilles) `[GD]` : les fidèles de Vaerdhun gardent les feux jusqu'à l'aube contre les naufrageurs. Le lieu est ajouté aux `lieux_de_culte`. **Vaerdhun → Sanctuaire (000206)**, de type `road` pour la procession. |

- **Route 000130** : elle sert toujours à rejoindre la Via Magna depuis Haeldorf sans traverser Lhynn,
  donc elle est gardée. Depuis N001_W008, N000_W008 est une chaîne de montagnes : la route passe par
  N000_W009 et rejoint la Via Magna en S001_W009 (deuxième case après Lhynn) au lieu de N000_W010.
- **Syllaërin** : `start_for` reste `["elfe"]`. `game_design.depart.cell` de `faction_hauts_elfes` vaut
  N007_W003, tandis que `game_design.depart.region` reste la région 3×3 centrée sur N007_W006.
  `region_id` du hameau suit sa case (`foret_elfique`). Test `world_gen_tests` : un départ doit être
  à 1 case de sa ville de référence, sauf l'elfe, admis à 2 cases de Northind. `start_locations.json`
  est une sortie du générateur, qui le réécrit à partir du master.
- **Axe économique** `axe_cote_ouest` : 000006 devient 000121, 000199, 000200 ; 000085 devient 000203,
  000204.
- **Renvois** : « Questions ouvertes » devient la section 17. Les deux renvois du lore (« section 16 »,
  « LORE_BD §16 ») sont corrigés.

**Réseau routier** (contrôle inspiré de `routes/reseau.py` : `MAXLEN` = 5, mer et chaînes de montagnes
bloquantes, villes traversées comptées comme étapes). Il compte 110 villes et hameaux et 152 routes.
Aucune route n'est trop longue ni ne traverse la mer ou une chaîne. La composante principale réunit
108 villes. Deux îles sont hors du réseau terrestre : **Dyprokh** (ligne 000111, comme avant) et
**Hishtarland**, desservie par les lignes maritimes 000108 et 000109 depuis la suppression de 000151.

**Services** : 1127 → 1137 PNJ de service. Les écuries suivent les routes `road` (au moins 1) :

| Ville | Écuries |
|---|---|
| Lhynn | 4 → 3 |
| Syzygie | 4 → 3 |
| Zaltheim | 3 → 2 |
| Dys | 3 → 4 |
| Magistrya | 2 → 3 |
| Vaerdhun | 2 → 3 |
| Haeldorf | 3 → 4 |
| Ombhral | 1 → 3 |
| Gredhaim | 2 → 3 |
| Lammerhin | 1 → 3 |
| Thydmar | 1 → 2 |
| Ghwenaëth | 1 → 4 |

**Monde jouable** (`world_master.json`, `tools/world_gen`) :

- **Section `reliefs`** (nouvelle). C'est un relief local ajouté au relief macro (`MacroHeight`), donc une
  fonction pure de la position : le raccord entre cellules est exact. Il est ancré sur le centre de la case
  de sa ville, et la ville y est posée, sans recherche de site.
  - `colline_verdansur` : sommet de 300 m de rayon, 130 m au-dessus du relief, pied à 1,4 km. Les deux
    cours tracés à la main `riviere_verdansur_sud` (N010_W015 → N009_W015 → N008_W015) et
    `riviere_verdansur_nord` (N010_W015 → N010_W014 → N011_W014 → N012_W014) remplacent la rivière
    automatique de ces cases. Ils naissent à 150 m au-delà du pied (`BuildMacroRivers`). Il n'y a pas
    de méandres sur le flanc, et le nord contourne Treuvhal.
  - `crevasse_wardholt` : massif de 1,5 km de rayon relevé de 110 m (contour ondulé, pente douce à
    l'extérieur), entaillé d'une fente à fond plat de 340 m de large, avec des parois raides de 70 m de
    large. La fente s'ouvre vers Ordhrann (N009_W010) et se
    ferme 260 m derrière le hameau. À 250 m, la grille des routes lisse les parois : le massif est donc
    fermé aux routes du générateur, qui n'entrent que par l'ouverture. La **mine** est un lieu procédural
    `mine` (nom « Mine de Vharholdt », peuple du hameau), posée au fond de la fente par `Places.cpp`
    (`<cellule>_crevasse_wardholt_mine`) et reliée par un chemin local.
- **POI décalé** : nouvelles clés `offset_x_m` / `offset_z_m` des `pois` (au plus 4,3 km). Le Sanctuaire
  des Brisants est à 3,2 km à l'ouest et 0,7 km au nord du centre de N010_W016, au sec.
- **Cellules générées** : 182 → 188. Gredhaim (N001_W008) et Syllaërin (N007_W003, en forêt sans
  rivière) sont ajoutées. Avec Syllaërin en N007_W003, le corridor des départs passe par N004_W003,
  N005_W003, N006_W003 et N007_W004. N003_W004, N004_W004, N005_W004 et N005_W005 sont gardées
  explicitement : aucune cellule n'est perdue. Le corridor ne change pas autour de Vharholdt.
- **Tests** : `world_gen_tests` gagne « reliefs » (sommet, rivières au pied, crevasse fermée, routes par
  l'ouverture, mine au fond, POI décalé).

**Ce qui n'est pas réel en jeu** :

- N010_W015 (Vaerdhun) et N010_W016 ne sont pas dans `generated_cells` (ajoutées en v1.0.78, §16.7). La colline, les rivières et le
  sanctuaire sont générés : carte du monde, `places.json`, et cellules dès qu'elles seront ajoutées. Ils
  ne sont pas jouables tant que ces cases sont derrière le mur invisible.
- Le générateur ne modèle pas de falaises : la côte y descend en pente douce.
- La Veillée des Feux des Brisants n'existe que dans le lore : il n'y a pas de système d'événements de
  monde côté serveur.

**Points incertains** :

- Les noms (Mine de Vharholdt, Sanctuaire des Brisants, Veillée des Feux des Brisants) et l'événement.
- La hauteur et l'emprise de la colline et de la crevasse.
- Ombhral et Gredhaim quittent la région 3×3 de Haeldorf, dont elles restent les fermes.
- Hishtarland n'a plus de route terrestre.
- Les sentiers à égalité de distance.

### 16.2 Villes moyennes et raid de la mine (v1.0.73)

**Décisions du propriétaire (10/11/2026)**. Chaque fiche touchée porte une note `[DÉCISION] (v1.0.73)`.
6 entrées sont ajoutées au registre `regle_canon_registre_decisions` : 3 villes, la mine, le raid, et une
entrée pour les routes. Noms, races et factions sont `[GD]` et restent à valider. Scripts reproductibles :
`master_v1073.py` puis `lore_v1073.py`, appliqués aux fichiers v1.0.72, puis
`tools/lore/generate_city_services.py`.

**Trois villes moyennes**. Elles ont la taille « ville » de la grille `game_design.cities_services`
(services complets, comme Iustarn). Chacune a une fiche `villes`, une entrée `cities` (`kind` « ville »)
dans `world_master.json` et une cellule générée. Le site choisi par le générateur est sec : pas de
rivière à moins de 300 m.

| Ville | Case | Race, faction `[GD]` | Type | Pourquoi |
|---|---|---|---|---|
| **Hazhaëlin** (`town_hazelgate`) | N007_E002 | Elfes des bois (`race_elfe_des_bois`, avec quelques marchands humains), neutre | `market_town` | Comptoir de lisière : la lisière de la forêt elfique est la strate des Elfes des bois `[HS]`. Les humains y commercent sans entrer dans la forêt. |
| **Fhelvakt** (`town_fellwatch`) | N009_W001 | Humains, Empire (`faction_empire`, `empire_hynn` au master) | `border_town` | Terres des Gigantomorphes : l'Empire en repousse les géants et ceux-ci restent le danger des marches `[HS]`. Ville de colons, de bûcherons (le bois de la région va à la reconstruction de Kendrhir) et de guet face aux géants. Elle ne coupe rien côté forêt elfique, dont les elfes rasent les villages de colons `[BD-A4]`. |
| **Kholvard** (`town_hollowgate`) | S001_W007 | Humains, Empire | `mining_town` | Plaine des Terres de l'Empereur traversée par la Via Magna (000120), dont elle devient une étape. Ville des mineurs de la Mine de la Traverse et des rouliers. |

- **N007_E002 plutôt que N007_E001** `[INFÉRÉ]`. Ce sont deux lisières sans rivière, libres. Sur le relief
  macro du générateur, N007_E002 est plus douce (écart type 18 m contre 21 m, pente moyenne 12 m contre
  14 m par 250 m). Elle est aussi plus près de l'eau : le lac N007_E005 (Mont des Neuf Pics) et la
  rivière N009_E005 sont à trois cases (quatre pour N007_E001). Enfin, Arkhanyra et Askhvir sont toutes deux à
  trois cases (Askhvir est à quatre cases de N007_E001).
- `mining_town` et `border_town` n'ont pas de taille par défaut dans la grille : le champ `taille` vaut
  « ville » dans les trois fiches.
- PNJ de Hazhaëlin : `RACE_VILLES` de `generate_city_services.py` (3 Elfes des bois pour 1 humain) et
  syllabes `NOMS` de `race_elfe_des_bois` (nouvelles).

**Raid de la mine**. Lecture retenue de la demande, **à confirmer** : Gredhaim (N001_W008) et Kholvard
(S001_W007) ne sont pas voisines. La mine est donc une galerie d'environ 14 km creusée sous N000_W008
(chaîne de montagnes), avec une entrée côté Gredhaim et une entrée côté Kholvard. Le raid la traverse
d'une entrée à l'autre.

- Lieu **Mine de la Traverse** (`lieu_mine_de_la_traverse`, `open_dungeon_poi`, N000_W008) `[GD]` :
  ancienne mine de fer impériale qui perçait la chaîne, abandonnée quand une couvée d'araignées géantes
  s'y est installée.
- Raid **La Mine de la Traverse** (`raid_mine_de_la_traverse`) : `min_teams` 1, `min_players_per_team` 5,
  `difficulty` 2, `location_ville_id` Kholvard, toutes factions. Titre « Passe-Galerie » `[GD]`.
- Niveau conseillé 10 à 15 : c'est l'échelle des départs de la Première Province et des Marches (1-15,
  `regle_bestiaire_echelles`). Les créatures existantes sont réutilisées : `animal_rat` (variante « rat des
  profondeurs ») et `animal_araignee` (« araignée géante »). Le boss est nouveau : **Mère des galeries**
  (`creature_mere_des_galeries`, `boss`, niveau 15, `raid_id`) `[GD]`. Les trois sont dans les
  `creature_ids` de N000_W008.
- Monde jouable : deux POI de genre `salle` (comme le Puits des Apprentis). `poi_mine_traverse_nord` est en
  N001_W008, décalé de 1,5 km à l'ouest et 3,6 km au sud, au pied de la chaîne, à 4,4 km de Gredhaim.
  `poi_mine_traverse_sud` est en S001_W007, décalé de 4 km à l'ouest et 4 km au nord, à 5,8 km de
  Kholvard. Le générateur ne sait poser une mine à une ville qu'au fond d'une crevasse (`reliefs`,
  v1.0.72) : la mécanique n'est pas branchée, pour ne pas mettre Gredhaim ni Kholvard dans une
  crevasse.

**Routes** (000207 à 000213) :

| Route | Type | Tracé |
|---|---|---|
| 000207 Hazhaëlin → Arkhanyra | `road` | N007_E002 → N006_E002 → N005_E002 → N004_E001 |
| 000208 Hazhaëlin → Askhvir | `road` | N007_E002 → N008_E003 → N009_E004 → N010_E005 |
| 000209 Fhelvakt → Hazhaëlin | `road` | N009_W001 → N009_E000 → N008_E001 → N007_E002 (hors forêt elfique) |
| 000210 Fhelvakt → Haevrin | `road` | N009_W001 → N010_W002 → N010_W003 → N011_W004 (par la côte, hors forêt elfique) |
| 000211 Kholvard → Skhaldrach | `road` | S001_W007 → S002_W007 → S003_W008 |
| 000212 Gredhaim → Mine de la Traverse | `path` | N001_W008 → N000_W008 |
| 000213 Kholvard → Mine de la Traverse | `path` | S001_W007 → N000_W008 |

Kholvard est aussi une étape de la Via Magna (000120). Le contrôle du réseau (`routes/reseau.py`,
`MAXLEN` = 5, en lecture seule) compte 113 villes et hameaux et 159 routes. Aucune route n'est trop
longue ni ne traverse la mer ou une chaîne. La composante principale réunit 111 villes, dont les trois
nouvelles. Seules Dyprokh et Hishtarland restent hors du réseau terrestre, desservies par leurs lignes
maritimes, comme en v1.0.72.

**Services** : 1137 → 1195 PNJ de service. Hazhaëlin en compte 17, Fhelvakt 19 et Kholvard 18. Une
écurie de plus pour Arkhanyra, Askhvir, Haevrin et Skhaldrach (une route `road` de plus chacune).

**Cellules générées** : 188 → 191 (N007_E002, N009_W001, S001_W007). Le corridor des départs ne change pas
et aucune cellule n'est perdue. L'entrée nord est dans N001_W008, déjà générée. Test `world_gen_tests` :
« villes v1.0.73 » (villes moyennes sur cellule générée, au sec, reliées par le générateur ; entrées de la
mine décalées vers la chaîne, au sec, à l'écart des villes).

**Ce qui n'est pas réel en jeu** :

- La galerie et le raid : le monde jouable n'a que les deux POI d'entrée. Il n'y a ni instance, ni
  souterrain, ni boss, ni récompense côté client ou serveur. Le passage souterrain ouvert après le raid
  n'existe que dans le lore.
- Les routes du lore : `tools/world_gen` trace ses propres routes entre les villes du master. Par exemple,
  il relie Fhelvakt à Syllaërin et Kholvard à Gredhaim, par la surface.
- Le générateur ne peut pas relier Fhelvakt à Dyprokh (île). L'avertissement « route impossible » vers
  Dyprokh vient maintenant de Fhelvakt au lieu de Whispers Hill ; celui d'Askhvir reste.

**Points incertains** :

- La lecture « galerie sous N000_W008 » et la longueur de la galerie.
- Les noms, races et factions des trois villes ; Elfes des bois neutres plutôt que Hauts Elfes.
- Le niveau du raid et du boss ; les récompenses.
- La route 000210 passe par N010_W003, une case du marais maudit autour de Haevrin.

### 16.3 Île du Nord : Île de la Trêve (v1.0.74)

**Décisions du propriétaire (10/11/2026, lot 3)**. Ce qu'il a fixé porte `[DÉCISION] (v1.0.74)` : le terrain,
la région à part entière commune à toutes les factions (Légion comprise) de niveau 45 à 60, les quatre cases de
villes, la ville-port de N017_W010, les ports avec ponton de Haevrin et d'Ostrevan, la route maritime et les
objectifs (quêtes entre factions, 3 raids simples, 1 raid multi-équipe). Tout le reste est `[GD]`, à valider :
noms, races, tailles, contrôle, tracés, quêtes, raids et créatures. 20 entrées sont ajoutées au registre
`regle_canon_registre_decisions`. Scripts reproductibles : `master_v1074.py` puis `lore_v1074.py`, appliqués
aux fichiers v1.0.73, puis `tools/lore/generate_city_services.py`.

**Terrain** (liste exacte du propriétaire, `cells` du lore et `grid` du master). Toute la zone N015-N023 ×
W008-W016 était en `deep_sea` ; les cases non listées y restent.

```
        W015 W014 W013 W012 W011 W010 W009
N023     .    -    -    -    -    -    -
N022     -    -    T    T    T    T    -      T = forest (20 cases)
N021     -    T    T    -    -    -    -      - = shallow_sea (34 cases)
N020     -    T    T    -    T    T    -      . = deep_sea
N019     -    T    T    -    T    T    -
N018     -    T    T    -    -    -    -
N017     -    -    T    T    T    T    -
N016     .    -    -    -    -    -    -
```

Fer à cheval de forêt autour d'une baie ouverte à l'est (W009), îlot au centre (N019-N020 × W010-W011).

**Générateur** (`tools/world_gen`). Le relief macro interpole les cases et les déforme d'environ une case :
la forêt émerge (40 à 140 m au-dessus de la mer) et son rivage passe dans les cases de haut-fond (le haut-fond
est à 26 m, la mer à 40 m). Les 20 cases de forêt **et** les 34 cases de haut-fond sont donc générées : sans
ces dernières, l'île serait coupée net au bord de ses cases terrestres, sans côte ni plage. Cellules générées :
191 → 245, aucune perdue. Hishtarland et Dyprokh (îles `o` ou haut-fond) ne sont pas générées et n'offraient
pas de précédent. Correction de `Output.cpp` : une île de forêt entourée de haut-fond n'a pas de case de côte,
et le code de couverture déformé y vaut souvent `-` (environ 30 % des terres de l'île). Une terre émergée sous
un code marin (` ` ou `-`) était un fond nu (splat « terre » à 0,9) ; elle est maintenant traitée comme une côte
(sable jusqu'à 1,5-6 m au-dessus de l'eau, prairie littorale au-dessus), dans le splat (`BaseSplat`) et dans
la carte du continent (`TerrainColor`). Cela touche aussi, sur le continent, les rares terres sous un code
marin en bord de côte. Le rivage à code `T` (environ 40 % du rivage de l'île) reste en forêt jusqu'à l'eau,
avec la bande de terre mouillée habituelle. Test `world_gen_tests` « ile v1.0.74 ».

**Région** `region_ile_de_la_treve` « Île de la Trêve » `[GD]` (lore) et `ile_de_la_treve` (master, W015-W009 ×
N016-N023). Zone commune à toutes les factions (« la Légion » lue comme la Légion infernale ; les Légions
Impériales sont aussi admises), niveau 45 à 60 pour toutes (`regle_bestiaire_echelles`,
`rule_niveaux_par_faction`). Règle proposée `rule_treve_des_comptoirs` `[GD]` : pas de combat dans l'enceinte des
quatre villes, combat ouvert hors les murs (sur le modèle de la zone neutre de l'Oracle).

**Villes** `[GD]` (fiches `villes`, `cities` du master, services par `generate_city_services.py`) :

| Ville | Case | Taille, type | Race | Contrôle | Rôle |
|---|---|---|---|---|---|
| **Brenhaval** (`town_brinehaven`) | N017_W010 | ville, `port_town` | humains et toutes races (PNJ) | neutre | port franc, départ de la ligne maritime, quartiers par bloc |
| **Duzhmoroth** (`town_duskmoor`) | N022_W010 | village, `fortress_town` | humains, Elfes noirs, Dzoraks, démons | Lune Noire | comptoir du bloc de la Lune Noire (Légion comprise) |
| **Thrival** (`town_truceholm`) | N019_W010 | hameau, `hamlet` | humains | neutre | îlot, Pierre de la Trêve, raid multi-équipe |
| **Lhantarn** (`town_lanternhold`) | N021_W014 | village, `fortress_town` | humains | Empire | comptoir du bloc de l'Empire (Lumière, Justice, Serpent) |

Les blocs suivent `game_design.faction_relations`. Services : 1195 → 1244 PNJ (Brenhaval 22, Lhantarn 11,
Duzhmoroth 8, Thrival 6 ; un capitaine de port de plus à Haevrin et Ostrevan). Brenhaval a une table de races
mêlées (`RACE_VILLES`) ; Lhantarn a un temple garanti (`ajustements` de la grille : chapelle des Ordres).

**Ports et liaisons**. Convention des ports existants : service `port` (`grille.ports` : Brenhaval, Thrival,
Haevrin, Ostrevan), champ `port` de la fiche (`ponton_cell` dans la case de la ville, `vers_cell` côté mer),
POI `port` au master (décalé au bord de l'eau : `poi_port_brinehaven`, `poi_port_truceholm`,
`poi_port_hayfen`, `poi_port_orderfen`), ligne `sea_lines` et routes `sea_route` avec `sea_line_id`.

| Route | Type | Tracé |
|---|---|---|
| 000214 Brenhaval → Camp du Cap | `road` | N017_W010 → N017_W011 → N017_W012 → N017_W013 |
| 000215 Camp du Cap → Lhantarn | `road` | N017_W013 → N018_W014 → N019_W014 → N020_W014 → N021_W014 |
| 000216 Lhantarn → Duzhmoroth | `road` | N021_W014 → N022_W013 → N022_W012 → N022_W011 → N022_W010 |
| 000217 Lhantarn → Tertre des Rois Noyés | `path` | N021_W014 → N020_W013 → N019_W013 |
| 000218 Lhantarn → Grottes des Naufrageurs | `path` | N021_W014 → N020_W014 → N019_W014 → N018_W014 |
| 000219 Duzhmoroth → Tanière du Jarl de Givre | `path` | N022_W010 → N022_W011 → N022_W012 |
| 000220 Thrival → Gouffre de la Baie | `path` | N019_W010 → N020_W011 |
| 000221 Brenhaval → Haevrin | `sea_route` (ligne du Nord) | N017_W010 → N016_W009 → N015_W008 → N014_W007 → N013_W006 → N012_W005 → N011_W004 |
| 000222 Haevrin → Ostrevan | `sea_route` (ligne du Nord) | N011_W004 → N012_W005 → N012_W006 → N012_W007 → N011_W008 |
| 000223 Brenhaval → Thrival | `sea_route` (navette) | N017_W010 → N018_W010 → N019_W010 |

- Ligne `ligne_maritime_du_nord` (`rule_ligne_maritime_du_nord`) : Brenhaval ↔ Haevrin ↔ Ostrevan, aller-retour,
  2 bateaux en positions opposées, pause de 30 s, eau navigable uniquement (comme la ligne du sud).
- **Îlot : navette plutôt que gué** (`navette_brinehaven_truceholm`, 1 bateau, comme Askhvir – Dyprokh). La
  case de haut-fond N018_W010 sépare l'îlot de Brenhaval : dans le générateur, environ 14 m d'eau sur 4 à 5 km.
  Un gué demanderait de changer le terrain fixé par le propriétaire.
- Le Camp du Cap (`lieu_camp_du_cap`, N017_W013) est un relais : la route directe Brenhaval → Lhantarn
  ferait six cases (au-delà de la limite de cinq du contrôle du réseau).
- Contrôle du réseau (`routes/reseau_check.py`, `--mer` pour compter les liaisons maritimes) : 117 villes et
  hameaux, 169 routes, aucune anomalie ; une seule composante avec les liaisons maritimes. Sans elles,
  l'île (Brenhaval, Duzhmoroth, Lhantarn), Thrival, Dyprokh et Hishtarland sont hors du réseau terrestre.

**Raids et lieux** (lieux du lore et POI du master, sur la forêt de l'île) :

| Raid | Équipes | Niveau | Lieu, POI | Boss `[GD]` | Ville |
|---|---|---|---|---|---|
| `raid_tertre_des_rois_noyes` Le Tertre des Rois Noyés | 1 × 5 | 45-50 | N019_W013, `ruine` | Roi noyé (50) | Lhantarn |
| `raid_grottes_des_naufrageurs` Les Grottes des Naufrageurs | 1 × 5 | 50-55 | N018_W014, `salle` (côte sud-ouest) | Capitaine Marée-Rouge (54) | Brenhaval |
| `raid_taniere_du_jarl` La Tanière du Jarl de Givre | 1 × 5 | 55-60 | N022_W012, `salle` (falaise nord) | Jarl de Givre (58) | Duzhmoroth |
| `raid_devoreur_des_brumes` Le Dévoreur des Brumes | 3 × 5, factions mêlées | 60 | N020_W011, `gouffre` (îlot) | Dévoreur des Brumes (boss mondial, 60) | Thrival |

Raid multi-équipe : `for_factions` vide (toutes) ; les équipes doivent venir d'au moins deux blocs (note
`[GD]`, le schéma n'a pas de champ pour cela). Créatures : réutilisées, `creature_geant_de_froid` (45-55, bras
nord, cohérent avec §5.1) et `creature_spectre` (40-52, tertre) ; nouvelles `[GD]`, `creature_loup_des_brumes`
(45-50, toute la forêt), `creature_naufrageur` (46-52) et les quatre boss. Les `creature_ids` des cases suivent
les `zones_chasse`.

**Quêtes entre factions** `[GD]` (`quests`, `region_id` de l'île) :

| Quête | Factions | Relation | Niveau |
|---|---|---|---|
| `quest_ile_gd_treve_des_brumes` La Trêve des Brumes | toutes | coopération (escorte des envoyés à Thrival) | 45 |
| `quest_ile_gd_bois_de_marine` / `quest_ile_gd_coupes_noires` | bloc Empire / bloc Lune Noire | affrontement (quête et contre-quête) | 47 |
| `quest_ile_gd_ambre_des_geants` / `quest_ile_gd_ambre_elfique` | Nains et Chevaliers-Dragons / Hauts Elfes | coopération et affrontement | 50 |
| `quest_ile_gd_ames_des_noyes` / `quest_ile_gd_repos_des_noyes` | Légion infernale et Lune Noire / Lumière et Justice | coopération et affrontement | 52 |
| `quest_ile_gd_chant_du_devoreur` Le Chant du Dévoreur | toutes | coopération, accès au raid multi-équipe | 55 |

**Ce qui n'est pas réel en jeu** :

- Les raids, les quêtes, la trêve et la formation d'équipes mêlées : seuls les POI d'entrée existent dans le
  monde jouable ; ni instance, ni boss, ni récompense côté client ou serveur.
- Les bateaux : aucune ligne maritime n'est jouée ; les POI `port` ne posent pas de ponton. L'île n'est
  accessible qu'en lore.
- Les routes du lore : le générateur relie Brenhaval à Duzhmoroth et Duzhmoroth à Lhantarn par la forêt ;
  Thrival n'a pas de route (« route impossible » vers Brenhaval, Duzhmoroth et Lhantarn), comme Ostrevan
  → Brenhaval (la plus courte liaison entre l'île et le continent passe par la mer).

**Points incertains** :

- Les noms (région, villes, lieux, raids, créatures), les tailles et le contrôle des villes.
- « La Légion » : Légion infernale retenue, Légions Impériales aussi admises.
- L'ordre des escales et le nombre de bateaux de la ligne du Nord.
- Le port d'Ostrevan : sa case ne touche pas l'eau dans le générateur ; le POI est au bord nord de la case,
  à 300 m de l'eau.
- Les niveaux des raids et des boss ; la règle de formation des équipes mêlées.

### 16.4 La cité redevient « Syzygie » (v1.0.75)

`[DÉCISION]` du propriétaire (10/11/2026) : `ville_sysigie` s'affiche **« Syzygie »**, le nom sous lequel les
lecteurs connaissent la cité pendant l'époque du jeu (T1-T11). L'affichage « Aurore » des v1.0.56 à v1.0.74
(§10 Q5, §7.2) est annulé.

- Nom de fondation « Cité d'Aurore » `[BD-A3 pl.7]` et « Aurore » : alias. Les **Portes d'Aurore** `[BD-T8 p.24-25]`
  gardent leur nom (nom de la BD).
- La cité et la principauté (région `syzygie`) portent le même nom, comme dans la BD.
- Toutes les mentions de la cité passent à « Syzygie » : lore (villes, zone `zone_sysigie`, quêtes, notes),
  `world_master.json` (`cities`, descriptions de POI), ROYAUMES.md et ce document. Le lac et le fleuve deviennent
  « lac de Syzygie » et « fleuve de Syzygie ».
- Identifiant inchangé (`ville_sysigie`) : aucun effet sur les sauvegardes ni sur le code.

### 16.5 Corrections de routes et de terrain (v1.0.76)

**Décisions du propriétaire (10/11/2026, lot 5)**. Chaque fiche touchée porte une note `[DÉCISION] (v1.0.76)` ;
13 entrées sont ajoutées au registre `regle_canon_registre_decisions`. Scripts reproductibles : `master_v1076.py`
puis `lore_v1076.py`, appliqués aux fichiers v1.0.75, puis `tools/lore/generate_city_services.py`.

**Tracés**. L'éditeur (`lune-noire-editor.html`) dessine une route par les centres des cases de ses `waypoints`
(identifiants de cases uniquement, pas de coordonnées de carte), ou en ligne droite entre `from_place` et
`to_place` sans `waypoints`. « Réaliser la forme » d'une route, c'est donc lui donner des `waypoints` qui évitent
la mer, la côte, les chaînes et les lacs, par les vallées et les cols. Une **jonction** (route qui part d'une
autre route) suit la convention de 000130 : `from_place` est une case du tracé de la route hôte, `from_ville_id`
et `from_lieu_id` sont vides ; la route hôte reçoit des `waypoints` explicites (même ligne qu'avant) pour porter
cette case.

| # | Demande | Ce qui change |
|---|---|---|
| 1 | Ostrevan – Haevrin passe dans la mer | 000095 : N011_W008 → N010_W007 → N010_W006 → N010_W005 → N011_W004, par la forêt et le marais au sud de la côte. |
| 2 | Tracer 000064, 000073, 000076 | **Lhynn → Dhuzmar** : N001_W010 → N000_W010 (sortie commune avec la Via Magna) → S001_W010 → S002_W011. **Dhuzmar → Klaerhin** : S002_W011 → S003_W012 → S003_W013 (Port Franc, déjà desservi par la ligne droite) → S004_W014 → S004_W015, par la plage. **Askhvir → Stohnvar** : N010_E005 → N010_E006 → N011_E007 → N012_E007 → N013_E007 → N014_E008, par la lisière, en retrait de la côte. |
| 3 | Sentier dans le sable Harndrecht – Ekvhitorn | **000224** (`path`) : S006_W008 → S006_W007 → S006_W006 → S006_W005 (plage) → S005_W004. |
| 4 | Village fantôme – forteresse naine, branche sur 000079 | Village Fantôme du Marais (`ville_village_fantome`, S003_E006) et Forteresse Naine d'Heureux (`ville_forteresse_naine_heureux_t11`, S006_E004). L'ancienne 000155 passait par la montagne (S005_E004). **000155** devient l'embranchement de 000079 Kholdvar → Frhozhol en **S007_E006** : S007_E006 → S006_E005 → S006_E004. Le village rejoint 000079 à Frhozhol par 000149 (déjà tracée). 000079 reçoit ses `waypoints` (S009_E004 → S008_E005 → S007_E006 → S006_E007 → S005_E008). |
| 5 | Réajuster Frhozhol – Der Hem Shelbem (000074) | La ligne droite franchissait la chaîne (Deathtrap Canyon). Nouveau tracé par le col entre les deux chaînes : S010_E010 → S009_E009 → S008_E008 → S007_E008 → S006_E008 → S005_E008. C'est aussi le passage du générateur. |
| 6 | Der Hem Shelbem S009_E010 → S010_E010 | Fiche, `cities` du master, lieu `lieu_der_hem_shelbem` (nécropole) et ses 7 espèces (morts-vivants, Shedu, roi-momie : `creature_ids` des cases et `zones_chasse`) ; extrémités de 000019, 000080, 000152 (tracés droits, au sec). |
| 7 | E020 : désert et zone rocheuse | S001_E020 à S003_E020 : `desert` (`~`). S004_E020 : **`arid_land`** (`:`), voir plus bas. |
| 8 | Supprimer Dhunfal → N015_E015 | 000146 Dhunfal → Nhorgast descendait de Dhunfal à N015_E015 puis remontait. Le tronçon Dhunfal → N015_E015 est supprimé, ainsi que le bout N015_E015 → N016_E016 qui ne menait plus nulle part. **000146** part de 000116 Mhorgad → Dhunfal en **N016_E016** : N016_E016 → N017_E017 → N018_E018. 000116 reçoit ses `waypoints`. |
| 9 | Règles des sentiers | Voir plus bas. |
| 10 | Bhridrak S004_W005 → S004_W006, Krakhol S004_W003 → S004_W002 | Fiches et `cities` ; routes 000180 et 000179 allongées par l'ancienne case ; animaux domestiques déplacés (marmottes et corbeaux en S004_W005, la marmotte reste en S004_W003). Les deux hameaux sortent de la région 3×3 de Drakhenhal (§15.5), qui ne change pas, comme Ombhral et Gredhaim en v1.0.72. |
| 11 | Ruines du prieuré → Lhumbrecht | 000140 (Orhendal) supprimé ; **000225** Lhumbrecht → Ruines du prieuré (`path`, cases voisines). |
| 12 | Gredhaim – Ombhral devient un sentier | 000201 : `type` `path`. |

**Zone rocheuse** (point 7). Le lore n'a pas de type « rocheux » utilisable par le générateur (`cliff` n'a pas de
code dans la grille du master). `arid_land` (terres arides) est retenu : sol de pierre et de terre sans dunes
(contrairement au désert), relief plat. La Tour Carrée de la Lune Noire, haute de trente étages, se dresse sur une
« plaine de terres désolées » ; la case continue aussi les terres arides de S003_E021 à S003_E023. `mountain` aurait
le plus de roche dans le splat, mais il lève un massif d'environ 250 m sous la tour.

**Règles des sentiers** (point 9, toute la carte). Un sentier qui a la même forme qu'une route est supprimé. Un
sentier s'arrête au premier croisement avec une route plus importante (`road` ou `imperial_road`). Le contrôle porte
sur les tracés dessinés par l'éditeur, le sentier étant orienté de son lieu vers sa ville. Si le lieu est déjà sur la
route (croisement à l'origine), le sentier n'a plus de longueur : il est supprimé. Le lieu reste desservi par la route
qui passe par sa case. Aucun lieu ni aucune ville ne perd son accès (contrôle dans le script).

| Sentier | Lieu → ville | Règle | Résultat |
|---|---|---|---|
| 000131 | Camp de manœuvres des Légions → Haeldorf | double 000199 | supprimé |
| 000133 | Ruines de la villa tharque → Haeldorf | double 000128 et 000129 | supprimé |
| 000169 | Lices des novices → Ordhrann | double 000166 (lices dans la case de Thydmar) | supprimé |
| 000170 | Camp des parjures → Ordhrann | lieu sur 000198 | supprimé |
| 000139 | Chapelle de l'ermite → Orhendal | lieu sur 000052 Dys → Iustarn | supprimé |
| 000175 | Arène des Jeunes-Sangs → Grakhul | double 000125 | supprimé |
| 000177 | Affût de la Grande Chasse → Grakhul | double 000142 | supprimé |
| 000197 | Ruines du Temple sylvestre → Ghwenaëth | double 000193, puis le quitte | supprimé |
| 000218 | Grottes des Naufrageurs → Lhantarn | double 000215 | supprimé |
| 000219 | Tanière du Jarl de Givre → Duzhmoroth | double 000216 | supprimé |
| 000138 | Calvaire des pèlerins → Orhendal | croise 000022 Iustarn → Altenberg | raccourci : **Altenberg** → Calvaire |
| 000141 | Camp des naufrageurs → Orhendal | croise 000200 Lammerhin → Altenberg | raccourci : **Lammerhin** → Camp |

Les tracés passent par les centres des cases : la nouvelle extrémité d'un sentier raccourci est le sommet de la route
hôte le plus proche du croisement. Pour 000022 et 000200, ce sont leurs extrémités. Pour 000141, Lammerhin et
Altenberg sont à égalité ; la ferme de la région de départ est retenue. Les autres sentiers ne doublent aucune route
et n'en croisent aucune. Les nouveaux sentiers 000224, 000225 et 000201 non plus.

**Réseau** (`routes/reseau.py --dry` et `reseau_check.py`, `MAXLEN` = 5). Avant : 117 villes et hameaux, 169 routes.
Après : **160 routes**, aucune anomalie. Une seule composante avec les liaisons maritimes. Sans elles, l'île de la
Trêve, Thrival, Dyprokh et Hishtarland restent hors du réseau terrestre, comme en v1.0.75. `reseau.py` ignore
les jonctions (extrémité sans ville) et voit la Forteresse d'Heureux isolée. `reseau_check.py` (v1.0.76) raccorde
une jonction aux villes de sa route hôte : la forteresse est dans la composante principale.

**Services** : 1244 → 1241 PNJ de service. Une écurie de moins pour Dhunfal (5 → 4 : 000146 n'y arrive plus),
Ombhral et Gredhaim (3 → 2 : 000201 devient un sentier).

**Monde jouable** (`world_master.json`) : villes déplacées, grille (`~` en S001_E020 à S003_E020, `:` en S004_E020),
`generated_cells`. S004_W006 et S004_W002 sont ajoutées. Avec Bhridrak en S004_W006, le corridor des départs passe
par S004_W007 et non plus par S005_W008 et S006_W004 à S006_W008. Ces six cases sont gardées explicitement : aucune
n'est perdue. Cellules générées : **245 → 248**. S010_E010 l'était déjà. Test `world_gen_tests` « corrections v1.0.76 »
(villes déplacées sur cellule générée, au sec, reliées ; codes de la colonne E020).

**Ce qui n'est pas réel en jeu** :

- Les routes et sentiers du lore. `tools/world_gen` ne lit pas la collection `routes` ni les `waypoints` : il trace
  ses propres routes entre les villes du master (Kruskal, 2 plus proches voisines, A* sur le relief). Le jeu ne peut
  donc pas suivre ces tracés sans un changement du générateur, par exemple une section de routes imposées dans le
  master. Il relie déjà Der Hem Shelbem à Frhozhol par le même col. Il relie aussi Bhridrak à Harndrecht,
  Skhaldrach et Ekvhitorn.
- Les sentiers et les jonctions n'existent que dans le lore et l'éditeur.

**Points incertains** :

- La lecture du point 4 : la liaison village – forteresse emprunte 000149 et 000079, avec 000155 en embranchement.
  Pas de route directe du village jusqu'à 000079, qui doublerait 000149.
- La lecture du point 8 : le bout N015_E015 → N016_E016 est aussi supprimé, et le reste de 000146 est raccroché à
  000116.
- `arid_land` pour « zone rocheuse ».
- Bhridrak est sur les neiges éternelles (S004_W006). Le Grand Ancien (`creature_dragon_grand_ancien`, 66-70)
  chasse dans cette case, à côté du haras, hors de la région de départ ; rien n'est retiré.
- Aucune « Porte du Sud » n'existe dans le lore ni dans le master. Seuls le Palais des Maîtres des Dragons
  (S003_W004, inchangé, toujours relié par 000124 et 000082) et Ekvhitorn ont été vérifiés.
- La sortie commune de Lhynn avec la Via Magna (N000_W010) pour 000064.

### 16.6 Île de la Trêve : relief et temples (v1.0.77)

**Décisions du propriétaire (10/11/2026, lot 6)**, notées `[DÉCISION] (v1.0.77)` : le nom « Île de la Trêve » est gardé
et rendu visible ; l'île reçoit un dénivelé important (crêtes, falaises, vallées encaissées) ; de nombreux temples de style
inca, de tailles différentes, servent aux quêtes et aux raids ; ils sont **habités** par un peuple vivant de l'île ; les
raids et quêtes de la v1.0.74 s'y rattachent. Tout le reste est `[GD]`, à valider : peuple, faction, culte, noms, tailles,
PNJ, dialogues, quêtes, hauteurs. 6 entrées sont ajoutées au registre `regle_canon_registre_decisions`. Scripts
reproductibles : `master_v1077.py` puis `lore_v1077.py`, appliqués aux fichiers v1.0.76.

**Nom visible**. Les cellules du lore portaient toutes le nom technique `cell_<id>` et l'éditeur n'affichait aucun nom de
cellule (seulement l'identifiant de région au survol). Les 20 cellules terrestres de l'île ont `name` = « Île de la
Trêve ». L'éditeur (`lune-noire-editor.html` et sa copie `-v20`) écrit maintenant chaque nom propre de cellule une seule
fois sur la carte, au-dessus des cellules qui le portent, et l'affiche au survol et dans le titre de la fiche de cellule ;
le survol donne aussi le nom de la région. Il liste en outre tous les lieux d'une case (temple et entrée de raid) au lieu
du premier seulement. En jeu, la région du master garde son nom sans accents (« Ile de la Treve », convention du master).

**Relief**. Lore : 7 cases passent en `mountain` (pics et crêtes boisés au cœur des branches du fer à cheval) ; les 13
autres restent en `forest` (vallées, cols, plateaux des villes) ; `visual_elements` renseignés sur les 20 cases.

```
        W014 W013 W012 W011 W010
N022          T    ^    T*   D        ^ = mountain (pic ou crête boisée)
N021     L    ^                       T = forest ; T* = forest à falaises (massif côtier)
N020     ^    T         ^    T        D, L, B, t = Duzhmoroth, Lhantarn, Brenhaval, Thrival
N019     T*   ^         T    t
N018     T*   ^
N017          T    ^    T*   B
```

Générateur : la grille `^` avait été essayée. Elle déplaçait la côte de 2 à 4 km vers le large (la montagne, interpolée
avec le haut-fond, émerge dans les cases de mer : la baie se refermait) et retirait la forêt des sommets. La grille du
master **reste donc en `T`** et le relief vient d'un nouveau type de relief local du générateur, `massif` (section
`reliefs`, `tools/world_gen`) : ancré sur une case (`cell`, décalage `offset_x_m` / `offset_z_m`) et non sur une ville,
dôme lobé (rayon `foot_radius_m` ≤ 7 km, hauteur `height_m` ≤ 600 m) découpé par un bruit en crêtes (pics, crêtes,
ravins) ; il ne s'ajoute qu'aux terres déjà émergées, si bien que la côte ne bouge pas et que le relief monte d'un coup
au rivage (falaises). C'est le seul écart volontaire entre `terrain_type` du lore et la grille du master (noté dans les
cases et dans `_comment_reliefs`).

| Massif | Case | Hauteur, rayon | Décalage | Rôle |
|---|---|---|---|---|
| `massif_n020_w014` | N020_W014 | 550 m, 6,5 km | 2,5 km à l'ouest | plus haut sommet, falaises ouest |
| `massif_n019_w013` | N019_W013 | 520 m, 6,5 km | 1 km à l'ouest | crête du bras ouest |
| `massif_n022_w012` | N022_W012 | 500 m, 6,5 km | 2,5 km au nord | crête et falaise nord |
| `massif_n021_w013` | N021_W013 | 480 m, 6,5 km | 1,5 km au nord | pic au-dessus de Lhantarn |
| `massif_n018_w013` | N018_W013 | 480 m, 6,5 km | 1 km O, 2 km S | pic sud du bras ouest |
| `massif_n017_w012` | N017_W012 | 480 m, 6,5 km | 2,5 km au sud | crête du bras sud, falaises sud |
| `massif_n020_w011` | N020_W011 | 380 m, 5 km | 1,5 km à l'ouest | butte de l'îlot |
| `massif_n019_w014`, `_n022_w011`, `_n017_w011`, `_n018_w014` | côtes O, N, S, SO | 240 à 280 m, 4,5 km | 2,5 à 3 km vers la mer | falaises côtières |

Sonde du relief macro : sommets de 470 à 570 m au-dessus de la mer sur les 7 cases de montagne (moyenne 140 à 180 m),
10 à 13 % de pentes au-delà de 25° ; vallées et cols entre 30 et 80 m en moyenne. Les sites des quatre villes ne changent
pas (même position, au sec, écart de 10 m au bord du plateau) ; les routes du générateur non plus (Brenhaval –
Duzhmoroth, Duzhmoroth – Lhantarn ; Thrival sur l'îlot reste sans route, comme en v1.0.74). Avertissements du
générateur identiques à la v1.0.76. Test `world_gen_tests` « relief v1.0.77 » : côte inchangée (mêmes terres émergées
avec et sans massifs), sommets, falaises, villes, temples. Rendu de la carte du générateur : `ile_relief_v1077.png`.

**Peuple** `[GD]` : les **Hwakhari**, rameau humain descendant du peuple de la mer dont les rois reposent dans le Tertre des
Rois Noyés ; quand la mer a pris les cités basses, ils sont montés sur les crêtes. Fiches : race `race_huacani` (PNJ
seulement, comme `race_elfe_des_bois`), faction non jouable `faction_huacani` (environ 1 000 âmes), culte
`religion_huacani_soleil` « Le Soleil des Cimes » (`lore.religions`, convention v1.0.58 : Soleil des Cimes, Lune
d'Argent, Mère des Eaux, esprits des sommets ; les prêtres de la Brume gardent le Dévoreur endormi ; fêtes à placer dans
le calendrier). **Trêve** : les Hwakhari en sont les plus anciens garants ; la Pierre de la Trêve de Thrival est une de
leurs pierres d'alliance ; ils ont accepté les comptoirs à condition qu'on ne se batte ni dans les villes ni dans les
temples (note ajoutée à `rule_treve_des_comptoirs`). Neutres envers les deux blocs (`game_design.faction_relations`
inchangé), alliés des neutres, ennemis des hors-la-loi, des naufrageurs, des géants de froid et du Dévoreur ; méfiants
envers la Légion infernale et la Lune Noire.

PNJ (`persons`, un dialogue chacun) : Ataw Kantu, Fils du Soleil (Grand Sanctuaire) ; Willak Rumi, Gardien de la Brume
(Temple de la Brume) ; Sami Qori, gardienne des cordes-mémoires (Temple des Ancêtres Noyés) ; Tamaru, coureur des crêtes
(Oratoire du Col) ; Amaru le Renégat, prêtre qui veut réveiller le Dévoreur (ennemi).

**Temples** `[GD]` (fiches `lieux` `shrine_poi`, `faction_control` = `faction_huacani`, POI au sommet ou sur la terrasse,
décalés dans la case ; architecture : pierre taillée sans mortier, terrasses, escaliers taillés, portes trapézoïdales,
or ou argent) :

| Temple | Taille | Case | POI (hauteur) | Habitants | Rattachement |
|---|---|---|---|---|---|
| Grand Sanctuaire du Soleil | grand, 9 degrés, 32 m, 90 m | N020_W014 | `temple` (553 m) | ~400 | siège du Fils du Soleil |
| Temple des Ancêtres Noyés | grand, 7 degrés, 24 m, 70 m | N019_W013 | `temple` (524 m) | ~40 | raid du Tertre, 3 quêtes |
| Temple de la Brume | grand, 5 degrés, 20 m, 60 m | N020_W011 | `temple` (374 m) | ~60 | raid du Dévoreur, 2 quêtes |
| Temple du Givre | moyen, 4 terrasses, 14 m | N022_W012 | `temple` (548 m) | ~25 | raid du Jarl, 2 quêtes |
| Temple de la Mère des Eaux | moyen, 3 degrés, 12 m | N018_W014 | `temple` (268 m) | ~20 | raid des Naufrageurs |
| Temple de la Lune d'Argent | moyen, 4 degrés, 15 m | N018_W013 | `temple` (486 m) | ~30 | quête du Renégat |
| Temple des Cimes | moyen, 3 degrés, 10 m | N021_W013 | `temple` (507 m) | ~15 | quêtes du bois de marine |
| Temple des Terrasses | moyen, 3 degrés, 9 m | N017_W012 | `temple` (473 m) | ~80 | quête des terrasses |
| Oratoire du Col | petit, 2 terrasses, 3 m | N022_W013 | `sanctuaire` (38 m) | 2 | coureur des crêtes |
| Autel des Sources | petit, 3 bassins, 4 m | N019_W014 | `sanctuaire` (150 m) | 3 | coureur des crêtes |
| Oratoire de l'Aube | petit, 5 m | N017_W011 | `sanctuaire` (320 m) | 2 | coureur des crêtes |

**Raids et quêtes rattachés**. Les quatre lieux de raid de la v1.0.74 sont gardés et rattachés au temple de leur case :
le Tertre des Rois Noyés s'ouvre sous la terrasse basse du Temple des Ancêtres Noyés (800 m plus bas sur le flanc) ; les
Grottes des Naufrageurs sont au pied de la falaise du Temple de la Mère des Eaux, dont les naufrageurs volent le feu
sacré ; la Tanière du Jarl est la salle basse du Temple du Givre, prise par les géants ; le Gouffre de la Baie est scellé
par le Temple de la Brume (Porte du Gouffre). Descriptions et notes des quatre raids mises à jour ; leur ville de départ
(`location_ville_id`) ne change pas, le schéma n'ayant pas de champ de lieu. Les 8 quêtes de la v1.0.74 ont une étape
réécrite pour passer par un temple. Nouvelles quêtes `[GD]`, toutes factions : `quest_ile_gd_coureur_des_cretes` (45),
`quest_ile_gd_terrasses_affamees` (48), `quest_ile_gd_fil_des_ancetres` (50, ouvre le choix Âmes / Repos des noyés),
`quest_ile_gd_renegat_de_la_brume` (54, précède le Chant du Dévoreur).

**Réseau**. Neuf temples sont dans une case traversée par une route ou un sentier (000214, 000215, 000216, 000217,
000220). Deux sentiers sont ajoutés, chacun partant de la route la plus proche sans en doubler aucune : **000226**
Lhantarn → Temple des Cimes (N021_W014 → N021_W013) et **000227** Camp du Cap → Temple de la Lune d'Argent
(N017_W013 → N018_W013). Contrôle (`reseau_check.py --mer`) : 117 villes et hameaux, 162 routes, une composante, aucune
anomalie ; `reseau.py --dry` donne les mêmes propositions qu'en v1.0.76.

**Services** : inchangés (1241 PNJ, aucune ville ajoutée). Cellules générées : inchangées (248).

**Ce qui n'est pas réel en jeu** :

- Les temples : aucun modèle 3D de temple inca n'existe. Les 11 POI (`temple`, `sanctuaire`, genres existants) ne sont
  que des repères (nom sur la carte et le radar) ; ni édifice, ni terrasse, ni escalier ne sont posés.
- Les Hwakhari, leurs PNJ, dialogues et quêtes, la paix des sanctuaires : lore seulement (aucun PNJ placé, aucun spawner).
- Les sentiers du lore et les liens raid – temple : seuls les POI d'entrée existent, comme en v1.0.74.
- Le relief est réel (heightmaps des cellules après régénération) ; la forêt y reste celle du code `T` (conifères
  au-dessus de 300 m, pas d'arbre au-delà de 34° de pente).

**Points incertains** :

- Les noms (peuple, culte, temples, PNJ, quêtes), les tailles et le nombre d'habitants.
- Le peuple : rameau humain (nouvelle fiche de race PNJ) plutôt que race existante.
- Les hauteurs des massifs et la part des cases en `mountain` ; l'écart lore `mountain` / grille `T`.
- Le rattachement des raids : lieux de raid gardés à côté des temples plutôt que fusionnés avec eux.

### 16.7 Cellules de Vaerdhun et des Brisants, décisions du 11/11 (v1.0.78)

Lot 7 du propriétaire (11/11/2026).

**Cellules N010_W015 et N010_W016.** Vaerdhun (`town_0119`, colline de 130 m d'où partent les deux rivières) et le
Sanctuaire des Brisants (`poi_sanctuaire_des_brisants`, à 3,2 km à l'ouest du centre de N010_W016, contre les
falaises) existent depuis la v1.0.72 (§16.1, points 10 et 11). Leurs cases n'étaient pas générées pour une seule
raison : elles ne figuraient pas dans `generated_cells` de `world_master.json`. Ce ne sont pas des cases de départ, et
le corridor des départs (`connect_start_cities`) ne passe pas par là : la cellule générée la plus proche est
N010_W011 (Syzygie). Le relief (`colline_verdansur`), les deux cours `riviere_verdansur_sud` / `riviere_verdansur_nord`
et le POI décalé étaient déjà calculés par le générateur (carte du monde, `places.json`), mais aucune heightmap de
ces deux cases n'était produite.

- `generated_cells` : nouvelle dernière ligne `"N010_W015", "N010_W016"` (commentaire du master mis à jour).
- Cellules générées : **248 → 250** (sonde : liste master + corridors, avant / après). Aucune cellule perdue, corridor
  inchangé.
- Génération des deux cases seules (`--cells N010_W015,N010_W016`) sans erreur : 16 lieux procéduraux,
  385 bâtiments, 952 spawners. Rendu : sommet de la colline sous Vaerdhun (191 m), rivière sud qui part du pied de
  la colline, cours nord-est vers N010_W014, mer au bord ouest de N010_W016, sanctuaire au sec à moins de 2 km du rivage.
- Test `world_gen_tests` « cellules v1.0.78 » (cases dans `generated_cells`, Vaerdhun en N010_W015, sanctuaire
  dans les lieux de N010_W016).

**Décisions inscrites au registre** (`regle_canon_registre_decisions`, 5 entrées v1.0.78, une par fiche) :

| # | Décision du propriétaire | Fiches |
|---|---|---|
| 1 | Du contenu (quêtes, raids, monstres) de plus haut niveau peut exister dans une zone de niveau inférieur. Les chemins doivent être bien balisés pour faire comprendre au joueur de faible niveau qu'il entre dans une zone où les adversaires sont plus forts. But : obliger à visiter tout le monde. Exemple : Bhridrak (départ 1-10) partage sa case S004_W006 avec le Grand Ancien (66-70) ; les deux restent en place. | `rule_niveaux_par_faction`, `regle_bestiaire_echelles`, cellule `S004_W006` |
| 2 | Le nom « lac / fleuve de Syzygie » est conservé. `riv_fleuve_sysigie` (nom provisoire « [à nommer] » depuis la v1.0.55) devient « Fleuve de Syzygie », dans le lore et dans `river_courses` du master (champ `name`, non utilisé par le générateur). | `riv_fleuve_sysigie` |
| 3 | Le niveau d'une zone est relatif à la faction : une zone de faible niveau pour une faction ne l'est pas forcément pour les autres. Principe seulement : le détail par faction sera défini plus tard ; à partir du niveau 20 du joueur, beaucoup de choses changent. | `rule_niveaux_par_faction` |

La décision 1 assouplit la règle « pas de dragon adulte en zone débutant » appliquée en v1.0.68 (§15.5) et le point incertain de la v1.0.76 (§16.5) : le
Grand Ancien reste en S004_W006, déjà dans ses `zones_chasse` et les `creature_ids` de la case. Aucun niveau,
aucune créature et aucune case ne changent.

**Services** : inchangés (1241 PNJ). Aucun identifiant supprimé du lore.

**Ce qui n'est pas réel en jeu** :

- Les deux cases sont générées mais isolées : quatre cases non générées (N010_W012 à N010_W014) les séparent des
  autres cellules jouables. On n'y arrive pas à pied tant que le corridor n'est pas étendu.
- La route 000206 Vaerdhun → Sanctuaire n'existe que dans le lore : le générateur ne relie pas les POI par des routes.
  Le sanctuaire n'est qu'un repère (aucun modèle de croix ni de brasier), et la côte descend en pente douce (pas de
  falaise modélisée).
- La Veillée des Feux des Brisants : lore seulement (pas de système d'événements de monde côté serveur).
- Le balisage des chemins vers les zones plus fortes (décision 1) et les niveaux par faction (décision 3) : règles
  du lore, sans mécanisme en jeu.

**Points incertains** :

- Raccorder Vaerdhun au réseau jouable (cases N010_W012 à N010_W014, ou corridor vers Syzygie) : fait en v1.0.79 (§16.8).
- La forme du balisage (panneaux, avertissements de PNJ, couleur du nom de zone) et le seuil d'avertissement.

### 16.8 Raccordement de Vaerdhun au monde jouable (v1.0.79)

Lot 8 du propriétaire (11/11/2026). Les cases N010_W015 et N010_W016 (v1.0.78, §16.7) étaient générées mais isolées.
Le propriétaire valide l'ajout des trois cases qui les séparaient du reste du monde jouable.

| Case | Terrain (grille et fiche) | Contenu |
|---|---|---|
| N010_W012 | `plain` | Plaine de la région de Syzygie ; touche N010_W011 (déjà générée, rive ouest du fleuve de Syzygie). |
| N010_W013 | `plain` | Plaine ; la route Syzygie → Vaerdhun la traverse. |
| N010_W014 | `plain_with_river` | Rivière nord de Vaerdhun (`riviere_verdansur_nord`, vers N011_W014) ; routes vers Syzygie et Treuvhal. |

- `generated_cells` : nouvelle dernière ligne `"N010_W012", "N010_W013", "N010_W014"` (commentaire du master mis à jour).
  Les cases N010_W011 à N010_W016 forment une rangée continue : Vaerdhun et le Sanctuaire des Brisants sont
  accessibles à pied depuis Syzygie (par la rive ouest de N010_W011).
- Cellules générées : **250 → 253** (sonde : liste master + corridors, avant / après). Aucune cellule perdue, corridor
  inchangé.
- Fiches de cellules : terrain cohérent avec la grille du master, aucune modification de game design. Une note
  `[DÉCISION]` par case et une entrée au registre (`regle_canon_registre_decisions`, 3 entrées v1.0.79).
- Génération des cinq cases (`--cells N010_W012,...,N010_W016`) sans erreur : 40 lieux procéduraux, 777 bâtiments,
  2405 spawners.
- Test `world_gen_tests` « cellules v1.0.78 » étendu : rangée N010_W011 à N010_W016 générée.
- Rendu de preview : le sommet de la colline (191 m) sortait blanc en v1.0.78 parce que l'outil de rendu (hors dépôt)
  normalisait la couleur entre l'altitude minimale et maximale de l'image. Il utilise désormais une rampe en altitude
  absolue (neige au-dessus de 520-600 m, comme le jeu). Le jeu n'est pas concerné : le splat (`Output.cpp`) ne met de
  neige qu'à partir de 520 m, et le canal neige des cinq cases est à 0.

**Route lore.** La route 000083 Syzygie → Vaerdhun existe dans le lore, ainsi que 000024 Vaerdhun → Treuvhal
(N011_W014). Le générateur trace bien les deux.

**Ce qui n'est pas réel en jeu** :

- Le tracé de la route Syzygie → Vaerdhun du générateur passe aussi par N009_W012 (environ 2 km en sortant de
  Syzygie) et par N011_W013 – N011_W014 (environ 12 km, jusqu'à 5,5 km au nord de la rangée), non générées : sur ces
  tronçons, la route est derrière le mur invisible et le joueur doit couper à travers la plaine de la rangée N010.
  Treuvhal (N011_W014) et la route 000024 restent hors du monde jouable (jusqu'à la v1.0.80, §16.9).
- Depuis Syzygie, la route part vers l'ouest et entre dans N009_W012 avant la rangée N010 : le joueur la quitte
  en N009_W011 et rejoint N010_W012 à travers champs par la rive ouest du fleuve (N010_W011).

**Points incertains** :

- Générer aussi N009_W012, N011_W013 et N011_W014 (route continue et Treuvhal jouable), ou retracer la route dans la
  rangée N010 : à décider par le propriétaire. Décidé en v1.0.80 : les trois cases sont générées (§16.9).

### 16.9 Route Syzygie – Vaerdhun et Treuvhal jouables (v1.0.80)

Lot 9 du propriétaire (11/11/2026). Le tracé en jeu de la route 000083 Syzygie → Vaerdhun sortait de la rangée N010
(§16.8). Le propriétaire valide l'ajout des trois cases qu'il traverse ; Treuvhal devient accessible.

| Case | Terrain (grille et fiche) | Contenu |
|---|---|---|
| N009_W012 | `plain` | Plaine à la sortie ouest de Syzygie (N009_W011) ; la route 000083 y passe sur environ 2 km. |
| N011_W013 | `plain` | Plaine ; la route 000083 la traverse. |
| N011_W014 | `plain_with_river` | Treuvhal (`hamlet_0148`), rivière nord de Vaerdhun (`riviere_verdansur_nord`, vers N012_W014) ; routes 000083 et 000024. |

- `generated_cells` : nouvelle dernière ligne `"N009_W012", "N011_W013", "N011_W014"` (commentaire du master mis à jour).
- Cellules générées : **253 → 256** (sonde : liste master + corridors, avant / après). Aucune cellule perdue, corridor
  inchangé.
- Fiches de cellules : terrain cohérent avec la grille du master (région `syzygie`), aucune modification de game design.
  Une note `[DÉCISION]` par case et une entrée au registre (`regle_canon_registre_decisions`, 3 entrées v1.0.80).
- Génération des 21 cases N009–N011 × W016–W010 (`--cells`) sans erreur, aucun avertissement sur les trois nouvelles
  cases (487, 341 et 494 spawners).
- Test `world_gen_tests` « cellules v1.0.78 » étendu : les trois cases sont générées, Treuvhal est en N011_W014, et
  chaque point des routes Syzygie → Vaerdhun et Vaerdhun → Treuvhal est dans une cellule générée.

**Ce qui est réel en jeu** :

- Route 000083 Syzygie → Vaerdhun : tracé du générateur N009_W011 → N009_W012 → N010_W012 → N010_W013 → N011_W013 →
  N011_W014 → N010_W014 → N010_W015, entièrement dans des cases générées. Il passe par Treuvhal et emprunte ensuite le
  tracé de Vaerdhun → Treuvhal.
- Route 000024 Vaerdhun → Treuvhal : N010_W015 → N010_W014 → N011_W014, entièrement dans des cases générées ; elle ne
  sort vers aucune case non générée. Treuvhal n'a pas d'autre route au lore.
- Le générateur trace aussi une liaison Syzygie → Treuvhal (maillage des villes proches, sans route au lore), qui suit
  000083 de Syzygie à Treuvhal, dans les mêmes cases.

**Ce qui n'est pas réel en jeu** : la rivière nord de Vaerdhun continue au nord de Treuvhal vers N012_W014, non générée
(mur invisible au bord nord de N011_W014). Aucune route n'y mène ; rien n'est ajouté.

### 16.10 Enfers et dragons de Xygga d'après les tomes 15-22 (v1.0.81)

Lot 10 du propriétaire (11/11/2026). Sources : notes de lecture des tomes 15 à 22 (section 7 Enfers, section 9
dragons). Pages : T16 et T19 en pages du PDF (T16 : planches 4-5 de l'album = p.7, planches 6-7 = p.8), autres tomes
en pages lues. Chaque fiche issue de ces tomes porte sa source `[BD-T15 p.N]` à `[BD-T22 p.N]`. Script reproductible :
`lore_v1081.py` (appliqué au fichier v1.0.80) et `master_v1081.py` (insertion des POI dans les deux masters).

**Décisions inscrites au registre** (`regle_canon_registre_decisions`, 13 entrées v1.0.81, note `[DÉCISION] (v1.0.81)`
sur chaque fiche) :

| # | Décision du propriétaire | Fiches |
|---|---|---|
| 1 | À l'époque du jeu (T1-T11), **Lucifer est maître des Enfers** ; les T16-T22 (Lucifer disparu, Belzébuth régent puis maître) sont postérieurs et ne s'appliquent pas. | `char_lucifer_prince_tenebres` |
| 2 | Hiérarchie : Lucifer ; princes Belzébuth, Pazuzu, Proserpine et Helle (archidémonesses), Quetebah, Bellatrux ; dessous la mère de Wismerhill, la conseillère de Belzébuth, les ducs des abysses, les démons de cour, la garde rouge, Sorrow, Bourzigutt. | `regle_hierarchie_enfers` |
| 3 | Tous les personnages relevés sont retenus ; pas de noms inventés, désignation descriptive. | `regle_hierarchie_enfers` |
| 4 | Belzébuth, Roi des Mouches : grand prince **non jouable**, utilisable dans les histoires (T16 p.12). | `char_belzebuth` |
| 5 | Pazuzu règne sur les cieux des Enfers (T22 p.5) : fiche existante complétée, pas dupliquée. | `char_pazuzu` |
| 6 | Sorrow, « fille des Enfers » à six bras, est incluse. | `char_sorrow` |
| 7 | Crâne est un archimage de Methraton, pas un démon : fiche de PNJ à part, hors hiérarchie. | `char_archimage_crane` |
| 8 | Palais de Lucifer et mers de lave sur la carte des Enfers ; ce qui n'y tient pas devient des plans accessibles par portail depuis un lieu de la carte. | `regle_plans_infernaux` |
| 9 | Les dragons vivent dans le monde des Royaumes, chacun caché dans une grotte difficile à trouver ; **un dans un volcan** (Xygga). | `regle_dragons_de_xygga` |
| 10 | Xygga a **10 enfants** (premiers-nés). | `regle_dragons_de_xygga` |
| 11 | **Pas de noms** pour les dragons pour l'instant. | `regle_dragons_de_xygga` |
| 12 | **Genre de Xygga non tranché** : ne pas l'affirmer (le T16 emploie le masculin, la Nuée des Dragons dit « Père de tous les Drakkhs »). | `char_xygga` |
| 13 | Tomes 15-22 = **référence visuelle et technique**, sauf Enfers et dragons intégrés (rang 2 bis dans `regle_canon_priorite_sources`, étiquettes dans `regle_canon_etiquettes`, libellés de `tomes_config`). | `regle_canon_corpus` |

**Personnages** (17 fiches `persons` ajoutées, faction `faction_legion_infernale`, race `race_demon` sauf Crâne) :

| Rang | Fiche | Source |
|---|---|---|
| Maître | `char_lucifer_prince_tenebres` (complétée : nom secret « Nerheneïm », apparence du T15 à la place de l'ancien visuel « enchaîné » sans appui, trône, rang) | `[BD-T15 p.15, 24-25, 31]`, `[BD-T16 p.12-16]` |
| Princes | `char_belzebuth` (graphie « Belfébut » au T17) ; `char_pazuzu` (complétée) ; `char_proserpine` ; `char_helle` (distincte d'Hellaynnea) ; `char_quetebah` ; `char_bellatrux` | T16-T22 ; T22 p.5 ; T21 p.8 ; T19-T21 ; T22 p.20 ; T22 p.22 |
| Maîtres des plans | `char_fleur_du_mal` (Jardin des Supplices) ; le prince-pieuvre violet, le démon de flammes à tête de crâne, le prince vert pustuleux, le prince insectoïde violet (`char_maitre_plan_*`) | T18 p.16-18 ; T21 p.36 |
| Dessous | mère de Wismerhill (`char_princesse_elfique_t00`, complétée : avatar démoniaque, âme aux Enfers, sans nom) ; `char_demone_conseillere_belzebuth` ; `char_ducs_des_abysses` ; `char_demons_de_cour_enfers` (dont les conseillers de Lucifer du T15) ; `char_garde_rouge_enfers` ; `char_sorrow` (lié à `loot_sorrow`) ; `char_bourzigutt` (cité) | T22 p.4-6 ; T16 p.12-16 ; T15, T16, T20, T22 ; T18 p.18, T22 p.3-4 ; T19 PDF p.16-18 ; T15 p.26 |
| Hors hiérarchie | `char_archimage_crane` (« L'archimage de Methraton, maître des démons ») | T16 p.10-11, T22 p.16, 22 |

Les événements postérieurs au T14 (régence, guerre infernale, captures par Methraton) sont notés `[ÉPOQUE]` sur
chaque fiche. Deux champs `name` évitent des mots déjà portés par des noms de famille de PNJ de service
(`validate_city_services.py` refuse tout PNJ dont un mot reprend un nom des fiches) : la garde rouge s'appelle « La
garde du palais des Enfers » (« garde rouge » dans la description) et Crâne « L'archimage de Methraton, maître des
démons » (nom en description) ; de même, les grottes portent un nom de lieu sans couleur. `religion_enfers_princes_demons`
reçoit les princes (divinités, clergé de rang 1) et le palais (lieux de culte).

**Enfers : lieux** (15 fiches `lieux`, `carte` `enfers`, `region_enfers_t00`) :

| Case | Lieu (POI) | Source |
|---|---|---|
| N001_E000 | **Palais de Lucifer** (`poi_enfers_palais_de_lucifer`, `temple`) : nef gothique rouge à voûtes, piliers de crânes, sol vert au pentagramme, Grand Trône doré ogival sur une pyramide de visages hurlants et de damnés, bain de sang aux défenses d'ivoire ; 66-70 | T15 p.24, T16 p.12-15, T20 p.13-14 et p.30, T21 p.8, 42 |
| N001_E000 | Nef des Âmes (`poi_enfers_nef_des_ames`, `salle`) | T15 p.15 |
| N001_E000 | Plateau du Rituel (`poi_enfers_plateau_du_rituel`, `sanctuaire`) | T15 p.24, T17 p.16 |
| N000_W001 | **Mer de lave** (`poi_enfers_mer_de_lave`, `volcan`) : lave, basalte, fleuves et cascades de lave | T16 p.16, T17 p.16, T22 p.22 |
| N000_W001 | Seuil des Plans (`poi_enfers_seuil_des_plans`, `faille`) `[GD]`, accès aux plans | — |
| N000_E001 | Canyon des Falaises rouges (`poi_enfers_canyon_des_falaises_rouges`, `defile`) et Viaduc des Gueules de lave (même repère) | T16 p.16 |
| N000_E001 | Plateaux suspendus des Légions (`poi_enfers_plateaux_des_legions`, `camp`), pyramides inversées flottantes | T16 p.16, T20 p.30-33, T21 p.6-17 |
| N000_E000 | Pentacle étoilé de seuil : lié aux 5 Portes (pentagramme centré sur la Citadelle) ; sans POI | T20 p.14-21 |
| hors carte | Plans du Jardin des Supplices, des eaux sombres, du feu, de la maladie, des insectes, plan prison | T18 p.16-18, T21 p.36, T22 p.8-16 |

Les 4 coins et la case de la Citadelle ne changent pas ; la région de départ reste 1-10, les lieux puissants sont du
contenu balisé de plus haut niveau (décision v1.0.78). Test `world_gen_tests` « lieux v1.0.81 ».

**Dragons de Xygga** (`regle_dragons_de_xygga`) : 11 fiches `creatures` (`world_boss`, `unique`), 11 `lieux`
(`entree_cachee`), 11 POI `salle` du master des Royaumes, créature ajoutée aux `creature_ids` de chaque case.
Niveaux `[GD]` : premiers-nés **66-70**, comme le Grand Ancien (« une des créatures les plus puissantes de l'univers »,
T16 p.6) ; Xygga **70**, plafond actuel des boss mondiaux, au-dessus du Grand Ancien dont il est le premier ; endormi
(statufié au cœur du volcan jusqu'au T14) : rencontre narrative, combat à décider. Cases choisies sans ville, sans route
à moins de 4 km du centre, sans POI ni lieu, hors régions de départ ; grottes à plus de 1,5 km de toute route (test).

| Dragon (souffle) | Case | Milieu | Grotte (entrée) | En jeu |
|---|---|---|---|---|
| Xygga, Grand Dragon rouge (non montré ; feu `[GD]`) | S008_E012 | volcan (`V`) | Grotte du volcan endormi : fissure dans une coulée figée | **réelle** (cellule générée) |
| doré enflammé (feu jaune-orange) | N002_E004 | désert | Grotte de la falaise de grès : faille | lore seul |
| rouge à cornes (non montré) | S005_E001 | montagne (nains du sud) | Grotte de la vire : paroi | lore seul |
| jaune-vert épineux, tête de tricératops (jet vert acide) | N011_W005 | marais maudits | Grotte des racines du marais : sous-bois | lore seul |
| petit rouge épineux (non montré) | S007_E021 | montagne (nains de l'est) | Grotte de la crevasse de l'est | lore seul |
| blanc à cornes noires (non montré) | S006_W001 | montagne (Royaume Drach) | Grotte du surplomb : falaise | lore seul |
| noir aux ailes brunes (non montré) | N010_E012 | forêt | Grotte des ronces : sous-bois | **réelle** (cellule générée) |
| hydre (non montré) | N013_E017 | marais (Marches) | Grotte de la cascade du marais | lore seul |
| dragon de glace (givre blanc) | N021_E016 | montagne enneigée du grand nord | Grotte du glacier : crevasse | lore seul |
| dragon bleu (rayon bleu-cyan) | N008_W015 | côte (Syzygie) | Grotte marine de la côte ouest : falaise, marée basse | lore seul |
| oriental doré-orangé (flammes orange) | N006_E007 | forêt à rivière | Grotte derrière la cascade | lore seul |

Aucun dragon existant n'est déplacé ni modifié (Grand Ancien en S004_W006, Kruikna, « Cruik », dragon bleu de Pilou,
dragons noirs de Moork) ; le dragon bleu est noté distinct du dragon bleu de Pilou.

**Ce qui est réel en jeu** : les 7 POI des Enfers et les 11 POI de grottes (repère, nom, aplanissement de 40 m)
dans les cellules générées ; pour les grottes, seulement S008_E012 et N010_E012. Génération complète des Enfers sans
avertissement. **Lore seulement** : pas de lave, de palais ni de grotte modélisés, aucun PNJ ni boss (Lucifer, princes,
dragons) côté jeu, plans sans instance ni portail ; les 9 autres grottes sont dans des cellules non générées.

**Services** : inchangés (1241 PNJ). Aucun identifiant supprimé (57 ajoutés).

**Points incertains** : combat contre Xygga endormi ; noms des dragons (à donner) ; Seuil des Plans (nom, et
instances des plans) ; la démone conseillère de Belzébuth du T16 est-elle celle des T17-T18 ; « 666 légions de
666 666 démons » (T20) ne sont pas des effectifs de l'époque du jeu.

### 16.11 Les douze peuples lointains et les Ophidiens d'après les tomes 15-22 (v1.0.82)

Lot 11 du propriétaire (11/11/2026). Sources : notes de lecture des tomes 15 à 22, sections 2 (« les 12 peuples ») et
passages sur les Ophidiens ; pages comme en 16.10 (T16 et T19 en pages du PDF). Chaque fiche porte ses sources
`[BD-T15 p.N]` à `[BD-T22 p.N]`. Scripts reproductibles : `lore_v1082.py` et `peuples_data.py` (appliqués au fichier
v1.0.81), `master_v1082.py` (POI du master des Royaumes).

**Décisions inscrites au registre** (`regle_canon_registre_decisions`, 12 entrées v1.0.82, note `[DÉCISION] (v1.0.82)`
sur chaque fiche) :

| # | Décision du propriétaire | Fiches |
|---|---|---|
| 1 | **Option b** : les 12 civilisations humaines des T15-T22 existent dès maintenant comme **peuples lointains**, hors carte ; territoires non générés (ouverts plus tard) ; comptoirs, marchands, ambassadeurs, mercenaires et PNJ dans les grandes villes dès maintenant. | `regle_peuples_lointains` |
| 2 | **Placement par climat** : Nordiques au grand nord ; Égyptiens, Arabes et peuple turc ou persan dans les déserts du sud ; Aztèques et Amérindiens à l'ouest au-delà de l'océan ; Chinois, Mongols et Japonais à l'est ; Africains, peuple pâle et peuple sombre placés de façon cohérente. | `regle_peuples_lointains` |
| 3 | **Noms de la BD gardés** : Tsin, Empire Minghol, Azteca, Haaghen (alias Bjorn, T16), les Nordiques, Murata ; noms inventés dans le style de la BD pour les autres (ni français ni anglais), lieux en français. | `regle_peuples_lointains`, `char_murata` |
| 4 | **Indépendants à l'époque du jeu** : pas d'empereur Wismerhill, de gemmes au front, de religion de l'Unique, de Grand Conseil ni de légions de 12 000 (post-T14) : **référence visuelle future** ; apparences, cultures, architectures gardées en fiches. | `regle_peuples_lointains` |
| 5 | **Ophidiens = peuple ennemi souterrain**, hostile à tous : hommes-serpents télépathes (bulles vertes, « sss »), Empereur, Impératrice pondeuse, Prince Serpent, frères inférieurs (serpents espions), montures sauriennes, cités souterraines, écloserie. | `regle_ophidiens` |
| 6 | **Pas d'Ophidia Prime en surface** (elle est sur Terra Secunda) ; entrées cachées de haut niveau, balisées, en marais, jungle et montagne. | `regle_ophidiens` |
| 7 | Tomes 15-22 : peuples et Ophidiens intégrés au lore, en plus des Enfers et des dragons (`regle_canon_priorite_sources`, `regle_canon_etiquettes`, libellés de `tomes_config` mis à jour). | `regle_canon_corpus` |
| 8-11 | Quatre comptoirs dans des ports existants (ci-dessous). | `town_brinehaven`, `ville_altenberg`, `hishtarland`, `town_riverfold` |

**Peuples lointains** (12 fiches `factions`, type `peuple_lointain`, non jouables, `fiche_peuple` : statut du nom,
apparence, culture, architecture, territoire, climat, relations, sources ; 12 `lieux` hors carte, sans cellule). La BD
compte douze civilisations dont celle de Wismerhill (l'Empire, post-T14) ; le peuple turc ou persan, montré aux T16,
T17 et T18, complète les douze du propriétaire. Directions sur la carte de synthèse (flèches depuis le bord de la
grille) :

| Fiche | Peuple (nom) | Dirigeant | Direction | Comptoir | PNJ dans nos villes `[GD]` |
|---|---|---|---|---|---|
| `faction_nordiques` | **Les Nordiques** (BD, T17 p.15) | Haaghen (BD, `char_haaghen`) | nord | Brenhaval | Sigvhard Ulfrekk (ambassadeur, Lhynn) ; Rhagnvild Skaeldar (marchande, Brenhaval) ; Bjarnulf Haddrek (mercenaire, Feyhin Lokcthat) |
| `faction_vharnoct` | **Les Vharnoctes** `[GD]`, peuple sombre gothique | Nhecthyra `[GD]` | nord-nord-est | Brenhaval | Dhraegor Nokhtvael (envoyé, Moork) ; Ysmhera Vhaul (alchimiste, Sauken Glude) |
| `faction_empire_minghol` | **L'Empire Minghol** (BD, T15 p.36) | Ogtaïr Khaan `[GD]` | nord-est | Ryvaldh | Ghaldaï Mönkhur (ambassadeur, Lhynn) ; Temughar Oïrdan (maquignon, Syzygie) ; Khulaïn Sorghat (archère, Feyhin Lokcthat) |
| `faction_akhazumi` | **L'archipel d'Akhazumi** `[GD]`, japonais | Ryodhaï `[GD]` | est-nord-est | Ryvaldh | Tokhimaru Seïdan (rônin, Feyhin Lokcthat) ; Okhiyu Shiranhe (marchande, Ryvaldh) |
| `faction_empire_tsin` | **L'Empire de Tsin** (BD, T17 p.7, T19 PDF 31) | Tsin (BD, `char_tsin`) | est | Ryvaldh | Tshao Yunkhai (mandarin ambassadeur, Lhynn) ; Pheng Lhaosu (marchand, Ryvaldh) |
| `faction_kharzhad` | **Le Kharzhad** `[GD]`, turc ou persan | Ouzmerhid `[GD]` | sud-est | Ryvaldh | Ozhmeran Tchavhul (marchand, Lhynn) ; Faryzhad Ilkhaner (astrologue, Sauken Glude) |
| `faction_sheydhar` | **Les émirats du Sheydhar** `[GD]`, arabe | Rashkhal `[GD]` | sud-sud-est | Hishtarland | Zhaïd Al-Mourhan (caravanier, Moork) ; Qasrim Bhen-Haddar (mercenaire, Feyhin Lokcthat) ; Leïmara Sabhaït (épicière, Hishtarland) |
| `faction_nebkhara` | **Le royaume de Nebkhara** `[GD]`, égyptien | Tahmesyr `[GD]` | sud | Hishtarland | Haremkhet Sobhaïr (prêtre ambassadeur, Lhynn) ; Senmhoret Djhaïs (scribe-mage, Sauken Glude) ; Peribhes Khaïmut (marchand, Hishtarland) |
| `faction_nkhaloa` | **Le royaume de Nkhaloa** `[GD]`, africain | Oyombhé `[GD]` | sud-sud-ouest | Hishtarland | Obhangu Ndheri (marchand, Hishtarland) ; Kwesenda Mbhoro (lancière, Feyhin Lokcthat) |
| `faction_azteca` | **Azteca** (BD, T16 PDF 37-39) | l'Azteca (BD, `char_azteca`) | ouest-sud-ouest | Altenberg | Ixtlamhoc Tecuhal (prêtre ambassadeur, Lhynn) ; Citlalhua Yaotzin (marchande, Altenberg) |
| `faction_tahokhan` | **Les nations tahokhanes** `[GD]`, amérindien | Ahkenhwa `[GD]` | ouest-nord-ouest | Altenberg | Ohsakhe Tawendra (éclaireur, Altenberg) ; Mahtenha Kwisoh (marchande, Altenberg) |
| `faction_saelvhyr` | **Le Saelvhyr** `[GD]`, peuple pâle d'allure elfique | Elvhaïr `[GD]` | nord-ouest | Brenhaval | Ysilvaë Caerhaïn (envoyée, Northind) ; Thaelor Vhyssenn (armateur, Brenhaval) |

- **Dirigeants** : seuls les trois nommés par la BD ont une fiche `persons` (`char_haaghen`, `char_tsin`, `char_azteca`) ;
  les autres sont dans `leadership` de leur faction. Haaghen est distinct de l'Empereur Haaghendorf. Murata (`char_murata`,
  `companion_murata_t2`) vient de l'archipel d'Akhazumi (« son bateau a coulé », T2) ; aucun PNJ ne le remplace.
- **PNJ** : 28 fiches `persons` (`char_pl_*`, rôles `ambassadeur_peuple_lointain`, `marchand_peuple_lointain`,
  `mercenaire_peuple_lointain`, `erudit_peuple_lointain`) rattachées à leur ville par `ville_ids`, dans 10 villes : Lhynn (6),
  Feyhin Lokcthat (5, arène), Hishtarland (3), Altenberg (3), Sauken Glude (3), Brenhaval (2), Ryvaldh (2), Moork (2),
  Syzygie (1), Northind (1). **Ce ne sont pas des PNJ de service** : `generate_city_services.py` ne gère que les métiers de
  la grille (un exemplaire de service = un PNJ) ; ajouter ces PNJ à `service_npcs` fausserait les comptes de la grille.
  Leurs noms entrent dans les noms interdits aux futurs PNJ de service (`noms_interdits` lit `persons`) ; aucun mot ne
  recoupe un PNJ de service existant, ni un nom du lore, du master ou de `noms_proposition.md`.
- **Comptoirs** (ports existants, `features` et note de la ville) : **Brenhaval** (nord : Nordiques, Vharnoctes,
  Saelvhyr ; port franc, quai des indépendants), **Altenberg** (ouest : Azteca, Tahokhans ; statut du port mis à jour,
  destination régulière toujours à décider), **Hishtarland** (sud : Sheydhar, Nebkhara, Nkhaloa ; royaume indépendant
  du sud), **Ryvaldh** (est : Tsin, Minghol, Akhazumi, Kharzhad). Ryvaldh était `port_town` sans port : ajouté à
  `grille.ports` et champ `port` (ponton vers N006_E024) ; un capitaine de port créé. Aucune ligne maritime vers les
  terres lointaines.
- **Distinctions** : l'Empire du Jade (extension, est-sud-est, par les terres) et Tsin (plus loin, au-delà de la mer
  orientale) sont deux empires rivaux, game design du Jade inchangé ; Azteca n'est pas les Hwakhari (Île de la Trêve) ;
  les Tharques tiennent la mer de l'ouest, sur la route d'Azteca ; le Nebkhara se dit héritier de l'Erypt, qui reste le
  passé (M1-M3). Notes `[GD]` ajoutées sur `region_empire_jade`, `faction_huacani`, `empire_tharque`, `lieu_erypt`.
- **Référence visuelle future** (note `[ÉPOQUE]` sur chaque fiche) : couronne des douze rois, gemmes et opales au front,
  l'Unique et son clergé, Grand Conseil, légions de 12 000 (1 000 par civilisation), Cité des Opales, Garde d'Opale.

**Ophidiens** (`regle_ophidiens`) : `faction_ophidiens` (ennemie de toutes les factions et des douze peuples),
`race_ophidien`, 3 fiches `persons` sans nom (`char_empereur_ophidien`, `char_imperatrice_ophidienne`,
`char_prince_serpent`), 10 fiches `creatures`, 5 `lieux`. Niveaux `[GD]`, sous les dragons de Xygga (66-70) :

| Créature | Niveaux | Source |
|---|---|---|
| Guerrier ophidien, archer ophidien | 55-60 | T16 PDF 4-6, 44 ; T17 p.5, 15, 24-29 |
| Naja ophidien (sans jambes), saurien de guerre | 58-62 | T16 PDF 4-9 ; T17 p.5 |
| Seigneur au capuchon doré (quatre bras pour certains) | 60-63 | T15 p.36 ; T17 p.5, 15, 24-25 |
| Sauropode harnaché | 60-64 | T15 p.36 ; T17 p.24 ; T18 p.4, 6 |
| Frère inférieur (serpent espion) | 50-54 | T16 PDF 20 ; T20 p.11 |
| Prince Serpent (boss) | 64-66 | T16 PDF 4-9, 18 |
| Impératrice pondeuse, Empereur (« Grand Ophidien ») (boss de raid) | 66-67, 67-68 | T17 p.8, 20 ; T18 p.4-10 ; T20 p.11 |

| Lieu | Case | Milieu | En jeu |
|---|---|---|---|
| Puits des Lianes (`lieu_ophidiens_puits_des_lianes`, POI `poi_ophidiens_puits_des_lianes`, salle, à 2,3 km à l'ouest et 2,5 km au nord du centre) | N010_E010 | jungle de Leviathan | **réel** (cellule générée) |
| Bouche des Roseaux (`lieu_ophidiens_bouche_des_roseaux`) | N010_W003 | Marais maudits de Thenhart | lore seul |
| Faille des Mues (`lieu_ophidiens_faille_des_mues`), entrée principale | S011_E015 | montagnes du sud-est | lore seul |
| La Cité-Nid (capitale souterraine) et l'Écloserie | souterrain, sans cellule | — | lore seul |

Cases choisies sans ville ni lieu, hors des régions de départ, hors des cases des grottes de Xygga et à deux cases au
moins de chacune (pas de conflit avec les dragons, lot 10) ; abords balisés (têtes sur des piques, ossements, T17 p.5),
aucune route n'y mène ; le Puits des Lianes est à plus de 1,5 km des routes et à plus de 15 km de toute grotte (test).
N010_E010 touche en diagonale la région de départ de Grakhul (1-10) : contenu de plus haut niveau en zone basse, permis
s'il est bien balisé (décision v1.0.78). Les créatures sont ajoutées aux `creature_ids` des trois cases. Distincts de la
Maison du Serpent, du Serpent de Methraton et des serpents marins du Jade. Hors époque : Ophidia Prime, pacte de Xygga,
guerres contre l'empire de Wismerhill.

**Ce qui est réel en jeu** : le POI du Puits des Lianes (repère, nom, aplanissement) dans la cellule générée N010_E010.
**Lore seulement** : les 12 peuples, leurs terres, dirigeants et 28 PNJ (aucun PNJ placé ni spawner), les comptoirs
(fiches de villes ; Brenhaval et Altenberg sont générées mais rien n'y est ajouté côté jeu), les deux autres entrées,
la Cité-Nid, l'écloserie, les créatures et boss ophidiens (aucun spawner). Test `world_gen_tests` « lieux v1.0.82 ».

**Services** : 1242 PNJ (+1, capitaine de port de Ryvaldh). Aucun identifiant supprimé (77 ajoutés).

**Points incertains** : noms `[GD]` des peuples et dirigeants ; directions exactes ; le peuple turc ou persan peut
être deux peuples (vieillard au bonnet pourpre du T15, barbu au turban rouge du T18) ; instances de la Cité-Nid et de
l'écloserie ; butin ophidien ; destinations maritimes régulières des comptoirs.

### 16.12 Palette infernale du générateur pour les Enfers (v1.0.83)

Lot 12 du propriétaire (11/11/2026). Défaut corrigé : la carte générée des Enfers sortait verte (herbe, forêts,
neige sur les sommets des coins), alors que les Enfers de la BD sont roche rouge, basalte, cendre et lave. Sources
visuelles (notes de lecture des tomes 15-22, section 7) : canyon de falaises rouges striées, fleuves et cascades de
lave jaune-orange `[BD-T16 p.16]` ; plaine de lave jaune-orange, îlots de basalte noir à colonnes `[BD-T17 p.16]` ;
ciel de feu jaune-orange `[BD-T20 p.30-31]`, `[BD-T21 p.6]` ; « mer de flammes », falaises noires et lave
`[BD-T21 p.36]` ; paysage de lave `[BD-T22 p.22]`.

**Décision inscrite au registre** (`regle_canon_registre_decisions`, 1 entrée v1.0.83, note `[DÉCISION] (v1.0.83)` sur
`region_enfers_t00`) : palette des Enfers d'après les T15-22, sans herbe, forêt ni neige.

| Sortie de `tools/world_gen` (carte `enfers` seulement) | Avant (v1.0.82) | Après (v1.0.83) |
|---|---|---|
| Splat `terrain_splat.slap` (4 couches à couleur fixe côté client) | herbe, terre, roche, neige | **terre rouge-brun** (plaines, lave, chemins) et **roche** (pentes, coins `M`, hauts sommets) ; herbe et neige à 0 |
| Masque d'herbe `terrain_grass.grms`, densité forestière `terrain_forest.frms` | herbe sur les plaines | **vides** |
| Décor `scatter.bin` | conifères des coins, broussailles, fleurs, herbes hautes | **arbres morts** (calcinés) et rochers seulement |
| Carte du monde `world_map.png` | vert, brun, neige blanche | **roche rouge striée** par l'altitude, **basalte noir**, **cendre grise** au sommet, **lave jaune-orange** non ombrée ; abîme sombre hors carte |

**Lave** (`InfernalLavaAt`, fonction pure de la position) : mer de lave d'environ 1,7 km de rayon autour de chaque
lieu `volcan` (la Mer de lave de N000_W001) ; fleuves de lave sinueux de 80 à 120 m dans la croix volcanique `V`,
tenus à 0,55-0,95 km des villes et des lieux (Citadelle, portails, palais restent praticables). **Limite** : le client
n'a ni couche ni matériau de lave (4 couches fixes herbe, terre, roche, neige ; aucune couleur par zone) : en jeu, la
lave est un sol de terre rouge-brun, sans lueur ni dégâts ; le basalte noir et la cendre grise ne sont visibles que
sur la carte du monde. Une couche émissive de lave (shader, 5e canal ou palette par zone) reste à faire côté client,
non faite ici.

**Monde des Royaumes inchangé** : toutes les branches sont gardées par `IsInfernalMap` (carte `enfers`). Preuve : les
34 fichiers générés de trois cellules des Royaumes (N003_W009 forêt, N021_E015 sommets enneigés, S006_E013
volcanique) sont identiques octet pour octet avant et après, et la carte du continent rendue par le même code aussi.
Test `world_gen_tests` « palette v1.0.83 » (Enfers : aucun canal herbe ou neige, masques vides, décor sans plante
verte, lave sur la carte, aucun pixel vert ; Royaumes : herbe et masque d'herbe présents, pas de lave).

**Services** : 1242 PNJ (inchangé). Aucun identifiant ajouté ni supprimé.

### 16.13 Route Frhozhol – Der Hem Shelbem par le Deathtrap Canyon (v1.0.84)

**Décision du propriétaire (11/11/2026)** : la route 000074 passe **obligatoirement** par la cellule S008_E010.
Elle franchit la chaîne de montagnes par le **Deathtrap Canyon**, gorge qui sert de passage.

Nouveau tracé : S010_E010 (Der Hem Shelbem) → S009_E010 → **S008_E010** → S007_E009 → S006_E008 → S005_E008
(Frhozhol). Il remplace le passage par le col S009_E009 → S008_E008 décidé en v1.0.76 (§16.5).

**En jeu** : rien ne change pour l'instant. Le générateur ne lit pas les tracés du lore ; il continue de calculer
son propre passage. Toutes les cellules du nouveau tracé sont déjà générées.

### 16.14 Balisage des zones dangereuses et ancrages manquants (v1.0.85)

Lot 13 du propriétaire (corrections rapides validées le 12/11/2026), d'après le relevé de densité v1.0.84 (§4 et
anomalies). **Rien n'est déplacé**, aucun identifiant n'est supprimé. Scripts reproductibles : `lore_v1085.py`
(appliqué au fichier v1.0.84) et `master_v1085.py` (POI du master des Royaumes).

**Décisions inscrites au registre** (`regle_canon_registre_decisions`, 5 entrées v1.0.85, une par type de correction,
note `[DÉCISION] (v1.0.85)` ou `[INFÉRÉ] (v1.0.85)` sur chaque fiche) :

| # | Correction | Fiches |
|---|---|---|
| 1 | Balisage des contenus nettement plus forts que leur zone (décision 1 de §16.7) | 10 cellules, `rule_niveaux_par_faction`, `regle_dragons_de_xygga` |
| 2 | Whispers Hill : poche de bas niveau des Terres du Nord | `region_terres_nord_t00` |
| 3 | Abysses de la baie : lieu sous-marin voulu, conservé | `lieu_abysses_baie` |
| 4 | Raids rattachés à une case | 7 raids |
| 5 | Quêtes ancrées à une case | 19 quêtes |

**1. Balisage.** Même mécanisme que pour les Ophidiens (v1.0.82) : note de cellule « balisée » (`[DÉCISION]
(v1.0.85) Balisage du danger…`), note sur les fiches de créature et de lieu, et, dans une cellule générée, la balise
décrite dans un POI réel. Formes retenues (`rule_niveaux_par_faction`) : repère visible sur le passage (panneau,
stèle, cairn, borne ; POI `monolithe` si la case est générée), PNJ avertisseur au hameau ou à la ville voisine (note
« avertisseur » de la ville, pas de nouveau PNJ), légendes et ossements **sans révéler l'entrée** pour les grottes des
dragons, enceinte interdite et gardée pour les créatures d'une faction.

| Case | Menace (niveaux) | Zone | Balise |
|---|---|---|---|
| N013_E017 | Premier-né de Xygga, l'hydre (66-70) | Marches 1-15 | Légende de la « bête aux têtes de toutes les couleurs » à Dhemorr, carcasses à la lisière du « marais du sud » |
| N008_W015 | Premier-né, dragon bleu (66-70) | Syzygie 10-30 | Pêcheurs de Vaerdhun : « la côte du sud » et son rayon bleu ; rochers vitrifiés, carcasses gelées |
| N006_E007 | Premier-né, dragon oriental (66-70) | Forêt elfique 10-30 | Bûcherons de Feyhin Lokcthat : « la rivière du sud » ; arbres roussis, ossements |
| N010_E012 | Premier-né, dragon noir aux ailes brunes (66-70) | Orkland 18-45, voisin du départ de Grakhul | **Cairn des Os noircis** `[GD]` (POI réel, au sud de la case, à plus de 3 km de la grotte) ; chamans de Grakhul : « la forêt du nord » |
| N001_W010 | Dragons d'or (50-65), dragon de l'Empereur (42-50), golems (35-50) | Lhynn, départ des Légions 1-10 | Forces de l'Empire, pas de la faune : dragonnières interdites et gardées (gardes rouges, hérauts) ; golems avec les mages de bataille |
| N001_W010 | Raid du Défilé de Mordrek (boss 55) | — | Faux positif du relevé (« Lhynn » cité comme but des réfugiés) : raid rattaché au Défilé de Mordrek (N016_E013, Gigantomorphes 30-56) |
| N002_W008 | Dragons d'or (50-65), golems (35-50) | Terres de l'Empereur 10-30 | Élevages impériaux palissadés et gardés, panneaux « défense d'approcher » sur le chemin Ombhral – Gredhaim |
| N008_W007 | Gargouilles (40-50), démons à tête de sanglier (30-40) | Départ de Syllaërin 1-10 | **Stèle des Flèches blanches** `[GD]` (POI réel, 3 km au sud de la Porte 5) : flèches à empennage blanc des guetteurs [HS] ; avertisseur à Faernilh |
| S003_W004 | Dragons des Chevaliers-Dragons (40-48) | Départ de Drakhenhal 1-10 | Montures de la faction : sentinelles et dragons guetteurs interdisent l'accès hors faction [BD-T2 p.29-30] |
| N016_E019, N016_E020 | Trolls (26-40) | Marches 1-15 | Panneaux « TROLLS » [BD-A3 pl.16] ; à Grhaevan et Klindor, on conseille le feu [BD-A1 p.60] |

Les 6 couples restants du relevé (faune 1-15 de N011_E008 et N012_E008 en zone 30-56) sont traités au point 2.

**2. Whispers Hill.** La case N011_E008 porte le village vivant de Whispers Hill et les Ruines de Wismerhill (décision
v1.0.56), N012_E008 la Forêt de la Rencontre (T1) ; leur faune (loup, sanglier, lapin du T00, monstre de forêt du T1)
est de niveau 1-15, alors que les cases étaient rattachées aux Terres des Gigantomorphes (30-56). La région
`region_terres_nord_t00` (« Terres du Nord — Château en Ruine ») existait déjà, décrivait ces lieux et portait les
quêtes du T00, et les repères de `regle_bestiaire_echelles` donnent déjà « Terres du Nord » 1-15. Correction minimale :
`region_id` des deux cases, de Whispers Hill et de la Forêt de la Rencontre → `region_terres_nord_t00`, **enclave de
deux cases** dans les Terres des Gigantomorphes ; faune, villes et lieux inchangés. Sorties balisées par trois
**Bornes des Géants** `[GD]` (POI réels, borne gravée d'une empreinte de géant, à moins de 500 m de la route) : nord
(N012_E008, route de Stohnvar), ouest (N011_E008, route d'Askhvir), sud (N011_E008, routes de Feyhin Lokcthat et de
Ghulvathan). Le master (boîtes `regions`) n'est pas modifié.

**3. Abysses.** `lieu_abysses_baie` (S008_W005, mer profonde) est voulu sous la mer : fiche dictée par le propriétaire
(`rule_abysses_baie_acces`, 25/10/2026), entrée cachée « jamais indiquée ». Gardé en place, avec l'accès précisé : en
plongeant, potion de respiration aquatique bue, depuis le bateau de la ligne maritime du sud, dont l'étape 4 (route
000110, Zaltheim → Ekvhitorn) passe au-dessus de S008_W005. Terre la plus proche : plages de S006_W005 / S006_W006 (deux
cases au nord) et îles d'Hishtarland (S010_W006, deux cases au sud) ; ports : Ekvhitorn (S005_W004) et Brokhendt
(S008_W002), à trois cases.

**4. Raids.** Rattachés (note `[INFÉRÉ] (v1.0.85)`, `location_ville_id` quand le lieu est une ville, sinon id du raid
cité dans la fiche du lieu ou de la cellule) :

| Raid | Case | Appui |
|---|---|---|
| `raid_bataille_dis_t4` | N005_W009 (Dys) | marché de Dis [BD-T4 p.24-31] |
| `raid_t00_armee_ghorghor` | N011_E008 (Whispers Hill) | village en contrebas du château [BD-T00 p.32] |
| `raid_mordrek_t14` | N016_E013 (`lieu_defile_mordrek`) | nom du raid ; boss `creature_gigantomorphe` |
| `raid_dzoraks_marais_maudits_t11` | N010_W004 (marais de Thenhart) | case du marais au pied des Ruines de Thenhart [BD-T11] ; l'Arbre aux Ossements n'a pas de lieu |
| `raid_abysses_temple_englouti`, `raid_abysses_antre` | S008_W005 (`lieu_abysses_baie`) | description du raid |
| `raid_coeur_du_volcan` | S008_E014 (`lieu_volcan_cratere`) | `rule_volcan_acces` |

Restent sans case (note sur la fiche) : `raid_grande_bataille_plaine_t10` (Grande Plaine sans case),
`raid_t12_01_porte_des_enfers` (Porte du T12 « au cœur de l'Empire », distincte des 5 Portes),
`raid_marches_est_t14` (Marche de l'Est, 56 cases, lieu du duel non situé), `raid_orques_greldinard_t14` (aucun lieu).

**5. Quêtes.** 19 ancrées sans ambiguïté par le lieu ou la ville cités (id ajouté à `source_lore`, note
`[INFÉRÉ] (v1.0.85)`) : `quest_mort_du_pere` (Dys) ; `quest_plan_parsifal` (Syzygie) ;
`quest_village_fantome_feidreiva_t3` (Village Fantôme) ; `quest_automne_chateau_garundel_t3`,
`quest_visite_haazheel_chateau_t4` (fief de Garundel) ; `quest_t01_gd_tenir_les_rangs`, `quest_t01_gd_assaut_sacre`,
`quest_t01_gd_observer_le_signe` (Kendrhir) ; 9 quêtes du T00 au château (Ruines de Wismerhill) et 2 au village
(Whispers Hill). Quêtes ancrées : **87 → 106** sur 158.

Restent sans case (52) : région seule — 6 de l'Empire du Jade (extension hors carte), `quest_campagne_6mois_t4`
(campagne contre plusieurs vassaux), 4 du T00 (`butin_maigre`, `apprendre_chasse`, `joute_errante`,
`echo_invocation` : forêt ou fermes de la vallée, sans lieu précis), 4 des Marches (`defendre_cite_marches`, cité non
nommée ; `eclaireur_marches`, `voie_psychique`, `deux_lames`) ; sans lieu identifiable — 37 quêtes (lieu non nommé,
mobile ou multiple : Juggernaut, pyramide volante, Grande Plaine, Porte du T12, château loué par Haazheel, taverne,
forteresse naine abandonnée, palais non désigné, T12-T14 sans lieu dans leur fiche, etc.).

**Ce qui est réel en jeu** : les 5 POI de balise (`poi_balise_*`, `monolithe` : bannière de nom à l'approche,
aplanissement) dans des cellules générées. **Lore seulement** : avertisseurs des villes, panneaux, enceintes gardées,
légendes ; balises des cases non générées (N013_E017, N008_W015, N006_E007, N016_E019, N016_E020) ; changement de
région de Whispers Hill (le générateur ne lit pas les régions du lore) ; rattachements de raids et de quêtes. Aucun
système de niveau de zone ni de couleur de nom de zone côté jeu.

Test `world_gen_tests` « lieux v1.0.85 » (5 balises au sec dans des cellules générées ; cairn à plus de 3 km de la
grotte ; stèle à plus de 2 km de la Porte 5 ; bornes dans la poche, à moins de 500 m d'une route ; grotte toujours à
plus de 1,5 km des routes). **Services** : 1242 PNJ (inchangé). Aucun identifiant supprimé (5 lieux et 5 POI ajoutés).

**Points incertains** : noms `[GD]` des balises ; la Stèle compte-t-elle dans les POI de la région de Syllaërin
(ici non) ; la poche 1-15 de Whispers Hill n'est accessible que par des cases 30-56 ; case exacte de l'Arbre aux
Ossements.

### 16.15 Lieux et quêtes de Moork, Tsaroth et du Désert de l'Oracle (v1.0.86)

Lot 14 du propriétaire (priorité 2 validée le 12/11/2026), d'après le relevé de densité v1.0.84 (trous 1 à 4, 10,
17, 22, 25). Cible : les cases **jouables** (générées : `generated_cells` du master **et** corridors des départs,
`AddStartCorridors`) **vides** (ni ville, ni lieu, ni quête) de trois régions. Les cases de la Baronnie de Moork et
du Désert de l'Oracle viennent des corridors, celles des Terres désolées de la liste du master. Chaque case reçoit un
lieu `[GD]` avec un POI réel dans `pois` du master ; aucune nouvelle ville ni nouveau hameau, aucun identifiant
supprimé, aucune ville renommée. Scripts reproductibles : `data_v1086.py`, `lore_v1086.py` (appliqué au fichier
v1.0.85), `quests_v1086.py`, `master_v1086.py`.

**Règles respectées** : Tsaroth (`ville_tsaroth`, S009_E011) reste **sans service** et sans POI, aucun donneur de
quête n'y est placé, le camp de la Légion (départ des démons, retiré du monde principal) n'est pas recréé ; Moork
reste intacte (avant le T11) ; l'Oracle (extérieur, lieu du T2, §9) ne change pas : les lieux sont autour. Balisage
selon le mécanisme v1.0.85 (repère visible POI `monolithe`, avertisseurs aux villes voisines, légende sans révéler
l'entrée des grottes de dragon).

**Fiches de région ajoutées** (identifiants des boîtes `regions` du master, aucune case ne change de région) :
`terres_desolees` (« Terres désolées de Tsaroth ») et `desert_central` (« Désert de l'Oracle »). Repères de niveau
`[GD]` proposés, **à valider** (niveau relatif à la faction, `rule_niveaux_par_faction`) : Tsaroth 20-40 en surface
(nécropole 40-56 et cases volcaniques 55-70 balisées) ; Oracle 10-25 (comme la Via Magna) ; Moork inchangée (18-45),
avec un gradient : plaines de l'ouest 18-26, piste du sel 20-30, route de Ghrunke Dash 26-36, monts 32-55 balisés.

**Baronnie de Moork** (11 cases ; les 13 quêtes existantes étaient toutes à Moork : 5 quêtes ajoutées hors de la ville)

| Case | Lieu `[GD]` (lieu, POI, genre) | Niveaux |
|---|---|---|
| N002_E011 | **Tour de guet de la route de Shezhold** (`lieu_tour_guet_route_shadehold`, `poi_tour_guet_route_shadehold`, `camp`) | 18-24 |
| N000_E011 | **Ferme fortifiée pillée** (`lieu_ferme_fortifiee_pillee`, `poi_ferme_fortifiee_pillee`, `ruine`) | 18-22 |
| S001_E011 | **Camp des clans insoumis** (`lieu_camp_clans_insoumis`, `poi_camp_clans_insoumis`, `camp`) | 20-24 |
| S003_E011 | **Tertre des Chefs de clan** (`lieu_tertre_chefs_de_clan`, `poi_tertre_chefs_de_clan`, `sanctuaire`) | 20-26 |
| S002_E012 | **Puits de la Piste du sel** (`lieu_puits_piste_du_sel`, `poi_puits_piste_du_sel`, `camp`) | 22-28 |
| S002_E013 | **Salines de la baronnie** (`lieu_salines_baronnie`, `poi_salines_baronnie`, `camp`) | 22-30 |
| S002_E014 | **Fosses aux vers** (`lieu_fosses_aux_vers`, `poi_fosses_aux_vers`, `gouffre`) | 20-28 |
| S003_E015 | **Enclos des bêtes de guerre** (`lieu_enclos_betes_de_guerre`, `poi_enclos_betes_de_guerre`, `elevage`) | 28-36 |
| S003_E016 | **Fortin impérial en ruine** (`lieu_fortin_imperial_ruine`, `poi_fortin_imperial_ruine`, `ruine`) | 28-34 |
| S004_E016 | **Relais des convois de Ghrunke Dash** (`lieu_relais_convois_ghrunke_dash`, `poi_relais_convois_ghrunke_dash`, `camp`) | 26-32 |
| S005_E016 | **Borne des Monts de Moork** (`lieu_borne_monts_de_moork`, `poi_borne_monts_de_moork`, `monolithe`) | balise (32-55 au-delà) |

- `quest_moork_gd_clans_insoumis` « Les Clans insoumis » (niveau 18+) : town_0127 → lieu_tour_guet_route_shadehold → lieu_ferme_fortifiee_pillee → lieu_camp_clans_insoumis → lieu_tertre_chefs_de_clan → hamlet_0196
- `quest_moork_gd_piste_du_sel` « La Piste du sel » (niveau 22+) : ville_moork → lieu_salines_baronnie → lieu_puits_piste_du_sel → hamlet_0196
- `quest_moork_gd_fosses_aux_vers` « Les Fosses aux vers » (niveau 20+) : ville_moork → lieu_fosses_aux_vers
- `quest_moork_gd_fortin_des_pilleurs` « Le Fortin des pilleurs » (niveau 28+) : lieu_enclos_betes_de_guerre → lieu_fortin_imperial_ruine
- `quest_moork_gd_acier_de_ghrunke_dash` « L'Acier de Ghrunke Dash » (niveau 26+) : ville_moork → lieu_relais_convois_ghrunke_dash → lieu_borne_monts_de_moork → lieu_enclos_betes_de_guerre

**Terres désolées de Tsaroth** (15 cases ; corridor vide de l'ancien départ démon)

| Case | Lieu `[GD]` (lieu, POI, genre) | Niveaux |
|---|---|---|
| S006_E008 | **Tour de guet effondrée** (`lieu_tour_guet_effondree`, `poi_tour_guet_effondree`, `ruine`) | 20-24 |
| S006_E009 | **Puits de la Lisière** (`lieu_puits_de_la_lisiere`, `poi_puits_de_la_lisiere`, `camp`) | 20-26 |
| S007_E008 | **Ferme fortifiée abandonnée** (`lieu_ferme_fortifiee_abandonnee`, `poi_ferme_fortifiee_abandonnee`, `ruine`) | 22-26 |
| S007_E009 | **Bivouac des déserteurs** (`lieu_bivouac_des_deserteurs`, `poi_bivouac_des_deserteurs`, `camp`) | 24-30 |
| S008_E008 | **Relais de la Piste des Morts** (`lieu_relais_piste_des_morts`, `poi_relais_piste_des_morts`, `ruine`) | 24-30 |
| S008_E009 | **Mine de soufre abandonnée** (`lieu_mine_de_soufre_abandonnee`, `poi_mine_de_soufre_abandonnee`, `salle`) | 26-32 |
| S008_E011 | **Grotte aux chauves-souris** (`lieu_grotte_aux_chauves_souris`, `poi_grotte_aux_chauves_souris`, `salle`) | 30-38 |
| S009_E009 | **Statues brisées de la Voie antique** (`lieu_statues_voie_antique`, `poi_statues_voie_antique`, `ruine`) | 28-34 |
| S009_E010 | **Stèle des Deux Fois Nés** (`lieu_stele_des_deux_fois_nes`, `poi_stele_des_deux_fois_nes`, `monolithe`) | balise (40-56 au-delà) |
| S009_E012 | **Plaine des Cendres** (`lieu_plaine_des_cendres`, `poi_plaine_des_cendres`, `champ_bataille`) | 30-38 |
| S010_E011 | **Lande des Pierres dressées** (`lieu_lande_pierres_dressees`, `poi_lande_pierres_dressees`, `monolithe`) | 30-38 |
| S010_E012 | **Ravin de l'Ichor** (`lieu_ravin_de_l_ichor`, `poi_ravin_de_l_ichor`, `faille`) | 32-40 |
| S006_E015 | **Galerie naine abandonnée** (`lieu_galerie_naine_abandonnee`, `poi_galerie_naine_abandonnee`, `salle`) | 18-25 |
| S007_E015 | **Forge engloutie** (`lieu_forge_engloutie`, `poi_forge_engloutie`, `ruine`) | 55-62 |
| S008_E015 | **Cairn des Coulées** (`lieu_cairn_des_coulees`, `poi_cairn_des_coulees`, `monolithe`) | balise (55-70 au-delà) |

- `quest_tsaroth_gd_fuyards` « Les Fuyards de Tsaroth » (niveau 22+) : hamlet_0193 → lieu_tour_guet_effondree → lieu_bivouac_des_deserteurs → lieu_ferme_fortifiee_abandonnee
- `quest_tsaroth_gd_soufre` « Le Soufre de Tsaroth » (niveau 26+) : ville_moork → lieu_puits_de_la_lisiere → lieu_mine_de_soufre_abandonnee → lieu_grotte_aux_chauves_souris
- `quest_tsaroth_gd_voie_antique` « La Voie antique » (niveau 28+) : lieu_relais_piste_des_morts → lieu_deathtrap_canyon → lieu_statues_voie_antique → lieu_stele_des_deux_fois_nes
- `quest_tsaroth_gd_cendres` « Les Cendres de Tsaroth » (niveau 30+) : lieu_stele_des_deux_fois_nes → lieu_plaine_des_cendres → lieu_lande_pierres_dressees → lieu_ravin_de_l_ichor → hamlet_0193
- `quest_tsaroth_gd_galerie_des_coulees` « La Galerie des coulées » (niveau 18+) : ville_ghrunke_dash → lieu_galerie_naine_abandonnee → lieu_cairn_des_coulees

**Désert de l'Oracle** (10 cases ; 12 quêtes concentrées sur l'Oracle)

| Case | Lieu `[GD]` (lieu, POI, genre) | Niveaux |
|---|---|---|
| N000_E001 | **Campement des pèlerins** (`lieu_campement_des_pelerins`, `poi_campement_des_pelerins`, `camp`) | 10-14 |
| N000_E002 | **Cairn des Caravaniers** (`lieu_cairn_des_caravaniers`, `poi_cairn_des_caravaniers`, `monolithe`) | balise (66-70 au-delà) |
| N000_W001 | **Menhirs de la plaine de l'Oracle** (`lieu_menhirs_plaine_oracle`, `poi_menhirs_plaine_oracle`, `monolithe`) | 12-18 |
| N001_W001 | **Échoppes des faux devins** (`lieu_echoppes_faux_devins`, `poi_echoppes_faux_devins`, `camp`) | 10-16 |
| N001_W002 | **Oasis des Palmes noires** (`lieu_oasis_palmes_noires`, `poi_oasis_palmes_noires`, `etang`) | 12-18 |
| N002_W002 | **Relais des caravanes** (`lieu_relais_des_caravanes`, `poi_relais_des_caravanes`, `camp`) | 12-18 |
| N002_W003 | **Repaire des pillards du désert** (`lieu_repaire_pillards_desert`, `poi_repaire_pillards_desert`, `camp`) | 14-20 |
| N003_W004 | **Tour effondrée des mages** (`lieu_tour_effondree_des_mages`, `poi_tour_effondree_des_mages`, `ruine`) | 16-22 |
| S001_E002 | **Puits des Pèlerins** (`lieu_puits_des_pelerins`, `poi_puits_des_pelerins`, `sanctuaire`) | 10-14 |
| S001_E003 | **Tour de guet de Garundel** (`lieu_tour_guet_garundel`, `poi_tour_guet_garundel`, `ruine`) | 14-20 |

- `quest_oracle_gd_faux_devins` « Les Faux Devins » (niveau 10+) : ville_oracle_camp → lieu_echoppes_faux_devins → lieu_repaire_pillards_desert
- `quest_oracle_gd_caravane` « La Caravane de la Via Magna » (niveau 12+) : lieu_relais_des_caravanes → lieu_oasis_palmes_noires → lieu_campement_des_pelerins
- `quest_oracle_gd_menhirs` « Les Menhirs de la plaine » (niveau 14+) : sauken_glude_of_magistrya → lieu_tour_effondree_des_mages → lieu_menhirs_plaine_oracle
- `quest_oracle_gd_puits_des_pelerins` « Le Puits des Pèlerins » (niveau 10+) : ville_garundel_fief → lieu_puits_des_pelerins → lieu_tour_guet_garundel → lieu_cairn_des_caravaniers

**Sources des lieux.** Inventés `[GD]` sauf : « océan de tentes » de l'Oracle `[BD-T3]` (Campement des pèlerins) ;
échoppes de faux voyants à cinq pièces d'argent `[BD-T2 p.47]` ; tumulus de l'Oracle et ses menhirs sur la carte du
T3 `[BD-T3 pl.13]` ; plaine de Tsaroth « embrasement endigué par des flots d'ichor et de sang » `[BD-T5]` (Plaine des
Cendres, Ravin de l'Ichor) ; morts levés par la Lune Noire devant des pierres dressées `[BD-T5 p.30, page rognée,
lecture incertaine]` (Lande des Pierres dressées) ; soufre de Tsaroth, sel et Vers de Moork (économie v1.0.58,
`[BD-T6]` pour les vers) ; Greldinard maître des clans orques `[HS]` (clans insoumis : `[GD]`) ; Moork cité impériale
conquise (Q15 : Fortin impérial). Les placements sont `[INFÉRÉ]` (aucune case dans la BD).

**Balisage** (`[DÉCISION] (v1.0.86)` sur les cellules, avertisseurs sur les villes, notes sur les créatures) :

| Case | Menace | Balise | Avertisseurs |
|---|---|---|---|
| S005_E016 | géants bruns (32-44), dragon noir (45-55), au nord du départ de Durnhalde (1-10) | Borne des Monts de Moork (< 500 m de la route de Ghrunke Dash) | Durnhalde, Ghrunke Dash |
| S009_E010 | nécropole de Der Hem Shelbem (spectres, shedu, rois-momies, Prince Hors-Vivant ; 40-56) | Stèle des Deux Fois Nés (< 500 m de la route de Frhozhol) | Frhozhol, Saelvik |
| S007_E015, S008_E015 | coulées et volcan (55-70), à l'ouest de Ghrunke Dash et du Relais des convois de minerai | Cairn des Coulées (S008_E015) ; panneaux runiques (lore) en S007_E015 | Ghrunke Dash, Durnhalde |
| N002_E004 | premier-né de Xygga, dragon doré enflammé (66-70), case non générée | Cairn des Caravaniers (N000_E002, à plus de 15 km de la grotte, qu'il ne désigne pas) | Camp de l'Oracle |

**Faune** : aucune créature créée ; espèces existantes rattachées aux nouveaux lieux (`zones_chasse` et
`creature_ids`) : Vers de Moork (Fosses aux vers), bête de guerre de la Lune Noire (Enclos), deux fois nés (Plaine
des Cendres, Lande, Ravin), chauve-souris géante (Grotte), géant de feu (Forge engloutie), grandes mouches de
l'Oracle (Puits des Pèlerins), serpent (Oasis). Les adversaires humains (pillards, déserteurs, clans insoumis) sont
des PNJ hostiles génériques, sans fiche.

**Registre** (`regle_canon_registre_decisions`) : 3 entrées v1.0.86, une par région (`regions` :
`baronnie_de_moorks`, `terres_desolees`, `desert_central`).

**Ce qui est réel en jeu** : les 36 POI (bannière de nom à l'approche, aplanissement) dans des cellules générées.
**Lore seulement** : quêtes (aucun système de quête de monde ne lit ces fiches), PNJ donneurs et adversaires,
avertisseurs, panneaux de S007_E015, intérieurs (mine, grotte, fosses), eau de l'oasis (`etang` sans plan d'eau),
repères de niveau des régions (aucun niveau de zone côté jeu).

**Contrôles** : test `world_gen_tests` « lieux v1.0.86 » (36 lieux au sec, seuls sur leur case jouable, dans la boîte
de leur région, sauf S004_E016 et S005_E016, Baronnie de Moork par le lore mais hors de la boîte du master ; 4 balises `monolithe` ; Borne et Stèle à moins de 500 m d'une route ; Cairn des Coulées en case
volcanique ; Cairn des Caravaniers à plus de 15 km de la grotte, grotte toujours à plus de 1,5 km des routes ; Tsaroth
sans POI ni ville). Cellules générées : 256 (inchangé, corridors identiques). **Services** : 1242 PNJ (inchangé). Lieu renommé avant livraison : « Relais des convois d'acier »
→ « Relais des convois de Ghrunke Dash » (le mot « acier » entrait dans les noms interdits et invalidait des PNJ nains).

**Points incertains** : tous les noms `[GD]` ; repères de niveau de Tsaroth et de l'Oracle ; lecture des pierres
dressées du T5 p.30 ; quêtes ouvertes à toutes les factions alors que la baronnie est tenue par la Lune Noire
(contre-quêtes à définir) ; le Krak de Jhenner, la citadelle impériale et la Passe de Heniak `[BD-T5]` restent sans
case (non placés ici).

### 16.16 Lieux et quêtes des Gigantomorphes, de la Via Magna et de la Marche de l'Est (v1.0.87)

Lot 15 du propriétaire (12/11/2026), même méthode que le lot 14 (§16.15). Cible : les cases **jouables**
(`generated_cells` du master **et** corridors des départs, `AddStartCorridors`) **vides** (ni ville, ni lieu, ni
quête) de trois régions. Chaque case reçoit un lieu `[GD]` avec un POI réel dans `pois` du master ; aucune nouvelle
ville ni nouveau hameau, aucun identifiant supprimé, aucune ville renommée. Scripts reproductibles : `data_v1087.py`,
`lore_v1087.py` (appliqué au fichier v1.0.86), `quests_v1087.py`, `master_v1087.py`.

**Périmètre réel.** Terres des Gigantomorphes : 6 cases jouables vides, toutes autour de l'enclave de Whispers Hill
(N011_E008 et N012_E008, Terres du Nord 1-15, §16.14), qui n'est pas touchée. Via Magna : 5 cases. **Marche de
l'Est : aucune** — sa seule case générée, N007_E013 (région de départ de Grakhul), porte déjà les Ruines du fortin des
Marches ; aucun lieu n'y est ajouté, seulement 3 quêtes (lore seulement). Ouvrir des cases de la Marche au jeu
(`generated_cells`, ou corridor le long de la Via Magna par Horkher) reste à décider (décidé en v1.0.88 : §16.17).

**Repères de niveau `[GD]` proposés, à valider** (niveau relatif à la faction, `rule_niveaux_par_faction`) :
Gigantomorphes, lisière sud autour de Whispers Hill 28-36 (chaîne de Mordrek et centre 35-50, extrême nord 45-56) ;
Via Magna 10-30 détaillée (désert au nord de Sauken Glude 14-20, abords de Garundel 16-22, Harrodh – Shezhold
18-26) ; Marche de l'Est hors baronnie 15-28. Aucun contenu ajouté ne dépasse sa zone : pas de nouvelle balise ; les
Bornes des Géants (v1.0.85) restent les sorties balisées de l'enclave (1-15 → 28-36).

**Terres des Gigantomorphes** (6 cases ; les géants eux-mêmes restent au nord, d'où l'Empire les a repoussés `[HS]` :
la lisière n'en garde que les traces)

| Case | Lieu `[GD]` (lieu, POI, genre) | Niveaux |
|---|---|---|
| N010_E007 | **Coupe des bûcherons** (`lieu_coupe_des_bucherons`, `poi_coupe_des_bucherons`, `camp`) | 28-32 |
| N010_E008 | **Poste avancé abandonné** (`lieu_poste_avance_abandonne`, `poi_poste_avance_abandonne`, `ruine`) | 30-34 |
| N011_E007 | **Carrière des géants** (`lieu_carriere_des_geants`, `poi_carriere_des_geants`, `camp`) | 28-32 |
| N011_E009 | **Cercle de pierres des géants** (`lieu_cercle_pierres_des_geants`, `poi_cercle_pierres_des_geants`, `sanctuaire`) | 32-36 |
| N012_E007 | **Camp des trappeurs** (`lieu_camp_des_trappeurs`, `poi_camp_des_trappeurs`, `camp`) | 28-32 |
| N012_E009 | **Tanière des trolls** (`lieu_taniere_des_trolls`, `poi_taniere_des_trolls`, `salle`) | 32-36 |

- `quest_gigantomorphes_gd_bois_de_la_reconstruction` « Le Bois de la reconstruction » (niveau 28+) : town_0123 → lieu_coupe_des_bucherons → lieu_poste_avance_abandonne
- `quest_gigantomorphes_gd_pierre_des_geants` « La Pierre des géants » (niveau 30+) : town_0123 → lieu_carriere_des_geants → lieu_cercle_pierres_des_geants
- `quest_gigantomorphes_gd_taniere_des_trolls` « La Tanière des trolls » (niveau 32+) : town_0139 → lieu_camp_des_trappeurs → lieu_taniere_des_trolls
- `quest_gigantomorphes_gd_au_dela_des_bornes` « Au-delà des Bornes » (niveau 28+) : ville_whispers_hill → lieu_balise_borne_geants_nord → lieu_camp_des_trappeurs → lieu_carriere_des_geants

**Via Magna** (5 cases ; postes impériaux tous les 30 km, insécurité des voyageurs, légions en marche `[HS]`)

| Case | Lieu `[GD]` (lieu, POI, genre) | Niveaux |
|---|---|---|
| N004_E010 | **Poste de relais impérial** (`lieu_poste_relais_imperial`, `poi_poste_relais_imperial`, `camp`) | 18-24 |
| N004_W003 | **Camp des détrousseurs** (`lieu_camp_des_detrousseurs`, `poi_camp_des_detrousseurs`, `camp`) | 14-20 |
| N004_W004 | **Caravane ensablée** (`lieu_caravane_ensablee`, `poi_caravane_ensablee`, `ruine`) | 14-18 |
| S002_E003 | **Bivouac de la légion** (`lieu_bivouac_de_la_legion`, `poi_bivouac_de_la_legion`, `camp`) | 16-22 |
| S002_E004 | **Sanctuaire profané** (`lieu_sanctuaire_profane`, `poi_sanctuaire_profane`, `sanctuaire`) | 16-22 |

- `quest_via_magna_gd_courrier_imperial` « Le Courrier impérial » (niveau 18+) : horker → lieu_poste_relais_imperial → town_0136
- `quest_via_magna_gd_detrousseurs` « Les Détrousseurs de la piste » (niveau 14+) : sauken_glude_of_magistrya → lieu_camp_des_detrousseurs → lieu_caravane_ensablee
- `quest_via_magna_gd_legion_en_marche` « La Légion en marche » (niveau 16+) : ville_garundel_fief → lieu_bivouac_de_la_legion → lieu_sanctuaire_profane

**Marche de l'Est** (aucune case jouable vide ; quêtes en lore seulement, villes non générées)

- `quest_marche_est_gd_grain_de_la_marche` « Le Grain de la Marche » (niveau 18+) : hamlet_0201 → ville_moork
- `quest_marche_est_gd_laine_d_horkher` « La Laine d'Horkher » (niveau 18+) : city_horkher → lieu_poste_relais_imperial → horker
- `quest_marche_est_gd_registre_du_fortin` « Le Registre du fortin » (niveau 15+) : city_horkher → lieu_ruines_fortin_des_marches

**Sources des lieux.** Inventés `[GD]` sauf : géants repoussés vers le nord par l'expansion impériale `[HS]` (poste
avancé, carrière, cercle de pierres) ; bois et pierre pour la reconstruction de Kendrhir, peaux et gibier de
Stohnvar (économie v1.0.58) ; trolls et feu `[BD-A1 p.60]` ; postes impériaux, insécurité et légions de la Via
Magna `[HS]` ; grain vers Moork et laine de la Marche (économie v1.0.58). Placements `[INFÉRÉ]` (aucune case dans la
BD). Aucun nom propre inventé. Les noms ont été vérifiés contre les noms des PNJ de service (aucun mot repris).

**Règles respectées** : l'enclave de Whispers Hill garde ses 5 POI ; l'entrée ophidienne du Puits des Lianes
(N010_E010) n'est ni citée ni désignée (Cercle de pierres des géants placé au nord de N011_E009, à plus de 10 km) ;
aucune grotte de dragon dans les cases traitées ni à côté.

**Faune** : aucune créature créée ; espèces existantes rattachées (`zones_chasse`, `creature_ids`) : ogre (Coupe des
bûcherons, Cercle de pierres), troll (Tanière), loup (Camp des trappeurs), dragonnet-léopard (Poste de relais),
serpent (Caravane ensablée), renard infernal et mante géante (Sanctuaire profané). Détrousseurs et pillards : PNJ
hostiles génériques, sans fiche.

**Registre** (`regle_canon_registre_decisions`) : 3 entrées v1.0.87, une par région (`regions` :
`terres_des_gigantomorphes`, `via_magna`, `region_marche_est`).

**Ce qui est réel en jeu** : les 11 POI (bannière de nom à l'approche, aplanissement) dans des cellules générées.
**Lore seulement** : quêtes (aucun système de quête de monde ne lit ces fiches), PNJ donneurs et adversaires,
intérieurs (tanière), repères de niveau (aucun niveau de zone côté jeu), toute la Marche de l'Est (ouverte en v1.0.88, §16.17).

**Contrôles** : test `world_gen_tests` « lieux v1.0.87 » (11 lieux au sec, seuls sur leur case jouable, sans autre POI
ni ville ; boîte de région vérifiée pour N004_W003 et N004_W004, les autres cases étant rattachées par le lore hors
des boîtes du master ; enclave de Whispers Hill inchangée ; Cercle de pierres à plus de 10 km du Puits des Lianes).
Cellules générées : 256 (inchangé). **Services** : 1242 PNJ (inchangé).

**Points incertains** : tous les noms `[GD]` ; repères de niveau proposés ; quêtes ouvertes à toutes les factions ;
la Marche de l'Est reste hors du monde jouable (ouverte en v1.0.88, §16.17) ; la quête « Au-delà des Bornes » (28+) part de l'enclave 1-15.

### 16.17 Ouverture de la Marche de l'Est (v1.0.88)

Lot 16, décision du propriétaire (12/11/2026) : la Marche de l'Est est ouverte au jeu. Jusqu'ici, seule N007_E013
(Ruines du fortin des Marches, région de départ de Grakhul) était générée. Scripts reproductibles (hors dépôt) :
`select_v1088.py` (sélection des cases), `data_v1088.py`, `quests_v1088.py`, `lore_v1088.py` (appliqué au fichier
v1.0.87), `master_v1088.py`.

**Choix des cases.** L'emprise lore de `region_marche_est` compte 56 cases de terre (E013 à E020 de N002 à N007,
plus les confins E019-E020 de S003 à N001 ; la boîte `marche_de_lest` du master couvre E013-E020 × N004-N007) :
plus de 40, donc la région n'est pas ouverte en entier. Sont générées :

- les cases des **5 villes** : Horkher (`city_horkher`, N006_E014), Nykhfeld (`hamlet_0201`, N002_E015), Vlakmorh
  (`town_0131`, N004_E018), Ebhrazul (`town_0115`, N005_E020), Dhazkmer (`town_0126`, N001_E020) ;
- les cases des **routes** qui les relient entre elles et au monde déjà jouable : tracés du générateur (Horkher –
  Thokrul et Horkher – Gharzuk par N006_E013 et le fortin, Moork – Nykhfeld, Nykhfeld – Vlakmorh, Vlakmorh –
  Ebhrazul, Vlakmorh – Dhazkmer) **et** routes du lore (000120 Via Magna de Shezhold à Moork par Horkher et
  Nykhfeld, 000056 Horkher – Mhozrak, 000067 Moork – Nykhfeld, 000097 Nakhtvar – Nykhfeld, 000100, 000101,
  000102) ;
- une **case de marge** autour de chaque ville (dans la région) ;
- les **trous intérieurs** (case de la région bordée sur au moins 3 côtés par des cases jouables) : N003_E013,
  N004_E013, N004_E015, N004_E016, N005_E016, N003_E020.

Total : **45 cases** (39 dans la région, 6 voisines : N006_E013 Orkland, N004_E012 sans région au lore, N001_E012,
N001_E015, N000_E015, S001_E015 baronnie de Moork). Toutes touchent des cases jouables ; la zone est reliée à pied à
Grakhul, Shezhold, Nakhtvar, Mhozrak, Thokrul, Gharzuk et Moork. Le Fortin des Marches (N007_E013) est relié à
Horkher par N006_E013. `generated_cells` : 5 nouvelles dernières lignes (commentaire du master mis à jour).
Cellules générées : **256 → 301** (sonde : liste master + corridors, avant / après ; aucune perdue, corridors
inchangés).

Restent fermées : la forêt du nord (N007_E016 à N007_E020, N006_E016 à N006_E018), N002_E017 et N002_E018 (encoche
au nord de la baronnie) et les confins désertiques S001-S003 × E019-E020.

Cases générées : N007_E015, N006_E013, N006_E014, N006_E015, N006_E019, N006_E020, N005_E013, N005_E014, N005_E015, N005_E016, N005_E017, N005_E018, N005_E019, N005_E020, N004_E012, N004_E013, N004_E014, N004_E015, N004_E016, N004_E017, N004_E018, N004_E019, N004_E020, N003_E013, N003_E014, N003_E015, N003_E016, N003_E017, N003_E018, N003_E019, N003_E020, N002_E013, N002_E014, N002_E015, N002_E016, N002_E019, N002_E020, N001_E012, N001_E015, N001_E019, N001_E020, N000_E015, N000_E019, N000_E020, S001_E015.

**Lieux** : chaque nouvelle case vide (40 ; les 5 cases de villes ne reçoivent rien) reçoit un lieu `[GD]` avec POI
réel dans `pois` du master ; aucune nouvelle ville ni hameau, aucun identifiant supprimé, aucune ville renommée.
Repère 15-28 du lot 15 détaillé (**à valider**) : abords d'Horkher et de la Via Magna 15-22, Nykhfeld et la route
de Moork 20-26, forêt de Vlakmorh et confins d'Ebhrazul et de Dhazkmer 22-28. Aucun lieu ne dépasse 28 : pas de
nouvelle balise.

| Case | Lieu `[GD]` (lieu, POI, genre) | Niveaux |
|---|---|---|
| N004_E012 | **Barrage de péage de la Lune Noire** (`lieu_barrage_de_peage_lune_noire`, `poi_barrage_de_peage_lune_noire`, `camp`) | 16-20 |
| N005_E013 | **Relais incendié de la Via Magna** (`lieu_relais_incendie_via_magna`, `poi_relais_incendie_via_magna`, `ruine`) | 15-18 |
| N006_E013 | **Camp de troc des orcs** (`lieu_camp_de_troc_des_orcs`, `poi_camp_de_troc_des_orcs`, `camp`) | 15-18 |
| N007_E015 | **Cabanes des charbonniers** (`lieu_cabanes_des_charbonniers`, `poi_cabanes_des_charbonniers`, `camp`) | 16-20 |
| N006_E015 | **Bergerie fortifiée** (`lieu_bergerie_fortifiee`, `poi_bergerie_fortifiee`, `elevage`) | 15-18 |
| N005_E014 | **Moulin à vent pillé** (`lieu_moulin_a_vent_pille`, `poi_moulin_a_vent_pille`, `ruine`) | 16-20 |
| N005_E015 | **Repaire des braconniers** (`lieu_repaire_des_braconniers`, `poi_repaire_des_braconniers`, `camp`) | 17-21 |
| N004_E013 | **Champ de bataille de la conquête** (`lieu_champ_de_bataille_de_la_conquete`, `poi_champ_de_bataille_de_la_conquete`, `champ_bataille`) | 18-22 |
| N003_E013 | **Tertre des légionnaires** (`lieu_tertre_des_legionnaires`, `poi_tertre_des_legionnaires`, `sanctuaire`) | 18-22 |
| N004_E014 | **Grenier à blé réquisitionné** (`lieu_grenier_a_ble_requisitionne`, `poi_grenier_a_ble_requisitionne`, `camp`) | 18-22 |
| N003_E014 | **Poste de garde de la Lune Noire** (`lieu_poste_de_garde_lune_noire`, `poi_poste_de_garde_lune_noire`, `camp`) | 20-24 |
| N004_E015 | **Chapelle impériale désaffectée** (`lieu_chapelle_imperiale_desaffectee`, `poi_chapelle_imperiale_desaffectee`, `sanctuaire`) | 18-22 |
| N003_E015 | **Grange incendiée** (`lieu_grange_incendiee`, `poi_grange_incendiee`, `ruine`) | 20-24 |
| N002_E013 | **Bivouac des colporteurs** (`lieu_bivouac_des_colporteurs`, `poi_bivouac_des_colporteurs`, `camp`) | 20-24 |
| N002_E014 | **Enclos à bétail** (`lieu_enclos_a_betail`, `poi_enclos_a_betail`, `elevage`) | 20-24 |
| N001_E012 | **Campement des pêcheurs** (`lieu_campement_des_pecheurs`, `poi_campement_des_pecheurs`, `camp`) | 20-24 |
| N001_E015 | **Relais de la route de Moork** (`lieu_relais_de_la_route_de_moork`, `poi_relais_de_la_route_de_moork`, `camp`) | 22-26 |
| N000_E015 | **Tour à signaux de la baronnie** (`lieu_tour_a_signaux_de_la_baronnie`, `poi_tour_a_signaux_de_la_baronnie`, `ruine`) | 22-26 |
| S001_E015 | **Cimetière des captifs** (`lieu_cimetiere_des_captifs`, `poi_cimetiere_des_captifs`, `sanctuaire`) | 22-26 |
| N002_E016 | **Croix de carrefour profanée** (`lieu_croix_de_carrefour_profanee`, `poi_croix_de_carrefour_profanee`, `sanctuaire`) | 22-26 |
| N003_E016 | **Haras de la Lune Noire** (`lieu_haras_de_la_lune_noire`, `poi_haras_de_la_lune_noire`, `elevage`) | 22-26 |
| N003_E017 | **Camp des réfugiés** (`lieu_camp_des_refugies`, `poi_camp_des_refugies`, `camp`) | 22-26 |
| N003_E018 | **Tumulus de la plaine** (`lieu_tumulus_de_la_plaine`, `poi_tumulus_de_la_plaine`, `sanctuaire`) | 24-28 |
| N004_E016 | **Scierie abandonnée** (`lieu_scierie_abandonnee`, `poi_scierie_abandonnee`, `ruine`) | 22-26 |
| N004_E017 | **Tour de guet de la lisière** (`lieu_tour_de_guet_de_la_lisiere`, `poi_tour_de_guet_de_la_lisiere`, `ruine`) | 24-28 |
| N005_E016 | **Tanière du loup géant** (`lieu_taniere_du_loup_geant`, `poi_taniere_du_loup_geant`, `salle`) | 24-28 |
| N005_E017 | **Autel sylvestre de la Lune Noire** (`lieu_autel_sylvestre_lune_noire`, `poi_autel_sylvestre_lune_noire`, `sanctuaire`) | 24-28 |
| N005_E018 | **Repaire des trolls de la forêt** (`lieu_repaire_des_trolls_de_la_foret`, `poi_repaire_des_trolls_de_la_foret`, `salle`) | 26-28 |
| N004_E019 | **Camp de la patrouille noire** (`lieu_camp_de_la_patrouille_noire`, `poi_camp_de_la_patrouille_noire`, `camp`) | 24-28 |
| N005_E019 | **Ermitage en ruine** (`lieu_ermitage_en_ruine`, `poi_ermitage_en_ruine`, `ruine`) | 24-28 |
| N006_E019 | **Mine de fer abandonnée** (`lieu_mine_de_fer_abandonnee`, `poi_mine_de_fer_abandonnee`, `salle`) | 24-28 |
| N006_E020 | **Poste-frontière du nord** (`lieu_poste_frontiere_du_nord`, `poi_poste_frontiere_du_nord`, `camp`) | 24-28 |
| N004_E020 | **Domaine seigneurial confisqué** (`lieu_domaine_seigneurial_confisque`, `poi_domaine_seigneurial_confisque`, `ruine`) | 24-28 |
| N003_E019 | **Fosse aux ossements** (`lieu_fosse_aux_ossements`, `poi_fosse_aux_ossements`, `gouffre`) | 24-28 |
| N003_E020 | **Camp de chasse orque** (`lieu_camp_de_chasse_orque`, `poi_camp_de_chasse_orque`, `camp`) | 24-28 |
| N002_E019 | **Menhir du carrefour** (`lieu_menhir_du_carrefour`, `poi_menhir_du_carrefour`, `monolithe`) | 24-28 |
| N002_E020 | **Arène de lutte des orcs** (`lieu_arene_de_lutte_des_orcs`, `poi_arene_de_lutte_des_orcs`, `camp`) | 24-28 |
| N001_E019 | **Enclos des sangliers de guerre** (`lieu_enclos_des_sangliers_de_guerre`, `poi_enclos_des_sangliers_de_guerre`, `elevage`) | 24-28 |
| N000_E019 | **Tour ensablée** (`lieu_tour_ensablee`, `poi_tour_ensablee`, `ruine`) | 26-28 |
| N000_E020 | **Camp des nomades** (`lieu_camp_des_nomades`, `poi_camp_des_nomades`, `camp`) | 26-28 |

**Quêtes.** Les 3 quêtes v1.0.87 deviennent jouables (note `[DÉCISION] (v1.0.88)`) : « Le Grain de la Marche »
(Nykhfeld → Moork, route du générateur dans des cases générées), « La Laine d'Horkher » (Horkher → Poste de relais
impérial → Harrodh, par Thokrul et Mhozrak), « Le Registre du fortin » (Horkher → Ruines du fortin des
Marches). Donneurs : Horkher et Nykhfeld, désormais jouables. 2 quêtes `[GD]` ajoutées :

- `quest_marche_est_gd_peage_de_la_via_magna` « Le Péage de la Via Magna » (niveau 16+) : city_horkher → lieu_barrage_de_peage_lune_noire → lieu_relais_incendie_via_magna
- `quest_marche_est_gd_trolls_de_la_foret` « Les Trolls de la forêt » (niveau 26+) : town_0131 → lieu_scierie_abandonnee → lieu_taniere_du_loup_geant → lieu_repaire_des_trolls_de_la_foret

**Sources.** Inventés `[GD]` sauf : conquête de la Marche par la Lune Noire avant le T1 et règne de Greldinard
`[BD-A5 p.38-44]` (péage, poste de garde, champ de bataille, chapelle désaffectée, domaine confisqué) ; Horker
« alors marche de l'Empire » `[BD-THS p.23]` ; blé, bétail, laine, grain vers Moork, importation d'armes (économie
v1.0.58) ; trolls et feu `[BD-A1 p.60]`. Placements `[INFÉRÉ]`. Aucun nom propre inventé ; noms vérifiés contre les
PNJ de service (« Pierre levée du carrefour » renommé « Menhir du carrefour » avant livraison : « pierre » est un
prénom de PNJ). Le flot de barbares du T14 (`raid_marches_est_t14`) n'est pas utilisé (après le T11).

**Faune** : aucune créature créée ; espèces existantes rattachées (`zones_chasse`, `creature_ids`) : loup,
dragonnet-léopard, deux fois nés (champ de bataille, cimetière), corbeau, mammouth de trait, loup géant de guerre,
troll, ogre, sanglier de guerre.

**Règles respectées** : aucune grotte de dragon ni entrée ophidienne dans les cases ouvertes ou voisines (rien
ajouté, rien désigné) ; services inchangés (1242 PNJ).

**Registre** (`regle_canon_registre_decisions`) : 1 entrée v1.0.88 « Ouverture de la Marche de l'Est ».

**Ce qui est réel en jeu** : les 45 cases et leurs villes, les 40 POI, les routes du générateur entre les villes de
la Marche (6 tracés entièrement dans des cases jouables). **Ce qui ne l'est pas** :

- le générateur ne trace pas la **Via Magna** du lore entre Shezhold et Horkher ni entre Horkher et Nykhfeld
  (son réseau relie Horkher à Thokrul et Gharzuk, Nykhfeld à Moork et Vlakmorh) : on y marche à travers les
  plaines des cases ouvertes ; les lieux « sur la Via Magna » sont au bord de son tracé lore ;
- routes sortant de la zone vers des villes hors région, coupées au mur invisible : Ebhrazul – Rhivkhar (000104,
  par N007_E019/N007_E020), Ebhrazul – Ryvaldh (000160) et Ebhrazul – Grhavelt (000161) par E021, Dhazkmer –
  Dhuzvelt (000162) et Dhazkmer – Grhavelt par E021 (ouvertes en v1.0.91, §16.20) ;
- quêtes, PNJ donneurs et adversaires, intérieurs, repères de niveau (lore seulement, comme aux lots 14-15).

**Contrôles** : test `world_gen_tests` « cellules et lieux v1.0.88 » (45 cases générées et terrestres, aucune
isolée, toutes atteintes à pied depuis Grakhul ; 5 villes jouables ; 6 routes de la Marche entièrement dans des
cases jouables ; 40 lieux au sec, seuls sur leur case ; aucun POI sur les cases de villes ; fortin inchangé).
Génération de cases de la Marche avec le générateur sans erreur (voir le rapport du lot).

**Points incertains** : tous les noms `[GD]` ; repère 15-28 et son gradient ; marge d'une case (16 cases) et trous
intérieurs ; ouvrir aussi la forêt du nord, les confins du sud-est et les routes vers Rhivkhar, Ryvaldh,
Grhavelt et Dhuzvelt ; tracer la Via Magna dans le générateur ; quêtes ouvertes à toutes les factions alors que la
Marche est tenue par la Lune Noire.

### 16.18 Quêtes et niveaux du Royaume des nains de l'est (v1.0.89)

Lot 17 du propriétaire (12/11/2026), même méthode que les lots 14 et 15 (§16.15, §16.16). Cible : les cases
**jouables** (`generated_cells` du master **et** corridors des départs, `AddStartCorridors`) **vides** du Royaume des
nains de l'est (85 % de cases vides au relevé v1.0.84). Aucune case ouverte (`generated_cells` inchangé), aucune
ville, aucun identifiant supprimé, aucune ville renommée. Scripts reproductibles (hors dépôt) : `select_v1089.py`
(sonde : liste master + corridors), `quests_v1089.py`, `lore_v1089.py` (appliqué au fichier v1.0.88).

**Périmètre réel : aucune case jouable vide.** La région compte 95 cases de terre au lore (`region_id`) et la boîte
`royaume_des_nains_de_lest` du master en couvre 129 (E015-E024 × S016-S004) ; seules 9 cases de la région sont
jouables — le départ 3×3 de Durnhalde (§15.6), Ghrunke Dash comprise — et toutes portent déjà une ville, un hameau ou
un lieu (Durnhalde, Kohlbrand, Eisenkehl, Ghrunke Dash, Autel des Ancêtres, Puits des Apprentis, Galeries effondrées
de la Vieille Veine, Camp des prospecteurs, Relais des convois de minerai). Les 5 autres cases jouables de la boîte
(Monts de Moork, Relais des convois de Ghrunke Dash, Galerie naine abandonnée, coulées de l'ouest) sont rattachées à
d'autres régions et occupées depuis la v1.0.86. Comme pour la Marche de l'Est au lot 15 : **aucun lieu ni POI
ajouté**, le master n'est pas modifié ; le reste de la région (Vozmarkh, Mhordaal, Ghrondak, montagnes de l'est et
du sud) reste hors du monde jouable. L'ouvrir est une décision à prendre (comme en v1.0.88 pour la Marche).

**Quêtes** (5, `[GD]`, région de départ, toutes les étapes dans des cases générées ; aucun système de quête de monde
ne les lit encore). Elles font sortir les joueurs des trois hameaux du départ vers ses lieux, qui n'avaient aucune
quête, et vers Ghrunke Dash :

- `quest_nains_est_gd_premiere_descente` « La Première Descente » (niveau 1+, solo) : hameau_durnhalde → lieu_puits_des_apprentis
- `quest_nains_est_gd_premier_outil` « Le Premier Outil » (niveau 2+, solo) : hameau_durnhalde → lieu_autel_des_ancetres
- `quest_nains_est_gd_convoi_de_kohlbrand` « Le Convoi de Kohlbrand » (niveau 3+, solo) : hamlet_kohlbrand → lieu_relais_des_convois → ville_ghrunke_dash
- `quest_nains_est_gd_vieille_veine` « La Vieille Veine » (niveau 5+, group) : hamlet_eisenkehl → lieu_galeries_effondrees_vieille_veine
- `quest_nains_est_gd_filon_des_prospecteurs` « Le Filon des prospecteurs » (niveau 7+, solo) : hameau_durnhalde → lieu_camp_des_prospecteurs → ville_ghrunke_dash

**Niveaux proposés, à valider** (niveau relatif à la faction, `rule_niveaux_par_faction`). Dans le départ 1-10 :
Durnhalde, Autel des Ancêtres et Puits des Apprentis 1-4 ; Kohlbrand et Relais des convois de minerai 3-6 ; Eisenkehl
et Galeries effondrées de la Vieille Veine 5-8 ; Camp des prospecteurs 7-10 (champ `ambiance.niveau_menace` des 5
lieux, vide jusqu'ici). Au-delà, pour l'ouverture future : abords de Vozmarkh et routes d'Eisenkehl, Mhordaal et
Shaddrek 10-20 ; montagnes de l'est vers Ghrondak et sud de la région 18-30. Les zones plus fortes au contact du
départ restent celles balisées en v1.0.86 (Monts de Moork au nord, 32-55 ; coulées et volcan à l'ouest, 55-70) ;
aucune balise ajoutée (aucun contenu plus fort ajouté).

**Sources.** Lieux et hameaux du départ `[GD]` (v1.0.69) ; première taille des apprentis et mines d'apprentissage de
Durnhalde (v1.0.61) ; outil déposé à l'Autel et marteau frappé contre le sol (« nés du métal, du feu et de la lave »)
`[BD-T14 p.39]` ; charbon de Ghrunke Dash `[BD-T9 p.12]` ; mercenaires nains et paiement en gemmes `[BD-T3 pl.17-18]`.
Aucun nom propre inventé ; aucun lieu nommé, donc aucun mot ajouté à la liste des noms interdits des PNJ de service.

**Règles respectées** : aucune grotte de dragon ni entrée ophidienne n'est citée, désignée ou approchée (aucune
étape hors du départ 3×3 et de Ghrunke Dash, rien ajouté) ; Grand Hall des nains et cratère inchangés (rien ajouté
dans la zone 55-70).

**Faune** : aucune créature créée ni rattachée (rats, araignées, corbeaux, mulets et vautours sont déjà dans les
`creature_ids` des cases du départ depuis la v1.0.69).

**Registre** (`regle_canon_registre_decisions`) : 1 entrée v1.0.89 (`regions` : `royaume_des_nains_de_lest`).

**Ce qui est réel en jeu** : rien de nouveau (les 9 cases et leurs POI le sont depuis la v1.0.69). **Lore
seulement** : les 5 quêtes, leurs donneurs et les niveaux proposés.

**Contrôles** : test `world_gen_tests` « lieux v1.0.89 » (14 cases jouables dans la boîte du Royaume des nains de
l'est, aucune ouverte ; chacune porte une ville ou un POI ; les 5 lieux des quêtes dans leurs cases jouables).
Cellules générées : 301 (inchangé). **Services** : 1242 PNJ (inchangé).

**Points incertains** : noms et étapes des 5 quêtes ; gradient proposé ; quêtes de départ ouvertes aux autres
factions ; ouvrir une partie du royaume (Vozmarkh, route Eisenkehl – Vozmarkh) pour donner une suite 10-20 au départ
(route ouverte en v1.0.91, §16.20).

### 16.19 Régions et niveaux du sud-centre et des Confins du Midi (v1.0.90)

Lot 18 du propriétaire (priorité 3 validée le 12/11/2026), d'après le relevé de densité v1.0.84 (§1 et §5) : **222 cases
terrestres** n'avaient aucune région (ni `region_id` du lore ni boîte `regions` du master) ni niveau, surtout au
sud-centre (S003-S014 × W001-E014). Chacune est rattachée à une région ; aucune ville ni aucun lieu déplacé, aucune
ville renommée, aucune case ouverte (`generated_cells` inchangé), aucun identifiant supprimé. Scripts reproductibles
(hors dépôt) : `base_v1090.py` (grille, régions du lore, boîtes du master), `assign_v1090.py` (règles de
rattachement), `lore_v1090.py` (appliqué au fichier v1.0.89), `boxes_v1090.py` (étude des boîtes du master),
`carte_v1090.py` (rendu).

**Découpage selon le relief.** Le bloc central est fermé à l'ouest par les rocs du Royaume Drach, à l'est par la
chaîne du désert (M, E006-E007, de S009 à la côte) et le désert de Tsaroth, au nord par la Via Magna et Moork. Trois
milieux s'y distinguent : la forêt et le marais du T3 au nord, les plaines et la forêt de Melvhord à l'ouest de la
chaîne, le désert à l'est de la chaîne au sud de Tsaroth. D'où trois nouvelles régions `[GD]` ; les marges (côte sud
de Lhynn, bords de la Via Magna, côtes) vont aux régions existantes voisines.

**Nouvelles régions** (fiches `regions`, identifiants de même forme que les boîtes du master ; noms à valider) :

| Région | Cases | Terrain | Niveau de référence | Factions | Lien avec le lore |
|---|---:|---|---|---|---|
| **Forêt des Lutins** (`foret_des_lutins`) | 34 | Forêt et lisières (S001-S004, E002-E011), marais du Village fantôme (S003_E006), plaines de Mhillenz et de Frhozhol (S005-S006) | **14-24** : lisière nord-ouest (Mhillenz, Garundel, Via Magna) 14-18 ; cœur de la forêt 16-22 ; marais du Village fantôme 18-24 ; plaine de Frhozhol 20-24 | Neutres (Mhillenz, Frhozhol, Village fantôme) ; Empire et Lune Noire de passage | Nom `[BD-T3]` (forêt des lutins, `zone_cursed_forest`, `quest_foret_lutins_t3`, Iaun Reith) ; trajet du T3 Oracle → Ghrunke Dash → forêt → village → fief de Garundel ; étendue `[INFÉRÉ]` ; Frhozhol (« Le Souffle du volcan ») ; Porte 4 des enfers |
| **Val de Suthvarn** (`val_de_southport`) | 59 | Plaines de Kholdvar (S007-S009), forêt de Melvhord (S010-S013, E000-E004), plaines de Khlaybur et de Suthvarn, rivière de Suthvarn ; monts de l'ouest (S007-S009, E000-E001) au contact du Drach, chaîne du désert à l'est | **18-28** : Kholdvar et routes de Frhozhol et d'Heureux 18-22 ; forêt de Melvhord 20-26 ; Khlaybur, Suthvarn et la côte 22-28 ; Porte 1 et contreforts du Drach (30-56) à baliser | Neutres ; Nains du sud (forteresse d'Heureux au nord) ; Chevaliers-Dragons (frontière gardée du Drach ; suite naturelle après le départ de Drakhenhal) | Forteresse naine d'Heureux `[BD-T9, BD-T11]` ; Royaume Drach interdit `[BD-T2]` ; rivière de Suthvarn ; Porte 1 des enfers |
| **Désert des Sels** (`desert_des_sels`) | 24 | Désert (S012-S014, E008-E012) prolongeant celui de Tsaroth, chaîne du désert (E007), plaines et lac de Klehrwath (S013-S014, E013-E014) | **24-36** : Saelvik et le lac 24-28 ; erg central 26-32 ; marge nord vers la Plaine des Cendres et Der Hem Shelbem 30-36 (nécropole 40-56 déjà balisée) | Neutres (Saelvik) ; Nains de l'est (Klehrwath, sel) ; Mort-vivants de Der Hem Shelbem au nord | Tsaroth et Der Hem Shelbem `[BD-T5]` ; sel gemme et sable à verre de Saelvik (v1.0.58) ; avertisseur de Saelvik (v1.0.86) |

Ambiances (fiches) : conte inquiétant de la forêt (lutins facétieux qui n'attaquent pas un groupe fort, brume et
arbres morts du marais le reste du siècle `[BD-T3 p.26-27, pl.25-30]`) ; campagne reculée du Val, loin de l'Empire,
sous les cimes interdites du Drach ; chaleur, mirages, croûtes de sel et caravanes du Désert des Sels, à l'ombre de
la nécropole.

**Cases rattachées à des régions existantes** (105 ; note `[INFÉRÉ] (v1.0.90)` sur chaque case et sur la fiche) :

| Région | Cases | Où | Repère |
|---|---:|---|---|
| Terres de l'Empereur | 48 | Côte sud de Lhynn (S001-S009, W016-W009) : Dhuzmar, Klaerhin, Aeskhol, Ghrepol (hameaux impériaux), Port Franc, forêt de Harndrecht, Porte 3 des enfers | 12-22 (côte près de Lhynn 12-16, Harndrecht 14-20, frontière d'Hishtarland 16-22) |
| Via Magna | 17 | Désert et lisières de N004-N000 (E007-E010), entre Harrodh, Shezhold et Moork, et N004_E012 (Barrage de péage) | 18-26 (tronçon Harrodh – Shezhold) |
| Royaume des nains de l'est | 21 | Côte orientale (E025, S004-S014), pointe sud des montagnes (S015-S016, E014), côte sud (S017, E014-E021) | 18-30 (sud de la région) |
| Terres désolées de Tsaroth | 6 | Colonne E007 (S006-S011) : plaine, chaîne et désert au bord du champ de bataille | 20-26 (abords de Frhozhol) |
| Marche de l'Est | 5 | Lisière nord de la forêt de Vlakmorh autour de Rhivkhar (N008, E016-E020) | 22-28 |
| Terres des Gigantomorphes | 3 | Côte de N010 (W001-E001) | lisière sud 28-36 |
| Syzygie, 1ère Province de l'Empire | 2 + 2 | Côtes de N012 (W009-W008) et de N006-N005 (W013) | 10-30 |
| Forêt elfique | 1 | Île de Dyprokh (N012_E002), jumelle de N012_E003 déjà dans la boîte `foret_elfique` | 10-30 |

Les villes et lieux de ces cases qui n'avaient pas de région reçoivent celle de leur case (`region_id`, note
`[INFÉRÉ] (v1.0.90)`) : 14 villes (dont Kholdvar, Suthvarn, Saelvik, Khlaybur, Mhillenz, Frhozhol, le Village
fantôme, Rhivkhar, Dyprokh, Thysskar, les quatre hameaux de la côte sud de Lhynn) et 4 lieux (Portes 1, 3 et
4 des enfers, Port Franc). Les régions déjà renseignées ne changent pas (Harrodh reste `via_magna`).

**Confins du Midi** (`confins_du_midi`). Boîte du master depuis la v1.0.52 (W004-E013 × S017-S015, 48 cases de terre,
§5.1 : « Plaines, chaîne du désert prolongée, terres arides, côte sud »), sans fiche, lieu, quête ni niveau. Fiche
ajoutée sous l'identifiant de la boîte et 48 cases rattachées. Identité `[GD]` : terres du bout du monde au sud du
continent — plaines agricoles et pâtures de Faervosk, pointe sud de la chaîne du désert, terres arides de Thistle
Creek au pied des montagnes naines, côte et villages de pêcheurs où les nouvelles de l'Empire arrivent avec des mois
de retard ; neutres, Nains de l'est au-delà des montagnes, cabotage d'Hishtarland. **Niveau de référence 22-34** :
ouest (Faervosk, plaines et côte, au sud du Val de Suthvarn) 22-28 ; est (Thysskar, terres arides, au sud du
Désert des Sels) 28-34. Cases non jouables : identité et niveau seulement, aucun lieu ni quête.

**Progression du sud-centre** (repères absolus de `regle_bestiaire_echelles` ; le niveau d'une zone reste relatif à
la faction, `rule_niveaux_par_faction`, et aucune faction ne démarre ici : départs les plus proches Drakhenhal à
l'ouest, Durnhalde à l'est) : Désert de l'Oracle et Via Magna (10-25) → **Forêt des Lutins 14-24** → **Val de
Suthvarn 18-28** (ouest, vers le Royaume Drach 30-56) et Terres désolées de Tsaroth 20-40 (est) → **Désert des Sels
24-36** → **Confins du Midi 22-34** ; à l'est, Baronnie de Moork 18-45 et sud du Royaume des nains de l'est 18-30.
Plus forts, déjà balisés : Der Hem Shelbem 40-56, volcan 55-70. Contenu plus fort que le repère dans ces régions
(faune du Village fantôme jusqu'à 30, abords des Portes 1, 3 et 4, contreforts du Drach) : balisage selon les formes
v1.0.85, à poser quand les cases seront remplies ; aucune balise ajoutée ici.

**Master inchangé.** Les régions du master sont des boîtes rectangulaires (`e_min`…`n_max`) ; le client retient la
plus petite boîte qui contient le joueur, écrit le nom de **chaque** boîte au centre de la carte (vue d'ensemble) et
l'annuaire MJ du shard en fait une entrée par boîte. Étude (`boxes_v1090.py`, boîtes qui ne changent la région affichée
d'aucune case déjà couverte) : le Désert des Sels tient dans une boîte (E007-E014 × S014-S012), mais il en faudrait 2
au Val de Suthvarn, **8** à la Forêt des Lutins et 13 pour les extensions des régions existantes, soit 21 boîtes et
autant d'étiquettes répétées sur la carte et d'entrées en double dans l'annuaire MJ. Le format ne le permet donc pas
proprement : rattachement **dans le lore seulement**, et pas de test « régions v1.0.90 » (rien à tester côté master).
Une liste de cases par région (ou une étiquette unique par identifiant de région) dans le master est une évolution à
décider.

**Règles respectées** : aucune grotte de dragon ni entrée ophidienne citée ou approchée (les monts de l'ouest du Val
s'arrêtent à S007) ; Tsaroth toujours sans service ; aucune créature créée ni rattachée.

**Registre** (`regle_canon_registre_decisions`) : 4 entrées v1.0.90 (`regions` : `foret_des_lutins`,
`val_de_southport`, `desert_des_sels`, `confins_du_midi`). Notes `[GD] (v1.0.90)` sur `regle_bestiaire_echelles`
(repères) et `rule_niveaux_par_faction`.

**Ce qui est réel en jeu** : rien de nouveau (26 des 222 cases étaient déjà générées, sans changement). **Lore
seulement** : régions, rattachements, niveaux.

**Contrôles** : aucune case terrestre sans région (0 sur 1259, contre 222) ; tests `world_gen_tests`,
`world_places_tests`, `start_locations_tests` inchangés et verts ; **Services** : 1242 PNJ (inchangé).

**Points à valider** : noms des trois régions ; limites (S001-S002 de la forêt, colonne E007 donnée à Tsaroth, côte
sud de Lhynn aux Terres de l'Empereur plutôt qu'une région propre, côtes S015-S017 de l'est aux nains) ; repères de
niveau ; Faervosk (S015_E001) garde `region_id = royaume_drach` (v1.0.52) alors que sa case est aux Confins ; forme
future des régions dans le master ; niveau des abords des Portes des enfers du continent.

### 16.20 Ouverture d'Eisenkehl – Vozmarkh et des routes de la Marche (v1.0.91)

Lot 19, décisions du propriétaire (12/11/2026) : ouvrir au jeu (A) la route Eisenkehl → Vozmarkh, suite 10-20 du
départ nain (mur invisible à la sortie du départ de Durnhalde jusqu'ici) ; (B) les 4 routes de la Marche de l'Est
coupées au mur invisible en v1.0.88 : Ebhrazul → Rhivkhar (000104), Ebhrazul → Ryvaldh (000160), Ebhrazul →
Grhavelt (000161), Dhazkmer → Dhuzvelt (000162). Même méthode qu'au lot 16 (§16.17). Scripts reproductibles (hors
dépôt) : `select_v1091.py` (sélection), `pick_v1091.py` (placement des POI d'après la sonde du générateur),
`data_v1091.py`, `quests_v1091.py`, `lore_v1091.py` (appliqué au fichier v1.0.90), `master_v1091.py`,
`carte_v1091.py` (rendu).

**Choix des cases.** Pour chaque route : cases de son tracé lore (waypoints ; Eisenkehl – Vozmarkh n'a pas de route au
lore : ligne droite), cases du tracé du générateur entre les mêmes villes, case de la ville d'arrivée, une case de
marge (terre) autour de chaque ville devenue jouable, puis trous intérieurs (case bordée sur au moins 3 côtés par des
cases jouables, itéré). Au nord de Vozmarkh, S008_E020 et S008_E021 (montagne, hors tracé) restent fermées.
Total : **57 cases** (`generated_cells`, 7 nouvelles dernières lignes, commentaire du master mis à jour), toutes
terrestres, aucune isolée. Cellules jouables (liste master + corridors) : **301 → 358**, aucune perdue.

| Route | Cases propres à la route (tracés lore + générateur) | Arrivée |
|---|---|---|
| A. Eisenkehl → Vozmarkh | S008_E019, S009_E019 (col) | Vozmarkh (`town_0125`, S009_E020) + marge S009_E021, S010_E019, S010_E020, S010_E021 : **7 cases** |
| B1. Ebhrazul → Rhivkhar | N007_E019, N007_E020, N008_E019 | Rhivkhar (`town_0132`, N008_E018) + marge N007_E017, N007_E018, N008_E017, N009_E017 à N009_E019 |
| B2. Ebhrazul → Ryvaldh | N004_E021, N005_E021, N005_E022, N006_E021, N006_E022 | Ryvaldh (`town_riverfold`, N006_E023) + marge N005_E023, N005_E024, N006_E024, N007_E022 à N007_E024 |
| B3. Ebhrazul → Grhavelt | N003_E022 (N004_E021 partagée avec B2) | Grhavelt (`town_greyfield`, N003_E023) + marge N002_E022 à N002_E024, N003_E024, N004_E022 à N004_E024 |
| B4. Dhazkmer → Dhuzvelt | N000_E021, N000_E022, N001_E021, S001_E022 | Dhuzvelt (`hamlet_dustfield`, S001_E023) + marge N000_E023, N000_E024, S001_E024, S002_E022 à S002_E024 |
| Trous intérieurs (B) | N006_E017, N006_E018 (forêt du nord de Vlakmorh), N007_E021, N003_E021, N002_E021, N001_E022 à N001_E024 | — |

Soit 50 cases pour (B) : 12 dans la Marche et ses abords (dont N009_E017 à N009_E019, dans la boîte Marches du Nord
du master, sans région au lore), 38 sur la côte orientale (boîte `rivage_des_geants`, région lore Terres des
Gigantomorphes). Le Dhazkmer – Grhavelt, le Ryvaldh – Grhavelt et le Grhavelt – Dhuzvelt du générateur sont aussi
entièrement dans des cases jouables. **Villes devenues jouables** : Vozmarkh, Rhivkhar, Ryvaldh, Grhavelt,
Dhuzvelt.

Cases générées : N009_E017, N009_E018, N009_E019, N008_E017, N008_E018, N008_E019, N007_E017, N007_E018, N007_E019, N007_E020, N007_E021, N007_E022, N007_E023, N007_E024, N006_E017, N006_E018, N006_E021, N006_E022, N006_E023, N006_E024, N005_E021, N005_E022, N005_E023, N005_E024, N004_E021, N004_E022, N004_E023, N004_E024, N003_E021, N003_E022, N003_E023, N003_E024, N002_E021, N002_E022, N002_E023, N002_E024, N001_E021, N001_E022, N001_E023, N001_E024, N000_E021, N000_E022, N000_E023, N000_E024, S001_E022, S001_E023, S001_E024, S002_E022, S002_E023, S002_E024, S008_E019, S009_E019, S009_E020, S009_E021, S010_E019, S010_E020, S010_E021.

**Lieux** : chaque nouvelle case vide (52 ; les 5 cases de villes ne reçoivent rien) reçoit un lieu `[GD]` avec POI
réel dans `pois` du master ; aucune nouvelle ville ni hameau, aucun identifiant supprimé, aucune ville renommée.
Repères **à valider** : route Eisenkehl – Vozmarkh et abords de Vozmarkh 10-20 (repère v1.0.89 ; col 10-15,
Vozmarkh et sa marge 14-20) ; abords de Rhivkhar et forêt du nord de Vlakmorh 22-28 (repère v1.0.90) ; côte
orientale de Ryvaldh à Dhuzvelt (sud du Rivage des Géants) **24-30** (routes au départ d'Ebhrazul et de Dhazkmer
24-28, côte et abords des ports 26-30, bord du désert 27-30), sous la lisière sud des Gigantomorphes (28-36). Aucun
lieu ne dépasse le repère de sa zone : pas de nouvelle balise.

| Case | Lieu `[GD]` (lieu, POI, genre) | Niveaux |
|---|---|---|
| S008_E019 | **Halte des muletiers du col** (`lieu_halte_des_muletiers_du_col`, `poi_halte_des_muletiers_du_col`, `camp`) | 10-13 |
| S009_E019 | **Borne-frontière renversée** (`lieu_borne_frontiere_renversee`, `poi_borne_frontiere_renversee`, `monolithe`) | 12-15 |
| S009_E021 | **Carrière des forçats** (`lieu_carriere_des_forcats`, `poi_carriere_des_forcats`, `camp`) | 14-18 |
| S010_E019 | **Ferme des chevriers nains** (`lieu_ferme_des_chevriers_nains`, `poi_ferme_des_chevriers_nains`, `elevage`) | 15-18 |
| S010_E020 | **Corps de garde de la route du sud** (`lieu_corps_de_garde_de_la_route_du_sud`, `poi_corps_de_garde_de_la_route_du_sud`, `camp`) | 16-20 |
| S010_E021 | **Puits de mine inondé** (`lieu_puits_de_mine_inonde`, `poi_puits_de_mine_inonde`, `salle`) | 17-20 |
| N009_E017 | **Cairn des voyageurs** (`lieu_cairn_des_voyageurs`, `poi_cairn_des_voyageurs`, `monolithe`) | 22-24 |
| N009_E018 | **Ferme abandonnée du nord** (`lieu_ferme_abandonnee_du_nord`, `poi_ferme_abandonnee_du_nord`, `ruine`) | 22-26 |
| N009_E019 | **Camp des chasseurs de loups** (`lieu_camp_des_chasseurs_de_loups`, `poi_camp_des_chasseurs_de_loups`, `camp`) | 22-26 |
| N008_E017 | **Chantier de flottage** (`lieu_chantier_de_flottage`, `poi_chantier_de_flottage`, `camp`) | 22-26 |
| N008_E019 | **Relais de la route du nord** (`lieu_relais_de_la_route_du_nord`, `poi_relais_de_la_route_du_nord`, `camp`) | 22-26 |
| N007_E017 | **Chêne aux pendus** (`lieu_chene_aux_pendus`, `poi_chene_aux_pendus`, `sanctuaire`) | 24-28 |
| N007_E018 | **Repaire de l'ogre de la forêt** (`lieu_repaire_de_l_ogre_de_la_foret`, `poi_repaire_de_l_ogre_de_la_foret`, `salle`) | 25-28 |
| N007_E019 | **Bac du passeur** (`lieu_bac_du_passeur`, `poi_bac_du_passeur`, `camp`) | 22-26 |
| N007_E020 | **Moulin à eau de la rivière** (`lieu_moulin_a_eau_de_la_riviere`, `poi_moulin_a_eau_de_la_riviere`, `camp`) | 24-28 |
| N006_E017 | **Pavillon de chasse impérial** (`lieu_pavillon_de_chasse_imperial`, `poi_pavillon_de_chasse_imperial`, `ruine`) | 24-28 |
| N006_E018 | **Camp des déserteurs** (`lieu_camp_des_deserteurs`, `poi_camp_des_deserteurs`, `camp`) | 26-28 |
| N007_E021 | **Pont effondré** (`lieu_pont_effondre`, `poi_pont_effondre`, `ruine`) | 24-28 |
| N007_E022 | **Pâturages de la côte** (`lieu_paturages_de_la_cote`, `poi_paturages_de_la_cote`, `elevage`) | 24-28 |
| N007_E023 | **Tour de guet côtière** (`lieu_tour_de_guet_cotiere`, `poi_tour_de_guet_cotiere`, `ruine`) | 26-30 |
| N007_E024 | **Épave échouée** (`lieu_epave_echouee`, `poi_epave_echouee`, `ruine`) | 26-30 |
| N006_E021 | **Relais de la route de Ryvaldh** (`lieu_relais_de_la_route_de_riverfold`, `poi_relais_de_la_route_de_riverfold`, `camp`) | 24-28 |
| N006_E022 | **Caravansérail des marchands** (`lieu_caravanserail_des_marchands`, `poi_caravanserail_des_marchands`, `camp`) | 25-29 |
| N006_E024 | **Marais salants** (`lieu_marais_salants`, `poi_marais_salants`, `camp`) | 26-30 |
| N005_E021 | **Ferme des colons** (`lieu_ferme_des_colons`, `poi_ferme_des_colons`, `elevage`) | 24-28 |
| N005_E022 | **Oratoire des voyageurs** (`lieu_oratoire_des_voyageurs`, `poi_oratoire_des_voyageurs`, `sanctuaire`) | 24-28 |
| N005_E023 | **Chemin de halage** (`lieu_chemin_de_halage`, `poi_chemin_de_halage`, `camp`) | 26-30 |
| N005_E024 | **Cimetière des marins** (`lieu_cimetiere_des_marins`, `poi_cimetiere_des_marins`, `sanctuaire`) | 26-30 |
| N004_E021 | **Gibet du carrefour** (`lieu_gibet_du_carrefour`, `poi_gibet_du_carrefour`, `ruine`) | 24-28 |
| N004_E022 | **Prairie des chevaux sauvages** (`lieu_prairie_des_chevaux_sauvages`, `poi_prairie_des_chevaux_sauvages`, `elevage`) | 24-28 |
| N004_E023 | **Grange aux dîmes** (`lieu_grange_aux_dimes`, `poi_grange_aux_dimes`, `ruine`) | 25-29 |
| N004_E024 | **Pêcherie de la côte** (`lieu_pecherie_de_la_cote`, `poi_pecherie_de_la_cote`, `camp`) | 26-30 |
| N003_E021 | **Hutte de la rebouteuse** (`lieu_hutte_de_la_rebouteuse`, `poi_hutte_de_la_rebouteuse`, `camp`) | 24-28 |
| N003_E022 | **Repaire des coupe-jarrets** (`lieu_repaire_des_coupe_jarrets`, `poi_repaire_des_coupe_jarrets`, `camp`) | 24-28 |
| N003_E024 | **Dolmen de la côte** (`lieu_dolmen_de_la_cote`, `poi_dolmen_de_la_cote`, `monolithe`) | 26-30 |
| N002_E021 | **Ossuaire du géant** (`lieu_ossuaire_du_geant`, `poi_ossuaire_du_geant`, `sanctuaire`) | 26-30 |
| N002_E022 | **Rucher de la forêt** (`lieu_rucher_de_la_foret`, `poi_rucher_de_la_foret`, `elevage`) | 26-30 |
| N002_E023 | **Menhirs des géants** (`lieu_menhirs_des_geants`, `poi_menhirs_des_geants`, `monolithe`) | 26-30 |
| N002_E024 | **Cabanes de pêcheurs abandonnées** (`lieu_cabanes_de_pecheurs_abandonnees`, `poi_cabanes_de_pecheurs_abandonnees`, `ruine`) | 26-30 |
| N001_E021 | **Source sacrée de la forêt** (`lieu_source_sacree_de_la_foret`, `poi_source_sacree_de_la_foret`, `sanctuaire`) | 24-28 |
| N001_E022 | **Fosse à loups** (`lieu_fosse_a_loups`, `poi_fosse_a_loups`, `gouffre`) | 24-28 |
| N001_E023 | **Ferme fortifiée de la côte** (`lieu_ferme_fortifiee_de_la_cote`, `poi_ferme_fortifiee_de_la_cote`, `elevage`) | 26-30 |
| N001_E024 | **Anse des naufrageurs** (`lieu_anse_des_naufrageurs`, `poi_anse_des_naufrageurs`, `camp`) | 26-30 |
| N000_E021 | **Puits de la piste** (`lieu_puits_de_la_piste`, `poi_puits_de_la_piste`, `camp`) | 24-28 |
| N000_E022 | **Bergerie des chevriers** (`lieu_bergerie_des_chevriers`, `poi_bergerie_des_chevriers`, `elevage`) | 25-29 |
| N000_E023 | **Rocher des vautours** (`lieu_rocher_des_vautours`, `poi_rocher_des_vautours`, `monolithe`) | 26-30 |
| N000_E024 | **Fortin côtier abandonné** (`lieu_fortin_cotier_abandonne`, `poi_fortin_cotier_abandonne`, `ruine`) | 26-30 |
| S001_E022 | **Carrière de la plaine aride** (`lieu_carriere_de_la_plaine_aride`, `poi_carriere_de_la_plaine_aride`, `camp`) | 26-30 |
| S001_E024 | **Tour à feu de la côte** (`lieu_tour_a_feu_de_la_cote`, `poi_tour_a_feu_de_la_cote`, `ruine`) | 26-30 |
| S002_E022 | **Tombeau des sables** (`lieu_tombeau_des_sables`, `poi_tombeau_des_sables`, `sanctuaire`) | 27-30 |
| S002_E023 | **Campement des pillards du désert** (`lieu_campement_des_pillards_du_desert`, `poi_campement_des_pillards_du_desert`, `camp`) | 27-30 |
| S002_E024 | **Crique des contrebandiers** (`lieu_crique_des_contrebandiers`, `poi_crique_des_contrebandiers`, `camp`) | 26-30 |

**Quêtes** (3, `[GD]`, toutes les étapes dans des cases générées) :

- `quest_nains_est_gd_route_de_voidmark` « La Route de Vozmarkh » (niveau 10+, solo) : hamlet_eisenkehl → lieu_halte_des_muletiers_du_col → lieu_borne_frontiere_renversee → town_0125 (suite du départ nain)
- `quest_nains_est_gd_carriers_de_voidmark` « Les Carriers de Vozmarkh » (niveau 15+, groupe) : hamlet_eisenkehl → lieu_ferme_des_chevriers_nains → lieu_carriere_des_forcats
- `quest_rivage_gd_cargaison_du_levant` « La Cargaison du levant » (niveau 24+, solo) : town_0115 → lieu_relais_de_la_route_de_riverfold → town_riverfold → lieu_epave_echouee → lieu_caravanserail_des_marchands

**Sources.** Inventés `[GD]` sauf : conquête de la Marche par la Lune Noire `[BD-A5]` (pavillon de chasse) ; géants
repoussés vers le nord et l'est `[HS]` (ossuaire, menhirs) ; Vozmarkh tenu par la Lune Noire (fiche) ; économie
v1.0.58 (minerai et pierre de taille de Vozmarkh, moulins, pêche et bois d'œuvre de Rhivkhar, chèvres et pierre de
Dhuzvelt) ; navires du levant à Ryvaldh (v1.0.82). Placements `[INFÉRÉ]`. Aucun nom propre inventé ; noms vérifiés
contre les PNJ de service (« Carrière de pierre de taille » renommée « Carrière des forçats » avant livraison :
« pierre » entre dans des noms de famille de PNJ).

**Faune** : aucune créature créée ; espèces existantes rattachées (`zones_chasse`, `creature_ids`) : ours, serpent,
chèvre, loup géant de guerre, ogre, corbeau, crabe, mouette, deux fois né, vautour.

**Registre** (`regle_canon_registre_decisions`) : 2 entrées v1.0.91 « Ouverture de la route Eisenkehl - Vozmarkh »
(`royaume_des_nains_de_lest`) et « Ouverture des routes de la Marche de l'Est » (`region_marche_est`).

**Routes encore coupées au mur invisible** (non suivies, à décider) : Vozmarkh – Mhordaal (000034, S011_E020),
Vozmarkh – Shaddrek, Vozmarkh – Ghrondak (générateur, par S010_E022 – S008_E024), Rhivkhar – Hilvakh
(000103, N009_E016 – N010_E016), Farvhenn – Rhivkhar (000105, N010_E019 – N011_E020).

**Ce qui est réel en jeu** : les 57 cases, les 5 villes, les 52 POI, les 7 routes du générateur entre ces villes.
**Lore seulement** : quêtes, PNJ donneurs et adversaires, intérieurs, repères de niveau.

**Contrôles** : test `world_gen_tests` « cellules et lieux v1.0.91 » (57 cases générées et terrestres, aucune isolée ;
Vozmarkh atteint à pied depuis Durnhalde, la Marche et la côte depuis Grakhul ; 5 villes jouables ; 7 routes du
générateur et 16 cases des 5 tracés du lore dans des cases jouables ; 52 lieux au sec, seuls sur leur case ; aucun POI
sur les cases des villes ; S008_E020 et S008_E021 fermées). Le test « lieux v1.0.89 » compte désormais 21 cases
jouables dans la boîte du Royaume des nains de l'est (14 + 7). Génération de cases avec le générateur sans erreur.
**Services** : 1242 PNJ (inchangé).

**Points incertains** : tous les noms `[GD]` ; repère 24-30 de la côte orientale ; marge d'une case (dont la rangée
N009 et les forêts N006-N007 × E017-E018) ; quêtes ouvertes à toutes les factions ; Vozmarkh, ville de la Lune
Noire au cœur du royaume nain, comme suite du départ nain.

### 16.21 Régions du hors-série et provinces (v1.0.92)

Lot 20, décisions du propriétaire (12/11/2026) : les **régions de référence** sont celles de la carte p.4 du hors-série
« L'Empire de la Négation » `[HS]` ; une région peut être découpée en **provinces** ; les régions du lore qui ne sont pas
des régions HS deviennent des provinces de leur région HS (identifiant, nom, notes et niveaux inchangés). Attribution
des cases : celle de la proposition (recalage affine sur 8 repères, RMS 1,7 case ; centre de la case dans le polygone
HS ; hors cadre, région HS parente de la région actuelle ; repères et cases de la Via Magna forcés), avec trois
décisions : la zone non nommée Altenberg – Dys (38 cases) va aux Terres de l'Empereur ; le **sud sans frontière HS**
(157 cases) va à la région HS la plus proche parmi Nains du Sud, Drach, Nains de l'Est et Baronnie de Moork (distance
de grille à la case la plus proche de chaque région) ; la Forêt elfique et le Royaume des Nains de l'Est suivent le
hors-série. **La grille ne bouge pas** (terrain, forêts, montagnes, `generated_cells`) ; aucune case ouverte ou fermée,
aucun identifiant supprimé, aucune ville renommée. Scripts reproductibles (hors dépôt) : `assign_v1092.py`,
`lore_v1092.py` (appliqué au fichier v1.0.91), `carte_v1092.py` (rendu), `test_editeur_v1092.py` (Playwright).

**Modèle de données** (une seule collection, compatible avec l'éditeur et avec toutes les références `region_id`
existantes des villes, lieux, quêtes, créatures et factions) :

- `regions[].type` : `"region_hs"` (11 régions du hors-série, plus `nom_hs` = nom de la carte HS ; les noms du lore
  ne changent pas), `"province"` (12, plus `parent_region`), `"region"` (Île de la Trêve, hors HS, et les 5 régions
  sans case : Empire du Jade, Empire Tharque, Les Enfers, La Grande Plaine, Terra Secunda).
- `cells[].region_id` = région HS (ou Île de la Trêve) ; `cells[].province` = province, vide pour le cœur de la
  région. Une province n'est portée que par des cases de sa région parente.
- `villes[]` et `lieux[]` : `region_id` et `province` de leur case (sans case : une ancienne région devenue province
  passe en `province` et sa région parente en `region_id`).
- Quêtes, créatures, raids, factions : `region_id` inchangé (une province reste une fiche de `regions`).
- Rien n'est perdu : quand l'ancienne région d'une case, d'une ville ou d'un lieu n'est conservée ni en région ni en
  province, une note `[DÉCISION] (v1.0.92)` la rappelle (196 cases, 8 villes, 70 lieux) ; les 157 cases du sud portent
  une note `[INFÉRÉ] (v1.0.92)`.

| Région HS | Cases | Cœur | Provinces (cases) |
|---|---:|---:|---|
| Terres des Gigantomorphes | 232 | 225 | Terres du Nord — Château en Ruine (2) ; Marais Maudits de Thenhart (5) |
| Royaume des Nains de l'Est | 216 | 204 | Désert des Sels (12) |
| Terres de l'Empereur Wismerhill Ier | 149 | 118 | 1ère Province de l'Empire (27) ; Province de Northind (4) |
| Territoires orques | 148 | 92 | Territoires des Marches (56) |
| Via Magna | 140 | 106 | Forêt des Lutins (34) ; Désert de l'Oracle (0 : aucune case au lore, boîte du master) |
| Royaume des Nains du Sud | 127 | 58 | Val de Suthvarn (42) ; Confins du Midi (21) ; Terres désolées de Tsaroth (6) |
| Baronnie de Moork | 80 | 62 | Marche de l'Est (18) |
| Royaume Drach | 50 | 50 | — |
| Forêt elfique | 40 | 40 | — |
| Syzygie | 37 | 37 | — |
| Hishtarland | 20 | 20 | — |
| Île de la Trêve (hors HS) | 20 | 20 | — |

Total : 1259 cases terrestres. Le sud sans frontière se répartit en Nains de l'Est 56, Nains du Sud 56, Drach 28,
Baronnie 17. Provinces du sud, à la majorité de leurs cases : Confins du Midi (Nains du Sud 21, Drach 15, Nains de
l'Est 12), Terres désolées (Nains du Sud 6), Désert des Sels (égalité 12/12 entre Nains du Sud et Nains de l'Est,
départagée par la case de Saelvik : Nains de l'Est). Marche de l'Est : Baronnie selon la hiérarchie de la proposition ;
ses autres cases suivent le hors-série (Nains de l'Est 20, Orques 19, Via Magna 4). Cases d'anciennes provinces hors de
leur région parente : Val de Suthvarn 17 (Drach 11, Hishtarland 6), Confins 27, Désert des Sels 12, Northind 5
(Gigantomorphes 2, Forêt elfique 3).

**Villes qui changent de région HS** (ancienne région de la ville ou de sa case) : Sauken Glude of Magistrya (Via
Magna → Terres de l'Empereur), Faernilh (Forêt elfique → Terres de l'Empereur), Ostrevan (Syzygie →
Gigantomorphes), Dyprokh (Forêt elfique → Gigantomorphes), Nhorvaël (Forêt elfique → Orques, province Territoires des
Marches), Horkher (Marche de l'Est → Via Magna), Rhivkhar (Marche de l'Est → Orques), Ebhrazul et Dhazkmer (Marche de
l'Est → Nains de l'Est), Ekvhitorn (Drach → Nains du Sud), Melvhord (Drach → Hishtarland), Thysskar (Confins du
Midi → Nains de l'Est). 22 villes sans région en reçoivent une (dont Tsaroth → Baronnie de Moork, Der Hem Shelbem →
Nains du Sud, Ghrunke Dash et Klehrwath → Nains de l'Est, Harndrecht et Zaltheim → Drach, Hishtarland → Hishtarland).
Lieux : 70 changent de région HS (surtout Marche de l'Est et côte orientale vers les Nains de l'Est et les Orques).

**Niveaux de référence** : inchangés, portés par les notes des provinces (v1.0.86 – v1.0.91) ; une note v1.0.92 sur
chaque région HS liste ses provinces et ses cases.

**Master inchangé.** Les `regions` de `world_master.json` restent des boîtes rectangulaires servant d'étiquettes (la plus
petite boîte contenant le joueur, nom au centre de la carte, annuaire MJ) : les régions HS sont irrégulières (voir
§16.19), les y reporter demanderait des dizaines de boîtes ou un changement de format (liste de cases par région), à
décider. Aucun code C++ ni serveur ne lit les régions du lore.

**Éditeur legacy** (`lune-noire-editor.html` et sa copie `-v20`) : l'export conservait seulement `id`, `name`,
`description`, `notes` des régions ; il garde maintenant tous les champs (`type`, `parent_region`, `nom_hs`). Formulaire
de région : type et région parente (obligatoire pour une province), champs non édités conservés. Carte : le survol
affiche « Région › Province » ; l'éditeur de cellule a une liste « Province » (refusée si elle n'appartient pas à la
région choisie) et la liste « Région » n'offre plus les provinces. Cartes de régions : badge « Province de … » ou
« Région du hors-série » avec la liste des provinces ; compte de cellules par province. Testé avec Playwright
(chargement du JSON, export, survol et édition de N005_W012 Altenberg : Terres de l'empereur › 1ère Province de
l'Empire, aucune erreur JavaScript).

**Registre** (`regle_canon_registre_decisions`) : 5 entrées v1.0.92 (régions de référence HS, provinces, zone
Altenberg – Dys, sud sans frontière, Forêt elfique et Nains de l'Est selon le HS).

**Contrôles** : 1259 cases terrestres, toutes avec une région HS ; 12 provinces, toutes avec une région parente HS ;
aucune case, ville ou lieu avec une province d'une autre région ; aucun identifiant supprimé (régions 29, villes 121,
lieux 294, cellules 1932) ; terrain inchangé. **Services** : 1242 PNJ (inchangé).

**Points à valider** : Marche de l'Est éclatée (18 cases seulement restent dans la province) ; tracé des régions au
sud (Tsaroth à la Baronnie, Der Hem Shelbem aux Nains du Sud, Harndrecht et Zaltheim au Drach, alors que leurs
provinces ou voisins sont ailleurs) ; Hishtarland exclu des candidats du sud ; Désert des Sels coupé en deux ;
cœur des Nains de l'Est très étendu au sud-est ; noms du lore différents des noms HS (« Territoires orcques »,
« Baronnie de Moorks », « Royaumes des nains du sud ») : `nom_hs` ajouté, renommage à décider ; boîtes du master.

### 16.22 Renommage des 88 villes aux noms anglais (v1.0.93)

`[DÉCISION]` du propriétaire (12/11/2026, lot 21) : les noms propres des villes ne sont **ni français ni anglais**, dans le
style de la BD. Les 88 propositions de la liste du 11/11/2026 sont validées : 87 villes et hameaux et le peuple Huacani.
Méthode de la v1.0.75 (§16.4, Syzygie) : le nouveau nom remplace l'ancien partout où il désigne la ville (fiches, noms
de lieux et de provinces dérivés — « Val de Suthvarn », « Mine de Vharholdt », « Port de Haevrin » —, routes, cellules,
quêtes, raids, créatures, factions, règles, registre, notes, `world_master.json` `cities`/`pois`/`regions`/`interactables`,
`carte_export.json`, ROYAUMES.md, ce document, générateur de services, commentaires et libellés des tests C++), par mots
entiers, du plus long au plus court (paires proches : Duskmoor/Duskmere, Dustfen/Dustfield, Saltfen/Saltwick…) ;
élision ajustée (« d'Eaglewatch » → « de Haeldorf », « de Dawnmere » → « d'Orhendal »).

- **Identifiants inchangés** (`hamlet_*`, `town_*`, `poi_port_*`, `riv_verdansur_*`, `colline_verdansur`,
  `crevasse_wardholt`, `race_huacani`, `faction_huacani`, `religion_huacani_soleil`, `quest_abysses_*`…) : aucun effet
  sur les sauvegardes ni sur le code.
- **Alias** : l'ancien nom est ajouté en tête du champ `alias` de chaque fiche `villes` (alias existants gardés, ex.
  `Unnamed-66` de Brookend) et une note `[DÉCISION] (v1.0.93)` le rappelle ; peuple Huacani : note `[DÉCISION]` sur
  `race_huacani` et `faction_huacani` (ces collections n'ont pas de champ `alias`). Master : ancien nom dans `source`.
  `carte_export.json` : `name` renommé, `alias` garde l'ancien nom.
- **Leviathan** : la ville (`leviathan_jungle`, « Leviathan Jungle », « Leviathan » dans le master et les docs) devient
  Ghulvathan. Restent : la forêt « jungle de Leviathan » (site ophidien, v1.0.82), le Leviathan de la Porte des Enfers,
  « Ecaille de Leviathan », « Defenses du Leviathan » et le « titanesque Leviathan » (créature et butins).
- **Harrowdale** (`horker`) : `RENOMMAGES` du générateur de services passe à « Harrodh » ; le master affichait encore
  « Horker » pour cette ville (graphie THS de Horkher) : nom aligné sur le lore. Sa description du master (comté de George
  d'Horkher, BD-T6) reste celle de la vraie Horkher (`city_horkher`) : à corriger.
- **Huacani** : adjectif en prose « huacani » → « hwakhari » ; étiquettes `tags` des quêtes (`huacani`) inchangées.
- Aucune citation de la BD ne contenait ces noms (tous `[GD]` ou repris de `carte_export.json`). Les anciennes copies
  `lune-noire-data-v18.json` et `-v19.json` ne sont pas modifiées. Section « À vérifier » de la proposition (Kendrhir
  Vale, Forteresse d'Heureux, Kohlbrand, Eisenkehl…) non traitée.

| Groupe | Identifiant | Ancien nom | Nouveau nom |
|---|---|---|---|
| Terres de l'Empereur | `hameau_eaglewatch` | Eaglewatch | **Haeldorf** |
| Terres de l'Empereur | `hamlet_ambermead` | Ambermead | **Ombhral** |
| Terres de l'Empereur | `hamlet_greyhedge` | Greyhedge | **Gredhaim** |
| Terres de l'Empereur | `town_hollowgate` | Hollowgate | **Kholvard** |
| Terres de l'Empereur | `hamlet_0154` | Greypool | **Ghrepol** |
| Terres de l'Empereur | `hamlet_0172` | Saltfen | **Zaltheim** |
| Terres de l'Empereur | `hamlet_0174` | Harrowbrook | **Harndrecht** |
| Terres de l'Empereur | `hamlet_0175` | Ashhollow | **Aeskhol** |
| Terres de l'Empereur | `hamlet_0197` | Dustfen | **Dhuzmar** |
| Terres de l'Empereur | `hamlet_0214` | Clearfen | **Klaerhin** |
| 1ère Province de l'Empire | `hameau_dawnmere` | Dawnmere | **Orhendal** |
| 1ère Province de l'Empire | `hamlet_brightholm` | Brightholm | **Lhumbrecht** |
| 1ère Province de l'Empire | `hamlet_whitesheaf` | Whitesheaf | **Ysselhaim** |
| 1ère Province de l'Empire | `hamlet_lambsfold` | Lambsfold | **Lammerhin** |
| 1ère Province de l'Empire | `town_0129` | Justfall | **Iustarn** |
| Syzygie | `hameau_oathford` | Oathford | **Ordhrann** |
| Syzygie | `hamlet_tithemead` | Tithemead | **Thydmar** |
| Syzygie | `hamlet_wardholt` | Wardholt | **Vharholdt** |
| Syzygie | `hamlet_0189` | Orderfen | **Ostrevan** |
| Syzygie | `hamlet_0148` | Trueford | **Treuvhal** |
| Syzygie | `town_0119` | Verdansur | **Vaerdhun** |
| Royaume Drach | `hameau_wyrmhallow` | Wyrmhallow | **Drakhenhal** |
| Royaume Drach | `hamlet_cragfold` | Cragfold | **Krakhol** |
| Royaume Drach | `hamlet_bridlecrag` | Bridlecrag | **Bhridrak** |
| Royaume Drach | `hamlet_0157` | Scalebrook | **Skhaldrach** |
| Royaume Drach | `ville_equiton` | Equiton | **Ekvhitorn** |
| Royaume Drach | `town_0137` | Brookend | **Brokhendt** |
| Royaume Drach | `hamlet_0218` | Millford | **Melvhord** |
| Royaume Drach | `town_fairbrook` | Fairbrook | **Faervosk** |
| Royaume des nains de l'Est | `town_ironbrook` | Ironbrook | **Ghrondak** |
| Royaume des nains de l'Est | `town_0108` | Darkwater | **Mhordaal** |
| Royaume des nains de l'Est | `town_0125` | Voidmark | **Vozmarkh** |
| Territoires orcques | `leviathan_jungle` | Leviathan Jungle | **Ghulvathan** |
| Territoires orcques | `hamlet_0206` | Mossy Rock | **Mhozrak** |
| Forêt elfique / Province de Northind | `hameau_silverglade` | Silverglade | **Syllaërin** |
| Forêt elfique / Province de Northind | `hamlet_greenbough` | Greenbough | **Ghwenaëth** |
| Forêt elfique / Province de Northind | `hamlet_fernhollow` | Fernhollow | **Faernilh** |
| Forêt elfique / Province de Northind | `hamlet_0166` | Northfen | **Nhorvaël** |
| Forêt elfique / Province de Northind | `town_hazelgate` | Hazelgate | **Hazhaëlin** |
| Île de la Trêve (zone commune 45-60) | `town_brinehaven` | Brinehaven | **Brenhaval** |
| Île de la Trêve (zone commune 45-60) | `town_duskmoor` | Duskmoor | **Duzhmoroth** |
| Île de la Trêve (zone commune 45-60) | `town_truceholm` | Truceholm | **Thrival** |
| Île de la Trêve (zone commune 45-60) | `town_lanternhold` | Lanternhold | **Lhantarn** |
| Île de la Trêve (zone commune 45-60) | `faction_huacani`, `race_huacani` (peuple) | Huacani | **Hwakhari** |
| Terres des Gigantomorphes | `hamlet_0153` | Hayfen | **Haevrin** |
| Terres des Gigantomorphes | `town_0123` | Ashwick | **Askhvir** |
| Terres des Gigantomorphes | `town_0139` | Stonehaven | **Stohnvar** |
| Terres des Gigantomorphes | `town_coldhollow` | Coldhollow | **Khaldhol** |
| Terres des Gigantomorphes | `hamlet_fernhill` | Fernhill | **Vernhild** |
| Terres des Gigantomorphes | `hamlet_draketon` | Draketon | **Dhraegtun** |
| Terres des Gigantomorphes | `town_stonebridge` | Stonebridge | **Shtenbrak** |
| Terres des Gigantomorphes | `town_fellwatch` | Fellwatch | **Fhelvakt** |
| Terres des Gigantomorphes | `town_ile_ashwick` | Deepbrook | **Dyprokh** |
| Territoires des Marches et côte nord-est | `ville_moorgate` | Moorgate | **Mhorgad** |
| Territoires des Marches et côte nord-est | `ville_dunfold` | Dunfold | **Dhunfal** |
| Territoires des Marches et côte nord-est | `ville_ashcroft` | Ashcroft | **Askhrof** |
| Territoires des Marches et côte nord-est | `ville_haywick` | Haywick | **Hayvekh** |
| Territoires des Marches et côte nord-est | `town_0112` | Westmoor | **Vestmhor** |
| Territoires des Marches et côte nord-est | `town_0113` | Northgate | **Nhorgast** |
| Territoires des Marches et côte nord-est | `town_0117` | Greyhaven | **Grhaevan** |
| Territoires des Marches et côte nord-est | `town_0134` | Deepmoor | **Dhemorr** |
| Territoires des Marches et côte nord-est | `town_0135` | Runecroft | **Runhkar** |
| Territoires des Marches et côte nord-est | `highfall` | Highfall | **Hagfhal** |
| Territoires des Marches et côte nord-est | `farwatch` | Farwatch | **Farvhenn** |
| Marche de l'Est / Baronnie de Moorks | `shadowfen` | Shadowfen | **Shaddrek** |
| Marche de l'Est / Baronnie de Moorks | `town_0115` | Ebonvale | **Ebhrazul** |
| Marche de l'Est / Baronnie de Moorks | `town_0126` | Duskmere | **Dhazkmer** |
| Marche de l'Est / Baronnie de Moorks | `town_0127` | Nightward | **Nakhtvar** |
| Marche de l'Est / Baronnie de Moorks | `town_0131` | Blackmoor | **Vlakmorh** |
| Marche de l'Est / Baronnie de Moorks | `hamlet_0201` | Nightfield | **Nykhfeld** |
| Marche de l'Est / Baronnie de Moorks | `hamlet_0196` | Grimfen | **Ghrimfeth** |
| Marche de l'Est / Baronnie de Moorks | `town_0136` | Shadehold | **Shezhold** |
| Via Magna et Rivage des Géants (centre-est) | `horker` | Harrowdale | **Harrodh** |
| Via Magna et Rivage des Géants (centre-est) | `hamlet_0205` | Arcanefield | **Arkhanyra** |
| Via Magna et Rivage des Géants (centre-est) | `town_0132` | Rivercross | **Rhivkhar** |
| Via Magna et Rivage des Géants (centre-est) | `town_0133` | Hillwatch | **Hilvakh** |
| Via Magna et Rivage des Géants (centre-est) | `town_riverfold` | Riverfold | **Ryvaldh** |
| Via Magna et Rivage des Géants (centre-est) | `town_greyfield` | Greyfield | **Grhavelt** |
| Via Magna et Rivage des Géants (centre-est) | `hamlet_dustfield` | Dustfield | **Dhuzvelt** |
| Confins du Midi (sud) | `crossroads` | Crossroads | **Khrozrad** |
| Confins du Midi (sud) | `clearwater` | Clearwater | **Klehrwath** |
| Confins du Midi (sud) | `town_0114` | Southport | **Suthvarn** |
| Confins du Midi (sud) | `town_0118` | Saltwick | **Saelvik** |
| Confins du Midi (sud) | `town_0120` | Coldport | **Kholdvar** |
| Confins du Midi (sud) | `hamlet_0193` | Frosthollow | **Frhozhol** |
| Confins du Midi (sud) | `hamlet_0146` | Millend | **Mhillenz** |
| Confins du Midi (sud) | `hamlet_0217` | Claybury | **Khlaybur** |
| Confins du Midi (sud) | `hamlet_thistle_creek` | Thistle Creek | **Thysskar** |

**Remplacements** : 3 401 (lore 2 137, master 360, `carte_export.json` 64, LORE_BD.md 726 avant cette section,
ROYAUMES.md 10, `generate_city_services.py` 10, `WorldGenTests.cpp` 91, `Terrain.cpp` 3). **Registre** : 1 entrée v1.0.93.

**Contrôles** : aucun des 88 anciens noms hors alias, notes v1.0.93 et exceptions ci-dessus ; aucun identifiant ajouté
ni supprimé (lore, master, `carte_export.json`) ; services : 121 villes, 1242 PNJ inchangés (`generate_city_services.py`,
`validate_city_services.py`, `--check`) ; `world_gen_tests`, `world_places_tests`, `start_locations_tests` verts.

## 17. Questions ouvertes

- Albums hors série (v1.0.56) : points ouverts listés en 10.5.
- Enfers et dragons (v1.0.81) : genre de Xygga, noms des dragons, combat contre Xygga endormi, instances des plans (16.10).
- Peuples lointains et Ophidiens (v1.0.82) : noms [GD] à valider, directions, instances de la Cité-Nid, butin ophidien, lignes maritimes des comptoirs (16.11).
- Balisage et ancrages (v1.0.85) : noms [GD] des balises, accès à la poche 1-15 de Whispers Hill, 4 raids et 52 quêtes sans case (16.14).
- Moork, Tsaroth, Oracle (v1.0.86) : noms [GD] des 36 lieux et 14 quêtes, repères de niveau proposés (Tsaroth 20-40, Oracle 10-25), factions des quêtes de la baronnie, Krak de Jhenner sans case (16.15).
- Gigantomorphes, Via Magna, Marche de l'Est (v1.0.87) : noms [GD] des 11 lieux et 10 quêtes, repères proposés (lisière des Gigantomorphes 28-36, Via Magna détaillée, Marche de l'Est 15-28), Marche de l'Est sans case jouable (16.16 ; ouverte en v1.0.88).
- Ouverture de la Marche de l'Est (v1.0.88) : noms [GD] des 40 lieux et 2 quêtes, gradient 15-28, marge et trous intérieurs, forêt du nord et confins du sud-est fermés, routes coupées vers Rhivkhar, Ryvaldh, Grhavelt et Dhuzvelt (ouvertes en v1.0.91), Via Magna non tracée par le générateur entre Shezhold, Horkher et Nykhfeld (16.17).
- Royaume des nains de l'est (v1.0.89) : aucune case jouable vide, 5 quêtes [GD] de départ, gradient proposé (1-10 détaillé, 10-20 et 18-30 au-delà), ouverture de la région à décider (16.18 ; route Eisenkehl – Vozmarkh ouverte en v1.0.91).
- Ouverture d'Eisenkehl – Vozmarkh et des routes de la Marche (v1.0.91) : noms [GD] des 52 lieux et 3 quêtes, repère 24-30 de la côte orientale, marge et trous intérieurs, routes encore coupées vers Mhordaal, Shaddrek, Ghrondak, Hilvakh et Farvhenn (16.20).
- Sud-centre (v1.0.90) : noms et repères des régions Forêt des Lutins, Val de Suthvarn, Désert des Sels et Confins du Midi ; régions irrégulières dans le master (16.19).
- Renommage des 88 villes (v1.0.93) : description du master de `horker` (Harrodh) reprise de Horkher ; « jungle de Leviathan » gardée ; Kendrhir Vale, Forteresse d'Heureux, Kohlbrand, Eisenkehl à décider (16.22).

- ~~Départ des Démons~~ : traité par la seconde carte des Enfers (voir [`ENFERS.md`](ENFERS.md)).
- ~~Lhynn : presqu'île dans un grand lac (BD-T3) ou côte~~ : **close** (v1.0.54) — océan / mer
  de l'ouest, décision du propriétaire ; le « lac » est la baie en fjord.
- ~~Pilou~~ : **close** (v1.0.54) — surnom de Pile-ou-Face ; `char_pilou` fusionné.
- Identité de « l'amant » vengé par la succube (pl.31) : non nommé.
- Sort de Feidreiva après les « brumes du temps » : à suivre dans les tomes suivants.
- Race `race_half_dark_elf` utilisée par des quêtes et compétences mais absente de `races`.
- Escaliers de l'Oracle : rampe et escalier de briques (BD-T2) contre huit escaliers
  blancs (lore, BD-T3) ; s'y ajoutent les « lacs du destin » au cœur de l'Empire (T11),
  une colline à sanctuaire (T12) et un antre souterrain où il est enchaîné (T14).
- Palais du Maître des Dragons « au-delà des sept régions » (p.29) mais visible depuis le
  pont de la Passe (p.42) : hyperbole ou second palais.
- Ghulvathan : N009_E011 dans le monde jouable (cellules générées) contre N012_E012 dans
  le lore (v1.0.40). Ce n'est plus une ville de départ (v1.0.61, section 14) ; le hameau
  dzorak Grakhul (N008_E012) est placé à côté de la position jouable.
- Krak de Jhenner : sans cellule.
- ~~Sysigie (N009_W011) : grand fleuve autour de la cellule~~ : **close** (v1.0.55) — fleuve
  `riv_fleuve_sysigie` à deux bras autour de l'île (section 8.2).
- ~~Placements sans cellule (T7-T14)~~ : **close** (v1.0.55) — section 8.1.
- Nom de la fille de Wismerhill (il a plusieurs enfants, T12) : « Elle s'appelle Hellaynnea,
  n'est-ce pas ? » (T12 p.7) — reste à préciser.
- ~~Retour de Parsifal au T12~~ : **close** (v1.0.55) — caché dans une cité, plus montré.
- ~~Kruikna, « Cruik », dragon bleu de Pilou~~ : **close** (v1.0.55) — trois dragons différents.
- ~~Greldinard appelé « Baron » (T10-T13)~~ : **close** (v1.0.55) — appellation de la BD, sans fief.
- Noms des fleuves, rivières et lacs : à donner par le propriétaire. Le fleuve et le lac de Syzygie sont nommés
  (v1.0.78, §16.7).
- Forteresse naine d'Heureux (S006_E004) : la zone `zone_forteresse_naine_abandonnee_t9` la disait
  « proche de Moork » (S002_E015), sans appui BD.
