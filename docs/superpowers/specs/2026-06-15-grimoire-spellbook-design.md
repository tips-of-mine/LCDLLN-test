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
- Barre d'action SP3 dans `src/client/app/Engine.cpp` (~ligne 10891) : **4 slots
  aujourd'hui**, touches 1-4 → `m_gameplayUdp.SendCastRequest(clientId, target,
  spell.spellId)`, cooldowns/coût affichés, barre de cast.
- Le client connaît son profil de combat : `uiModel.playerStats.profileId`.

Point déterminant : **la barre d'action envoie `spell.spellId`** dans le
`CastRequest`, jamais un numéro de slot. Le shard est donc **indifférent à
l'ordre des slots** — cet ordre est purement un concept d'**affichage + de
persistance côté personnage**.

Il manque donc un **panneau « livre de sorts »** : visualiser en détail les
sorts de son set connu, et **réassigner quel sort occupe quel slot** de la barre
d'action (assignation persistée). Avec une cible de **~60 sorts connus par
personnage** (sur une liste de 180 par classe, à venir), la barre passe de **4 à
10 slots** et la liste de sorts devient **défilante**.

## 2. Objectif

Livrer un panneau ouvrable (« Grimoire » pour les casters, « Carnet de
techniques » pour les martiaux) qui :

1. **Affiche en détail** les sorts connus du personnage — liste **défilante** +
   **recherche** (donnée 100% locale via `SpellKitCatalog`).
2. Permet de **réassigner les 10 slots** de la barre d'action par
   **glisser-déposer**.
3. **Persiste** l'assignation côté serveur (suit le personnage entre machines).

## 3. Hors périmètre (non-goals)

- **Le lancement de sorts** : déjà couvert par SP3 (barre d'action / cast bar).
- **Système de contenu de sorts (180/classe) + set connu (~60/perso) +
  progression/apprentissage** : **chantier séparé** (prérequis futur). Le
  Grimoire est conçu **agnostique au nombre** : il affiche le set connu du
  personnage quel qu'en soit le cardinal (3-4 aujourd'hui via le kit du profil,
  ~60 demain), **sans retouche**. La validation serveur s'appuie sur « le set
  connu du perso » (= le kit du profil aujourd'hui).
- **Bouton « Réinitialiser »** : retiré (YAGNI ; le joueur peut re-glisser).
- **Changement de profil en jeu** : la classe (donc le profil) est figée à la
  création du personnage.

## 4. Décisions de design (validées)

| Sujet | Décision |
|---|---|
| Rôle | Référence (lecture détaillée) **+ assignation de slots** |
| Persistance | **Serveur** (shard, `PersistedCharacterState`) — Design A |
| Wire | **Shard UDP**, bump `kProtocolVersion` (cohérent avec SP3 / `profileId`) |
| Accès | **Toutes les classes**, thème adaptatif (Grimoire / Carnet) |
| Slots barre d'action | **10** sur la **rangée du haut physique** ; **remappables** via `controls.keybind.action_slot_1..10` (défaut `VK_1..VK_0` ; AZERTY = `& é " ' ( - è _ ç à`) ; glyphe d'affichage adapté au clavier |
| Liste de sorts | **Défilante** (barre de défilement) + **recherche** par nom |
| Keybind ouverture | **`controls.keybind.grimoire`** (défaut **V**, remappable) + slash `/grimoire`, `/sorts` |
| Interaction | **Glisser-déposer** ImGui |
| Contenu 180/connu 60 | **Découplé** — chantier séparé, le Grimoire le consomme sans retouche |
| Livraison | **2 PR server-first**, lock-step shardd + client |

## 5. Modèle de données

### 5.1 Layout (concept central)

Un **`ActionBarLayout`** = tableau de **10 slots → spellId** (chaîne vide = slot
vide). Contraintes :

- Chaque `spellId` non vide doit appartenir au **set connu du personnage**
  (aujourd'hui = le kit du profil ; demain = le set de ~60 sorts).
- Un sort ne peut occuper **qu'un seul slot** (unicité).
- Slots vides autorisés (cas normal tant que le set connu < 10).

### 5.2 Layout par défaut

Si le personnage n'a **aucun** layout persisté (1er login), le layout est
**vide** côté serveur ; le client le remplit avec les **premiers sorts connus
dans l'ordre** (slots = ordre du set, comportement actuel de la barre d'action,
étendu à 10). **Aucune régression** SP3 : avec 3-4 sorts connus, les slots
au-delà restent simplement vides.

### 5.3 Thème adaptatif (cosmétique client)

`profileId ∈ {lanceur, healer, sacre}` → **Grimoire** (visuel or, blason ✦).
Sinon (tank, melee, voleur, pisteur, distance) → **Carnet de techniques**
(visuel bleu acier, blason ⚔). Pure cosmétique : aucune incidence serveur.

## 6. Architecture serveur (PR-1)

### 6.1 Persistance

