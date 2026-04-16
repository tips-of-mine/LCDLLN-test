# World Editor + zone_builder — backlog



Index **chronologique** des tickets (ordre d’**exécution** pour la phase implémentation). Chaque fichier `.md` est un ticket autonome avec chaînage explicite (sections *Chaînement* en tête de fichier).



## Chaîne d’exécution (002 → 013)



1. **001** — Livré : `docs/world_editor_zone_pipeline.md` + `001_*.md` (renvoi).

2. **002** — **Livré** : JSON édition symétrique (`textures` / `objects`, version).

3. **003** — **Livré** : `terrain_world_size_m` + override `TerrainRenderer` (WE), doc §4.1.

4. **004** — **Livré** : bundle `zones/<id>/` + `exported_textures/` + manifeste v2 + doc inventaire §2.3.

5. **005** — **Livré** : `zone_builder` chemins (`zones/` → content), validations, `--help`, layout minimal.

6. **006** — **Livré** : `layout_from_editor.json` à l’export + scripts `tools/world/` + doc §5.1.

7. **007** — **Livré** : zone `demo_plains`, checklist, README World / build.

8. **008** — **Livré** : peinture splat (herbe / sable / roc…), persistance SLAP + JSON, export bundle (`terrain_splat.slap`).

9. **009** — **Livré** : placement d’instances (catalogue MVP 2 glTF), snap sol, JSON + export `layout_from_editor`.

10. **010** — **Livré** : herbe / détail surface (masque GRMS + rendu + WE) ; **révision ticket** : tailles (plafond **1 m**), paliers **densité** / dissimulation accroupi.

11. **011** — **Livré** : routes branche A (splat polyline + JSON `routes`) ; branche B spline mesh = extension.

12. **012** — **Livré** : checklist client + `demo_plains` (SLAP/GRMS) + logs boot ; streaming décor = doc zone_builder.

13. **013** — **Livré** : arbres dans le WE — catalogue `world_editor/tree_species_catalog.json`, **espèces** / **formes** / **tailles** (clamp + aléatoire), JSON d’édition `species_id` + `shape_variant`.

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

| 8 | `008_world_editor_peinture_splat_persist_export.md` | **Livré** — splat auteur, sauvegarde, export. |

| 9 | `009_world_editor_instances_arbres_rochers.md` | **Livré** — instances + layout export. |

| 10 | `010_world_editor_herbe_detail_surface.md` | **Livré** — masque GRMS + rendu ; extension doc taille ≤ 1 m, densités. |

| 11 | `011_world_editor_routes_couches_ou_splines.md` | **Livré** — routes splat polyline (011 A). |

| 12 | `012_monde_complet_validation_client_streaming.md` | **Livré** — checklist §5 + démo + logs terrain. |

| 13 | `013_world_editor_arbres_especes_taille_forme.md` | **Livré** — catalogue arbres (espèces, taille, formes). |



## Références



- Spec pipeline : **`docs/world_editor_zone_pipeline.md`**

- Terrain rendu : `docs/terrain_et_world_editor.md`

- Code : `engine/editor/*`, `tools/zone_builder/*`, `tools/world/*`, `engine/world/WorldModel.h`


