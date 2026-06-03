# Issue: SERVER-CORE.39

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- src/shardd/skills/SkillBook.h
- src/shared/network/SkillPayloads.h

## Note
Skills craft/discovery/proc

---

## Contenu du ticket (SERVER-CORE.39)

# SERVER-CORE.39_Skills_craft_discovery_extra_item_proc

## Objectif

Mettre en place les **hooks data-driven du craft** côté shard LCDLLN,
inspirés de `src/game/Skills` server-core. Deux piliers :

1. **Discovery** : `skill_discovery_template` (spell_required,
   spell_discovered, chance) — unlock probabiliste de recettes via
   crafting. Pattern « progression cachée » sympa.
2. **Extra item proc** : `skill_extra_item_template` (chance,
   items_per_craft) appliqué post-craft. Découple « la recette donne
   X » de « parfois bonus Y ».

Tout en SQL : zéro hardcoded, designers ajoutent recettes sans toucher
au code.

C'est un **P3 shard**, **uniquement si LCDLLN prévoit du craft**. Sinon
ignorer.

## Dépendances

- M00.1 (build base)
- SERVER-CORE.13 (Database)
- Système de craft existant (préalable fonctionnel)
- SERVER-CORE.26 (Spells — la recette EST un sort)

## Livrables

### Côté shard (`engine/server/shard/skills/`)

- `SkillDiscovery.{h,cpp}` :
  - `Load(ConnectionPool&)` — charge les rows.
  - `bool TryDiscover(Player&, uint32_t spellRequiredId)` — appelé
    après un craft réussi ; tire vs chance ; si succès, apprend
    `spellDiscoveredId` au joueur.
- `SkillExtraItem.{h,cpp}` :
  - `Load(ConnectionPool&)`.
  - `int RollExtraItems(Player&, uint32_t spellId)` — retourne le
    nombre additionnel à donner (0 si pas de proc).

### Migration DB

```sql
CREATE TABLE skill_discovery_template (
  spell_required_id   INT UNSIGNED NOT NULL,
  spell_discovered_id INT UNSIGNED NOT NULL,
  chance              FLOAT NOT NULL,
  description         VARCHAR(255),
  PRIMARY KEY (spell_required_id, spell_discovered_id)
);

CREATE TABLE skill_extra_item_template (
  spell_id            INT UNSIGNED NOT NULL,
  chance              FLOAT NOT NULL,
  min_extra_items     TINYINT UNSIGNED NOT NULL DEFAULT 0,
  max_extra_items     TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (spell_id)
);
```

### Configuration (`config.json`)

```json
"craft": {
  "discovery_enabled": true,
  "extra_item_enabled": true
}
```

### Tests

- `SkillDiscoveryTests.cpp` — chance 100% → discovery garantie ;
  chance 0% → jamais.
- `SkillExtraItemTests.cpp` — proc retourne dans la range min/max.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Étapes d'implémentation

1. Créer `engine/server/shard/skills/`.
2. Migrations DB.
3. Implémenter `SkillDiscovery` + `SkillExtraItem`.
4. Câbler dans le hook craft : `OnCraftSuccess(player, spell)` →
   `TryDiscover(...)` + `RollExtraItems(...)`.
5. Tests : 2 fichiers.
6. Doc : section « Skills hooks » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard)
- [ ] Tests passent
- [ ] Smoke test : recipe X craftée 1000 fois, discovery moyenne ≈ chance config
- [ ] Migrations idempotentes
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Discovery déjà connue** : si le joueur a déjà appris `spellDiscoveredId`, ne pas re-tirer.
- **Faim de chance** : les designers vont vouloir des chances très basses (0.01% pour la recette ultime). RNG correct, pas de pseudo-bad luck protection sauf décision.
- **Extra item rarity** : un proc à 100% = bonus systématique (économie inflatée). Surveiller via metrics.
- **Pas pour le MVP si pas de craft** : si LCDLLN n'a pas (encore) de système de craft, ce ticket attend.

## Références

- `SERVER-CORE_ANALYSIS.md` § Skills (P3 shard)
- server-core `src/game/Skills/SkillDiscovery.cpp`,
  `SkillExtraItems.cpp`
