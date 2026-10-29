# FIX.10 — Plaques de nom : moins d'informations à l'écran

Retour joueur du 29/10/2026 (captures « Hameau de Marn » et prairie) : plaques de
« Lapin des pres » visibles à des centaines de mètres, dizaines de sangliers / loups
nommés à l'arrivée au hameau, noms « Coffre », « Caisse en bois », « Caisse metal »
partout.

## Règles (client)

Champ de vision : `client.nameplate.vision_distance_m` (40 m, distance horizontale).

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
  (`ShouldShowNameplate`, `ShouldAutoDeselect`, `ClassHasAnimalSense`), testées par
  `nameplate_visibility_tests`.
- `CreatureCatalog` : champ `animal` lu depuis `"category"` (facultatif ; le serveur
  l'ignore).
- `Engine.cpp` : filtre des plaques, désélection automatique, limites du clic et du Tab,
  libellés d'objets à portée.
