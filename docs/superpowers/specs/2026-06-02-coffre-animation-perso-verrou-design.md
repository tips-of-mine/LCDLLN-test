# Design — Animation d'ouverture de coffre + verrou de déplacement

**Date** : 2026-06-02
**Portée** : client uniquement (rendu / gameplay), pas de redéploiement serveur.

## Problème

À l'appui sur **E** près d'un coffre (`Chest_Wood`) :

1. Le couvercle s'ouvre puis se referme automatiquement à +2 s. **Déjà en place**
   ([`Engine.cpp:8753`](../../../src/client/app/Engine.cpp) et suivantes).
2. Aucune animation du **personnage** n'accompagne le geste de façon spécifique
   au coffre. Un état `Interact` générique existe et se déclenche à *chaque*
   appui sur E, mais il n'est ni spécifique au coffre ni verrouillant.
3. **Bug constaté** : le joueur peut repartir immédiatement. Le
   `CharacterController` est mis à jour **inconditionnellement** avec les entrées
   de déplacement ([`Engine.cpp:8282`](../../../src/client/app/Engine.cpp)), donc
   le mouvement n'est jamais verrouillé pendant un geste.

## Découverte préalable

Il **n'existe pas** de clip « Chest Open » pour l'avatar. `Chest_Open` est
l'animation du **couvercle** du coffre. La bibliothèque d'animations du
personnage (`Humanoid_Base_Standard.glb`) contient comme candidats pour « se
pencher dans le coffre puis se redresser » :

- `PickUp_Table` — penché → saisie → redressé (**retenu**)
- `Interact` — geste d'interaction générique (déjà câblé)
- `Fixing_Kneeling` — s'agenouille

## Décisions

| Sujet | Décision |
|---|---|
| Animation perso | `PickUp_Table` |
| Verrou | Tout bloqué (déplacement + saut + attaque/sort/roulade), **durée du clip** ; caméra libre |
| Portée | Coffres uniquement ; le geste `Interact` générique ailleurs reste inchangé |
| Couvercle | Ouverture instantanée à l'appui (comportement actuel inchangé) |

## Approche retenue (A) — Verrou temporel + clip d'interaction dynamique

Réutilise l'état de locomotion `Interact` existant ; pas de nouvel état
(éviter de toucher l'enum répliqué `ToWireAnimState` et ses `static_assert`).

### Nouveaux membres `Engine`

- `std::string m_currentInteractRole` — rôle de clip joué par l'état `Interact`.
  Défaut `"Interact"`. Mis à `"PickUp_Table"` près d'un coffre. Calque le
  pattern existant `m_currentPunchRole` (alternance Jab/Cross).
- `float m_avatarMoveLockUntilSec = 0.0f` — horodatage (base `EngineNowSec()`)
  jusqu'auquel les entrées de déplacement sont neutralisées.

### Détection « près d'un coffre »

Sans réordonner le code, la state machine dispose déjà de
`m_interactableInRange` (valeur de la frame précédente) et de
`m_chestInteractableIndex` :

```
nearChest = m_chestLoaded && m_interactableInRange == m_chestInteractableIndex
```

### Déclenchement

Au site où l'état `Interact` est armé (~[`Engine.cpp:8634`](../../../src/client/app/Engine.cpp)) :

- si `nearChest` :
  - `m_currentInteractRole = "PickUp_Table"`
  - récupérer le clip via `m_currentSkinnedMesh->FindClip("PickUp_Table")`
  - `m_avatarMoveLockUntilSec = EngineNowSec() + (clip ? clip->duration : 0.0f)`
- sinon :
  - `m_currentInteractRole = "Interact"`
  - pas de verrou (comportement actuel)

### Lecture du clip dans l'état `Interact`

L'état `Interact` cherche `FindClip(m_currentInteractRole.c_str())` au lieu du
littéral `"Interact"`. La condition de fin de one-shot
(`stateElapsed >= clip->duration`) utilise le même clip dynamique.

### Verrou de déplacement

Juste avant `m_characterController.Update(...)` ([`Engine.cpp:8282`](../../../src/client/app/Engine.cpp)) :

```
const bool moveLocked = EngineNowSec() < m_avatarMoveLockUntilSec;
auto moveInput = BuildMoveInput(...);
if (moveLocked) moveInput = engine::gameplay::MoveInput{};  // zéro dir + jumpPressed=false
m_characterController.Update(dt, moveInput, m_worldCollider);
```

Les déclencheurs d'actions one-shot (attaque, sort, roulade, punch) reçoivent
en plus `&& !moveLocked` pour empêcher toute action pendant le verrou.

La caméra (`m_orbitalCameraController`) n'est **pas** touchée → reste libre.

## Garde-fous / cas limites

- **Clip absent** (race non-UE5 / fallback y_bot) : `clip` nul → durée 0 → verrou
  expire immédiatement (pas de gel permanent). Repli sur le clip `Interact`.
- **Re-presser E pendant le verrou** : bloqué par `busyOneShot()` / `moveLocked` ;
  la refermeture auto à +2 s reste pilotée comme aujourd'hui (re-arme le timer).
- **Yaw avatar** : aucun mouvement pendant le verrou → l'avatar reste orienté tel
  quel face au coffre. (Snap explicite vers le coffre non demandé ; hors périmètre.)

## Limite connue (v1)

Les autres joueurs reçoivent l'état réseau `Interact` (le rôle de clip est
**local**), donc côté distant l'avatar joue le geste `Interact` générique et non
`PickUp_Table`. Strictement cosmétique pour les observateurs distants ; acceptable
pour la v1. Une réplication du rôle de clip serait un travail séparé.

## Déploiement

✅ **Client uniquement, pas de redéploiement serveur.** Aucun nouvel opcode,
aucun changement de `ToWireAnimState`, aucune migration. Pur rendu/gameplay
client.

## Hors périmètre

- Réplication réseau du clip `PickUp_Table` aux joueurs distants.
- Synchro fine couvercle ↔ mi-animation (couvercle reste instantané).
- Snap d'orientation de l'avatar vers le coffre.
- Contenu / loot du coffre.
