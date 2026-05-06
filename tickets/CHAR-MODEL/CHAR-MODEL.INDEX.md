# CHAR-MODEL — Index des tickets

Famille de tickets : **modèles de personnages et d'animaux, animation
squelettique, viewer 3D de création, vie indépendante** — partie client
`lcdlln.exe`.

Convention : `CHAR-MODEL.<numéro>` ; un ticket = une branche = un PR.
Mêmes règles que `AUTH-UI.*` :
- fichiers complets (jamais de patch),
- `CMakeLists.txt` à jour à chaque ticket,
- validation de profondeur d'include,
- commentaires en français, identifiants C++ en anglais.

---

## Vue d'ensemble

| Ticket | Titre court | Phase | Type |
|---|---|---|---|
| CHAR-MODEL.1  | Format `.skinmesh` + builder offline | 0 | Format + outil |
| CHAR-MODEL.2  | Squelette runtime + format `.skel` + builder | 0 | Format + outil |
| CHAR-MODEL.3  | GPU skinning (LBS) + shader vertex skin | 0 | Render |
| CHAR-MODEL.4  | Extension `AssetRegistry` (skinned) | 0 | Asset |
| CHAR-MODEL.5  | Pipeline rendu skinné dans deferred | 0 | Render |
| CHAR-MODEL.6  | Format clip `.anim` + builder | 1 | Format + outil |
| CHAR-MODEL.7  | Animation sampler | 1 | Runtime |
| CHAR-MODEL.8  | Animation blender (cross-fade) | 1 | Runtime |
| CHAR-MODEL.9  | State Machine locomotion | 1 | Gameplay |
| CHAR-MODEL.10 | State Machine actions | 1 | Gameplay |
| CHAR-MODEL.11 | State Machine combat + couches additives | 1 | Gameplay |
| CHAR-MODEL.12 | Sockets / attachements d'os | 1 | Runtime |
| CHAR-MODEL.13 | IK pied | 1 | Gameplay |
| CHAR-MODEL.14 | Rig humanoïde `humanoid_v1.skel` + clips placeholder | 2 | Asset |
| CHAR-MODEL.15 | Modèle Humain | 2 | Asset |
| CHAR-MODEL.16 | Modèle Elfe | 2 | Asset |
| CHAR-MODEL.17 | Modèle Nain | 2 | Asset |
| CHAR-MODEL.18 | Modèle Orc | 2 | Asset |
| CHAR-MODEL.19 | Modèle Démon (+ variante ailé) | 2 | Asset |
| CHAR-MODEL.20 | Modèle Chevalier-Dragon | 2 | Asset |
| CHAR-MODEL.21 | Modèle Orkh | 2 | Asset |
| CHAR-MODEL.22 | Modèle Gobelin | 2 | Asset |
| CHAR-MODEL.23 | Code couleur de race (`MaterialOverride`) | 2 | Render |
| CHAR-MODEL.24 | Viewer 3D off-screen `CharacterCreationUi` | 3 | UI |
| CHAR-MODEL.25 | Customisation morphologique (sliders) | 3 | UI |
| CHAR-MODEL.26 | Câblage `CharacterController → Animation` | 3 | Gameplay |
| CHAR-MODEL.27 | Rig quadrupède générique + clips | 4 | Asset |
| CHAR-MODEL.28 | FSM IA d'animal | 4 | Gameplay |
| CHAR-MODEL.29 | Cheval (montable) | 4 | Asset |
| CHAR-MODEL.30 | Système de monture | 4 | Gameplay |
| CHAR-MODEL.31 | Vache, cochon, chèvre, lapin | 4 | Asset |
| CHAR-MODEL.32 | Loup, ours | 4 | Asset |
| CHAR-MODEL.33 | Poule, coq, oiseau (rig aviaire) | 4 | Asset |
| CHAR-MODEL.34 | Serpent (rig spline) | 4 | Asset |
| CHAR-MODEL.35 | Poisson | 4 | Asset |
| CHAR-MODEL.36 | Dragons (3 tailles) | 4 | Asset |
| CHAR-MODEL.37 | Tick AI global / `AmbientLifeSystem` | 5 | Gameplay |

---

## Matrice de dépendances

```
Phase 0  : 1 → 2 → 3 → 4 → 5
Phase 1  : 6 → 7 → 8 → {9, 10, 11} ; 12 ; 13
Phase 2  : 14 → {15..22} → 23
Phase 3  : 24 → 25 ; 26
Phase 4  : 27 → 28 → 29 → 30 ; {31..36}
Phase 5  : 37
```

**Règle d'or** : un ticket ne référence que des dépendances *déjà livrées*.

---

## Stratégie

Chaque ticket contient :

1. **Cadrage** (1 paragraphe).
2. **Pré-requis vérifiables** (commandes git/ls).
3. **Spécification technique exhaustive** (structures, signatures, formats binaires, conventions de nommage).
4. **Liste exacte des fichiers à créer / modifier**.
5. **Mise à jour `CMakeLists.txt`** (lignes à ajouter, contraintes : pas de lien `engine_core` côté serveur, pattern `zone_builder` pour les outils offline).
6. **Critères d'acceptation** (build OK Windows + Linux, tests, comportement visuel attendu).
7. **Anti-objectifs** (ce que le ticket ne doit *pas* faire — typiquement : ne pas casser `AssetRegistry` pour les meshes statiques existants).

Tickets monstres (rig humanoïde, dragons) déjà découpés par phase.
