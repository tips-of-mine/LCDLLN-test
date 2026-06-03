# Spec — Saut plus haut & dessus de props atterrissables (Épic A)

Date : 2026-06-02
Statut : **à implémenter** (branche `feat/saut-caisses-atterrissables`).

Premier sous-projet d'un ensemble de 8 épics issus d'un brainstorming. Périmètre
volontairement restreint et autonome : il débloque la mécanique d'exploration de
« zones cachées » en permettant de sauter **sur** les caisses (et plus généralement
sur le dessus de tout prop solide).

## 1. Objectif

1. **Augmenter la hauteur de saut** des personnages, globalement, pour qu'un saut
   permette d'atteindre le dessus d'une « caisse métal » avec une marge de sécurité
   de +0,1 m.
2. **Rendre le dessus des props solides atterrissable** : aujourd'hui la collision
   des props ne bloque que l'horizontal, jamais le vertical — on retombe donc *à
   travers* une caisse. Il faut pouvoir atterrir et se tenir dessus.
3. **Confirmer/documenter** que les caisses (`Crate_Metal`, `Crate_Wooden`) ne sont
   pas des emplacements de loot.

Le tout livré dans **une seule PR**.

## 2. État actuel (contexte vérifié)

- **Saut** — `src/client/gameplay/CharacterController.h`, struct `Config` :
  - `jumpSpeed = 4.9f`, `gravity = -20.0f` → apex `4.9² / (2·20) ≈ 0,60 m`.
  - Saut intégralement **côté client** (le controller vit dans `src/client/gameplay`).
  - `maxStep = 0.3f` (marche d'escalier max), `coyoteTimeSec = 0.1f`, `jumpBufferSec = 0.1f`.
- **Caisse métal** — `game/data/meshes/props/Crate_Metal.gltf` :
  - Bounding box locale (échelle 1) : Y de ~0,003 à **~0,87 m** (deux sous-mesh : max Y
    0,8473 et 0,8705). Hauteur ≈ **0,87 m**. Le nœud glTF n'a **pas** de scale propre.
- **Placement des props** — `src/client/app/Engine.cpp` (~10255 et ~10464) :
  - L'échelle de placement est lue depuis `config.json` (`...scale`, défaut 1.0), cuite
    dans les sommets monde. La hauteur de collision `topY` est calculée **à partir des
    sommets après scale** (`topY = maxY + lift`, Engine.cpp:10492) → reflète la hauteur
    réelle dans le monde.
  - Si solide, on enregistre `PropCylinder{ wx, wz, radius, groundY, topY }`
    (Engine.cpp:10591).
- **Collision props** — `src/client/gameplay/CompositeWorldCollider.{h,cpp}` :
  - `PropCylinder` = cylindre vertical `(cx, cz, radius)` borné en Y par `[baseY, topY]`.
  - `SweepCapsule` ne bloque **que le déplacement horizontal** entrant dans le cylindre.
    Les sweeps verticaux ne sont **jamais** bloqués (commentaire explicite
    CompositeWorldCollider.cpp:56-60), pour éviter que la **sonde anti-encastrement**
    du `CharacterController` (départ 50 m au-dessus) ne téléporte le perso au sommet
    d'un prop.
- **Détection de sol** — `src/client/gameplay/CharacterController.cpp` :
  - Sweep de gravité par frame (court, ~`vitesse·dt`) (~ligne 300).
  - « Sticky ground probe » : sweep descendant court quand déjà au sol (~ligne 228).
  - Sonde anti-encastrement : `kRecoverProbeUp = 50.0f`, sweep depuis 50 m au-dessus
    vers la position (~ligne 376) — **doit rester ignorée par les props**.
- **Loot** — le loot n'est branché que sur les nœuds de récolte
  (`GatheringSystem`, `game/data/gathering/*.json`). Aucune table de loot n'est
  associée à `Crate_Metal`/`Crate_Wooden`. Rien à retirer.

## 3. Décisions de design (validées au brainstorming)

| Sujet | Décision |
|---|---|
| Surfaces atterrissables | **Tous les props solides** (réutilise `PropCylinder.topY`). |
| Portée du saut | **Global, partout**. Constante en dur (pas de clé config). |
| Hauteur cible | **Valeur fixe ~0,97 m** d'apex (caisse 0,87 + marge 0,1). |
| Approche collision | **Approche 1** : couvercle de cylindre conditionné à la descente. |

## 4. Changements

Trois unités isolées.

### 4.1 Hauteur de saut

> **⚠️ Levier runtime = `config.json`, PAS le défaut du header.** L'Engine construit le
> `CharacterController::Config` en lisant `config.json` (`player.movement.jump_speed`) à
> [Engine.cpp:4952](src/client/app/Engine.cpp:4952), avec un fallback en dur. Le défaut
> de la struct `Config` n'est **pas** utilisé au runtime du client de jeu. **Tous** les
> emplacements doivent être alignés, sinon le changement n'a aucun effet en jeu (bug
> constaté au test : le perso ne sautait pas plus haut car `config.json` valait 4.9).

