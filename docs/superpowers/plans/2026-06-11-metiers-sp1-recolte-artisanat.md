# Métiers SP1 — Récolte + artisanat de bout en bout : Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Chantier n°3 de la liste validée. Les systèmes serveur M36.1/M36.2 (GatheringSystem, CraftingSystem) tournent et tous les kinds wire existent (64-68 récolte, 70-77 artisanat) — mais : (a) les **nodes de récolte ne sont pas répliqués** (le client ne peut ni les voir ni connaître leurs entityId — « no replication » assumé par le header d'origine), (b) **aucune donnée de nodes** n'existe en zone, (c) le client **n'émet aucun** des messages (HarvestRequest, CraftRecipeListRequest, CraftRequest…), (d) les présentateurs `HarvestCastBarPresenter` et `CraftingUiPresenter` sont orphelins (audit).

**Architecture:** Réplication des nodes par le canal snapshot existant SANS bump wire : les nodes entrent dans la grille d'intérêt et `TryBuildSnapshotEntity` gagne une branche node avec `archetypeId = kGatheringNodeArchetypeBase (1 000 000) + typeId` (constante partagée dans ReplicationTypes.h ; le format u32 ne change pas). `stateFlags = kEntityStateDead` encode « épuisé » (label grisé). Côté client : labels flottants (pas de mesh V1 — noms FR par type), touche **E** (interact, déjà réservée) sur le node le plus proche à portée quand aucun interactible local n'a la priorité, `HarvestCastBar` câblée (UIModel.harvest déjà alimenté), panneau d'artisanat **K** (CraftingUi câblée : onglets métiers → CraftRecipeListRequest, ligne → sélection, bouton → CraftRequest, cast bar + qualité M36.3).

**Livraison : 1 PR `metiers-sp1`** (serveur léger + client), stackée sur party-sp1. Déploiement : ⚠️ shardd à redéployer (réplication des nodes) — porté par la fenêtre lock-step v12 du chantier combat ; pas de bump de version.

### Task 1 — Données : `game/data/zones/zone_0/gathering_nodes.json`
Schéma de `GatheringSystem::LoadZoneNodes` : `{ "zoneId": 0, "nodes": [ { "id": "...", "type_id": 1-4, "position": [x,y,z] } ] }`. 3 nodes près du spawn (minerai type 1, herbe type 2, arbre type 3).

### Task 2 — Réplication des nodes (serveur, pas de bump)
- `ReplicationTypes.h` : `inline constexpr uint32_t kGatheringNodeArchetypeBase = 1000000u;` (doc : plage réservée, jamais un archétype de créature).
- `GatheringSystem.h` : accesseur `const std::vector<ResourceNodeRuntimeState>& Nodes() const`.
- `ServerApp::InitGathering` : enregistre chaque node dans la grille de sa zone (`UpsertEntity`) → ils entrent dans l'AoI/replicatedEntityIds.
- `TryBuildSnapshotEntity` : branche node (position, hp 1/1, `stateFlags` = dead si épuisé, archetypeId = base + typeId).

### Task 3 — Client : émissions réseau
`GameplayUdpClient` : `SendHarvestRequest(clientId, nodeEntityId)`, `SendHarvestCancelRequest(clientId)`, `SendCraftRecipeListRequest(clientId, professionKey)`, `SendCraftRequest(clientId, recipeId)`, `SendCraftCancelRequest(clientId)` (encodeurs existants). `UIModelBinding::SelectCraftRecipe(rowIndex)` (mutation + notif Crafting).

### Task 4 — Client : récolte
- Rendu : labels flottants des nodes dans la boucle nameplates (archetypeId ≥ base) — noms FR par type (map locale V1 : 1 « Filon de minerai », 2 « Herbes », 3 « Arbre », 4 « Carcasse » — les noms en data = amélioration future) + « [E] » si à portée (~5 m) et disponible ; grisé si épuisé. Les nodes sont EXCLUS du rendu mesh des mobs (RecordRemoteAvatars saute archetypeId ≥ base) et du ciblage Tab/clic combat.
- Interaction : E (gardes inGame habituelles) — si aucun interactible local actif (`m_interactableInRange < 0`) et node disponible le plus proche < 5 m → `SendHarvestRequest`. Le serveur valide tout ; l'annulation au mouvement est déjà serveur.
- `HarvestCastBarPresenter` câblé (Init/viewport/observer/rendu barre depuis GetState()).

### Task 5 — Client : artisanat (touche K)
`CraftingUiPresenter` câblé + panneau ImGui : onglets métiers (`crafting.professions`) → `SendCraftRecipeListRequest` ; lignes de recettes → `SelectCraftRecipe` ; bouton Fabriquer (actif si sélection) → `SendCraftRequest` ; cast bar `craftFillFraction` ; libellé qualité M36.3 (couleur du présentateur) ; statut/compétence affichés. K déclaré au registre de binds (libre, vérifié).

### Task 6 — Docs + PR
CODEBASE_MAP, push, PR stackée + draft CI cumul si la pile n'est pas encore mergée.

## Self-review
- Bout en bout : voir un node → E → cast bar → loot en inventaire (InventoryDelta déjà routé) + skill-up (ProfessionUpdate déjà routé) → K → liste de recettes → fabrication → cast bar + qualité.
- Pas de bump wire ; serveur touché = réplication des nodes uniquement (additif).
- AoEPreview/SkillSystem non concernés. Touches : E réutilisée (priorité interactibles), K nouvelle (libre).
