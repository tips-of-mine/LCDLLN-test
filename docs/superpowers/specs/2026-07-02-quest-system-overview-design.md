# Système de quêtes — Design d'ensemble & découpage

**Date** : 2026-07-02
**Statut** : validé (brainstorming), spec de découpage
**Portée** : cycle de vie complet des quêtes (récupération → suivi → réalisation →
terminé) côté client, persistance côté serveur, + authoring GM.

---

## 1. Contexte : l'existant réel (audit code du 2026-07-02)

Contrairement à l'impression initiale (« il manque toute la gestion des quêtes »),
il existe en réalité **deux systèmes de quêtes parallèles et déconnectés**.

### Système A — master (status-machine)

- `src/masterd/quests/QuestState.h` : `QuestStateTracker`, machine d'états par
  **compte** (`None/Available/Accepted/Completed/Rewarded/Failed`), **statut seul**.
- `src/masterd/quests/MysqlQuestStateStore.h/.cpp` : persistance du statut
  (table `account_quest_state`, migration 0048). `Load()` **jamais rappelé au login**.
- `src/masterd/handlers/quest/QuestHandler.cpp` : opcodes 59-67
  (Accept/Complete/Reward/List + push), câblés.
- Wire `src/shared/network/QuestPayloads.h` : `questId` (uint32) + `status` (uint8),
  **aucun texte, aucune progression, aucune récompense**.
- Client `src/client/quest/QuestUi.h/.cpp` : `QuestUiPresenter` (cache statut A).

**Verdict** : pauvre (statut seul), redondant, à moitié mort. → **retiré** (cleanup).

### Système B — shard (data-driven, riche) — le vrai système, qui MARCHE

- `src/shardd/gameplay/quest/QuestRuntime.h/.cpp` : définitions chargées depuis
  `game/data/quests/quest_definitions.json` (clé config `server.quest_definitions_path`).
  Étapes typées (`Kill/Collect/Talk/Enter`), progression par étape
  (`stepProgressCounts`), récompenses (xp/gold/items), sync de pré-requis.
- **Alimenté par le vrai gameplay** : `ApplyQuestEvent` (wrapper de
  `QuestRuntime::ApplyEvent`) appelé sur Kill (`ServerApp.cpp:3111`),
  Collect/pickup (`:3361`), Talk (`:3422`), auto-loot (`:3676`), Enter zone (`:2929`).
- **Complétion + récompenses automatiques** : à étapes remplies → `Completed`, puis
  XP (`ApplyLevelUpsAfterXp`), or (`m_playerWallet.AddCurrency`), items
  (`AddItemToInventory`) — `ServerApp.cpp:5226-5240`.
- **Persistance du suivi DÉJÀ FAITE** : `CharacterPersistence` sérialise
  `questId` + `status` + `stepProgressCounts` par personnage (`:264-274`).
- **Bootstrap client** : `SendQuestStateBootstrap` au login + `SendQuestDelta` →
  `UIModel.quests` (`UIQuestEntry` avec steps + rewards) → `ApplyQuestDelta` client.

**Verdict** : cycle complet fonctionnel côté serveur, données qui atteignent le
client. **La « sauvegarde du suivi » que l'on croyait manquante existe déjà.**

### Le vrai manque

1. **🔴 Rendu client ImGui absent.** La data riche arrive dans `UIModel.quests`,
   `QuestUiPresenter` prépare même `QuestUiState`… mais **aucun renderer ne dessine**.
   Le joueur est aveugle. (Aucun `*QuestRenderer*`.)
2. **🟠 Deux systèmes concurrents** (A pauvre vs B riche) — à rationaliser.
3. **🟡 Pas de « récupération » ni de « turn-in » pilotés par le joueur** — B fait
   tout automatiquement (déblocage par pré-requis, récompense à étapes finies).
4. **🟡 Pas d'authoring GM** — le JSON de contenu s'édite à la main, sans outil.

---

## 2. Décisions de design (validées)

