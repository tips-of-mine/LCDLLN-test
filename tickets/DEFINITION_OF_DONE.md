# DEFINITION_OF_DONE.md (DoD)

Cette checklist s’applique à tous les tickets.

## A) Build & exécution
- [ ] Le projet configure (CMake) sans erreur
- [ ] Le projet compile sans erreur
- [ ] L’exécutable se lance (si applicable) et ne crash pas immédiatement
- [ ] Les commandes exécutées sont listées dans le rapport final

## B) Structure du repo (anti-dérive)
- [ ] Aucun nouveau dossier racine non autorisé n’a été créé (ex: /assets, /textures, /data)
- [ ] Le code moteur est uniquement sous `/engine`
- [ ] Le contenu (textures/meshes/json/audio…) est uniquement sous `/game/data`
- [ ] Les outils offline sont uniquement sous `/tools`

## C) Portée du ticket
- [ ] Aucune fonctionnalité hors scope du ticket
- [ ] Aucun refactor global non demandé
- [ ] Les fichiers modifiés sont cohérents avec la section “Livrables” du ticket

## D) Chemins & contenu
- [ ] Aucun chemin absolu hardcodé
- [ ] Tout chemin de contenu passe par `paths.content` (config.json) + chemin relatif

## E) Qualité minimale
- [ ] Code lisible, cohérent, sans TODO gratuit
- [ ] Logs/Assertions cohérents
- [ ] Pas d’erreurs validation Vulkan (si concerné)

## F) Rapport final obligatoire
- [ ] Fichiers modifiés/créés/supprimés listés
- [ ] Commandes exécutées listées
- [ ] Résultats (OK/KO) clairement indiqués
