# SP2 — Client : rendu & interaction des quêtes

**Date** : 2026-07-02
**Statut** : design (à relire)
**Doc parent** : `2026-07-02-quest-system-overview-design.md`
**Dépend de** : SP1 (wire 92/93/94, états `Offered`/`ReadyToTurnIn`) — branche stackée.
**Portée** : 100 % client. Affiche le système B (shard) : journal, tracker HUD,
interaction PNJ (dialogue **et** panneau donneur), marqueur world-space au-dessus
des PNJ donneurs, textes lisibles.

> **Déploiement** : ✅ client uniquement, **aucun nouveau wire** (SP1 a déjà ajouté
> opcodes 92/93/94 + bump v14). MAIS à livrer **en lock-step avec le déploiement
> shard SP1** : sans shard SP1 déployé, le client neuf n'obtient ni giver-list ni
> transitions accept/turn-in. Contenu client ajouté (`quest_texts`, table donneurs).

---

## 1. État client actuel (audit)

Deux chemins quêtes coexistent côté client (miroir des deux systèmes serveur) :

- **Système A (master)** : `QuestUiPresenter` (`src/client/quest/QuestUi.h`) possède
  un cache `m_questStates` (statut seul) et des méthodes `AcceptQuest/CompleteQuest/
  RewardQuest` qui envoient les **opcodes 59/61/63 au master** via
  `AuthUi::SendGenericRequestAsync`. **Redondant** — à ne plus utiliser (retrait = Cleanup).
- **Système B (shard)** : `UIModel.quests` (`UIQuestEntry`/`UIQuestStep`) est peuplé
  par `UIModelBinding::ApplyQuestDelta` (`UIModel.cpp:1266`, décode `QuestDeltaMessage`,
  `MessageKind::QuestDelta`). `QuestUiPresenter::ApplyModel(const UIModel&)` lit déjà
  `model.quests` pour construire `QuestUiState` (journal/tracker). **C'est la bonne source.**

**Trous** confirmés :
- **Aucun renderer ImGui** de quêtes (`QuestImGuiRenderer` absent) → journal/tracker jamais dessinés.
- **Aucune réception** de `QuestGiverList` (92) ni **envoi** de `QuestAcceptRequest`/
  `QuestTurnInRequest` (93/94) côté client.
- **Aucun texte lisible** : `UIQuestEntry` n'a ni titre ni description ; les libellés
  d'étape sont synthétisés (`BuildQuestStepLabel` = « Kill mob:100 »).
- **Callback dialogue non branché** vers l'accept/turn-in shard.
- **Aucun marqueur** au-dessus des PNJ donneurs.

### Ancrages réutilisables (fichier:ligne)

| Ancre | Fichier | Rôle |
|---|---|---|
| `QuestUiPresenter` + `QuestUiState` | `src/client/quest/QuestUi.h:82` | Presenter journal/tracker (lit `UIModel.quests`) |
| `UIModelBinding::ApplyQuestDelta` + dispatch `MessageKind` | `src/client/ui_common/UIModel.cpp:570,1266` | Réception shard→UIModel |
| `GameplayUdpClient::SendTalkRequest` | `src/client/net/GameplayUdpClient.cpp:251` | Patron d'envoi shard (`Encode*`+`SendBytes`) |
| `DialogueImGuiRenderer::Render` | `src/client/render/DialogueImGuiRenderer.h` | Dialogue déjà rendu (`Engine.cpp:10556`) |
| `DialoguePresenter::SetQuestActionCallback` | `src/client/dialogue/DialoguePresenter.h:88` | Seam accept/complete (action, questId int) |
| Renderer in-game (patron) | `src/client/render/ChatImGuiRenderer.h` | `BindXxx()` + `Render()` appelé chaque frame (`Engine.cpp:10538`) |
| Overlay world-space interactables | `src/client/app/Engine.cpp:10580` | Label flottant via `WorldToScreenPx` (`:486`), culling par état/portée |
| `InteractableEntity{position,label,role,...}` | `src/client/app/Engine.h:854` | PNJ (position monde + label) chargé de `config.json world.interactables` |
| `LocalizationService::Translate` | `src/client/localization/LocalizationService.h:14` | Résolution de textes localisés |

---

## 2. Architecture SP2

