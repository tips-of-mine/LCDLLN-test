# Spec — Cellule de dialogue PNJ dédiée

- **Date** : 2026-06-02
- **Périmètre** : client uniquement (Windows/Vulkan/ImGui). Pas de changement protocole, pas de migration DB.
- **Déploiement** : ✅ client uniquement, pas de redéploiement serveur.
- **Maquette de référence** : `docs/superpowers/mockups/dialogue-pnj-mockup.html` (disposition retenue : **B · fenêtre centrale**).

## 1. Objectif

Lorsqu'un joueur parle à un PNJ, la conversation ne doit **plus** s'afficher dans
le canal chat, mais dans une **cellule de dialogue dédiée** à l'écran. Cette
cellule doit :

- afficher le texte du PNJ avec **auto-scroll fluide et lent** vers le bas, et une
  **barre de scroll verticale à droite** permettant de remonter consulter le texte ;
- effectuer un **retour à la ligne automatique** (word-wrap) sur le texte du PNJ ;
- réserver une zone pour les **réponses du joueur**, de **2 à 5 choix** possibles,
  chaque réponse étant elle aussi en **word-wrap** ;
- **figer le joueur** pendant le dialogue (déplacement bloqué) ;
- **se fermer automatiquement** si le joueur s'éloigne de plus de **1,50 m** du PNJ ;
- **consigner** toute conversation liée à une quête ou un raid dans un **journal
  local par personnage** pour en assurer le suivi.

## 2. Décisions de conception (validées)

| Sujet | Décision |
|-------|----------|
| Source des dialogues | **Client / `config.json` étendu** (statique, pas de protocole serveur). |
| Immobilité | **Verrouillage total** de l'input de déplacement à l'ouverture + fermeture auto si dépassement de 1,50 m (filet de sécurité). |
| Sauvegarde journal | **Local client**, par personnage (fichier JSON). Branchement serveur possible ultérieurement. |
| Défilement | **Auto-scroll fluide** du bloc (texte complet présent, vue qui glisse doucement). |
| Disposition | **B · fenêtre centrale** parchemin, cadre or, police de jeu (Windlass). |
| Portée d'engagement | **Resserrée à 1,50 m** pour les PNJ dialoguants, cohérente avec la distance de rupture. |

## 3. Architecture

### 3.1 Nouveaux composants

- **`DialoguePresenter`** (`src/client/dialogue/DialoguePresenter.{h,cpp}`)
  — logique pure, testable hors rendu. Tient l'état runtime :
  - nœud de dialogue courant, lignes à afficher, choix disponibles ;
  - référence au PNJ cible (label, rôle, position) ;
  - position d'auto-scroll et état pause/reprise ;
  - distance courante joueur↔PNJ et seuil de rupture.
  - API (esquisse) :
    - `OpenDialogue(const DialogueTree&, const NpcRef&)`
    - `SelectChoice(int index)` → applique l'`action` du choix, navigue vers
      `nextNodeId`, ou ferme.
    - `Tick(float dt, float distanceXZ)` → avance l'auto-scroll, déclenche la
      rupture > 1,50 m.
    - `Close(CloseReason)` (`UserClose`, `TooFar`, `EndNode`).
    - Accesseurs lecture seule pour le renderer (`CurrentLines()`, `Choices()`,
      `Npc()`, `IsActive()`, `AutoScrollOffset()`…).

- **`DialogueImGuiRenderer`** (`src/client/render/DialogueImGuiRenderer.{h,cpp}`)
  — rendu ImGui (Windows). Lit l'état du presenter, dessine la fenêtre centrale,
  gère scrollbar / molette / pause auto-scroll, et émet les callbacks de
  sélection de choix et de fermeture. **Non testé en CI** (rendu).

### 3.2 Branchement dans `Engine`

- `Engine.h` : `std::unique_ptr<DialoguePresenter> m_dialogue;`
  `std::unique_ptr<DialogueImGuiRenderer> m_dialogueImGui;` + `bool m_dialogueActive`.
- `Engine::Render()` : appeler `m_dialogueImGui->Render(...)` quand `m_dialogueActive`.
- Bloc interaction (`Engine.cpp` ~8715) :
  - **E** près d'un PNJ dialoguant → `m_dialogue->OpenDialogue(...)`, `m_dialogueActive = true`.
  - **Supprimer** la poussée actuelle du dialogue dans le canal chat
    (`[PNJ] label: text` / `[Interaction] …`) au profit de la cellule dédiée.
