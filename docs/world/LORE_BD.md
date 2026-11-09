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
- **Corpus** : Tome 0, Tomes 1 à 14, « L'Empire de la Négation », Methraton 1-3, Arcanes 1-5 ; les tomes 15 et suivants sont exclus.
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
| 1.0.45 | Kendrhir déplacée au sud de la Faille de Kashogs (N015_E012, sur une falaise, pas face à la mer) ; Tbarak, Klindor, Hazol dans les Marches ; **Equiton** (S005_W004) reprend la ligne maritime et la quête de la baie. |
| 1.0.46 | Intérieur de Kendrhir : 19 PNJ de service placés, PNJ d'ambiance. |
| 1.0.47 | Kendrhir **en reconstruction** (après le T1) — état `reconstruction` du générateur. |
| 1.0.48 | Marches étendues au nord de la Faille ; itinéraire complet de la horde (carte p.25) : Tbarak → Klindor → Hazol → **Moorgate** → **Dunfold** → Faille → Kendrhir. |
| 1.0.49 | Lieux mineurs : Forêt de la Rencontre (N012_E008), **Haywick** (N018_E013, ruine), Camp de l'Étang (N018_E012), Champ de la Première Bataille (N018_E011), **Ashcroft** (N017_E012, ruine). |
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
  16 lisières (v1.0.29), île de Deepbrook en N012_E002-E003 (v1.0.36), côtes
  S006_W004 et S006_W007, rivière de Feyhin en N008_E007.
- **Millway, Stonemark, Ridgeholt** sont retirées du master : elles avaient été
  supprimées du lore (v1.0.29-1.0.30).
- **Leviathan** reste en N009_E011 dans le master (ville de départ des orcs), alors que le
  lore la place en N012_E012 (v1.0.40) : voir les questions ouvertes.

### 5.3 Villes

- **44 villes du lore** entrent dans le monde jouable : race déduite de la région, sauf
  factions humaines, ports du royaume Drach et Horkher (comté humain, BD-T6) ; faction
  reprise du lore.
- Le **palais du Maître des Dragons** reste un POI (`poi_palais_passe`). La Cité du Jade,
  la Nécropole du Jade et le Krak de Jhenner n'ont pas de cellule et restent hors carte.
- Noms : Unnamed-64 → **Runecroft**, Unnamed-143 → **Clearfen** (noms libres de
  `carte_export.json`).
