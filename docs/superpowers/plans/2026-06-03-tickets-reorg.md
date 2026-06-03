# Nettoyage branches + réorganisation `tickets/` — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Nettoyer les branches git obsolètes et réorganiser `tickets/` de sorte que terminé / partiel / à-faire soit lisible d'un coup d'œil, avec clôture (Issue) des tickets réellement implémentés et un tableau de suivi.

**Architecture:** Travail 100 % sur fichiers Markdown sous `tickets/` + `docs/` (aucun code applicatif modifié), sur la branche `chore/tickets-reorg`. Vérification d'implémentation déléguée à des sous-agents par lots de milestones, qui renvoient un verdict structuré ; le verdict pilote la clôture/le déplacement.

**Tech Stack:** git, PowerShell/Bash, sous-agents (`Explore`/`general-purpose`), Markdown.

**Spec de référence :** [2026-06-03-tickets-reorg-design.md](../specs/2026-06-03-tickets-reorg-design.md)

**Déploiement :** ✅ aucun (docs/tickets uniquement).

---

## Référentiels réutilisés dans tout le plan

### R1 — Schéma de verdict (retour des sous-agents)

```json
{
  "code": "M06.1",
  "file": "tickets/M06/M06.1_SSAO_kernel_noise_generation.md",
  "title": "SSAO: kernel + noise generation",
  "statut": "DONE | PARTIAL | TODO",
  "preuves": ["engine/render/SsaoKernelNoise.cpp:1", "engine/Engine.cpp:NNN"],
  "manque": ["(si PARTIAL/TODO) livrable X absent du code"],
  "note": "1 phrase de justification"
}
```

### R2 — Prompt-type de sous-agent de vérification

> Tu reçois une liste de chemins de tickets Markdown (dossier `tickets/`). Pour
> CHAQUE ticket : lis-le, puis lis le code réel du dépôt (`engine/`, `game/`,
> `src/`, `tools/`, serveur master/shard) pour déterminer si ses **Livrables /
> Étapes / Critères DoD** sont implémentés. Verdict par ticket : `DONE` (tous
> livrables présents et corrects), `PARTIAL` (une partie présente, une partie
> manquante), `TODO` (rien de significatif). Donne des **preuves** sous forme
> `fichier:ligne`. Un ticket purement spec/docs est `DONE` si le document qu'il
> décrit existe. Ne modifie AUCUN fichier. Réponds STRICTEMENT au schéma R1
> (un objet par ticket).

### R3 — Gabarit d'Issue de clôture (`tickets/issues/<stem>_Issue.md`)

```markdown
# Issue: <CODE> — <Titre>

**Status:** Closed

---

## Rapport final

### 1) FICHIERS (preuves d'implémentation présentes dans le repo)
- <fichier:ligne>
- ...

### 2) COMMANDES
- (rappel build/run si pertinent, sinon « N/A — vérification statique »)

### 3) RÉSULTAT
- **Implémenté :** OUI — <résumé 1-2 phrases de la vérification code>

### 4) VALIDATION DoD
- <points du DoD couverts par le code observé>

---

## Ticket content (<CODE>)

<contenu intégral du ticket d'origine, copié tel quel>
```

### R4 — Encart d'état PARTIAL (inséré juste sous le titre H1 du ticket)

```markdown
> **État : PARTIEL** (vérifié 2026-06-03)
> - ✅ Fait : <liste>
> - ⬜ Manque : <liste>
> - Preuves : <fichier:ligne>
```

---

## Task 0 : Phase 0 — Nettoyage des branches

**Files:** aucun fichier modifié (opérations git locales).

- [ ] **Step 1 — Resynchroniser**

Run :
```bash
git fetch --prune
```
Expected : les remotes supprimés disparaissent (déjà fait au préalable, ré-exécuter est idempotent).

- [ ] **Step 2 — Lister les branches `gone` et leur état de merge**

Run (Bash) :
```bash
git for-each-ref --format='%(refname:short) %(upstream:track)' refs/heads \
  | grep '\[gone\]' | awk '{print $1}' > /tmp/gone.txt
echo "=== gone branches ==="; cat /tmp/gone.txt
echo "=== merged into main ==="; git branch --merged main | sed 's/^[* ]*//'
```
Expected : liste des branches `gone` + liste de celles strictement mergées.

- [ ] **Step 3 — Classer chaque branche `gone`**

Pour chaque branche de `/tmp/gone.txt`, déterminer si son travail est dans `main` :
```bash
while read b; do
  ahead=$(git rev-list --count main.."$b" 2>/dev/null)
  echo "$b  commits_uniques_vs_main=$ahead"
done < /tmp/gone.txt
```
Règle :
- `commits_uniques_vs_main=0` → mergée proprement → **supprimable** (`-d`).
- `>0` mais branche issue d'un squash-merge (remote supprimé après merge PR) → inspecter `git log --oneline main.."$b"` ; si les sujets correspondent à des commits déjà dans `main` (squash) → **supprimable** (`-D` après confirmation visuelle) ; sinon **conserver** et signaler.
- **Exclure** systématiquement `feat/prop-draw-distance` (worktree `LCDLLN-foret`).

