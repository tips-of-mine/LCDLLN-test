# Lore et bande dessinée — confrontation tome par tome

Ce document trace les corrections apportées au fichier de lore
`legacy/world-builder-html/lune-noire-data-v20.json` (et à sa copie identique
`lune-noire-data.json`) après lecture des albums *Les Chroniques de la Lune Noire*,
ainsi que les décisions de placement reportées dans le monde jouable
(`game/data/world/royaumes/world_master.json`). Il complète
[`ROYAUMES.md`](ROYAUMES.md), qui décrit le générateur du monde.

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

## 5. Plans de référence

Plans vus du dessus (repères pleins = BD, en pointillés = lore placé) publiés comme
documents de conception : Kendrhir (en reconstruction), Feyhin Lokcthat, Magistrya,
puis Mont des Neuf Pics, l'Atmosphère, le palais du Maître des Dragons, Altenberg et
l'Oracle (extérieur) ; pour le T3 : Ghrunke Dash, Lhynn, le village fantôme (village et
marais) et le fief de Garundel.

## 6. Questions ouvertes

- Compagnon `companion_pilou` du T5 (« petit goblinoïde ») : contredit les T1-T3, à relire
  dans ce tome.
- Lhynn : la BD-T3 la montre sur une presqu'île dans un grand lac cerné de montagnes,
  alors que sa cellule (N001_W010) est une plaine près de la côte.
- Identité de « l'amant » vengé par la succube (pl.31) : non nommé.
- Sort de Feidreiva après les « brumes du temps » : à suivre dans les tomes suivants.
- Race `race_half_dark_elf` utilisée par des quêtes et compétences mais absente de `races`.
- Escaliers de l'Oracle : rampe et escalier de briques (BD-T2) contre huit escaliers
  blancs (lore, BD-T3).
- Palais du Maître des Dragons « au-delà des sept régions » (p.29) mais visible depuis le
  pont de la Passe (p.42) : hyperbole ou second palais.
