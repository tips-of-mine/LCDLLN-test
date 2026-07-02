# SP1 — Shard : cycle de vie des quêtes piloté par le joueur

**Date** : 2026-07-02
**Statut** : validé (brainstorming), prêt pour plan d'implémentation
**Doc parent** : `2026-07-02-quest-system-overview-design.md`
**Portée** : premier sous-projet. Transforme le système B (shard) « tout
automatique » en un cycle « bornes pilotées par le joueur » (accept + turn-in).

> ⚠️ **Déploiement** : wire-breaking (nouveaux opcodes + bump `kProtocolVersion`)
> → **REDÉPLOIEMENT SHARD requis**. Aucun changement master. Lock-step avec SP2
> côté client (le client neuf ne doit pas parler à un shard ancien).

---

## 1. Objectif

Aujourd'hui (`src/shardd/gameplay/quest/QuestRuntime.cpp`) :
- `SyncQuestStates` : pré-requis remplis → `Active` (auto). (`:610-618`)
- `ApplyEvent` : étapes remplies → `Completed` + récompenses versées auto.
  (`:733-746`, versement dans `ServerApp.cpp:5226-5240`)

Cible : introduire deux états intermédiaires (`Offered`, `ReadyToTurnIn`) et deux
gestes joueur (Accept au giver, TurnIn au turn-in NPC), avec versement des
récompenses **au turn-in uniquement**.

---

## 2. Machine d'états

Enum `QuestStatus` (actuellement `Locked=0 / Active=1 / Completed=2`) étendu et
**réordonné** :

```cpp
enum class QuestStatus : uint8_t
{
    Locked        = 0,   // pré-requis non remplis (interne, non affiché)
    Offered       = 1,   // proposée au PNJ giver (non encore acceptée)
    Active        = 2,   // acceptée, en cours
    ReadyToTurnIn = 3,   // étapes remplies, à rendre (pas encore récompensée)
    Completed     = 4,   // rendue + récompensée (terminal)
};
```

Transitions :

| De | Vers | Déclencheur | Où |
|----|------|-------------|-----|
| Locked | Offered | pré-requis tous `Completed` | `SyncQuestStates` |
| Offered | Active | `AcceptQuest` (valide) | nouveau handler |
| Active | ReadyToTurnIn | toutes les étapes remplies | `ApplyEvent` |
| Active | Active | progression partielle | `ApplyEvent` |
| ReadyToTurnIn | Completed | `TurnInQuest` (valide) → **récompenses** | nouveau handler |

> **Compat persistance** : l'ancien format sérialisait `0/1/2` =
> `Locked/Active/Completed`. Au **load**, mapper les anciennes valeurs :
> `0→Locked`, `1→Active`, `2→Completed` (les persos existants n'ont ni `Offered`
> ni `ReadyToTurnIn`). Comme l'ancien `Completed` valait `2` et le nouveau vaut
> `4`, la relecture DOIT passer par une **table de compat** au chargement
> (`CharacterPersistence`), pas par un cast brut. Voir §6.

---

## 3. Schéma de données / JSON

`QuestDefinition` (struct C++) + `game/data/quests/quest_definitions.json` gagnent
deux champs :

```jsonc
{
  "id": "hunt_collect_enter",
  "giver":  "npc:elder_marn",     // NOUVEAU — PNJ qui propose (targetId Talk)
  "turnIn": "npc:elder_marn",     // NOUVEAU — PNJ où rendre (souvent = giver)
  "prereqs": [],
  "steps": [ { "type": "kill", "target": "mob:100", "requiredCount": 1 } ],
  "rewards": { "xp": 100, "gold": 25, "items": [ { "itemId": 3001, "quantity": 1 } ] }
}
```

- `giver` / `turnIn` : chaînes dans **le même espace de nommage que le `targetId`
  du `Talk`** (ex. `npc:<slug>`). Obligatoires (acquisition explicite).
- Rétrocompat de chargement : une quête sans `giver`/`turnIn` est **rejetée au load**
  avec un log d'erreur clair (le contenu doit être migré). Le JSON exemple existant
  (`hunt_collect_enter`, `report_to_guard`) sera mis à jour avec des giver/turnIn.
- Champs `steps`/`prereqs`/`rewards` : inchangés.

---

## 4. Réseau (wire) & flux gameplay

### 4.1 Nouveaux opcodes (shard ↔ client)

| Opcode | Sens | Payload | Rôle |
|--------|------|---------|------|
| `QuestGiverList` | shard→client | `{ npcTargetId, entries[]: {questId, role: offer\|turnin} }` | Réponse au Talk : quêtes offertes/rendables de ce PNJ |
| `AcceptQuest` | client→shard | `{ questId, giverEntityId }` | Le joueur accepte |
| `TurnInQuest` | client→shard | `{ questId, npcEntityId }` | Le joueur rend |

- Le **delta existant** (`QuestDelta` / bootstrap) est étendu pour transporter le
  nouvel enum `status` (5 valeurs). Le format de `stepProgressCounts` ne change pas.
- Bump `kProtocolVersion` (gameplay UDP).

### 4.2 Flux au `Talk` (`ServerApp::HandleTalkRequest`, `:3421`)

Comportement conservé **et enrichi** :
1. Continuer à tirer l'event d'étape `Talk` (`ApplyQuestEvent(Talk, targetId, 1)`) —
   une étape « parler à X » reste possible.