| # | Décision | Choix |
|---|----------|-------|
| 1 | Source de vérité | **Système B (shard, data-driven)**. Système A retiré. |
| 2 | Récupération | **Accept explicite** chez un PNJ (nouvel état `Offered`). |
| 3 | Fin de quête | **Turn-in explicite** chez un PNJ ; récompenses versées au turn-in ; **sans** choix de récompense (V1). |
| 4 | Périmètre UI V1 | Journal + Tracker HUD + Dialogue PNJ accept/turn-in + POI minimap. |
| 5 | Authoring GM | **Panneau dans l'éditeur monde** (hors-ligne, écrit le JSON de contenu). |
| 6 | Texte de quête | Fichier compagnon localisé `quest_texts.<lang>.json` (client-side), **pas** transporté par le wire. |
| 7 | Interaction PNJ donneur | **Les deux** : dialogue existant (`accept_quest`/`complete_quest` → Accept/TurnIn de SP1) **et** panneau donneur dynamique piloté par `QuestGiverList`. |
| 8 | Tranche verticale jouable | Un **PNJ dans la carte actuelle** donne une quête « tuer 10 sangliers » (archétype existant `mob:100`), validée puis rendue au PNJ pour la récompense. Capstone d'acceptation. |
| 9 | Marqueur au-dessus des PNJ donneurs | **Rune/glyphe du jeu** (asset dédié, **≠ « ! »**), **deux variantes** : `Offered` = doré plein (venir chercher) / `ReadyToTurnIn` = variante distincte (venir rendre). **Culling par distance** (config) pour ne pas polluer le HUD. Feature **SP2**. |

### Marqueur PNJ donneur (SP2) — détail

- **Rendu** : petit **billboard texturé world-space** ancré au-dessus du PNJ (pas un
  glyphe de police — la police Windlass est ASCII-only, cf. atlas). Réutiliser le
  précédent des marqueurs de réapparition (overlays ImGui foreground world-space,
  `Engine.cpp`). Deux teintes/variantes selon l'état (doré plein `Offered` /
  variante `ReadyToTurnIn`).
- **Visibilité** : n'apparaît que si le joueur est à **≤ distance seuil** du PNJ
  (clé config `client.quest.giver_marker_distance_m`, défaut à fixer en SP2) ET si
  ce PNJ a, **pour ce joueur**, une quête `Offered` (role offer) ou `ReadyToTurnIn`
  (role turnin).
- **Donnée nécessaire côté client** : la table `npcTargetId → quêtes (giver/turnIn)`.
  Elle vient d'un **fichier de contenu client** (le compagnon `quest_texts` ou une
  petite table `quest_givers`), **sans nouveau wire** — le client croise cette table
  avec les états de quête déjà reçus (`UIModel.quests`) pour décider quel marqueur
  afficher. À figer au design SP2.

### Note d'impédance : `questId` numérique (dialogue) ↔ `string` (système B)

Le dialogue (`game/data/dialogues/*.json`, `DialogueChoice.questId` **int**) et le
système B (`questId` **string**) ne partagent pas le même espace d'identifiants.
SP2 devra introduire une correspondance : soit un champ `questKey` (string) ajouté
aux choix de dialogue, soit une table de mapping `int → string`. À trancher au
design SP2 ; **le champ `questKey` string est la piste recommandée** (explicite,
pas de table parallèle à maintenir).

---

## 3. Nouvelle machine d'états (système B étendu)

```
Locked                 (pré-requis non remplis — interne, non affiché)
  │ prérequis Completed (SyncQuestStates)
  ▼
Offered                (proposée : visible SEULEMENT dans le dialogue du PNJ giver)
  │ AcceptQuest (joueur clique « Accepter » au PNJ)
  ▼
Active                 (en cours : journal + tracker ; events progressent)
  │ toutes les étapes remplies (ApplyEvent)
  ▼
ReadyToTurnIn          (à rendre : PAS de récompense encore ; marqueur au PNJ turn-in)
  │ TurnInQuest (joueur clique « Terminer » au PNJ) → récompenses versées ICI
  ▼
Completed              (terminée, récompensée — persistée définitivement)
```