- **Deepmoor** passe de N014_E016 (cellule de Hoengardt) à N014_E017.
- **10 villes** sont créées sur les nouvelles terres : Coldhollow, Fernhill, Draketon,
  Stonebridge, Riverfold (port à l'embouchure de la rivière de Rivercross), Greyfield,
  Dustfield, Ironbrook (nains), Fairbrook, Thistle Creek.
- Le master compte **90 agglomérations** et le lore 93 villes (1 008 PNJ de service).
- Nouveaux POI : Haawling Spines (N002_E006), cratère du volcan (S008_E014), entrée des
  Abysses (S008_W005) ; genres `volcan` et `gouffre` ajoutés au client.

### 5.4 Eau, forêts, champs

- **Rivières** : de Rivercross (N010_E019 → Rivercross → Riverfold, mer orientale) et de
  Southport (S013_E006 → Southport → mer du sud).
- **Lacs de plaine** : Clearwater (S014_E014), Hillwatch (N010_E017), Millend
  (S002_E002), Nightward (N001_E012), Darkwater (S011_E021).
- **Forêts** (avec lisières) : Blackmoor (N007–N005, E016–E018), Harrowbrook
  (S005–S006, W010–W008), Millford (S011–S012, E001–E003), Landes (N016–N015,
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
| Horkher | Notes BD (T6) de la fiche `horker` (Harrowdale, N004_E009) déplacées vers `city_horkher` (N006_E014). Harrowdale reste une ville du jeu (`RENOMMAGES` du générateur). |
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
Parsifal, Kruikna / Cruik / dragon de Pilou, titre de Greldinard. Reste ouvert (section 16) : le nom
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
**Mise à jour v1.0.56** (section 10, Q5) : la cité s'affiche « **Aurore** » (alias Sysigie, Syzygie ;
identifiant `ville_sysigie` inchangé), capitale de la principauté de Syzygie ; le grand fleuve traverse
le **lac d'Aurore**, au milieu duquel se trouve l'île. Tracé et grille ci-dessous inchangés.
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
  Methraton, le Serpent, Parsifal, Haghendorf, Feidreiva, Urmarcht, Okhost, Moork, Lhynn, Aurore, etc.

### 10.3 Décisions du propriétaire (05/11/2026)

| N° | Sujet | Décision appliquée |
|---|---|---|
| 1 | Époque de THS | « Après T11 ». Entrent dans le monde jouable : Via Magna, Hishtarland, plan de Lhynn (chenal, deux enceintes, port ; ancien palais conservé). Institutions du nouvel empire, porte des Enfers ouverte, bâtons de prière : lore `[HS]` hors époque. |
| 2 | Préquelles | État de fin d'album puis évolution des T1-T10 : lieux détruits en ruines, lieux debout intacts, PNJ morts absents (souvenir, tombe), PNJ vivants sans suite seulement si une quête les utilise (`regle_epoque_hors_serie`). |
| 3 | Frise | A4 → A5 → M2/M3 → A3, puis T00, A1, A2 au seuil du T1 ; positions M2, M3, A3 `[HYPOTHÈSE]` ; seule durée chiffrée : 3000 ans. |
| 4 | Methraton, archimages, Erypt | Methraton caché dans son plan, accessible par son temple de Lhynn ; archimages libérés avant le T9 (`tl_hs_liberation_archimages`) ; panthéon d'Erypt = culte lointain, sans dieu incarné. |
| 5 | Sysigie | Syzygie = principauté (région `syzygie`) ; `ville_sysigie` affichée **« Aurore »** (alias Sysigie, Syzygie) dans le lore et `world_master.json` ; le fleuve traverse le lac d'Aurore, île dans le lac, brouillard conservé ; Passe et muraille = gorge du T11 et Portes d'Aurore ; carte THS = schéma. |
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
| Almuria (`lieu_almuria`) | N018_E017 | POI `poi_almuria` (`cite`) | Marches du Nord, sous les montagnes de N019_E017 et les neiges de N020_E018 (grande ville aux tours coniques enneigées, A1 p.30 `[HYPOTHÈSE]`), à l'est de Rériah ; Runecroft (N018_E016) entre les deux. |
| Château de la Mort (`lieu_chateau_de_la_mort`) | N013_E016 | POI `poi_chateau_de_la_mort` (`ruine`) | Chaîne (code `M`) au bord des Territoires orcques (N013_E015, Q19), même chaîne que le défilé de Mordrek aux falaises rouges : forteresse-mine dans une falaise de canyon sous un plateau (A4 p.16-22). **Ruine** (prise par les orques, A4 p.29). |
| Passe de Syzygie et sa muraille (`lieu_passe_syzygie`) | N008_W013 | POI `poi_passe_syzygie` (`defile`) | Au sud-ouest d'Aurore (N009_W011), entre la côte (N008_W014) et la vallée : Passe à l'ouest de la Syzygie, route venue du sud par la côte puis vers l'est (A3 pl.16, pl.41) ; Parsifal part vers Altenberg (N005_W012) par les Portes d'Aurore (T8 p.24-25). Hors du fleuve (N008_W011) et de Justfall (N007_W011). |
| Marche de l'Est (`region_marche_est`) | 56 cellules sans région de E013 à E020, S003 à N007 | région `marche_de_lest` (E013-E020, N004-N007) | Extrême est autour de Moork (A5 p.38-44) : comté d'Horkher au nord (N006_E014), confins à l'est de la baronnie. Les cellules de `baronnie_de_moorks` gardent leur région (elle fait partie de la Marche) ; dans le monde jouable, la baronnie garde son rectangle et la Marche couvre la bande au nord (la plus petite région l'emporte, Horkher s'y affiche). |
| Marais maudits de Thenhart (`marais_maudits_t11`) | N010_W005, N010_W004, N010_W003, N011_W005, N011_W004 | région `marais_maudits` (W005-W003, N010-N011) | Cellules de marais du bestiaire v1.0.58 (Dvorak) ; `region_id` `terres_des_gigantomorphes` remplacé. Hayfen (N011_W004) au bord du marais. |
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
  Harrowdale, Shadehold, Horkher (« Horker » de la carte, Q12), Nightfield, Moork. Sources : carte
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
départs par race (Hameau de Marn, Whispers Hill, Leviathan, Ghrunke Dash) redeviennent des villes
normales ; leurs cellules restent générées. « Pas loin de X » : un **nouveau hameau** dédié à la faction,
dans une case voisine de X (libre, habitable, hors de l'eau), relié à X par une route (`routes` 000121
à 000127, type `road`) et doté des services d'un hameau. Noms des hameaux `[GD]`, **à valider** ;
cellules `[INFÉRÉ]`. Script reproductible : `lore_v1061.py` (appliqué au fichier v1.0.60).

| Faction (lore) | Faction de jeu | Départ | Cellule | Justification |
|---|---|---|---|---|
| Chevaliers de la Lumière (`chevaliers_de_la_lumiere`) | `lumiere` | hameau Dawnmere (`hameau_dawnmere`), près d'Altenberg | N005_W011 | Plaine de la Première Province à l'est d'Altenberg (N005_W012) ; ouest et sud sont côte et plage. |
| Chevaliers de la Justice (`chevaliers_de_la_justice`) | `justice` | hameau Oathford (`hameau_oathford`), près d'Aurore | N009_W010 | Plaine de la Syzygie à l'est de l'île (N009_W011), hors du fleuve ; la route franchit un bras. |
| Dzorak (`dzorak_orcs`) | `dzorak` | hameau Grakhul (`hameau_grakhul`), près de Leviathan | N008_E012 | Lisière de forêt au sud-est de Leviathan du monde jouable (N009_E011) ; cellule déjà générée. |
| Chevaliers-Dragons (`faction_dragon_knights`) | `dragons` | hameau Wyrmhallow (`hameau_wyrmhallow`), près du Palais des Maîtres des Dragons | S004_W004 | Montagne au sud du palais (S003_W004) ; nord, est et ouest sont des neiges éternelles. |
| Nains (`faction_dwarves`) | `naine` | hameau Durnhalde (`hameau_durnhalde`), près de Ghrunke Dash | S007_E017 | Montagne du royaume des nains de l'Est (S007_E016 à l'ouest), hors des cases volcaniques ; cellule déjà générée. |
| Hauts Elfes (`faction_hauts_elfes`) | `elfe` | hameau Silverglade (`hameau_silverglade`), près de Northind | N007_W006 | Forêt de la région de Northind à l'ouest (N007_W005), hors des cases à rivière et de la case d'Orkh. |
| Légions Impériales (`faction_legions_imperiales`) | `legions_imperiales` (réservé) | hameau Eaglewatch (`hameau_eaglewatch`), près de Lhynn | N002_W010 | Pâturages des Terres de l'Empereur au nord de Lhynn (N001_W010) ; cellule déjà générée, voisine de Marn. |
| La Lune Noire (`faction_black_moon`) | `lune_noire` | DANS le palais de la Lune Noire, sur la Lune (`lieu_palais_lune_noire_lunaire`) | hors carte | Lieu existant ; pas de hameau. |
| Les Archimages (`les_archimages`) | aucune | DANS la pyramide de Methraton (`lieu_plan_methraton`, `zone_flying_pyramid`) | hors carte | Lieu existant ; pas de hameau. |
| La Légion infernale (`faction_legion_infernale`, **nouvelle**) | `legion` | Citadelle de la Légion (`citadelle_legion`), carte des Enfers | Enfers N000_E000 | Lieu central de la croix des 5 cellules des Enfers ; un démon sort et rentre par n'importe laquelle des 5 portes ([`ENFERS.md`](ENFERS.md) §2-3). |
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
  le repli (première ville de départ du master de cette race : humains → Eaglewatch, orcs → Grakhul,
  nains → Durnhalde, elfes → Silverglade, démons → Citadelle). Le master (création) et le shard
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

