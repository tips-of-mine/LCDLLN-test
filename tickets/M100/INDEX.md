# Milestone M100 — Éditeur de monde 3D AAA et systèmes environnementaux

> **Cluster M100.** Distinct de M43 (panneaux ImGui spécialisés) et de M44 (infrastructure
> serveur). M100 livre **l'éditeur de monde 3D** et **toute la simulation
> environnementale gameplay côté client** (surfaces, hazards, saisons, météo,
> ombre thermique, interaction joueur ↔ végétation, objets interactifs sobres).
>
> **Pré-requis livré :** M43.4 — ImGui foundation.

## Principes architecturaux structurants

| Couche | Rôle | Ne fait PAS |
|--------|------|-------------|
| **Client (`engine_core` + Vulkan)** | Tout le rendu, toute la simulation gameplay locale, lecture des chunk packages, déclenchement réseau. | Rien d'autre que ce qui est listé. |
| **Éditeur (`world_editor_app`, `--editor`)** | Création/modification de la carte. Écriture des chunk packages. Mode playtest F5 utilisant le code client de prod. | Pas de pipeline de rendu séparé. Pas de format "preview". Pas de logique gameplay propriétaire. |
| **Serveur (`server_app`)** | Auth + relais des messages multi-clients (positions, actions, broadcasts saison/météo). | Aucune simulation environnementale. Aucun chargement de splat / shade / thermal. Aucun calcul de surface. |

**Contrat de parité éditeur ↔ client.** Tout ce que l'éditeur écrit est lu et rendu
identiquement par le client en mode jeu normal. Pipeline Vulkan unique. Format
binaire unique. Shaders uniques. Versions strictement alignées. Tests de round-trip
obligatoires.

## Liste des tickets

| Ticket | Titre | Phase | Dépendances directes | Statut |
|--------|-------|-------|----------------------|--------|
| M100.1  | World Editor Bootstrap | 1 — Fondations | M43.4 | Done (CI pending) |
| M100.2  | Command Stack & Undo/Redo | 1 — Fondations | M100.1 | Done (CI pending) |
| M100.3  | Zone Builder Library Extraction | 1 — Fondations | M100.1 | Done (CI pending) |
| M100.4  | Editor Camera Modes | 1 — Fondations | M100.1 | Done (CI pending) |
| M100.5  | Heightmap Data Structure | 2 — Terrain | M100.3, M100.4 | Done (CI pending — drawcall livré par PR Terrain Chunk Runtime) |
| M100.6  | Terrain Sculpting Brushes | 2 — Terrain | M100.2, M100.5 | Done (CI pending) |
| M100.7  | Terrain Stamps & Procedural Generators | 2 — Terrain | M100.6 | Done (CI pending) |
| M100.8  | Terrain LOD Regeneration | 2 — Terrain | M100.5 | Done (CI pending) |
| M100.9  | Splat Map System | 3 — Splat / Surfaces / Collision | M100.5 | Done (CI pending — drawcall livré par PR Terrain Chunk Runtime) |
| M100.10 | Splat Painting Brushes | 3 — Splat / Surfaces / Collision | M100.2, M100.9 | Done (CI pending) |
| M100.11 | Surface Material System & SurfaceQuery (client) | 3 — Splat / Surfaces / Collision | M100.9 | Done (CI pending) |
| M100.12 | Collision Proxy System | 3 — Splat / Surfaces / Collision | M100.1 | Done (CI pending) |
| M100.13 | Water Surfaces (Lakes & Rivers) | 4 — Hydrologie & Hazards | M100.5, M100.6 | Done (CI pending) |
| M100.14 | Water Render Pass | 4 — Hydrologie & Hazards | M100.13 | Ready |
| M100.15 | Water Surface Hook (Wading & Swimming) | 4 — Hydrologie & Hazards | M100.11, M100.13 | Ready |
| M100.16 | Hazard Volume System | 4 — Hydrologie & Hazards | M100.11 | Ready |
| M100.17 | Easy Placement Tool | 5 — Placement & Végétation | M100.2, M100.5, M100.12 | Ready |
| M100.18 | Vegetation Library & Density Painting | 5 — Placement & Végétation | M100.5, M100.17 | Ready |
| M100.19 | Procedural Forest & Field Tools | 5 — Placement & Végétation | M100.11, M100.18 | Ready |
| M100.20 | Vegetation Wind Animation | 5 — Placement & Végétation | M100.18 | Ready |
| M100.21 | Vegetation Player Interaction Shader | 5 — Placement & Végétation | M100.20 | Ready |
| M100.22 | Volumetric Fog Volumes | 6 — Atmosphère & Brouillard | M100.4 | Ready |
| M100.23 | Distance Fog & Height Fog Tuning | 6 — Atmosphère & Brouillard | M100.22 | Ready |
| M100.24 | Sun, Sky & Probes Editor | 6 — Atmosphère & Brouillard | M100.23 | Ready |
| M100.25 | Season System & Time-of-Year | 7 — Saisons / Météo / Thermal | M100.24 | Ready |
| M100.26 | Weather System & Dynamic Surface Modifiers | 7 — Saisons / Météo / Thermal | M100.11, M100.25 | Ready |
| M100.27 | Shade Map & Thermal Map (`ThermalQuery`) | 7 — Saisons / Météo / Thermal | M100.18, M100.25, M100.26 | Ready |
| M100.28 | Gameplay Zones & Weather Zones | 7 — Saisons / Météo / Thermal | M100.26 | Ready |
| M100.29 | Spline Tool & Roads | 8 — Routes / Ponts / Structures | M100.10, M100.17 | Ready |
| M100.30 | Bridges & Modular Walls | 8 — Routes / Ponts / Structures | M100.17, M100.29 | Ready |
| M100.31 | Hamlet Generator | 8 — Routes / Ponts / Structures | M100.17 | Ready |
| M100.32 | Interactive Props (Doors, Windows, Trapdoors, Simple Chests) | 9 — Objets interactifs | M100.12, M100.17 | Ready |
| M100.33 | Footstep Audio Surface Hook & Playtest Mode (F5) | 10 — Polissage final | M100.11, M100.16, M100.26, M100.27 | Ready |
| M100.34 | Selection, Layers, Minimap & Save/Load Zone | 10 — Polissage final | M100.5, M100.9, M100.16, M100.27, M100.28, M100.32 | Ready |

