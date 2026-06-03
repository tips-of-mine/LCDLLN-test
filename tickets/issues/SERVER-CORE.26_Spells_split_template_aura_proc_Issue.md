# Issue: SERVER-CORE.26

**Status:** Closed

_Re-verifie DONE le 2026-06-03 (correction d'un faux-negatif du au decalage de chemins engine/ -> src/)._

## Preuves d'implementation
- src/shardd/spell/SpellTemplate.h
- src/shardd/spell/SpellMgr.h

## Note
SpellTemplate/FamilyMask/SpellMgr + tests sous src/shardd/spell (faux-negatif corrige)

---

## Contenu du ticket (SERVER-CORE.26)

# SERVER-CORE.26_Spells_split_template_aura_proc

> **Etat : PARTIEL** (verifie 2026-06-03)
> - Fait / preuves : src/shardd/spell/SpellFamilyMask.h
> - Manque : SpellTemplate/Aura/Proc/Effects absents
> - Resume : Spells partiel (mask seul)

## Objectif

Mettre en place le **système de sorts/abilities** côté shard LCDLLN
inspiré de `src/game/Spells` server-core. Cinq piliers :

1. **Split `Spell` / `SpellTemplate` / `SpellAura` / `ProcEvent`** :
   séparation claire entre l'**instance** (cast en cours, durée), la
   **définition immuable** (DB), l'**effet appliqué** (buff/debuff
   actif sur une cible) et la **réactivité** (proc on hit/crit/etc.).
2. **State machine de cast** : `PREPARING → CASTING → FINISHED` avec
   timers (cast time, GCD, channel). Modèle générique applicable à
   toute action joueur à durée non-instantanée (récolte, build, drink).
3. **Aura proc system** : `SpellProcEventEntry` (procFlags bitmask,
   ppmRate, cooldown) déclenche effects sur events (melee hit, spell
   crit). Pattern « réactivité data-driven ».
4. **`SpellFamily` + `ClassFamilyMask`** : permet « ce talent buffe
   tous les sorts feu du mage » sans énumérer chaque sort.
5. **Stacking rules séparées** (`SpellStacking.cpp` isolé) : algèbre des
   buffs (replace, refresh, sum) raisonnable indépendamment des
   effects.

C'est un **P2 shard**, pré-requis dès le premier sort/ability joueur ou
PNJ. **Gros morceau** — ce ticket trace le découpage canonique mais ne
porte pas le contenu (chaque sort sera un suivi).

## Dépendances

- M00.1 (build base)
- SERVER-CORE.02 (Entities) — Unit porte la liste d'auras actives
- SERVER-CORE.04 (Movement) — un cast peut être interrompu par mouvement
- SERVER-CORE.05 (vmap) — LOS check avant le cast
- SERVER-CORE.11 (Combat) — un cast hostile met en combat
- SERVER-CORE.13 (Maps) — `Spell::Update` tickté par la Map
- SERVER-CORE.16 (Globals/Conditions) — `SpellTemplate.required_condition`

## Livrables

### Côté shard (`engine/server/shard/spells/`)

- `SpellTemplate.{h,cpp}` — définition immuable d'un sort. Chargée DB au
  boot via `SQLStorage` cache (SERVER-CORE.13). Champs : id, name, school,
  family, family_mask, range, cast_time, cooldown, gcd, mana_cost,
  effects[3] (effect_type, effect_param, target_type, base_value),
  proc_chance, proc_flags, attributes (bitmask), required_condition_id,
  cast_interrupted_by_movement.
- `Spell.{h,cpp}` — **instance** d'un cast en cours sur un Unit. Tient
  l'état machine (Preparing/Casting/Finished), timers, target résolu,
  payload final.
- `SpellAura.{h,cpp}` — **effet appliqué** sur une cible : référence vers
  SpellTemplate, durée restante, stacks, source (qui a appliqué), tick
  périodique pour DoT/HoT.
- `SpellAuraHolder.{h,cpp}` — collection d'auras d'un Unit, indexée par
  SpellTemplate id pour stacking O(1).
- `SpellProcEvent.{h,cpp}` — système réactif. Quand un event (melee hit,
  spell crit, target dies) survient, parcourt les auras de la source/
  cible cherchant des `SpellAura` dont les `procFlags` matchent.
- `SpellTargeting.{h,cpp}` — targeting matrix : un `target_type` (enum :
  TARGET_UNIT_ENEMY, TARGET_AREA_FRIEND…) résout en liste d'Units selon
  filtres. Centralise la logique au lieu de switch géants.
