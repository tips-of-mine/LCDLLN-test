# SERVER-CORE.11_Combat_threat_hostile_decomposition

> **Etat : PARTIEL** (verifie 2026-06-03)
> - Fait / preuves : src/shardd/combat/ThreatList.h;src/shardd/combat/ThreatListTests.cpp
> - Manque : CombatManager/duel absents
> - Resume : Combat partiel

## Objectif

Mettre en place le **cœur du combat** côté shard LCDLLN, inspiré de la
décomposition canonique server-core `src/game/Combat` :

1. **`CombatManager`** par Unit : gère l'entrée/sortie de combat, le
   timer "leaving combat", les ennemis actuels.
2. **`ThreatManager`** par Creature : ordering des cibles selon une
   table de threat (qui aggro le plus). Distinct de "être en combat".
3. **`HostileRefManager`** : graphe **bidirectionnel** "qui menace qui"
   pour O(1) cleanup quand un acteur meurt ou despawn — évite les
   "fantômes d'aggro" sur PNJ orphelins.
4. **State machine** : `OutOfCombat → Combat → LeavingCombat (5s) →
   OutOfCombat` — délai de sortie évite "PvE skip" instantané.
5. **`DuelHandler`** comme mode opt-in séparé du PvP général : duel = zone
   bornée + timer + win-condition à 1 HP. Bon précédent pour "mode arène
   1v1 amical" dans une ville LCDLLN.

C'est un **P2 shard**, pré-requis dès le premier PNJ hostile.

## Dépendances

- M00.1 (build base)
- SERVER-CORE.02 (Entities) — `Unit` porte `CombatManager`, `Creature` porte aussi `ThreatManager`
- SERVER-CORE.03 (Grids) — `MessageDistDeliverer` pour broadcast "X est en combat"
- SERVER-CORE.07 (AI) — l'IA réagit aux events combat

## Livrables

### Côté shard (`engine/server/shard/combat/`)

- `CombatManager.{h,cpp}` — état combat par Unit. API :
  - `EnterCombat(Unit& enemy)` — set state Combat, démarre timer `leaving`
  - `LeaveCombat(LeaveReason reason)` — passe LeavingCombat puis OutOfCombat après délai
  - `IsInCombat() const`
  - `Update(int32 dtMs)` — tick le timer
- `ThreatManager.{h,cpp}` — table de menace par Creature :
  - `AddThreat(Unit& source, float amount)`
  - `GetTopThreatTarget()` — pour aggro
  - `Reset()`
  - `RemoveTarget(Unit& target)` — ex. si target meurt
- `HostileRef.{h,cpp}` — référence bidirectionnelle (intrusive list) entre 2 Units en hostilité.
- `HostileRefManager.{h,cpp}` — gestion par Unit du set d'`HostileRef`. À la mort d'un acteur, `HostileRefManager::DeleteAllReferences()` invalide proprement tous les côtés.
- `DuelHandler.{h,cpp}` — opt-in 1v1 :
  - `StartDuel(Unit& a, Unit& b, Vector3 zoneCenter, float radius, int durationSec)`
  - tick : si un joueur sort de la zone → forfait ; HP < 1 → victoire
  - broadcast `SMSG_DUEL_REQUEST`, `SMSG_DUEL_COMPLETE` (opcodes à allouer)

### Tests

- `engine/server/shard/combat/CombatManagerTests.cpp` — entrée combat, leaving timer, sortie après 5s sans dégât.
- `engine/server/shard/combat/ThreatManagerTests.cpp` — ordering des cibles ; un dégât remet en tête ; reset.
- `engine/server/shard/combat/HostileRefTests.cpp` — A menace B + B menace A → delete A → B a plus de ref vers A.
- `engine/server/shard/combat/DuelHandlerTests.cpp` — sortie de zone = forfait ; HP=1 = victoire.

### Configuration (`config.json`)

