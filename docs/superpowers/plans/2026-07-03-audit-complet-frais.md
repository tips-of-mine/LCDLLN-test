# Audit complet frais — plan d'exécution

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produire un rapport d'audit unique (`docs/audit/2026-07-03-audit-complet-frais.md`) recensant anomalies, problèmes de sécurité et optimisations candidates sur tout le code du projet hors `legacy/`, sans modifier de code applicatif.

**Architecture:** Workflow multi-agents en 4 phases — audit (fan-out par cellule sous-système × dimension), dédup (barrière, code), vérification adversariale (réfutation des P0/P1), synthèse (rédaction). Chaque agent d'audit et de vérification retourne des données structurées via schéma JSON.

**Tech Stack:** Outil `Workflow` (JS), agents `Explore`/`general-purpose`, schémas JSON pour la sortie structurée. Aucun build local (pas de toolchain), analyse statique de lecture uniquement.

## Global Constraints

- Livrable en **français** ; identifiants/commandes/chemins restent en anglais.
- **Aucune modification de code applicatif** dans cette session — seul le fichier rapport (et éventuels artefacts de travail dans le scratchpad) est écrit.
- Exclure **totalement** `legacy/` de tout scan.
- Ne pas employer le terme « CMANGOS » dans le rapport.
- Classement des sévérités : **P0** sécurité critique / **P1** bug sérieux ou sécurité modérée / **P2** bug mineur ou perf probable / **P3** qualité.
- Chaque finding : `fichier:ligne` cliquable (chemin relatif à la racine repo), catégorie (sécurité/bug/perf), description, recommandation.
- Findings perf = candidats à mesurer (pas de profiling possible), le signaler comme tel.
- Zéro finding P0/P1 non contre-vérifié dans le rapport final.
- Toute cellule qui échoue doit être mentionnée dans le rapport (pas d'abandon silencieux — règle « no silent caps »).
- Rapport committé + poussé sur la branche `claude/funny-bardeen-f6f803`.
- Déploiement : ✅ docs uniquement, pas de redéploiement serveur.

---

## Découpage en cellules (Phase 1)

Chaque cellule = un agent d'audit. Dimensions notées S (sécurité), B (bug/anomalie), P (perf).

| # | Cellule | Chemins | Dimensions | Priorité risque |
|---|---------|---------|------------|-----------------|
| C1 | masterd — handlers & wire | `src/masterd/handlers`, `src/masterd/*/` handlers d'opcodes | S, B | Élevée (surface réseau) |
| C2 | masterd — auth/account/session | `src/masterd/account`, `src/masterd/admin`, `src/masterd/email` | S, B | Élevée (gating) |
| C3 | masterd — autres sous-systèmes | `src/masterd/{arena,auction,battleground,chat,cinematics,events,gmtickets,guild,lfg,loot,mail,metrics,Groups}` | S, B | Moyenne |
| C4 | shardd — net & anticheat | `src/shardd/net`, `src/shardd/anticheat`, `src/shardd/app` | S, B | Élevée |
| C5 | shardd — gameplay & entities | `src/shardd/{gameplay,entities,combat,Movement,ai,maps,internals,dbscripts}` | B, P | Moyenne |
| C6 | shardd — autres sous-systèmes | `src/shardd/{arena,auction,battleground,cinematics,guild,loot}` | S, B | Moyenne |
| C7 | shared — net/network/messager | `src/shared/{net,network,messager}` | S, B | Élevée (parsing wire partagé) |
| C8 | shared — security/auth/account/db | `src/shared/{security,auth,account,db}` | S, B | Élevée |
| C9 | shared — core/util/formulas/math/world/routine/platform | `src/shared/{core,util,formulas,math,world,routine,platform,server_bootstrap,metric}` | B, P | Moyenne |
| C10 | client — render | `src/client/render` | B, P | Moyenne (Vulkan, races, fuites) |
| C11 | client — app & net | `src/client/app`, `src/client/net` | B, P, S | Élevée (threads réseau vs rendu — précédent crash géoloc) |
| C12 | client — gameplay/UI/systèmes | `src/client/{quest,combat,inventory,trade,chat,auth,grimoire,hud,dialogue,social,guild,mail,loot,economy,crafting,skills,reputation,character_creation,settings,localization,gameplay,world,weather,audio,fx,ui_common,main.cpp}` | B, S | Moyenne |
| C13 | world_editor | `src/world_editor`, `src/client/render/terrain/TerrainEditingTools.*` | B, P | Faible-moyenne |
| C14 | web-portal | `web-portal/{app,lib,components,middleware.ts,email-templates,nginx,next.config.mjs}` | S, B | Élevée (XSS/CSRF/secrets/injection) |
| C15 | sql + tools + deploy/config | `sql/migrations`, `tools/{hlod_builder,zone_builder,impostor_builder,load_tester}`, `deploy/docker`, `config.json`, `game/data/config/slash_commands.json` | S, B | Moyenne (injection SQL, secrets, RBAC) |

**Consigne commune à toutes les cellules d'audit** (incluse dans le prompt de chaque agent) :
- Lire le code réel, ne pas deviner. Exclure `legacy/`.
- Pour chaque finding, fournir : `file` (relatif racine), `line` (1-indexé), `severity` (P0/P1/P2/P3), `category` (securite/bug/perf), `summary` (1 phrase), `detail` (mécanisme + condition de déclenchement), `recommendation`.
- Perf : indiquer que c'est un candidat non mesuré.
- Ne pas inventer de findings pour « remplir » ; retourner une liste vide est acceptable.

---

## Task 1 : Écrire et lancer le workflow d'audit (Phases 1–3)

**Files:**
- Create (scratchpad, artefact de travail) : script du workflow via l'outil `Workflow` (persisté automatiquement sous la session)
- Aucun fichier du repo modifié à cette étape

**Interfaces:**
- Produces : un tableau JS `findingsVerifies` — liste d'objets `{file, line, severity, category, summary, detail, recommendation, verdict, cellule}` où `verdict ∈ {CONFIRMED, PLAUSIBLE, REFUTED}` ; les REFUTED sont exclus ou rétrogradés.

- [ ] **Step 1 : Définir les schémas JSON**

Schéma finding (sortie des agents d'audit) :
```
FINDINGS_SCHEMA = {
  type: "object",
  required: ["findings"],
  properties: {
    findings: {
      type: "array",
      items: {
        type: "object",
        required: ["file","line","severity","category","summary","detail","recommendation"],
        properties: {
          file:   {type:"string"},
          line:   {type:"integer"},
          severity:{type:"string", enum:["P0","P1","P2","P3"]},
          category:{type:"string", enum:["securite","bug","perf"]},
          summary:{type:"string"},
          detail: {type:"string"},
          recommendation:{type:"string"}
        }
      }
    }
  }
}
```
Schéma verdict (sortie des agents de vérification) :
```
VERDICT_SCHEMA = {
  type:"object",
  required:["verdict","justification"],
  properties:{
    verdict:{type:"string", enum:["CONFIRMED","PLAUSIBLE","REFUTED"]},
    severiteCorrigee:{type:"string", enum:["P0","P1","P2","P3"]},
    justification:{type:"string"}
  }
}
```

- [ ] **Step 2 : Phase 1 — fan-out des 15 cellules**

Utiliser `pipeline()` sur les 15 cellules décrites dans la table ci-dessus. Stage 1 = agent d'audit (agentType `Explore`, schéma `FINDINGS_SCHEMA`), label `audit:C<n>`, phase `Audit`. Le prompt de chaque agent contient : les chemins de la cellule, les dimensions à couvrir, et la **consigne commune** ci-dessus verbatim.

- [ ] **Step 3 : Phase 2 — dédup (barrière implicite dans la 2e étape du pipeline)**

Après collecte, dédupliquer en code : clé = `file` + `line` arrondie à ±3 lignes + `category`. En cas de doublon, conserver la sévérité la plus haute et concaténer les détails distincts. (La dédup se fait sur le tableau agrégé — c'est le seul point nécessitant tous les résultats d'audit, donc collecter via `.flat()` avant vérification.)

- [ ] **Step 4 : Phase 3 — vérification adversariale**

Pour chaque finding dédupliqué de sévérité **P0 ou P1** : agent sceptique (`general-purpose`, schéma `VERDICT_SCHEMA`), prompt = « Tente de RÉFUTER ce finding en relisant le code réel à `file:line`. Par défaut, considère-le douteux : ne le confirme que si le mécanisme et la condition de déclenchement tiennent. Renvoie REFUTED si le code cité ne correspond pas, si la garde manquante existe en réalité ailleurs, ou si la condition n'est pas atteignable. » Label `verify:<file>`, phase `Verify`.
Pour chaque finding **P2/P3** : vérification simple (`Explore`, `VERDICT_SCHEMA`) — le code cité existe-t-il et le finding est-il plausible ? Renvoyer REFUTED si le code cité est absent/erroné.
Filtrer : exclure les `verdict === "REFUTED"`. Appliquer `severiteCorrigee` si présente.

- [ ] **Step 5 : Retourner le tableau `findingsVerifies`**

Le workflow retourne `{findingsVerifies, cellulesEchouees}` où `cellulesEchouees` liste les cellules dont l'agent a renvoyé `null` (skip/erreur).

- [ ] **Step 6 : Lancer le workflow**

Invoquer `Workflow` avec le script. Attendre la notification de complétion. Récupérer le tableau retourné.

---

## Task 2 : Rédiger le rapport d'audit

**Files:**
- Create : `docs/audit/2026-07-03-audit-complet-frais.md`

**Interfaces:**
- Consumes : `{findingsVerifies, cellulesEchouees}` de Task 1.

- [ ] **Step 1 : Écrire l'en-tête et le résumé exécutif**

En-tête : titre, date, périmètre audité (lister les 15 cellules), méthode (workflow multi-agents, vérification adversariale), rappel « analyse statique, findings perf non mesurés ». Résumé exécutif : compte de findings par sévérité (tableau), et les 3-5 points les plus importants en prose.

- [ ] **Step 2 : Écrire les sections par sévérité**

Quatre sections : P0, P1, P2, P3. Dans chaque section, un sous-titre par finding avec : localisation `[fichier:ligne](chemin:ligne)` cliquable, catégorie, description (mécanisme + condition), recommandation. Trier au sein de chaque section par catégorie puis par fichier.

- [ ] **Step 3 : Écrire la section « Couverture & limites »**

Lister les cellules couvertes, les cellules échouées (`cellulesEchouees`) le cas échéant, et rappeler explicitement : pas de build/profiling local, `legacy/` exclu, `game/data` (hors slash_commands.json) exclu.

- [ ] **Step 4 : Ligne de déploiement**

Terminer par : `**Déploiement** : ✅ docs uniquement, pas de redéploiement serveur.`

- [ ] **Step 5 : Commit + push**

```bash
git add docs/audit/2026-07-03-audit-complet-frais.md
git commit -m "docs(audit): audit complet frais 2026-07-03 — anomalies, sécurité, perf"
git push -u origin claude/funny-bardeen-f6f803
```

---

## Task 3 : Restituer à l'utilisateur

**Files:** aucun.

- [ ] **Step 1 : Message de synthèse final**

Dans le dernier message : nombre de findings par sévérité, les findings P0/P1 en clair (localisation + une phrase chacun), lien vers le rapport committé, mention des cellules échouées s'il y en a, et la ligne de déploiement. Proposer (sans l'exécuter) de cadrer les corrections P0/P1 en PRs séparées.

---

## Self-Review

- **Couverture spec** : périmètre (15 cellules couvrent src/*, web-portal, sql, tools, deploy/config → C1–C15) ✅ ; 3 dimensions (S/B/P réparties par cellule) ✅ ; workflow 4 phases (Task 1 steps 2–4 + Task 2) ✅ ; livrable rapport classé P0→P3 (Task 2) ✅ ; vérification adversariale P0/P1 (Task 1 step 4) ✅ ; français + pas de correction code + legacy exclu + pas de « CMANGOS » (Global Constraints) ✅ ; commit+push (Task 2 step 5) ✅ ; cellules échouées signalées (Task 1 step 5 → Task 2 step 3) ✅.
- **Placeholders** : les schémas et prompts sont donnés en toutes lettres ; pas de « TBD ».
- **Cohérence des types** : `FINDINGS_SCHEMA`/`VERDICT_SCHEMA` définis en Task 1 step 1 et réutilisés steps 2/4 ; champ `verdict` produit step 4 et consommé step 5 ; `findingsVerifies`/`cellulesEchouees` produits Task 1 et consommés Task 2. Cohérent.
