# CMANGOS Gap Analysis — Design

> **Date** : 2026-05-08
> **Auteur** : Claude (brainstorming session)
> **Type** : audit / gap analysis (pas une spec d'implémentation moteur)
> **Branche** : `claude/cmangos-gap-analysis`

## 1. Problème à résoudre

Le projet LCDLLN possède 44 tickets dans `tickets/CMANGOS/` (issus de
l'analyse `CMANGOS_ANALYSIS.md`) qui décrivent des patterns/abstractions
à porter ou adapter depuis cmangos-tbc. L'INDEX existant
(`tickets/CMANGOS/CMANGOS.INDEX.md`) propose un classement par priorité
P1-P4 et un ordre d'implémentation théorique, **mais** :

- Cet ordre est théorique : il ne tient pas compte de l'état réel du code
  engine (certains tickets sont déjà partiellement implémentés).
- Aucun document n'identifie les chevauchements avec les milestones
  existantes M00-M44/M100.
- Il n'existe pas de vue claire sur les **risques d'intégration** par
  ticket (wire-breaking, migration DB, redéploiement serveur).
- Sans cette analyse, le risque est de re-développer des choses déjà
  faites, ou d'enchaîner les tickets dans un ordre qui crée des
  blocages.

L'objectif de cet audit est de produire un **état des lieux exhaustif et
honnête** des 44 tickets CMANGOS face au code actuel, et un **ordre
d'intégration recalibré** basé sur cet état.

## 2. Livrable attendu

### Structure cible

```
docs/superpowers/audits/2026-05-08-cmangos-gap-analysis/
├── INDEX.md                       # synthèse globale + tableau récap
├── CMANGOS.01.md                  # une fiche par ticket
├── CMANGOS.02.md
├── ...
└── CMANGOS.45.md                  # 44 fiches au total (numérotation non contiguë)
```

### Périmètre protégé (lecture seule)

- `CMANGOS_ANALYSIS.md` (racine, 778 lignes) — source des tickets
- `tickets/CMANGOS/*.md` (44 tickets + INDEX) — tickets eux-mêmes
- `docs/superpowers/audits/2026-05-06-m100-gap-analysis.md` — précédent
  audit (utilisé comme référence de format uniquement)
- Toutes les autres specs/plans/audits dans `docs/superpowers/`

### Hors périmètre

- Toute modification de code engine (`engine/...`)
- Toute modification des tickets sources (`tickets/CMANGOS/...`)
- Toute écriture dans `docs/superpowers/specs/` ou `docs/superpowers/plans/`
  autre que ce design doc et un éventuel plan d'implémentation futur

## 3. Structure d'une fiche `CMANGOS.NN.md`

Chaque fiche suit ce template à 9 sections (voir exemple complet dans
la conversation de brainstorming) :

```
# CMANGOS.NN — <Module> (<keywords>)

> **Ticket source** : [chemin relatif vers tickets/CMANGOS/...]
> **Priorité** : Px — <one-liner>
> **Cible** : master / shard / cross / client / outil

## 1. Statut implémentation       (✅ / 🟡 / ❌ / 🔄)
## 2. Preuves dans le code        (fichiers + lignes existants/manquants)
## 3. Recouvrement milestones     (binaire : couvert / non couvert)
## 4. Écart vs spec CMANGOS       (ce qui manque concrètement)
## 5. Effort estimé               (S / M / L / XL)
## 6. Valeur joueur/serveur       (Critique / Élevée / Moyenne / Faible)
## 7. Dépendances bloquantes      (autres tickets ou milestones requis)
## 8. Risque / piège ⚠️           (wire-breaking / migration DB / etc.)
## 9. Recommandation finale       (✅ / 🔧 / ⏸ / 🚫 / 🟢)
```

Convention emoji (alignée avec `tickets/CMANGOS/CMANGOS.INDEX.md` qui
utilise déjà ✅ ⌛ ❌) :

| Statut | Effort | Reco |
|---|---|---|
| ✅ Fait | S — ≤1 PR | ✅ Faire en l'état |
| 🟡 Partiel | M — 2-3 PR | 🔧 Adapter et faire |
| ❌ Absent | L — 1 sprint | ⏸ Reporter |
| 🔄 En cours | XL — multi-sprints | 🚫 Ne pas faire |
| | | 🟢 Déjà couvert |

## 4. Structure de l'`INDEX.md`

Sections :
1. **En-tête** : date, périmètre, méthode, sources
2. **Comment lire cet audit** : 3 légendes (Statut, Effort, Reco)
3. **Synthèse — répartition** : compteurs (X fait / Y partiel / Z absent…),
   remplis à la fin de l'audit
4. **Top reco court terme** : 3-5 tickets prioritaires à attaquer avec
   rationale en 1 phrase, rempli à la fin
5. **Tableau récap 44 tickets** : 8 colonnes
   (#, Module, Cible, Statut, Effort, Valeur, Reco, Risque), avec lien
   vers chaque fiche
6. **Ordre d'intégration recommandé (recalibré)** : 5 phases avec
   dépendances vérifiées (≠ ordre théorique de l'INDEX existant)
7. **Notes globales** : observations transversales (ex. "la plupart des
   P2 dépendent de CMANGOS.13 Database — à faire tôt")

## 5. Méthodologie d'audit par fiche

### Étape 1 — Lecture intégrale du ticket source

Lire tout le `tickets/CMANGOS/CMANGOS.NN_*.md`, en notant : livrables
nommés, opcodes/protocole, tables DB, dépendances explicites, cible.

### Étape 2 — Recherche d'évidence dans le code (3 passes)

- **Passe A** : match direct par nom de fichier/classe (Glob + Grep)
- **Passe B** : match par concept fonctionnel (mots-clés du ticket)
- **Passe C** : opcode / DB schema (`grep kOpcode<Name>`, `glob migrations/*.sql`)

Verdict :
- 0 hit pertinent = ❌ Absent
- ≥ 1 livrable trouvé mais incomplet = 🟡 Partiel + liste des manques
- Tous les livrables présents et reliés = ✅ Fait
- Branche/PR active sur le sujet (`git log --all`) = 🔄 En cours

### Étape 3 — Cross-référence avec milestones M00-M44/M100

Grep `tickets/M*/INDEX.md` et `tickets/M*/M*.md` avec mots-clés du
ticket. Statut **binaire** (B2 choisi par l'utilisateur) :
"couvert par milestone" / "non couvert".

### Étape 4 — Heuristiques d'effort

| Signal | Effort |
|---|---|
| Aucun fichier nouveau, juste modifs dans 1-2 fichiers existants | S |
| 1-3 nouveaux composants, pas de migration DB ni opcode | M |
| 4+ nouveaux composants OU migration DB OU nouvel opcode | L |
| Refonte transversale (hiérarchie Object→Unit, UpdateFields) | XL |

### Étape 5 — Heuristiques de valeur

| Signal | Valeur |
|---|---|
| "Déblocant", "MVP critique", sécurité ouverte | Critique |
| Gameplay visible joueur (chat, combat, quête, loot, mail) | Élevée |
| Backoffice / outils GM / metrics / admin | Moyenne |
| Optimisation interne, edge cases, low-pop only | Faible |

### Étape 6 — Détection systématique des risques

Checklist exhaustive (au moins une coche → mention obligatoire en
Section 8 de la fiche) :

| Test | Risque marqué |
|---|---|
| Ajoute/modifie un opcode ? | ⚠️ Wire-breaking → redéploiement lock-step |
| Bump `kProtocolVersion` ? | ⚠️ Wire-breaking |
| Ajoute table/colonne DB ? | ⚠️ Migration DB requise |
| Ajoute handler master ou shard ? | ⚠️ Redéploiement serveur (côté concerné) |
| Touche AUTH/Session/AccountStore ? | ⚠️ Risque sécurité — review obligatoire |
| Ajoute clé `config.json` lue serveur ? | ⚠️ Config serveur à provisionner |
| Suppose une nouvelle dépendance externe ? | ⚠️ Interdit par AGENTS.md sans accord |

Cette checklist colle à la règle CLAUDE.md du projet sur le
redéploiement serveur — chaque fiche dira explicitement si
redéploiement requis.

### Étape 7 — Recommandation finale (flowchart)

```
Statut == ✅ Fait ?
  → 🟢 Déjà couvert (fiche pour mémoire)
Cible == client only ET ticket parle de patch serveur ?
  → 🚫 Ne pas faire (hors-scope)
Valeur == Critique ET aucune dépendance non livrée ?
  → ✅ Faire en l'état
Spec OK mais détails LCDLLN à ajuster ?
  → 🔧 Adapter et faire
Valeur == Faible OU dépendances non livrées ?
  → ⏸ Reporter
```

### Étape 8 — Honnêteté épistémique

Si une information ne peut pas être déterminée avec certitude après
les passes A/B/C, l'écrire explicitement plutôt qu'inventer :
- "🟡 Partiel _(évaluation provisoire — vérification manuelle
  recommandée sur `XYZ.cpp:120-180`)_"
- "Effort : M ?? — incertain, dépend de si `loot_template` existe déjà"

Vu le souhait utilisateur "bien fait pas vite", on lève la main plutôt
que de produire une fausse certitude.

## 6. Plan d'exécution

### Approche : itératif par priorité (B)

**6 commits prévus**, avec point de validation utilisateur après chaque
batch (sauf Batch 0).

| Batch | Tickets | Durée | Commit message |
|---|---|---|---|
| 0 — Squelette | INDEX vide + design doc | 10 min | `docs(audit): squelette CMANGOS gap analysis` |
| 1 — P1 | 5 fiches (.01-.05) | 45 min | `docs(audit): CMANGOS gap analysis P1 (5 tickets)` |
| 2a — P2 part 1 | 12 fiches (.06-.17) | 1h | `docs(audit): CMANGOS gap analysis P2 part 1` |
| 2b — P2 part 2 | 11 fiches (.18-.28) | 1h | `docs(audit): CMANGOS gap analysis P2 part 2` |
| 3 — P3 | 14 fiches (.29-.42) | 1h | `docs(audit): CMANGOS gap analysis P3` |
| 4 — P4 | 2 fiches (.44-.45) | 15 min | `docs(audit): CMANGOS gap analysis P4` |
| 5 — Consolidation | INDEX rempli | 30 min | `docs(audit): CMANGOS gap analysis consolidation` |

**Total estimé** : ~4h30 cumulées sur 2-3 sessions.

### Points de validation utilisateur

- 🛑 Après Batch 1 (P1) : critique du format/profondeur sur 5 fiches
  pour ajuster avant les 39 autres
- 🛑 Optionnel : après chaque batch P2/P3 si l'utilisateur souhaite
  contrôler la qualité progressivement

### Règles d'exécution

- **Commit propre obligatoire** : aucune session ne se termine avec un
  fichier audit à moitié rempli — soit le batch en cours est commité,
  soit son travail est rollback
- **Branche dédiée** : `claude/cmangos-gap-analysis` (créée depuis
  l'état clean de `claude/inspiring-swanson-291de6` au 2026-05-08)
- **Pas de redéploiement serveur** : l'audit est purement
  documentaire — chaque commit portera la mention
  `Déploiement : ✅ docs uniquement`

## 7. Critères de succès

Cet audit est un succès si :

- ✅ Les 44 fiches existent et suivent le template à 9 sections
- ✅ L'INDEX.md contient les compteurs synthèse remplis, le tableau
  récap complet, l'ordre recalibré, et 3-5 top recos
- ✅ Aucune fiche ne contient de placeholder ("TODO", "TBD") sauf
  marquage explicite "incertain — vérification manuelle recommandée"
- ✅ Les risques wire-breaking / migration DB / redéploiement sont
  remontés systématiquement
- ✅ Aucun fichier hors `docs/superpowers/audits/2026-05-08-cmangos-gap-analysis/`
  n'est créé ou modifié (sauf ce design doc + un futur plan
  d'implémentation)
- ✅ L'utilisateur peut **prendre une décision** "quel ticket attaquer
  ensuite" en lisant uniquement l'INDEX.md (sans avoir à lire les 44
  fiches)

## 8. Suite après l'audit

Une fois cet audit livré, deux suites possibles selon la décision
utilisateur :

- **Option A** : créer un plan d'implémentation
  (`docs/superpowers/plans/`) pour le top 3-5 des recos prioritaires,
  via la skill `writing-plans`
- **Option B** : laisser l'audit comme référence et l'utilisateur
  pioche dedans au fil des sprints, sans plan global

Cette décision est repoussée à la fin de l'audit.

---

*Design validé en brainstorming le 2026-05-08. Modifications futures :
éditer ce fichier puis incrémenter la mention "v2" en en-tête.*
