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
| **SP2** | Client : renderer ImGui (journal, tracker HUD, dialogue PNJ) + lecture `quest_texts` | client, **lock-step SP1** | SP1 (wire) |
| **SP4** | Éditeur monde : panneau d'authoring (mécanique + texte) | outillage client ; contenu → restart shard | SP1 (schéma + `quest_texts`) |
| **SP3** | Client : POI minimap/carte | client (+ contenu localisation objectifs) | SP1, SP2 |
| **Cleanup** | Retrait système A (master : tracker, handler, opcodes 59-67, migration 0048, cache A du presenter) | petit redéploiement **master** | — |

**Ordre de merge / déploiement** :
`SP1` (shard, déployé en premier) → `SP2` + `SP4` (parallèles, consomment le schéma
figé par SP1 ; SP2 lock-step avec le déploiement shard SP1) → `SP3` → `Cleanup`.

> ⚠️ **Déploiement (global)** : le chantier introduit du **wire-breaking** en SP1
> (nouveaux opcodes accept/turn-in/giver-list + bump `kProtocolVersion`). Un client
> SP2 neuf parlerait dans le vide à un shard SP1 ancien → **SP1 doit être déployé
> avant/avec SP2**. SP4 (éditeur) ne redéploie pas le jeu, mais toute quête créée
> nécessite un **restart shard** pour être chargée.

---

## 5. Specs détaillées par sous-projet

- **SP1** : `2026-07-02-quest-sp1-player-lifecycle-design.md` (ci-joint).
- SP2 / SP4 / SP3 / Cleanup : specs à écrire au fil du chantier, après SP1.
