# World Editor + zone_builder — backlog



Index **chronologique** des tickets (ordre d’**exécution** pour la phase implémentation). Chaque fichier `.md` est un ticket autonome avec chaînage explicite (sections *Chaînement* en tête de fichier).



## Chaîne d’exécution (002 → 007)



1. **001** — Livré : `docs/world_editor_zone_pipeline.md` + `001_*.md` (renvoi).

2. **002** — **Livré** : JSON édition symétrique (`textures` / `objects`, version).

3. **003** — **Livré** : `terrain_world_size_m` + override `TerrainRenderer` (WE), doc §4.1.

4. **004** — **Livré** : bundle `zones/<id>/` + `exported_textures/` + manifeste v2 + doc inventaire §2.3.

5. **005** — **Livré** : `zone_builder` chemins (`zones/` → content), validations, `--help`, layout minimal.

6. **006** — **Livré** : `layout_from_editor.json` à l’export + scripts `tools/world/` + doc §5.1.

7. **007** — **Livré** : zone `demo_plains`, checklist, README World / build.



**Branche recommandée :** `feature/world-00X-<slug-court>` (un ticket = une PR, voir `tickets/AGENTS.md`).



| Ordre | Fichier | Thème |

|------:|---------|--------|

| 1 | `001_spec_pipeline_et_contraintes_monde.md` | **Livré** — `docs/world_editor_zone_pipeline.md`. |

| 2 | `002_world_map_edit_json_champs_et_parsing.md` | **Livré** — parsing `textures`/`objects`, version max, doc §4. |

| 3 | `003_heightmap_resolution_vs_zone_10km.md` | **Livré** — override taille monde WE + doc. |

| 4 | `004_export_runtime_bundle_et_zone_meta.md` | **Livré** — export textures + manifeste v2, `zone.meta` header seul. |

| 5 | `005_zone_builder_layout_sorties_et_chemins.md` | **Livré** — CLI, résolution `--output`, exemple `_templates`. |

| 6 | `006_pont_world_editor_vers_zone_builder.md` | **Livré** — pont export WE → `zone_builder`. |

| 7 | `007_zone_demo_validation_et_documentation.md` | **Livré** — `demo_plains` + `docs/world_zone_demo_checklist.md`. |



## Références



- Spec pipeline : **`docs/world_editor_zone_pipeline.md`**

- Terrain rendu : `docs/terrain_et_world_editor.md`

- Code : `engine/editor/*`, `tools/zone_builder/*`, `tools/world/*`, `engine/world/WorldModel.h`


