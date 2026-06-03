# Animation d'ouverture de coffre + verrou de déplacement — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** À l'appui sur E près d'un coffre, le personnage joue l'animation `PickUp_Table` (penché → saisie → redressé) et son déplacement est verrouillé pendant toute la durée du clip ; ailleurs, le geste `Interact` générique reste inchangé.

**Architecture:** Réutilise l'état de locomotion `Interact` existant (pas de nouvel état réseau). Un rôle de clip dynamique (`m_currentInteractRole`, calqué sur `m_currentPunchRole`) sélectionne `PickUp_Table` près d'un coffre, `Interact` ailleurs. Un horodatage `m_avatarMoveLockUntilSec` neutralise les entrées de déplacement (`MoveInput` vide) avant `CharacterController::Update` tant que le clip joue. Caméra non touchée.

**Tech Stack:** C++ / Engine client (`src/client/app/Engine.{h,cpp}`), Vulkan, state machine de locomotion inline dans `Engine::Update`.

**Note sur la vérification (lire avant de commencer) :** La state machine de locomotion est codée inline dans `Engine::Update` (pas de fonction extraite testable) et le poste n'a **pas** de toolchain de build local (cf. mémoire projet : cmake/MSVC/vcpkg absents). Il n'y a donc **pas de test unitaire** pour ce code. La vérification se fait par (1) **compilation via la CI GitHub** (build-windows + build-linux) et (2) un **protocole de test manuel en jeu** (Task 5). Ne pas inventer de faux tests unitaires.

**Spec de référence :** [docs/superpowers/specs/2026-06-02-coffre-animation-perso-verrou-design.md](../specs/2026-06-02-coffre-animation-perso-verrou-design.md)

---

## File Structure

- **Modify** `src/client/app/Engine.h` — 2 nouveaux membres (`m_currentInteractRole`, `m_avatarMoveLockUntilSec`) à côté des champs d'anim avatar existants (~ligne 716).
- **Modify** `src/client/app/Engine.cpp` — 4 points :
  1. Lecture dynamique du clip de l'état `Interact` (fetch ~ligne 8363 + ternaire de transition ~ligne 8690).
  2. Déclenchement near-chest : rôle + verrou au site du trigger Interact (~ligne 8634).
  3. Neutralisation des entrées quand verrouillé, avant `CharacterController::Update` (~ligne 8280).
  4. Garde anti-roulade pendant le verrou (~ligne 8596).

Aucun fichier créé. Pur client. Pas de migration, pas d'opcode, pas de changement de `ToWireAnimState`.

---

## Task 1 : Déclarer les deux nouveaux membres dans Engine.h

**Files:**
- Modify: `src/client/app/Engine.h` (après `m_punchAlt`, ~ligne 717)

- [ ] **Step 1 : Ajouter les membres**

Dans `src/client/app/Engine.h`, juste après la ligne `bool m_punchAlt = false;` (~717), insérer :

```cpp
		/// Interaction : rôle d'anim joué par l'état Interact (clip dynamique, calqué
		/// sur m_currentPunchRole). "Interact" par défaut (geste générique sur E) ;
		/// passe à "PickUp_Table" près d'un coffre (se pencher → saisir → se redresser).
		std::string                                              m_currentInteractRole = "Interact";
		/// Verrou de déplacement : instant (s, EngineNowSec) jusqu'auquel les entrées
		/// de déplacement de l'avatar sont neutralisées (MoveInput vide) pendant un
		/// geste verrouillant (ouverture de coffre). 0 = aucun verrou actif.
		float                                                    m_avatarMoveLockUntilSec = 0.0f;
```

- [ ] **Step 2 : Vérifier la cohérence locale (lecture)**

