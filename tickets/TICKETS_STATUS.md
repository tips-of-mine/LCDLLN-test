# Suivi des tickets — etat au 2026-06-03

Genere automatiquement apres analyse de code approfondie (reorganisation tickets/), avec une passe de correction des faux-negatifs lies a l'ancien chemin engine/ (code reel sous src/).
Convention : **DONE** = implemente, cloture par une Issue dans `tickets/issues/` (le .md du ticket a ete retire du dossier milestone). **PARTIAL** = partiellement implemente, ticket conserve avec encart d'etat. **TODO** = a faire, ticket conserve.

## Synthese

| Statut | Nombre |
|--------|-------:|
| DONE (clotures) | 335 |
| PARTIAL | 33 |
| TODO | 45 |
| **Total** | 413 |

### Par milestone

| Milestone | DONE | PARTIAL | TODO | Total |
|-----------|-----:|--------:|-----:|------:|
| M00 | 7 | 0 | 0 | 7 |
| M01 | 6 | 0 | 0 | 6 |
| M02 | 6 | 0 | 0 | 6 |
| M03 | 5 | 0 | 0 | 5 |
| M04 | 3 | 0 | 0 | 3 |
| M05 | 3 | 0 | 1 | 4 |
| M06 | 4 | 0 | 0 | 4 |
| M07 | 4 | 0 | 0 | 4 |
| M08 | 6 | 0 | 0 | 6 |
| M09 | 8 | 0 | 3 | 11 |
| M10 | 5 | 0 | 0 | 5 |
| M11 | 3 | 2 | 0 | 5 |
| M12 | 4 | 0 | 0 | 4 |
| M13 | 4 | 0 | 0 | 4 |
| M14 | 4 | 0 | 0 | 4 |
| M15 | 3 | 0 | 1 | 4 |
| M16 | 5 | 0 | 0 | 5 |
| M17 | 4 | 0 | 0 | 4 |
| M18 | 5 | 0 | 0 | 5 |
| M19 | 11 | 0 | 0 | 11 |
| M20 | 10 | 0 | 0 | 10 |
| M21 | 9 | 0 | 0 | 9 |
| M22 | 8 | 0 | 0 | 8 |
| M23 | 7 | 0 | 0 | 7 |
| M24 | 6 | 1 | 0 | 7 |
| M25 | 8 | 0 | 0 | 8 |
| M26 | 3 | 0 | 0 | 3 |
| M27 | 2 | 0 | 0 | 2 |
| M28 | 4 | 0 | 0 | 4 |
| M29 | 3 | 0 | 0 | 3 |
| M30 | 2 | 1 | 0 | 3 |
| M31 | 2 | 0 | 0 | 2 |
| M32 | 4 | 0 | 0 | 4 |
| M33 | 2 | 1 | 0 | 3 |
| M34 | 4 | 0 | 0 | 4 |
| M35 | 4 | 0 | 0 | 4 |
| M36 | 3 | 0 | 0 | 3 |
| M37 | 3 | 0 | 0 | 3 |
| M38 | 3 | 0 | 0 | 3 |
| M39 | 4 | 0 | 0 | 4 |
| M40 | 2 | 1 | 0 | 3 |
| M41 | 2 | 0 | 0 | 2 |
| M42 | 1 | 1 | 0 | 2 |
| M43 | 1 | 1 | 6 | 8 |
| M44 | 4 | 0 | 0 | 4 |
| M45 | 8 | 0 | 0 | 8 |
| M100 | 42 | 0 | 9 | 51 |
| M101 | 8 | 1 | 2 | 11 |
| AUTH-UI | 12 | 0 | 0 | 12 |
| CHAR-MODEL | 3 | 12 | 22 | 37 |
| SERVER-CORE | 32 | 11 | 1 | 44 |
| STAB | 13 | 1 | 0 | 14 |
| docs | 3 | 0 | 0 | 3 |
| world | 13 | 0 | 0 | 13 |

## Detail par milestone

### M00

| Ticket | Statut | Detail |
|--------|--------|--------|
| M00.1 | DONE | cloture (issue existante) |
| M00.1.1 | DONE | cloture (issue existante) |
| M00.1.2 | DONE | cloture (issue existante) |
| M00.2 | DONE | Memory allocateur arenas tags |
| M00.3 | DONE | Job System thread pool |
| M00.4 | DONE | Platform window/input/fs |
| M00.5 | DONE | Game loop double-buffer |

### M01