```json
"combat": {
  "leaving_combat_timeout_sec": 5,
  "duel_default_duration_sec": 60,
  "duel_default_radius_m": 30.0,
  "threat_decay_per_sec": 0.0
}
```

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. State machine combat

```
OutOfCombat ----enemy attacks----> Combat
Combat ----no damage 5s----> LeavingCombat
LeavingCombat ----receive damage---> Combat (cancel timer)
LeavingCombat ----timer expire----> OutOfCombat
```

### 2. Graphe HostileRef bidirectionnel

```
Player A ---HostileRef---> Creature B
Player A <--HostileRef--- Creature B
```

Quand Creature B meurt :
```cpp
B.GetHostileRefManager().DeleteAllReferences();
// → invalide les HostileRef côté B (trivial : son set est vidé)
// → invalide aussi les HostileRef côté A (chaque ref est intrusive sur A et B)
```

Pas de fantômes : si A consultait `m_threats[guid_B]` après la mort de B, le lookup échoue proprement.

### 3. ThreatManager ordering

Liste triée par threat décroissant. À chaque dégât :
```cpp
m_threats[guid_attacker] += damageAmount;
ResortIfNeeded();
```

`GetTopThreatTarget()` est utilisé par `BaseAI::SelectTarget()` (SERVER-CORE.07).

### 4. DuelHandler

Distinct du PvP général :
- Zone bornée : test `distance(player, zoneCenter) < radius` à chaque tick. Sortie = forfait, HP restauré.
- Timer : `durationSec`. Égalité au timer = match nul (pas de gain rep, pas de gold).
- Win condition : un joueur < 1 HP → victoire. Pas mort, pas de butin, juste un message.
- Pas de `HostileRefManager` standard : duel utilise un mode `DuelMode` séparé pour ne pas polluer le combat système.

## Étapes d'implémentation

1. Créer `engine/server/shard/combat/`.
2. Implémenter `HostileRef` + `HostileRefManager` (le plus isolé).
3. Implémenter `ThreatManager` (Creature uniquement).
4. Implémenter `CombatManager` (par Unit, state machine).
5. Câbler `Unit::DealDamage` → met les deux côtés en combat + ajoute threat si Creature.
6. Implémenter `DuelHandler` + opcodes nouveaux (`SMSG_DUEL_REQUEST/ACCEPT/COMPLETE`).
7. Tests : 4 fichiers listés.
8. Doc : section « Combat shard » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard)
- [ ] Tests passent (4 fichiers)
- [ ] Smoke test : 1 player attaque 1 Creature → les 2 en combat ; player s'éloigne 5s sans dégât → out of combat
- [ ] Smoke test duel : 2 players acceptent duel, l'un sort de la zone → forfait
- [ ] HostileRef cleanup : Creature meurt → l'attaquant n'a plus la ref
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **PvE skip** : sans le timer `LeavingCombat`, un joueur sort instantanément du combat dès qu'il fuit l'attaquant pendant 1 tick → exploit régen full HP. Le timer 5s est crucial.
- **Threat decay** : optionnel (défaut 0). Si activé, un PNJ peut "oublier" un attaquant inactif. Évite les chains d'aggro à perpétuité, mais peut casser l'équilibre raid. **Démarrer à 0**, profiler avant d'activer.
- **Duel et chat** : pendant un duel, désactiver les whispers (sinon abus de macro distraction). Hook côté `ChatGate` (SERVER-CORE.01) avec un état `IsInDuel`.
- **Reconnexion en combat** : si un joueur déconnecte en combat, la Creature continue d'attaquer son fantôme → bug. Au logout, soit réussir LeaveCombat (reconnexion = reset), soit auto-mort (server-core style "logout in combat = die").

## Références

- `SERVER-CORE_ANALYSIS.md` § Combat (P2 shard)
- server-core `src/game/Combat/CombatManager.cpp`, `ThreatManager.cpp`,
  `HostileRefManager.cpp`, `DuelHandler.cpp`