Relire le bloc 705-722 de `Engine.h` : les nouveaux membres doivent être dans la même section que `m_currentPunchRole` / `m_castPhase` (état d'animation avatar). Aucune autre déclaration touchée.

- [ ] **Step 3 : Commit**

```bash
git add src/client/app/Engine.h
git commit -m "feat(avatar): champs m_currentInteractRole + m_avatarMoveLockUntilSec (coffre)"
```

---

## Task 2 : Rendre le clip de l'état Interact dynamique

L'état `Interact` joue actuellement le clip littéral `"Interact"`. On le fait pointer sur `m_currentInteractRole`. Deux points à modifier.

**Files:**
- Modify: `src/client/app/Engine.cpp:8363` (fetch du clip Interact)
- Modify: `src/client/app/Engine.cpp:8690-8693` (ternaire de sélection du clip à la transition)

- [ ] **Step 1 : Fetch dynamique du clip Interact**

Remplacer (~ligne 8363) :

```cpp
						const engine::render::skinned::AnimationClip* interactClip =
							m_currentSkinnedMesh->FindClip("Interact");
```

par :

```cpp
						// Clip dynamique : "Interact" (geste générique) ou "PickUp_Table"
						// (près d'un coffre). m_currentInteractRole est fixé au déclenchement.
						const engine::render::skinned::AnimationClip* interactClip =
							m_currentSkinnedMesh->FindClip(m_currentInteractRole.c_str());
```

> `interactClip` ne sert qu'à la condition de fin du one-shot dans le `case Interact`
> (~ligne 8550), évaluée seulement quand l'avatar est *déjà* en état Interact —
> à ce moment `m_currentInteractRole` est stable depuis le dernier appui. OK.

- [ ] **Step 2 : Sélection dynamique du clip à la transition**

Remplacer le ternaire (~ligne 8690-8693) :

```cpp
							const char* clipName =
								(newState == AvatarLocomotionState::Emote && !m_currentEmoteRole.empty()) ? m_currentEmoteRole.c_str()
								: (newState == AvatarLocomotionState::Punch) ? m_currentPunchRole.c_str()
								: StateToClipName(newState);
```

par :

```cpp
							const char* clipName =
								(newState == AvatarLocomotionState::Emote && !m_currentEmoteRole.empty()) ? m_currentEmoteRole.c_str()
								: (newState == AvatarLocomotionState::Punch) ? m_currentPunchRole.c_str()
								: (newState == AvatarLocomotionState::Interact) ? m_currentInteractRole.c_str()
								: StateToClipName(newState);
```

> Ce ternaire s'exécute APRÈS les triggers (le bloc transition ~8669 vient après
> le trigger Interact ~8634), donc `m_currentInteractRole` est déjà à jour ce frame.

- [ ] **Step 3 : Commit**

```bash
git add src/client/app/Engine.cpp
git commit -m "feat(avatar): clip dynamique pour l'état Interact (rôle m_currentInteractRole)"
```

---

## Task 3 : Déclencher PickUp_Table + armer le verrou près d'un coffre

**Files:**
- Modify: `src/client/app/Engine.cpp:8633-8635` (trigger de l'état Interact)

- [ ] **Step 1 : Remplacer le trigger Interact**

Remplacer (~ligne 8633-8635) :

```cpp
						// Interagir (touche E par defaut) : geste Interact one-shot, action non-combat.
						if (interactPressed && !moveInput.jumpPressed && !busyOneShot())
							newState = AvatarLocomotionState::Interact;
```

par :

```cpp
						// Interagir (touche E par defaut) : geste Interact one-shot, action non-combat.
						// Près d'un coffre : joue "PickUp_Table" (se pencher → saisir → se redresser)
						// et verrouille le déplacement le temps du clip. Ailleurs : geste "Interact"
						// générique, sans verrou (comportement historique inchangé).
						if (interactPressed && !moveInput.jumpPressed && !busyOneShot())
						{
							const bool nearChest =
								m_chestLoaded && m_interactableInRange == m_chestInteractableIndex;
							if (nearChest)
							{
								m_currentInteractRole = "PickUp_Table";
								const engine::render::skinned::AnimationClip* puClip =
									m_currentSkinnedMesh->FindClip("PickUp_Table");
								// Clip absent (race non-UE5 / fallback) -> durée 0 -> verrou expire
								// immédiatement, pas de gel permanent ; le repli sur "Interact"
								// est géré juste en dessous.
								m_avatarMoveLockUntilSec = nowSec + (puClip ? puClip->duration : 0.0f);
								if (!puClip)
									m_currentInteractRole = "Interact";
							}
							else
							{
								m_currentInteractRole = "Interact";
							}
							newState = AvatarLocomotionState::Interact;
						}
```

> `nearChest` s'appuie sur `m_interactableInRange` (mis à jour à la frame
> précédente dans le bloc interactibles ~8745) et `m_chestInteractableIndex`
> (stable). Les deux valent -1 par défaut, mais `m_chestLoaded` garde le cas
> (faux tant qu'aucun coffre animé n'est chargé). `nowSec` est déjà défini
> (~ligne 8344) dans ce bloc.

