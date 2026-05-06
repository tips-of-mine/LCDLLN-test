# M100 — Plan d'exécution des 34 tickets de l'éditeur de monde 3D

> **Sortie de session de brainstorming** (2026-05-06).
> **Sujet** : comment réaliser l'intégralité des 34 tickets du milestone M100
> (`tickets/M100/`) vers du code mergé.

## 1. Contexte

Le commit `2050118` (« feat(tickets/M100): suite complete pour editeur monde 3D AAA + systemes environnementaux ») a livré les **34 specs** de M100 + 29 maquettes HTML compagnons sous `tickets/M100/`, mais explicitement **aucune ligne C++**. À ce jour :

- Les 34 tickets sont au statut « Ready » dans `tickets/M100/INDEX.md`.
- L'éditeur de monde existe déjà côté code (`engine/editor/WorldEditorImGui.cpp`, `WorldEditorSession.cpp`, terrain edit, splat, routes, vegetation placement, atmosphère, binaire `lcdlln_world_editor.exe`).
- Les specs M100 utilisent un namespace cible distinct (`engine::editor::world::*`) et un nouveau flag CLI (`--editor-world` ≠ `--world-editor` actuel).

L'objectif de ce document est de fixer la **stratégie d'exécution** pour ship les 34 tickets, pas de redéfinir leur contenu (déjà figé dans les specs individuels).

## 2. Décisions structurantes

| # | Question | Décision | Conséquence |
|---|---------|----------|-------------|
| 1 | Statut de M100 vs. éditeur existant | **Couche au-dessus** | On adapte ticket-par-ticket, pas de refonte. Beaucoup de tickets sont partiellement faits ; l'audit doit le révéler. |
| 2 | Stratégie d'attaque | **Audit complet d'abord, build ensuite** | Aucun code C++ écrit avant que l'audit fichier-par-fichier soit livré. |
| 3 | Granularité de l'audit | **Niveau fichier** | Pour chaque ticket : table des fichiers exigés vs. existants, verdict (done / partiel / vide), effort estimé. Pas de descente au symbole. |
| 4 | Cadence d'exécution post-audit | **1 PR par phase** (10 PRs au total) | Les 34 tickets sont bundlés selon les 10 phases de l'INDEX. PR potentiellement grosses, acceptées comme tel. |
| 5 | Critères de « done » par phase | **CI vert + conformité spec** | Build OK Windows + Linux, tests unitaires verts, fichiers livrés conformes au spec. Pas de gate manuel ni de smoke ni de round-trip imposé hors de ce que les specs exigent déjà. |
| 6 | Exécution de l'audit lui-même | **Sous-agents en parallèle, 1 par phase** | 10 sous-agents `Explore` produisent chacun la sous-section de leur phase, je consolide en main session. |

## 3. Architecture du workflow

```
[Audit complet]  →  [10 PRs phase-par-phase]  →  [Done]
       ↓                       ↓
 gap-analysis.md         1 PR par phase
 (livrable principal)    M100.1-4, M100.5-8, ...
                         CI green = mergeable
                         Notification redeploy si phase 7/9
```

**Trois livrables documentaires :**

1. `docs/superpowers/specs/2026-05-06-m100-execution-design.md` — ce document (sortie de brainstorming, décisions de méthode).
2. `docs/superpowers/audits/2026-05-06-m100-gap-analysis.md` — l'audit fichier-par-fichier des 34 tickets vs. code existant. Livrable principal de l'étape audit.
3. Plan d'implémentation produit par `writing-plans` (étape suivante de cette session).

## 4. Format de l'audit

Pour chacun des 34 tickets, une sous-section :