```
Shard (SP1)                         Client (SP2)
───────────                         ────────────
QuestDelta (13) ───────────────►  UIModelBinding::ApplyQuestDelta ─► UIModel.quests
QuestGiverList (92) ───────────►  UIModelBinding::ApplyQuestGiverList ─► UIModel.giverList   [NOUVEAU]
                                          │
                                          ▼
                                  QuestUiPresenter::ApplyModel(UIModel)
                                          │  (+ quest_texts pour les libellés)
                                          ▼
                                  QuestImGuiRenderer::Render()   [NOUVEAU]
                                    ├─ Journal (liste + détail)
                                    ├─ Tracker HUD (quêtes actives)
                                    └─ Panneau donneur (giver-list) : Accepter / Terminer

Dialogue PNJ (existant) ──accept_quest/complete_quest(questKey)──┐
Panneau donneur (bouton) ──────────────────────────────────────┤
                                                                ▼
                                  GameplayUdpClient::SendQuestAcceptRequest / SendQuestTurnInRequest  [NOUVEAU]
                                    (EncodeQuestAcceptRequest/TurnIn — déjà fournis par SP1) ──► Shard (93/94)

Marqueur donneur (world-space) : interactables + UIModel.giverList/quests + table giver/turnIn client
                                  → rune 2 variantes, culling distance (réutilise l'overlay Engine.cpp:10580)
```

---

## 3. Composants / livrables

### 3.1 Réception `QuestGiverList` (92)
- `UIModelBinding` : ajouter `case MessageKind::QuestGiverList: return ApplyQuestGiverList(packet);`
  (`UIModel.cpp:~570`) + méthode `ApplyQuestGiverList` (décode via `DecodeQuestGiverList`
  fourni par SP1).
- Stocker dans `UIModel` un champ `giverList` : `{ npcTargetId, entries: [{questId, role}] }`
  (dernière giver-list reçue au Talk). Consommé par le panneau donneur.

### 3.2 Envoi accept/turn-in (93/94)
- `GameplayUdpClient` : `bool SendQuestAcceptRequest(uint32 clientId, const std::string& questId, const std::string& giverTargetId)`
  et `SendQuestTurnInRequest(...)`, calqués sur `SendTalkRequest` (build message → `EncodeQuestAcceptRequest`/`EncodeQuestTurnInRequest` → `SendBytes`).

### 3.3 Renderer ImGui — `QuestImGuiRenderer`
- Nouveau `src/client/render/QuestImGuiRenderer.{h,cpp}`, patron `ChatImGuiRenderer` :
  `BindQuestUi(QuestUiPresenter*, const LocalizationService*, const QuestTextCatalog*, const Config*)`
  + `Render(viewportW, viewportH, bool inWorldShard)`.