- [ ] **Step 2 : Commit**

```bash
git add src/client/app/Engine.cpp
git commit -m "feat(coffre): E près d'un coffre -> PickUp_Table + arme le verrou déplacement"
```

---

## Task 4 : Neutraliser le déplacement pendant le verrou

**Files:**
- Modify: `src/client/app/Engine.cpp:8280-8282` (neutralisation avant CharacterController::Update)
- Modify: `src/client/app/Engine.cpp:8596` (garde anti-roulade pendant le verrou)

- [ ] **Step 1 : Neutraliser MoveInput avant le CharacterController**

Remplacer (~ligne 8280-8282) :

```cpp
					const auto moveInput = BuildMoveInput(m_input, m_orbitalCameraController, movementLayout, sprintKey, crouchKey);
					// Collisionneur composite : terrain (sol + eau) + cylindres des props/décor.
					m_characterController.Update(static_cast<float>(dt), moveInput, m_worldCollider);
```

par :

```cpp
					auto moveInput = BuildMoveInput(m_input, m_orbitalCameraController, movementLayout, sprintKey, crouchKey);
					// Verrou de geste (ouverture de coffre) : tant qu'il est actif, on
					// neutralise toutes les entrées de déplacement (direction + saut) pour
					// que l'avatar reste immobile pendant l'animation. La caméra reste libre
					// (m_orbitalCameraController non touché).
					const bool moveLocked = EngineNowSec() < m_avatarMoveLockUntilSec;
					if (moveLocked)
						moveInput = engine::gameplay::MoveInput{};
					// Collisionneur composite : terrain (sol + eau) + cylindres des props/décor.
					m_characterController.Update(static_cast<float>(dt), moveInput, m_worldCollider);
```

> Changement `const auto` -> `auto` car on réaffecte `moveInput`. Le reste du code
> qui lit `moveInput` (yaw, state machine) voit alors des entrées nulles pendant
> le verrou : `moving` devient faux, la SM laisse l'état Interact se terminer puis
> repasse Idle. Le `jumpPressed` neutralisé empêche aussi le saut.

- [ ] **Step 2 : Empêcher la roulade pendant le verrou**

Le trigger de roulade/esquive (double-tap Ctrl) ne passe PAS par `busyOneShot()`,
il faut donc l'exclure explicitement. Remplacer (~ligne 8596) :

```cpp
						// Esquive/roulade (double-appui Crouch) : Roll one-shot, prioritaire sur crouch.
						if (dodgePressed && m_avatarLocoState != AvatarLocomotionState::Roll)
```

par :

```cpp
						// Esquive/roulade (double-appui Crouch) : Roll one-shot, prioritaire sur crouch.
						// Bloquée pendant le verrou de geste (ouverture de coffre).
						if (dodgePressed && m_avatarLocoState != AvatarLocomotionState::Roll
							&& nowSec >= m_avatarMoveLockUntilSec)
```

> Les autres actions one-shot (attaque clic gauche, punch, cast) sont déjà
> bloquées par `busyOneShot()` puisque l'avatar est en état `Interact` pendant
> le verrou — pas de garde supplémentaire nécessaire pour elles.

- [ ] **Step 3 : Commit**

```bash
git add src/client/app/Engine.cpp
git commit -m "feat(coffre): verrou déplacement (MoveInput neutralisé + roulade bloquée)"
```

---

## Task 5 : Vérification (compilation CI + test manuel en jeu)

Pas de test unitaire possible (SM inline, pas de toolchain locale). Vérification en deux temps.

