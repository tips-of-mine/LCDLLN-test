# AUTH-UI.11 — Écran Création de personnage · CharacterCreate (split uniquement)

## Dépendances
- AUTH-UI.1 (socle commun)
- AUTH-UI.10 (Terms — CharacterCreate est atteint après acceptation des CGU ou login sur compte sans personnage)

## Objectif

**Split uniquement** : déplacer dans `AuthScreenCharacterCreate.cpp` et `AuthImGuiCharacterCreate.cpp` les méthodes relatives à `Phase::CharacterCreate`.

Pas de maquette HTML disponible dans `auth_flow/`. La maquette complète de création de personnage est attendue dans un kit dédié (probablement `ui_kits/character_creation/`). Le rendu ImGui STAB.13 est conservé.

---

## Périmètre fonctionnel (Phase::CharacterCreate)

`Phase::CharacterCreate` est activée lors de la première connexion d'un compte (aucun personnage créé sur le shard). L'utilisateur saisit un nom puis valide.

### Méthodes presenter → `engine/client/auth/screens/AuthScreenCharacterCreate.cpp`

| Méthode |
|---|
| `StartCharacterCreateWorker()` |
| `ImGuiSubmitCharacterCreate()` |
| `ImGuiCancelCharacterCreateReturnToLogin()` |
| `BuildModel_CharacterCreate()` **(nouvelle méthode privée)** |
| `Update_CharacterCreate()` **(nouvelle méthode privée)** |

### Renderer → `engine/render/auth/screens/AuthImGuiCharacterCreate.cpp`

Extraction de la logique ImGui CharacterCreate depuis `AuthUiRenderer.cpp` vers ce fichier dédié.

---

## Rendu ImGui (état existant STAB.13, non modifié)

L'écran CharacterCreate actuel affiche :
- Panel "Créer votre personnage" (ou libellé i18n équivalent)
- Field "Nom du personnage" (`m_characterName`)
  - Validation : 3–16 caractères, alphanumérique + tirets
- Bouton "Créer" → `ImGuiSubmitCharacterCreate()`
- Bouton "Annuler" → `ImGuiCancelCharacterCreateReturnToLogin()`
- Banner error si nom invalide ou pris

Ce rendu est **conservé tel quel** après le split.

---

## Relation avec M39.1

Le ticket `M39.1_Character_creation_screen_race_class_customization` couvrira la refonte complète de l'écran de création (choix de race, classe, personnalisation visuelle, etc.).

`AuthScreenCharacterCreate.cpp` extrait ici est le point d'entrée minimal (nom uniquement) qui sera **remplacé ou étendu** par M39.1. Le split réalisé ici facilite cette future substitution : M39.1 n'a qu'à modifier `AuthScreenCharacterCreate.cpp` et `AuthImGuiCharacterCreate.cpp` sans toucher au reste du flux auth.

---

## Livrables

**Créés / Complétés :**
- `engine/client/auth/screens/AuthScreenCharacterCreate.cpp`
- `engine/render/auth/screens/AuthImGuiCharacterCreate.cpp`

**Modifiés :**
- `engine/client/AuthUi.h` — `BuildModel_CharacterCreate()`, `Update_CharacterCreate()`
- `engine/client/auth/AuthUiPresenterCore.cpp` — dispatch

---

## Definition of Done

- [ ] Build Windows OK
- [ ] AUTH-UI.11 est le **dernier ticket de la série** : à ce stade, `AuthUi.cpp` et `AuthUiRenderer.cpp` originaux **n'existent plus** et tous les stubs créés en AUTH-UI.1 sont implémentés
- [ ] Vérification qu'aucun stub `/* AUTH-UI.N */` vide ne subsiste dans le codebase
- [ ] Écran CharacterCreate s'affiche identiquement à l'état STAB.13
- [ ] Validation du nom : longueur et format
- [ ] "Créer" → worker → transition vers ShardPick (ou world entry)
- [ ] "Annuler" → retour Login
- [ ] Aucune régression sur l'ensemble du flux auth
- [ ] Rapport final incluant une confirmation que **zéro stub vide** subsiste