- Instancié + bindé dans `Engine` ; appelé dans la boucle ImGui in-game (près de
  `Engine.cpp:10556`). Appeler enfin `QuestUiPresenter::Init(cfg)` (jamais fait aujourd'hui).
- **Journal** : liste des quêtes `Active` + `ReadyToTurnIn` (jamais `Offered`) ; panneau
  détail (titre, description, étapes `X/Y`, récompenses). Ouvre/ferme via touche (config).
- **Tracker HUD** : encart compact permanent des quêtes actives (étapes + compteurs),
  masquable, position depuis `QuestUiState.trackerBounds`.
- **Panneau donneur** : quand `UIModel.giverList` non vide (après Talk), fenêtre listant
  les quêtes `offer` (bouton **Accepter** → `SendQuestAcceptRequest`) et `turnin`
  (bouton **Terminer** → `SendQuestTurnInRequest`).

### 3.4 Textes lisibles — `quest_texts.<lang>.json` (décision 6 overview)
- Nouveau contenu client `game/data/quests/quest_texts.<lang>.json`, clé par `questId` :
  ```json
  { "kill_10_boars": {
      "title": "La menace des sangliers",
      "description": "Les sangliers du nord terrorisent le village…",
      "steps": ["Sangliers tués : {current}/{required}"] } }
  ```
- Nouveau `QuestTextCatalog` (client) : charge le fichier de la locale active (via
  `LocalizationService`/`paths.content`), expose `Title/Description/StepLabel(questId, i, cur, req)`.
- Le renderer/presenter utilisent le catalogue **au lieu** de `BuildQuestStepLabel`
  (fallback synthétique si une clé manque, pour ne jamais afficher vide).

### 3.5 Câblage dialogue (interaction « les deux »)
- **Impédance `questId` int ↔ string** : ajouter un champ optionnel **`questKey`** (string)
  à `DialogueChoice` + au JSON de dialogue (`accept_quest`/`complete_quest`). Quand présent,
  c'est le `questId` système B ; le `questId` int historique reste pour compat.
- Dans `Engine`, brancher `DialoguePresenter::SetQuestActionCallback` :
  `AcceptQuest` → `GameplayUdpClient::SendQuestAcceptRequest(clientId, questKey, npcTargetId)` ;
  `CompleteQuest` → `SendQuestTurnInRequest(...)`. Le `npcTargetId` = cible du PNJ courant.

### 3.6 Marqueur world-space donneur (décision 9 overview)
- **Table donneurs client** : `npcTargetId → { questId, role(giver|turnIn) }` issue du
  **contenu client** (fichier `quest_givers.json`, ou dérivée de `quest_texts`/config) —
  **sans wire**. Croisée avec `UIModel.quests` (états) : afficher un marqueur sur un PNJ si
  une de ses quêtes est `Offered` (giver → variante « proposer ») ou `ReadyToTurnIn`
  (turnIn → variante « rendre »).
- **Rendu** : réutilise l'overlay `Engine.cpp:10580` (`WorldToScreenPx` + foreground draw).
  Symbole = **rune du jeu**, **deux variantes** (doré plein `Offered` / variante distincte
  `ReadyToTurnIn`). **Culling par distance** : visible seulement si dist(joueur, PNJ) ≤
  `client.quest.giver_marker_distance_m` (config, défaut à fixer, ex. 35 m), et masqué
  pendant dialogue/menu comme l'overlay existant.

---

## 4. Décisions à confirmer (relecture)

1. **Rendu de la rune** : l'overview a retenu « asset dédié ». La police Windlass étant
   ASCII-only (atlas), deux options : **(a)** petite **texture rune** (PNG) chargée +
   dessinée en billboard/`ImGui::Image` foreground — fidèle à « asset dédié » ; **(b)**
   **rune procédurale** dessinée à l'`ImDrawList` (polygones/anneaux, 2 teintes) — zéro
   asset, plus simple, atlas-safe. **Ma reco V1 : (b) procédurale** (rapide, cohérente
   avec l'overlay existant), asset texture en polish ultérieur. À trancher.
2. **`questKey`** : ajout d'un champ string aux choix de dialogue (plutôt qu'une table de
   mapping int→string). OK ?
3. **Périmètre** : minimap **POI** reste en **SP3** (SP2 = journal + tracker + dialogue +
   panneau donneur + marqueur). La minimap n'est pas rendue par SP2. OK ?
4. **Contenu table donneurs** : fichier client dédié `quest_givers.json` vs champs
   `giver`/`turnIn` recopiés dans `quest_texts.<lang>.json`. Reco : **fichier dédié
   `quest_givers.json`** (non localisé, une seule source). OK ?

---

## 5. Tests
- `QuestTextCatalogTests` : parse + résolution + fallback clé manquante + interpolation `{current}/{required}`.
- `QuestGiverTableTests` : parse `quest_givers.json` + résolution `npcTargetId→quêtes`.
- `UIModelBinding` : round-trip `ApplyQuestGiverList` (décode + peuple `UIModel.giverList`).
- Presenter : `ApplyModel` produit un journal excluant `Offered`, un tracker des `Active`,
  et un détail avec libellés issus du catalogue.
- (Rendu ImGui + marqueur = validés en jeu, non testables unitairement — smoke test SP5.)

## 6. Hors périmètre SP2
- POI minimap (SP3). Retrait complet du système A client (Cleanup). Choix de récompense au turn-in.
- Le PNJ + la quête « 10 sangliers » = **SP5** (contenu), qui valide SP2 en jeu.

## 7. Definition of Done
- [ ] Réception `QuestGiverList` (dispatch + `ApplyQuestGiverList` + `UIModel.giverList`).
- [ ] Envoi `SendQuestAcceptRequest`/`SendQuestTurnInRequest` (GameplayUdpClient).
- [ ] `QuestImGuiRenderer` (journal + tracker + panneau donneur) instancié, bindé, rendu ; `QuestUiPresenter::Init` appelé.
- [ ] `quest_texts.<lang>.json` + `QuestTextCatalog` ; libellés lisibles (plus de « Kill mob:100 »).
- [ ] `questKey` sur les choix de dialogue + callback branché → envoi shard 93/94.
- [ ] Marqueur donneur world-space (rune, 2 variantes, culling distance, masqué en dialogue).
- [ ] Tests §5 verts (CI).
- [ ] Client n'utilise plus les envois système A (59/61/63) pour accept/complete/reward.
- [ ] Commentaires FR ; rapport final : ✅ client only, lock-step déploiement shard SP1.
