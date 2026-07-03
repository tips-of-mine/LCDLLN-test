# EXT-1 — Quêtes mutuellement exclusives / contre-quêtes

**Date** : 2026-07-03
**Statut** : design (à relire)
**Doc parent** : `2026-07-02-quest-system-overview-design.md`
**Portée** : ajouter un champ `excludes: [questId,…]` aux définitions de quête (système B, shard). S'engager dans une quête rend indisponibles les quêtes qu'elle exclut (et réciproquement). Sert aux choix de faction et aux embranchements narratifs mutuellement exclusifs. **Shard + éditeur uniquement — aucun changement de wire.**

---

## 1. Sémantique

- **« Engagée »** = statut ∈ `{Active, ReadyToTurnIn, Completed}`. Le statut `Offered` **ne bloque pas** : on peut se voir proposer deux quêtes exclusives ; **accepter** l'une verrouille l'autre.
- **Exclusion symétrique au runtime** : si A déclare exclure B, alors B est aussi bloquée quand A est engagée, **même si B ne déclare pas A**. Le GM déclare l'exclusion **une seule fois** (sur A ou B).
- **Purement côté serveur** : une quête exclue reste `Locked`, ne devient jamais `Offered` → elle n'apparaît ni au journal client, ni dans la giver-list (opcode 92, dérivée des quêtes `Offered`). **Le client n'a pas besoin de connaître `excludes`** ; il ne voit que le statut résultant via `QuestDelta`. → **pas de bump `kProtocolVersion`, client inchangé.**

## 2. Deux gates d'enforcement

1. **Offer-sync** (`QuestRuntime::SyncQuestStates`, `QuestRuntime.cpp:~612`) : au calcul de `desiredStatus`, en plus de `prerequisitesComplete`, exiger `!IsBlockedByExclusion(states, definition)` pour passer à `Offered` ; sinon rester `Locked`. → masque automatiquement les quêtes exclues de la giver-list.
2. **Accept** (`ServerApp::HandleAcceptQuest`, `ServerApp.cpp:5356`, juste après `CanAccept`) : re-vérif atomique — `if (m_questRuntime.IsBlockedByExclusion(client->questStates, *def)) { LOG_WARN; return; }`. Ferme la **fenêtre de course** où deux quêtes exclusives sont `Offered` simultanément et où un client tenterait d'accepter la seconde après avoir accepté la première (la sync n'a pas encore rétrogradé la seconde).

## 3. Ajouts

### 3.1 Shard — `src/shardd/gameplay/quest/QuestRuntime.{h,cpp}`
- **`QuestDefinition`** (`QuestRuntime.h:55`, après `prerequisiteQuestIds`) : ajouter `std::vector<std::string> excludedQuestIds;`.
- **`LoadDefinitions`** (`QuestRuntime.cpp:~885`) : parser le champ **optionnel** `"excludes"` (array de strings non vides), **miroir exact** du bloc `prereqs`. Absent = OK (rétro-compatible).
- **Nouvelle méthode** `bool QuestRuntime::IsBlockedByExclusion(const std::vector<QuestState>& states, const QuestDefinition& def) const` :
  - `true` si **(a)** une quête de `def.excludedQuestIds` est engagée, **OU (b)** une autre `QuestDefinition` de `m_definitions` dont `excludedQuestIds` contient `def.questId` est engagée.
  - Helper privé `bool IsQuestEngaged(const std::vector<QuestState>& states, const std::string& questId) const` : `true` si l'état existe et status ∈ `{Active, ReadyToTurnIn, Completed}`.
- **`SyncQuestStates`** : intégrer le gate (cf. §2.1).

### 3.2 Shard — `src/shared/server_bootstrap/ServerApp.cpp`
- **`HandleAcceptQuest`** (5356) : ajouter le gate accept (cf. §2.2), après `CanAccept` et avant le passage à `Active`.

