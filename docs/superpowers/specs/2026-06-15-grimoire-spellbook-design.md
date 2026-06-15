# Grimoire / Carnet de techniques — Design

> Statut : **validé utilisateur** (brainstorming 2026-06-15).
> Maquette UI : [`2026-06-15-grimoire-mockup.html`](2026-06-15-grimoire-mockup.html).

## 1. Contexte & problème

L'audit client du 2026-06-15 (12 sous-systèmes) a montré que le « Grimoire » est
le **seul vrai greenfield** : aucun fichier `Grimoire`/`Spellbook` n'existe. En
revanche, **tout le lancement de sorts est déjà livré** par Combat SP3 :

- `src/client/gameplay/SpellKitCatalog.{h,cpp}` — catalogue client des kits de
  sorts (charge `game/data/gameplay/spells/*.json`, expose `SpellDisplay` :
  nom, slot, castTimeMs, cooldownMs, resourceCostPercent, type de cible).
- Barre d'action SP3 dans `src/client/app/Engine.cpp` (~ligne 10891) : 4 slots,
  touches 1-4 → `m_gameplayUdp.SendCastRequest(clientId, target, spell.spellId)`,
  cooldowns/coût affichés, barre de cast.
- Le client connaît son profil de combat : `uiModel.playerStats.profileId`.

Point déterminant : **la barre d'action envoie `spell.spellId`** dans le
`CastRequest`, jamais un numéro de slot. Le shard est donc **indifférent à
l'ordre des slots** — cet ordre est purement un concept d'**affichage + de
persistance côté personnage**.

Il manque donc un **panneau « livre de sorts »** : visualiser en détail les
sorts de son kit, et **réassigner quel sort occupe quel slot** de la barre
d'action (assignation persistée).

## 2. Objectif

Livrer un panneau ouvrable (« Grimoire » pour les casters, « Carnet de
techniques » pour les martiaux) qui :

1. **Affiche en détail** les sorts du kit du joueur (donnée 100% locale via
   `SpellKitCatalog`).
2. Permet de **réassigner les slots** de la barre d'action par **glisser-déposer**.
3. **Persiste** l'assignation côté serveur (suit le personnage entre machines).

## 3. Hors périmètre (non-goals)

- **Le lancement de sorts** : déjà couvert par SP3 (barre d'action / cast bar).
- **Apprentissage / déblocage de sorts par niveau** : pas de progression de
  sorts en V1 (le kit est entièrement « connu » dès le départ).
- **Bouton « Réinitialiser »** : retiré (YAGNI ; le joueur peut re-glisser).
- **Plus de 4 sorts par kit** : les kits actuels ont 3-4 sorts pour 4 slots.
  L'assignation est conçue pour le futur (> 4 sorts) mais livrée maintenant.
- **Changement de profil en jeu** : la classe (donc le profil) est figée à la
  création du personnage.

## 4. Décisions de design (validées)

| Sujet | Décision |
|---|---|
| Rôle | Référence (lecture détaillée) **+ assignation de slots** |
| Persistance | **Serveur** (shard, `PersistedCharacterState`) — Design A |
| Wire | **Shard UDP**, bump `kProtocolVersion` (cohérent avec SP3 / `profileId`) |
| Accès | **Toutes les classes**, thème adaptatif (Grimoire / Carnet) |
| Keybind | **K** pour ouvrir/fermer + slash commands `/grimoire` et `/sorts` |
| Interaction | **Glisser-déposer** ImGui (pas de clic-puis-clic requis en V1) |
| Livraison | **2 PR server-first**, lock-step shardd + client |

## 5. Modèle de données

### 5.1 Layout (concept central)

Un **`ActionBarLayout`** = tableau de **4 slots → spellId** (chaîne vide = slot
vide). Contraintes :

- Chaque `spellId` non vide doit appartenir au **kit du profil** du personnage.
- Un sort ne peut occuper **qu'un seul slot** (unicité).
- Slots vides autorisés.

### 5.2 Layout par défaut

Si le personnage n'a **aucun** layout persisté (1er login), le layout est
**vide** côté serveur ; le client retombe sur l'**ordre du kit** (slots = ordre
JSON, comportement actuel de la barre d'action). **Aucune régression** SP3.

### 5.3 Thème adaptatif (cosmétique client)

`profileId ∈ {lanceur, healer, sacre}` → **Grimoire** (visuel or, blason ✦).
Sinon (tank, melee, voleur, pisteur, distance) → **Carnet de techniques**
(visuel bleu acier, blason ⚔). Pure cosmétique : aucune incidence serveur.

## 6. Architecture serveur (PR-1)

### 6.1 Persistance

- Ajouter `actionBarLayout` (4 `std::string` spellId) à `PersistedCharacterState`
  (`src/shardd/gameplay/character/CharacterPersistence.{h,cpp}`), à côté de
  l'inventaire / l'or / les quêtes / les auras déjà persistés.
- Chargé à l'enter-world, sauvegardé à chaque modification validée.

### 6.2 Restitution à l'enter-world

- Le shard envoie le layout persisté avec les données d'entrée en jeu (même
  chemin que `profileId` dans le message de stats joueur). Layout vide si absent.