- Tick par frame : calcul de la distance XZ (boucle existante ~8722-8728) passé à
  `m_dialogue->Tick(dt, distanceXZ)`.

### 3.3 Verrouillage du déplacement

- Tant que `m_dialogueActive` : l'`input` de déplacement est ignoré dans le mapping
  d'input (la caméra reste libre). Réutiliser l'état de locomotion existant ou un
  garde booléen dédié — sans casser la machine d'états `AvatarLocomotionState`.
- Fermeture par **✕**, **Échap**, ou rupture > 1,50 m → `m_dialogueActive = false`,
  l'input de déplacement reprend.

## 4. Modèle de données

### 4.1 Structures (extension de `InteractableEntity`, Engine.h ~726)

```cpp
struct DialogueLine {
  std::string text;
  bool        isCue = false;   // didascalie affichée en italique
};

enum class DialogueAction { Continue, AcceptQuest, CompleteQuest, End };

struct DialogueChoice {
  std::string    text;             // libellé du choix (word-wrap)
  std::string    nextNodeId;       // nœud suivant si action == Continue
  DialogueAction action = DialogueAction::Continue;
  int            questId = -1;     // renseigné pour Accept/Complete ou lien journal
  std::string    icon;             // optionnel (ex. "⚔️")
};

struct DialogueNode {
  std::string                 id;
  std::vector<DialogueLine>   lines;
  std::vector<DialogueChoice> choices;   // 2 à 5 ; 1 seul (« Au revoir ») toléré
};

struct DialogueTree {
  std::string               startNodeId;
  std::vector<DialogueNode> nodes;
};
```

`InteractableEntity` gagne un `DialogueTree dialogueTree;` optionnel.

### 4.2 Rétro-compatibilité

Si un PNJ n'a que l'ancien champ `dialogue: [...]` (liste de lignes), le client le
convertit en un `DialogueTree` à **un seul nœud** dont les `lines` sont ces lignes
et avec un unique choix « Au revoir » → `End`. Aucune rupture pour les PNJ existants.

### 4.3 Format `config.json`

> **Mise à jour 2026-06-02 (relocalisation)** : sur demande, les **définitions** de
> dialogues ne vivent plus inline dans `config.json` mais dans des **fichiers dédiés,
> un par PNJ/zone**, sous `game/data/dialogues/<id>.json` (racine `{ start, nodes }`,
> lus via `engine::core::Config`, mêmes clés `count`+index). `config.json` ne porte
> plus qu'une **référence** `world.interactables.<i>.dialogue_id`. Le bloc inline
> `dialogue_tree` décrit ci-dessous est conservé pour mémoire mais n'est plus utilisé ;
> `LoadDialogueTree` lit `dialogue_id`, charge le fichier, normalise, et retombe sur
> le champ legacy `dialogue` si l'id est absent ou le fichier introuvable.

Extension de `world.interactables[i]` avec un bloc `dialogue_tree` (le champ
`dialogue` reste lu en fallback) :

```json
"world.interactables.0": {
  "x": 4.0, "z": 0.0, "radius": 1.5, "npc": true,
  "label": "Aldric le Veilleur",
  "role": "Garde du pont",
  "dialogue_tree": {
    "start": "intro",
    "nodes": [
      {
        "id": "intro",
        "lines": [
          { "text": "Halte, voyageur..." },
          { "text": "(Il pose la main sur son épée.)", "cue": true }
        ],
        "choices": [
          { "text": "Parle-moi de ces ruines.", "next": "ruines", "icon": "❓" },
          { "text": "J'accepte ta quête.", "action": "accept_quest", "questId": 1012, "icon": "⚔️" },
          { "text": "Au revoir.", "action": "end", "icon": "👋" }
        ]
      }
    ]
  }
}
```

Mapping `action` (string config → enum) : `"continue"`→`Continue`,
`"accept_quest"`→`AcceptQuest`, `"complete_quest"`→`CompleteQuest`, `"end"`→`End`.
Validation au chargement : 1 à 5 choix par nœud, `start`/`next` doivent référencer
un nœud existant ; sinon log d'erreur + fallback nœud unique.

## 5. Comportement immobilité & distance

- **Engagement** : portée des PNJ dialoguants resserrée à **1,50 m** (`radius` par
  défaut ajusté pour ces PNJ). Évite d'ouvrir un dialogue déjà hors seuil de rupture.
