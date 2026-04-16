# 004 — Export runtime bundle (`zones/<id>/`) & contenu `zone.meta`

**Statut : livré** — copie `textureAssets` → `exported_textures/`, manifeste v2, doc §2.3 inventaire ; `zone.meta` reste header seul (pas de bump version).



## Chaînement



| Précédent | Ce ticket | Suivant |

|-----------|-----------|---------|

| **002** (listes + champs JSON), **003** (option terrain) | Dossier runtime complet et documenté | **005** (même arborescence côté builder), **006** (pont) |



Le **006** suppose que l’on sait **quels fichiers** doivent vivre sous `zones/<id>/` après export + builder.



---



## Objectif



Clarifier et compléter **`ExportRuntimeBundle`** pour que `game/data/zones/<zone_id>/` corresponde au **contrat** documenté dans `docs/world_editor_zone_pipeline.md`, et lister l’écart avec ce que le **streaming** consommera une fois les chunks présents.



## Inventaire (à tenir à jour dans la doc lors de l’implémentation)



| Artefact | Aujourd’hui (export WE) | Souhaité / remarque |

|----------|-------------------------|---------------------|

| `terrain_height.r16h` | ✅ copie | — |

| `zone.meta` | ✅ header seul | Vérifier lecteurs : champs additionnels = bump version si binaire étendu |

| `runtime_manifest.json` | ✅ v2 (`texture_assets`, `exported_textures`, `texture_assets_source_missing`, `object_prefab_ids`, terrain) | Splat/hole si le doc les porte (futur) |

| `textures/*`, `audio/*` | Import WE vers content ; export recopie les entrées **`textures`** du JSON vers `zones/<id>/exported_textures/` + listes dans le manifeste | Audio : pas de copie zone (hors périmètre MVP) |

| `chunks/chunk_i_j/*` | ❌ | **006** + **005** |

| `layout_from_editor.json` | ✅ stub vide (WE) | Remplaçable par un layout riche avant **006** |



## Livrables



1. Mettre à jour **`docs/world_editor_zone_pipeline.md`** §2.3 / §5 avec le tableau « présent vs futur » validé par l’équipe.

2. Code : au choix dans la même PR ou ticket suivant —

   - copier les fichiers listés dans `textureAssets` (chemins relatifs content) vers un sous-dossier `zones/<id>/exported_textures/` **ou** documenter que ce n’est pas fait ;

   - idem **splat / hole** si champs ajoutés au document (**002**).

3. Si extension `zone.meta` : suivre `OutputVersionHeader` / `kZoneMetaVersion` — **ne pas** casser les lecteurs existants sans bump de version.



## Critères d’acceptation (DoD)



- [x] Tree attendu après export **écrit** dans la doc + une commande `tree` / liste dans le rapport PR.

- [x] Export sur zone de test : pas d’erreur si lists vides ; pas d’erreur si lists pleines post-**002** (sources manquantes → warn + entrée `texture_assets_source_missing`).

- [x] Build World Editor + export manuel documenté.



## Fichiers concernés



- `engine/editor/WorldMapIo.cpp` (`ExportRuntimeBundle`)

- `engine/world/OutputVersion.h` / lecteurs `zone.meta` s’il y en a côté client

- `docs/world_editor_zone_pipeline.md`



## Dépendances



- **001** ✅ ; **002** (fortement recommandé avant copie de textures) ; **003** optionnel (manifeste terrain).


