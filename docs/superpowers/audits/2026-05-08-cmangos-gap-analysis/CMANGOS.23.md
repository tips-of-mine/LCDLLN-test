# CMANGOS.23 — Quests (template / status / objectives variant)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.23_Quests_template_status_objectives_variant.md](../../../../tickets/CMANGOS/CMANGOS.23_Quests_template_status_objectives_variant.md)
> **Priorité** : P2 — gameplay essentiel
> **Cible** : cross (master + shard)

## 1. Statut implémentation

❌ **Absent** (vs spec cmangos C++20). M15.1 milestone existe pour
quêtes data-driven JSON mais selon une approche divergente (JSON vs
SQL, pas de `std::variant` typed).

## 2. Preuves dans le code

**Existant :**
- M15.1 — Quests data-driven JSON progress events (milestone existante,
  livraison à vérifier)
- (Aucun fichier `Quest*.{h,cpp}` ni `QuestMgr` détecté dans `engine/`)

**Manquant (vs spec ticket) :**
- ❌ `engine/server/quests/` — dossier inexistant
- ❌ `QuestTemplate` (immuable, SQLStorage) + `QuestStatusData`
  (per-player) split
- ❌ `QuestObjective` `std::variant` (KillCredit, CollectItem,
  InteractGameObject, VisitArea, CastSpell, ...)
- ❌ Flags bitmask `Daily`/`Weekly`/`Repeatable`/`AutoComplete`
- ❌ Choice items reward (6 choice + 4 given)
- ❌ Chaînes `prevQuestId`/`nextQuestId`
- ❌ Tables DB `quest_template`, `quest_objectives`, `quest_chain`,
  `character_quest_status`
- ❌ Hooks `quest_start_scripts` / `quest_end_scripts` (CMANGOS.14)

## 3. Recouvrement milestones existantes

✅ **Couvert (en théorie, approche différente)** — M15.1 est explicitement
le ticket LCDLLN pour quêtes. Mais l'approche **JSON data-driven** vs
**SQL + std::variant cmangos** crée une divergence architecturale.

À auditer : que livre M15.1 exactement ? Si JSON-driven uniquement, la
spec cmangos est différente.

## 4. Écart par rapport à la spec CMANGOS

L'écart **architectural** est important :

1. **JSON vs SQL** — LCDLLN tend vers JSON content (cf. SpawnerRuntime,
   archetypes). cmangos tend vers SQL. Choix architectural à valider.
2. **`std::variant` objectives** — pattern C++20 type-safe, plus
   moderne que les arrays parallèles cmangos. Avantage majeur.
3. **Split template/status** — économie RAM critique (100k joueurs ×
   1000 quêtes possibles, le `QuestStatusData` n'a que les actives).
4. **Choice items reward** — 6 choix proposés au client → choix → check
   serveur. Pattern simple.
5. **Chaînes prev/next** — graphe orienté résolu au load.

## 5. Effort estimé

**L** (1 sprint) :
- Migration DB (3-4 tables)
- `QuestTemplate` + `QuestStatusData` + `QuestObjective` variant
- `QuestMgr` Load + tests
- Opcodes (Accept/Complete/Abandon/QueryStatus)
- Intégration `kill credit` (CMANGOS.07/.11) + `collect item`
  (inventaire) + `interact gameobject`
- Choice items reward (validation serveur)
- Chaînes prev/next

Wire-breaking probable (nouveaux opcodes quêtes).

## 6. Valeur joueur/serveur

**Critique** — déblocant pour le contenu narratif PvE. MMO sans quêtes
= pas de progression dirigée.

## 7. Dépendances bloquantes

Le ticket dépend explicitement de :
- **CMANGOS.13 Database** — SQLStorage cache
- **CMANGOS.16 Globals/Conditions** — `requiredCondition` filtre
- **CMANGOS.14 DBScripts** — hooks `quest_start_scripts`, `quest_end_scripts`
- **CMANGOS.17 Loot** — kill credit interagit avec drop
- **CMANGOS.18 Mails** — récompenses livrées par mail si offline

→ **CMANGOS.23 dépend de 5 tickets P2 amont**. Pas le premier à attaquer
dans la chaîne P2.

## 8. Risque / piège ⚠️

- ⚠️ **Migration DB** — 3-4 nouvelles tables. Idempotent.
- ⚠️ **Wire-breaking** — opcodes quêtes (Accept/Complete/Abandon/Query).
  Bump `kProtocolVersion` + lock-step.
- ⚠️ **Doublon avec M15.1** — risque réimplémentation. Audit obligatoire.
- ⚠️ **JSON vs SQL** — décision archi à acter. Mixer les 2 = dette.
- ⚠️ **`QuestStatusData` volumétrie** — par joueur × quêtes actives.
  100 joueurs online × 25 quêtes actives = 2500 rows synchro. Pattern
  cache RAM + flush DB.
- ⚠️ **Choice items reward** — UX critique : si client envoie un choix
  invalide, refus serveur clair. Sinon exploit possible.
- ⚠️ **Daily/Weekly reset** — cron hebdo (CMANGOS.08 Arena pattern).
  Lock distribué si multi-master.
- ⚠️ **AutoComplete quêtes** — pas d'interaction NPC, juste finir. Bug
  possible si déclenchement précoce.
- ⚠️ **Chaînes prev/next cycles** — détection au load (DFS).

## 9. Recommandation finale

🔧 **Adapter et faire**, **après** audit M15.1 et la chaîne P2 amont :

1. **Étape 0 (cadrage critique)** : audit M15.1 livraison + décision
   archi JSON vs SQL.
   - Si M15.1 livre JSON-driven et fonctionne → ne pas réinventer.
     Adopter `std::variant` C++20 par-dessus l'archi JSON.
   - Si M15.1 pas livré → adopter approche cmangos SQL.
2. **Étape 1** : `QuestTemplate` + `QuestObjective` variant + tests.
3. **Étape 2** : `QuestStatusData` + persistence per-player + cache.
4. **Étape 3** : opcodes Accept/Complete/Abandon/Query + handlers.
5. **Étape 4** : choice items reward + validation serveur.
6. **Étape 5** : intégration kill credit + collect item + interact GO.
7. **Étape 6** : chaînes prev/next + Daily/Weekly reset.
8. **Étape 7** : intégration mail rewards offline.

À planifier après la chaîne P2 (.13/.16/.14/.17/.18). Effort important,
critique pour contenu.

---

*Audit du 2026-05-08. Mises à jour : —*