Changements clés vs l'existant :
- `SyncQuestStates` : prérequis remplis → **`Offered`** (au lieu d'`Active`).
- `ApplyEvent` : étapes remplies → **`ReadyToTurnIn`** (au lieu de `Completed`),
  **sans** verser les récompenses.
- Le **versement des récompenses migre** dans le handler `TurnInQuest`.

### Règles UI validées

- **Journal ≠ Offered** : le journal affiche `Active` + `ReadyToTurnIn` ; les quêtes
  `Offered` n'apparaissent que dans le dialogue du PNJ.
- **`Talk` garde son double rôle** : il tire l'event d'étape `Talk` **et** ouvre la
  giver-list si le PNJ a des quêtes `Offered`/`ReadyToTurnIn` pour ce joueur.
- **`giverId`/`turnInId` = mêmes chaînes que le `targetId` du `Talk`** (pas un
  nouvel espace d'identifiants d'entité).

---

## 4. Découpage en sous-projets

Chaque SP a son propre cycle spec → plan → implémentation.

| SP | Contenu | Déploiement | Dépend de |
|----|---------|-------------|-----------|
| **SP1** | Shard : cycle de vie joueur (Offered/ReadyToTurnIn ; accept/turn-in ; reward au turn-in ; schéma `giver`/`turnIn`) | ⚠️ redéploiement **shard** (wire-breaking) | — |
| **SP2** | Client : renderer ImGui (journal, tracker HUD) + câblage **dialogue** (`accept_quest`/`complete_quest`) **et** panneau donneur (`QuestGiverList`) + `questKey` + lecture `quest_texts` | client, **lock-step SP1** | SP1 (wire) |
| **SP4** | Éditeur monde : panneau d'authoring (mécanique + texte) | outillage client ; contenu → restart shard | SP1 (schéma + `quest_texts`) |
| **SP3** | Client : POI minimap/carte | client (+ contenu localisation objectifs) | SP1, SP2 |
| **SP5** | **Tranche verticale jouable** : PNJ dans la carte + quête « tuer 10 sangliers » (`mob:100` ×10) + texte + branchement dialogue giver/turnIn | contenu ; **restart shard** pour charger la quête | SP1, SP2 |
| **Cleanup** | Retrait système A (master : tracker, handler, opcodes 59-67, migration 0048, cache A du presenter) | petit redéploiement **master** | — |

**Ordre de merge / déploiement** :
`SP1` (shard, déployé en premier) → `SP2` + `SP4` (parallèles, consomment le schéma
figé par SP1 ; SP2 lock-step avec le déploiement shard SP1) → `SP5` (tranche
verticale, dès que SP1+SP2 sont en place) → `SP3` → `Cleanup`.

### SP5 — Tranche verticale jouable (détail)

Capstone d'acceptation, entièrement **contenu + branchement** (pas de nouveau wire) :

- **Créature** : réutiliser l'archétype existant `mob:100` (« Sanglier des collines »,
  `game/data/creatures/archetypes.json`) — déjà tuable, émet l'event `Kill` avec
  `target = "mob:100"`. Vérifier qu'au moins ~10 sangliers **spawnent** dans une zone
  accessible de la carte actuelle (sinon ajouter des points de spawn).
- **PNJ donneur** : ajouter un PNJ interactif dans la carte via
  `config.json world.interactables.<i>` (position + `dialogue_id`) + un fichier
  `game/data/dialogues/<pnj>.json` avec un choix `accept_quest` (au début) et
  `complete_quest` (au retour), tous deux liés à la quête via `questKey`.
- **Définition de quête** (`game/data/quests/quest_definitions.json`) :
  ```jsonc
  { "id": "kill_10_boars", "giver": "npc:<pnj>", "turnIn": "npc:<pnj>",
    "prereqs": [],
    "steps": [ { "type": "kill", "target": "mob:100", "requiredCount": 10 } ],
    "rewards": { "xp": 200, "gold": 50, "items": [] } }
  ```
- **Texte** : entrée `kill_10_boars` dans `quest_texts.<lang>.json` (titre, description,
  libellé d'étape « Sangliers tués : X/10 »).
- **Correspondance** : le `questKey` du dialogue = `"kill_10_boars"` (string système B).
- **Critère d'acceptation** = le smoke test du DoD SP1, mais joué en vrai :
  parler au PNJ → accepter → tuer 10 `mob:100` (tracker 0/10 → 10/10) → retourner au
  PNJ → « Terminer » → +200 XP / +50 or → quête `Completed` ; survit à un restart shard.

> **Identité/emplacement du PNJ** : à préciser au moment de SP5 (nom, position dans
> la carte actuelle — vraisemblablement Feyhin Lokcthat). Choix laissé ouvert ici car
> non bloquant pour SP1.

> ⚠️ **Déploiement (global)** : le chantier introduit du **wire-breaking** en SP1
> (nouveaux opcodes accept/turn-in/giver-list + bump `kProtocolVersion`). Un client
> SP2 neuf parlerait dans le vide à un shard SP1 ancien → **SP1 doit être déployé
> avant/avec SP2**. SP4 (éditeur) ne redéploie pas le jeu, mais toute quête créée
> nécessite un **restart shard** pour être chargée.

---

## 5. Specs détaillées par sous-projet

- **SP1** : `2026-07-02-quest-sp1-player-lifecycle-design.md` (ci-joint).
- SP2 / SP4 / SP3 / SP5 / Cleanup : specs à écrire au fil du chantier, après SP1.
