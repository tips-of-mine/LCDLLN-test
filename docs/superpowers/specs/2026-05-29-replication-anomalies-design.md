# Design — Correction de 3 anomalies de réplication / mouvement (2026-05-29)

## Contexte

Avec deux sessions joueur observées simultanément, trois anomalies ont été
constatées :

1. **/dance et roulade invisibles aux autres** (le saut, lui, est visible).
2. **Saut trop haut** (~1× la taille de l'avatar ; on veut un saut réaliste).
3. **Invisibilité mutuelle à la première connexion** juste après création de
   personnage ; une déconnexion/reconnexion répare tout.

Livraison en **3 PR séparées**, ordre de merge **A → C → B**.

---

## Diagnostic (cause racine par anomalie)

### #1 — Animation non répliquée
`SnapshotEntity` (`src/shared/network/ReplicationTypes.h`) ne transporte pas
l'état d'animation. Pour les avatars distants, `Engine::RecordRemoteAvatars`
**dérive** l'animation de la seule vélocité serveur (`< 0.1 m/s → Idle`, sinon
`Walk`, cf. CODEBASE_MAP §56). Conséquence :
- Le **saut** semble visible parce que la position Y (répliquée) monte
  réellement — pas parce que le clip Jump est joué.
- **/dance** (sur place) et la **roulade** ne modifient pas l'animation perçue →
  invisibles. **Run / Sprint / Attaque** ne sont pas répliqués non plus
  (limite notée CODEBASE_MAP ligne ~1994, « TD.8 »).

### #2 — Saut trop haut
`CharacterController.h` : `jumpSpeed = 9.0` m/s, `gravity = -20` m/s². Apex ≈
v²/(2·g) = 81/40 ≈ **2,0 m** (~1,1× la taille de 1,8 m). Réglage purement client.

### #3 — Invisibilité 1ère connexion
`Engine.cpp` (~7181) : à l'EnterWorld, le `character_key` de la session UDP
n'est surchargé que **si `enterCmd.characterId != 0`**. Si le personnage
fraîchement créé n'a pas (encore) d'`character_id` non nul dans la
`CHARACTER_LIST` rechargée, le bloc est sauté ; `InitGameplayNet` (~9840) relit
alors la valeur de config par défaut/résiduelle (1) et envoie le Hello UDP avec
une **mauvaise clé** → le shard associe le client au mauvais personnage → pas de
réplication AoI mutuelle. La reconnexion par login normal répare car la
`CHARACTER_LIST` contient alors le perso avec un `character_id` valide. **Bug et
fix côté client** (le shard est déjà correct).

---

## PR A — Saut réaliste (#2) · client uniquement

- Cible ~0,6 m : `jumpSpeed = √(2·g·h) = √(2·20·0,6) ≈ 4.9` m/s.
- `src/client/gameplay/CharacterController.h` : `jumpSpeed` default `9.0f → 4.9f`.
- `config.json` → `player.movement.jump_speed` : `9.0 → 4.9`.
- **Test** : test unitaire CharacterController — saut depuis le sol, apex ≈ 0,6 m ±5 %.
- **Déploiement** : ✅ client uniquement.

## PR C — Visibilité mutuelle 1ère connexion (#3) · client uniquement

Défense en profondeur, côté client :
1. **Garde dure** (`Engine.cpp` ~7181) : si `characterId == 0`, ne PAS lancer
   `InitGameplayNet` avec une clé résiduelle ; logger une erreur claire et
   forcer un rafraîchissement de `CHARACTER_LIST` au lieu de se connecter en
   « fantôme 1 ».
2. **Propagation fiable de l'id** : garantir que le `character_id` du perso
   fraîchement créé est présent et sélectionné dans la `CHARACTER_LIST`
   rechargée avant d'autoriser « Jouer » (capturer l'id retourné par
   `CHARACTER_CREATE` comme fallback, ou bloquer l'entrée tant que l'id de
   sélection est nul).
3. **Étape 0 (debug confirmatoire)** : instrumenter le cas `characterId==0`,
   reproduire (créer → entrer) pour confirmer le déclencheur exact.
- **Test** : logique de sélection/propagation d'id (non nul après création) +
  garde (refus si id 0).
- **Déploiement** : ✅ client uniquement.

## PR B — Réplication de l'état d'animation (#1) · lock-step client + serveur

1. **Enum partagé** : `enum class AvatarAnimState : uint8_t` dans
   `ReplicationTypes.h` (`Idle, Walk, Run, Sprint, Jump, Fall, Land,
   CrouchIdle, CrouchWalk, Roll, Emote, Attack, …`), aligné sur
   `AvatarLocomotionState` (`Engine.h`). Source de vérité wire.
2. **Client → shard** : ajout de `animationState` au paquet d'input client→shard
   (`HandleInput` + encodeur). Le shard le stocke sur `ConnectedClient`.
3. **Shard → clients** : ajout de `uint8_t animationState` à `SnapshotEntity` ;
   `EncodeSnapshot`/`DecodeSnapshot` écrivent/lisent ; min payload/entité
   56 → 57 octets.
4. **Bump protocole** : `kProtocolVersion 7 → 8` (`ServerProtocol.h`) —
   lock-step obligatoire (double bump, deux directions).
5. **Rendu distant** : `UIRemoteEntity` reçoit `animationState` ;
   `RecordRemoteAvatars` mappe l'état reçu vers le clip (réutilise
   `StateToClipName` / `ClipLoops` + crossfade 150 ms). Fallback dérivation
   vélocité si état inconnu (master/mob legacy).
- **Tests** : round-trip `ServerProtocolTests` (encode/decode avec
  `animationState`, rejet payload tronqué < 57) + mapping état→clip.
- **Déploiement** : ⚠️ redéploiement serveur (shard) requis + client neuf, en
  **lock-step**.

---

## Ordre de merge
1. **PR A** (saut) — indépendante, mergeable immédiatement.
2. **PR C** (visibilité) — client only, indépendante.
3. **PR B** (animation) — dernière, déploiement serveur lock-step (client + shard ensemble).

CODEBASE_MAP.md mis à jour dans chaque PR.
