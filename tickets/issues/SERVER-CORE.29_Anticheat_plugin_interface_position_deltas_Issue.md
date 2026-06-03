# Issue: SERVER-CORE.29

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- src/shardd/anticheat/AntiCheatGameplay.h

## Note
Anticheat position deltas

---

## Contenu du ticket (SERVER-CORE.29)

# SERVER-CORE.29_Anticheat_plugin_interface_position_deltas

## Objectif

Réserver une **interface plugin anticheat** côté shard LCDLLN, inspirée
de `src/game/Anticheat` server-core. Trois piliers :

1. **Architecture plugin** (interface abstraite `IServerSideValidator`
   + impl chargeable) — permet une nouvelle heuristique sans rebuild
   du shard.
2. **Validation deltas de position** côté serveur : recalcul de la
   vitesse réelle entre deux paquets `MoveUpdate` et comparaison au
   max théorique du joueur (buffs inclus). Pattern critique pour le
   shard.
3. **Hook sur les commandes chat** — l'anticheat peut intercepter pour
   logger/bloquer ; utile pour détecter spam de slash commands ou
   exploits par macros.

C'est un **P3 shard**. Pas prioritaire MVP, mais **réserver l'interface
maintenant** évite une refonte plus tard quand les premières triches
apparaîtront.

## Dépendances

- M00.1 (build base)
- SERVER-CORE.04 (Movement) — l'anticheat consulte les MoveSpline pour
  vitesse théorique
- SERVER-CORE.05 (vmap) — anti-fly-hack via height check
- SERVER-CORE.01 (Chat) — hook intercept

## Livrables

### Côté shard (`engine/server/shard/anticheat/`)

- `IServerSideValidator.{h,cpp}` — interface abstraite :
  ```cpp
  class IServerSideValidator {
  public:
    virtual ~IServerSideValidator() = default;
    virtual bool ValidateMove(Player& p, Vector3 newPos, int64_t serverTimeMs) = 0;
    virtual bool ValidateChat(Player& p, ChatChannel ch, std::string_view msg) = 0;
    virtual bool ValidateSpellCast(Player& p, SpellId spell, Unit* target) = 0;
    virtual void OnViolation(Player& p, ViolationType, std::string_view detail) = 0;
  };
  ```
- `NoOpValidator.{h,cpp}` — impl par défaut, valide tout. Utilisée si
  `anticheat.enabled = false`.
- `BaseValidator.{h,cpp}` — impl minimale :
  - ValidateMove : compare vitesse réelle vs `Player::GetMaxSpeed`.
  - ValidateSpellCast : vérifie cooldown + range + LOS (vmap).
  - ValidateChat : passe (SERVER-CORE.01 ChatGate fait déjà l'essentiel).
- `ValidatorRegistry.{h,cpp}` — singleton, permet de remplacer le
  validator runtime via config.

### Configuration (`config.json`)

```json
"anticheat": {
  "enabled": false,
  "validator_name": "BaseValidator",
  "speed_tolerance_factor": 1.10,
  "speed_violation_threshold_count": 5,
  "violation_action": "log_only",     // log_only | kick | ban_24h
  "fly_hack_height_threshold_m": 50.0
}
```

### Tests

- `BaseValidatorTests.cpp` — speed hack détecté (déplacement > maxSpeed × tolerance).
- `BaseValidatorChatTests.cpp` — passes through (déjà filtré par ChatGate).
- `ValidatorRegistryTests.cpp` — swap validator runtime.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. Validation des deltas de position

```cpp
bool BaseValidator::ValidateMove(Player& p, Vector3 newPos, int64_t serverTimeMs) {
  auto last = p.GetLastValidatedPosition();
  auto deltaTime = (serverTimeMs - last.timestampMs) / 1000.0;
  auto distance = (newPos - last.pos).Length();
  auto observedSpeed = distance / deltaTime;
  auto maxSpeed = p.GetMaxSpeed() * config.speed_tolerance_factor;
  if (observedSpeed > maxSpeed) {
    OnViolation(p, ViolationType::SpeedHack,
                std::format("observed={:.2f} max={:.2f}", observedSpeed, maxSpeed));
    return false;
  }
  return true;
}
```

Tolérance configurable (défaut +10%) pour éviter faux positifs sur
RTT/jitter.

### 2. Violation count

Un seul faux positif n'est pas une preuve. Compter sur
`speed_violation_threshold_count` violations consécutives avant action.

### 3. Action sur violation

- `log_only` (défaut) : log warning, pas de sanction.
- `kick` : déconnecte le joueur, audit log.
- `ban_24h` : ban temporaire compte.

Toutes les violations vont dans `SecurityAuditLog` (existant côté master)
+ `LogFilter::Auth`.

## Étapes d'implémentation

1. Créer `engine/server/shard/anticheat/`.
2. Implémenter `IServerSideValidator` interface.
3. Implémenter `NoOpValidator` (default).
4. Implémenter `BaseValidator` (speed check + spell sanity).
5. Implémenter `ValidatorRegistry` (lookup par nom config).
6. Câbler `Player::OnMoveUpdate` → `validator.ValidateMove(...)`.
7. Tests : 3 fichiers.
8. Doc : section « Anticheat shard » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard)
- [ ] Tests passent
- [ ] Smoke test : avec `enabled=true`, déplacement à 2× speed → violation log + count incrémenté
- [ ] `enabled=false` → comportement identique avant ce ticket (NoOpValidator)
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Faux positifs RTT** : un client avec 200ms ping peut envoyer 2 paquets de mouvement séparés par 50ms réels mais 250ms perçus. Tolérance + lissage de la vélocité observée sur N samples.
- **Téléport légitime** : un sort de téléport, un knockback PvP sont des changements abrupts mais valides. Le validator doit consulter `Player::GetLastTeleport()` ou un flag "expecting teleport" pour ignorer un saut prévu.
- **Plugin chargeable runtime** : pour vraiment "remplacer sans rebuild", il faudrait `dlopen`/`LoadLibrary`. Compromis : recompile + restart shard suffit pour la v1, plugin runtime = futur ticket.
- **Privacy log** : un ban audit doit contenir l'IP + l'accountId mais pas la position complète (privacy mineure).
- **Double check serveur** : `vmap.GetHeight` peut être utilisé pour un anti-fly-hack basique. Si le joueur reste à > `fly_hack_height_threshold_m` au-dessus du sol pendant N secondes sans buff Flying → violation.

## Références

- `SERVER-CORE_ANALYSIS.md` § Anticheat (P3 shard)
- server-core `src/game/Anticheat/Anticheat.hpp`, `module/`
