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
- **Migration DB** : tout fichier `engine/server/migrations/00xx_*.sql` ajouté
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