```markdown
### M100.x — <Titre>

**Phase** : <n>
**Dépendances** : <liste>
**Pivot/réseau** : <"PIVOT M100.11" OU "redéploiement serveur requis" OU "—">

**Fichiers spec vs. code existant**

| Fichier exigé par le spec        | Statut          | Note                              |
|----------------------------------|-----------------|-----------------------------------|
| engine/editor/world/Shell.h      | absent          | à créer                           |
| engine/editor/world/Shell.cpp    | absent          | à créer                           |
| (équivalent existant)            | WorldEditorImGui.cpp existe | refactor partiel possible |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : XS / S / M / L / XL
**Risques** : <texte court si applicable>
```

En tête du document, **un tableau récap** de tous les tickets pour vue d'ensemble :

```markdown
| Ticket | Phase | Verdict  | Effort | Bloque | Notes      |
|--------|-------|----------|--------|--------|------------|
| M100.1 | 1     | partiel  | M      | —      | refactor   |
| M100.2 | 1     | vide     | M      | —      |            |
| ...    |       |          |        |        |            |
```

**Calibration de l'effort :**

| Tag | Adaptation/code | Cas typique |
|-----|-----------------|-------------|
| XS  | <50 lignes      | rebranchement, renommage |
| S   | ~200 lignes     | nouveau panneau ImGui simple, glue de classe |
| M   | ~500 lignes     | nouvelle classe + sérialisation + tests |
| L   | ~1500 lignes    | nouveau système (saisons, météo, fog) |
| XL  | >3000 lignes    | shader pass, format binaire neuf, refactor lourd |

L'effort cumulé par phase guide le découpage si nécessaire (cf. §6, risque 2).

## 5. Exécution de l'audit

**Dispatch parallèle, un sous-agent `Explore` par phase :**

- Phase 1 (M100.1–4) — Fondations
- Phase 2 (M100.5–8) — Terrain
- Phase 3 (M100.9–12) — Splat / Surfaces / Collision
- Phase 4 (M100.13–16) — Hydrologie & Hazards
- Phase 5 (M100.17–21) — Placement & Végétation
- Phase 6 (M100.22–24) — Atmosphère & Brouillard
- Phase 7 (M100.25–28) — Saisons / Météo / Thermal
- Phase 8 (M100.29–31) — Routes / Ponts / Structures
- Phase 9 (M100.32) — Objets interactifs
- Phase 10 (M100.33–34) — Polissage final

**Prompt sous-agent (modèle) :**
- Lire les specs de la phase X (`tickets/M100/M100.<a>-*.md` à `M100.<b>-*.md`).
- Pour chaque ticket, recenser la liste des fichiers exigés par la section « Spec technique ».
- Croiser avec le code existant (`engine/editor/`, `engine/render/`, `engine/core/`, `engine/server/`, etc.).
- Produire la sous-section au format §4 ci-dessus.
- Ne pas écrire de code ni de patch. Audit en lecture seule.

**Consolidation en main session :**
- Je récupère les 10 sous-sections.
- Je rédige le tableau récap en tête.
- Je marque les flags transverses : pivot M100.11 (impacte 6 tickets aval), redéploiement serveur (M100.25, .26, .32), tests round-trip listés dans les specs (M100.5, .9, .12, .16, .27, .32, .34).
- J'écris un bloc « Risques détectés à l'audit » en fin de doc si l'audit a découvert des conflits.
- Commit `docs(audit): gap analysis M100 (34 tickets)`.

## 6. Risques structurants & atténuations

**1. M100.11 (`SurfaceQuery`) — pivot gameplay.**
Cité dans l'INDEX comme bloquant pour 6 tickets aval (M100.15, .16, .19, .26, .27, .33). L'audit le flagge `PIVOT` et lui consacre une section approfondie. Si son contrat (enum `SurfaceType` + `surface_table.json` + signature `SurfaceQuery::At(...)`) dérive entre l'audit et la phase 3 build, j'arrête avant phase 4 et j'escalade.