2. **En plus** : calculer pour ce joueur les quêtes dont `giver == targetId` et
   `status == Offered`, et celles dont `turnIn == targetId` et
   `status == ReadyToTurnIn`. Si non vide → envoyer `QuestGiverList`.

### 4.3 Handler `AcceptQuest`

Validations (anti-exploit, toutes avant mutation) :
1. la quête existe (définition chargée) ;
2. `status == Offered` pour ce joueur ;
3. `giverEntityId` résout un PNJ dont `giver == ` l'id de cette quête ;
4. joueur **à portée** du PNJ (réutiliser le contrôle de portée du Talk / interaction).

Si OK → `Offered → Active`, réinitialiser `stepProgressCounts` à 0, émettre un delta,
`SaveConnectedClient`.

### 4.4 Handler `TurnInQuest`

Validations symétriques :
1. la quête existe ; 2. `status == ReadyToTurnIn` ; 3. `npcEntityId` = `turnIn` de la
quête ; 4. joueur à portée.

Si OK → **verser les récompenses** (déplacer ici le bloc XP/or/items de
`ServerApp.cpp:5226-5240`) → `ReadyToTurnIn → Completed`, émettre delta,
`SaveConnectedClient`.

> Le versement de récompense est **retiré** de `ApplyEvent` (il n'y crée plus le
> `Completed` ni les `reward*` du delta). `ApplyEvent` s'arrête à `ReadyToTurnIn`.

---

## 5. Refonte de `QuestRuntime`

- `SyncQuestStates` : `desiredStatus` passe de `Active` à **`Offered`** quand les
  pré-requis sont remplis. Une quête déjà `Active`/`ReadyToTurnIn`/`Completed` n'est
  pas rétrogradée.
- `ApplyEvent` : ne progresse que les quêtes `Active` (déjà le cas, `:691`). À
  `AreAllStepsComplete` → `Active → ReadyToTurnIn` **sans** remplir `reward*` dans le
  delta.
- Nouvelles méthodes pures/testables :
  - `bool CanAccept(state, def, giverTargetId) const`
  - `bool CanTurnIn(state, def, npcTargetId) const`
  - `QuestReward TakeRewardOnTurnIn(def) const` (retourne le bundle à verser).
- Chargement : rejet des définitions sans `giver`/`turnIn` ; parse des 2 champs.

Toute fonction ajoutée/modifiée est documentée (`///`, FR) — convention repo.

---

## 6. Persistance

- `CharacterPersistence` : format clé-valeur inchangé (`quests.N.id`,
  `quests.N.status`, `quests.N.step_count`, `quests.N.step.M.progress`).
- **Pas de migration DB** (blob clé-valeur), mais **map de compat au load** :
  - Nouveau save : écrit l'enum étendu (0..4).
  - Ancien load : `0→Locked, 1→Active, 2→Completed` (mapping explicite, pas de cast).
  - Un `Offered` persisté qui n'aurait plus de définition, ou dont le giver a changé,
    est re-synchronisé par `SyncQuestStates` au login.

---

## 7. Tests

- **Transitions** : `Locked→Offered` (sync), `Offered→Active` (accept OK),
  `Active→ReadyToTurnIn` (étapes finies), `ReadyToTurnIn→Completed` (turn-in OK).
- **Anti-exploit** :
  - accept d'une quête non `Offered` → refus ;
  - accept au mauvais giver → refus ;
  - accept/turn-in hors portée → refus ;
  - turn-in d'une quête `Active` (étapes non finies) → refus ;
  - turn-in au mauvais PNJ → refus.
- **Récompense** : versée **une seule fois**, **au turn-in**, jamais à
  `ReadyToTurnIn`.
- **Persistance** : round-trip avec les 5 états ; relecture de l'**ancien format**
  (0/1/2) correcte.
- **Sync** : quête dont le pré-requis n'est pas `Completed` reste `Locked`
  (n'apparaît pas comme `Offered`).
- **Chargement** : définition sans `giver`/`turnIn` rejetée avec log.

---

## 8. Definition of Done

- [ ] Build Linux OK (shard).
- [ ] Enum `QuestStatus` étendu + réordonné + map de compat au load.
- [ ] `giver`/`turnIn` parsés ; définitions sans ces champs rejetées ; JSON exemple migré.
- [ ] `SyncQuestStates` → `Offered` ; `ApplyEvent` → `ReadyToTurnIn` sans reward.
- [ ] Opcodes `QuestGiverList` / `AcceptQuest` / `TurnInQuest` + bump `kProtocolVersion`.
- [ ] `Talk` renvoie la giver-list (offer + turn-in) en plus de l'event d'étape.
- [ ] Récompenses versées au turn-in uniquement.
- [ ] Persistance : round-trip 5 états + compat ancien format.
- [ ] Tous les tests §7 passent.
- [ ] Fonctions nouvelles/modifiées documentées (FR).
- [ ] Rapport final + mention **REDÉPLOIEMENT SHARD requis**.

---

## 9. Hors périmètre SP1 (rappel)

- Rendu client ImGui (→ SP2).
- Texte lisible `quest_texts.<lang>.json` (→ SP2 / SP4).
- Éditeur d'authoring (→ SP4).
- POI minimap (→ SP3).
- Choix de récompense au turn-in (post-V1).
- Retrait du système A master (→ Cleanup).
