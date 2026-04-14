# Design — Phase 1 : Corrections de bugs (LCDLLN)

**Date :** 2026-04-13
**Projet :** Les Chroniques De La Lune Noire
**Scope :** Phase 1 — Corrections de bugs et ajustements UI/serveur
**Approche retenue :** Fix par couche technique (serveur → layout/rendu → événements souris → textes/labels)

---

## Contexte

Le projet est un MMO C++20 avec une UI Vulkan custom (state machine + layout calculations, pas de Dear ImGui), un serveur master MySQL, et un système audio maison. Cette phase corrige les bugs bloquants et les problèmes d'ergonomie identifiés avant d'aborder les nouvelles fonctionnalités (Phase 2).

---

## Couche 1 — Serveur

### 1.1 Auto-enregistrement du master au démarrage

**Problème :** Le master ne s'inscrit pas en base de données au démarrage → la liste des serveurs est vide côté client après authentification.

**Migration :** `db/migrations/0017_game_servers.sql`

```sql
CREATE TABLE game_servers (
    server_id      INT AUTO_INCREMENT PRIMARY KEY,
    name           VARCHAR(64)  NOT NULL,
    host           VARCHAR(128) NOT NULL,
    port           SMALLINT UNSIGNED NOT NULL,
    max_players    INT UNSIGNED NOT NULL DEFAULT 0,
    online_players INT UNSIGNED NOT NULL DEFAULT 0,
    status         ENUM('online','offline','maintenance') NOT NULL DEFAULT 'offline',
    last_heartbeat DATETIME NULL,
    created_at     DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
);
```

**Nouvelle classe `ServerRegistry`** (engine/server/) :
- Au démarrage : `INSERT ... ON DUPLICATE KEY UPDATE` avec les valeurs lues depuis `config.json` (name, host, port, max_players), statut → `online`
- À l'arrêt propre (SIGTERM/SIGINT) : statut → `offline`

**Protocole client :**
- Après auth réussie, le master envoie un paquet `SERVER_LIST_RESPONSE` contenant toutes les entrées `status = 'online'` de `game_servers`
- Nouveau fichier `engine/network/ServerListPayloads.cpp/.h` pour la sérialisation/désérialisation
- Le client parse la liste et l'affiche sur la page de sélection de serveur

### 1.2 Horodatage des logs

**Format :** `[JJ/MM/AAAA][HH:MM:SS] [Catégorie] Message...`

**Exemple :** `[13/04/2026][14:32:07] [Auth] Connexion acceptée pour login=foo`

**Modification :** `engine/core/Log.cpp` — préfixe ajouté à chaque ligne via `std::chrono` ou `std::localtime`.

### 1.3 Bannière de démarrage

Affichée dans le point d'entrée du serveur, après toutes les initialisations, avant d'accepter des connexions :

```
###############################################################
#
# Serveur
# Les Chroniques De La Lune Noire
# Version X.Y.Z
#
###############################################################
# Serveur ready
###############################################################
```

La version est lue depuis une constante définie via CMake (`PROJECT_VERSION` ou constante dédiée).

---

## Couche 2 — Layout / Rendu

### 2.1 Superpositions de texte (problème systémique)

**Cause probable :** Des éléments de l'état précédent restent dans le `RenderModel` lors d'un changement d'état.

**Fix :** S'assurer que `BuildRenderModel()` commence par un clear complet et ne produit des éléments que pour la phase/état courant. Tout changement de `Phase` dans `AuthUiPresenter` déclenche un reset du `RenderModel`.

Écrans concernés : page d'auth, page de connexion, messages "serveur indisponible".

### 2.2 Taille de police dans les champs de saisie

Augmentation de la constante de taille de police pour les zones de saisie dans `AuthUiRenderer.h` (ou les métriques de layout). Appliquée uniformément sur toutes les pages (auth, inscription).

### 2.3 Logo d'information ("i") — affichage + popup

**Affichage :** Correction du path de rendu dans `AuthGlyphPass` ou `AuthLogoPass` pour inclure l'icône "i" dans le `RenderModel` sur les pages d'auth et d'inscription.

**Popup au clic :**
- Fond opaque (rectangle semi-transparent foncé sur toute la surface)
- Cadre centré avec le texte issu des fichiers de localisation (contenu déjà défini)
- Fermeture au clic en dehors du cadre ou sur un bouton "Fermer"

**État dans `AuthUiPresenter` :**
- `m_infoPopupVisible` (bool)
- `m_infoPopupText` (string_view vers la clé de localisation)

Le popup est rendu en dernier dans `BuildRenderModel()` pour s'afficher par-dessus tout le reste.

### 2.4 Sélecteurs de date (jour/mois/année)

Remplacement des champs texte libres par trois composants `DropdownField` dans le `RenderModel` :
- **Jour :** liste 1–31 (validée selon le mois sélectionné)
- **Mois :** liste Jan–Déc (texte localisé)
- **Année :** liste sur plage définie (ex. 1900–2010)

Nouveau type `DropdownField` dans le `RenderModel`, rendu par `AuthGlyphPass`.

### 2.5 Rectangle blanc au lancement

**Cause probable :** Couleur de clear Vulkan initialisée à blanc, ou passe de rendu exécutée avant la disponibilité des assets.

