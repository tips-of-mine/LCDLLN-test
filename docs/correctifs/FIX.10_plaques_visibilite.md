# FIX.10 — Plaques de nom : moins d'informations à l'écran

Retour joueur du 29/10/2026 (captures « Hameau de Marn » et prairie) : plaques de
« Lapin des pres » visibles à des centaines de mètres, dizaines de sangliers / loups
nommés à l'arrivée au hameau, noms « Coffre », « Caisse en bois », « Caisse metal »
partout.

## Règles (client)

Champ de vision = **Perception du personnage** (distance horizontale), reprise du classeur
de game design `LCDLLN_Faction_Races_Classes.xlsx` (onglets « Paramètres » et « Tableau ») et
transcrite dans `game/data/gameplay/perception.json` :

```
Perception (m) = (10 + 0,5 × (niveau − 1)) × M profil(classe) × M race × M sexe   (arrondi 0,1)
```

| Profil (classe du jeu) | M | Race | M |
|---|---|---|---|
| melee (warrior) | 0,9 | humains | 1 |
| sacre (paladin) | 0,95 | nains | 0,9 |
| pisteur (hunter) | 1,25 | orcs (Orkh) | 0,9 |
| voleur (rogue) | 1 | elfes | 1,25 |
| healer (priest) | 1 | demons | 1,05 |
| lanceur (mage, necromancer, shaman) | 1,15 | morts_vivants, divins | 1 (absents du classeur) |

Les multiplicateurs de sexe de la perception valent tous 1 dans le classeur (gardés dans le
fichier pour pouvoir les ajuster). Exemples : guerrier humain niv. 1 = 9 m, niv. 50 = 31,1 m,
niv. 100 = 53,6 m ; mage elfe niv. 1 = 14,4 m, niv. 100 = 85,5 m. Valeurs vérifiées contre
le classeur par `perception_model_tests`.

**Cœur du cercle** (`half_circle_ratio` = ½ rayon, onglet « Code couleur ») : le **niveau**
d'une créature n'est affiché qu'à cette distance.

Repli si `perception.json` manque : `client.nameplate.vision_distance_m` (40 m).

| Entité | Plaque affichée |
|---|---|
| Animal (`"category": "animal"` dans `creatures/archetypes.json`) | seulement s'il est **sélectionné** et dans le champ de vision |
| Mob hostile (bandit, pillard, golem…) | seulement s'il est **sélectionné** et dans le champ de vision |
| PNJ d'ambiance (villageois, voyageur, garde…) | dans le champ de vision |
| Chasseur (`client.nameplate.animal_sense_classes`, défaut `hunter`) | voit en plus les animaux du champ de vision **sans** les sélectionner |
| Joueurs, butin, nœuds de récolte | inchangé |
| Objets interactifs (coffre, caisse…) | nom seulement **à portée d'interaction** (avec le badge E) ; le marqueur de quête garde sa portée |

- **Désélection automatique** : toute cible (créature ou joueur) qui sort du champ de
  vision est désélectionnée.
- **Sélection** (clic gauche / droit, Tab) : limitée aux entités du champ de vision.

Animaux balisés : sanglier des collines, loup gris, lapin des prés, sanglier alpha,
araignée des sous-bois, cerf des clairières, loup corrompu, raptor de la jungle, rat des
galeries, chien des Cendres, ours brun, scorpion géant. Les règles propres au lore
(familles, exceptions par race ou par zone) seront reprises dans une session dédiée.

## Code

- `src/client/gameplay/NameplateVisibility.{h,cpp}` : règles pures
  (`ShouldShowNameplate`, `ShouldAutoDeselect`, `ShouldShowLevel`, `ClassHasAnimalSense`),
  testées par `nameplate_visibility_tests`.
- `src/client/gameplay/PerceptionModel.{h,cpp}` + `game/data/gameplay/perception.json` :
  rayon de perception, testé par `perception_model_tests` (valeurs du classeur).
- `CreatureCatalog` : champ `animal` lu depuis `"category"` (facultatif ; le serveur
  l'ignore).
- `Engine.cpp` : filtre des plaques, désélection automatique, limites du clic et du Tab,
  libellés d'objets à portée.