| Ticket | Statut | Detail |
|--------|--------|--------|
| M01.1 | DONE | Vulkan instance/validation/surface |
| M01.2 | DONE | Device selection + queues |
| M01.3 | DONE | Swapchain/renderpass/framebuffers |
| M01.4 | DONE | Command pools/buffers/sync 2 frames |
| M01.5 | DONE | Shader hot reload |
| M01.5b | DONE | cloture (issue existante) |

### M02

| Ticket | Statut | Detail |
|--------|--------|--------|
| M02.1 | DONE | Frame Graph API |
| M02.2 | DONE | Frame Graph compilation deps |
| M02.3 | DONE | Frame Graph barrieres Vulkan |
| M02.3b | DONE | cloture (issue existante) |
| M02.4 | DONE | Offscreen SceneColor blit |
| M02.5 | DONE | Asset system mesh/texture |

### M03

| Ticket | Statut | Detail |
|--------|--------|--------|
| M03.0 | DONE | cloture (issue existante) |
| M03.1 | DONE | cloture (issue existante) |
| M03.2 | DONE | Deferred lighting PBR GGX |
| M03.3 | DONE | Materials BaseColor/Normal/ORM |
| M03.4 | DONE | Tonemap HDR->LDR filmic |

### M04

| Ticket | Statut | Detail |
|--------|--------|--------|
| M04.1 | DONE | CSM splits/matrices stabilization |
| M04.2 | DONE | Shadow pass depth-only |
| M04.3 | DONE | Shadow sampling PCF bias |

### M05

| Ticket | Statut | Detail |
|--------|--------|--------|
| M05.1 | DONE | IBL BRDF LUT compute |
| M05.2 | TODO | IrradianceCubemapPass/shader absents |
| M05.3 | DONE | cloture (issue existante) |
| M05.4 | DONE | cloture (issue existante) |

### M06

| Ticket | Statut | Detail |
|--------|--------|--------|
| M06.1 | DONE | cloture (issue existante) |
| M06.2 | DONE | cloture (issue existante) |
| M06.3 | DONE | cloture (issue existante) |
| M06.4 | DONE | cloture (issue existante) |

### M07

| Ticket | Statut | Detail |
|--------|--------|--------|
| M07.1 | DONE | cloture (issue existante) |
| M07.2 | DONE | cloture (issue existante) |
| M07.3 | DONE | cloture (issue existante) |
| M07.4 | DONE | cloture (issue existante) |

### M08

| Ticket | Statut | Detail |
|--------|--------|--------|
| M08.1 | DONE | cloture (issue existante) |
| M08.2 | DONE | cloture (issue existante) |
| M08.3 | DONE | cloture (issue existante) |
| M08.4 | DONE | cloture (issue existante) |
| M08.5 | DONE | AutoExposure staging double buffering |
| M08.6 | DONE | AutoExposure histogramme luminance |

### M09

| Ticket | Statut | Detail |
|--------|--------|--------|
| M09.1 | DONE | cloture (issue existante) |
| M09.2 | DONE | cloture (issue existante) |
| M09.3 | DONE | cloture (issue existante) |
| M09.4 | DONE | cloture (issue existante) |
| M09.5 | DONE | cloture (issue existante) |
| M09.6 | TODO | VMA identity buffer non migre (vkAllocateMemory brut) |
| M09.7 | DONE | WorldModel GlobalChunkCoord |
| M09.8 | DONE | HlodRuntime distance_sq |
| M09.9 | TODO | LogStatsIfDue/timer reel absents |
| M09.10 | DONE | LOD distance reelle engine |
| M09.11 | TODO | JobSystem BuildChunkDrawList sequentiel |

### M10

| Ticket | Statut | Detail |
|--------|--------|--------|
| M10.1 | DONE | cloture (issue existante) |
| M10.2 | DONE | cloture (issue existante) |
| M10.3 | DONE | cloture (issue existante) |
| M10.4 | DONE | cloture (issue existante) |
| M10.5 | DONE | cloture (issue existante) |

### M11

| Ticket | Statut | Detail |
|--------|--------|--------|
| M11.1 | DONE | Zone Builder import glTF/layout |
| M11.2 | DONE | Zone Builder chunking outputs |
| M11.3 | PARTIAL | Recast navmesh/portals absents |
| M11.4 | PARTIAL | Generation probes IBL absente |
| M11.5 | DONE | Versioning builder/engine/contentHash |

### M12

