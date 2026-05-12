# Milestone M100 — Éditeur de monde 3D AAA et systèmes environnementaux

> **Cluster M100.** Distinct de M43 (panneaux ImGui spécialisés) et de M44 (infrastructure
> serveur). M100 livre **l'éditeur de monde 3D**, **toute la simulation
> environnementale gameplay côté client** (surfaces, hazards, saisons, météo,
> ombre thermique, interaction joueur ↔ végétation, objets interactifs sobres),
> ainsi que les **outils macro de génération de terrain naturaliste**, les
> **volumes 3D** (caves, donjons) et la **couche d'accessibilité** qui rend
> l'éditeur utilisable par un contributeur communautaire.
>
> **Pré-requis livré :** M43.4 — ImGui foundation.

## Principes architecturaux structurants

| Couche | Rôle | Ne fait PAS |
|--------|------|-------------|
| **Client (`engine_core` + Vulkan)** | Tout le rendu, toute la simulation gameplay locale, lecture des chunk packages, déclenchement réseau. | Rien d'autre que ce qui est listé. |
| **Éditeur (`world_editor_app`, binaire `lcdlln_world_editor.exe`)** | Création/modification de la carte. Écriture des chunk packages. Mode playtest F5 utilisant le code client de prod. | Pas de pipeline de rendu séparé. Pas de format "preview". Pas de logique gameplay propriétaire. |
| **Serveur (`server_app`, master + shard)** | Auth + relais des messages multi-clients (positions, actions, broadcasts saison/météo, allocation d'instance donjon). | Aucune simulation environnementale. Aucun chargement de splat / shade / thermal. Aucun calcul de surface. Aucune validation gameplay sur les objets interactifs. |

**Contrat de parité éditeur ↔ client.** Tout ce que l'éditeur écrit est lu et rendu
identiquement par le client en mode jeu normal. Pipeline Vulkan unique. Format
binaire unique. Shaders uniques. Versions strictement alignées. Tests de round-trip
obligatoires.

## Conventions de cet INDEX

### Statuts (4 niveaux)

| Statut | Sens |
|--------|------|
| **Done** | Spec figée + code mergé sur `main` + CI verte. Le ticket est en production. |
| **Ready** | Spec figée, dépendances livrées, prêt à coder. Aucun blocage. |
| **Draft** | Spec en cours de rédaction ou de revue. Peut bouger. |
| **Blocked** | Spec figée mais bloqué par une dépendance non résolue ou une décision en attente (raison documentée). |

> Note : « Done (CI pending) » de l'ancien INDEX correspond à **Done** ici (le
> CI est vert au moment du merge ; le suffixe « pending » était un artefact
> d'écriture, pas un état métier).

### Phases logiques (12)

L'**ordre logique** ci-dessous ne suit pas strictement l'ordre numérique des
tickets : les Phases 2.5, 11 et 12 ont été ajoutées après la première rédaction
de M100.1–34, et leurs numéros (M100.35–51) sont supérieurs à ceux de la
Phase 10 (M100.33–34) sans être postérieurs en logique d'implémentation.

| # | Phase | Tickets | But |
|---|-------|---------|-----|
| 1 | Fondations | M100.1–4 | Shell ImGui, Undo/Redo, lib `zone_builder` partagée, caméras éditeur. |
| 2 | Terrain (édition de base) | M100.5–8 | Heightmap, sculpting, stamps, LOD. |
| **2.5** | **Terrain naturaliste** | **M100.35–39** | Toolbar à icônes, montagnes/vallées macro, rivières (D8 watershed), côtes + sea level, érosion hydraulique / thermique / éolienne. |
| 3 | Splat / Surfaces / Collision | M100.9–12 | Splat map 8 couches, peinture, `SurfaceQuery`, collision proxy. |
| 4 | Hydrologie & Hazards | M100.13–16 | Lacs/rivières, water render pass, wading/swimming, hazard volumes. |
| 5 | Placement & Végétation | M100.17–21 | Placement universel, foliage, vent, interaction joueur. |
| 6 | Atmosphère & Brouillard | M100.22–24 | Fog volumes, distance/height fog, soleil + IBL probes. |
| 7 | Saisons / Météo / Thermal | M100.25–28 | `SeasonClock`, `WeatherSystem`, shade/thermal map, gameplay zones. |
| 8 | Routes / Ponts / Structures | M100.29–31 | Splines, bridges/walls, hamlet generator. |
| 9 | Objets interactifs | M100.32 | 5 types gelés : doors hinge/sliding, windows, trapdoor, simple chest. |
| 10 | Polissage final | M100.33–34 | Footstep audio, Playtest F5, Selection/Layers/Minimap, **Save Zone** (orchestrateur). |
| **11** | **Volumes 3D** | **M100.40–44** | Mesh insert foundation, cave/overhang/arch tools, dungeon portals, **VMap bridge** (clôture serveur). |
| **12** | **Accessibilité éditeur** | **M100.45–51** | Mode Simple/Advanced + presets, zone presets, tooltips/F1, validation, tutoriel + diagnostic, quick start wizard, **deployment pipeline CI**. |

## Liste complète des tickets (51)

### Phase 1 — Fondations

| Ticket | Titre | Dépendances | Statut |
|--------|-------|-------------|--------|
| M100.1 | World Editor Bootstrap | M43.4 | Done |
| M100.2 | Command Stack & Undo/Redo | M100.1 | Done |
| M100.3 | Zone Builder Library Extraction | M100.1 | Done |
| M100.4 | Editor Camera Modes | M100.1 | Done |

### Phase 2 — Terrain (édition de base)

| Ticket | Titre | Dépendances | Statut |
|--------|-------|-------------|--------|
| M100.5 | Heightmap Data Structure | M100.3, M100.4 | Done |
| M100.6 | Terrain Sculpting Brushes | M100.2, M100.5 | Done |
| M100.7 | Terrain Stamps & Procedural Generators | M100.6 | Done |
| M100.8 | Terrain LOD Regeneration | M100.5 | Done |

### Phase 2.5 — Terrain naturaliste (extensions macro)

> **Insérée après Phase 2 dans l'ordre logique d'implémentation**, malgré ses numéros
> supérieurs à la Phase 10. Ces tickets étendent le système terrain de base avec
> des outils macroscopiques. M100.35 a fait l'objet de deux versions de spec :
> seule la v2 (toolbar à icônes + invariant de visibilité du terrain strict) est
> conservée — la v1 a été archivée le 2026-05-12.

| Ticket | Titre | Dépendances | Statut |
|--------|-------|-------------|--------|
| M100.35 | Toolbar à icônes + Outils macros terrain (Mountain Ranges & Valley Chains) | M100.2, M100.5, M100.6, M100.7, M100.8 | Draft |
| M100.36 | River Network Generator (Watershed Simulation) — **introduit `OceanSettings`** | M100.1, M100.2, M100.5, M100.6, M100.8, M100.13, M100.35 | Draft |
| M100.37 | Coastline & Sea Level Editor — **étend `OceanSettings`** | M100.13, M100.35, M100.36 | Draft |
| M100.38 | Hydraulic Erosion (Particle-Based) | M100.36, M100.37 | Draft |
| M100.39 | Thermal & Wind Erosion | M100.36, M100.37, M100.38 | Draft |

### Phase 3 — Splat / Surfaces / Collision

| Ticket | Titre | Dépendances | Statut |
|--------|-------|-------------|--------|
| M100.9 | Splat Map System | M100.5 | Done |
| M100.10 | Splat Painting Brushes | M100.2, M100.9 | Done |
| M100.11 | Surface Material System & SurfaceQuery (client) | M100.9 | Done |
| M100.12 | Collision Proxy System | M100.1 | Done |

### Phase 4 — Hydrologie & Hazards

| Ticket | Titre | Dépendances | Statut |
|--------|-------|-------------|--------|
| M100.13 | Water Surfaces (Lakes & Rivers) | M100.5, M100.6 | Done |
| M100.14 | Water Render Pass | M100.13 | Done |
| M100.15 | Water Surface Hook (Wading & Swimming) | M100.11, M100.13 | Ready |
| M100.16 | Hazard Volume System | M100.11 | Ready |

### Phase 5 — Placement & Végétation

| Ticket | Titre | Dépendances | Statut |
|--------|-------|-------------|--------|
| M100.17 | Easy Placement Tool | M100.2, M100.5, M100.12 | Ready |
| M100.18 | Vegetation Library & Density Painting | M100.5, M100.17 | Ready |
| M100.19 | Procedural Forest & Field Tools | M100.11, M100.18 | Ready |
| M100.20 | Vegetation Wind Animation | M100.18 | Ready |
| M100.21 | Vegetation Player Interaction Shader | M100.20 | Ready |

### Phase 6 — Atmosphère & Brouillard

| Ticket | Titre | Dépendances | Statut |
|--------|-------|-------------|--------|
| M100.22 | Volumetric Fog Volumes | M100.4 | Ready |
| M100.23 | Distance Fog & Height Fog Tuning | M100.22 | Ready |
| M100.24 | Sun, Sky & Probes Editor | M100.23 | Ready |

### Phase 7 — Saisons / Météo / Thermal

| Ticket | Titre | Dépendances | Statut |
|--------|-------|-------------|--------|
| M100.25 | Season System & Time-of-Year — ⚠️ opcode `SeasonBroadcast` | M100.24 | Ready |
| M100.26 | Weather System & Dynamic Surface Modifiers — ⚠️ opcode `WeatherBroadcast` | M100.11, M100.25 | Ready |
| M100.27 | Shade Map & Thermal Map (`ThermalQuery`) | M100.18, M100.25, M100.26 | Ready |
| M100.28 | Gameplay Zones & Weather Zones | M100.26 | Ready |

### Phase 8 — Routes / Ponts / Structures

| Ticket | Titre | Dépendances | Statut |
|--------|-------|-------------|--------|
| M100.29 | Spline Tool & Roads | M100.10, M100.17 | Ready |
| M100.30 | Bridges & Modular Walls | M100.17, M100.29 | Ready |
| M100.31 | Hamlet Generator | M100.17 | Ready |

### Phase 9 — Objets interactifs

| Ticket | Titre | Dépendances | Statut |
|--------|-------|-------------|--------|
| M100.32 | Interactive Props (Doors, Windows, Trapdoors, Simple Chests) — ⚠️ 3 opcodes `InteractiveStateChange/Broadcast/Sync` | M100.12, M100.17 | Ready |

### Phase 10 — Polissage final

| Ticket | Titre | Dépendances | Statut |
|--------|-------|-------------|--------|
| M100.33 | Footstep Audio Surface Hook & Playtest Mode (F5) | M100.11, M100.16, M100.26, M100.27 | Ready |
| M100.34 | Selection, Layers, Minimap & Save/Load Zone | M100.5, M100.9, M100.16, M100.27, M100.28, M100.32 | Blocked |

> **M100.34 — note de rétro-compatibilité.** Spec écrite avant l'existence des
> Phases 11 et 12. L'orchestrateur `WorldEditorExporter` devra être étendu pour
> exporter aussi `instances/mesh_inserts.bin` (M100.40–42), `instances/dungeons.bin`
> (M100.43), ainsi que les fichiers d'accessibilité utilisateur (`tool_presets/`,
> `wizard_template.json`, `tutorials/first_launch.json`) **après** que les Phases
> 11 et 12 soient livrées. Statut **Blocked** tant que ces dépendances ne sont
> pas figées. Une fois les Phases 11 et 12 stables, M100.34 passera à **Ready**
> avec une mise à jour de scope.