- `SpellStacking.{h,cpp}` — règles de cumul : replace, refresh, sum,
  reject. Algèbre testable isolément.
- `SpellEffects.{h,cpp}` — registry des effects (`EFFECT_DAMAGE`,
  `EFFECT_HEAL`, `EFFECT_APPLY_AURA`, `EFFECT_TELEPORT`, `EFFECT_SUMMON`,
  …). Chaque effect = une fonction `void Apply(Unit& caster, Unit&
  target, SpellTemplate const&, int effectIdx)`.

### Migration DB

```sql
CREATE TABLE spell_template (
  id              INT UNSIGNED NOT NULL,
  name            VARCHAR(128) NOT NULL,
  school          TINYINT UNSIGNED NOT NULL,
  family          TINYINT UNSIGNED NOT NULL,
  family_mask_lo  BIGINT UNSIGNED NOT NULL DEFAULT 0,
  family_mask_hi  BIGINT UNSIGNED NOT NULL DEFAULT 0,
  range_yds       FLOAT NOT NULL DEFAULT 0,
  cast_time_ms    INT UNSIGNED NOT NULL DEFAULT 0,
  cooldown_ms     INT UNSIGNED NOT NULL DEFAULT 0,
  gcd_ms          INT UNSIGNED NOT NULL DEFAULT 1500,
  mana_cost       INT UNSIGNED NOT NULL DEFAULT 0,
  duration_ms     INT NOT NULL DEFAULT 0,    -- <0 = passif
  effect1_type    TINYINT UNSIGNED NOT NULL DEFAULT 0,
  effect1_target  TINYINT UNSIGNED NOT NULL DEFAULT 0,
  effect1_value   INT NOT NULL DEFAULT 0,
  effect1_param   INT NOT NULL DEFAULT 0,
  effect2_*, effect3_*  -- idem
  proc_chance     TINYINT UNSIGNED NOT NULL DEFAULT 0,
  proc_flags      INT UNSIGNED NOT NULL DEFAULT 0,
  attributes      INT UNSIGNED NOT NULL DEFAULT 0,
  required_condition_id INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (id)
);

CREATE TABLE creature_spell (
  creature_entry  INT UNSIGNED NOT NULL,
  spell_id        INT UNSIGNED NOT NULL,
  PRIMARY KEY (creature_entry, spell_id)
);
```

### Configuration (`config.json`)

```json
"spells": {
  "max_simultaneous_auras_per_unit": 50,
  "default_gcd_ms": 1500,
  "los_check_required": true,
  "movement_interrupts_cast": true
}
```

### Tests

- `SpellTargetingTests.cpp` — TARGET_UNIT_ENEMY filtre les amis ; TARGET_AREA_FRIEND inclut alliés en zone.
- `SpellStackingTests.cpp` — cumul `replace` remplace ; `refresh` reset durée ; `sum` additionne ; `reject` ignore.
- `SpellAuraTests.cpp` — DoT tick toutes les 3s, expire à durée 0.
- `SpellProcEventTests.cpp` — `procFlags = ON_MELEE_HIT_TAKEN` déclenche bien l'effect au coup reçu, respecte cooldown.
- `SpellTemplateLoadTests.cpp` — round-trip DB load.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. State machine cast

```
            cast_time>0
PREPARING --------------> CASTING (channel ou not)
   |                          |
   |  (cast_time=0 instant)   |
   v                          v
FINISHED <-----------------+ Apply effects
   |                          |
   |    (interrupt)           |
   +--------------------------+
```

Interrupts : mouvement (si `cast_interrupted_by_movement`), dégât pris,
silence.

### 2. Aura lifecycle

```cpp
class SpellAura {
public:
  void Apply(Unit& target, Unit& source, SpellTemplate const&);
  void Update(int32 dtMs);   // tick si DoT/HoT, decrement duration
  bool Expired() const;
  void Remove(AuraRemoveMode mode);    // EXPIRED, DISPELLED, OVERWRITTEN
};
```

### 3. Stacking rules

```cpp
enum class StackingRule : uint8 {
  Replace,    // nouveau remplace ancien (1 stack)
  Refresh,    // reset duration, max 1 stack
  Sum,        // accumule jusqu'à max_stacks
  Reject,     // si déjà présent, refuse l'application
  PerCaster,  // 1 par caster, pas de fusion entre casters
};
```

### 4. Family mask

```cpp
struct SpellFamilyFlag {
  uint64_t lo;
  uint64_t hi;
};
```

