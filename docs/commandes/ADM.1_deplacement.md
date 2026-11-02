# ADM.1 — Commandes de déplacement (maître de jeu)

Toutes ces commandes sont réservées au rôle **maître de jeu** (et administrateur).
Elles sont exécutées par le shard qui héberge le personnage de l'appelant (relais
ADM.0, opcodes 216/217). Aide en jeu : `/help movement`, `/help /tp`…

| Commande | Alias | Effet |
|---|---|---|
| `/tp <x> <z>` | `/teleport` | coordonnées globales (m), posé au sol |
| `/tp <x> <y> <z>` | | coordonnées avec hauteur |
| `/tp <lieu>` | | ville, lieu remarquable ou région (sans majuscules ni accents) |
| `/tp @<joueur>` | | auprès d'un joueur du même shard |
| `/goto <joueur>` | `/appear` | = `/tp @<joueur>` |
| `/summon <joueur>` | | fait venir le joueur devant soi (il est prévenu) |
| `/recall <joueur>` | | renvoie le joueur à sa position d'avant `/summon` |
| `/back` | | revient à la position d'avant le dernier déplacement MJ (aller-retour) |
| `/places [filtre] [page]` | | lieux connus, 15 par page, avec coordonnées |
| `/mark <nom>` | `/bookmark` | mémorise la position (50 favoris au plus) |
| `/marks` | `/bookmarks` | liste des favoris et distance |
| `/gomark <nom>` | | va au favori |
| `/unmark <nom>` | | supprime le favori |
| `/forward <m>` | `/fwd` | avance tout droit (négatif : recule), 20 000 m au plus |
| `/up <m>` / `/down <m>` | | monte / descend |
| `/ground` | | se recale au sol |
| `/spawn` | | retour au point d'entrée en monde de la session |
| `/speed <0.1–10> [joueur]` | | multiplie la vitesse ; `/speed reset` |
| `/fly [on\|off] [joueur]` | | vol libre (Espace monte, Accroupi descend) |
| `/freeze <joueur>` / `/unfreeze <joueur>` | | gèle / libère un joueur |

## Hauteur et terrain

Le shard ne connaît pas le relief. Une destination **sans hauteur** (`/tp x z`,
lieu, `/forward` à pied, `/ground`) est envoyée avec une hauteur très basse : le
client pose le personnage au sol (règle existante « max(y, sol + 0,9 m) »). La
hauteur de la **commande de mouvement suivante** du client est alors reprise
telle quelle par le shard (`MovementState::trustNextHeight`), puis les règles
normales (pente, saut, chute) reprennent. Les destinations avec hauteur connue
(joueur, favori, `/back`, `/up`) la gardent ; le client la relève au sol si elle
est sous le terrain.

Toute destination hors du monde généré (mur invisible) est refusée.

## Vitesse, vol, gel

- `ConnectedClient::gm` (`src/shardd/gameplay/gm/GmPlayerState.h`) porte le
  multiplicateur et les drapeaux. **Non persisté** : une reconnexion remet tout à
  l'état normal.
- Shard : `RefreshMaxMoveSpeed` multiplie la vitesse autoritaire ; en vol, la
  hauteur du client est acceptée (`MovementState::verticalFree`, conservé par
  `ResetMovement`) ; gelé, la vitesse autorisée est nulle.
- Client : nouveau message **GmStateUpdate** (MessageKind 104, fiable, 12 octets :
  clientId, drapeaux, multiplicateur). Le `CharacterController` applique le
  multiplicateur à la vitesse et à l'accélération (`SetSpeedMultiplier`), le vol
  sans endurance (`SetGmFlight`, vitesse verticale = course × /speed) ; gelé,
  aucune entrée de déplacement. L'état est oublié à chaque Welcome (nouvelle
  session). Un client plus ancien ignore le message (journalisé) : il ne vole
  pas et ne va pas plus vite, mais le serveur reste cohérent.

## Favoris

Gardés en mémoire du shard (par personnage) : perdus à son redémarrage.

## Annuaire des lieux

`src/shardd/gameplay/gm/GmPlaceDirectory.{h,cpp}`, chargé au premier `/tp <lieu>`
ou `/places` : `world/<monde>/places.json` (villes, lieux remarquables, régions —
centre du rectangle) + villes de départ de `start_locations.json`. Recherche :
nom exact, identifiant, début du nom, puis nom contenant le texte ; à score égal
une ville passe avant un lieu, puis une région, puis un départ. Si `places.json`
n'est pas déployé sur le shard, seules les villes de départ sont connues (la
réponse le signale).

## Limites connues

- `/goto`, `/summon`, `/recall`, `/freeze` visent un joueur **du même shard**.
- Le shard ne connaît pas le rôle du joueur visé : un maître de jeu peut geler un
  administrateur (les deux actions sont tracées dans l'audit).

## Fichiers

| Fichier | Rôle |
|---|---|
| `src/shared/server_bootstrap/ServerAppGmMovement.cpp` | les 19 commandes |
| `src/shared/server_bootstrap/GmCommandExecutor.h` | déclaration de l'exécuteur (handlers répartis par fichier) |
| `src/shardd/gameplay/gm/GmPlayerState.h` | état MJ (vitesse, vol, gel, positions mémorisées) |
| `src/shardd/gameplay/gm/GmPlaceDirectory.{h,cpp}` | annuaire des lieux |
| `src/shared/movement/MovementRules.{h,cpp}` | `verticalFree`, `trustNextHeight` |
| `src/shared/network/ServerProtocol.{h,cpp}` | GmStateUpdate (kind 104) |
| `src/client/gameplay/CharacterController.{h,cpp}` | multiplicateur de vitesse, vol de MJ |
| `src/client/ui_common/UIModel.{h,cpp}`, `src/client/app/Engine.cpp` | réception et application de l'état MJ |

## Tests

- `gm_place_directory_tests` — repli du texte, chargement, recherche, liste,
  état MJ, GmStateUpdate.
- `movement_rules_tests` — vol (hauteur libre, conservée au téléport), confiance
  à la première commande après un téléport.
- `command_help_tests` — cohérence JSON ↔ catalogue du shard.

## Déploiement

Lock-step **client + shard** (nouveau message 104, règles de mouvement), et
**master** pour le registre de commandes (relais automatique des nouvelles
entrées). Pas de migration.
