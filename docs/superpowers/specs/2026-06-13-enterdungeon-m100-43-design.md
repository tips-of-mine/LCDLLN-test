# Spec — Système de portails de donjon jouables (M100.43 / EnterDungeon)

> Statut : **brouillon de conception, à valider** avant implémentation.
> Issu du Lot C (audit 2026-06-13). Objectif : rendre **jouables** les portails de
> donjon que l'éditeur de carte sait déjà poser, en câblant l'émetteur client de
> l'opcode `EnterDungeon` (le handler master est **déjà** complet et câblé).

## 1. État actuel (vérifié)

- **Opcodes définis** : `kOpcodeEnterDungeonRequest = 197` (client→master) / `kOpcodeEnterDungeonResponse = 198` (`ProtocolV1Constants.h:817-818`).
- **Handler master COMPLET et câblé** (`src/masterd/handlers/dungeon/EnterDungeonHandler.{h,cpp}`, instancié+dispatché `main_linux.cpp:452,1129`) : valide session→accountId, vérifie l'**ownership** du personnage (prepared statement), `INSERT dungeon_instances`, renvoie la réponse 198 `{success, instanceId, shardEndpoint, errorCode}`.
- **Payloads dans `engine_core`** (`DungeonPayloads.{h,cpp}`, testés) : `BuildEnterDungeonRequestPayload(characterId, dungeonTemplateId, difficulty)`, `ParseEnterDungeonResponsePayload(...)`. **Déjà disponibles côté client** (le client linke `engine_core`).
- **Migration `0063_dungeon_instances.sql`** : table cible de l'INSERT (doit être appliquée côté master, sinon le handler répond `INTERNAL_ERROR`).
- **Manques (100% client)** : (a) aucun **émetteur** client de 197 ; (b) aucun **traitement** de la réponse 198 ; (c) aucun **consommateur runtime** des portails — l'éditeur écrit un `DungeonPortalDocument` mais le jeu ne charge ni n'affiche ni ne déclenche rien.
- **Limite serveur connue (MVP)** : `shardEndpoint` revient **vide** — le routage multi-instance (reconnexion à un shard de donjon dédié) est un follow-up serveur.

## 2. Objectif et périmètre

**But** : un joueur s'approche/interagit avec un portail posé dans l'éditeur → confirmation → le serveur crée une instance et répond → le client en informe le joueur.

**Hors périmètre (follow-up serveur, lock-step ultérieur)** : la reconnexion effective à un shard d'instance (routage `shardEndpoint`), le cap d'instances, le gating de progression/difficulté côté serveur.

## 3. Composants à livrer (client)

### 3.1 Chargement runtime des portails (`DungeonPortalRuntime`)
- Au chargement de zone, lire les instances de portail produites par l'éditeur (mêmes données que `DungeonPortalDocument` ; chemins namespacés par zone via `ZonePaths`). Catalog : `game/data/meshes/dungeons/catalog.json`.
- Représenter chaque portail comme une **entité interactible** + un **volume de déclenchement** (position, rayon/AABB, `dungeonTemplateId`, `difficulty` par défaut).
- Modèle d'accroche recommandé : **système d'interactibles existant (touche E)** plutôt qu'un nouveau sous-système (réutilise l'infra d'interaction in-world).

### 3.2 Déclencheur + UI de confirmation
- À l'entrée dans le volume **ou** à l'appui E sur le portail → ouvrir une **modale de confirmation** : « Entrer dans le donjon *\<nom>* ? » + (optionnel) sélecteur de **difficulté**.
- Boutons Entrer / Annuler. Anti-spam : un seul request en vol à la fois (mémoriser `pending`, comme `LfgUi` mémorise `m_pendingRole`).

### 3.3 Émetteur (opcode 197)
- À la confirmation : `m_send(kOpcodeEnterDungeonRequest, BuildEnterDungeonRequestPayload(characterId, templateId, difficulty))`.
- Patron exact : `LfgUi.cpp:83-92` + `SetSendCallback([this]{ return m_authUi.SendGenericRequestAsync(...); })` (déjà le mécanisme master côté client).

### 3.4 Traitement de la réponse (opcode 198)
- Ajouter `case kOpcodeEnterDungeonResponse:` dans le switch des réponses master d'`Engine.cpp` (modèle des `case kOpcodeGuild…Response`/`Auction…Response`, ~3050-3162) → `ParseEnterDungeonResponsePayload`.
- `success=false` → afficher le message d'erreur selon `errorCode` (ownership, internal…).
- `success=true`, `shardEndpoint` vide (MVP) → message « Instance \<instanceId> créée » (pas de reconnexion encore). Le hook de reconnexion shard est laissé en TODO explicite pour la phase serveur.

## 4. Flux de données

```
[Portail in-world] --E/volume--> [Modale confirmation] --confirm-->
  client: BuildEnterDungeonRequestPayload(charId, templateId, diff)
  --opcode 197 (TCP master)--> EnterDungeonHandler
     valide session + ownership perso --> INSERT dungeon_instances
  --opcode 198 {success,instanceId,shardEndpoint,errorCode}--> client
  client: parse --> succès: message/instanceId (MVP) | échec: message d'erreur
```

## 5. Gestion d'erreur
- `errorCode` master : ownership échouée, `INTERNAL_ERROR` (ex. table absente si 0063 non jouée). Le client mappe chaque code vers un message localisé (`LocalizationService`).
- Migration 0063 : prérequis master. **À vérifier qu'elle a été appliquée** lors d'un déploiement master antérieur ; sinon rejouer le master une fois.

## 6. Découpage en phases (proposé)
- **Phase 1 (S)** — Plomberie : émetteur 197 + `case` réponse 198 + mapping d'erreurs. Déclencheur minimal (interactible E sur un portail de test ou commande de validation). → testable de bout en bout contre le master déjà câblé.
- **Phase 2 (M)** — `DungeonPortalRuntime` : chargement des portails de la zone, volumes/interactibles, modale de confirmation + sélecteur de difficulté.
- **Phase 3 (serveur, lock-step)** — routage `shardEndpoint` (reconnexion à un shard d'instance), cap d'instances, gating progression.

## 7. Déploiement
- **Phases 1-2 : 100% client** — émetteur + loader + UI. ⚠️ vérifier que la **migration 0063 est appliquée côté master** (sinon `INTERNAL_ERROR`) ; si elle ne l'est pas, **rejouer le master une fois** (pas de wire-break, opcodes 197/198 déjà définis).
- **Phase 3 : redéploiement serveur (master/shard) en lock-step** dès que le routage d'instance est ajouté.

## 8. Questions ouvertes (à trancher avant implémentation)
1. **Déclencheur** : proximité (auto-prompt à l'entrée du volume) ou **touche E** explicite ? (reco : E, moins intrusif).
2. **Difficulté** : sélecteur dans la modale dès la phase 2, ou difficulté fixe par portail en MVP ?
3. **Comportement au succès en MVP** (sans routage shard) : simple message + log, ou téléportation locale provisoire dans la même zone ?
4. **Source des portails au runtime** : relire le `DungeonPortalDocument` éditeur, ou passer par le bundle d'export runtime (`ExportRuntimeBundle`) comme les autres assets de zone ? (reco : bundle runtime, cohérent avec le pipeline de livraison).