Permet `talent.affects = SpellFamilyFlag{lo: 0x000F, hi: 0}` pour buffer
tous les sorts feu du mage sans énumérer leurs IDs.

### 5. Proc system

```cpp
enum class ProcFlag : uint32 {
  OnMeleeHitDealt    = 1u << 0,
  OnMeleeHitTaken    = 1u << 1,
  OnSpellHitDealt    = 1u << 2,
  OnSpellHitTaken    = 1u << 3,
  OnSpellCritDealt   = 1u << 4,
  OnSpellCritTaken   = 1u << 5,
  OnTargetDeath      = 1u << 6,
  OnSelfDeath        = 1u << 7,
  // ...
};
```

À chaque event combat, parcourir les auras avec ProcFlag matchant ;
tirer une RNG vs `proc_chance` ; respecter cooldown intra-aura ;
appliquer l'effect.

## Étapes d'implémentation

1. Créer `engine/server/shard/spells/`.
2. Migration DB `spell_template` + chargement via SQLStorage (SERVER-CORE.13).
3. Implémenter `SpellTemplate` + chargement.
4. Implémenter `Spell` (instance, state machine cast).
5. Implémenter `SpellEffects` (registry des effect types — démarrer avec DAMAGE, HEAL, APPLY_AURA).
6. Implémenter `SpellAura` + `SpellAuraHolder`.
7. Implémenter `SpellTargeting` (registry des target types).
8. Implémenter `SpellStacking` (règles isolées).
9. Implémenter `SpellProcEvent` (réactivité).
10. Câbler `Unit::CastSpell(spellId, target)` qui crée un `Spell`, l'ajoute au tick.
11. Tests : 5 fichiers listés.
12. Doc : section « Spells shard » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard)
- [ ] Tests passent (5 fichiers)
- [ ] Smoke test : 1 player cast "Fireball" 1.5s, applique damage à la cible ; mouvement pendant cast → interruption
- [ ] Smoke test DoT : applique 12s/tick 3s → 4 ticks puis expire
- [ ] Smoke test proc : aura "ProcOnMeleeHitTaken → Heal 50" déclenchée à chaque coup reçu (avec proc_chance = 100)
- [ ] Stacking : `Refresh` reset la durée
- [ ] Migration `spell_template` appliquée et idempotente
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Ne pas porter les sorts WoW** : copier le **découpage** est légitime, copier les contenus (ID 133 = Fireball avec 1.5s cast 30 yards…) est juridiquement risqué. Créer **vos propres** sorts dans `spell_template`.
- **Performance** : le proc system est appelé à chaque hit/crit/melee. **Pas de virtual call dans la hot path**. Itérer sur un `std::vector<SpellAura*>` plutôt que `std::map`.
- **Aura limit per unit** : sans plafond, un boss peut accumuler 1000 auras → tick coûteux. `max_simultaneous_auras_per_unit = 50` évite ça (dépasser = remove le plus ancien dispellable).
- **Channeled spells** : différents de cast classiques (effect appliqué périodiquement pendant le cast). State `CASTING` avec ticks dédiés. À gérer mais reportable au début.
- **GCD vs cooldown** : 2 timers distincts. GCD (Global Cooldown ~1.5s) bloque tous les sorts ; le cooldown spécifique bloque ce sort. Le mage caste Fireball → GCD 1.5s, mais Fireball cooldown peut être 0 (insta-recast).
- **Targeting Z-axis** : pour les AoE au sol (TARGET_DEST_DEST), le ground point validé via `vmap.GetHeight` (SERVER-CORE.05).
- **Spell range vs LOS** : range OK ne signifie pas LOS OK. Toujours vérifier les deux. Erreur classique = "j'ai range mais le mur me bloque" → frustrant côté joueur.
- **Auras passives** : `duration_ms < 0` = passif (talent permanent). Ne pas tick, ne pas expirer. Filtrage dans `Update`.
- **Stacking PerCaster vs Sum** : un Bleed appliqué par 2 rogues ne stack pas en `Sum` (=2× damage) mais en `PerCaster` (2 instances séparées). Sinon multi-rogue exploit.

## Références

- `SERVER-CORE_ANALYSIS.md` § Spells (P2 shard)
- server-core `src/game/Spells/Spell.cpp`, `SpellAuras.cpp`, `SpellEffects.cpp`,
  `SpellMgr.cpp`, `SpellStacking.cpp`, `SpellTargeting.cpp`, `Unit.cpp`
  (proc dispatch)
