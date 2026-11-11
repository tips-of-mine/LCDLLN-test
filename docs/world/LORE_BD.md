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
| Almuria (`lieu_almuria`) | N018_E017 | POI `poi_almuria` (`cite`) | Marches du Nord, sous les montagnes de N019_E017 et les neiges de N020_E018 (grande ville aux tours coniques enneigées, A1 p.30 `[HYPOTHÈSE]`), à l'est de Rériah ; Runecroft (N018_E016) entre les deux. |
| Château de la Mort (`lieu_chateau_de_la_mort`) | N013_E016 | POI `poi_chateau_de_la_mort` (`ruine`) | Chaîne (code `M`) au bord des Territoires orcques (N013_E015, Q19), même chaîne que le défilé de Mordrek aux falaises rouges : forteresse-mine dans une falaise de canyon sous un plateau (A4 p.16-22). **Ruine** (prise par les orques, A4 p.29). |
| Passe de Syzygie et sa muraille (`lieu_passe_syzygie`) | N008_W013 | POI `poi_passe_syzygie` (`defile`) | Au sud-ouest de Syzygie (N009_W011), entre la côte (N008_W014) et la vallée : Passe à l'ouest de la Syzygie, route venue du sud par la côte puis vers l'est (A3 pl.16, pl.41) ; Parsifal part vers Altenberg (N005_W012) par les Portes d'Aurore (T8 p.24-25). Hors du fleuve (N008_W011) et de Justfall (N007_W011). |
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
| Chevaliers de la Justice (`chevaliers_de_la_justice`) | `justice` | hameau Oathford (`hameau_oathford`), près de Syzygie | N009_W010 | Plaine de la Syzygie à l'est de l'île (N009_W011), hors du fleuve ; la route franchit un bras. |
| Dzorak (`dzorak_orcs`) | `dzorak` | hameau Grakhul (`hameau_grakhul`), près de Leviathan | N008_E012 | Lisière de forêt au sud-est de Leviathan du monde jouable (N009_E011) ; cellule déjà générée. |
| Chevaliers-Dragons (`faction_dragon_knights`) | `dragons` | hameau Wyrmhallow (`hameau_wyrmhallow`), près du Palais des Maîtres des Dragons | S004_W004 | Montagne au sud du palais (S003_W004) ; nord, est et ouest sont des neiges éternelles. |
| Nains (`faction_dwarves`) | `naine` | hameau Durnhalde (`hameau_durnhalde`), près de Ghrunke Dash | S007_E017 | Montagne du royaume des nains de l'Est (S007_E016 à l'ouest), hors des cases volcaniques ; cellule déjà générée. |
| Hauts Elfes (`faction_hauts_elfes`) | `elfe` | hameau Silverglade (`hameau_silverglade`), près de Northind | N007_W006 | Forêt de la région de Northind à l'ouest (N007_W005), hors des cases à rivière et de la case d'Orkh. |
| Légions Impériales (`faction_legions_imperiales`) | `legions_imperiales` (réservé) | hameau Eaglewatch (`hameau_eaglewatch`), près de Lhynn | N002_W010 | Pâturages des Terres de l'Empereur au nord de Lhynn (N001_W010) ; cellule déjà générée, voisine de Marn. |
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

> **v1.0.72** (section 16.1) : Ambermead est déplacée en N003_W008 et Greyhedge en N001_W008, hors de la
> région 3×3 ; routes 000128, 000129 et 000130 retracées.
>
> **v1.0.73** (section 16.2) : l'entrée nord de la Mine de la Traverse (raid à une équipe, niveaux 10 à
> 15) s'ouvre au sud de Greyhedge, au pied de la chaîne N000_W008.

> **v1.0.76** (section 16.5) : sentiers 000131 (Camp de manœuvres) et 000133 (Ruines de la villa tharque)
> supprimés, leurs lieux étant sur les routes 000199 et 000128 ; Greyhedge → Ambermead (000201) devient un sentier.

### 15.2 Dawnmere — Chevaliers de la Lumière (v1.0.64)

Centre N005_W011 (`hameau_dawnmere`, v1.0.61). Les 9 cases étaient déjà générées. Altenberg (N005_W012,
citadelle de l'Ordre, intacte à l'époque du jeu) est dans la région mais **rien n'y est posé** ; Dys
(N005_W009) et Justfall (N007_W011) sont hors région. Thème : l'Ordre de la Lumière et l'Église du Dieu
de Lumière (`lore.religions`, v1.0.58) — commanderie, chapelle, pèlerinage ; trois des POI sont ajoutés
aux `lieux_de_culte` de `religion_eglise_lumiere`. Script reproductible : `lore_v1064.py` (appliqué au
fichier v1.0.62).

| Case | Terrain | Contenu | Faune (niveaux) |
|---|---|---|---|
| N006_W012 | plaine (Première Province) | POI **Calvaire des pèlerins** (`lieu_calvaire_des_pelerins`, `poi_calvaire_des_pelerins`, `sanctuaire`) : première station du pèlerinage d'Altenberg à Syzygie pour la Grande Messe de la Lumière `[GD]` (`fete_eglise_grande_messe_lumiere`) ; croix cerclée, bannières rouges à croix blanche `[BD-T8 p.28-33]` `[BD-T2 p.34]` | lapin (1-3), corbeau (1-4), renard (2-5) |
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

