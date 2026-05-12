# Instructions persistantes pour Claude (LCDLLN)

## Coordination de déploiement client/serveur

Le projet a un cycle de release séparé client (Windows, Vulkan) ↔ serveur (Linux,
master + shard). L'utilisateur peut oublier de redéployer le serveur quand un
changement client le requiert. À la fin de **chaque PR / changement** je dois
**lui dire explicitement** si un redéploiement serveur (master et/ou shard) est
nécessaire ou non.

### Indique « **REDÉPLOIEMENT SERVEUR REQUIS** » si la PR contient l'une de :

- **Wire-breaking** : nouveau opcode, payload modifié, ordre des champs changé,
  taille fixe modifiée, bump `kProtocolVersion` (UDP gameplay) ou tout changement
  qui rend un client neuf incompatible avec un serveur ancien (ou inverse).
- **Nouveau handler côté serveur** : un handler master/shard qui répond à un
  nouvel opcode (sinon le client envoie dans le vide et reçoit BAD_REQUEST).
- **Migration DB** : tout fichier `sql/migrations/00xx_*.sql` ajouté
  ou modifié (idempotent ou pas — il faut quand même rejouer le binaire serveur
  pour que la migration s'applique au boot).
- **Modification de gating sécurité** : changement dans SessionManager,
  ConnectionSessionMap, AuthRegisterHandler, AccountStore qui change le
  comportement à l'AUTH ou la session.
- **Config serveur** : nouvelle clé lue par le serveur depuis `config.json`
  qui doit être renseignée pour que la feature fonctionne.

### Indique « **redéploiement serveur PAS nécessaire** » si la PR est :

- Purement client : rendu (Vulkan, ImGui, fonts, shaders), UI (auth screens,
  chat panel, HUD), localization, animations, son.
- Purement docs : CODEBASE_MAP.md, README, commentaires.
- Tests : tests unitaires sans nouveau handler serveur.
- Refactoring interne client : presenter / renderer split, accesseurs,
  callbacks UI, sans changement protocole.

### Format de la mention

À la fin du résumé de PR (dans le message de réponse, et si possible aussi
dans la description GitHub de la PR), inclure une ligne claire :

> **Déploiement** : ⚠️ redéploiement serveur (master) requis — nouveau opcode 47.
ou
> **Déploiement** : ✅ client uniquement, pas de redéploiement serveur.

Si la PR change à la fois client ET serveur (cas fréquent), préciser
**les deux côtés** doivent être déployés en lock-step (le client neuf parlerait
à un serveur ancien sinon).

### Cas particulier : chat MVP (PR #402)

A introduit opcodes 45/46 + ChatRelayHandler côté master → **redéploiement
serveur master requis** pour que le chat fonctionne.

## Éditeur monde (`lcdlln_world_editor.exe`)

L'éditeur monde est un binaire séparé (`--world-editor`) qui partage l'engine
avec le client de jeu mais a des conventions visuelles et de code propres.

### Police d'écriture

L'éditeur monde **n'utilise pas** la police décorative Windlass du jeu. Il
utilise **Arial** (`C:/Windows/Fonts/arial.ttf` par défaut) — neutre, lisible,
riche en glyphes accentués/ponctuation, standard sur Windows.

- Configuration : `editor.font.arial_path` + `editor.font.arial_pixel_height`
  dans `config.json`.
- Branchement : `WorldEditorImGui::Init(... isWorldEditorExe=true)` charge
  Arial comme police par défaut au lieu de Windlass. Le fallback ProggyClean
  et la fonte « valeurs » Morpheus sont aussi désactivés (Arial couvre tout).
- Pour un futur changement (autre police, taille différente), modifier la
  branche `if (isWorldEditorExe)` dans `src/world_editor/WorldEditorImGui.cpp`.

### Documentation des fonctions

**Règle stricte** : toute fonction (libre, méthode, lambda nommée) ajoutée
ou modifiée pour l'éditeur monde **doit être documentée systématiquement**
au moment de sa création/modification. La documentation se fait via un
commentaire `///` (Doxygen) juste au-dessus de la déclaration, ou un
commentaire `//` au-dessus de la définition si c'est une lambda interne.

Doivent être présents au minimum :
1. **Rôle** : que fait la fonction (1-2 phrases).
2. **Paramètres non-évidents** : `\param nom description` — pas besoin de
   redocumenter `cfg` ou `device` si leur usage est évident, mais documenter
   les flags, les indices, les unités physiques (mètres, radians, ms…).
3. **Effet de bord** : si la fonction modifie un état global (atlas ImGui,
   buffer GPU, fichier disque), le mentionner.
4. **Contraintes thread/timing** : ex. « doit être appelée en main thread,
   avant `ImGui_ImplVulkan_Init` ».

Périmètre concerné par cette règle :
- `src/world_editor/` (tous les fichiers)
- `src/client/render/terrain/TerrainEditingTools.{h,cpp}`
- toute partie de `src/client/app/Engine.cpp` ou `src/client/render/WorldEditorImGui.cpp`
  spécifique au mode éditeur (gardée par `m_worldEditorExe` ou
  `m_editorEnabled`).

Cette règle aide les futurs mappeurs et contributeurs à comprendre
l'outillage sans relire tout le code. Elle s'ajoute à la convention générale
du repo (commentaires en français, clarté > brièveté).