### 3.3 Éditeur — `src/world_editor/quests/QuestEditIo.{h,cpp}`
- **`EditableQuest`** : ajouter `std::vector<std::string> excludes;`.
- **Parse** (`QuestEditIo.cpp:~576`) : lire `"excludes"` (miroir `prereqs`).
- **Validation** : chaque id de `excludes` doit exister parmi les quêtes chargées (comme les prereqs dangling — même politique de message) **et** `id != quest.id` (rejet auto-exclusion). **Pas** de détection de cycle (l'exclusion n'est pas un DAG, aucune contrainte d'acyclicité).
- **Sérialisation** (`QuestEditIo.cpp:~807`) : émettre `"excludes"` (miroir `prereqs`). `quest_definitions.json` reste du **JSON pur relisible par le shard**.

### 3.4 Éditeur — `src/world_editor/panels/QuestEditorPanel.{h,cpp}`
- Ajouter `std::vector<std::string> m_excludesBuffer;` + `void RenderExcludesSection();` (miroir `m_prereqBuffer` / `RenderPrereqSection`, liste de cases à cocher des **autres** quêtes, `id != q.id` masqué ou désactivé).
- Charger/sauver le buffer dans les hooks Load/Save existants (miroir prereqs, `QuestEditorPanel.cpp:45/62/80`).
- **Doc `///`** obligatoire sur toute nouvelle fonction (règle éditeur monde, CLAUDE.md).

## 4. Tests
- **`QuestRuntimeTests.cpp`** : (a) offer-sync garde B `Locked` quand A engagée ; (b) accept de B refusé quand A engagée ; (c) direction **symétrique** (A exclut B non déclaré sur B) ; (d) **pas** de blocage quand la quête concurrente est seulement `Offered` ; (e) rétro-compat : quête sans `excludes` inchangée.
- **`QuestEditIoTests.cpp`** (ou l'équivalent existant) : round-trip `excludes` (parse→serialize→parse) ; rejet auto-exclusion ; rejet id inexistant ; sérialisation relue OK par un `QuestRuntime`.
- Tests **non-strippables** (CI Release `-DNDEBUG` : pas de `assert()` nu ; `std::cerr` + compteur + `return 1`), sauf si le fichier fait déjà `#undef NDEBUG`.

## 5. Compat / contenu
- Champ `excludes` **optionnel** → 100 % rétro-compatible, aucune quête existante impactée, pas de migration de contenu.
- Pas de persistance nouvelle : `excludes` est une **propriété de définition** (statique, JSON), pas un état par joueur. La persistance par personnage (statut + stepProgressCounts) est inchangée.

## 6. Déploiement
> **⚠️ REDÉPLOIEMENT SHARD requis** — `QuestRuntime` charge et évalue `excludes` au boot et à chaque `SyncQuestStates`. **Client inchangé** (aucun wire). Éditeur = outillage (restart éditeur pour charger la nouvelle UI ; restart shard pour charger le contenu créé).

## 7. Hors périmètre
- EXT-2 (répétables/quotidiennes) et EXT-3 (groupe/raid) — sous-projets séparés.
- Exclusion **par tag faction** (« toutes les quêtes faction X ») : ici on exclut quête-à-quête ; un tag faction serait une couche au-dessus, non couverte.

## 8. Definition of Done
- [ ] `QuestDefinition.excludedQuestIds` + parse `"excludes"` + `IsBlockedByExclusion`/`IsQuestEngaged` ; gate offer-sync + gate accept.
- [ ] Éditeur : `EditableQuest.excludes` + parse/validate (existence, anti-auto-exclusion)/serialize + `RenderExcludesSection` + Load/Save.
- [ ] Tests shard (5 cas) + éditeur (round-trip + rejets), non-strippables.
- [ ] CI verte (shard + éditeur + client — client non modifié mais linke `QuestRuntime`). Rapport : ⚠️ REDÉPLOIEMENT SHARD, client inchangé.