> **v1.0.76** (section 16.5) : sentier 000139 (Chapelle de l'ermite, sur la route Dys → Justfall) supprimé ;
> 000140 remplacé par Brightholm → Ruines du prieuré (000225) ; 000138 (Calvaire des pèlerins) part d'Altenberg et
> 000141 (Camp des naufrageurs) de Lambsfold : ils croisaient une route avant Dawnmere.

### 15.3 Oathford — Chevaliers de la Justice (v1.0.66)

Centre N009_W010 (`hameau_oathford`, v1.0.61). Les 9 cases étaient déjà générées. Syzygie (N009_W011,
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
| N010_W010 | plaine (Syzygie) | Ferme **Tithemead** (`hamlet_tithemead`, `farm_hamlet` / `hameau`) : ferme de la dîme, blé, foin et chevaux de remonte des garnisons de Syzygie `[GD]` ; route 000166 vers Oathford. POI **Lices des novices** (`lieu_lices_des_novices`, `poi_lices_des_novices`, `camp`) à distance de la ferme : exercices des novices `[GD]` | poule, coq (1), chien (2-6), bœuf (3-6), cheval (3-8), lapin |
| N010_W009 | lisière (Syzygie) | POI **Camp des parjures** (`lieu_camp_des_parjures`, `poi_camp_des_parjures`, `camp`) : écuyers chassés de l'Ordre pour parjure et brigands `[GD]` | renard, araignée (3-8), corbeau, biche (2-6) |
| N009_W011 | plaine avec fleuve | Syzygie (grande ville, inchangée) | inchangée |
| N009_W010 | plaine | **Oathford** (hameau de départ) ; routes 000123 (Syzygie), 000166, 000167 ; sentiers 000168 à 000171 vers les 4 POI | chien, poule |
| N009_W009 | lisière (Première Province) | Hameau de garnison **Wardholt** (`hamlet_wardholt`, `farm_hamlet` / `hameau`) : palissade, poste de garde de l'Ordre face à la forêt elfique, bûcherons et charbonniers `[GD]` ; route 000167 vers Oathford | chien, âne (2-4), biche, lapin, renard |
| N008_W011 | plaine avec fleuve (Syzygie) | aucun lieu (lit du fleuve ; route 000021 Syzygie → Justfall inchangée) | lapin, corbeau, loutre, renard |
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
- **Faune** : les 8 cases hors Syzygie n'avaient aucune créature ; espèces existantes de niveau 1 à 10
  seulement.
- **Points incertains** : noms ; « Stèle » plutôt que « Pierre » des Serments (le mot « Pierre » est un
  nom du monde et ferait refuser des PNJ nains par `validate_city_services.py`) ; les Lices partagent la
  case de Tithemead (5 cases libres pour 6 lieux) ; Parsifal et l'évacuation de l'Ordre vers Syzygie
  (`[BD-T10]`) ne sont pas évoqués ; loutre et mouton ont un niveau par défaut (1-1) ; la stèle et les
  lices pourraient porter des PNJ ou quêtes d'accueil (aucun créé).

> **v1.0.72** (section 16.1) : Wardholt est au fond d'une crevasse en cul-de-sac, avec une mine ; la
> route Tithemead → Orderfen (000198) remplace la route 000066 Syzygie → Orderfen.

> **v1.0.76** (section 16.5) : sentiers 000169 (Lices des novices) et 000170 (Camp des parjures) supprimés,
> leurs lieux étant sur les routes 000166 et 000198.

### 15.4 Grakhul — Dzorak (v1.0.67)

Centre N008_E012 (`hameau_grakhul`, v1.0.61). Les 9 cases étaient déjà générées. Leviathan est en
N009_E011 dans le monde jouable (`world_master.json`) mais en N012_E012 dans le lore (section 17) :
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
  touche la case de Leviathan, dont la position reste ouverte (section 17) ; la chèvre (1-3) et le chien
  sont les seuls animaux de Grakhul ; le totem, l'arène et l'affût pourraient porter des PNJ ou quêtes
  d'accueil (aucun créé).

> **v1.0.76** (section 16.5) : sentiers 000175 (Arène des Jeunes-Sangs) et 000177 (Affût de la Grande Chasse)
> supprimés, leurs lieux étant sur les routes 000125 et 000142.

### 15.5 Wyrmhallow — Chevaliers-Dragons (v1.0.68)

Centre S004_W004 (`hameau_wyrmhallow`, v1.0.61). Les 9 cases étaient déjà générées. Le Palais des Maîtres
des Dragons et le Sanctuaire des Chevaliers Dragons (S003_W004, `poi_palais_passe` et
`poi_sanctuaire_chevaliers_dragons` dans le monde jouable) et Equiton (S005_W004, port du Royaume Drach)
sont dans la région mais **rien n'y est posé**, faune comprise. Thème : l'élevage et le dressage des
Chevaliers-Dragons — bergers qui nourrissent les dragons du Palais, haras des montures des écuyers, aire
de dressage, nids abandonnés (les œufs sont gardés dans les sous-sols du Palais, aucun commerce de dragons
ni d'œufs, v1.0.58), cairn du pacte ; le cairn est ajouté aux `lieux_de_culte` de
`religion_croyances_mineures` (culte du pacte). Routes selon la règle du propriétaire v1.0.65 : chaque
nouveau hameau est relié au voisin le plus proche (ici Wyrmhallow, case voisine orthogonale, sans étape) ;
routes existantes inchangées (000124 vers le Palais, 000082 Palais → Equiton, 000017). Population
humaine : `RACE_FACTIONS` de `generate_city_services.py` inchangé. Script reproductible : `lore_v1068.py`
(appliqué au fichier v1.0.67).

| Case | Terrain | Contenu | Faune (niveaux) |
|---|---|---|---|
| S003_W003 | neiges éternelles | POI **Nids abandonnés des Crêtes** (`lieu_nids_abandonnes_des_cretes`, `poi_nids_abandonnes_des_cretes`, `ruine`) : aires de ponte désertées depuis des générations, coquilles brisées et ossements `[GD]` ; œufs dans les sous-sols du Palais (`city_dragon_palace`), vieux dragons et progéniture `[BD-T2]` ; aucun dragon n'y vit | vautour (1), corbeau (1-4), aigle (1), marmotte (1) |
| S003_W004 | neiges éternelles | Palais des Maîtres des Dragons et Sanctuaire des Chevaliers Dragons (inchangés, rien n'est posé) | inchangée |
| S003_W005 | neiges éternelles | POI **Cairn des Ailes tombées** (`lieu_cairn_des_ailes_tombees`, `poi_cairn_des_ailes_tombees`, `sanctuaire`) : chevaliers et dragons tombés en vol, serment des écuyers au pacte `[GD]` ; pacte `[BD-T2]`, rompu au T14 seulement `[BD-T14 p.21-23]` | aigle, corbeau, marmotte, chèvre (1-3) ; Grand Ancien (66-70) retiré |
| S004_W003 | montagne | Hameau **Cragfold** (`hamlet_cragfold`, `farm_hamlet` / `hameau`) : bergers, chèvres, moutons et bœufs dont une part monte au Palais pour nourrir les dragons `[GD]` ; route 000179 vers Wyrmhallow | chèvre, mouton (1), bœuf (3-6), chien (2-6), marmotte |
| S004_W004 | montagne | **Wyrmhallow** (hameau de départ) ; routes 000124 (Palais), 000179, 000180 ; sentiers 000181 à 000184 vers les 4 POI | chien, chèvre, mulet (1) |
| S004_W005 | montagne | Hameau **Bridlecrag** (`hamlet_bridlecrag`, `farm_hamlet` / `hameau`) : élevage de montures (chevaux de montagne, mulets, poneys) des écuyers, selliers `[GD]` ; écuyers qui apprennent à monter (v1.0.61), « J'ai pas le droit de monter un dragon » `[BD-T2]` ; route 000180 vers Wyrmhallow | cheval (3-8), mulet, poney (1), âne (2-4), chien |
| S005_W003 | montagne (Royaume Drach) | POI **Ruines du pont des Gorges** (`lieu_ruines_pont_des_gorges`, `poi_ruines_pont_des_gorges`, `ruine`) : pont suspendu et corps de garde abandonnés depuis des générations `[GD]` ; gorges et ponts suspendus du royaume des Chevaliers-Dragons `[BD]` (lieu distinct du pont de la Passe, `[BD-T2]`) | araignée (3-8), vautour, corbeau, marmotte |
| S005_W004 | plaine (Royaume Drach) | Equiton (ville portuaire, inchangée) | inchangée |
| S005_W005 | montagne (Royaume Drach) | POI **Aire de dressage des écuyers** (`lieu_aire_de_dressage_des_ecuyers`, `poi_aire_de_dressage_des_ecuyers`, `camp`) : selle de dragon sur poutre pivotante, lance, ordres en dracchen `[GD]` ; combat aérien `[BD]`, langue dracchen `[BD-T2]` | cheval, corbeau, aigle, chèvre ; dragon des Princes Dragons (40-48) retiré |

- **Routes** (`routes`) : 000179 Wyrmhallow → Cragfold, 000180 Wyrmhallow → Bridlecrag (`road`, cases
  voisines, sans `waypoints`) ; sentiers 000181 à 000184 (`path`, sans écurie) de Wyrmhallow vers les 4 POI.
- **Services** : 2 hameaux (taille `hameau`) ; écuries d'après les routes `road` : Wyrmhallow 3 (au lieu
  de 1), Cragfold et Bridlecrag 1 ; 1090 → 1102 PNJ de service, tous humains.
- **Faune** : bestiaire v1.0.58, pas de dragon adulte en zone débutant : le Grand Ancien
  (`creature_dragon_grand_ancien`) est retiré de S003_W005 (il reste en S002_W005 et S004_W006, hors
  région) et le dragon des Princes Dragons (`creature_dragon_fratus_t8`) de S005_W005 (il reste en
  S005_W006 et au Palais). Espèces existantes de niveau 1 à 10 seulement ; aucun dragonnet du bestiaire
  n'est dans la tranche (dragonnet-léopard 14-22), aucun n'est créé.
- **Points incertains** : noms ; les dragons du Palais (40-48) restent dans la case du Palais, au cœur de
  la région (lieu de la BD, non modifié) ; la note v1.0.61 de Wyrmhallow dit les cases est et ouest en
  neiges éternelles alors que S004_W003 et S004_W005 sont en montagne (les hameaux suivent le terrain des
  cellules) ; le tribut de bétail pour les dragons et l'abandon des nids sont inventés ; aigle, vautour,
  marmotte, mulet, poney et mouton ont un niveau par défaut (1-1) ; l'aire de dressage et le cairn
  pourraient porter des PNJ ou quêtes d'accueil (aucun créé).

> **v1.0.76** (section 16.5) : Bridlecrag passe en S004_W006 et Cragfold en S004_W002, hors de la région 3×3
> (`game_design.depart.region` inchangée), dont ils restent le haras et la bergerie ; routes 000180 et 000179
> allongées par leur ancienne case. Leurs animaux domestiques les suivent ; S004_W005 garde marmottes et corbeaux,
> S004_W003 sa marmotte. Le Palais (S003_W004) et Equiton (S005_W004) ne changent pas.

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

### 15.7 Silverglade — Hauts Elfes (v1.0.70)

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
Greenbough et Fernhollow à Silverglade (case voisine orthogonale ; Northind n'est que diagonal de
Greenbough, le village Orkh que diagonal de Fernhollow) ; routes existantes inchangées (000127
Silverglade → Northind, 000077 et 000085 de Northind). Les PNJ de service des hameaux sont hauts elfes
(`RACE_FACTIONS` de `generate_city_services.py` : `faction_hauts_elfes`). Script reproductible :
`lore_v1070.py` (appliqué au fichier v1.0.69).

| Case | Terrain | Contenu | Faune (niveaux) |
|---|---|---|---|
| N006_W007 | forêt (forêt elfique) | POI **Bosquet des Pavanes** (`lieu_bosquet_des_pavanes`, `poi_bosquet_des_pavanes`, `sanctuaire`) : cercle d'arbres centenaires où les jeunes elfes apprennent les pavanes sacrées `[GD]` ; pavanes en l'honneur de Mère Nature `[BD-T14 p.19]` | biche (2-6), lutin (5-10), hibou (1), écureuil (1) |
| N006_W006 | forêt (Province de Northind) | Hameau de cueilleurs **Greenbough** (`hamlet_greenbough`, `farm_hamlet` / `hameau`) : herboristes, séchoirs à herbes et à champignons `[GD]` ; route 000192 vers Silverglade. POI **Autel des Vents** (`lieu_autel_des_vents`, `poi_autel_des_vents`, `sanctuaire`) à l'écart du hameau : premier message confié aux Vents `[GD]` ; Vents messagers `[BD-T12 p.6, 10]` | lapin (1-3), écureuil, biche, renard (2-5) |
| N006_W005 | forêt avec rivière (Province de Northind) | aucun lieu (rivière au centre de la case) | biche, loutre (1), renard, lapin |
| N007_W007 | forêt (forêt elfique) | Hameau de facteurs d'arcs **Fernhollow** (`hamlet_fernhollow`, `farm_hamlet` / `hameau`) : arcs, bois d'if, replantation `[GD]` (`[BD-A4 p.6]`) ; route 000193 vers Silverglade. POI **Tour des Flèches blanches** (`lieu_tour_des_fleches_blanches`, `poi_tour_des_fleches_blanches`, `camp`) à l'écart du hameau : tour de guet de bois vivant `[GD]` ; flèche à empennage blanc `[HS]` | biche, renard, corbeau (1-4), lutin |
| N007_W006 | forêt (Province de Northind) | **Silverglade** (hameau de départ) ; routes 000127 (Northind), 000192, 000193 ; sentiers 000194 à 000197 vers les 4 POI | lapin, écureuil, renard |
| N007_W005 | forêt (Province de Northind) | Northind (grande ville, inchangée, rien n'est posé) | inchangée |
| N008_W007 | forêt (forêt elfique) | Porte 5 des enfers (lieu BD, inchangé, rien n'est posé) | inchangée (démon à tête de sanglier 30-40) |
| N008_W006 | forêt (Province de Northind) | village Orkh (monde jouable, inchangé, rien n'est posé) | inchangée (aucune) |
| N008_W005 | forêt (Province de Northind) | POI **Ruines du Temple sylvestre** (`lieu_ruines_du_temple_sylvestre`, `poi_ruines_du_temple_sylvestre`, `ruine`) : temple elfe abandonné depuis des générations, quand la forêt reculait devant les colons `[GD]` (`[BD-A4 p.4-6]` ; aucune destruction de l'époque du jeu, sans rapport avec l'incendie de Northind `[BD-T8]`) | araignée (3-8), corbeau, chouette (1), renard |

- **Routes** (`routes`) : 000192 Silverglade → Greenbough, 000193 Silverglade → Fernhollow (`road`, cases
  voisines, sans `waypoints`) ; sentiers 000194 à 000197 (`path`, sans écurie) de Silverglade vers les 4 POI.
- **Services** : 2 hameaux (taille `hameau`) ; écuries d'après les routes `road` : Silverglade 3 (au lieu
  de 1), Greenbough et Fernhollow 1 ; 1114 → 1127 PNJ de service, tous hauts elfes dans les nouveaux hameaux.
- **Corridor des départs** (`connect_start_cities`, contrôlé par `tools/world_gen`) : avec Greenbough, le
  corridor ne passe plus par N006_W004 (forêt de la Province de Northind), qui est ajoutée explicitement à
  `generated_cells` (dernière ligne) ; avec Fernhollow, il passe par N007_W008 (forêt elfique, sans
  créature ni lieu), désormais générée. L'ordre des villes compte (Kruskal) : les deux hameaux sont en fin
  de liste de `cities` (insérés après Silverglade, ils ajoutaient aussi N004_W005). 181 → 182 cellules
  générées, aucune perdue, aucun nouvel avertissement du dry-run. Aucune case libre de la région ne
  laissait le corridor inchangé avec deux hameaux.
- **Faune** : les 5 cases libres et le centre n'avaient aucune créature, aucune n'est retirée ; espèces
  existantes de niveau 1 à 10 seulement. La fée (12-20) et les lutins de N009_W006 à N009_W008 restent hors
  région.
- **Points incertains** : noms (consonance anglaise alignée sur Silverglade) ; le **démon à tête de
  sanglier (30-40)** reste dans la case de la Porte 5 des enfers (lieu BD non modifié, comme le Palais de
  Wyrmhallow) : à retirer de N008_W007 si le propriétaire veut la région entièrement débutante (il resterait
  à la porte, `zones_chasse` `lieu_portail_enfer_5`) ; la Tour est de genre `camp` (aucun genre « tour »
  côté client) ; l'Autel et la Tour partagent la case d'un hameau (4 cases libres pour 6 lieux) ; le
  village-ambassade elfe `[HS]` n'est pas placé ; écureuil, loutre, hibou et chouette ont un niveau par
  défaut (1-1) ; le bosquet et l'autel pourraient porter des PNJ ou quêtes d'accueil (aucun créé).

> **v1.0.72** (section 16.1) : Silverglade est déplacée seule en N007_W003 ; la région 3×3 reste centrée
> sur N007_W006. Les sentiers 000194 à 000197 partent de Greenbough ou de Fernhollow.

> **v1.0.76** (section 16.5) : sentier 000197 (Ruines du Temple sylvestre) supprimé, le lieu étant sur la route
> Silverglade → Fernhollow (000193).

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
| 1 | Route 000066 Syzygie → Orderfen supprimée | Chaîne Syzygie → Oathford (000123), Oathford → Tithemead (000166), **Tithemead → Orderfen (000198)**, par N010_W009. |
| 2 | Wardholt (`hamlet_wardholt`, N009_W009) au fond d'une crevasse, avec une mine | Fiche : topographie et notes. Lieu **Mine de Wardholt** (`lieu_mine_de_wardholt`, `open_dungeon_poi`) `[GD]`. Monde jouable : relief `crevasse_wardholt` et mine au fond (voir plus bas). La route 000167 depuis Oathford reste la seule. |
| 3 | Route 000006 Lhynn → Altenberg supprimée | Lhynn → Eaglewatch (000121), **Eaglewatch → Lambsfold (000199)**, **Lambsfold → Altenberg (000200)**. |
| 4 | Ambermead (`hamlet_ambermead`) de N003_W010 à N003_W008 | **Greyhedge → Ambermead (000201)**, **Ambermead → Dys (000202)**. La route 000128 vers Eaglewatch est gardée, avec un tracé par N002_W009 qui évite la case du hameau de Marn. La basse-cour, les bœufs et les chiens suivent la ferme ; le lapin reste en N003_W010. |
| 5 | Greyhedge (`hamlet_greyhedge`) de N001_W009 à N001_W008 | 000129 depuis Eaglewatch passe par N002_W009. Les chèvres, les ânes et les chiens suivent la ferme ; le molosse et le renard restent. Voir 000130 ci-dessous. |
| 6 | Route terrestre 000151 Hishtarland → Saltfen supprimée | La route maritime 000109 est gardée. Hishtarland (île, code `o`) n'est plus desservie que par les lignes 000108 et 000109. |
| 7 | La route 000085 Dys → Northind passe par Greenbough | **Dys → Greenbough (000203)**, par N005_W008 et N005_W007, et **Greenbough → Northind (000204)**. |
| 8 | Silverglade (`hameau_silverglade`, départ des Hauts Elfes) seul de N007_W006 à N007_W003 | 000127 vers Northind, 000192 vers Greenbough et 000193 vers Fernhollow sont retracées depuis N007_W003. 000193 passe par N007_W006, la case laissée libre, pour éviter Northind et le village Orkh. Les sentiers partent du hameau le plus proche : 000194 Greenbough → Autel des Vents (même case), 000195 Fernhollow → Bosquet des Pavanes, 000196 Fernhollow → Tour des Flèches blanches (même case), 000197 Greenbough → Ruines du Temple sylvestre. Pour le Bosquet et les Ruines, les deux hameaux sont à égalité : la région du lieu tranche. |
| 9 | Route Greenbough → Magistrya | **Greenbough → Sauken Glude of Magistrya (000205)**, par N005_W005 et N004_W004. |
| 10 | Verdansur (`town_0119`, N010_W015) perchée en haut d'une haute colline, deux rivières en partent | Fiche : topographie et hydrographie. Les rivières `riv_verdansur_sud` et `riv_verdansur_nord` existaient : elles naissent maintenant au pied de la colline, pas dans la ville. Monde jouable : relief `colline_verdansur` et deux `river_courses`. |
| 11 | « Événement » tout à gauche de N010_W016, au niveau des falaises, avec une route depuis Verdansur | POI **Sanctuaire des Brisants** (`poi_sanctuaire_des_brisants`, `sanctuaire`, fiche `lieu_sanctuaire_des_brisants`, `shrine_poi`) `[GD]` : croix de pierre et brasier au-dessus des brisants. Événement **Veillée des Feux des Brisants** (`fete_eglise_feux_des_brisants`, fête de `religion_eglise_lumiere`, 28e jour de la Lune des Feuilles) `[GD]` : les fidèles de Verdansur gardent les feux jusqu'à l'aube contre les naufrageurs. Le lieu est ajouté aux `lieux_de_culte`. **Verdansur → Sanctuaire (000206)**, de type `road` pour la procession. |

- **Route 000130** : elle sert toujours à rejoindre la Via Magna depuis Eaglewatch sans traverser Lhynn,
  donc elle est gardée. Depuis N001_W008, N000_W008 est une chaîne de montagnes : la route passe par
  N000_W009 et rejoint la Via Magna en S001_W009 (deuxième case après Lhynn) au lieu de N000_W010.
- **Silverglade** : `start_for` reste `["elfe"]`. `game_design.depart.cell` de `faction_hauts_elfes` vaut
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
108 villes. Deux îles sont hors du réseau terrestre : **Deepbrook** (ligne 000111, comme avant) et
**Hishtarland**, desservie par les lignes maritimes 000108 et 000109 depuis la suppression de 000151.

**Services** : 1127 → 1137 PNJ de service. Les écuries suivent les routes `road` (au moins 1) :

| Ville | Écuries |
|---|---|
| Lhynn | 4 → 3 |
| Syzygie | 4 → 3 |
| Saltfen | 3 → 2 |
| Dys | 3 → 4 |
| Magistrya | 2 → 3 |
| Verdansur | 2 → 3 |
| Eaglewatch | 3 → 4 |
| Ambermead | 1 → 3 |
| Greyhedge | 2 → 3 |
| Lambsfold | 1 → 3 |
| Tithemead | 1 → 2 |
| Greenbough | 1 → 4 |

**Monde jouable** (`world_master.json`, `tools/world_gen`) :

- **Section `reliefs`** (nouvelle). C'est un relief local ajouté au relief macro (`MacroHeight`), donc une
  fonction pure de la position : le raccord entre cellules est exact. Il est ancré sur le centre de la case
  de sa ville, et la ville y est posée, sans recherche de site.
  - `colline_verdansur` : sommet de 300 m de rayon, 130 m au-dessus du relief, pied à 1,4 km. Les deux
    cours tracés à la main `riviere_verdansur_sud` (N010_W015 → N009_W015 → N008_W015) et
    `riviere_verdansur_nord` (N010_W015 → N010_W014 → N011_W014 → N012_W014) remplacent la rivière
    automatique de ces cases. Ils naissent à 150 m au-delà du pied (`BuildMacroRivers`). Il n'y a pas
    de méandres sur le flanc, et le nord contourne Trueford.
  - `crevasse_wardholt` : massif de 1,5 km de rayon relevé de 110 m (contour ondulé, pente douce à
    l'extérieur), entaillé d'une fente à fond plat de 340 m de large, avec des parois raides de 70 m de
    large. La fente s'ouvre vers Oathford (N009_W010) et se
    ferme 260 m derrière le hameau. À 250 m, la grille des routes lisse les parois : le massif est donc
    fermé aux routes du générateur, qui n'entrent que par l'ouverture. La **mine** est un lieu procédural
    `mine` (nom « Mine de Wardholt », peuple du hameau), posée au fond de la fente par `Places.cpp`
    (`<cellule>_crevasse_wardholt_mine`) et reliée par un chemin local.
- **POI décalé** : nouvelles clés `offset_x_m` / `offset_z_m` des `pois` (au plus 4,3 km). Le Sanctuaire
  des Brisants est à 3,2 km à l'ouest et 0,7 km au nord du centre de N010_W016, au sec.
- **Cellules générées** : 182 → 188. Greyhedge (N001_W008) et Silverglade (N007_W003, en forêt sans
  rivière) sont ajoutées. Avec Silverglade en N007_W003, le corridor des départs passe par N004_W003,
  N005_W003, N006_W003 et N007_W004. N003_W004, N004_W004, N005_W004 et N005_W005 sont gardées
  explicitement : aucune cellule n'est perdue. Le corridor ne change pas autour de Wardholt.
- **Tests** : `world_gen_tests` gagne « reliefs » (sommet, rivières au pied, crevasse fermée, routes par
  l'ouverture, mine au fond, POI décalé).

**Ce qui n'est pas réel en jeu** :

- N010_W015 (Verdansur) et N010_W016 ne sont pas dans `generated_cells` (ajoutées en v1.0.78, §16.7). La colline, les rivières et le
  sanctuaire sont générés : carte du monde, `places.json`, et cellules dès qu'elles seront ajoutées. Ils
  ne sont pas jouables tant que ces cases sont derrière le mur invisible.
- Le générateur ne modèle pas de falaises : la côte y descend en pente douce.
- La Veillée des Feux des Brisants n'existe que dans le lore : il n'y a pas de système d'événements de
  monde côté serveur.

**Points incertains** :

- Les noms (Mine de Wardholt, Sanctuaire des Brisants, Veillée des Feux des Brisants) et l'événement.
- La hauteur et l'emprise de la colline et de la crevasse.
- Ambermead et Greyhedge quittent la région 3×3 d'Eaglewatch, dont elles restent les fermes.
- Hishtarland n'a plus de route terrestre.
- Les sentiers à égalité de distance.

### 16.2 Villes moyennes et raid de la mine (v1.0.73)

**Décisions du propriétaire (10/11/2026)**. Chaque fiche touchée porte une note `[DÉCISION] (v1.0.73)`.
6 entrées sont ajoutées au registre `regle_canon_registre_decisions` : 3 villes, la mine, le raid, et une
entrée pour les routes. Noms, races et factions sont `[GD]` et restent à valider. Scripts reproductibles :
`master_v1073.py` puis `lore_v1073.py`, appliqués aux fichiers v1.0.72, puis
`tools/lore/generate_city_services.py`.

**Trois villes moyennes**. Elles ont la taille « ville » de la grille `game_design.cities_services`
(services complets, comme Justfall). Chacune a une fiche `villes`, une entrée `cities` (`kind` « ville »)
dans `world_master.json` et une cellule générée. Le site choisi par le générateur est sec : pas de
rivière à moins de 300 m.

| Ville | Case | Race, faction `[GD]` | Type | Pourquoi |
|---|---|---|---|---|
| **Hazelgate** (`town_hazelgate`) | N007_E002 | Elfes des bois (`race_elfe_des_bois`, avec quelques marchands humains), neutre | `market_town` | Comptoir de lisière : la lisière de la forêt elfique est la strate des Elfes des bois `[HS]`. Les humains y commercent sans entrer dans la forêt. |
| **Fellwatch** (`town_fellwatch`) | N009_W001 | Humains, Empire (`faction_empire`, `empire_hynn` au master) | `border_town` | Terres des Gigantomorphes : l'Empire en repousse les géants et ceux-ci restent le danger des marches `[HS]`. Ville de colons, de bûcherons (le bois de la région va à la reconstruction de Kendrhir) et de guet face aux géants. Elle ne coupe rien côté forêt elfique, dont les elfes rasent les villages de colons `[BD-A4]`. |
| **Hollowgate** (`town_hollowgate`) | S001_W007 | Humains, Empire | `mining_town` | Plaine des Terres de l'Empereur traversée par la Via Magna (000120), dont elle devient une étape. Ville des mineurs de la Mine de la Traverse et des rouliers. |

- **N007_E002 plutôt que N007_E001** `[INFÉRÉ]`. Ce sont deux lisières sans rivière, libres. Sur le relief
  macro du générateur, N007_E002 est plus douce (écart type 18 m contre 21 m, pente moyenne 12 m contre
  14 m par 250 m). Elle est aussi plus près de l'eau : le lac N007_E005 (Mont des Neuf Pics) et la
  rivière N009_E005 sont à trois cases (quatre pour N007_E001). Enfin, Arcanefield et Ashwick sont toutes deux à
  trois cases (Ashwick est à quatre cases de N007_E001).
- `mining_town` et `border_town` n'ont pas de taille par défaut dans la grille : le champ `taille` vaut
  « ville » dans les trois fiches.
- PNJ de Hazelgate : `RACE_VILLES` de `generate_city_services.py` (3 Elfes des bois pour 1 humain) et
  syllabes `NOMS` de `race_elfe_des_bois` (nouvelles).

**Raid de la mine**. Lecture retenue de la demande, **à confirmer** : Greyhedge (N001_W008) et Hollowgate
(S001_W007) ne sont pas voisines. La mine est donc une galerie d'environ 14 km creusée sous N000_W008
(chaîne de montagnes), avec une entrée côté Greyhedge et une entrée côté Hollowgate. Le raid la traverse
d'une entrée à l'autre.

- Lieu **Mine de la Traverse** (`lieu_mine_de_la_traverse`, `open_dungeon_poi`, N000_W008) `[GD]` :
  ancienne mine de fer impériale qui perçait la chaîne, abandonnée quand une couvée d'araignées géantes
  s'y est installée.
- Raid **La Mine de la Traverse** (`raid_mine_de_la_traverse`) : `min_teams` 1, `min_players_per_team` 5,
  `difficulty` 2, `location_ville_id` Hollowgate, toutes factions. Titre « Passe-Galerie » `[GD]`.
- Niveau conseillé 10 à 15 : c'est l'échelle des départs de la Première Province et des Marches (1-15,
  `regle_bestiaire_echelles`). Les créatures existantes sont réutilisées : `animal_rat` (variante « rat des
  profondeurs ») et `animal_araignee` (« araignée géante »). Le boss est nouveau : **Mère des galeries**
  (`creature_mere_des_galeries`, `boss`, niveau 15, `raid_id`) `[GD]`. Les trois sont dans les
  `creature_ids` de N000_W008.
- Monde jouable : deux POI de genre `salle` (comme le Puits des Apprentis). `poi_mine_traverse_nord` est en
  N001_W008, décalé de 1,5 km à l'ouest et 3,6 km au sud, au pied de la chaîne, à 4,4 km de Greyhedge.
  `poi_mine_traverse_sud` est en S001_W007, décalé de 4 km à l'ouest et 4 km au nord, à 5,8 km de
  Hollowgate. Le générateur ne sait poser une mine à une ville qu'au fond d'une crevasse (`reliefs`,
  v1.0.72) : la mécanique n'est pas branchée, pour ne pas mettre Greyhedge ni Hollowgate dans une
  crevasse.

**Routes** (000207 à 000213) :

| Route | Type | Tracé |
|---|---|---|
| 000207 Hazelgate → Arcanefield | `road` | N007_E002 → N006_E002 → N005_E002 → N004_E001 |
| 000208 Hazelgate → Ashwick | `road` | N007_E002 → N008_E003 → N009_E004 → N010_E005 |
| 000209 Fellwatch → Hazelgate | `road` | N009_W001 → N009_E000 → N008_E001 → N007_E002 (hors forêt elfique) |
| 000210 Fellwatch → Hayfen | `road` | N009_W001 → N010_W002 → N010_W003 → N011_W004 (par la côte, hors forêt elfique) |
| 000211 Hollowgate → Scalebrook | `road` | S001_W007 → S002_W007 → S003_W008 |
| 000212 Greyhedge → Mine de la Traverse | `path` | N001_W008 → N000_W008 |
| 000213 Hollowgate → Mine de la Traverse | `path` | S001_W007 → N000_W008 |

Hollowgate est aussi une étape de la Via Magna (000120). Le contrôle du réseau (`routes/reseau.py`,
`MAXLEN` = 5, en lecture seule) compte 113 villes et hameaux et 159 routes. Aucune route n'est trop
longue ni ne traverse la mer ou une chaîne. La composante principale réunit 111 villes, dont les trois
nouvelles. Seules Deepbrook et Hishtarland restent hors du réseau terrestre, desservies par leurs lignes
maritimes, comme en v1.0.72.

**Services** : 1137 → 1195 PNJ de service. Hazelgate en compte 17, Fellwatch 19 et Hollowgate 18. Une
écurie de plus pour Arcanefield, Ashwick, Hayfen et Scalebrook (une route `road` de plus chacune).

**Cellules générées** : 188 → 191 (N007_E002, N009_W001, S001_W007). Le corridor des départs ne change pas
et aucune cellule n'est perdue. L'entrée nord est dans N001_W008, déjà générée. Test `world_gen_tests` :
« villes v1.0.73 » (villes moyennes sur cellule générée, au sec, reliées par le générateur ; entrées de la
mine décalées vers la chaîne, au sec, à l'écart des villes).

**Ce qui n'est pas réel en jeu** :

- La galerie et le raid : le monde jouable n'a que les deux POI d'entrée. Il n'y a ni instance, ni
  souterrain, ni boss, ni récompense côté client ou serveur. Le passage souterrain ouvert après le raid
  n'existe que dans le lore.
- Les routes du lore : `tools/world_gen` trace ses propres routes entre les villes du master. Par exemple,
  il relie Fellwatch à Silverglade et Hollowgate à Greyhedge, par la surface.
- Le générateur ne peut pas relier Fellwatch à Deepbrook (île). L'avertissement « route impossible » vers
  Deepbrook vient maintenant de Fellwatch au lieu de Whispers Hill ; celui d'Ashwick reste.

**Points incertains** :

- La lecture « galerie sous N000_W008 » et la longueur de la galerie.
- Les noms, races et factions des trois villes ; Elfes des bois neutres plutôt que Hauts Elfes.
- Le niveau du raid et du boss ; les récompenses.
- La route 000210 passe par N010_W003, une case du marais maudit autour de Hayfen.

### 16.3 Île du Nord : Île de la Trêve (v1.0.74)

**Décisions du propriétaire (10/11/2026, lot 3)**. Ce qu'il a fixé porte `[DÉCISION] (v1.0.74)` : le terrain,
la région à part entière commune à toutes les factions (Légion comprise) de niveau 45 à 60, les quatre cases de
villes, la ville-port de N017_W010, les ports avec ponton de Hayfen et d'Orderfen, la route maritime et les
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
191 → 245, aucune perdue. Hishtarland et Deepbrook (îles `o` ou haut-fond) ne sont pas générées et n'offraient
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
| **Brinehaven** (`town_brinehaven`) | N017_W010 | ville, `port_town` | humains et toutes races (PNJ) | neutre | port franc, départ de la ligne maritime, quartiers par bloc |
| **Duskmoor** (`town_duskmoor`) | N022_W010 | village, `fortress_town` | humains, Elfes noirs, Dzoraks, démons | Lune Noire | comptoir du bloc de la Lune Noire (Légion comprise) |
| **Truceholm** (`town_truceholm`) | N019_W010 | hameau, `hamlet` | humains | neutre | îlot, Pierre de la Trêve, raid multi-équipe |
| **Lanternhold** (`town_lanternhold`) | N021_W014 | village, `fortress_town` | humains | Empire | comptoir du bloc de l'Empire (Lumière, Justice, Serpent) |

Les blocs suivent `game_design.faction_relations`. Services : 1195 → 1244 PNJ (Brinehaven 22, Lanternhold 11,
Duskmoor 8, Truceholm 6 ; un capitaine de port de plus à Hayfen et Orderfen). Brinehaven a une table de races
mêlées (`RACE_VILLES`) ; Lanternhold a un temple garanti (`ajustements` de la grille : chapelle des Ordres).

**Ports et liaisons**. Convention des ports existants : service `port` (`grille.ports` : Brinehaven, Truceholm,
Hayfen, Orderfen), champ `port` de la fiche (`ponton_cell` dans la case de la ville, `vers_cell` côté mer),
POI `port` au master (décalé au bord de l'eau : `poi_port_brinehaven`, `poi_port_truceholm`,
`poi_port_hayfen`, `poi_port_orderfen`), ligne `sea_lines` et routes `sea_route` avec `sea_line_id`.

| Route | Type | Tracé |
|---|---|---|
| 000214 Brinehaven → Camp du Cap | `road` | N017_W010 → N017_W011 → N017_W012 → N017_W013 |
| 000215 Camp du Cap → Lanternhold | `road` | N017_W013 → N018_W014 → N019_W014 → N020_W014 → N021_W014 |
| 000216 Lanternhold → Duskmoor | `road` | N021_W014 → N022_W013 → N022_W012 → N022_W011 → N022_W010 |
| 000217 Lanternhold → Tertre des Rois Noyés | `path` | N021_W014 → N020_W013 → N019_W013 |
| 000218 Lanternhold → Grottes des Naufrageurs | `path` | N021_W014 → N020_W014 → N019_W014 → N018_W014 |
| 000219 Duskmoor → Tanière du Jarl de Givre | `path` | N022_W010 → N022_W011 → N022_W012 |
| 000220 Truceholm → Gouffre de la Baie | `path` | N019_W010 → N020_W011 |
| 000221 Brinehaven → Hayfen | `sea_route` (ligne du Nord) | N017_W010 → N016_W009 → N015_W008 → N014_W007 → N013_W006 → N012_W005 → N011_W004 |
| 000222 Hayfen → Orderfen | `sea_route` (ligne du Nord) | N011_W004 → N012_W005 → N012_W006 → N012_W007 → N011_W008 |
| 000223 Brinehaven → Truceholm | `sea_route` (navette) | N017_W010 → N018_W010 → N019_W010 |

- Ligne `ligne_maritime_du_nord` (`rule_ligne_maritime_du_nord`) : Brinehaven ↔ Hayfen ↔ Orderfen, aller-retour,
  2 bateaux en positions opposées, pause de 30 s, eau navigable uniquement (comme la ligne du sud).
- **Îlot : navette plutôt que gué** (`navette_brinehaven_truceholm`, 1 bateau, comme Ashwick – Deepbrook). La
  case de haut-fond N018_W010 sépare l'îlot de Brinehaven : dans le générateur, environ 14 m d'eau sur 4 à 5 km.
  Un gué demanderait de changer le terrain fixé par le propriétaire.
- Le Camp du Cap (`lieu_camp_du_cap`, N017_W013) est un relais : la route directe Brinehaven → Lanternhold
  ferait six cases (au-delà de la limite de cinq du contrôle du réseau).
- Contrôle du réseau (`routes/reseau_check.py`, `--mer` pour compter les liaisons maritimes) : 117 villes et
  hameaux, 169 routes, aucune anomalie ; une seule composante avec les liaisons maritimes. Sans elles,
  l'île (Brinehaven, Duskmoor, Lanternhold), Truceholm, Deepbrook et Hishtarland sont hors du réseau terrestre.

**Raids et lieux** (lieux du lore et POI du master, sur la forêt de l'île) :

| Raid | Équipes | Niveau | Lieu, POI | Boss `[GD]` | Ville |
|---|---|---|---|---|---|
| `raid_tertre_des_rois_noyes` Le Tertre des Rois Noyés | 1 × 5 | 45-50 | N019_W013, `ruine` | Roi noyé (50) | Lanternhold |
| `raid_grottes_des_naufrageurs` Les Grottes des Naufrageurs | 1 × 5 | 50-55 | N018_W014, `salle` (côte sud-ouest) | Capitaine Marée-Rouge (54) | Brinehaven |
| `raid_taniere_du_jarl` La Tanière du Jarl de Givre | 1 × 5 | 55-60 | N022_W012, `salle` (falaise nord) | Jarl de Givre (58) | Duskmoor |
| `raid_devoreur_des_brumes` Le Dévoreur des Brumes | 3 × 5, factions mêlées | 60 | N020_W011, `gouffre` (îlot) | Dévoreur des Brumes (boss mondial, 60) | Truceholm |

Raid multi-équipe : `for_factions` vide (toutes) ; les équipes doivent venir d'au moins deux blocs (note
`[GD]`, le schéma n'a pas de champ pour cela). Créatures : réutilisées, `creature_geant_de_froid` (45-55, bras
nord, cohérent avec §5.1) et `creature_spectre` (40-52, tertre) ; nouvelles `[GD]`, `creature_loup_des_brumes`
(45-50, toute la forêt), `creature_naufrageur` (46-52) et les quatre boss. Les `creature_ids` des cases suivent
les `zones_chasse`.

**Quêtes entre factions** `[GD]` (`quests`, `region_id` de l'île) :

| Quête | Factions | Relation | Niveau |
|---|---|---|---|
| `quest_ile_gd_treve_des_brumes` La Trêve des Brumes | toutes | coopération (escorte des envoyés à Truceholm) | 45 |
| `quest_ile_gd_bois_de_marine` / `quest_ile_gd_coupes_noires` | bloc Empire / bloc Lune Noire | affrontement (quête et contre-quête) | 47 |
| `quest_ile_gd_ambre_des_geants` / `quest_ile_gd_ambre_elfique` | Nains et Chevaliers-Dragons / Hauts Elfes | coopération et affrontement | 50 |
| `quest_ile_gd_ames_des_noyes` / `quest_ile_gd_repos_des_noyes` | Légion infernale et Lune Noire / Lumière et Justice | coopération et affrontement | 52 |
| `quest_ile_gd_chant_du_devoreur` Le Chant du Dévoreur | toutes | coopération, accès au raid multi-équipe | 55 |

**Ce qui n'est pas réel en jeu** :

- Les raids, les quêtes, la trêve et la formation d'équipes mêlées : seuls les POI d'entrée existent dans le
  monde jouable ; ni instance, ni boss, ni récompense côté client ou serveur.
- Les bateaux : aucune ligne maritime n'est jouée ; les POI `port` ne posent pas de ponton. L'île n'est
  accessible qu'en lore.
- Les routes du lore : le générateur relie Brinehaven à Duskmoor et Duskmoor à Lanternhold par la forêt ;
  Truceholm n'a pas de route (« route impossible » vers Brinehaven, Duskmoor et Lanternhold), comme Orderfen
  → Brinehaven (la plus courte liaison entre l'île et le continent passe par la mer).

**Points incertains** :

- Les noms (région, villes, lieux, raids, créatures), les tailles et le contrôle des villes.
- « La Légion » : Légion infernale retenue, Légions Impériales aussi admises.
- L'ordre des escales et le nombre de bateaux de la ligne du Nord.
- Le port d'Orderfen : sa case ne touche pas l'eau dans le générateur ; le POI est au bord nord de la case,
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
| 1 | Orderfen – Hayfen passe dans la mer | 000095 : N011_W008 → N010_W007 → N010_W006 → N010_W005 → N011_W004, par la forêt et le marais au sud de la côte. |
| 2 | Tracer 000064, 000073, 000076 | **Lhynn → Dustfen** : N001_W010 → N000_W010 (sortie commune avec la Via Magna) → S001_W010 → S002_W011. **Dustfen → Clearfen** : S002_W011 → S003_W012 → S003_W013 (Port Franc, déjà desservi par la ligne droite) → S004_W014 → S004_W015, par la plage. **Ashwick → Stonehaven** : N010_E005 → N010_E006 → N011_E007 → N012_E007 → N013_E007 → N014_E008, par la lisière, en retrait de la côte. |
| 3 | Sentier dans le sable Harrowbrook – Equiton | **000224** (`path`) : S006_W008 → S006_W007 → S006_W006 → S006_W005 (plage) → S005_W004. |
| 4 | Village fantôme – forteresse naine, branche sur 000079 | Village Fantôme du Marais (`ville_village_fantome`, S003_E006) et Forteresse Naine d'Heureux (`ville_forteresse_naine_heureux_t11`, S006_E004). L'ancienne 000155 passait par la montagne (S005_E004). **000155** devient l'embranchement de 000079 Coldport → Frosthollow en **S007_E006** : S007_E006 → S006_E005 → S006_E004. Le village rejoint 000079 à Frosthollow par 000149 (déjà tracée). 000079 reçoit ses `waypoints` (S009_E004 → S008_E005 → S007_E006 → S006_E007 → S005_E008). |
| 5 | Réajuster Frosthollow – Der Hem Shelbem (000074) | La ligne droite franchissait la chaîne (Deathtrap Canyon). Nouveau tracé par le col entre les deux chaînes : S010_E010 → S009_E009 → S008_E008 → S007_E008 → S006_E008 → S005_E008. C'est aussi le passage du générateur. |
| 6 | Der Hem Shelbem S009_E010 → S010_E010 | Fiche, `cities` du master, lieu `lieu_der_hem_shelbem` (nécropole) et ses 7 espèces (morts-vivants, Shedu, roi-momie : `creature_ids` des cases et `zones_chasse`) ; extrémités de 000019, 000080, 000152 (tracés droits, au sec). |
| 7 | E020 : désert et zone rocheuse | S001_E020 à S003_E020 : `desert` (`~`). S004_E020 : **`arid_land`** (`:`), voir plus bas. |
| 8 | Supprimer Dunfold → N015_E015 | 000146 Dunfold → Northgate descendait de Dunfold à N015_E015 puis remontait. Le tronçon Dunfold → N015_E015 est supprimé, ainsi que le bout N015_E015 → N016_E016 qui ne menait plus nulle part. **000146** part de 000116 Moorgate → Dunfold en **N016_E016** : N016_E016 → N017_E017 → N018_E018. 000116 reçoit ses `waypoints`. |
| 9 | Règles des sentiers | Voir plus bas. |
| 10 | Bridlecrag S004_W005 → S004_W006, Cragfold S004_W003 → S004_W002 | Fiches et `cities` ; routes 000180 et 000179 allongées par l'ancienne case ; animaux domestiques déplacés (marmottes et corbeaux en S004_W005, la marmotte reste en S004_W003). Les deux hameaux sortent de la région 3×3 de Wyrmhallow (§15.5), qui ne change pas, comme Ambermead et Greyhedge en v1.0.72. |
| 11 | Ruines du prieuré → Brightholm | 000140 (Dawnmere) supprimé ; **000225** Brightholm → Ruines du prieuré (`path`, cases voisines). |
| 12 | Greyhedge – Ambermead devient un sentier | 000201 : `type` `path`. |

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
| 000131 | Camp de manœuvres des Légions → Eaglewatch | double 000199 | supprimé |
| 000133 | Ruines de la villa tharque → Eaglewatch | double 000128 et 000129 | supprimé |
| 000169 | Lices des novices → Oathford | double 000166 (lices dans la case de Tithemead) | supprimé |
| 000170 | Camp des parjures → Oathford | lieu sur 000198 | supprimé |
| 000139 | Chapelle de l'ermite → Dawnmere | lieu sur 000052 Dys → Justfall | supprimé |
| 000175 | Arène des Jeunes-Sangs → Grakhul | double 000125 | supprimé |
| 000177 | Affût de la Grande Chasse → Grakhul | double 000142 | supprimé |
| 000197 | Ruines du Temple sylvestre → Greenbough | double 000193, puis le quitte | supprimé |
| 000218 | Grottes des Naufrageurs → Lanternhold | double 000215 | supprimé |
| 000219 | Tanière du Jarl de Givre → Duskmoor | double 000216 | supprimé |
| 000138 | Calvaire des pèlerins → Dawnmere | croise 000022 Justfall → Altenberg | raccourci : **Altenberg** → Calvaire |
| 000141 | Camp des naufrageurs → Dawnmere | croise 000200 Lambsfold → Altenberg | raccourci : **Lambsfold** → Camp |

Les tracés passent par les centres des cases : la nouvelle extrémité d'un sentier raccourci est le sommet de la route
hôte le plus proche du croisement. Pour 000022 et 000200, ce sont leurs extrémités. Pour 000141, Lambsfold et
Altenberg sont à égalité ; la ferme de la région de départ est retenue. Les autres sentiers ne doublent aucune route
et n'en croisent aucune. Les nouveaux sentiers 000224, 000225 et 000201 non plus.

**Réseau** (`routes/reseau.py --dry` et `reseau_check.py`, `MAXLEN` = 5). Avant : 117 villes et hameaux, 169 routes.
Après : **160 routes**, aucune anomalie. Une seule composante avec les liaisons maritimes. Sans elles, l'île de la
Trêve, Truceholm, Deepbrook et Hishtarland restent hors du réseau terrestre, comme en v1.0.75. `reseau.py` ignore
les jonctions (extrémité sans ville) et voit la Forteresse d'Heureux isolée. `reseau_check.py` (v1.0.76) raccorde
une jonction aux villes de sa route hôte : la forteresse est dans la composante principale.

**Services** : 1244 → 1241 PNJ de service. Une écurie de moins pour Dunfold (5 → 4 : 000146 n'y arrive plus),
Ambermead et Greyhedge (3 → 2 : 000201 devient un sentier).

**Monde jouable** (`world_master.json`) : villes déplacées, grille (`~` en S001_E020 à S003_E020, `:` en S004_E020),
`generated_cells`. S004_W006 et S004_W002 sont ajoutées. Avec Bridlecrag en S004_W006, le corridor des départs passe
par S004_W007 et non plus par S005_W008 et S006_W004 à S006_W008. Ces six cases sont gardées explicitement : aucune
n'est perdue. Cellules générées : **245 → 248**. S010_E010 l'était déjà. Test `world_gen_tests` « corrections v1.0.76 »
(villes déplacées sur cellule générée, au sec, reliées ; codes de la colonne E020).

**Ce qui n'est pas réel en jeu** :

- Les routes et sentiers du lore. `tools/world_gen` ne lit pas la collection `routes` ni les `waypoints` : il trace
  ses propres routes entre les villes du master (Kruskal, 2 plus proches voisines, A* sur le relief). Le jeu ne peut
  donc pas suivre ces tracés sans un changement du générateur, par exemple une section de routes imposées dans le
  master. Il relie déjà Der Hem Shelbem à Frosthollow par le même col. Il relie aussi Bridlecrag à Harrowbrook,
  Scalebrook et Equiton.
- Les sentiers et les jonctions n'existent que dans le lore et l'éditeur.

**Points incertains** :

- La lecture du point 4 : la liaison village – forteresse emprunte 000149 et 000079, avec 000155 en embranchement.
  Pas de route directe du village jusqu'à 000079, qui doublerait 000149.
- La lecture du point 8 : le bout N015_E015 → N016_E016 est aussi supprimé, et le reste de 000146 est raccroché à
  000116.
- `arid_land` pour « zone rocheuse ».
- Bridlecrag est sur les neiges éternelles (S004_W006). Le Grand Ancien (`creature_dragon_grand_ancien`, 66-70)
  chasse dans cette case, à côté du haras, hors de la région de départ ; rien n'est retiré.
- Aucune « Porte du Sud » n'existe dans le lore ni dans le master. Seuls le Palais des Maîtres des Dragons
  (S003_W004, inchangé, toujours relié par 000124 et 000082) et Equiton ont été vérifiés.
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
N020     ^    T         ^    T        D, L, B, t = Duskmoor, Lanternhold, Brinehaven, Truceholm
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
| `massif_n021_w013` | N021_W013 | 480 m, 6,5 km | 1,5 km au nord | pic au-dessus de Lanternhold |
| `massif_n018_w013` | N018_W013 | 480 m, 6,5 km | 1 km O, 2 km S | pic sud du bras ouest |
| `massif_n017_w012` | N017_W012 | 480 m, 6,5 km | 2,5 km au sud | crête du bras sud, falaises sud |
| `massif_n020_w011` | N020_W011 | 380 m, 5 km | 1,5 km à l'ouest | butte de l'îlot |
| `massif_n019_w014`, `_n022_w011`, `_n017_w011`, `_n018_w014` | côtes O, N, S, SO | 240 à 280 m, 4,5 km | 2,5 à 3 km vers la mer | falaises côtières |

Sonde du relief macro : sommets de 470 à 570 m au-dessus de la mer sur les 7 cases de montagne (moyenne 140 à 180 m),
10 à 13 % de pentes au-delà de 25° ; vallées et cols entre 30 et 80 m en moyenne. Les sites des quatre villes ne changent
pas (même position, au sec, écart de 10 m au bord du plateau) ; les routes du générateur non plus (Brinehaven –
Duskmoor, Duskmoor – Lanternhold ; Truceholm sur l'îlot reste sans route, comme en v1.0.74). Avertissements du
générateur identiques à la v1.0.76. Test `world_gen_tests` « relief v1.0.77 » : côte inchangée (mêmes terres émergées
avec et sans massifs), sommets, falaises, villes, temples. Rendu de la carte du générateur : `ile_relief_v1077.png`.

**Peuple** `[GD]` : les **Huacani**, rameau humain descendant du peuple de la mer dont les rois reposent dans le Tertre des
Rois Noyés ; quand la mer a pris les cités basses, ils sont montés sur les crêtes. Fiches : race `race_huacani` (PNJ
seulement, comme `race_elfe_des_bois`), faction non jouable `faction_huacani` (environ 1 000 âmes), culte
`religion_huacani_soleil` « Le Soleil des Cimes » (`lore.religions`, convention v1.0.58 : Soleil des Cimes, Lune
d'Argent, Mère des Eaux, esprits des sommets ; les prêtres de la Brume gardent le Dévoreur endormi ; fêtes à placer dans
le calendrier). **Trêve** : les Huacani en sont les plus anciens garants ; la Pierre de la Trêve de Truceholm est une de
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
Lanternhold → Temple des Cimes (N021_W014 → N021_W013) et **000227** Camp du Cap → Temple de la Lune d'Argent
(N017_W013 → N018_W013). Contrôle (`reseau_check.py --mer`) : 117 villes et hameaux, 162 routes, une composante, aucune
anomalie ; `reseau.py --dry` donne les mêmes propositions qu'en v1.0.76.

**Services** : inchangés (1241 PNJ, aucune ville ajoutée). Cellules générées : inchangées (248).

**Ce qui n'est pas réel en jeu** :

- Les temples : aucun modèle 3D de temple inca n'existe. Les 11 POI (`temple`, `sanctuaire`, genres existants) ne sont
  que des repères (nom sur la carte et le radar) ; ni édifice, ni terrasse, ni escalier ne sont posés.
- Les Huacani, leurs PNJ, dialogues et quêtes, la paix des sanctuaires : lore seulement (aucun PNJ placé, aucun spawner).
- Les sentiers du lore et les liens raid – temple : seuls les POI d'entrée existent, comme en v1.0.74.
- Le relief est réel (heightmaps des cellules après régénération) ; la forêt y reste celle du code `T` (conifères
  au-dessus de 300 m, pas d'arbre au-delà de 34° de pente).

**Points incertains** :

- Les noms (peuple, culte, temples, PNJ, quêtes), les tailles et le nombre d'habitants.
- Le peuple : rameau humain (nouvelle fiche de race PNJ) plutôt que race existante.
- Les hauteurs des massifs et la part des cases en `mountain` ; l'écart lore `mountain` / grille `T`.
- Le rattachement des raids : lieux de raid gardés à côté des temples plutôt que fusionnés avec eux.

### 16.7 Cellules de Verdansur et des Brisants, décisions du 11/11 (v1.0.78)

Lot 7 du propriétaire (11/11/2026).

**Cellules N010_W015 et N010_W016.** Verdansur (`town_0119`, colline de 130 m d'où partent les deux rivières) et le
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
  385 bâtiments, 952 spawners. Rendu : sommet de la colline sous Verdansur (191 m), rivière sud qui part du pied de
  la colline, cours nord-est vers N010_W014, mer au bord ouest de N010_W016, sanctuaire au sec à moins de 2 km du rivage.
- Test `world_gen_tests` « cellules v1.0.78 » (cases dans `generated_cells`, Verdansur en N010_W015, sanctuaire
  dans les lieux de N010_W016).

**Décisions inscrites au registre** (`regle_canon_registre_decisions`, 5 entrées v1.0.78, une par fiche) :

| # | Décision du propriétaire | Fiches |
|---|---|---|
| 1 | Du contenu (quêtes, raids, monstres) de plus haut niveau peut exister dans une zone de niveau inférieur. Les chemins doivent être bien balisés pour faire comprendre au joueur de faible niveau qu'il entre dans une zone où les adversaires sont plus forts. But : obliger à visiter tout le monde. Exemple : Bridlecrag (départ 1-10) partage sa case S004_W006 avec le Grand Ancien (66-70) ; les deux restent en place. | `rule_niveaux_par_faction`, `regle_bestiaire_echelles`, cellule `S004_W006` |
| 2 | Le nom « lac / fleuve de Syzygie » est conservé. `riv_fleuve_sysigie` (nom provisoire « [à nommer] » depuis la v1.0.55) devient « Fleuve de Syzygie », dans le lore et dans `river_courses` du master (champ `name`, non utilisé par le générateur). | `riv_fleuve_sysigie` |
| 3 | Le niveau d'une zone est relatif à la faction : une zone de faible niveau pour une faction ne l'est pas forcément pour les autres. Principe seulement : le détail par faction sera défini plus tard ; à partir du niveau 20 du joueur, beaucoup de choses changent. | `rule_niveaux_par_faction` |

La décision 1 assouplit la règle « pas de dragon adulte en zone débutant » appliquée en v1.0.68 (§15.5) et le point incertain de la v1.0.76 (§16.5) : le
Grand Ancien reste en S004_W006, déjà dans ses `zones_chasse` et les `creature_ids` de la case. Aucun niveau,
aucune créature et aucune case ne changent.

**Services** : inchangés (1241 PNJ). Aucun identifiant supprimé du lore.

**Ce qui n'est pas réel en jeu** :

- Les deux cases sont générées mais isolées : quatre cases non générées (N010_W012 à N010_W014) les séparent des
  autres cellules jouables. On n'y arrive pas à pied tant que le corridor n'est pas étendu.
- La route 000206 Verdansur → Sanctuaire n'existe que dans le lore : le générateur ne relie pas les POI par des routes.
  Le sanctuaire n'est qu'un repère (aucun modèle de croix ni de brasier), et la côte descend en pente douce (pas de
  falaise modélisée).
- La Veillée des Feux des Brisants : lore seulement (pas de système d'événements de monde côté serveur).
- Le balisage des chemins vers les zones plus fortes (décision 1) et les niveaux par faction (décision 3) : règles
  du lore, sans mécanisme en jeu.

**Points incertains** :

- Raccorder Verdansur au réseau jouable (cases N010_W012 à N010_W014, ou corridor vers Syzygie) : à décider.
- La forme du balisage (panneaux, avertissements de PNJ, couleur du nom de zone) et le seuil d'avertissement.

## 17. Questions ouvertes

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
- Noms des fleuves, rivières et lacs : à donner par le propriétaire. Le fleuve et le lac de Syzygie sont nommés
  (v1.0.78, §16.7).
- Forteresse naine d'Heureux (S006_E004) : la zone `zone_forteresse_naine_abandonnee_t9` la disait
  « proche de Moork » (S002_E015), sans appui BD.