### Phase 11 — Volumes 3D

| Ticket | Titre | Dépendances | Statut |
|--------|-------|-------------|--------|
| M100.40 | Mesh Insert Foundation + Cave Tool | M100.1, M100.2, M100.5, M100.9, M100.11, M100.12, M100.17, M100.35 | Draft |
| M100.41 | Overhang Cliff Tool | M100.40 | Draft |
| M100.42 | Natural Arch Tool | M100.40 | Draft |
| M100.43 | Dungeon Portal System — ⚠️⚠️ opcodes `INSTANCE_ENTER_REQUEST/RESPONSE` + migration DB `dungeons` + bump `kProtocolVersion` | M100.40 | Draft |
| M100.44 | VMap Bridge & Phase 11 Validation — ⚠️⚠️⚠️ bump format `.vmap` + lock-step client/server | M100.35, M100.36, M100.37, M100.38, M100.39, M100.40, M100.41, M100.42, M100.43 | Draft |

### Phase 12 — Accessibilité éditeur

| Ticket | Titre | Dépendances | Statut |
|--------|-------|-------------|--------|
| M100.45 | Simple/Advanced Mode + Tool Parameter Presets | M100.1, M100.6, M100.7, M100.9, M100.13, M100.17, M100.35–43 | Draft |
| M100.46 | Zone Presets Library | M100.1, M100.2, M100.35–43, M100.45 | Draft |
| M100.47 | Tooltips & F1 Doc | M100.1, M100.45 | Draft |
| M100.48 | Zone Validation Service | M100.1, M100.5, M100.9, M100.13, M100.40, M100.41, M100.42, M100.43, M100.47 | Draft |
| M100.49 | Tutoriel interactif & Diagnostic | M100.45, M100.46, M100.47, M100.48 | Draft |
| M100.50 | Quick Start Wizard | M100.45, M100.46, M100.47, M100.48, M100.49 | Draft |
| M100.51 | Deployment Pipeline CI — ⚠️ infra staging (pas d'opcode mais déploiement automatisé serveur) | M100.43, M100.44, M100.48 | Draft |

## Ordre d'implémentation recommandé

L'ordre **logique** — dans lequel chaque ticket peut être livré sans bloquer le
suivant — est :

```
Phase 1 → Phase 2 → Phase 2.5 → Phase 3 → Phase 4 → Phase 5 → Phase 6 →
Phase 7 → Phase 8 → Phase 9 → Phase 11 → Phase 12 → Phase 10
```

Trois remarques sur cet ordre :

1. **Phase 1 doit être livrée avant tout le reste** (shell, Command stack, lib
   `zone_builder` commune, modes caméra) sinon les phases suivantes n'ont pas
   de surface UI ni de sérialisation propre.
2. **M100.11 (`SurfaceType` + `SurfaceQuery`) est le pivot gameplay** ;
   M100.15, M100.16, M100.19, M100.26, M100.27, M100.33, M100.40 en dépendent.
   Ne pas le retarder.
3. **Phase 10 est livrée en dernier** parce que M100.34 (Save/Load Zone) doit
   connaître l'ensemble des fichiers binaires produits par l'éditeur, y compris
   ceux des Phases 11 (mesh_inserts.bin, dungeons.bin) et 12 (presets, wizard,
   tutorials).
4. **Phase 7 (saisons/météo/thermal) doit attendre la végétation (M100.18)**
   pour pouvoir générer la shade map à partir de la canopée.

## Gates de déploiement serveur

Tous les tickets de M100 ne sont pas équivalents en termes d'impact serveur.
Trois niveaux de risque :

### Tier 1 — Relais simple (aucune logique gameplay serveur)

Un opcode est ajouté, le serveur le relaie sans validation ni état persistant.
Redéploiement master requis au merge.

| Ticket | Opcodes |
|--------|---------|
| M100.25 | `SeasonBroadcast` (master broadcast 60 s) |
| M100.26 | `WeatherBroadcast` (master broadcast 30 s, tirage RAM) |
| M100.32 | `InteractiveStateChange` + `InteractiveStateBroadcast` + `InteractiveStateSync` (relais pur, état RAM par zone) |

### Tier 2 — Handler gameplay + migration DB + bump `kProtocolVersion`

Premier vrai handler gameplay côté master + table SQL nouvelle. Redéploiement
master requis, migration à rejouer au boot. Lock-step client/master nécessaire
au minimum (le shard reste optionnel à ce ticket).

| Ticket | Impact serveur |
|--------|----------------|
| M100.43 | `INSTANCE_ENTER_REQUEST/RESPONSE` (validation level / faction / quest / day-night) ; `engine/server/migrations/00XX_dungeons.sql` ; bump `kProtocolVersion`. Réponse `NotImplemented` pour l'allocation d'instance shard (follow-up CMANGOS.19). |

### Tier 3 — Lock-step client + server obligatoire (bump format binaire serveur)

Le serveur lit un format binaire éditeur (`.vmap`) qui change. Les deux côtés
doivent être déployés simultanément ; un mismatch hash → tile rejetée
(sécurité version). Pas de hot-reload : l'extraction `.vmap` est offline.

| Ticket | Impact serveur |
|--------|----------------|
| M100.44 | Format `.vmap` v(N) → v(N+1) : section `AuxiliaryColliders` + champ `meshInsertsBinHash` (SHA-256). Outil `lcdlln_vmap_extractor` étendu (offline). Runtime serveur intègre les colliders dans LOS / GetHeight / Raycast. **7 tests d'intégration end-to-end** bloquants. |

### Hors gates serveur (mais infra de déploiement modifiée)

| Ticket | Impact infra |
|--------|--------------|
| M100.51 | Workflows GitHub Actions, scripts shell, CLI `zone_validator_cli`. Automatise le bake `.vmap` + sync DB `dungeons_staging` + cold restart shard staging + smoke tests + rollback auto. **Aucun nouvel opcode**, mais demande des secrets staging (SSH, DB). |

### Bumps de format binaire (hors `.vmap`)

| Format | Bump | Ticket | Reader rétro-compat |
|--------|------|--------|--------------------|
| `instances/water.bin` | v1 → v2 | M100.36 | Oui (v1 lu, sea level défaut 50 m) |
| `instances/water.bin` | v2 → v3 | M100.37 | Oui (v2 lu, nouveaux champs ocean défaut + `isOcean=false`) |
| `splines.bin` | v1 → v2 | M100.30 | Oui (v1 lu, `kitMode=0` par défaut) |
| `atmosphere.json` | v1 → v2 | M100.23 | Migration auto au chargement |
| `atmosphere.json` | v2 → v3 | M100.24 | Migration auto au chargement |

## Risques techniques connus

| Risque | Ticket | Atténuation |
|--------|--------|-------------|
| Budget perf foliage interaction (< 0.5 ms sur 100k instances) | M100.21 | Buffer GPU compact, 32 influences max par chunk, shader simplifié, mesure obligatoire avec `engine::core::Profiler`. |
| Transitions saisonnières fluides | M100.25 | LUT 4×N + interpolation linéaire sur 2 jours in-game. Pas de blend per-asset, on bascule des paramètres globaux. |
| Génération automatique de proxies de collision sur meshes complexes | M100.12 | Trois niveaux explicites (capsule / convex hull / triangle mesh). Heuristique d'auto-fit + édition manuelle obligatoire pour les meshes irréguliers. |
| Latence réseau de l'animation des objets interactifs | M100.32 | Animation locale immédiate à la décision client. Réception distante avec interpolation de phase d'animation. Aucune validation serveur. |
| Régénération LOD coûteuse en édition continue | M100.8 | Régénération en arrière-plan + LOD0 immédiat ; LODs N>0 retardés au commit du brushstroke. |
| Cohérence des coutures inter-chunks en peinture splat | M100.10 | Gather sur le chunk voisin lu en lecture seule, écriture des deux côtés dans la même commande. |
| **Bug terrain invisible** (`docs/INVESTIGATION_terrain_invisible.md`) | M100.35–39 | Invariant strict documenté dans M100.35 v2 : pas de bascule caméra auto, pas de modification du frustum cull bypass mode éditeur, pas de modification de `m_noUserTextures`. Toutes les preview overlay sont 2D `ImGui` ou mesh additif blended (lecture seule sur depth). Tests bloquants : l'outil reste fonctionnel quand le terrain est en fallback orange. |
| **Consolidation heightmap zone-complète RAM** (~104 MB en `float`) | M100.36, M100.38, M100.39 | `ConsolidatedHeightGrid` partagée par M100.36/38/39 (introduite en M100.36 ou en M100.38 si M100.36 ne la promeut pas). Charger une seule fois par sim, libérer après. Si la zone n'est pas résidente, `TerrainDocument::EnsureChunkResident()` charge en bloquant. |
| **Déterminisme des simulations particle-based** (érosion) | M100.38, M100.39 | RNG seeded explicite, parallélisation sous contrainte de byte-equality avec single-thread. Test `Test_Simulation_Deterministic_SameInputSameOutput` bloquant. |
| **Cohabitation érosion ↔ rivières existantes** | M100.38, M100.39 | Warning UI : « tu as érodé après le watershed → re-lance le watershed pour adapter les rivières ». Pas de re-sim auto (l'utilisateur tranche). |
| **Mesh inserts ne creusent pas la heightmap** | M100.40–43 | Anchoring point d'entrée explicite (mesh par-dessus, pas à travers). Splat patch circulaire camouflage la transition. Server invisible jusqu'à M100.44. |
| **Lock-step client+server VMap Bridge** | M100.44 | Format `.vmap` v(N+1) bumpé. Hash SHA-256 anti-mismatch. Bake offline obligatoire. Pas de hot-reload. Déploiement coordonné master + shard. |
| **Migration 13 outils vers Simple/Advanced sans regression** | M100.45 | Backward compat : outil non-migré reste en Advanced par défaut. JSON preset validation (champ manquant → default, pas de crash). Persistent prefs `user_prefs.json` en write atomique (`.tmp` + rename). |
| **Performance ZoneValidator sur grandes zones** (10×10 km, 100 inserts) | M100.48 | Cible < 2 s pour validation complète. Mode background incrémental optionnel (rules réévaluées au commit du brushstroke). Persistance des issues ignorées via `user_prefs.json`. |
| **Reproductibilité par seed (zone presets / wizard)** | M100.46, M100.50 | Byte-equal test obligatoire : même preset + même seed → même `terrain.bin` + `splat.bin` + `instances/*.bin`. Substitutions `{{var}}` et `$auto_*` strictement déterministes. |
| **Rate-limit + secrets CI** | M100.51 | `.github/state/last_deploy.json` committé (rate limit 5 min). Secrets : `STAGING_SSH_KEY`, `STAGING_HOST`, `STAGING_DB_PASSWORD`. Rotation manuelle quarterly. Bump format binaire détecté → require manual approval. |

## Contrats partagés (référencés transversalement)

### Formats binaires

| Contrat | Défini dans | Consommé par |
|---------|-------------|--------------|
| `TerrainChunk` (heightmap binaire `terrain.bin`) | M100.5 | M100.6, M100.7, M100.8, M100.9, M100.13, M100.27, M100.34, M100.35, M100.36, M100.37, M100.38, M100.39, M100.44 |
| `terrain_lods.bin` | M100.8 | M100.5 (consommateur runtime), tous tickets terrain via hook `OnCommit` |
| Splat-map binaire `splat.bin` | M100.9 | M100.10, M100.11, M100.27, M100.34, M100.37 (plage auto), M100.40 (camouflage cave) |
| `surface_table.json` + enum `SurfaceType` | M100.11 | M100.15, M100.16, M100.19, M100.26, M100.29, M100.30, M100.33, M100.34, M100.40 |
| `*.collision.bin` (format proxy) | M100.12 | M100.17, M100.30, M100.32, M100.34, M100.40 (mesh insert collision) |
| `instances/water.bin` v3 | M100.13 (v1) → M100.36 (v2 + ocean) → M100.37 (v3 + ocean enrichi + `isOcean`) | M100.14, M100.15, M100.34, M100.36, M100.37 |
| `instances/foliage.bin` | M100.18 | M100.19, M100.20, M100.21, M100.27, M100.34 |
| `wind_zones.bin` | M100.20 | M100.21 |
| `fog_volumes.bin` | M100.22 | M100.23, M100.34 |
| `atmosphere_zones.bin` + `atmosphere.json` v3 | M100.23 (v2), M100.24 (v3) | M100.24, M100.34 |
| `instances/hazards.bin` | M100.16 | M100.34 |
| `instances/zones.bin` | M100.28 | M100.26, M100.34 |
| `instances/interactives.bin` | M100.32 | M100.34 |
| `weather_modifiers.json` | M100.26 | M100.11, M100.27, M100.33 |
| Shade map binaire `shade.bin` | M100.27 | M100.27 (runtime), M100.34 |
| Thermal map (calculée client RAM, **pas persistée**) | M100.27 | M100.33 |
| `splines.bin` v2 | M100.29 (v1) → M100.30 (v2 + bridge/wall kits) | M100.34 |
| `instances/mesh_inserts.bin` | M100.40 | M100.41, M100.42, M100.43, M100.44, M100.34 (ré-export) |
| `mesh_inserts_collision.bin` | M100.40 | M100.44, M100.34 (ré-export) |
| `instances/dungeons.bin` | M100.43 | M100.44, M100.51 (`dungeon_db_sync`), M100.34 (ré-export) |
| Format `.vmap` v(N+1) (section `AuxiliaryColliders` + `meshInsertsBinHash`) | M100.44 | M100.51, runtime serveur (LOS / GetHeight / Raycast) |

### Structures C++ et services partagés

| Contrat | Défini dans | Consommé par |
|---------|-------------|--------------|
| `OceanSettings { seaLevelMeters }` | M100.36 (champ minimal) | M100.37 (étend avec couleur, turbidité, vagues, enabled), M100.38 (sea level pour stop érosion sous mer) |
| `WaterDocument::GetOcean() / SetOceanSettings()` | M100.36 | M100.37 |
| `ConsolidatedHeightGrid` (assembly RAM zone-complète) | M100.36 (créée pour D8) ou M100.38 (promue si M100.36 ne le fait pas) | M100.36, M100.38, M100.39 |
| `SparseChunkDeltas` (pattern d'écriture multi-chunks avec parité couture) | M100.35 | M100.36 (carving), M100.37 (smoothing/falaises), M100.38, M100.39 |
| `BilinearGradientSample` / `BilinearDistribute` | M100.38 | M100.39 |
| `SimulationProgressModal` (factoring UI) | M100.38 | M100.39 |
| `WindParams` constant buffer | M100.20 | M100.21 |
| `EditorOverlayPass` (passe Vulkan dédiée à l'éditeur) | M100.12 | M100.40+ (gizmo mesh insert, overlay côte, etc.) |
| `MeshInsertSystem` (data model, I/O, streaming, gizmo) | M100.40 | M100.41 (overhang), M100.42 (arch), M100.43 (dungeon portal) |
| `ZonePresetExecutor` | M100.46 | M100.50 (resolver wizard → preset → executor) |
| `HelpContentStore` + `RichTooltipWidget` | M100.47 | M100.49 (overlay tutorial réutilise infra) |
| `OverlayGuidanceSystem` | M100.49 | (futur — réutilisable pour onboarding additionnel) |
| `ZoneValidator` (mode CLI + library) | M100.48 | M100.51 (wrapper `zone_validator_cli`), M100.49 (diagnostic réutilise les règles) |

### Protocoles réseau

| Contrat | Défini dans | Consommé par (relais) |
|---------|-------------|----------------------|
| `SeasonBroadcast` (opcode) | M100.25 | Master `SeasonBroadcaster` (60 s) |
| `WeatherBroadcast` (opcode) | M100.26 | Master tirage aléatoire (5 min) + broadcast (30 s) |
| `InteractiveStateChange` + `InteractiveStateBroadcast` + `InteractiveStateSync` (3 opcodes) | M100.32 | Master `InteractiveStateRelay` (RAM par zone) |
| `INSTANCE_ENTER_REQUEST` + `INSTANCE_ENTER_RESPONSE` (2 opcodes) | M100.43 | Master `InstanceEnterHandler` (validation gameplay, réponse `NotImplemented` pour allocation shard jusqu'à CMANGOS.19) |

## Format des tickets

Chaque ticket suit le contrat de la section 7 du prompt directeur (titre, résumé,
dépendances, portée, spec fonctionnelle, spec technique, diff CMake, schéma
d'interaction, critères d'acceptation, tests, hors scope). Les fichiers C++ /
shaders sont décrits **complets** (pas de patch/diff). Seul le `CMakeLists.txt`
racine est modifié sous forme de blocs `before` / `after`.

Les tickets qui exposent une surface ImGui ont un fichier compagnon
`visuals/M100.x-Nom.html` reproduisant la mise en page exacte.

## Synthèse déploiement (bilan global)

> **Déploiement M100.** Cinq moments imposent un redéploiement serveur :
>
> - **Tier 1 (relais simple)** : M100.25 (Season), M100.26 (Weather),
>   M100.32 (Interactive). Master uniquement, opcodes nouveaux, aucune
>   logique. Déployable indépendamment.
> - **Tier 2 (handler + DB + protocole)** : M100.43 (Dungeon Portal). Master,
>   migration `dungeons` à rejouer, bump `kProtocolVersion`. Requiert
>   coordination client+master.
> - **Tier 3 (lock-step + format binaire serveur)** : M100.44 (VMap Bridge).
>   Master + shard, format `.vmap` bumpé, hash anti-mismatch, déploiement
>   simultané obligatoire.
>
> Tous les autres tickets sont **client/éditeur uniquement**. Voir la section
> *Gates de déploiement serveur* pour le détail par ticket.

---

*Dernière révision : 2026-05-12 — INDEX étendu aux 51 tickets (Phases 1, 2, 2.5, 3-12), statuts normalisés (Done/Ready/Draft/Blocked), gates de déploiement serveur hiérarchisés en 3 tiers, contrats partagés étendus aux structures C++ et services. M100.34 marqué Blocked en attente de Phases 11/12. M100.35 v1 archivée.*
