# CMANGOS.16 — Globals (Conditions / ObjectAccessor / Graveyard / Locales)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.16_Globals_conditions_objectaccessor_locales.md](../../../../tickets/CMANGOS/CMANGOS.16_Globals_conditions_objectaccessor_locales.md)
> **Priorité** : P2 — gameplay essentiel (game-changer data-driven)
> **Cible** : shard

## 1. Statut implémentation

❌ **Absent** — pas de `ConditionMgr` data-driven, pas d'`ObjectAccessor`
façade, pas de `GraveyardManager`, pas de `LocaleStrings` côté shard.
La sélection de locale existe dans l'UI auth (différent).

## 2. Preuves dans le code

**Existant (orthogonal) :**
- [engine/render/auth/screens/AuthImGuiOptions.cpp](../../../../engine/render/auth/screens/AuthImGuiOptions.cpp) — UI sélection locale
  (côté client auth, pas localisation strings serveur)
- [engine/client/auth/screens/AuthScreenLanguageSelect.cpp](../../../../engine/client/auth/screens/AuthScreenLanguageSelect.cpp) — choix
  langue côté client
- [engine/server/TermsRepository.cpp](../../../../engine/server/TermsRepository.cpp) — repo CGU multi-locale (mais sur
  un cas spécifique CGU, pas un système Locales général)

**Manquant (vs spec ticket) :**
- ❌ `engine/server/shard/globals/` — dossier inexistant
- ❌ `Condition` struct + `ConditionType` enum (~15 types : `HasAura`,
  `LevelGE`, `InGroup`, `HasItem`, `QuestState`, `ZoneId`,
  `Reputation`, etc.)
- ❌ `ConditionGroup` (composition AND/OR/NOT)
- ❌ `ConditionMgr` singleton avec `Evaluate()`
- ❌ `ObjectAccessor` façade thread-safe (`GetPlayer/GetCreature/GetGameObject`)
- ❌ `GraveyardManager` (zones + faction + closest valid)
- ❌ `LocaleStrings` cache `(stringId, locale)` avec fallback
- ❌ Migration DB `conditions`, `condition_groups`, `graveyards`,
  `locale_strings`
- ❌ Config

## 3. Recouvrement milestones existantes

❌ **Non couvert** — aucune milestone "Conditions data-driven" ni
"ObjectAccessor" ni "Graveyard". TermsRepository est un cas spécifique
CGU, pas un système général.

## 4. Écart par rapport à la spec CMANGOS

100% absent. C'est un **multiplicateur** pour tout le contenu
data-driven : sans `ConditionMgr`, chaque ticket downstream qui veut
filtrer un drop / une quête / un script en C++ va réimplémenter sa
propre version de "le joueur est en groupe / a un buff / a fait telle
quête". Énorme dette en perspective.

Le ticket le note explicitement :
> Pré-requis pour : CMANGOS.17 (Loot), CMANGOS.23 (Quests),
> CMANGOS.14 (DBScripts), CMANGOS.07 (AI/EventAI), CMANGOS.26 (Spells).

## 5. Effort estimé

**M-L** (3 PR) :
- PR 1 : `Condition` + `ConditionGroup` + `ConditionMgr` + 5-10 types
  de base + tests d'évaluation par type
- PR 2 : `ObjectAccessor` thread-safe (façade au-dessus des registries
  shard existants)
- PR 3 : `GraveyardManager` + `LocaleStrings` + migration DB

Pas de wire-breaking. Migration DB substantielle (4 tables nouvelles)
mais simples.

## 6. Valeur joueur/serveur

**Élevée → Critique pour le contenu** — invisible joueur direct,
**critique pour la productivité** des ajouts de contenu downstream.

Sans ConditionMgr, chaque feature P2 réimplémente ses propres prédicats.
Avec, on a un mini-DSL réutilisé partout, designer scripts en SQL.

## 7. Dépendances bloquantes

Le ticket dépend explicitement de :
- **CMANGOS.13 Database** — `SQLStorage` pour cache des conditions au
  boot.

→ **CMANGOS.16 est un déblocant amont** pour 5 tickets P2 downstream.

## 8. Risque / piège ⚠️

- ⚠️ **Migration DB** — 4 nouvelles tables (`conditions`,
  `condition_groups`, `graveyards`, `locale_strings`). Idempotent.
- ⚠️ **ConditionMgr cycles** — un ConditionGroup référence d'autres
  groups. Détection cycles au load obligatoire (DFS).
- ⚠️ **ObjectAccessor thread-safety** — façade thread-safe via
  `std::shared_mutex`. Mauvais usage = deadlock ou contention. Tester
  sous load.
- ⚠️ **GraveyardManager edge case** — joueur faction X mort dans zone
  faction Y → quel graveyard ? Politique à expliciter (zone d'origine,
  graveyard neutre, fallback).
- ⚠️ **LocaleStrings fallback** — si stringId manquant pour locale FR,
  fallback EN. Si manquant aussi en EN, log warning + return raw key.
- ⚠️ **Hot-reload conditions** — si GM modifie `conditions` table,
  hot-reload via `.reload conditions` (CMANGOS.14 DBScripts pattern).
  Atomic swap.
- ⚠️ **Custom conditions** (`ConditionType::Custom` + handler_id) —
  porte ouverte sur la complexité. Convention claire pour limiter
  l'usage (gros risque de "Custom partout" qui annule l'intérêt
  data-driven).
- Pas de wire-breaking côté protocole.

## 9. Recommandation finale

✅ **Faire en l'état**, **priorité haute** — c'est un **déblocant amont**
pour beaucoup de P2 downstream :

1. **Étape 0** : valider ordre — CMANGOS.16 **avant** CMANGOS.17
   Loot, .23 Quests, .14 DBScripts, .07 AI EventAI.
2. **Étape 1** : implémenter `Condition`/`ConditionGroup`/`ConditionMgr`
   avec 5-10 types essentiels (LevelGE, InGroup, HasItem, QuestState,
   ZoneId — couvre 80% des usages).
3. **Étape 2** : `ObjectAccessor` thread-safe façade (refactor du code
   shard existant qui fait des lookups maps).
4. **Étape 3** : `GraveyardManager` (peut être simple : zone → graveyard
   par faction, flat table).
5. **Étape 4** : `LocaleStrings` (cache load au boot, GetString avec
   fallback).
6. **Étape 5** : ajout de types Condition au fil des besoins downstream.

À planifier **tôt** dans la roadmap P2 (juste après CMANGOS.13 Database
qu'il consomme).

---

*Audit du 2026-05-08. Mises à jour : —*