| Ticket | Statut | Detail |
|--------|--------|--------|
| M12.1 | DONE | cloture (issue existante) |
| M12.2 | DONE | cloture (issue existante) |
| M12.3 | DONE | cloture (issue existante) |
| M12.4 | DONE | cloture (issue existante) |

### M13

| Ticket | Statut | Detail |
|--------|--------|--------|
| M13.1 | DONE | Server core tick loop/snapshots |
| M13.2 | DONE | Spatial partition cells 64m interest |
| M13.3 | DONE | Replication spawn/despawn/state |
| M13.4 | DONE | Zone transitions server-authoritative |

### M14

| Ticket | Statut | Detail |
|--------|--------|--------|
| M14.1 | DONE | cloture (issue existante) |
| M14.2 | DONE | cloture (issue existante) |
| M14.3 | DONE | cloture (issue existante) |
| M14.4 | DONE | cloture (issue existante) |

### M15

| Ticket | Statut | Detail |
|--------|--------|--------|
| M15.1 | DONE | Quests data-driven JSON |
| M15.2 | DONE | Spawners respawn/leash |
| M15.3 | DONE | Dynamic events phases/rewards |
| M15.4 | TODO | engine/server CMake target+main_server absents |

### M16

| Ticket | Statut | Detail |
|--------|--------|--------|
| M16.1 | DONE | cloture (issue existante) |
| M16.2 | DONE | cloture (issue existante) |
| M16.3 | DONE | cloture (issue existante) |
| M16.4 | DONE | cloture (issue existante) |
| M16.5 | DONE | cloture (issue existante) |

### M17

| Ticket | Statut | Detail |
|--------|--------|--------|
| M17.1 | DONE | cloture (issue existante) |
| M17.2 | DONE | cloture (issue existante) |
| M17.3 | DONE | cloture (issue existante) |
| M17.4 | DONE | cloture (issue existante) |

### M18

| Ticket | Statut | Detail |
|--------|--------|--------|
| M18.1 | DONE | cloture (issue existante) |
| M18.2 | DONE | cloture (issue existante) |
| M18.3 | DONE | cloture (issue existante) |
| M18.4 | DONE | cloture (issue existante) |
| M18.5 | DONE | cloture (issue existante) |

### M19

| Ticket | Statut | Detail |
|--------|--------|--------|
| M19.1 | DONE | cloture (issue existante) |
| M19.2 | DONE | cloture (issue existante) |
| M19.3 | DONE | cloture (issue existante) |
| M19.4 | DONE | cloture (issue existante) |
| M19.5 | DONE | cloture (issue existante) |
| M19.6 | DONE | cloture (issue existante) |
| M19.7 | DONE | cloture (issue existante) |
| M19.8 | DONE | cloture (issue existante) |
| M19.9 | DONE | cloture (issue existante) |
| M19.10 | DONE | cloture (issue existante) |
| M19.11 | DONE | cloture (issue existante) |

### M20

| Ticket | Statut | Detail |
|--------|--------|--------|
| M20.1 | DONE | cloture (issue existante) |
| M20.1 | DONE | cloture (issue existante) |
| M20.2 | DONE | cloture (issue existante) |
| M20.2 | DONE | cloture (issue existante) |
| M20.3 | DONE | cloture (issue existante) |
| M20.3 | DONE | cloture (issue existante) |
| M20.4 | DONE | cloture (issue existante) |
| M20.4 | DONE | cloture (issue existante) |
| M20.5 | DONE | cloture (issue existante) |
| M20.6 | DONE | cloture (issue existante) |

### M21

| Ticket | Statut | Detail |
|--------|--------|--------|
| M21.1 | DONE | cloture (issue existante) |
| M21.1 | DONE | cloture (issue existante) |
| M21.2 | DONE | cloture (issue existante) |
| M21.2 | DONE | cloture (issue existante) |
| M21.3 | DONE | cloture (issue existante) |
| M21.3 | DONE | cloture (issue existante) |
| M21.4 | DONE | cloture (issue existante) |
| M21.5 | DONE | cloture (issue existante) |
| M21.6 | DONE | cloture (issue existante) |

### M22

| Ticket | Statut | Detail |
|--------|--------|--------|
| M22.1 | DONE | cloture (issue existante) |
| M22.1 | DONE | cloture (issue existante) |
| M22.2 | DONE | cloture (issue existante) |
| M22.2 | DONE | cloture (issue existante) |
| M22.3 | DONE | cloture (issue existante) |
| M22.4 | DONE | cloture (issue existante) |
| M22.5 | DONE | cloture (issue existante) |
| M22.6 | DONE | cloture (issue existante) |

