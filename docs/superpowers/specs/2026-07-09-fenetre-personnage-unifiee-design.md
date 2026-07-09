# Fenêtre « Personnage » unifiée à onglets — design (Chantier 1)

Date : 2026-07-09
Statut : validé (direction approuvée par l'utilisateur, maquette `fenetre_personnage_onglets_mockup`)

## Contexte & problème

Retour joueur 2026-07-08 : « il y a trop d'interface concernant le personnage, ce
qui complexifie l'expérience ». Aujourd'hui cinq panneaux séparés, chacun sa
touche et sa fenêtre :

- F1 → fiche personnage (`CharacterSheetImGuiRenderer`, stats)
- F2 → livre de compétences (`SkillBookImGuiRenderer`)
- F3 → carnet de techniques / Grimoire (`GrimoireImGuiRenderer`)
- F4 → arbre de compétences (`ClassSkillTreeImGuiRenderer`)
- I → inventaire (grille 4×4 de #958)

Cet éparpillement charge le HUD et les touches. L'utilisateur veut **une seule
fenêtre à onglets**, ouverte par **F1**, regroupant tout, avec un onglet par
défaut montrant le **personnage en 3D d'un côté et l'inventaire de l'autre**,
argent inclus.

## Découpage en deux chantiers

Le projet mêle deux tailles de travail très différentes ; on les sépare.

- **Chantier 1 (cette spec)** — la fenêtre unifiée à onglets. Surtout client :
  conteneur à onglets, ré-hébergement des panneaux existants, onglet par défaut
  (3D **en vue seule** + inventaire + caractéristiques + argent). Corrige au
  passage le bug d'inventaire en double.
- **Chantier 2 (spec ultérieure)** — équiper le loot sur le perso 3D. Sous-système
  complet client **+ serveur** : slots d'équipement fonctionnels, protocole
  equip/unequip, persistance DB, recalcul de stats, loot modifiant le mesh 3D.
  ⚠️ redéploiement serveur. **Hors périmètre ici.**

L'onglet par défaut du Chantier 1 **dessine déjà les emplacements d'équipement**
mais **inactifs** (pointillés), pour réserver la place ; le Chantier 2 les
activera sans refonte de disposition.

## Objectif du Chantier 1

Une fenêtre `Personnage` ouverte par **F1**, 4 onglets, les touches **F2/F3/F4/I
libérées** (anciens toggles retirés). 100% client, aucun redéploiement serveur.

## Architecture

### Nouveau composant : `CharacterWindowImGuiRenderer`

Un renderer conteneur (dossier `src/client/render/`) qui :

1. Dessine le cadre de fenêtre ImGui (`Begin("Personnage##ln_character_window",
   &m_characterWindowVisible, …)`), la barre d'onglets (`ImGui::BeginTabBar`),
   et route vers le contenu de l'onglet actif.
2. **Est invoqué au même endroit que les panneaux existants** (`Engine::Update`,
   bloc ~`Engine.cpp:12580-12740`, à côté de `m_grimoireImGui->Render()` etc.).
   C'est le point de la frame finalisé par un **unique** `ImGui::Render()` — d'où
   l'absence de doublon. (cf. section « Correctif inventaire en double ».)
3. Pour les onglets Compétences / Techniques / Arbre, **délègue aux renderers
   existants** (`SkillBookImGuiRenderer`, `GrimoireImGuiRenderer`,
   `ClassSkillTreeImGuiRenderer`) — pas de réécriture. Le conteneur les affiche
   « embarqués » dans l'onglet plutôt qu'en fenêtres autonomes (voir « Intégration
   des panneaux existants »).
4. Pour l'onglet Personnage (défaut), rend lui-même : aperçu 3D + emplacements
   d'équipement inactifs + caractéristiques compactes + inventaire 4×4 + bourse.

### Les 4 onglets

| Onglet | Contenu | Source |
|--------|---------|--------|
| **Personnage** (défaut) | 3D (vue seule) + slots équipement inactifs + caractéristiques + inventaire + argent | nouveau, réutilise `RacePreviewViewport`, `InventoryUiPresenter`, `CurrencyFormat.h` |
| **Compétences** | livre de compétences | `SkillBookImGuiRenderer` (délégué) |
| **Techniques** | carnet de techniques | `GrimoireImGuiRenderer` (délégué) |
| **Arbre** | arbre de compétences par classe | `ClassSkillTreeImGuiRenderer` (délégué) |

Les **caractéristiques** (ex-fiche F1) sont fusionnées dans l'onglet Personnage
(bloc compact lu depuis `UIModel`), plutôt qu'un 5ᵉ onglet. La fenêtre autonome
`CharacterSheetImGuiRenderer` est retirée (son contenu stats est repris compact).

### Onglet « Personnage » — disposition

Deux colonnes (cf. maquette validée) :

- **Gauche (~46%)** :
  - Aperçu 3D encadré, alimenté par `RacePreviewViewport` (déjà existant, utilisé
    à la création de personnage : rend le perso skinné dans une texture 512×512).
    Vue seule ; rotation à la souris si simple à câbler, sinon rotation lente
    auto. Affiché via `ImGui::Image(<texId viewport>)`.
  - Emplacements d'équipement **inactifs** dessinés autour du 3D (pointillés,
    libellé « Chantier 2 »). Aucune logique d'équipement.
  - Bloc **Caractéristiques** compact : niveau, PV, ressource, dégâts (lus sur
    `uiModel.playerStats`).
- **Droite (~54%)** :
  - **Inventaire** 4×4 depuis `InventoryUiPresenter.GetState()` (icônes via
    `m_skillIconCache`, quantités, tooltips) — même rendu que #958 mais placé
    dans l'onglet (donc single-pass).
  - **Bourse** or/argent/bronze via `CurrencyFormat.h` (#959), en bas.

### Interaction / touches

- **F1** : ouvre/ferme la fenêtre (`controls.keybind.charactersheet`, défaut F1 —
  on réutilise cette clé de config, renommée conceptuellement « ouvrir Personnage »).
- **F2, F3, F4, I** : anciens toggles **retirés** (les blocs correspondants dans
  `Engine::Update` — grimoire ~7752, skilltree ~7774, inventaire I ~7318 ajouté
  en #958, et le toggle F1 fiche ~7920). Les touches redeviennent libres.
- **Navigation onglets** : clic sur la barre d'onglets. À l'ouverture, la fenêtre
  montre l'onglet par défaut (Personnage) — mémorisation du dernier onglet
  optionnelle (non requise en v1).
- Gardes d'ouverture identiques aux panneaux actuels (pas en chat focus / menu
  pause / éditeur ; post-auth flow complet).

### Correctif « inventaire en double » (#958)

**Cause racine confirmée** : le `ImGui::Begin` de l'inventaire de #958 était placé
dans la **région HUD avant-plan** de `Engine::Update` (~`Engine.cpp:12360`), entre
deux `ImGui::Render()` (10614 et 12904). Son contenu de fenêtre était finalisé
dans deux passes → **deux grilles empilées**. Les panneaux existants (Grimoire…)
ne doublent pas car ils sont rendus plus bas (~12580-12740), finalisés par un
unique `ImGui::Render()`.

**Correctif** : l'inventaire n'existe plus en fenêtre autonome ; il devient
l'onglet Personnage de `CharacterWindowImGuiRenderer`, rendu dans le **bon bloc**
(single-pass) → plus de doublon. Le bloc HUD `m_inventoryVisible`/`ln_inventory`
de #958 est supprimé.

### Intégration des panneaux existants

Les `*ImGuiRenderer` existants font aujourd'hui leur propre `ImGui::Begin(...)`
(fenêtre autonome) selon un flag `SetEnabled(...)`. Deux options d'intégration :

- **A (recommandée)** : le conteneur ouvre le corps de l'onglet
  (`BeginTabItem`) et appelle le `Render()` du panneau **dans** ce corps ; on
  ajuste chaque renderer pour dessiner son contenu sans re-`Begin` une fenêtre
  propre (mode « embarqué » via un flag, p. ex. `SetEmbedded(true)`), en gardant
  le mode fenêtre autonome pour compatibilité/tests.
- **B (repli)** : conserver les fenêtres autonomes mais les positionner/afficher
  pilotées par l'onglet actif (moins propre, fenêtres qui flottent).

On part sur **A** : petit flag « embedded » par renderer, le corps de contenu
existant réutilisé tel quel.

## Réutilisation (anti-duplication)

- `RacePreviewViewport` — rendu 3D perso (existe, création de personnage).
- `InventoryUiPresenter` — grille 4×4 (existe).
- `CurrencyFormat.h` — or/argent/bronze (#959).
- `SkillBookImGuiRenderer`, `GrimoireImGuiRenderer`, `ClassSkillTreeImGuiRenderer`
  — contenu des 3 onglets (existent).
- `m_skillIconCache` — icônes d'objets/sorts (existe).

Aucune classe/logique n'est réécrite ; le Chantier 1 est essentiellement de
l'assemblage + le retrait des anciens toggles + le correctif de passe.

## Hors périmètre (→ Chantier 2)

- Slots d'équipement fonctionnels, drag-to-equip du loot.
- Protocole réseau equip/unequip, persistance DB, recalcul des stats depuis le
  stuff, modification du mesh 3D selon l'équipement.
- ⚠️ Tout redéploiement serveur.

## Déploiement

✅ **Client uniquement, aucun redéploiement serveur.** (Aucun opcode, aucun
handler, aucune migration ; lit uniquement des données déjà reçues.)

## Vérification

Pas de toolchain locale → validation en jeu après build CI :

1. **F1** ouvre la fenêtre Personnage ; re-presser F1 ferme.
2. **F2/F3/F4/I** ne font plus rien (touches libérées).
3. Les 4 onglets s'affichent et basculent au clic ; Compétences/Techniques/Arbre
   montrent le même contenu qu'avant.
4. Onglet Personnage : le perso 3D s'affiche, l'inventaire apparaît **une seule
   fois** (bug #958 corrigé), l'argent s'affiche en or/argent/bronze.
5. Aucune régression des autres éléments de HUD (barre d'action, bourse HUD,
   cadre cible, etc.).

Tests unitaires : pas de nouvelle logique pure justifiant un test dédié (assemblage
UI) ; les presenters réutilisés ont déjà leurs tests.

## Risques / points d'attention

- **Embarquer `RacePreviewViewport` dans un onglet ImGui** : il rend dans une
  texture ; il faut exposer son `ImTextureID` et le rendre à chaque frame où
  l'onglet Personnage est ouvert. Coût 512×512, acceptable ; ne rendre que si
  l'onglet est actif (économie GPU).
- **Flag « embedded » des renderers existants** : petite modif par renderer ;
  garder le chemin fenêtre-autonome pour ne rien casser ailleurs.
- **Convention winding / pipelines Vulkan** : le viewport 3D perso a déjà son
  pipeline (création de personnage) — ne pas toucher aux `frontFace`/`cullMode`
  (cf. CLAUDE.md, garde anti-régression terrain/avatar).

## Petit correctif indépendant (hors cette fenêtre)

Le texte de zoom « 200 m » du radar est mal placé (chevauche la glissière en arc) ;
il doit passer **au-dessus** de l'arc. Radar-seul, indépendant de cette fenêtre —
traité comme correctif séparé (ne pas mêler à cette spec).
