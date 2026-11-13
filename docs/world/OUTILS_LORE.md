# Outils du lore (`tools/lore/`) et contrôles de la CI

> Pour qui : un contributeur qui modifie le fichier de lore (par l'éditeur ou à la main) et doit
> savoir quel script lancer, quand, et ce que la CI vérifie.
> État décrit : lore **v1.0.108**. Index : [`README.md`](README.md). Guide de l'éditeur :
> [`EDITEUR_LORE.md`](EDITEUR_LORE.md).

Tous les scripts sont en **Python 3 standard** (aucune dépendance), sauf les tests de l'éditeur
(`test_editor_*.py`, Playwright). Ils se lancent **depuis la racine du dépôt**. Sans Python sur
le poste : conteneur ponctuel, par exemple
`docker run --rm -v "${PWD}:/w" -w /w python:3.12 python tools/lore/<script>.py --check`.

## Sommaire

1. [Règles de format du JSON de lore](#1-règles-de-format-du-json-de-lore)
2. [Les scripts, un par un](#2-les-scripts-un-par-un)
3. [Quand lancer quoi](#3-quand-lancer-quoi)
4. [Le workflow CI « Lore — services des villes »](#4-le-workflow-ci--lore--services-des-villes-)
5. [Ce que vérifient les builds (générateur du monde)](#5-ce-que-vérifient-les-builds-générateur-du-monde)
6. [Scripts reproductibles hors dépôt](#6-scripts-reproductibles-hors-dépôt)

## 1. Règles de format du JSON de lore

| Règle | Détail |
|---|---|
| Deux copies identiques | `legacy/world-builder-html/lune-noire-data.json` et `lune-noire-data-v20.json` doivent être **identiques octet pour octet** (`cmp`) ; `validate_city_services.py` échoue sinon. |
| Sérialisation | exactement `json.dumps(data, indent=2, ensure_ascii=False)` de Python : indentation 2 espaces, UTF-8, caractères accentués **non échappés**, ordre des clés conservé, flottants entiers écrits `1.0`. |
| Fin de fichier | **aucun retour à la ligne final**. |
| Écriture | `open(chemin, "w", encoding="utf-8").write(json.dumps(...))` ; puis copier le fichier sur l'autre copie. |
| Éditeur | son export (**⬇ Exporter JSON**) produit exactement ce format ; un export sans modification est identique au fichier chargé. |
| Interdit | reformater avec un autre outil (éditeur de texte qui ajoute `\n` final, `jq`, Prettier…), échapper les accents, trier les clés. |

Lecture / écriture type en Python :

```python
import json
P = ["legacy/world-builder-html/lune-noire-data.json", "legacy/world-builder-html/lune-noire-data-v20.json"]
d = json.load(open(P[0], encoding="utf-8"))
# ... modifications ...
texte = json.dumps(d, indent=2, ensure_ascii=False)
for p in P:
    open(p, "w", encoding="utf-8").write(texte)
```

Conventions de contenu (rappel de [`LORE_BD.md`](LORE_BD.md) §0 et §1) : toute décision du
propriétaire porte une note `[DÉCISION] (v1.0.xx)` sur sa fiche **et** une entrée au registre
(`regles_transverses` → `regle_canon_registre_decisions` → `registre` : `collection`, `id`,
`nom`, `decision`, `version`) ; `meta.version` est incrémentée et une ligne `vX.Y.Z : …` est
ajoutée **en tête** de `meta.notes` ; ajouts sans source BD étiquetés `[GD]`, déductions
`[INFÉRÉ]`. On ne supprime pas d'identifiant (on renomme le `name`, l'ancien nom va dans
`alias`).

Le master `game/data/world/royaumes/world_master.json` **n'est pas** au format `json.dumps` :
il est mis en forme à la main (une ligne par entrée). Les scripts `sync_*` l'éditent donc **de
façon chirurgicale** (lignes ciblées, contrôle que rien d'autre ne change).

## 2. Les scripts, un par un

### `generate_city_services.py` — services et PNJ de service des villes

| | |
|---|---|
| Rôle | Remplit, pour chaque ville du lore, ses **services** (auberge, forge, écurie, temple, port, arène, cimetière…) et **un PNJ par exemplaire de service**, d'après la grille `game_design.cities_services.grille` (taille de ville → [min, max] par service). |
| Entrées | le lore (`villes`, `routes` de type `road` pour les écuries, `grille.ports`, `grille.arenes`, `grille.ajustements`) ; tables du script : `TAILLE_VILLES`, `RACE_VILLES` (ex. Brenhaval, Hazhaëlin, Thurvhal), `RACE_FACTIONS`, `NOMS`, `RENOMMAGES`. |
| Sorties | les deux copies du lore (`villes[].taille`, `services`, `service_npcs`). |
| Options | `--check` : n'écrit rien, code 1 si le fichier n'est pas à jour. |
| Propriétés | déterministe (graine par ville et service), idempotent : un PNJ existant garde nom, genre, race ; seuls les manquants sont créés, les surnuméraires retirés ; noms uniques dans tout le monde, jamais repris des BD. |
| Quand | après création / suppression d'une ville, changement de taille, **ajout ou retrait d'une route terrestre** d'une ville (1 écurie par sortie `road`), changement de la grille. |
| Chiffres v1.0.108 | 122 villes (Thurvhal comprise), **1264 PNJ** de service. |

### `validate_city_services.py` — conformité à la grille

| | |
|---|---|
| Rôle | Vérifie les services et PNJ par rapport à la grille. |
| Contrôles | JSON valide ; **les deux copies identiques** ; taille connue ; nombre de chaque service dans [min, max] (ajustements compris) ; écurie = sorties terrestres (≥ 1) ; port / arène seulement dans les villes listées ; cimetière dans chaque ville habitée ; `services` = services ayant un PNJ ; PNJ : id et nom uniques, métier cohérent, race et genre valides, **aucun mot repris des BD** (noms des fiches), pas plus de PNJ que la population maximale. |
| Options | aucune. Code 1 en cas d'erreur. |
| Quand | après toute modification du lore (c'est aussi le garde-fou des deux copies). |

Conséquence pratique : un nom de lieu `[GD]` qui contient un mot déjà porté par un PNJ de
service (« pierre », « rond », « cuivre », « pic »…) fait échouer la validation : choisir un
autre mot.

### `sync_positions.py` — positions manuelles (lore → master)

| | |
|---|---|
| Rôle | Recopie les positions posées à la main dans l'éditeur (`position_x_m` / `position_z_m` des `villes` et `lieux`) vers `position: { "x_m", "z_m" }` des `cities` et `pois` du master (« tu places, le jeu suit », v1.0.95). |
| Unité | mètres depuis le **centre de la case**, X est, Z sud, \|x\|, \|z\| ≤ 4300 m ; `null` = position automatique (la clé `position` est retirée du master). |
| Correspondance | ville = même `id` ; lieu `lieu_x` = POI `poi_x`, sinon POI de même nom dans la même case, sinon table `EXPLICITES` (portes des Enfers, lieux du T1, temple de Methraton). Lieux d'une autre carte (`carte: enfers`) ignorés. |
| Refus (code 2) | position hors de la case ; ville ancrée sur un relief (`reliefs`) **sauf un gouffre** (il suit sa ville, v1.0.107) ; deux fiches pour la même entrée. |
| Avertissements | position posée sur une fiche sans entrée au master (ignorée). Attendus au v1.0.108 : 2 (lieux secrets du gouffre, sans POI). |
| Options | `--check` : n'écrit rien, code 1 si le master n'est pas à jour. |
| Quand | après tout déplacement dans l'éditeur (mode ✥ Déplacer ou champs Position). |
| Positions posées v1.0.108 | `ville_thurvhal`, 3 Bornes des Géants, Stèle des Deux Fois Nés. |

### `sync_routes.py` — routes du lore (lore → master)

| | |
|---|---|
| Rôle | Réécrit la clé `lore_roads` du master à partir de la collection `routes` du lore (v1.0.105) ; `tools/world_gen` trace ensuite chaque route **case par case**. |
| Format écrit | une ligne par route, ordre du lore : `{ "id", "type": road \| imperial_road \| path, "from", "to", "waypoints" }`. Les `sea_route` **ne sont pas recopiées** (pas de ponton dans le générateur). |
| Extrémités | id du master : ville (même id), POI (`lieu_x` → `poi_x`), table `EXTREMITES` (entrées nord / sud de la Mine de la Traverse, Hameau de Marn), table `LIEUX_VILLES` (lieu du lore qui est une ville du master) ; `""` = jonction (embranchement) ou extrémité sans entrée au master (avertissement). |
| Waypoints | cases du tracé, extrémités comprises ; sans tracé : `[départ, arrivée]`. |
| Refus (code 2) | route sans case de départ ou d'arrivée, case invalide. |
| Options | `--check` (code 1 si pas à jour), `--lore <json>`, `--master <json>` (autres fichiers, pour les tests). |
| Quand | après toute création, modification ou suppression d'une route terrestre dans l'éditeur. |
| Chiffres v1.0.108 | 165 routes recopiées. |

### `test_sync_routes.py` — test de `sync_routes.py`

Python standard (~2 s), sur des **copies** : master du dépôt à jour (`--check`) ; master sans
`lore_roads` → `--check` échoue, une synchronisation l'insère, une seconde ne change rien
(idempotence), résultat identique au master du dépôt ; route retracée suivie, route maritime
ignorée ; extrémités (ville, lieu → POI, jonction, `EXTREMITES`) ; route sans case refusée
(code 2) sans écriture. Lancement : `python3 tools/lore/test_sync_routes.py`.

### `check_references.py` — références orphelines

| | |
|---|---|
| Rôle | Liste les valeurs de champs de référence qui désignent un identifiant **inexistant** (case d'une ville, région d'une case, PNJ d'une étape, butin d'une créature…). |
| Champs contrôlés | table `REFERENCES` du script (~160 champs) ; exclus : champs libres ou d'énumération (`type`, `category`, `tags`, `faction_control`…), registre des décisions. |
| Identifiants connus | `id` des fiches de premier niveau, de `lore`, de `hydrography`, compétences des arbres, noms des collections ; cases des Enfers `enfers:<case>`. Seules les valeurs de forme identifiant sont contrôlées. |
| Sortie | une ligne par orpheline (collection, fiche, champ, valeur) avec une suggestion `difflib` à vérifier, puis le total. |
| Options | `--strict` (code 1 s'il reste une orpheline), `--fichier <json>`. |
| Quand | après une suppression ou un renommage d'identifiant. |
| État v1.0.108 | **89 références (36 identifiants)** à trancher ; le contrôle reste **non bloquant** en CI jusqu'à ce que la liste soit vide (passer alors à `--strict`). |

### Tests de l'éditeur (Playwright)

Installation une fois : `pip install playwright && python3 -m playwright install chromium`.
Tous acceptent `--editeur <html>` et `--donnees <json>` (pour vérifier les copies `-v20`) ;
aucun n'écrit dans le dépôt ; code 0 = tout passe ; **hors CI** (à lancer à la main).

| Test | Durée | Vérifie | Options |
|---|---|---|---|
| `test_editor_roundtrip.py` | ~3 s | chargement sans changement de type ; export sans modification = fichier ; renommage d'une ville par son formulaire : seul `name` change. Module réutilisé par les autres tests (`ouvrir_editeur`, `charger`, `exporter`, `diff_profond`). | — |
| `test_editor_routes.py` | ~10 s | mode Tracer (sentier, points glissés / insérés / supprimés, tracé continu), embranchement, suppression + Ctrl+Z, Brenhaval déplacée (routes, routes maritimes, pontons suivis), pile vidée → export identique. | `--captures` (GIF + PNG) |
| `test_editor_forms.py` | ~4 min (`--rapide` ~1,5 min) | format octet pour octet ; **toutes** les fiches de chaque formulaire enregistrées sans modification ; un champ modifié = seul chemin changé ; création puis suppression ; suppression de la fiche la plus citée ; séquence mixte qui passe les contrôles du lore ; import partiel de villes. | `--rapide` |
| `test_editor_layers.py` | ~15 s | défauts (secrets inactifs), chargement du master, chaque calque et sous-filtre, lieux secrets seulement avec leur calque, niveaux, survol d'une quête, mémorisation (sauf secrets), `localStorage` en panne, performance, export identique. | `--captures` (4 PNG) |
| `test_editor_fiches.py` | ~20 s | `niveau_ref`, `niveau_par_faction` (et départ à 1-10), `etapes_struct`, type `mine_poi`, formulaires de faction (dont `territory.ville_id`) / race / PNJ, suppression avec compte des références, export final identique. | `--captures` (2 PNG) |

## 3. Quand lancer quoi

| J'ai… | Lancer (sans `--check`) | Puis vérifier |
|---|---|---|
| modifié une fiche quelconque | — | `validate_city_services.py`, `generate_city_services.py --check` |
| créé / supprimé une ville, changé sa taille | `generate_city_services.py` | `validate_city_services.py` |
| déplacé une ville ou un lieu | `sync_positions.py` | `sync_positions.py --check` |
| créé / modifié / supprimé une route terrestre | `sync_routes.py` puis `generate_city_services.py` (écuries) | `sync_routes.py --check`, `test_sync_routes.py`, `validate_city_services.py` |
| supprimé ou renommé un identifiant | — | `check_references.py` (ne pas augmenter le nombre d'orphelines) |
| modifié l'éditeur HTML | recopier sur `-v20.html` | les 5 tests Playwright, sur les deux copies |
| modifié le master à la main (cases ouvertes, reliefs…) | — | `sync_positions.py --check`, `sync_routes.py --check`, build + `world_gen_tests` |

## 4. Le workflow CI « Lore — services des villes »

Fichier : `.github/workflows/lore-city-services.yml`. Déclenché sur `push` et `pull_request`
vers `develop` et `main` quand changent `legacy/world-builder-html/lune-noire-data*.json`,
`tools/lore/**`, `game/data/world/royaumes/world_master.json` ou le workflow lui-même. Python
3.12 standard, ~10 s, indépendant des builds.

| Étape | Commande | Bloquante |
|---|---|---|
| Conformité à la grille | `python tools/lore/validate_city_services.py` | oui |
| Fichier à jour par rapport au générateur | `python tools/lore/generate_city_services.py --check` | oui |
| Positions manuelles recopiées au master | `python tools/lore/sync_positions.py --check` | oui |
| Routes du lore recopiées au master | `python tools/lore/sync_routes.py --check` | oui |
| Test du script de recopie des routes | `python tools/lore/test_sync_routes.py` | oui |
| Références orphelines | `python tools/lore/check_references.py` | **non** (rapport) |

La CI **ne réécrit rien** : un `--check` en échec signifie qu'il fallait lancer le script sans
`--check` et committer son résultat.

## 5. Ce que vérifient les builds (générateur du monde)

- `.github/workflows/build-linux.yml` (étape « Générer les mondes royaumes et enfers ») et
  `.github/workflows/build-windows.yml` (`Invoke-WorldGen`) compilent `lcdlln_world_gen` et
  **génèrent** les mondes `royaumes` puis `enfers` avant l'empaquetage
  (paquet client Windows, bundle Docker du master et du shard). Échec si `start_locations.json`
  ou les `portals.json` manquent.
- `world_gen_tests` (`tools/world_gen/tests/WorldGenTests.cpp`, lancé par `ctest` du build Linux ;
  Windows ne lance pas `ctest`) contient un test par lot de la
  branche (« île v1.0.74 », « lieux v1.0.81 »… « routes du lore v1.0.105 », « routes v1.0.106 »,
  « gouffre v1.0.107 ») : cases ouvertes terrestres et reliées, lieux au sec, routes passant par
  leurs cases, déterminisme, monde inchangé hors des zones touchées.
- Le générateur valide strictement le master (`LoadWorldMaster`) : `position` hors ±4300 m,
  ville de relief déplacée, ville de départ secrète, `lore_roads` incohérent → échec.

Génération locale (Windows) : voir [`ROYAUMES.md`](ROYAUMES.md) §4 (`--cells` pour quelques
cellules, `--dry-run`).

## 6. Scripts reproductibles hors dépôt

Chaque lot de la branche (v1.0.72 à v1.0.107) a été appliqué par des scripts ponctuels
(`master_v10xx.py`, `lore_v10xx.py`, `data_*.py`, `quests_*.py`, `select_*.py`, `pick_*.py`…)
**qui ne sont pas versionnés** : ils sont cités dans chaque section de [`LORE_BD.md`](LORE_BD.md)
§16 pour la traçabilité. Seuls les outils réutilisables listés ci-dessus sont dans
`tools/lore/`. Pour un nouveau lot, préférer l'éditeur ; à défaut, un script ponctuel qui
respecte les règles du [§1](#1-règles-de-format-du-json-de-lore) puis les contrôles du [§4](#4-le-workflow-ci--lore--services-des-villes-).