- Ajouter `actionBarLayout` (**10** `std::string` spellId) à
  `PersistedCharacterState`
  (`src/shardd/gameplay/character/CharacterPersistence.{h,cpp}`), à côté de
  l'inventaire / l'or / les quêtes / les auras déjà persistés.
- Chargé à l'enter-world, sauvegardé à chaque modification validée.

### 6.2 Restitution à l'enter-world

- Le shard envoie le layout persisté avec les données d'entrée en jeu (même
  chemin que `profileId` dans le message de stats joueur). Layout vide si absent.

### 6.3 Handler `SetActionBarLayout`

- Nouveau message gameplay (UDP) : `{clientId, spellId[10]}`.
- Validation (autorité serveur) : chaque `spellId` non vide ∈ **set connu du
  perso** (aujourd'hui via `SpellKitLibrary::FindSpell(profileId, spellId)`),
  unicité des sorts, ≤ 10 slots. Invalide → rejet (ACK erreur), pas de
  persistance.
- Valide → met à jour l'état runtime + persiste (`PersistedCharacterState`) +
  ACK Ok.

### 6.4 Wire (`src/shared/network/ServerProtocol.{h,cpp}`)

- **Bump `kProtocolVersion`** (+ entrée d'historique).
- Nouveau(x) kind(s) : `SetActionBarLayoutRequest` {clientId u32, **10** ×
  spellId string u16-prefixed (vide = slot libre)} et son ACK
  `SetActionBarLayoutResponse` {clientId u32, status u8 (Ok / Invalid)}.
- Champ `actionBarLayout` (**10** spellId) ajouté au message d'enter-world /
  stats, dans le même bump de version.
- Encode/Decode + tests roundtrip + rejets tronqués (pattern des tests SP3).

> **Note CMakeLists** : tout nouveau `.cpp` partagé côté serveur doit être ajouté
> à la liste `server_app` **et** aux listes shardd dans `src/CMakeLists.txt`
> (cf. convention `SpellKitLibrary`). `server_app` ne linke pas `engine_core`.

## 7. Architecture client (PR-2)

### 7.1 État `ActionBarLayout`

- Tenu côté Engine / `UIModel` : **10** spellId résolus pour le personnage
  courant.
- Initialisé depuis la donnée d'enter-world ; si vide → premiers sorts connus
  via `SpellKitCatalog.FindKit(profileId)` dans l'ordre.
- `spellId` persisté absent du set connu courant → slot vidé côté client
  (re-validé au prochain set).

### 7.2 Branchement barre d'action (modif SP3)

- Étendre la barre d'action SP3 (`Engine.cpp` ~10894) de **4 à 10 slots**.
- Elle lit le **layout résolu** au lieu de l'ordre brut du catalogue : slot
  `index` → `layout[index]` → `spellId`.
- **Touches remappables** (cf. §7.6) : chaque slot lit sa touche depuis
  `controls.keybind.action_slot_{index+1}` (défaut `VK_1..VK_9` puis `VK_0`),
  **remplace** le `'1'+index` codé en dur (Engine.cpp:10950, valable seulement
  jusqu'à 9 — le slot 10 nécessite `Digit0`).
- Slot vide → case vide, pas de cast sur cette touche.

### 7.3 `GrimoireUiPresenter` (`src/client/grimoire/`)

- Pattern presenter standard (cf. `SkillBookUiPresenter`,
  `src/client/skills/SkillBookUi.{h,cpp}`) : `Init` / `Shutdown` /
  `GetState()` / `SetSendCallback`.
- Construit l'état à partir de `SpellKitCatalog.FindKit(profileId)` + layout +
  filtre de recherche courant.
- Mutation locale (optimiste) lors d'un drag&drop + envoi `SetActionBarLayout` ;
  réverte si ACK erreur.

### 7.4 `GrimoireImGuiRenderer` (`src/client/render/`)

- Pattern renderer standard (cf. `SkillBookImGuiRenderer`,
  `GuildImGuiRenderer`). Lit `GetState()`, propage les inputs au presenter.
- **Layout** (cf. maquette) : titre/blason adaptatif ; à gauche un **champ de
  recherche** + une **liste défilante** de cartes de sorts (icône, description,
  tags coût/CD/incantation/portée/cible/effet, badge « Slot N ») — `BeginChild`
  avec scroll ImGui, dimensionné pour ~60 entrées ; à droite les **10 slots**
  (grille, touche, sort assigné/vide, ✕ pour vider) ; mini-aperçu de la barre.
- **Drag & drop** : `BeginDragDropSource` sur chaque carte de sort,
  `BeginDragDropTarget` sur chaque slot, payload = `spellId`. Drop sur un slot
  occupé remplace ; un sort déjà placé ailleurs est retiré de son ancien slot
  (unicité).

### 7.5 Ouverture / commandes

- Keybind `controls.keybind.grimoire` (défaut **V**, remappable, cf. §7.6) →
  toggle `m_grimoireVisible` (pattern des autres toggles Engine, cf.
  `m_guildVisible`).
- Slash commands `/grimoire` et `/sorts` → même toggle.
- Rendu conditionnel à `m_grimoireVisible` dans la boucle de rendu ImGui.

### 7.6 Intégration au sous-système de raccourcis

Le projet a déjà un sous-système de binds : `controls.keybind.*` (résolu via
`KeyFromName`/`KeyName`, Engine.cpp ~415-447), rebindable depuis le menu Options
(M39.2). Le Grimoire **réutilise** ce mécanisme plutôt que de coder des touches
en dur :

- **Ouverture** : `controls.keybind.grimoire` (défaut **V** — `K`/`B`/`J` sont
  déjà pris par artisanat/SkillBook/combat-avancé) → toggle `m_grimoireVisible`.
- **10 slots de cast** : `controls.keybind.action_slot_1..10` (défaut
  `VK_1..VK_0`), exposés dans le menu Options comme les autres binds.
- **Glyphe d'affichage adapté au clavier** : le **nom de config sérialisé reste
  stable et portable** (la table `kRebindableKeys`/`KeyName` actuelle, ex.
  « 1 ».. « 0 ») — **ne pas** le rendre layout-dépendant (sinon le round-trip
  `KeyName↔KeyFromName` et le fichier de config cassent). Ajouter un **helper
  d'affichage séparé** `KeyGlyph(Key)` qui résout le glyphe via
  `MapVirtualKey(VK_n, MAPVK_VK_TO_CHAR)` → AZERTY `& é " ' ( - è _ ç à`,
  QWERTY `1 … 0`. Ce glyphe est utilisé par la **barre d'action**, les **slots
  du Grimoire** et l'**écran Options**.

> **Dette connue (hors périmètre, à signaler)** : `default_keybindings.json`
> est désynchronisé du code (il déclare `open_skills = K` alors qu'Engine câble
> `B` = SkillBook et `K` = artisanat). On ne corrige pas cette dette ici ; le
> Grimoire s'aligne sur le mécanisme **réellement utilisé** (`controls.keybind.*`).

> **Caveat nommage** : le SkillBook existant s'intitule déjà « **Carnet de
> sorts** » dans le menu (Engine.cpp:11657). Le thème martial du Grimoire reste
> « **Carnet de techniques** » (distinct), mais surveiller la confusion ; un
> renommage du SkillBook pourra être proposé hors de ce chantier.

## 8. Flux end-to-end

1. **Enter-world** : shard → client, layout persisté (ou vide).
2. Client résout `ActionBarLayout` (10) ; barre d'action + Grimoire le lisent.
3. Joueur ouvre le Grimoire (V par défaut), glisse un sort sur un slot.
4. Client applique immédiatement (optimiste), envoie `SetActionBarLayoutRequest`.
5. Shard valide (set connu/unicité), persiste, ACK Ok → confirmé ; sinon ACK
   Invalid → client réverte + message d'erreur.

## 9. Cas limites

- **1er login** : layout vide → premiers sorts connus dans l'ordre (pas de
  régression ; slots au-delà du set connu = vides).
- **Contenu modifié** : `spellId` persisté absent du set connu → slot vidé côté
  client, re-validé au prochain set.
- **Profil figé** : pas de gestion de changement de profil.
- **Sort en double** : interdit (unicité validée client + serveur).
- **Catalogue client vide** (JSON illisible) : le Grimoire se masque, la barre
  d'action reste pilotée serveur (politique tolérante `SpellKitCatalog`).

## 10. Tests

- **Serveur** : roundtrip wire `SetActionBarLayout` (10 slots) + enter-world
  layout ; rejets (spellId hors set connu, doublon, payload tronqué) ;
  persistance/relecture du layout dans `PersistedCharacterState`.
- **Client** : résolution du layout (vide → ordre du set ; persisté → ordre
  custom ; spellId obsolète → slot vidé) ; unicité au drop ; filtre de recherche.
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
| **PR-1 serveur+wire** | `kProtocolVersion`++ ; `actionBarLayout` (10) dans `PersistedCharacterState` ; restitution enter-world ; handler + validation `SetActionBarLayout` ; tests roundtrip/persistance ; CMake | ⚠️ redéploiement **shardd** |
| **PR-2 client** | barre d'action SP3 étendue 4→10 ; binds `controls.keybind.action_slot_1..10` (défaut `VK_1..VK_0`) + `controls.keybind.grimoire` (défaut V) + helper `KeyGlyph` layout-aware (Options inclus) ; état `ActionBarLayout` ; `GrimoireUiPresenter` + `GrimoireImGuiRenderer` (liste défilante + recherche + 10 slots + drag&drop) ; slash `/grimoire`/`/sorts` ; tests client | lock-step avec PR-1 |

## 13. Chantier séparé (prérequis futur, hors de ce design)

Pour atteindre l'échelle visée (~60 sorts connus / 180 par classe), un chantier
distinct devra fournir : la **liste maîtresse de 180 sorts par classe**, le
**set connu par personnage** (~60) avec sa **persistance serveur**, et le
**système d'apprentissage/déblocage**. Le Grimoire consommera ce set connu sans
modification (la validation serveur basculera de « kit du profil » vers « set
connu » au même point de code).