- [ ] **Step 4 — Supprimer les branches confirmées mergées**

Run (pour les `-d` sûres ; lister explicitement les noms, pas de boucle aveugle) :
```bash
git branch -d <branche1> <branche2> ...
```
Pour les squash-merges confirmés visuellement à l'étape 3 :
```bash
git branch -D <branche-squash> ...
```
Expected : « Deleted branch … ». Ne JAMAIS `-D` une branche `ahead` non poussée.

- [ ] **Step 5 — Vérifier les conservées**

Run :
```bash
git branch -vv
```
Expected : restent `main`, les branches `ahead`/non poussées (`feat/online-presence-enriched`, `fix/props-collision-vertical-sweep`, specs locales), les branches à PR ouvert (remote présent), et `feat/prop-draw-distance` (worktree).

- [ ] **Step 6 — Rapport branches**

Produire dans le message un récapitulatif « Supprimées (N) / Conservées (N) + raison ». (Pas de commit — opérations locales.)

---

## Task 1 : Phase 1 — Normalisation (sur `chore/tickets-reorg`)

**Files:**
- Renommer : `tickets/CMANGOS/` → `tickets/SERVER-CORE/` (+ fichiers `CMANGOS.xx_*` → `SERVER-CORE.xx_*`)
- Déplacer : `tickets/M03/M03.0_ISSUE.md`, `tickets/M03/M03.1_ISSUE.md`, `tickets/annexe/STAB.13_*_Issue.md` → `tickets/issues/`
- Créer : `tickets/INVENTORY.tmp.md` (table de travail interne, supprimée en fin de Phase 4)

- [ ] **Step 1 — Renommer le dossier CMANGOS**

Run (Bash) :
```bash
git mv tickets/CMANGOS tickets/SERVER-CORE
for f in tickets/SERVER-CORE/CMANGOS.*.md; do
  git mv "$f" "${f/CMANGOS./SERVER-CORE.}"
done
ls tickets/SERVER-CORE | head
```
Expected : dossier `SERVER-CORE/` avec fichiers `SERVER-CORE.xx_*.md`.

- [ ] **Step 2 — Retirer le terme dans le contenu**

Pour chaque fichier de `tickets/SERVER-CORE/`, remplacer les occurrences du terme interdit (titre, références internes) par « SERVER-CORE » / « cœur serveur MMO ». Utiliser Grep pour lister les occurrences, puis Edit fichier par fichier (pas de sed aveugle qui casserait des URLs/historique).

Run de contrôle :
```bash
grep -rin "cmangos" tickets/SERVER-CORE || echo "OK: aucune occurrence"
```
Expected : « OK: aucune occurrence ».

- [ ] **Step 3 — Normaliser les clôtures inline**

Run (Bash), en adaptant le stem au nom réel du ticket d'origine :
```bash
git mv tickets/M03/M03.0_ISSUE.md "tickets/issues/M03.0_Camera_frustum_culling_mvp_Issue.md"
git mv tickets/M03/M03.1_ISSUE.md "tickets/issues/M03.1_Deferred_GBuffer_resources_geometry_pass_Issue.md"
git mv tickets/annexe/STAB.13_UI_Login_Register_Client_Missing_Issue.md tickets/issues/
ls tickets/issues | grep -E "M03.0|M03.1|STAB.13"
```
Expected : les 3 clôtures présentes dans `tickets/issues/`.

- [ ] **Step 4 — Construire l'inventaire ticket ↔ issue**

Générer `tickets/INVENTORY.tmp.md` : pour chaque ticket (hors `issues/`, hors `*INDEX*`, `AGENTS.md`, `DEFINITION_OF_DONE.md`), indiquer s'il a déjà une `*_Issue.md` de même stem. Les tickets **déjà clôturés** sont marqués DONE sans re-vérification ; les autres partent en Phase 2.

Run :
```bash
issues=$(find tickets/issues -name '*_Issue.md' -printf '%f\n' | sed 's/_Issue\.md$//')
find tickets -name '*.md' -not -path 'tickets/issues/*' \
  -not -name '*INDEX*' -not -name 'AGENTS.md' -not -name 'DEFINITION_OF_DONE.md' \
  | sort > /tmp/all_tickets.txt
wc -l /tmp/all_tickets.txt
```
Expected : liste complète des tickets à classer.

- [ ] **Step 5 — Commit Phase 1**

```bash
git add -A tickets/
git commit -m "chore(tickets): renomme CMANGOS->SERVER-CORE + normalise clôtures inline"
```

---

## Task 2 : Phase 2 — Vérification approfondie (sous-agents)

**Files:** lecture seule du code ; écriture interne `tickets/INVENTORY.tmp.md`.

- [ ] **Step 1 — Constituer les lots**

Grouper les tickets NON déjà clôturés par dossier/milestone (un lot ≈ 1 milestone, ou demi-milestone si >15 tickets). Objectif : lots cohérents pour qu'un sous-agent garde le contexte d'un sous-système.

