# SP4 — Éditeur monde : authoring de quêtes

**Date** : 2026-07-02
**Statut** : design (à relire)
**Doc parent** : `2026-07-02-quest-system-overview-design.md` (décision 5)
**Dépend de** : SP1 (schéma `quest_definitions.json` + `giver`/`turnIn`), SP2 (fichiers client `quest_texts`/`quest_givers`) — tous mergés dans `main`.
**Portée** : un panneau de `lcdlln_world_editor.exe` pour **créer/éditer des quêtes** (authoring hors-ligne, écrit le JSON de contenu). Aucun changement runtime jeu/serveur.

> **Déploiement** : ✅ outillage éditeur (binaire client `world_editor_app`), **pas de redéploiement jeu**. En revanche, toute quête créée/éditée = **contenu** → le shard la charge à l'`Init` de `QuestRuntime`/`SpawnerRuntime` → **restart shard** pour la prendre en compte.

---

## 1. Contexte & ancrages (cartographie 2026-07-02)

L'éditeur monde a une infra de panneaux stable :
- **`IPanel`** (`src/world_editor/core/IPanel.h`) : `GetName()`, `Render()`, `IsVisible()`/`SetVisible()`. Appelé **main thread**, phase ImGui.
- **Patron `BuildingEditorPanel`** (`src/world_editor/panels/BuildingEditorPanel.{h,cpp}`) : formulaire ImGui + injections par setters + bouton **Enregistrer** → `BuildingTemplateLibrary::SaveVariant(m_contentRoot, …)`.
- **Enregistrement** : `WorldEditorShell::Init` (`src/world_editor/core/WorldEditorShell.cpp:128-136`) instancie les panneaux **en fin de liste** (pour ne pas décaler les index fixes) + injecte `SetContentRoot(cfg.GetString("paths.content","game/data"))`. Le rendu est **automatique** via la boucle `RenderFrame` (`:431` : `for(panel) if(IsVisible()) panel->Render();`).
- **Sérialisation** : `BuildingTemplateLibrary::SaveVariant` (`src/client/world/instances/BuildingTemplateLibrary.cpp:153-259`) écrit à la main via `std::ostringstream` (+ helpers `JsonEscape`/`Num`) puis `std::ofstream`. **MAIS** au format Config (`count`-indexé). `quest_definitions.json` est du **pur JSON** (tableaux `[]`) → format différent.
- **Contraintes éditeur (CLAUDE.md)** : **toute fonction** de `src/world_editor/` doit être documentée `///` (rôle, params non-évidents, effets de bord, thread/timing) ; police **Arial** ; commentaires **FR** ; **PascalCase** pour le nouveau code.

Les 3 fichiers de contenu quête existent déjà dans `game/data/quests/` :
- `quest_definitions.json` (shard) : `[{ id, giver, turnIn, prereqs[], steps[{type,target,requiredCount}], rewards{xp,gold,items[{itemId,quantity}]} }]`.
- `quest_texts.fr.json` (client) : `{ "<questId>": { title, description, steps[] } }`.
- `quest_givers.json` (client) : `{ "<npcTargetId>": [{ questId, role }] }` (role 0=giver,1=turnIn).

---

## 2. Architecture SP4

```
QuestEditorPanel  (src/world_editor/panels/, IPanel)   ← formulaire ImGui
        │  injections : SetContentRoot(), SetIo(QuestEditIo*)
        │  Render() : charger/éditer/valider/enregistrer
        ▼
QuestEditIo  (src/world_editor/quests/)                ← I/O des 3 fichiers
        ├─ Load(contentRoot, out<vector<EditedQuest>>, outError)
        │     lit quest_definitions.json (JsonParser inline, comme QuestRuntime)
        │     + fusionne quest_texts.fr.json (title/desc/stepLabels)
        ├─ Validate(quests, outErrors)                 ← pur, testable
        └─ Save(contentRoot, quests, outError)
              écrit quest_definitions.json (pur JSON hand-sérialisé)
              + quest_texts.fr.json
              + quest_givers.json (RÉGÉNÉRÉ depuis giver/turnIn)

Enregistrement : WorldEditorShell::Init  (après BuildingEditorPanel, ~:136)
Rendu          : WorldEditorShell::RenderFrame  (:431, automatique)
```

- **`QuestEditorPanel`** : le panneau (UI). Pas d'aperçu 3D (les quêtes sont des données).
- **`QuestEditIo`** : la logique I/O + validation (séparée du rendu → **testable** sans ImGui).
- **`EditedQuest`** (struct éditeur) : `id, giver, turnIn, prereqs[], steps[{type,target,requiredCount}], rewards{xp,gold,items[]}, title, description, stepLabels[]` — **mécanique + texte réunis** pour l'édition.

---

## 3. UX du panneau (`Render`)