### M23

| Ticket | Statut | Detail |
|--------|--------|--------|
| M23.1 | DONE | cloture (issue existante) |
| M23.1 | DONE | cloture (issue existante) |
| M23.2 | DONE | cloture (issue existante) |
| M23.2 | DONE | cloture (issue existante) |
| M23.3 | DONE | cloture (issue existante) |
| M23.3 | DONE | cloture (issue existante) |
| M23.4 | DONE | cloture (issue existante) |

### M24

| Ticket | Statut | Detail |
|--------|--------|--------|
| M24.1 | DONE | cloture (issue existante) |
| M24.1 | DONE | cloture (issue existante) |
| M24.2 | DONE | cloture (issue existante) |
| M24.2 | DONE | cloture (issue existante) |
| M24.3 | DONE | cloture (issue existante) |
| M24.4 | DONE | cloture (issue existante) |
| M24.5 | PARTIAL | Packaging/deploy docs incomplets |

### M25

| Ticket | Statut | Detail |
|--------|--------|--------|
| M25.1 | DONE | cloture (issue existante) |
| M25.1 | DONE | cloture (issue existante) |
| M25.2 | DONE | cloture (issue existante) |
| M25.2 | DONE | cloture (issue existante) |
| M25.3 | DONE | cloture (issue existante) |
| M25.3 | DONE | cloture (issue existante) |
| M25.4 | DONE | cloture (issue existante) |
| M25.4 | DONE | cloture (issue existante) |

### M26

| Ticket | Statut | Detail |
|--------|--------|--------|
| M26.1 | DONE | cloture (issue existante) |
| M26.2 | DONE | cloture (issue existante) |
| M26.3 | DONE | cloture (issue existante) |

### M27

| Ticket | Statut | Detail |
|--------|--------|--------|
| M27.1 | DONE | Camera 3e personne orbit+collision |
| M27.2 | DONE | cloture (issue existante) |

### M28

| Ticket | Statut | Detail |
|--------|--------|--------|
| M28.1 | DONE | cloture (issue existante) |
| M28.2 | DONE | cloture (issue existante) |
| M28.3 | DONE | cloture (issue existante) |
| M28.4 | DONE | cloture (issue existante) |

### M29

| Ticket | Statut | Detail |
|--------|--------|--------|
| M29.1 | DONE | cloture (issue existante) |
| M29.2 | DONE | cloture (issue existante) |
| M29.3 | DONE | cloture (issue existante) |

### M30

| Ticket | Statut | Detail |
|--------|--------|--------|
| M30.1 | DONE | Client prediction mouvement |
| M30.2 | DONE | Reconciliation serveur smooth |
| M30.3 | PARTIAL | Lag-comp rewind/historique absents |

### M31

| Ticket | Statut | Detail |
|--------|--------|--------|
| M31.1 | DONE | Status effects stacking/dispel |
| M31.2 | DONE | Buff/debuff UI + auras |

### M32

| Ticket | Statut | Detail |
|--------|--------|--------|
| M32.1 | DONE | Friend system |
| M32.2 | DONE | Party system 4 modes loot |
| M32.3 | DONE | Guild system rangs/permissions |
| M32.4 | DONE | Guild tabard/emblem |

### M33

| Ticket | Statut | Detail |
|--------|--------|--------|
| M33.1 | PARTIAL | Session/token backend incomplet |
| M33.2 | DONE | Reset password + email verif |
| M33.3 | DONE | Rate limit/ban/captcha |

### M34

| Ticket | Statut | Detail |
|--------|--------|--------|
| M34.1 | DONE | Heightmap R16 + LOD |
| M34.2 | DONE | Terrain splatting 4 layers |
| M34.3 | DONE | Terrain holes/cliffs |
| M34.4 | DONE | Terrain editing tools |

### M35

| Ticket | Statut | Detail |
|--------|--------|--------|
| M35.1 | DONE | cloture (issue existante) |
| M35.2 | DONE | cloture (issue existante) |
| M35.3 | DONE | cloture (issue existante) |
| M35.4 | DONE | cloture (issue existante) |

### M36

| Ticket | Statut | Detail |
|--------|--------|--------|
| M36.1 | DONE | cloture (issue existante) |
| M36.2 | DONE | cloture (issue existante) |
| M36.3 | DONE | cloture (issue existante) |

### M37