Aligner `jumpSpeed` à **6.25** aux trois endroits :

- **`config.json`** : `player.movement.jump_speed : 4.9 → 6.25` (**valeur réelle en jeu**).
- **`src/client/app/Engine.cpp:4952`** : fallback `9.0 → 6.25` (cohérence si la clé est
  absente d'un `config.json` déployé).
- **`src/client/gameplay/CharacterController.h`** : défaut de struct `4.9 → 6.25` (doc +
  utilisé par les tests).

Apex = `6.25² / (2·20) = 39,0625 / 40 ≈ **0,977 m**` > 0,87 + 0,1 = 0,97. ✓

- **Effet de bord assumé** : tous les sauts montent à ~0,98 m → beaucoup de rebords du
  monde deviennent atteignables (objectif « zones cachées »).
- **Leçon test** : `Test_Jump_DefaultClearsMetalCrate` valide la *physique* (apex pour la
  vitesse par défaut) mais **pas le câblage `config.json`** — vérifié manuellement en jeu
  (instancier l'Engine en test nécessite un `VkDevice`).

### 4.2 Couvercle atterrissable — `CompositeWorldCollider::SweepCapsule`

Dans la boucle des cylindres, **ajouter** un test « dessus de cylindre » qui s'applique
uniquement aux **sweeps descendants courts**, sans toucher au blocage horizontal existant.

Condition d'émission d'un contact « sol sur prop » :

1. Sweep descendant : `endCenter.y < startCenter.y`.
2. Le **bas de la capsule** (`center.y - halfHeight - radius`) part au-dessus de `topY`
   au début du sweep et finit à/sous `topY` à la fin.
3. La position XZ au moment du franchissement est **dans l'empreinte** du cylindre
   (distance horizontale à l'axe ≤ `c.radius` ; tolérance possible de `+capsule.radius`
   pour rester cohérent avec le rayon combiné utilisé en horizontal — à figer à
   l'implémentation).
4. **Garde anti-sonde 50 m** : n'émettre que si `startCenter.y - c.topY ≤ kPropTopMargin`,
   avec `kPropTopMargin` une petite marge (quelques mètres, ≪ 50). La sonde
   anti-encastrement part de 50 m → exclue → comportement actuel préservé.
   - Borne basse de la marge : elle doit couvrir le « sticky ground probe » et le sweep
     de gravité par frame quand le centre de la capsule est juste au-dessus du prop
     (≈ `halfHeight + radius` + `vitesse·dt`). Valeur retenue à figer après lecture des
     distances de sonde, p. ex. `kPropTopMargin = 4.0f` (≫ demi-capsule, ≪ 50).

Sortie quand la condition est remplie et que la fraction est meilleure que `best` :
- `best.hit = true`.
- `best.normal = {0, 1, 0}` (sol horizontal → `IsWalkable` vrai).
- `best.fraction` = fraction du sweep à laquelle le **bas de la capsule** atteint `topY`
  (le `CharacterController` en déduit la position de repos, comme pour le terrain).

Le blocage **horizontal** existant (côtés du cylindre) reste inchangé : les deux
contributions coexistent (approche latérale → côté ; approche descendante dans
l'empreinte → couvercle).

### 4.3 Documentation « caisses non-lootables »

- Ajouter une note dans `CODEBASE_MAP.md` (section props/loot) actant la règle :
  les caisses (`Crate_Metal`, `Crate_Wooden`) **ne sont pas** des emplacements de loot ;
  le loot reste réservé aux nœuds de récolte (et, plus tard, aux coffres de l'épic B).
- Aucune suppression de code (rien n'associe de loot aux caisses aujourd'hui).

## 5. Flux nominal

1. Le joueur saute → apex ~0,98 m.
2. Il retombe au-dessus d'une caisse.
3. Le sweep de gravité descendant croise `topY` dans l'empreinte du cylindre.
4. `SweepCapsule` renvoie un contact sol (normale haut) à `topY`.
5. `CharacterController` passe `grounded = true` ; le perso se tient sur la caisse.
6. En marchant hors de l'empreinte → plus de contact couvercle → chute naturelle.

## 6. Cas limites

- **Approche horizontale** : bloquée par le côté du cylindre (inchangé). Le step-up
  (0,3 m) ne suffit pas pour 0,87 m → il faut sauter (voulu).
- **Sonde anti-encastrement (50 m)** : exclue par `kPropTopMargin` → pas de
  téléportation au sommet du prop (préserve la parade existante).
- **Bord de plateforme** : sortie d'empreinte XZ → plus de hit → chute.
- **Arbres / piliers** : auront aussi un dessus plat atterrissable à leur point le plus
  haut (conséquence assumée de « tous les props solides »). Un opt-out par prop pourra
  être ajouté plus tard si nécessaire (hors périmètre).
- **Empilement / props imbriqués** : on retient le hit de plus petite fraction (le plus
  proche), donc le couvercle le plus haut rencontré en premier en descente l'emporte.

## 7. Tests

- **`CharacterControllerJumpTests`** (MAJ) : apex attendu ≈ 0,98 m (au lieu de 0,60).
  Ajuster la/les valeurs attendues et le commentaire.
- **`CompositeWorldCollider`** (nouveaux) :
  - (a) sweep descendant court dont le bas de capsule franchit `topY` dans l'empreinte
    → `hit`, `normal ≈ (0,1,0)`, position de repos au niveau `topY`.
  - (b) sweep depuis 50 m au-dessus (`start.y - topY > kPropTopMargin`) → **pas** de hit
    couvercle (régression anti-encastrement).
  - (c) sweep purement horizontal entrant dans le cylindre → toujours bloqué (normale
    horizontale) — régression du comportement existant.
  - (d) sweep descendant **hors** empreinte XZ → pas de hit couvercle.
- **Intégration** : capsule lâchée au-dessus d'une caisse (cylindre `topY = 0,87`) →
  après quelques `Update`, `IsGrounded()` vrai et `GetPosition().y` stabilisé au niveau
  du dessus de la caisse.

Tests à enregistrer dans la liste CTest (`build-linux.yml` lance `ctest`).

## 8. Impact déploiement serveur — **vérifié : client uniquement**

La position est client-autoritaire avec validation serveur (réplication T0.1). Le
validateur de mouvement côté shard (`src/shardd/anticheat/AntiCheatGameplay.h`)
contrôle :

- la **vitesse 3D** : `dist/dt` vs `maxSpeedMps (13) × speedTolerance (1.5) = 19,5 m/s` ;
- un **saut de téléport** : `dist > maxSingleStepM (50 m)`.

Il ne contrôle **pas** la hauteur de saut en tant que telle. Avec `jumpSpeed = 6.25` :

- sprint + saut simultanés = `√(13² + 6,25²) ≈ 14,4 m/s < 19,5 m/s` → pas de faux
  positif `SpeedHack` ;
- la gravité (`-20`) est **inchangée** → vitesse de chute identique à aujourd'hui ;
- pas de déplacement > 50 m introduit.

**Conclusion : ✅ client uniquement, pas de redéploiement serveur.** Les sections
4.1/4.2/4.3 vivent toutes dans `src/client/` (controller, collision) ou la doc.

## 9. Hors périmètre (épics suivants)

- Coffres à clé (épic B), progression/XP (épic C), etc.
- Opt-out « prop non atterrissable » par donnée.
- Volume « plateforme invisible » plaçable dans l'éditeur monde.
