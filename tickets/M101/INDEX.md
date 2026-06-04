# Milestone M101 — Éditeur de routines visuel nodal (type n8n)

> **Cluster M101.** Distinct de M100 (éditeur de monde 3D + simulation
> environnementale) et de M43 (anciens panneaux ImGui). M101 livre **l'éditeur de
> routines visuel nodal** : un panneau dockable à droite de l'éditeur de monde
> permettant de **programmer visuellement des routines** par graphe de nœuds, à la
> manière de n8n / Blueprint / StateTree.
>
> **Le seul système réellement absent** identifié par `AUDIT_22_SYSTEMES.md`
> (système #23). Tous les autres systèmes de référence UE5 sont déjà Couverts ou
> Partiels dans M100.
>
> **Pré-requis livrés :** M43.4 (ImGui foundation), M100.1 (World Editor
> Bootstrap), M100.2 (Command Stack & Undo/Redo).

## Principes architecturaux structurants

| Couche | Rôle pour les routines | Ne fait PAS |
|--------|------------------------|-------------|
| **Éditeur (`lcdlln_world_editor`)** | Écrit le graphe de routine (nouveau segment de chunk **additif** `routines.bin`). UI nodale ImGui, validation, mode Playtest F5. | Pas de pipeline de rendu séparé. N'exécute pas la logique gameplay « pour de vrai » hors Playtest (qui utilise le code client de prod). |
| **Client (`engine_core` + Vulkan)** | Interprète les graphes **zone/gameplay** au runtime via une **VM pure** (lib `routine_vm`, aucune dépendance Vulkan/GLFW). Charge `routines.bin` par le même chemin de streaming que les autres segments. | Ne simule pas l'IA PNJ (elle vit côté shard). |
| **Shard (`shardd`)** | Pour la cible PNJ : consomme des `EventAIRow` **générés depuis le graphe** par `RoutineToEventAI`, via l'`EventAIRuntime` existant. | N'introduit **pas** une seconde IA concurrente d'`EventAI`. |
| **Master (`masterd`)** | **Relaie uniquement** les changements d'état (gabarit `GameEventPayloads.h`). | Aucune exécution de graphe. Aucune validation gameplay propriétaire. |

**Data-driven, zéro recompilation.** Un graphe est un **artefact JSON** édité par
l'éditeur, sérialisé dans `routines.bin`, et **interprété** au runtime. Aucun nœud
ne génère ni ne compile du C++. Même philosophie que l'`EventAI` data-driven déjà
en place côté shard.

**Contrat de parité éditeur ↔ client.** Format binaire unique (`routines.bin`),
VM unique, **évaluation déterministe** (mêmes entrées → même sortie). Tests de
round-trip obligatoires (édité → écrit → relu → réinterprété = état identique),
exécutables headless en CI Linux **et** Windows.

`engine_core` (client + éditeur) **ne linke pas** la cible serveur. La VM est une
lib pure réutilisable par le client ; le shard ne linke que la sérialisation
(`routine_graph`) + le pont `RoutineToEventAI`.

## Décision de format actée : deux types de graphes distincts

> Le prompt directeur laissait ce choix à Hubert. **Décision prise le 2026-06-03 :**
> **deux types de graphes distincts**, partageant la sérialisation mais avec deux
> schémas et deux VM séparés.

| Type de graphe | Modèle de référence | VM | Cible |
|----------------|---------------------|-----|-------|
| `npc_routine` | StateTree (états hiérarchiques + sélection par utilité + tick rate par AI-LOD + séparation capteurs/actions) | `NpcRoutineEvaluator` → génère des `EventAIRow` (cible A) | Comportements PNJ (M101.7, **Blocked**) |
| `zone_event` | Blueprint event graph (nœuds Event racines, execution wires + data pins typés, graphe attaché à une cellule de zone) | `ZoneEventVm` (exécution data-flow client) | Événements de zone / gameplay (M101.8) |

**Justification.** Deux VM simples et bornées (un schéma par cible) sont plus
faciles à tester, à valider en CI et à raisonner qu'une VM unifiée à sémantique
double (un nœud pouvant être state OU event). La sérialisation JSON et le segment
`routines.bin` sont **communs** : un champ `RoutineGraphKind` discrimine. On garde
ainsi un format binaire unique tout en isolant les sémantiques d'exécution.

## Conventions de cet INDEX

### Statuts

| Statut | Sens |
|--------|------|
| **Done** | Spec figée + code mergé sur `main` + CI verte. |
| **Ready** | Spec figée, dépendances livrées, prêt à coder, aucun blocage. |
| **Draft** | Spec proposée, **en attente de validation Hubert** (feu vert ticket par ticket). |
| **Blocked** | Spec figée mais bloqué par une dépendance non résolue (raison documentée). |

> **Tous les tickets M101 sont en `Draft`** tant que Hubert ne les a pas validés
> un par un (règle « Stop & validate » du prompt). À la validation, les tickets
> dont toutes les dépendances sont livrées passent à `Ready`. **M101.7** reste
> `Blocked` indépendamment de la validation (infra PNJ absente).

### Phases logiques (4)

| # | Phase | Tickets | But |
|---|-------|---------|-----|
| 1 | Modèle de données & VM | M101.1–3 | Modèle de graphe + sérialisation JSON, VM déterministe (lib pure), segment `routines.bin` additif. |
| 2 | UI nodale (ImGui) | M101.4–6 | Panneau nodal dockable, palette + inspecteur, validation visuelle. |
| 3 | Bibliothèques de nœuds | M101.7–8 | Nœuds PNJ (Blocked) et nœuds zone/gameplay. |
| 4 | Intégration & tests | M101.9–11 | Mode Playtest F5, round-trip & tests CI headless, doc F1 + tooltips. |

## Liste complète des tickets (11)

> **Statut au 2026-06-03 (post-merge #806).** Les tickets **Done** ont été
> clôturés et déplacés vers `tickets/issues/M101.N-*_Issue.md` (convention dépôt :
> le .md quitte le dossier milestone). Restent ici : M101.7 (PARTIAL/Blocked),
> M101.8 et M101.9 (différés tant que leurs dépendances M100 ne sont pas livrées).
> Source de vérité des statuts : `tickets/TICKETS_STATUS.md`.

### Phase 1 — Modèle de données & VM

| Ticket | Titre | Dépendances | Statut |
|--------|-------|-------------|--------|
| M101.1 | Modèle de graphe & sérialisation JSON | — (lib pure) | **Done** (→ issues/) |
| M101.2 | VM d'interprétation déterministe (lib pure `routine_vm`) | M101.1 | **Done** (→ issues/) |
| M101.3 | Segment de chunk `routines.bin` (extension additive) | M101.1, M100.3 (Done) | **Done** (→ issues/) |

### Phase 2 — UI nodale (ImGui)

| Ticket | Titre | Dépendances | Statut |
|--------|-------|-------------|--------|
| M101.4 | Panneau nodal dockable (canvas, pan/zoom, CRUD) | M101.1, M100.1 (Done), M100.2 (Done) | **Done** (→ issues/) |
| M101.5 | Palette de nœuds + inspecteur de propriétés | M101.4 | **Done** (→ issues/) |
| M101.6 | Validation visuelle du graphe | M101.4, M101.1 | **Done** (→ issues/) |

### Phase 3 — Bibliothèques de nœuds (les deux cibles)

| Ticket | Titre | Dépendances | Statut |
|--------|-------|-------------|--------|
| M101.7 | Nœuds **PNJ** (génère `EventAIRow`) | M101.1, M101.2, `EventAI` (Done) ; **Role Registry + Smart Objects (ABSENTS)** | **PARTIAL / Blocked** (`RoutineToEventAI` livré en #806) |
| M101.8 | Nœuds **zone/gameplay** | M101.1, M101.2, M100.16 (TODO), M100.28 (TODO), M100.32 (TODO) | **TODO** (différé) |

### Phase 4 — Intégration & tests

| Ticket | Titre | Dépendances | Statut |
|--------|-------|-------------|--------|
| M101.9 | Mode Playtest (F5) — exécution via code client de prod | M101.2, M101.8, M100.33 (TODO, Playtest F5) | **TODO** (différé) |
| M101.10 | Round-trip & tests CI (headless, Linux + Windows) | M101.1, M101.2, M101.3 | **Done** (→ issues/) |
| M101.11 | Documentation F1 + tooltips | M101.4, M101.5, M100.47 (Done) | **Done** (→ issues/) |

## Ordre d'implémentation recommandé

```
M101.1 → M101.2 → M101.3 → M101.4 → M101.5 → M101.6 → M101.8 → M101.10 → M101.9 → M101.11
                                                          ↘ M101.7 (Blocked, hors séquence)
```

Remarques :

1. **M101.1 d'abord** : tout dépend du modèle de graphe + sérialisation.
2. **M101.2 (VM) et M101.3 (segment binaire) peuvent être parallèles** une fois
   M101.1 figé ; M101.10 (round-trip) exige les deux.
3. **M101.7 (PNJ) est hors séquence** car `Blocked` : il sera débloqué quand
   l'infra Role Registry / Smart Objects sera livrée par un futur milestone PNJ.
   La partie « génération d'`EventAIRow` » est livrable indépendamment et sert de
   preuve d'intégration.
4. **M101.9 (Playtest) après M101.8** : il exécute des graphes `zone_event`
   réels dans le code client de prod (pas de chemin « preview » séparé).

## Décisions laissées à Hubert (posées, non tranchées)

| Décision | Où | Reco |
|----------|----|------|
| **Autorité shard ↔ client** sur l'état déclenché par une routine `zone_event` (qui fait foi si client et shard divergent ?) | M101.8, M101.9 | Posée. Reco : client-autoritaire pour les objets de zone locaux (cohérent M100.32), master relais pur. À trancher si un cas d'usage compétitif émerge. |
| **Déblocage de M101.7** : créer un milestone PNJ (Role Registry, Smart Objects, Utility AI runtime) avant ou après M101 ? | M101.7 | Posée. Reco : livrer M101.1–6 + M101.8–11 d'abord (cible zone fonctionnelle), puis le milestone PNJ, puis débloquer M101.7. |
| **Tick rate AI-LOD** pour `npc_routine` : réutiliser l'enum `AiLod` du ticket CHAR-MODEL.37 (non codé) ou en définir un dans `routine_vm` ? | M101.2, M101.7 | Posée. Reco : aligner sur `AiLod` de CHAR-MODEL.37 quand il sera codé ; en attendant, M101.2 expose un `tickRateHz` paramétrable neutre. |

## Gates de déploiement serveur

| Ticket | Impact serveur | Gate |
|--------|----------------|------|
| M101.1–6 | Aucun (modèle, VM cliente, UI éditeur, validation). | ✅ client/éditeur uniquement. |
| M101.7 (Blocked) | À son déblocage : le shard charge des `EventAIRow` générés → **redéploiement shard requis** quand livré. | ⚠️ (futur, à la levée du blocage). |
| M101.8 | Les nœuds « broadcast » **réutilisent les opcodes existants** M100.25 (`SeasonBroadcast`), M100.26 (`WeatherBroadcast`), M100.32 (`InteractiveStateChange`). **Aucun nouvel opcode** introduit par M101.8 lui-même. | ✅ pas de redéploiement propre à M101.8 (les opcodes réutilisés ont déjà leur gate dans M100). |
| M101.9–11 | Aucun. | ✅ client/éditeur uniquement. |

> **Synthèse déploiement M101.** Le cluster est **client/éditeur uniquement**, à
> **une exception future** : M101.7, une fois débloqué, ajoutera un chargement de
> routines PNJ côté shard (redéploiement shard). Aucun nouvel opcode n'est créé par
> M101 ; les nœuds broadcast s'appuient sur les opcodes M100 existants.

## Contrats partagés (référencés transversalement)

### Formats

| Contrat | Défini dans | Consommé par |
|---------|-------------|--------------|
| Modèle `RoutineGraph`/`RoutineNode`/`RoutinePin`/`RoutineLink` + JSON | M101.1 | M101.2, M101.3, M101.4, M101.5, M101.6, M101.7, M101.8 |
| Segment `routines.bin` (+ `ChunkSegment::Routines`, `kChunkMetaHasRoutines = 1u << 7`) | M101.3 | M101.9 (chargement client), M101.10 (round-trip), M100.34 (ré-export — note de compat) |
| Lib pure `routine_vm` (`ZoneEventVm`, `NpcRoutineEvaluator`) | M101.2 | M101.7, M101.8, M101.9, M101.10 |

### Structures C++ et services partagés

| Contrat | Défini dans | Consommé par |
|---------|-------------|--------------|
| `RoutineNodeSchema` (bornage des types de nœuds par cible, versionné) | M101.1 | M101.5 (palette), M101.6 (validation), M101.7, M101.8 |
| `RoutineToEventAI` (graphe `npc_routine` → `EventAIRow`) | M101.7 | shard `EventAIRuntime` (existant) |
| `RoutineGraphPanel` (canvas) + intégration `CommandStack` | M101.4 | M101.5, M101.6 |

## Format des tickets

Chaque ticket calque `tickets/M100/M100.32-InteractiveProps.md` et ajoute la
section obligatoire `## Code runtime référencé` (fichiers client/shard/réseau
réels étendus ou appelés, étiquetés « étendu » / « appelé en lecture » / « hook
manquant → Blocked »). Voir `tickets/DEFINITION_OF_DONE.md` pour la DoD globale ;
chaque DoD M101 ajoute les **trois axes** : CI Linux verte, CI Windows verte, test
de round-trip headless passant — plus le checkpoint de validation manuelle Hubert.

> **Note convention chemins.** La DoD globale mentionne `/engine` ; l'arbre réel
> du dépôt est `src/`. Les tickets M101 utilisent les chemins réels `src/`,
> `game/data/`, `tools/`, conformément à l'ensemble du cluster M100.

---

*Index produit le 2026-06-03. Cluster en attente de validation Hubert, ticket par
ticket (« Stop & validate »). Aucun code de production écrit.*