| Ticket | Statut | Detail |
|--------|--------|--------|
| M37.1 | DONE | cloture (issue existante) |
| M37.2 | DONE | cloture (issue existante) |
| M37.3 | DONE | cloture (issue existante) |

### M38

| Ticket | Statut | Detail |
|--------|--------|--------|
| M38.1 | DONE | cloture (issue existante) |
| M38.2 | DONE | cloture (issue existante) |
| M38.3 | DONE | cloture (issue existante) |

### M39

| Ticket | Statut | Detail |
|--------|--------|--------|
| M39.1 | DONE | cloture (issue existante) |
| M39.2 | DONE | cloture (issue existante) |
| M39.3 | DONE | cloture (issue existante) |
| M39.4 | DONE | cloture (issue existante) |

### M40

| Ticket | Statut | Detail |
|--------|--------|--------|
| M40.1 | DONE | cloture (issue existante) |
| M40.2 | PARTIAL | Bot client/scenarios gameplay incomplets |
| M40.3 | DONE | cloture (issue existante) |

### M41

| Ticket | Statut | Detail |
|--------|--------|--------|
| M41.1 | DONE | Combat serveur validation movement |
| M41.2 | DONE | Audit log securite |

### M42

| Ticket | Statut | Detail |
|--------|--------|--------|
| M42.1 | DONE | cloture (issue existante) |
| M42.2 | PARTIAL | CJK/RTL absents |

### M43

| Ticket | Statut | Detail |
|--------|--------|--------|
| M43.1 | TODO | Material node editor absent |
| M43.2 | TODO | Particle editor absent |
| M43.3 | TODO | Quest flowchart absent |
| M43.4 | PARTIAL | Panneaux editeurs specifiques absents |
| M43.5 | DONE | Audio zone editor panel |
| M43.6 | TODO | EditorFxPanel absent |
| M43.7 | TODO | EditorLootPanel absent |
| M43.8 | TODO | EditorThemePanel absent |

### M44

| Ticket | Statut | Detail |
|--------|--------|--------|
| M44.1 | DONE | cloture (issue existante) |
| M44.2 | DONE | cloture (issue existante) |
| M44.3 | DONE | Multi-shard registry/load balancing |
| M44.4 | DONE | cloture (issue existante) |

### M45

| Ticket | Statut | Detail |
|--------|--------|--------|
| M45.1 | DONE | Perspective aerienne |
| M45.2 | DONE | Brouillard volumique god rays |
| M45.3 | DONE | Depth of field/bokeh |
| M45.4 | DONE | impostor_builder offline |
| M45.5 | DONE | Rendu runtime impostors |
| M45.6 | DONE | DDGI structure |
| M45.7 | DONE | DDGI runtime |
| M45.8 | DONE | DDGI qualite/fallback |

### M100