- **Verrouillage** : à l'ouverture, déplacement figé (caméra libre).
- **Rupture auto** : à chaque frame, distance XZ joueur↔PNJ. Si `> 1,50 m`, fermeture
  (`CloseReason::TooFar`). **Hystérésis** : rupture déclenchée à 1,60 m pour éviter le
  clignotement aux abords du seuil.
- **Fermeture manuelle** : ✕ ou Échap (`CloseReason::UserClose`).
- **Fin naturelle** : choix d'action `End` ou nœud sans choix (`CloseReason::EndNode`).

## 6. Cellule de dialogue (rendu, disposition B)

- **Fenêtre centrale** parchemin, cadre or, police décorative du jeu (Windlass).
- **Barre de titre** : portrait + nom PNJ + rôle + bouton ✕.
- **Zone texte** :
  - texte complet présent ; **auto-scroll fluide** vers le bas (vitesse lente,
    réglable ; ~0,3-0,5 px/frame de référence) ;
  - **scrollbar verticale dorée à droite** ; utiliser la scrollbar ou la molette
    **met l'auto-scroll en pause** ; il **reprend** lorsqu'on revient en bas ;
  - **word-wrap** automatique ; didascalies (`isCue`) en italique atténué.
- **Zone réponses** :
  - 2 à 5 choix, empilés, chacun = numéro (raccourcis **1-5**) + icône + libellé ;
  - **word-wrap** sur les libellés.

## 7. Journal de quête local

- Déclenchement : un choix dont `action ∈ {AcceptQuest, CompleteQuest}` **ou** dont
  `questId >= 0` consigne la conversation.
- **Stockage** : fichier JSON **par personnage**, côté client (chemin sous le
  répertoire de données utilisateur du client ; chemin exact figé au plan selon la
  convention de persistance client locale existante).
- **Entrée journal** : `{ timestamp, npcLabel, questId, lines[] (texte échangé),
  choiceText (choix retenu) }`.
- **Lecture** : le panneau journal de quête peut afficher ces entrées sous la quête
  associée (relecture du suivi).
- **Aucune écriture serveur** ; pas d'opcode, pas de migration.

## 8. Tests (Linux / ctest)

Sur `DialoguePresenter` (logique pure, sans ImGui) :

1. Navigation : `SelectChoice` avec `action == Continue` passe au `nextNodeId` correct.
2. Action `End` / nœud sans choix → ferme avec `CloseReason::EndNode`.
3. Conversion rétro-compat : `dialogue: [...]` → nœud unique + choix « Au revoir ».
4. Rupture distance : `Tick` avec distance > 1,60 m → `CloseReason::TooFar` ;
   ≤ 1,50 m → reste ouvert ; comportement d'hystérésis vérifié.
5. Auto-scroll : progression de l'offset dans le temps, pause/reprise.
6. Journal : un choix `AcceptQuest`/`questId>=0` produit une entrée journal correcte ;
   un choix neutre n'en produit pas.
7. Validation config : arbre invalide (`next` inconnu, 0 ou >5 choix) → fallback +
   log d'erreur.

Le rendu ImGui (`DialogueImGuiRenderer`) n'est pas couvert en CI (Windows-only).

## 9. Fichiers concernés

**Nouveaux**
- `src/client/dialogue/DialoguePresenter.{h,cpp}`
- `src/client/render/DialogueImGuiRenderer.{h,cpp}`
- `tests/...` test unitaire `DialoguePresenter` (selon arborescence de tests existante)

**Modifiés**
- `src/client/app/Engine.h` — structures dialogue, membres `m_dialogue*`, flag.
- `src/client/app/Engine.cpp` — bind/render, ouverture sur E, suppression poussée
  chat, tick distance, verrouillage déplacement.
- `config.json` — bloc `dialogue_tree` + `radius` 1,5 m sur PNJ dialoguants.
- `src/CMakeLists.txt` — ajout des nouveaux `.cpp` ; **le nouveau `.cpp` partagé
  testé doit aussi être ajouté à la liste `server_app`** si linké côté tests serveur
  (sinon uniquement aux cibles client/test concernées).

## 10. Hors périmètre (YAGNI)

- Protocole serveur de dialogue (opcodes), persistance DB de l'historique.
- Voix/audio, animations faciales, caméra cinématique dédiée.
- Édition de dialogues dans l'éditeur monde.
- Localisation multi-langue des arbres (les textes restent dans `config.json`).

## 11. Déploiement

> **Déploiement** : ✅ client uniquement, pas de redéploiement serveur.
> (Dialogue piloté par `config.json` côté client ; journal de conversation écrit en
> local ; aucun nouvel opcode, handler ou migration DB.)