### 15.1 Eaglewatch — Légions Impériales (v1.0.62)

Centre N002_W010 (`hameau_eaglewatch`, v1.0.61). Les 9 cases étaient déjà générées. Lhynn (N001_W010,
capitale, mer à l'ouest) et le hameau de Marn (N003_W009) sont dans la région mais **rien n'y est posé**.
Script reproductible : `lore_v1062.py` (appliqué au fichier v1.0.61).

| Case | Terrain | Contenu | Faune (niveaux) |
|---|---|---|---|
| N003_W011 | plaine (Première Province) | POI **Camp de manœuvres des Légions** (`lieu_camp_manoeuvres_legions`, `poi_camp_manoeuvres_legions`, `camp`) : lices et cibles des recrues `[GD]` ; ancrage : légions et camps impériaux `[BD-THS p.8]` | lapin (1-3), renard (2-5), cheval (3-8) |
| N003_W010 | plaine (Première Province) | Ferme **Ambermead** (`hamlet_ambermead`, `farm_hamlet` / `hameau`) : blé et fourrage de la garnison `[GD]` ; route 000128 vers Eaglewatch | poule, coq (1), bœuf (3-6), chien (2-6), lapin |
| N003_W009 | plaine | Hameau de Marn (existant, inchangé) | poule, lapin |
| N002_W011 | plaine | POI **Autel des Aigles** (`lieu_autel_des_aigles`, `poi_autel_des_aigles`, `sanctuaire`) : serment des recrues `[GD]` (description d'Eaglewatch, v1.0.61) | lapin, corbeau (1-4) |
| N002_W010 | plaine | **Eaglewatch** (hameau de départ) ; routes 000121 (Lhynn), 000128, 000129 ; sentiers 000131 à 000134 vers les 4 POI | chien, poule ; molosse impérial (10-20, existant) |
| N002_W009 | plaine (pâturages) | POI **Ruines de la villa tharque** (`lieu_ruines_villa_tharque`, `poi_ruines_villa_tharque`, `ruine`) : domaine de l'ancien Empire tharque `[GD]`, ancré sur la Via Magna « héritée du grand Empire tharque » `[BD-THS p.8]` | araignée (3-8), corbeau, renard |
| N001_W011 | côte | POI **Grotte des contrebandiers** (`lieu_grotte_contrebandiers`, `poi_grotte_contrebandiers`, `salle`) : contrebande du port de guerre de Lhynn `[GD]` (`[BD-T7 p.5-6]`) | crabe, mouette (1), araignée |
| N001_W010 | plaine | Lhynn (capitale, inchangée) | inchangée |
| N001_W009 | plaine | Ferme **Greyhedge** (`hamlet_greyhedge`, `farm_hamlet` / `hameau`) : volailles, chèvres et ânes de bât `[GD]` ; route 000129 vers Eaglewatch, route 000130 vers la Via Magna | chèvre (1-3), âne (2-4), chien, renard ; molosse impérial (existant) |

- **Routes** (`routes`) : 000128 Ambermead → Eaglewatch, 000129 Eaglewatch → Greyhedge et 000130
  Greyhedge → Via Magna (`road`, embranchement sur la route 000120 en N000_W010, première case après
  Lhynn, sans ville à l'arrivée) : d'Eaglewatch on rejoint la Via Magna sans traverser la capitale.
  Sentiers 000131 à 000134 (`path`, sans écurie) d'Eaglewatch vers les 4 POI.
- **Services** : 2 fermes (taille `hameau`) ; écuries d'après les routes `road` : Eaglewatch 3 (au lieu
  de 1), Ambermead 1, Greyhedge 2 ; 1036 → 1050 PNJ de service.
- **Faune** : les bêtes de guerre des élevages impériaux (rhinocéros cuirassé, golem de roche,
  béhémoth, léviathan impérial, niveaux 18 à 55, v1.0.58) sont retirées de N002_W009 et N002_W010 et
  restent dans les enclos de N002_W008 (`poi_elevages_imperiaux`, hors région).
- **Points incertains** : noms ; le molosse impérial (10-20) reste sur les routes de Lhynn (N001_W009,
  N002_W010), au plafond de la tranche ; crabe et mouette ont un niveau par défaut (1-1) ; le camp de
  manœuvres et l'autel pourraient porter des PNJ ou des quêtes d'accueil des Légions (aucun créé).

### 15.2 Dawnmere — Chevaliers de la Lumière (v1.0.64)

Centre N005_W011 (`hameau_dawnmere`, v1.0.61). Les 9 cases étaient déjà générées. Altenberg (N005_W012,
citadelle de l'Ordre, intacte à l'époque du jeu) est dans la région mais **rien n'y est posé** ; Dys
(N005_W009) et Justfall (N007_W011) sont hors région. Thème : l'Ordre de la Lumière et l'Église du Dieu
de Lumière (`lore.religions`, v1.0.58) — commanderie, chapelle, pèlerinage ; trois des POI sont ajoutés
aux `lieux_de_culte` de `religion_eglise_lumiere`. Script reproductible : `lore_v1064.py` (appliqué au
fichier v1.0.62).

| Case | Terrain | Contenu | Faune (niveaux) |
|---|---|---|---|
| N006_W012 | plaine (Première Province) | POI **Calvaire des pèlerins** (`lieu_calvaire_des_pelerins`, `poi_calvaire_des_pelerins`, `sanctuaire`) : première station du pèlerinage d'Altenberg à Aurore pour la Grande Messe de la Lumière `[GD]` (`fete_eglise_grande_messe_lumiere`) ; croix cerclée, bannières rouges à croix blanche `[BD-T8 p.28-33]` `[BD-T2 p.34]` | lapin (1-3), corbeau (1-4), renard (2-5) |
| N006_W011 | plaine | Ferme **Whitesheaf** (`hamlet_whitesheaf`, `farm_hamlet` / `hameau`) : grain des novices et pain d'autel d'Altenberg `[GD]` ; route 000136 vers Dawnmere | poule, coq (1), chien (2-6), lapin |
| N006_W010 | forêt avec rivière | POI **Chapelle de l'ermite** (`lieu_chapelle_de_l_ermite`, `poi_chapelle_de_l_ermite`, `sanctuaire`) : chapelle et source d'eau bénite `[GD]` ; prêtres de chapelle `[BD-A3 pl.19-20]`, croix de pierre en forêt `[BD-A3 pl.33]` | biche (2-6), araignée (3-8), renard, lapin |
| N005_W012 | plaine | Altenberg (grande ville, inchangée) | inchangée |
| N005_W011 | plaine | **Dawnmere** (hameau de départ) ; routes 000122 (Altenberg), 000135 à 000137 ; sentiers 000138 à 000141 vers les 4 POI | chien, poule |
| N005_W010 | plaine | Commanderie rurale **Brightholm** (`hamlet_brightholm`, `farm_hamlet` / `hameau`) : domaine agricole et haras de l'Ordre tenu par des frères convers (`[BD-T8 p.33-35]`) `[GD]` — distincte de la Grande et de la Haute Commanderie, salles de la citadelle `[BD-T8]` ; route 000135 vers Dawnmere | cheval (3-8), bœuf (3-6), chien, poule |
| N004_W012 | plage | POI **Camp des naufrageurs** (`lieu_camp_des_naufrageurs`, `poi_camp_des_naufrageurs`, `camp`) : faux feux sur la longue plage au sud d'Altenberg `[GD]`, citadelle « sur un rocher battu par les vagues » `[BD-T2 p.32-33]` | crabe, mouette (1), corbeau |
| N004_W011 | plaine | Ferme **Lambsfold** (`hamlet_lambsfold`, `farm_hamlet` / `hameau`) : bergerie, laine blanche des habits et capuches de l'Ordre (`[BD-T8 p.31-35]`) `[GD]` ; route 000137 vers Dawnmere | mouton (1), bœuf, âne (2-4), chien, renard |
| N004_W010 | plaine | POI **Ruines du prieuré** (`lieu_ruines_du_prieure`, `poi_ruines_du_prieure`, `ruine`) : prieuré abandonné depuis des générations, crypte à vermine `[GD]` (aucune destruction de l'époque du jeu) | araignée, corbeau, renard |

- **Routes** (`routes`) : 000135 Dawnmere → Brightholm, 000136 Dawnmere → Whitesheaf, 000137 Dawnmere →
  Lambsfold (`road`, cases voisines) ; sentiers 000138 à 000141 (`path`, sans écurie) de Dawnmere vers les
  4 POI.
- **Services** : 3 hameaux (taille `hameau`) ; écuries d'après les routes `road` : Dawnmere 4 (au lieu
  de 1), Brightholm, Whitesheaf et Lambsfold 1 ; 1050 → 1067 PNJ de service.
- **Faune** : les 8 cases hors Altenberg n'avaient aucune créature ; espèces existantes de niveau 1 à 10
  seulement.
- **Points incertains** : noms ; Fratus Sinister et ses « Fils de la Lumière » (`[BD-T4]`, `[BD-T5]`)
  ne sont pas évoqués : la région reste neutre sur la direction de l'Ordre ; crabe, mouette et mouton ont
  un niveau par défaut (1-1) ; Dawnmere porte 4 écuries pour un hameau (règle des routes) ; le calvaire et
  la chapelle pourraient porter des PNJ ou quêtes d'accueil (aucun créé).

### 15.3 Oathford — Chevaliers de la Justice (v1.0.66)

Centre N009_W010 (`hameau_oathford`, v1.0.61). Les 9 cases étaient déjà générées. Aurore (N009_W011,
`ville_sysigie`, cité de l'île du lac, intacte à l'époque du jeu) est dans la région mais **rien n'y est
posé** ; le fleuve de Sysigie passe par le centre de N010_W011 et de N008_W011 (section 8.2, le
générateur pose les POI au centre de leur case) : **aucun lieu** dans ces deux cases, seulement de la
faune. La Passe de Syzygie (N008_W013) et Justfall (N007_W011) sont hors région. Thème : l'Ordre de la
Justice — serments des novices, garnisons, parjures ; la stèle est ajoutée aux `lieux_de_culte` de
`religion_eglise_lumiere`. Routes selon la règle du propriétaire v1.0.65 : chaque nouvelle ferme ou
hameau est relié au voisin le plus proche (ici Oathford, case voisine, sans étape) ; routes existantes
inchangées. Script reproductible : `lore_v1066.py` (appliqué au fichier v1.0.65).

| Case | Terrain | Contenu | Faune (niveaux) |
|---|---|---|---|
| N010_W011 | plaine avec fleuve (Syzygie) | aucun lieu (lit du fleuve) | lapin (1-3), corbeau (1-4), loutre (1), renard (2-5) |
| N010_W010 | plaine (Syzygie) | Ferme **Tithemead** (`hamlet_tithemead`, `farm_hamlet` / `hameau`) : ferme de la dîme, blé, foin et chevaux de remonte des garnisons d'Aurore `[GD]` ; route 000166 vers Oathford. POI **Lices des novices** (`lieu_lices_des_novices`, `poi_lices_des_novices`, `camp`) à distance de la ferme : exercices des novices `[GD]` | poule, coq (1), chien (2-6), bœuf (3-6), cheval (3-8), lapin |
| N010_W009 | lisière (Syzygie) | POI **Camp des parjures** (`lieu_camp_des_parjures`, `poi_camp_des_parjures`, `camp`) : écuyers chassés de l'Ordre pour parjure et brigands `[GD]` | renard, araignée (3-8), corbeau, biche (2-6) |
| N009_W011 | plaine avec fleuve | Aurore (grande ville, inchangée) | inchangée |
| N009_W010 | plaine | **Oathford** (hameau de départ) ; routes 000123 (Aurore), 000166, 000167 ; sentiers 000168 à 000171 vers les 4 POI | chien, poule |
| N009_W009 | lisière (Première Province) | Hameau de garnison **Wardholt** (`hamlet_wardholt`, `farm_hamlet` / `hameau`) : palissade, poste de garde de l'Ordre face à la forêt elfique, bûcherons et charbonniers `[GD]` ; route 000167 vers Oathford | chien, âne (2-4), biche, lapin, renard |
| N008_W011 | plaine avec fleuve (Syzygie) | aucun lieu (lit du fleuve ; route 000021 Aurore → Justfall inchangée) | lapin, corbeau, loutre, renard |
| N008_W010 | plaine (Première Province) | POI **Stèle des Serments** (`lieu_stele_des_serments`, `poi_stele_des_serments`, `sanctuaire`) : stèle de granit gravée de la devise de l'Ordre, serment des novices devant un frère aîné `[GD]` ; devise et frères aînés `[BD-T4]` | mouton (1), lapin, corbeau, renard |
| N008_W009 | lisière (Première Province) | POI **Ruines de la tour du guet** (`lieu_ruines_tour_du_guet`, `poi_ruines_tour_du_guet`, `ruine`) : tour abandonnée depuis des générations face à la forêt elfique, cave à vermine `[GD]` (aucune destruction de l'époque du jeu) | araignée, corbeau, renard |

- **Routes** (`routes`) : 000166 Oathford → Tithemead, 000167 Oathford → Wardholt (`road`, cases
  voisines, sans `waypoints`) ; sentiers 000168 à 000171 (`path`, sans écurie) d'Oathford vers les 4 POI.
- **Services** : 2 hameaux (taille `hameau`) ; écuries d'après les routes `road` : Oathford 3 (au lieu
  de 1), Tithemead et Wardholt 1 ; 1067 → 1077 PNJ de service.
- **Corridor des départs** (`connect_start_cities`) : avec Wardholt, le corridor qui relie les départs
  passe par N008_W008 (forêt elfique, désormais générée) au lieu de N006_W009 et N006_W008, ce qui aurait
  retiré Dys (N005_W009) du monde jouable ; ces trois cellules sont ajoutées explicitement à
  `generated_cells` (dernière ligne) : 180 → 181 cellules générées, aucune perdue.
- **Faune** : les 8 cases hors Aurore n'avaient aucune créature ; espèces existantes de niveau 1 à 10
  seulement.
- **Points incertains** : noms ; « Stèle » plutôt que « Pierre » des Serments (le mot « Pierre » est un
  nom du monde et ferait refuser des PNJ nains par `validate_city_services.py`) ; les Lices partagent la
  case de Tithemead (5 cases libres pour 6 lieux) ; Parsifal et l'évacuation de l'Ordre vers Syzygie
  (`[BD-T10]`) ne sont pas évoqués ; loutre et mouton ont un niveau par défaut (1-1) ; la stèle et les
  lices pourraient porter des PNJ ou quêtes d'accueil (aucun créé).

### 15.4 Grakhul — Dzorak (v1.0.67)

Centre N008_E012 (`hameau_grakhul`, v1.0.61). Les 9 cases étaient déjà générées. Leviathan est en
N009_E011 dans le monde jouable (`world_master.json`) mais en N012_E012 dans le lore (section 16) :
**rien n'est posé** dans N009_E011, pas même de la faune. Mossy Rock (N006_E011) et Horkher (N006_E014)
sont hors région. Thème : les clans Dzorak et les croyances des esprits des orques (`lore.religions`,
`religion_esprits_orques`, v1.0.58) — kraals, totem chamanique, arène, camp de guerre, chasse ; le totem
est ajouté aux `lieux_de_culte` de `religion_esprits_orques`. Routes selon la règle du propriétaire
v1.0.65 : chaque nouveau kraal est relié au voisin le plus proche (ici Grakhul, case voisine, sans étape) ;
routes existantes inchangées (000125 vers Leviathan Jungle, 000142 Mossy Rock → Grakhul). Les PNJ de
service des kraals sont Dzorak (`RACE_FACTIONS` de `generate_city_services.py` : `dzorak_orcs`). Script
reproductible : `lore_v1067.py` (appliqué au fichier v1.0.66).

| Case | Terrain | Contenu | Faune (niveaux) |
|---|---|---|---|
| N009_E011 | forêt | Leviathan du monde jouable (inchangée, rien n'est posé) | inchangée |
| N009_E012 | forêt | POI **Arène des Jeunes-Sangs** (`lieu_arene_des_jeunes_sangs`, `poi_arene_des_jeunes_sangs`, `camp`) : fosse de combat entre Grakhul et Leviathan, épreuve de l'arène des jeunes guerriers `[GD]` ; épreuve et festin `[BD-A4 p.30-32]`, trône de défenses `[BD-A5 p.12]` | araignée (3-8), corbeau (1-4), renard (2-5), biche (2-6) |
| N009_E013 | forêt | POI **Camp de guerre du clan Ghazrak** (`lieu_camp_de_guerre_ghazrak`, `poi_camp_de_guerre_ghazrak`, `camp`) : le clan rassemble ses guerriers avant de rejoindre les armées de la Lune Noire `[GD]` ; trophées de têtes `[BD-A4 p.29-30]` | chien (2-6), araignée, corbeau, renard |
| N008_E011 | forêt | POI **Totem d'Ogunta** (`lieu_totem_d_ogunta`, `poi_totem_d_ogunta`, `sanctuaire`) : cercle de pierres et totem de crânes, osselets, runes de puissance, mémoire de la chamane Ogunta `[GD]` ; `[BD-A4 p.8-15]` | corbeau, araignée, biche, lapin (1-3) |
| N008_E012 | lisière | **Grakhul** (hameau de départ) ; routes 000125 (Leviathan Jungle), 000142 (Mossy Rock), 000172, 000173 ; sentiers 000174 à 000178 vers les 5 POI | chien, chèvre (1-3) |
| N008_E013 | lisière | Kraal **Gharzuk** (`hamlet_gharzuk`, `farm_hamlet` / `hameau`) : chasseurs et charbonniers, gibier fumé, peaux, bois des palissades `[GD]` ; route 000173 vers Grakhul | chien, biche, lapin, renard |
| N007_E011 | lisière | POI **Affût de la Grande Chasse** (`lieu_affut_de_la_grande_chasse`, `poi_affut_de_la_grande_chasse`, `camp`) : camp de chasseurs, départ de la Grande Chasse (`fete_orques_grande_chasse`) `[GD]` ; route 000142 inchangée | biche, lapin, renard, corbeau |
| N007_E012 | plaine | Kraal **Thokrul** (`hamlet_thokrul`, `farm_hamlet` / `hameau`) : éleveurs, chèvres, bœufs et ânes de bât, peaux tannées `[GD]` ; route 000172 vers Grakhul | chèvre, bœuf (3-6), âne (2-4), chien, lapin |
| N007_E013 | plaine (Marche de l'Est) | POI **Ruines du fortin des Marches** (`lieu_ruines_fortin_des_marches`, `poi_ruines_fortin_des_marches`, `ruine`) : fortin impérial abandonné depuis la conquête de la Marche de l'Est par la Lune Noire, avant le T1 `[BD-A5 p.38-44]` ; le fortin est `[GD]` | araignée, corbeau, renard, lapin |

- **Routes** (`routes`) : 000172 Grakhul → Thokrul, 000173 Grakhul → Gharzuk (`road`, cases voisines,
  sans `waypoints`) ; sentiers 000174 à 000178 (`path`, sans écurie) de Grakhul vers les 5 POI.
- **Services** : 2 kraals (taille `hameau`) ; écuries d'après les routes `road` : Grakhul 4 (au lieu
  de 2), Thokrul et Gharzuk 1 ; 1077 → 1090 PNJ de service, tous Dzorak dans les kraals.
- **Faune** : les 8 cases hors Leviathan n'avaient aucune créature ; espèces existantes de niveau 1 à 10
  seulement. Sanglier (8-15), loup (5-12) et sanglier de guerre (15-25, N007_E014, hors région) dépassent
  la tranche : la Grande Chasse et le festin de sanglier restent un décor.
- **Points incertains** : noms ; le clan Ghazrak est inventé (aucun personnage de la BD) ; le totem ne dit
  rien de l'esprit d'Ogunta lié à l'anneau de Greldinard (Q22) ; Greldinard n'est pas évoqué ; l'arène
  touche la case de Leviathan, dont la position reste ouverte (section 16) ; la chèvre (1-3) et le chien
  sont les seuls animaux de Grakhul ; le totem, l'arène et l'affût pourraient porter des PNJ ou quêtes
  d'accueil (aucun créé).

## 16. Questions ouvertes

- Albums hors série (v1.0.56) : points ouverts listés en 10.5.

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
- Leviathan : N009_E011 dans le monde jouable (cellules générées) contre N012_E012 dans
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
- Noms des fleuves, rivières et lacs : à donner par le propriétaire (fleuve de Sysigie « [à nommer] »).
- Forteresse naine d'Heureux (S006_E004) : la zone `zone_forteresse_naine_abandonnee_t9` la disait
  « proche de Moork » (S002_E015), sans appui BD.