| Ticket | Statut | Detail |
|--------|--------|--------|
| M100.1 | DONE | Shell editeur (dockspace, panels, menu, raccourcis) |
| M100.2 | DONE | Undo/redo command stack + history panel + tests |
| M100.3 | DONE | Zone builder lib extraite + tests roundtrip |
| M100.4 | DONE | 3 modes camera (FPS/Orbital/TopDown) |
| M100.5 | DONE | Heightmap 257x257 + mesh builder |
| M100.6 | DONE | 5 brosses sculpt + seams cross-chunk |
| M100.7 | DONE | Stamps PNG + 3 generateurs proceduraux |
| M100.8 | DONE | 4 LOD + worker async |
| M100.9 | DONE | Splat 8 couches + shader blend |
| M100.10 | DONE | Peinture splat manuel+auto, invariant sum=255 |
| M100.11 | DONE | 13 types surface + query service |
| M100.12 | DONE | Proxies capsule/hull/trimesh + overlay |
| M100.13 | DONE | Lacs/rivieres + mesh + flow |
| M100.14 | DONE | Passe eau SSR + refraction |
| M100.15 | DONE | Wading/swimming detection |
| M100.16 | DONE | Hazard volumes + simulateur + outil (cloture #808) |
| M100.17 | DONE | Easy placement tool + props.bin (cloture #810) |
| M100.18 | DONE | Vegetation library + Poisson-disk + density (cloture #811) |
| M100.19 | DONE | Forest/Field generators procéduraux (cloture #811) |
| M100.20 | DONE | Wind system + zones (sway/override) (cloture #811) |
| M100.21 | DONE | EntityInfluenceCollector + flexion (cloture #811) |
| M100.22 | DONE | Fog volumetrique froxel |
| M100.23 | DONE | Distance/height fog + zones |
| M100.24 | DONE | Sun/sky ToDxToY + probes |
| M100.25 | DONE | Saisons client+broadcaster |
| M100.26 | DONE | Meteo 6 types + modifiers |
| M100.27 | DONE | Shade map + ComputeTemperature + ThermalQuery (cloture #811) |
| M100.28 | DONE | Gameplay zones + WeatherOverride blend (cloture #811) |
| M100.29 | DONE | Spline sampler + roads (Catmull-Rom, splat) (cloture #811) |
| M100.30 | DONE | Bridges/walls + splines v2 + surface pont (cloture #811) |
| M100.31 | DONE | Hamlet generator + kits (cloture #811) |
| M100.32 | DONE | Interactive props + relay master + simulateur (cloture #814) |
| M100.33 | DONE | Footstep audio + PlaytestMode (cloture #811) |
| M100.34 | TODO | SelectionTool/Layers/Minimap/Exporter absents |
| M100.35 | DONE | Mountain range tools |
| M100.36 | DONE | River network generator |
| M100.37 | DONE | Coastline + sea level |
| M100.38 | DONE | Hydraulic erosion |
| M100.39 | DONE | Thermal/wind erosion |
| M100.40 | DONE | Mesh insert + cave tool |
| M100.41 | TODO | src/world_editor/volumes/overhangs/ absent |
| M100.42 | TODO | src/world_editor/volumes/arches/ absent |
| M100.43 | DONE | Dungeon portal system |
| M100.44 | DONE | VMap bridge |
| M100.45 | TODO | spec doc only |
| M100.46 | DONE | ZonePresets library + tests (corrige) |
| M100.47 | TODO | spec doc only |
| M100.48 | TODO | spec doc only |
| M100.49 | TODO | spec doc only |
| M100.50 | TODO | spec doc only |
| M100.51 | TODO | spec doc only |

### M101

| Ticket | Statut | Detail |
|--------|--------|--------|
| M101.1 | DONE | Modele de graphe + serialisation JSON deterministe (routine_graph) |
| M101.2 | DONE | VM zone_event deterministe (routine_vm, lib pure) |
| M101.3 | DONE | Segment routines.bin additif + codec + round-trip |
| M101.4 | DONE | Panneau nodal + commandes undo/redo (CommandStack) |
| M101.5 | DONE | Palette schema-driven + inspecteur de proprietes |
| M101.6 | DONE | Validation de graphe (cycles/pins/orphelins/schema) |
| M101.7 | PARTIAL | RoutineToEventAI livre ; PNJ complet Blocked (Role Registry/Smart Objects/MoveTo absents) |
| M101.8 | TODO | Noeuds zone/gameplay differes (deps M100.16/28/32 non livrees) |
| M101.9 | TODO | Playtest F5 differe (dep M100.33 non livree) |
| M101.10 | DONE | Round-trip + tests CI headless |
| M101.11 | DONE | Tooltips + contenu d'aide (reutilise HelpContentStore) |

### AUTH-UI

| Ticket | Statut | Detail |
|--------|--------|--------|
| AUTH-UI.0 | DONE | Architecture decoupage (doc) |
| AUTH-UI.1 | DONE | Socle commun Core/Settings/Native/Renderer |
| AUTH-UI.2 | DONE | LoginScreen |
| AUTH-UI.3 | DONE | RegisterScreen |
| AUTH-UI.4 | DONE | RegisterErrorScreen |
| AUTH-UI.5 | DONE | ConfirmEmailScreen |
| AUTH-UI.6 | DONE | OptionsScreen |
| AUTH-UI.7 | DONE | LangSelectScreen |
| AUTH-UI.8 | DONE | ShardPickScreen |
| AUTH-UI.9 | DONE | ForgotPassword |
| AUTH-UI.10 | DONE | Terms |
| AUTH-UI.11 | DONE | CharacterCreate |

### CHAR-MODEL

| Ticket | Statut | Detail |
|--------|--------|--------|
| CHAR-MODEL.1 | PARTIAL | builder offline/assets binaires absents |
| CHAR-MODEL.2 | PARTIAL | builder/.skel assets absents |
| CHAR-MODEL.3 | DONE | GPU skinning LBS + shader |
| CHAR-MODEL.4 | TODO | AssetRegistry skinned absent |
| CHAR-MODEL.5 | PARTIAL | GeometryPass/CSM skinned absents |
| CHAR-MODEL.6 | PARTIAL | anim_builder/.anim assets absents |
| CHAR-MODEL.7 | DONE | Sampler lerp/slerp + tests |
| CHAR-MODEL.8 | DONE | Crossfade blend + tests |
| CHAR-MODEL.9 | TODO | LocomotionStateMachine absent |
| CHAR-MODEL.10 | TODO | ActionStateMachine absent |
| CHAR-MODEL.11 | TODO | CombatStateMachine/additive layers absents |
| CHAR-MODEL.12 | TODO | AttachmentSocket absent |
| CHAR-MODEL.13 | TODO | FootIK absent |
| CHAR-MODEL.14 | PARTIAL | .skel/.anim binaires absents |
| CHAR-MODEL.15 | PARTIAL | skinmesh binaire absent |
| CHAR-MODEL.16 | PARTIAL | skinmesh binaire absent |
| CHAR-MODEL.17 | PARTIAL | skinmesh binaire absent |
| CHAR-MODEL.18 | PARTIAL | skinmesh binaire absent |
| CHAR-MODEL.19 | PARTIAL | skinmesh/skel winged absents |
| CHAR-MODEL.20 | TODO | assets chevalier_dragon absents |
| CHAR-MODEL.21 | TODO | assets orkh absents |
| CHAR-MODEL.22 | TODO | assets gobelin absents |
| CHAR-MODEL.23 | PARTIAL | RaceColorTable/MaterialOverride absents |
| CHAR-MODEL.24 | PARTIAL | pipeline forward/anim partiels |
| CHAR-MODEL.25 | TODO | MorphologyParams/Applicator absents |
| CHAR-MODEL.26 | TODO | CharacterAnimator absent |
| CHAR-MODEL.27 | TODO | quadruped_v1.skel/clips absents |
| CHAR-MODEL.28 | TODO | AnimalAI absent |
| CHAR-MODEL.29 | TODO | assets cheval absents |
| CHAR-MODEL.30 | TODO | MountSystem absent |
| CHAR-MODEL.31 | TODO | assets ferme absents |
| CHAR-MODEL.32 | TODO | assets loup/ours absents |
| CHAR-MODEL.33 | TODO | bird_v1.skel/modeles absents |
| CHAR-MODEL.34 | TODO | snake_v1.skel/serpent absents |
| CHAR-MODEL.35 | TODO | fish_v1.skel/poisson absents |
| CHAR-MODEL.36 | TODO | dragon_v1.skel/dragon absents |
| CHAR-MODEL.37 | TODO | AmbientLifeSystem absent |

### SERVER-CORE

| Ticket | Statut | Detail |
|--------|--------|--------|
| SERVER-CORE.01 | DONE | Chat routing/safety/commands + tests |
| SERVER-CORE.02 | DONE | Entities + ObjectGuid + UpdateFields |
| SERVER-CORE.03 | DONE | Grids + AOI + visitor |
| SERVER-CORE.04 | DONE | MoveSpline + tests (corrige engine->src) |
| SERVER-CORE.05 | DONE | VMap collisions + tests (corrige) |
| SERVER-CORE.06 | DONE | Roles/security levels + tests |
| SERVER-CORE.07 | DONE | EventAI db-driven + tests |
| SERVER-CORE.08 | PARTIAL | ArenaTeam/MMR/maintenance absents |
| SERVER-CORE.09 | PARTIAL | AuctionEntry/Manager/expire absents |
| SERVER-CORE.10 | PARTIAL | BG abstract/score/manager absents |
| SERVER-CORE.11 | DONE | Threat/HostileRef + tests |
| SERVER-CORE.12 | DONE | PacketLog + tests |
| SERVER-CORE.13 | DONE | SQLStorage + prepared/delay + tests |
| SERVER-CORE.14 | DONE | DBScripts VM + tests (corrige) |
| SERVER-CORE.15 | PARTIAL | GroupReference/loot rules/raid absents |
| SERVER-CORE.16 | DONE | Globals condition/accessor/locale + tests (corrige) |
| SERVER-CORE.17 | DONE | Loot templates/refs + tests |
| SERVER-CORE.18 | DONE | Mails COD/expire/massmail + tests |
| SERVER-CORE.19 | DONE | Maps subclasses + InstanceManager |
| SERVER-CORE.20 | DONE | MotionGenerators + tests |
| SERVER-CORE.21 | PARTIAL | Guild/ranks/bank/eventlog absents |
| SERVER-CORE.22 | PARTIAL | WeightedSelector/nesting/persist absents |
| SERVER-CORE.23 | DONE | Quest state machine + payloads |
| SERVER-CORE.24 | DONE | Reputation matrix + spillover |
| SERVER-CORE.25 | DONE | Social friends/ignore + payloads |
| SERVER-CORE.26 | DONE | Spells template/aura/proc + tests (corrige) |
| SERVER-CORE.27 | DONE | Trade 2-phase commit anti-scam |
| SERVER-CORE.28 | PARTIAL | DSL parser/lexer/shutdown absents |
| SERVER-CORE.29 | DONE | Anticheat position deltas |
| SERVER-CORE.30 | DONE | Cinematics trigger/camera |
| SERVER-CORE.31 | DONE | GameEvents scheduler/cascade |
| SERVER-CORE.32 | DONE | GMTickets CRUD + state machine |
| SERVER-CORE.33 | DONE | LFG queue/matchmaking |
| SERVER-CORE.34 | PARTIAL | Measurement RAII/influx/pool absents |
| SERVER-CORE.35 | DONE | Messager swap-execute MPSC |
| SERVER-CORE.36 | DONE | OutdoorPvP plugins/objectives |
| SERVER-CORE.37 | DONE | Crash dump Windows |
| SERVER-CORE.38 | DONE | PlayerBot headless/strategies |
| SERVER-CORE.39 | DONE | Skills craft/discovery/proc |
| SERVER-CORE.40 | PARTIAL | PlayerDump/Cleaner/Language/tool absents |
| SERVER-CORE.41 | PARTIAL | ByteBuffer/PCQueue/Trackable absents |
| SERVER-CORE.42 | DONE | Weather markov authoritative |
| SERVER-CORE.44 | PARTIAL | Config/timing/rarity logic absents |
| SERVER-CORE.45 | TODO | BigNumber/Srp6/CryptoHash absents |

### STAB

| Ticket | Statut | Detail |
|--------|--------|--------|
| STAB.1 | DONE | cloture (issue existante) |
| STAB.2 | DONE | cloture (issue existante) |
| STAB.3 | DONE | cloture (issue existante) |
| STAB.4 | DONE | cloture (issue existante) |
| STAB.5 | DONE | cloture (issue existante) |
| STAB.6 | DONE | cloture (issue existante) |
| STAB.7 | DONE | cloture (issue existante) |
| STAB.8 | PARTIAL | 1 fprintf debug restant |
| STAB.9 | DONE | cloture (issue existante) |
| STAB.10 | DONE | cloture (issue existante) |
| STAB.11 | DONE | cloture (issue existante) |
| STAB.12 | DONE | cloture (issue existante) |
| STAB.12b | DONE | cloture (issue existante) |
| STAB.14 | DONE | cloture (issue existante) |

### docs

| Ticket | Statut | Detail |
|--------|--------|--------|
| docs-M20.2_Argon2_params | DONE | cloture (issue existante) |
| docs-accounts_schema_v1 | DONE | Schema accounts v1 implemente |
| docs-protocol_v1 | DONE | Protocole v1 implemente |

### world

| Ticket | Statut | Detail |
|--------|--------|--------|
| world-001 | DONE | Spec pipeline monde documentee+implementee |
| world-002 | DONE | world_map.edit JSON parse/save roundtrip |
| world-003 | DONE | Heightmap vs zone 10km override |
| world-004 | DONE | Export runtime bundle + zone.meta |
| world-005 | DONE | Zone builder layout sorties/chemins |
| world-006 | DONE | Pont world_editor -> zone_builder |
| world-007 | DONE | Zone demo validation+doc |
| world-008 | DONE | Peinture splat persist/export |
| world-009 | DONE | Instances arbres/rochers |
| world-010 | DONE | Herbe detail surface |
| world-011 | DONE | Routes couches/splines (branche A) |
| world-012 | DONE | Monde complet validation streaming |
| world-013 | DONE | Arbres especes/taille/forme |

## Notes

- Les tickets DONE retires du dossier milestone restent consultables dans `tickets/issues/` (Issue de cloture embarquant le contenu du ticket) et dans l'historique git.
- Les milestones entierement DONE n'ont plus de dossier (supprime car vide) ou ne conservent qu'un INDEX/README.
- Le dossier serveur a ete renomme `SERVER-CORE/` (terme precedent proscrit, neutralise dans tout `tickets/`).