## Ordre d'implémentation recommandé

L'ordre des tickets reflète l'ordre d'implémentation. Trois remarques :

1. **Phase 1 doit être livrée avant tout le reste** (shell, Command stack, lib zone_builder
   commune, modes caméra) sinon les phases suivantes n'ont pas de surface UI ni de
   sérialisation propre.
2. **M100.11 (`SurfaceType` + `SurfaceQuery`) est le pivot gameplay** ; M100.15, M100.16,
   M100.19, M100.26, M100.27, M100.33 en dépendent. Ne pas le retarder.
3. **Phase 7 (saisons/météo/thermal) doit attendre la végétation (M100.18)** pour
   pouvoir générer la shade map à partir de la canopée.

## Risques techniques connus

| Risque | Ticket | Atténuation |
|--------|--------|-------------|
| Budget perf foliage interaction (< 0.5 ms sur 100k instances) | M100.21 | Buffer GPU compact, 32 influences max par chunk, shader simplifié, mesure obligatoire avec `engine::core::Profiler`. |
| Transitions saisonnières fluides | M100.25 | LUT 4×N + interpolation linéaire sur 2 jours in-game. Pas de blend per-asset, on bascule des paramètres globaux. |
| Génération automatique de proxies de collision sur meshes complexes | M100.12 | Trois niveaux explicites (capsule / convex hull / triangle mesh). Heuristique d'auto-fit + édition manuelle obligatoire pour les meshes irréguliers. |
| Latence réseau de l'animation des objets interactifs | M100.32 | Animation locale immédiate à la décision client. Réception distante avec interpolation de phase d'animation. Aucune validation serveur. |
| Régénération LOD coûteuse en édition continue | M100.8 | Régénération en arrière-plan + LOD0 immédiat ; LODs N>0 retardés au commit du brushstroke. |
| Cohérence des coutures inter-chunks en peinture splat | M100.10 | Gather sur le chunk voisin lu en lecture seule, écriture des deux côtés dans la même commande. |

## Contrats partagés (référencés transversalement)

| Contrat | Défini dans | Consommé par |
|---------|-------------|--------------|
| `TerrainChunk` (heightmap binaire) | M100.5 | M100.6, M100.7, M100.8, M100.9, M100.13, M100.27, M100.34 |
| `surface_table.json` + enum `SurfaceType` | M100.11 | M100.15, M100.16, M100.19, M100.26, M100.29, M100.30, M100.33, M100.34 |
| Shade map binaire `shade.bin` | M100.27 | M100.27, M100.34 |
| Thermal map (calculée client, pas persistée) | M100.27 | M100.33 |
| `*.collision.bin` (format proxy) | M100.12 | M100.17, M100.30, M100.32, M100.34 |
| `instances/hazards.bin` | M100.16 | M100.34 |
| `instances/interactives.bin` | M100.32 | M100.34 |
| `instances/zones.bin` | M100.28 | M100.26, M100.34 |
| `weather_modifiers.json` | M100.26 | M100.11, M100.27, M100.33 |
| Protocole `InteractiveStateChange` | M100.32 | (relais serveur) |
| Protocole `SeasonBroadcast` | M100.25 | (relais serveur) |
| Protocole `WeatherBroadcast` | M100.26 | (relais serveur) |
| Splat-map binaire `splat.bin` | M100.9 | M100.10, M100.11, M100.27, M100.34 |
| `WindParams` constant buffer | M100.20 | M100.21 |

## Format des tickets

Chaque ticket suit le contrat de la section 7 du prompt directeur (titre, résumé,
dépendances, portée, spec fonctionnelle, spec technique, diff CMake, schéma
d'interaction, critères d'acceptation, tests, hors scope). Les fichiers C++ /
shaders sont décrits **complets** (pas de patch/diff). Seul le `CMakeLists.txt`
racine est modifié sous forme de blocs `before` / `after`.

Les tickets qui exposent une surface ImGui ont un fichier compagnon
`visuals/M100.x-Nom.html` reproduisant la mise en page exacte.

## Déploiement

> **Déploiement** : ⚠️ M100 introduit trois nouveaux protocoles relais
> (`InteractiveStateChange` — opcode à allouer dans le ticket M100.32,
> `SeasonBroadcast` — M100.25, `WeatherBroadcast` — M100.26). **Redéploiement
> serveur (master + shard) requis** au moment où chaque ticket réseau est mergé
> (M100.25, M100.26, M100.32). Tous les autres tickets de M100 sont
> client/éditeur-only et ne nécessitent pas de redéploiement serveur.