**Files:** aucun (vérification).

- [ ] **Step 1 : Pousser la branche et lancer la CI**

```bash
git push -u origin feat/coffre-anim-perso-verrou
```

Ouvrir une PR (ou laisser la CI tourner sur la branche). Attendre la CI GitHub :
- `build-windows` : DOIT compiler sans erreur (c'est le binaire client réel).
- `build-linux` : DOIT compiler ; `ctest` ne couvre pas ce code mais ne doit pas régresser.

Attendu : ✅ vert sur les deux. (Build Windows ~30 min, cf. mémoire projet.)

- [ ] **Step 2 : Vérifier l'absence de warning de compilation sur les zones touchées**

Dans les logs CI, vérifier qu'aucun nouveau warning n'apparaît sur `Engine.cpp`
(notamment autour du changement `const auto` -> `auto moveInput`).

- [ ] **Step 3 : Protocole de test manuel en jeu (sur le poste client Windows)**

Lancer le client de jeu, entrer dans le monde, s'approcher du coffre `Chest_Wood`
(message « Près de Coffre - appuyez sur E »), puis vérifier dans l'ordre :

1. **Animation perso** : appuyer sur E → l'avatar se penche (saisie) puis se
   redresse (clip `PickUp_Table`). Le couvercle du coffre s'ouvre (instantané).
2. **Verrou** : pendant l'animation, presser Z/Q/S/D (ou WASD), Espace (saut),
   double-Ctrl (roulade), clic gauche (attaque) → l'avatar ne bouge PAS, ne saute
   PAS, ne roule PAS, n'attaque PAS jusqu'à la fin du clip.
3. **Caméra libre** : pendant le verrou, le clic droit + souris tourne toujours
   la caméra (non bloquée).
4. **Reprise** : à la fin du clip, le déplacement redevient immédiat (Z avance).
5. **Refermeture** : le coffre se referme bien ~2 s après l'ouverture (inchangé).
6. **Hors coffre** : appuyer sur E loin de tout coffre → geste `Interact`
   générique, AUCUN verrou (on peut bouger immédiatement). Comportement historique.
7. **Clip absent (optionnel)** : si une race sans `PickUp_Table` est testable,
   E près d'un coffre joue `Interact` et ne fige PAS l'avatar (pas de gel).

Vérifier aussi les logs `[Avatar SM]` : la transition `-> Interact` doit logguer
`Play('PickUp_Table')` près d'un coffre, `Play('Interact')` ailleurs.

- [ ] **Step 4 : Renseigner le résultat du test manuel**

Noter dans la PR (case cochée) le résultat des points 1-6 (7 si testé). Si un
point échoue, repasser par superpowers:systematic-debugging avant de merger.

---

## Notes de livraison

- **Déploiement** : ✅ client uniquement, **pas de redéploiement serveur** (aucun
  opcode, aucun changement `ToWireAnimState`, aucune migration).
- **Merge** : une seule PR, mergeable dès que la CI est verte ET le test manuel
  (Task 5 Step 3) validé. Pas de stack, pas de lock-step serveur.
- **Limite connue (v1)** : les joueurs distants voient l'état réseau `Interact`
  (rôle de clip local) → ils voient le geste `Interact` générique, pas
  `PickUp_Table`. Cosmétique, hors périmètre (cf. spec).

## Self-review (couverture spec)

- Animation `PickUp_Table` près d'un coffre → Task 2 + Task 3. ✅
- Verrou « tout bloqué, durée du clip » → Task 3 (durée) + Task 4 (déplacement/saut/roulade ; attaque/cast/punch via `busyOneShot`). ✅
- Caméra libre → Task 4 Step 1 (orbital controller non touché). ✅
- Portée coffres uniquement, `Interact` générique inchangé ailleurs → Task 3 (`nearChest`). ✅
- Couvercle instantané inchangé → aucun changement du bloc coffre ~8763. ✅
- Garde-fou clip absent → Task 3 Step 1 (durée 0 + repli `Interact`). ✅
- Pas de redéploiement serveur → confirmé (client only). ✅
