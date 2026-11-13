# Documentation du monde (`docs/world/`)

Point d'entrée de la documentation du monde de LCDLLN : le continent des Royaumes, la seconde
carte des Enfers, le lore tiré des albums, l'éditeur de lore et ses outils.

## Par où commencer ?

| Je veux… | Lire |
|---|---|
| comprendre comment le monde jouable est découpé, généré et servi (cellules, serveur, client, générateur) | [`ROYAUMES.md`](ROYAUMES.md) §0 à §10 |
| connaître l'état du monde au v1.0.108 (régions, niveaux, peuples, cases jouables, routes, secrets) | [`ROYAUMES.md`](ROYAUMES.md) §11 |
| travailler sur la carte des Enfers | [`ENFERS.md`](ENFERS.md) (synthèse §7) |
| modifier le monde dans l'éditeur (villes, lieux, routes, quêtes…) | [`EDITEUR_LORE.md`](EDITEUR_LORE.md) |
| savoir quel script lancer et ce que vérifie la CI | [`OUTILS_LORE.md`](OUTILS_LORE.md) |
| retrouver pourquoi une décision a été prise, avec ses sources BD | [`LORE_BD.md`](LORE_BD.md) (table des matières du §16) |
| voir ce que la branche carte v1.0.72 → v1.0.108 a changé, son impact et les points à valider | [`CHANGELOG_CARTE_v1.0.72-v1.0.108.md`](CHANGELOG_CARTE_v1.0.72-v1.0.108.md) |

## Index des documents

| Document | Contenu |
|---|---|
| [`ROYAUMES.md`](ROYAUMES.md) | Monde continu en cellules de 10 km : convention des cellules, serveur (régions actives, mur invisible, départs), client (streaming, repère de rendu), génération (`tools/world_gen`, sorties, pipeline), départs par faction, limites connues ; §11 : contenu du monde au v1.0.108. |
| [`ENFERS.md`](ENFERS.md) | Seconde carte « Les Enfers » : 9 cellules, Citadelle de la Légion, portails appariés, hostilité envers les humains, hiérarchie de Lucifer, lieux, plans, palette du générateur. |
| [`LORE_BD.md`](LORE_BD.md) | Confrontation du lore avec les albums, tome par tome ; règles canon (§0) ; étiquettes (§1) ; départs par faction (§14-15) ; **§16 : les 36 lots du propriétaire** (v1.0.72 → v1.0.107), avec motifs, chiffres, preuves et points incertains ; §17 : questions ouvertes. |
| [`EDITEUR_LORE.md`](EDITEUR_LORE.md) | Guide de l'éditeur `lune-noire-editor.html` : ouverture, export sans perte, modes Déplacer et Tracer, calques (dont secrets), formulaires, suppression, raccourcis, flux éditeur → scripts → CI → jeu, tests Playwright. |
| [`OUTILS_LORE.md`](OUTILS_LORE.md) | Scripts de `tools/lore/` (rôle, entrées / sorties, options, quand les lancer), règles de format du JSON, workflow CI « Lore — services des villes », contrôles des builds. |
| [`CHANGELOG_CARTE_v1.0.72-v1.0.108.md`](CHANGELOG_CARTE_v1.0.72-v1.0.108.md) | Synthèse de la branche : tableau version → commit → résumé → impact jeu → points à valider ; liste consolidée des points ouverts ; description de PR proposée. |
| [`img/`](img/) | Captures de l'éditeur utilisées par `EDITEUR_LORE.md`. |

## Fichiers de données et outils concernés

| Chemin | Rôle |
|---|---|
| `legacy/world-builder-html/lune-noire-data.json` (+ copie `-v20`) | Lore : source de vérité du monde. |
| `legacy/world-builder-html/lune-noire-editor.html` (+ copie `-v20`) | Éditeur de lore. |
| `game/data/world/royaumes/world_master.json` | Master du continent, lu par le générateur. |
| `game/data/world/enfers/world_master.json` | Master de la carte des Enfers. |
| `tools/lore/` | Scripts de services, de recopie lore → master, de contrôle, tests de l'éditeur. |
| `tools/world_gen/` | Générateur du monde (`lcdlln_world_gen`) et ses tests (`world_gen_tests`). |
| `.github/workflows/lore-city-services.yml` | Contrôles du lore en CI. |

## Conventions

- Documentation en français ; les décisions du propriétaire sont notées `[DÉCISION] (v1.0.xx)` sur
  les fiches et inscrites au registre `regle_canon_registre_decisions` ; `[GD]` = ajout de game design
  à valider ; `[INFÉRÉ]` = déduction de conception (voir [`LORE_BD.md`](LORE_BD.md) §0 et §1).
- Chaque changement du monde se termine par la mention de déploiement de `CLAUDE.md` : un
  changement du master ou du générateur demande client, master et shard **en lock-step** avec le
  monde régénéré par la CI.
- Les sections signalées **« secret joueurs »** (grottes des dragons, entrées ophidiennes, gouffre
  caché) sont de la conception interne : ne pas les recopier dans un document destiné aux joueurs.