**Fix :**
- Initialiser la couleur de clear à noir `(0, 0, 0, 1)` dès la création de la swapchain
- Conditionner les premières passes de rendu à la disponibilité des ressources nécessaires

### 2.6 Page intermédiaire post-inscription

Nouvelle `Phase::EmailConfirmationPending` affichée après confirmation serveur d'une inscription réussie :
- Message localisé : "Un email vous a été envoyé, veuillez le valider."
- Bouton retour vers la page de connexion

Le TAG-ID peut rester affiché en complément s'il est déjà implémenté dans le bandeau de confirmation.

### 2.7 Suppression de l'affichage du nombre de comptes

Retrait de l'élément UI affichant le nombre de comptes existants du `RenderModel`. Aucun changement côté serveur.

---

## Couche 3 — Événements Souris

### 3.1 Boutons non cliquables (validation inscription + retour)

**Cause probable :** Bounding boxes incorrectes ou hit-testing non actif pour certains états dans `AuthUi::Update()`.

**Fix :** Audit de toutes les bounding boxes générées par `BuildAuthUiLayoutMetrics()` pour chaque bouton de chaque `Phase`. Vérification que le chemin `mouse click → hit-test → action` est actif dans tous les états concernés.

### 3.2 Sélecteurs de date — interaction souris

Logique d'interaction pour les `DropdownField` introduits en 2.4, dans `AuthUi::Update()` :
- Clic sur le dropdown → ouvre la liste (`m_openDropdown`)
- Clic sur une valeur → sélectionne et ferme
- Clic en dehors → ferme sans modifier la valeur
- Un seul dropdown ouvert à la fois

### 3.3 Logo "i" — clic souris

Le logo "i" reçoit sa bounding box dans le `RenderModel`. Hit-test dans `AuthUi::Update()` détecte le clic et bascule `m_infoPopupVisible = true`. Clic sur le fond opaque ou le bouton "Fermer" → `m_infoPopupVisible = false`.

---

## Couche 4 — Textes / Labels / États

### 4.1 Label "Se souvenir" → libellé explicite

Remplacement par **"Mémoriser mon identifiant"** (ou équivalent localisé). Modification dans le fichier de localisation + ajustement de la bounding box si nécessaire.

### 4.2 Bouton "Inscription" — libellé / taille

Selon ce que révèle l'inspection du code :
- Si texte incorrect dans le fichier de langue → correction du texte
- Si bounding box trop petite → ajustement de la largeur dans les métriques de layout

### 4.3 Message d'erreur générique en cas d'échec d'auth

Remplacement des messages spécifiques (login incorrect / mot de passe incorrect) par un message unique localisé : **"Identifiant ou mot de passe incorrect."**

Modification côté client uniquement — aucun changement serveur.

### 4.4 Gestion du cycle de vie de l'écran de sélection de langue

**Problème :** L'écran de sélection de langue reste visible et se superpose aux autres pages après la première utilisation.

**Fix :** La `Phase::LanguageSelection` est marquée comme terminée via un flag persistant (en mémoire pour la session courante, potentiellement sauvegardé dans les préférences locales). Le rendu de cet écran est conditionné à ce flag.

### 4.5 Page d'erreur — empilement vertical des messages

Le `RenderModel` pour la page d'erreur est modifié pour empiler les messages verticalement (offset Y cumulatif) :
- Cadre **rouge** pour les erreurs
- Cadre **orange** pour les warnings

Espacement fixe entre chaque message. Pas de superposition.

---

## Fichiers principalement impactés

| Couche | Fichiers |
|--------|----------|
| Serveur | `engine/server/ServerRegistry.cpp/.h` (nouveau), `engine/core/Log.cpp`, `engine/server/main.cpp` (ou point d'entrée), `engine/network/ServerListPayloads.cpp/.h` (nouveau), `db/migrations/0017_game_servers.sql` (nouveau) |
| Layout/Rendu | `engine/render/AuthUiRenderer.cpp/.h`, `engine/render/AuthGlyphPass.cpp/.h`, `engine/render/AuthLogoPass.cpp/.h`, `engine/client/AuthUi.h` (RenderModel) |
| Événements | `engine/client/AuthUi.cpp` (Update()), `engine/client/AuthUiPresenter.cpp` |
| Textes/États | Fichiers de localisation, `engine/client/AuthUiPresenter.cpp`, `engine/client/AuthUi.cpp` |

---

## Hors scope Phase 1 (reporté Phase 2)

- Menu options structuré (audio, luminosité, contrôles)
- Volume musique indépendant
- Gestion de la luminosité
- Effets sonores sur clic de bouton
- Page de sélection de serveur (présentation, personnages, suppression, création)
- Validation nom de personnage unique + noms interdits

---

## Critères de succès Phase 1

1. Le master s'enregistre en DB au démarrage et la liste des serveurs est non vide après auth
2. Aucune superposition de texte sur aucun écran
3. Tous les boutons répondent au clic souris
4. Les sélecteurs de date sont utilisables à la souris
5. Le logo "i" s'affiche et ouvre un popup avec fond opaque
6. Le rectangle blanc au lancement a disparu
7. Les logs serveur sont horodatés au format `[JJ/MM/AAAA][HH:MM:SS]`
8. La bannière de démarrage s'affiche correctement
9. L'écran de sélection de langue ne se superpose plus après la première utilisation
10. Les messages d'erreur sont empilés verticalement sans superposition