1. **Charger** : combo de la liste des `id` de quêtes chargées (au premier `Render`, `QuestEditIo::Load`) → remplit le formulaire. Bouton **Nouvelle quête** (formulaire vide).
2. **Métadonnées** : champs texte `id`, `giver`, `turnIn` ; multi-sélection `prereqs` parmi les ids existants.
3. **Étapes** : liste de lignes typées (combo `type` = kill/collect/talk/enter, champ `target`, `requiredCount`), boutons **+ étape** / **× supprimer**.
4. **Récompenses** : `xp`, `gold`, liste d'items `{itemId, quantity}` (ajout/suppression).
5. **Texte** : `title`, `description`, et un libellé par étape (`stepLabels[i]`, ex. « Sangliers tués : {current}/{required} »).
6. **Enregistrer** : bouton → `Validate` puis `QuestEditIo::Save`. Message de statut (succès / liste d'erreurs) affiché en bas (comme `BuildingEditorPanel.m_status`).
7. **Annuler/Rétablir** local (optionnel V1) : pile de snapshots du formulaire, comme `BuildingEditorPanel`.

---

## 4. Validation (`QuestEditIo::Validate`, pur/testable)

- `id` non vide et **unique** dans l'ensemble.
- `giver` et `turnIn` **non vides** (acquisition/turn-in explicites, cf. SP1).
- au moins **une étape** ; chaque `target` non vide ; `requiredCount` ≥ 1 ; `type` ∈ {kill,collect,talk,enter}.
- `prereqs` : chaque id **existe** dans l'ensemble (pas de dangling).
- **Détection de cycle** dans le graphe `prereqs` (DFS) — refuse un cycle.
- items reward : `itemId` > 0, `quantity` ≥ 1.

`Validate` renvoie une liste d'erreurs lisibles (affichées dans le panneau) ; `Save` est refusé si non vide.

---

## 5. I/O (`QuestEditIo::Load` / `Save`)

- **Load** : `FileSystem::ReadAllTextContent(cfg-less path)` sur `<root>/quests/quest_definitions.json` + `JsonParser` **inline** (le même que `QuestRuntime::LoadDefinitions` — recopié, cf. dette parser JSON repo-wide). Fusionne `quest_texts.fr.json` par `questId` (title/desc/stepLabels) ; les quêtes sans entrée texte prennent des champs vides.
- **Save** (écrit les **3** fichiers, pur JSON pretty, via `std::ostringstream` + `std::ofstream`, patron `SaveVariant`) :
  - `quest_definitions.json` : tableau `quests[]` (mécanique). **Format pur JSON** (pas le format Config `count`-indexé) — pour rester lisible par `QuestRuntime` côté shard **sans changement serveur**.
  - `quest_texts.fr.json` : map `questId → {title, description, steps[]}`.
  - `quest_givers.json` : **régénéré** depuis les `giver`/`turnIn` de toutes les quêtes (chaque `giver` → `{questId, role:0}`, chaque `turnIn` → `{questId, role:1}`), groupé par `npcTargetId`. Garantit la cohérence des 3 fichiers.
- Helpers `JsonEscape`/`Num` réutilisés/recopiés du patron `BuildingTemplateLibrary`.

---

## 6. Décisions à confirmer (relecture)

1. **L'éditeur écrit les 3 fichiers** (`quest_definitions` + `quest_texts.fr` + `quest_givers` régénéré) — reco, garde le contenu cohérent de bout en bout. *Alternative : `quest_definitions` seul (texte/givers hand-maintenus).* → **reco : les 3.**
2. **Placement du PNJ donneur** (interactable + `npc_target_id` dans `zone.json`) = **hors périmètre SP4** (l'éditeur édite la *donnée quête* ; le placement PNJ reste l'édition d'interactables/zone existante). OK ?
3. **Langue des textes** : V1 = **`fr`** uniquement (fichier `quest_texts.fr.json`) ; les autres langues restent hand-maintenues/copiées. OK ?
4. **Emplacement code** : `src/world_editor/panels/QuestEditorPanel.{h,cpp}` + `src/world_editor/quests/QuestEditIo.{h,cpp}`. OK ?
5. **Autocomplete des références** (giver/turnIn/target/itemId depuis des catalogues) — **hors V1** (champs texte libres + validation de forme). Un durcissement « la cible existe-t-elle » viendra plus tard. OK ?

---

## 7. Tests
- `QuestEditIoTests` (non-strippable, cf. contrainte CI Release/NDEBUG) :
  - **Round-trip** : charger un `quest_definitions.json` de fixture → sérialiser → re-parser → égalité structurelle.
  - **Validation** : id dupliqué rejeté ; prereq dangling rejeté ; **cycle** (`A→B→A`) rejeté ; giver/turnIn vide rejeté ; étape sans target rejetée.
  - **Régénération `quest_givers`** : giver/turnIn → entrées {role 0/1} correctes, groupées par npc.
- Le panneau ImGui (`QuestEditorPanel::Render`) = **intégration** (validé dans l'éditeur, pas de test unitaire) — comme `BuildingEditorPanel`.

## 8. Contraintes (rappel)
- **CLAUDE.md** : toute fonction nouvelle de `src/world_editor/` documentée `///` (rôle, params, effets de bord disque, thread=main/ImGui). Police Arial (héritée du shell). Commentaires **FR**. **PascalCase**.
- Pas de build local → CI (`build-linux` compile `world_editor_app`). Tests non-strippables.
- Format `quest_definitions.json` **inchangé** (pur JSON) : ne PAS le basculer au format Config `count`-indexé (casserait `QuestRuntime` côté shard).

## 9. Hors périmètre
- Placement/édition des PNJ dans le monde (interactables/zone). Autocomplete/validation d'existence des références. Édition multi-langue. Le POI minimap (SP3), le Cleanup système A.

## 10. Definition of Done
- [ ] `QuestEditIo` (Load/Validate/Save 3 fichiers) + tests verts.
- [ ] `QuestEditorPanel` (IPanel) : formulaire charger/éditer/valider/enregistrer, statut.
- [ ] Enregistré dans `WorldEditorShell::Init` (après BuildingEditor) + rendu auto.
- [ ] `quest_definitions.json` reste du pur JSON relisible par le shard (round-trip testé).
- [ ] `quest_givers.json` régénéré cohérent ; `quest_texts.fr.json` écrit.
- [ ] Toutes les fonctions éditeur documentées `///` (FR). PascalCase. Pas de « CMANGOS ».
- [ ] CI verte (build-linux world_editor_app + ctest). Rapport final : ✅ outillage éditeur, restart shard pour charger le contenu créé.
