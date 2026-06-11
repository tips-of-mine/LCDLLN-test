# Combat SP4 — Menace répliquée, threat meter, FX d'auras : Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Clore le chantier combat (spec §7) : la table de menace des mobs est répliquée aux clients (threat meter du panneau J enfin alimenté — `AdvancedCombatPresenter::UpdateThreat` attend ces données depuis M39.4), et les auras gagnent un retour visuel in-world (halo coloré aux pieds des entités, via l'`AuraFXSystem` orphelin).

**Architecture:** L'IA d'agro serveur (poursuite, leash, threat tables, evade) existe depuis les Waves 8/19 et fonctionne — SP4 n'ajoute côté serveur QU'UN push `ThreatUpdate` (wire v11→v12, kind 85) throttlé par mob. Côté client : routage vers `AdvancedCombatPresenter::UpdateThreat` + rendu du threat meter dans le panneau J (la struct `threatMeter` existe), et câblage d'`AuraFXSystem` (couche données : Sync depuis `UIModel::entityAuras`) avec rendu écran-espace (cercle coloré aux pieds, couleur de `ResolveAuraVisuals` — pas de nouvel asset).

**Décisions d'exécution :**
- **AoEPreviewSystem reste non câblé** (déviation spec §7 assumée) : il prévisualise un placement souris→sol, or aucun sort des kits validés n'est `AreaAtTarget` (Nova = autour de soi). Il sera câblé avec le premier sort ciblé-sol. Documenté ici plutôt qu'un câblage artificiel.
- Le push ThreatUpdate est throttlé à 1/s par mob ET seulement si la table a changé ; mob mort/évadé → push d'une liste vide (le client efface).

**Livraison : 1 PR `combat-sp4`** (serveur+client, base main après merge de #878/#879). ⚠️ wire v12, lock-step.

### Task 1: Wire v11→v12 — ThreatUpdate (kind 85)

ServerProtocol.h/.cpp/Tests : `kProtocolVersion = 12` + historique ; kind `ThreatUpdate = 85` ; `struct ThreatWireEntry { EntityId playerEntityId; uint32_t threatValue; }` ; `struct ThreatUpdateMessage { EntityId mobEntityId; std::vector<ThreatWireEntry> entries; }` (payload : u64 + u8 count + n×12, liste complète idempotente, vide = effacement) ; Encode/Decode + roundtrip + tronqué.

### Task 2: Push serveur

ServerApp : `MobEntity` gagne `uint64_t lastThreatPushMs = 0; size_t lastThreatHash = 0;` (hash simple = somme entityId^threat). Dans le tick d'IA des mobs (ou TickAuras — choisir le site appelé ~1/s : `ResolveMobAiIntervalTicks`), pour chaque mob avec table non vide : si hash ≠ dernier ET now-last ≥ 1000 ms → `BroadcastThreatUpdate(mob)` (même périmètre d'intérêt que les auras). `ResetMobThreat`/mort → push vide immédiat.

### Task 3: Client — routage + threat meter

- UIModel : route `ThreatUpdate` → pour chaque entrée, résoudre le nom (joueur local = nom du perso ou « Vous » ; sinon displayName des remoteEntities, fallback "P<clientId>" impossible sans clientId → fallback "Joueur <entityId>") puis pousser au présentateur. Le présentateur n'est pas membre du binding → exposer les données : `UIModel.threatByMob[mobEntityId] = vector<{entityId, threat}>` + notif Combat ; Engine (observer) appelle `m_advancedCombat.UpdateThreat(...)` par entrée et `ClearThreat(mobId)` sur liste vide.
- Rendu : dans le panneau J (bloc SP2), section « Menace » si `threatMeterVisible` : par entrée `threatMeter[]` — nom, % (threatPercent), barre colorée selon `ThreatColor` (vert/jaune/rouge).

### Task 4: Client — AuraFXSystem câblé

- Engine : membre `m_auraFx` + Init/Shutdown (GameplayNet) ; chaque frame (bloc combat SP2) : `Sync(entityId, effets, pos lissée)` pour le joueur local + toutes les entités de `entityAuras` visibles (réutilise le `buildEffects` SP3-B — le factoriser en méthode/lambda partagée) ; `RemoveEntity` géré par Sync(nullptr) via la purge AoI existante ; rendu : pour chaque `GetAuras()` — `WorldToScreenPx(pos aux pieds)` → `fg->AddCircle` (rayon ~22 px, couleur glowColor, 2 px) + léger remplissage alpha.

### Task 5: Docs + PR

CODEBASE_MAP (section SP4 + clôture chantier combat), push, PR base main, CI, mention déploiement lock-step v12.

## Self-review
- Spec §7 : poursuite/leash/evade déjà serveur (constaté SP1/SP2) ; menace tickée = existante ; ThreatUpdate+meter = Tasks 1-3 ; AuraFX = Task 4 ; AoEPreview = déviation documentée. Multi-cibles d'agro : déjà géré par ThreatList (TopTarget).
- Types : threatValue u32 (= ThreatEntry.threat), EntityId u64 partout.