**2. Diff PR phase trop gros (>5000 lignes).**
Phase 5 (5 tickets végétation) et phase 7 (4 tickets saisons + météo + thermal + zones) sont à risque. Atténuation : pour les phases dont l'effort cumulé prévu à l'audit dépasse ~3000 lignes, je découpe en **2 PRs séquentielles dans la même phase** sans changer le contrat de cadence (ex. phase 5a `M100.17–18` puis phase 5b `M100.19–21`). Le découpage est annoncé dans le résumé de PR.

**3. Contrat de parité éditeur ↔ client jamais testé end-to-end.**
Avec validation CI-only, le risque est qu'un format binaire écrit par l'éditeur ne soit pas lu correctement par le client. Atténuation faible : tests round-trip côté CI quand le ticket le mentionne (M100.5, .9, .12, .16, .27, .32, .34). L'audit vérifie que ces tests existent ou sont planifiés ; sinon flag.

**4. Fenêtres `lcdlln_world_editor.exe` cassées par M100.1.**
M100.1 ajoute `engine::editor::world::WorldEditorShell` mais on est en mode couche au-dessus. Risque : le nouveau code casse l'existant. Atténuation : phase 1 garde l'ancien `WorldEditorImGui` opérationnel, le nouveau shell vit côté à côté pendant la phase. La cohabitation est résolue plus tard si nécessaire (potentiellement phase 10 polissage).

## 7. Déploiement serveur

| Phase | Tickets | Déploiement |
|-------|---------|-------------|
| 1, 2, 3, 4, 5, 6, 8, 10 | 30 tickets | ✅ client/éditeur uniquement, pas de redéploiement serveur |
| 7 | M100.25, .26 | ⚠️ **redéploiement serveur master requis** — nouveaux opcodes `SeasonBroadcast` (M100.25) et `WeatherBroadcast` (M100.26) |
| 9 | M100.32 | ⚠️ **redéploiement serveur master requis** — nouvel opcode `InteractiveStateChange` |

Pour chaque phase à wire-breaking, je flagge dans la description GitHub de la PR + dans le résumé chat de la PR (règle CLAUDE.md). Lock-step client + serveur sinon `BAD_REQUEST`.

## 8. Gestion d'escalade

Trois catégories d'imprévus, règles claires :

1. **Audit révèle un ticket « spec impossible »** (format binaire incompatible avec l'existant, contrat pivot M100.11 qui rentre en conflit avec une convention du jeu actuel) → flag dans la section audit du ticket + arrêt avant la phase build correspondante + question utilisateur.

2. **PR phase fail CI persistant** (>2 tentatives de fix) → arrêt, écriture du blocage, demande utilisateur. Pas de `--no-verify` ni de désactivation de tests.

3. **Conflit avec `main`** (autre PR mainline mergée pendant le travail) → rebase standard, pas d'escalade sauf conflit non-trivial.

## 9. Hors scope

- Refactor de l'éditeur existant au-delà de ce qu'exigent les specs M100. La règle est « couche au-dessus », pas « grand nettoyage ».
- Implémentation des tickets M43 (panneaux ImGui spécialisés) ou M44 (infrastructure serveur) — distincts par construction (cf. INDEX).
- Réécriture du contrat réseau au-delà des 3 opcodes mentionnés (M100.25, .26, .32).
- Validation manuelle / smoke / playtest par phase. Hors scope par décision §2 question 5.
- Optimisation perf au-delà des budgets explicitement écrits dans les specs (ex. < 0.5 ms pour 100k instances de foliage interaction, M100.21).

## 10. Étapes suivantes

1. Lecture et validation de ce document par l'utilisateur.
2. Invocation de `superpowers:writing-plans` pour produire le **plan d'implémentation** détaillé : étape 1 = audit (dispatch des 10 sous-agents + consolidation), étapes 2–11 = les 10 PRs de build phase-par-phase, en s'appuyant sur les chiffres effort/verdict produits par l'audit.
3. Exécution du plan via `superpowers:executing-plans`.