- [ ] **Step 2 — Dispatcher les sous-agents par vagues**

Pour chaque lot, lancer un sous-agent (`Explore` pour repérage large, `general-purpose` si analyse fine nécessaire) avec le prompt **R2** et la liste de chemins du lot. Lancer plusieurs lots en parallèle par vague (plusieurs `Agent` dans un même message). Chaque agent renvoie les objets **R1**.

Règle de fiabilité : si un verdict DONE repose sur une preuve faible (1 symbole approximatif), re-vérifier ce ticket en lecture directe avant de le clôturer.

- [ ] **Step 3 — Agréger**

Consolider tous les verdicts dans `tickets/INVENTORY.tmp.md` (colonnes : code, fichier, statut, preuves, manque). Émettre un point d'avancement par vague (compteurs DONE/PARTIAL/TODO cumulés).

- [ ] **Step 4 — (pas de commit ici ; l'inventaire est un artefact de travail)**

---

## Task 3 : Phase 3 — Application des verdicts

**Files:**
- Créer : `tickets/issues/<stem>_Issue.md` pour chaque DONE
- Supprimer : le `.md` correspondant dans `Mxx/` pour chaque DONE
- Modifier : insertion encart R4 pour chaque PARTIAL
- Supprimer : dossiers milestone devenus vides

- [ ] **Step 1 — Clôturer les DONE**

Pour chaque ticket DONE : créer son Issue via le gabarit **R3** (rapport + contenu du ticket ré-embarqué), puis :
```bash
git rm "tickets/<Mxx>/<stem>.md"
```
Procéder par lots de milestone pour des commits lisibles.

- [ ] **Step 2 — Annoter les PARTIAL**

Pour chaque ticket PARTIAL : insérer l'encart **R4** juste sous le titre H1 (via Edit). Le ticket reste dans son dossier.

- [ ] **Step 3 — TODO : inchangés**

Aucune action sur les TODO (restent dans `Mxx`).

- [ ] **Step 4 — Supprimer les dossiers vides**

Run :
```bash
find tickets -type d -empty -not -path 'tickets/issues' -print
find tickets -type d -empty -delete
```
Expected : suppression des seuls dossiers milestone vidés par les clôtures.

- [ ] **Step 5 — Commit Phase 3 (par lots)**

```bash
git add -A tickets/
git commit -m "chore(tickets): clôture DONE (issues) + encarts PARTIAL + purge dossiers vides"
```

---

## Task 4 : Phase 4 — Tableau de suivi

**Files:**
- Créer : `tickets/TICKETS_STATUS.md`
- Supprimer : `tickets/INVENTORY.tmp.md`

- [ ] **Step 1 — Générer `tickets/TICKETS_STATUS.md`**

Structure :
```markdown
# Suivi des tickets — état au 2026-06-03

## Synthèse
| Statut | Nombre |
|--------|--------|
| DONE (clôturés) | N |
| PARTIAL | N |
| TODO | N |
| **Total** | N |

## Détail par milestone

### M06 — SSAO
| Ticket | Titre | Statut | Preuve / Issue | Manque |
|--------|-------|--------|----------------|--------|
| M06.1 | kernel + noise | DONE | issues/M06.1_..._Issue.md | — |
| ...
```
Remplir depuis `INVENTORY.tmp.md` (issues existantes incluses comme DONE).

- [ ] **Step 2 — Supprimer l'inventaire de travail**

```bash
git rm tickets/INVENTORY.tmp.md
```

- [ ] **Step 3 — Commit Phase 4**

```bash
git add -A tickets/
git commit -m "docs(tickets): tableau de suivi TICKETS_STATUS.md"
```

---

## Task 5 : Phase 5 — Livraison

- [ ] **Step 1 — Pousser la branche**

```bash
git push -u origin chore/tickets-reorg
```

- [ ] **Step 2 — Ouvrir la PR**

```bash
gh pr create --base main --head chore/tickets-reorg \
  --title "chore(tickets): nettoyage branches + réorganisation tickets/ (statuts + suivi)" \
  --body "<résumé phases + ligne Déploiement: ✅ aucun redéploiement serveur>"
```

- [ ] **Step 3 — Indiquer le merge**

Dire à l'utilisateur : PR docs-only, CI verte attendue, merge possible dès validation visuelle. Mention déploiement : ✅ aucun redéploiement serveur (client/master/shard).

---

## Self-review (couverture spec)

- D1 branches → Task 0 ✅
- D2 CMANGOS→SERVER-CORE → Task 1 ✅
- D3 Issue=vérité + retrait Mxx → Task 3 ✅
- D4 vérif approfondie → Task 2 ✅
- D5 branche+PR → Task 5 ✅
- D6 encart PARTIAL → Task 3 Step 2 ✅
- Tableau de suivi → Task 4 ✅
- Garde-fous (legacy intact, aucun code touché, pas de redéploiement) → respectés (opérations limitées à `tickets/` + `docs/`).