### 6.3 Handler `SetActionBarLayout`

- Nouveau message gameplay (UDP) : `{clientId, spellId[4]}`.
- Validation (autorité serveur) : chaque `spellId` non vide ∈ kit du profil
  (via `SpellKitLibrary::FindSpell(profileId, spellId)`), unicité des sorts,
  ≤ 4 slots. Invalide → rejet (ACK erreur), pas de persistance.
- Valide → met à jour l'état runtime + persiste (`PersistedCharacterState`) +
  ACK Ok.

### 6.4 Wire (`src/shared/network/ServerProtocol.{h,cpp}`)

- **Bump `kProtocolVersion`** (+ entrée d'historique).
- Nouveau(x) kind(s) : `SetActionBarLayoutRequest` {clientId u32, 4 × spellId
  string u16-prefixed (vide = slot libre)} et son ACK
  `SetActionBarLayoutResponse` {clientId u32, status u8 (Ok / Invalid)}.
- Champ `actionBarLayout` (4 spellId) ajouté au message d'enter-world / stats,
  dans le même bump de version.
- Encode/Decode + tests roundtrip + rejets tronqués (pattern des tests SP3).

> **Note CMakeLists** : tout nouveau `.cpp` partagé côté serveur doit être ajouté
> à la liste `server_app` **et** aux listes shardd dans `src/CMakeLists.txt`
> (cf. convention `SpellKitLibrary`). `server_app` ne linke pas `engine_core`.

## 7. Architecture client (PR-2)

### 7.1 État `ActionBarLayout`

- Tenu côté Engine / `UIModel` : 4 spellId résolus pour le personnage courant.
- Initialisé depuis la donnée d'enter-world ; si vide → ordre du
  `SpellKitCatalog.FindKit(profileId)`.
- `spellId` persisté absent du kit courant → slot vidé côté client (re-validé au
  prochain set).

### 7.2 Branchement barre d'action (modif SP3)

- La boucle barre d'action (`Engine.cpp` ~10894) lit le **layout résolu** au lieu
  de l'ordre brut du catalogue : touche `1+index` → `layout[index]` → `spellId`.
- Slot vide → case vide, pas de cast sur cette touche.

### 7.3 `GrimoireUiPresenter` (`src/client/grimoire/`)

- Pattern presenter standard (cf. `SkillBookUiPresenter`,
  `src/client/skills/SkillBookUi.{h,cpp}`) : `Init` / `Shutdown` /
  `GetState()` / `SetSendCallback`.
- Construit l'état à partir de `SpellKitCatalog.FindKit(profileId)` + layout.
- Mutation locale (optimiste) lors d'un drag&drop + envoi `SetActionBarLayout` ;
  réverte si ACK erreur.

### 7.4 `GrimoireImGuiRenderer` (`src/client/render/`)

- Pattern renderer standard (cf. `SkillBookImGuiRenderer`,
  `GuildImGuiRenderer`). Lit `GetState()`, propage les inputs au presenter.
- **Layout** (cf. maquette) : titre/blason adaptatif ; liste de cartes de sorts
  à gauche (icône, description, tags coût/CD/incantation/portée/cible/effet,
  badge « Slot N ») ; 4 slots à droite (touche, sort assigné, vide) ; mini-aperçu
  de la barre.
- **Drag & drop** : `BeginDragDropSource` sur chaque carte de sort,
  `BeginDragDropTarget` sur chaque slot, payload = `spellId`. Drop sur un slot
  occupé remplace ; un sort déjà placé ailleurs est retiré de son ancien slot
  (unicité). Drop d'un slot vers « hors slot » = vider (optionnel V1).

### 7.5 Ouverture / commandes

- Keybind **K** (libre) → toggle `m_grimoireVisible` (pattern des autres toggles
  Engine, cf. `m_guildVisible`).
- Slash commands `/grimoire` et `/sorts` → même toggle.
- Rendu conditionnel à `m_grimoireVisible` dans la boucle de rendu ImGui.

## 8. Flux end-to-end

1. **Enter-world** : shard → client, layout persisté (ou vide).
2. Client résout `ActionBarLayout` ; barre d'action + Grimoire le lisent.
3. Joueur ouvre le Grimoire (K), glisse un sort sur un slot.
4. Client applique immédiatement (optimiste), envoie `SetActionBarLayoutRequest`.
5. Shard valide (kit/unicité), persiste, ACK Ok → confirmé ; sinon ACK Invalid →
   client réverte + message d'erreur.

## 9. Cas limites

- **1er login** : layout vide → ordre JSON par défaut (pas de régression).
- **Contenu modifié** : `spellId` persisté absent du kit → slot vidé client,
  re-validé au prochain set.
- **Profil figé** : pas de gestion de changement de profil.
- **Sort en double** : interdit (unicité validée client + serveur).
- **Catalogue client vide** (JSON illisible) : le Grimoire se masque, la barre
  d'action reste pilotée serveur (politique tolérante `SpellKitCatalog`).

## 10. Tests

- **Serveur** : roundtrip wire `SetActionBarLayout` + enter-world layout ;
  rejets (spellId hors kit, doublon, payload tronqué) ; persistance/relecture du
  layout dans `PersistedCharacterState`.
- **Client** : résolution du layout (vide → ordre JSON ; persisté → ordre
  custom ; spellId obsolète → slot vidé) ; unicité au drop.
- CI : `build-linux` exécute ctest (ajouter les tests aux listes CMake).

## 11. Déploiement

- **PR-1** (serveur+wire) : ⚠️ **REDÉPLOIEMENT SERVEUR REQUIS** — bump
  `kProtocolVersion`, nouveau handler shard, champ persistance. Un shard neuf
  rejette les clients anciens (et inversement).
- **PR-2** (client) : à déployer **en lock-step** avec PR-1 (client neuf ↔ shard
  neuf). Merge PR-1 d'abord (CI verte), puis PR-2, puis déploiement simultané
  shardd + client.

## 12. Découpage des PR

| PR | Contenu | Déploiement |
|---|---|---|
| **PR-1 serveur+wire** | `kProtocolVersion`++ ; `actionBarLayout` dans `PersistedCharacterState` ; restitution enter-world ; handler + validation `SetActionBarLayout` ; tests roundtrip/persistance ; CMake | ⚠️ redéploiement **shardd** |
| **PR-2 client** | état `ActionBarLayout` ; barre d'action branchée sur le layout ; `GrimoireUiPresenter` + `GrimoireImGuiRenderer` (drag&drop) ; keybind K + `/grimoire`/`/sorts` ; tests client | lock-step avec PR-1 |
