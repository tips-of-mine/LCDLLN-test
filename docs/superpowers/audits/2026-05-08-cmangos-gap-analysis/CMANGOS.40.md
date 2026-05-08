# CMANGOS.40 — Tools (playerdump / cleaner / formulas / language)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.40_Tools_playerdump_cleaner_formulas_language.md](../../../../tickets/CMANGOS/CMANGOS.40_Tools_playerdump_cleaner_formulas_language.md)
> **Priorité** : P3 — ajout à valeur (ops + balancing)
> **Cible** : cross (master + shard)

## 1. Statut implémentation

❌ **Absent** — pas de `PlayerDump`, pas de `CharacterDatabaseCleaner`
périodique, pas de `Formulas.h` centralisé, pas de `Language.h`
server-side.

## 2. Preuves dans le code

**Manquant :**
- ❌ `PlayerDump` (sérialisation texte perso → restorable cross-shard)
- ❌ `CharacterDatabaseCleaner` async (orphelins items/mails)
- ❌ `Formulas.h` (XP per kill, money loot, level penalty centralisés)
- ❌ `Language.h` server-side i18n (broadcasts, GM messages)

## 3. Recouvrement milestones existantes

❌ **Non couvert** pour ces 4 utilitaires.

## 4. Écart par rapport à la spec CMANGOS

100% absent. 4 utilitaires distincts, pertinence variable :

1. **PlayerDump** — utile pour migration inter-shard et restore après
   désastre. Critique si multi-shard.
2. **DBCleaner** — anti dette DB. Utile en prod.
3. **Formulas** — équilibrage centralisé. Critique pour économie/XP.
4. **Language** — i18n serveur. Utile si multi-locale.

## 5. Effort estimé

**M** (3 PR — 1 par utilitaire majeur) :
- PR 1 : `PlayerDump` format + tools/ binary
- PR 2 : `DBCleaner` cron + tests
- PR 3 : `Formulas.h` + migration code existant
- (Language couvert par CMANGOS.16 LocaleStrings, redondant)

## 6. Valeur joueur/serveur

**Moyenne (pré-launch) → Élevée (post-launch)** — invisible joueur,
critique ops + balancing.

## 7. Dépendances bloquantes

- **CMANGOS.13 Database**
- **CMANGOS.16 Globals/Conditions** — LocaleStrings (alternative pour
  Language)

## 8. Risque / piège ⚠️

- ⚠️ **PlayerDump versioning** — format texte évolue → tooling backward
  compat à acter.
- ⚠️ **DBCleaner agressif** — peut détruire des données légitimes si
  mal calibré. Dry-run mode obligatoire + log avant suppression.
- ⚠️ **Formulas hot-reload** — re-balance live sans restart. Bonus
  désiré, complexe à faire safe.
- ⚠️ **Language redondance avec LocaleStrings** — éviter doublon. Le
  ticket lui-même note que `LocaleStrings` est l'équivalent runtime.

## 9. Recommandation finale

🔧 **Adapter et faire**, mais **par utilitaire séparément** (effort
fragmentable) :

1. **Étape 1 (priorité)** : `Formulas.h` centralisé (gain équilibrage
   immédiat).
2. **Étape 2** : `DBCleaner` cron (anti dette prod).
3. **Étape 3** : `PlayerDump` (utile si migration inter-shard
   probable).
4. **Étape 4 (skip)** : `Language.h` couvert par `LocaleStrings`
   (CMANGOS.16).

À planifier opportunément (1 PR par utilitaire). Pas urgent.

---

*Audit du 2026-05-08. Mises à jour : —*
