# PR 1 — Web portal : fail-fast sur secrets manquants — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Éliminer le fallback secret en dur `"lcdlln-dev-recovery-secret"` dans `web-portal/lib/auth/passwordRecovery.ts:61` et faire planter le serveur au boot si `AUTH_SECRET` ou `NEXT_PUBLIC_PORTAL_URL` ne sont pas définis. Conserver le défaut industriel `SMTP_PORT=587` (port standard, pas un secret).

**Architecture:** Introduire un helper interne `requireEnv(name)` dans `passwordRecovery.ts` qui lève une `Error` claire si la variable d'environnement est absente ou vide. Remplacer les deux fallbacks risqués (`AUTH_SECRET`, `NEXT_PUBLIC_PORTAL_URL`) par cet helper. La validation est paresseuse (au premier appel des fonctions concernées, qui sont elles-mêmes appelées au boot Next.js sur les routes API de récupération de mot de passe — donc l'erreur se manifestera dès la première requête, pas réellement au boot, mais avec un message clair côté logs et une réponse 500 immédiate).

**Tech Stack:** Next.js 14 (App Router), TypeScript strict, aucune dépendance ajoutée. Pas d'infra de test introduite (web-portal n'en a pas et l'ajout serait un scope creep).

**Spec source :** [docs/superpowers/audits/2026-05-27-codebase-audit.md](../audits/2026-05-27-codebase-audit.md) section 8 (PR 1, chantier A).

**Déploiement :** ✅ web portal uniquement, aucun redéploiement serveur. Lock-step requis avec la mise à jour de l'env de prod : **avant de merger, vérifier que `AUTH_SECRET` ET `NEXT_PUBLIC_PORTAL_URL` sont déjà définis sur l'env Docker/serveur web** — sinon le portail tombera au premier appel des routes de récupération mot de passe.

---

## File structure

| Fichier | Action | Responsabilité |
|---|---|---|
| `web-portal/lib/auth/passwordRecovery.ts` | Modify (L.60-62, L.285-287) | Remplacer 2 fallbacks par `requireEnv()`. Garder L.431 (`SMTP_PORT`) intact. |
| `web-portal/lib/env.ts` | **Create** | Nouveau module : helper `requireEnv(name: string): string`. Petit module dédié pour réutilisation future. |
| `deploy/docker/.env.example` | Modify | Documenter explicitement que `AUTH_SECRET` et `NEXT_PUBLIC_PORTAL_URL` sont **obligatoires** (annoter avec `# REQUIRED — portal will throw at boot if missing`). |

> Pas d'ajout de tests unitaires (web-portal n'a pas d'infra de test ; introduire vitest gonflerait la PR). Validation par `npm run build` + smoke test manuel décrit en Task 5.

---

## Task 1 : Préparer la branche

**Files:**
- Aucun fichier modifié à cette étape — opération git pure.

- [ ] **Step 1 : Créer une branche dédiée depuis `main`**

```bash
git fetch origin
git switch -c claude/pr1-web-portal-secret-fail-fast origin/main
```

Vérifier qu'on part bien d'un `main` propre, pas de la branche actuelle `claude/tg3-scope-doc` qui n'a aucun rapport avec ce sujet.

- [ ] **Step 2 : Vérifier l'état**

```bash
git status
git log --oneline -1
```

Expected : working tree clean, HEAD = dernier commit `main`.

- [ ] **Step 3 : Reporter le rapport d'audit déjà écrit sur la nouvelle branche**

Le rapport `docs/superpowers/audits/2026-05-27-codebase-audit.md` a été écrit pendant la phase brainstorming sur la branche `claude/tg3-scope-doc`. Il doit suivre la PR 1.

```bash
git checkout claude/tg3-scope-doc -- docs/superpowers/audits/2026-05-27-codebase-audit.md docs/superpowers/plans/2026-05-27-pr1-web-portal-secret-fail-fast.md
git status
```

Expected : 2 fichiers ajoutés en staging (`docs/superpowers/audits/...` et `docs/superpowers/plans/...`).

- [ ] **Step 4 : Premier commit (docs)**

```bash
git commit -m "$(cat <<'EOF'
docs(audit): rapport d'audit codebase 2026-05-27 + plan PR 1

Audit transverse des 4 sous-systèmes (client, serveur, éditeur, web-portal)
+ revue code orphelin. 10 findings (F1-F10), 4 PRs packagisées (A,C,D,E F10).

Plan d'implémentation pour la PR 1 (sécurité web-portal — fail-fast secrets).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2 : Créer le helper `requireEnv`

**Files:**
- Create : `web-portal/lib/env.ts`

- [ ] **Step 1 : Créer le module helper**

Contenu intégral du fichier :

```typescript
// Helper centralisé pour les variables d'environnement obligatoires.
// L'objectif : un message d'erreur clair et explicite quand une variable
// critique manque, plutôt qu'un fallback silencieux vers une valeur de dev.
//
// Usage :
//   const secret = requireEnv("AUTH_SECRET");
//
// Lance : Error("Missing required environment variable: AUTH_SECRET")

export function requireEnv(name: string): string {
  const value = process.env[name];
  if (value === undefined || value === null || value.trim().length === 0) {
    throw new Error(
      `Missing required environment variable: ${name}. ` +
      `Set it in the deployment environment (Docker .env, systemd EnvironmentFile, etc.) before starting the web portal.`
    );
  }
  return value;
}
```

- [ ] **Step 2 : Vérifier la compilation TypeScript du nouveau fichier**

```bash
cd web-portal
npx tsc --noEmit
```

Expected : aucune erreur. Le fichier est self-contained, aucun import.

- [ ] **Step 3 : Commit**

```bash
git add web-portal/lib/env.ts
git commit -m "$(cat <<'EOF'
feat(web-portal): add requireEnv helper for mandatory env vars

Centralise la validation des variables d'environnement obligatoires.
Lance une Error explicite si la variable est absente, vide ou null —
plutôt qu'un fallback silencieux vers une valeur de dev.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3 : Remplacer le fallback `AUTH_SECRET` par `requireEnv`

**Files:**
- Modify : `web-portal/lib/auth/passwordRecovery.ts:60-62`

- [ ] **Step 1 : Ajouter l'import de `requireEnv` en tête du fichier**

Repérer le bloc d'imports actuel (en haut de `web-portal/lib/auth/passwordRecovery.ts`) et y ajouter :

```typescript
import { requireEnv } from "@/lib/env";
```

Note : si le path alias `@/` n'est pas configuré dans `tsconfig.json` (à vérifier), utiliser un chemin relatif :

```typescript
import { requireEnv } from "../env";
```

Préfère le path relatif pour rester safe — le alias `@/` est configuré par défaut par `create-next-app` mais ce projet a peut-être divergé. Étape 2 valide.

- [ ] **Step 2 : Vérifier la convention d'import déjà utilisée dans le fichier**

```bash
grep -E "^import.*from" web-portal/lib/auth/passwordRecovery.ts | head -10
```

Si les imports utilisent `@/`, adopter `@/`. Sinon, utiliser le chemin relatif `"../env"`.

- [ ] **Step 3 : Remplacer la fonction `getRecoverySecret`**

Avant (lignes 60-62) :
```typescript
function getRecoverySecret(): string {
  return process.env.AUTH_SECRET || "lcdlln-dev-recovery-secret";
}
```

Après :
```typescript
function getRecoverySecret(): string {
  return requireEnv("AUTH_SECRET");
}
```

- [ ] **Step 4 : Type-check**

```bash
cd web-portal
npx tsc --noEmit
```

Expected : aucune erreur.

- [ ] **Step 5 : Commit**

```bash
git add web-portal/lib/auth/passwordRecovery.ts
git commit -m "$(cat <<'EOF'
fix(web-portal): fail-fast on missing AUTH_SECRET instead of dev fallback

Le fallback "lcdlln-dev-recovery-secret" expose un secret HMAC en dur
dans le binaire si AUTH_SECRET n'est pas défini en production. Le hash
des réponses de récupération de mot de passe devient devinable.

Remplace le fallback par requireEnv("AUTH_SECRET") qui throw avec un
message explicite. Lock-step : AUTH_SECRET doit être défini sur l'env
de prod AVANT le déploiement de cette PR.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4 : Remplacer le fallback `NEXT_PUBLIC_PORTAL_URL` par `requireEnv`

**Files:**
- Modify : `web-portal/lib/auth/passwordRecovery.ts:285-287`

- [ ] **Step 1 : Identifier la fonction concernée**

```bash
sed -n '283,290p' web-portal/lib/auth/passwordRecovery.ts
```

Expected : une fonction qui construit un baseUrl à partir de `process.env.NEXT_PUBLIC_PORTAL_URL || "http://127.0.0.1:3000"`. Probablement utilisée pour générer les liens de récupération envoyés par email.

- [ ] **Step 2 : Remplacer le fallback**

Avant (autour de la ligne 286) :
```typescript
const baseUrl = (process.env.NEXT_PUBLIC_PORTAL_URL || "http://127.0.0.1:3000").replace(/\/+$/, "");
```

Après :
```typescript
const baseUrl = requireEnv("NEXT_PUBLIC_PORTAL_URL").replace(/\/+$/, "");
```

Justification : un site prod déployé sans `NEXT_PUBLIC_PORTAL_URL` enverrait des emails de récupération de mot de passe avec des liens vers `http://127.0.0.1:3000` — donc cassés pour 100% des utilisateurs. Préférable de planter au premier envoi avec un message clair que de spammer des liens morts.

- [ ] **Step 3 : Type-check**

```bash
cd web-portal
npx tsc --noEmit
```

Expected : aucune erreur.

- [ ] **Step 4 : Commit**

```bash
git add web-portal/lib/auth/passwordRecovery.ts
git commit -m "$(cat <<'EOF'
fix(web-portal): fail-fast on missing NEXT_PUBLIC_PORTAL_URL

Le fallback "http://127.0.0.1:3000" rend les liens de récupération
de mot de passe envoyés par email inutilisables en prod si l'env var
n'est pas définie. Préférable de planter clairement au premier envoi
que de spammer des liens morts à 100% des utilisateurs.

Lock-step : NEXT_PUBLIC_PORTAL_URL doit être défini sur l'env de prod.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5 : Confirmer qu'il n'y a pas d'autres fallbacks à corriger

**Files:**
- Aucun — vérification grep transverse.

- [ ] **Step 1 : Grep complet dans `web-portal/`**

```bash
grep -rnE 'process\.env\.\w+\s*\|\|\s*["'"'"'`]' web-portal/ --include='*.ts' --include='*.tsx'
```

Expected : **3 matches uniquement**, tous dans `web-portal/lib/auth/passwordRecovery.ts` :
- L.61 (AUTH_SECRET) — **déjà fixé en Task 3**, ne doit plus apparaître après Task 3
- L.286 (NEXT_PUBLIC_PORTAL_URL) — **déjà fixé en Task 4**, ne doit plus apparaître après Task 4
- L.431 (SMTP_PORT) — **conservé volontairement** (port standard 587, défaut industriel acceptable)

Après Tasks 3 & 4, le grep doit retourner uniquement L.431 (SMTP_PORT). Si d'autres matches apparaissent (autres fichiers introduits entre temps, ou matches manqués par l'audit initial), les corriger de la même manière (créer une étape supplémentaire dans cette task).

- [ ] **Step 2 : Documenter le choix de conservation pour `SMTP_PORT`**

Ajouter un commentaire au-dessus de la ligne 431 dans `passwordRecovery.ts` pour expliquer pourquoi celui-là est conservé :

Avant :
```typescript
const port = Number.parseInt(process.env.SMTP_PORT || "587", 10);
```

Après :
```typescript
// SMTP_PORT : défaut "587" conservé volontairement — c'est le port submission
// SMTP standard universel (RFC 6409). Pas un secret, pas une URL prod-critique.
const port = Number.parseInt(process.env.SMTP_PORT || "587", 10);
```

- [ ] **Step 3 : Type-check**

```bash
cd web-portal
npx tsc --noEmit
```

Expected : aucune erreur.

- [ ] **Step 4 : Commit**

```bash
git add web-portal/lib/auth/passwordRecovery.ts
git commit -m "$(cat <<'EOF'
docs(web-portal): document SMTP_PORT default rationale

Explicite pourquoi le défaut "587" est conservé alors que AUTH_SECRET
et NEXT_PUBLIC_PORTAL_URL ont basculé en fail-fast : c'est le port
submission SMTP standard universel (RFC 6409), pas un secret.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6 : Mettre à jour la doc de déploiement

**Files:**
- Modify : `deploy/docker/.env.example` (et éventuellement `deploy/docker/README.md`)

- [ ] **Step 1 : Localiser le fichier d'exemple d'env**

```bash
ls deploy/docker/
cat deploy/docker/.env.example
```

Vérifier qu'il existe et lister les variables actuellement documentées.

- [ ] **Step 2 : Annoter `AUTH_SECRET` et `NEXT_PUBLIC_PORTAL_URL` comme `REQUIRED`**

Modifier `deploy/docker/.env.example` pour ajouter ou marquer ces deux variables.

Exemple de bloc à ajouter ou modifier (adapter selon le format existant) :

```bash
# === Web portal — secrets et URL critiques ===
# REQUIRED : portail web throw au premier appel si absent.
# Doit être une chaîne secrète d'au moins 32 caractères aléatoires (HMAC).
AUTH_SECRET=

# REQUIRED : URL publique du portail. Utilisée dans les liens des emails
# de récupération de mot de passe. Sans valeur valide, les liens envoyés
# sont inutilisables et le portail throw au premier appel.
NEXT_PUBLIC_PORTAL_URL=https://lunenoire.example.com

# Optional : port SMTP, défaut 587 (port submission standard RFC 6409).
SMTP_PORT=587
```

- [ ] **Step 3 : Mettre à jour `deploy/docker/README.md` si nécessaire**

```bash
grep -n "AUTH_SECRET\|PORTAL_URL" deploy/docker/README.md
```

Si des références existent, ajouter une note précisant que ces variables sont maintenant **obligatoires** depuis cette PR (ne plus présenter de fallbacks).

- [ ] **Step 4 : Commit**

```bash
git add deploy/docker/.env.example deploy/docker/README.md
git commit -m "$(cat <<'EOF'
docs(deploy): mark AUTH_SECRET and PORTAL_URL as required in .env.example

Documente côté ops que ces variables ne peuvent plus être omises
depuis la PR fail-fast. Avant cette PR : valeur de dev en dur si
absente. Après : le portail throw au premier appel.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7 : Smoke test build + comportement

**Files:**
- Aucun — validation.

- [ ] **Step 1 : Build production complet**

```bash
cd web-portal
npm run build
```

Expected : `Compiled successfully` + génération `.next/`. Aucune erreur de type ni de lint qui n'existait pas avant. Si erreur sur les imports `@/lib/env` vs chemin relatif, revenir sur Task 3 Step 2 et ajuster.

- [ ] **Step 2 : Smoke test "manque AUTH_SECRET" via dev server (optionnel mais fortement recommandé)**

Si l'environnement de dev local est dispo :

```bash
cd web-portal
unset AUTH_SECRET  # ou: $env:AUTH_SECRET = "" en PowerShell
npm run dev
```

Puis dans un autre terminal, simuler un appel sur une route de récupération mot de passe (ex. via curl ou via le navigateur sur `http://127.0.0.1:3000/password-recovery`). Expected : 500 Internal Server Error + log serveur clair `Error: Missing required environment variable: AUTH_SECRET`.

Si le smoke test n'est pas faisable localement (pas de DB MySQL up), au minimum vérifier que `npm run build` passe.

- [ ] **Step 3 : Lint**

```bash
cd web-portal
npm run lint
```

Expected : aucune nouvelle erreur de lint introduite par les modifications.

---

## Task 8 : Push et création de la PR

**Files:**
- Aucun — opérations git/gh.

- [ ] **Step 1 : Push de la branche**

```bash
git push -u origin claude/pr1-web-portal-secret-fail-fast
```

- [ ] **Step 2 : Création de la PR via `gh`**

```bash
gh pr create --title "fix(web-portal): fail-fast sur AUTH_SECRET et PORTAL_URL absents" --body "$(cat <<'EOF'
## Summary

- Remplace le fallback secret en dur `"lcdlln-dev-recovery-secret"` par un `requireEnv("AUTH_SECRET")` qui throw avec un message clair.
- Remplace le fallback `http://127.0.0.1:3000` pour `NEXT_PUBLIC_PORTAL_URL` par le même mécanisme — empêche d'envoyer des emails de récupération avec des liens morts en prod.
- Conserve `SMTP_PORT=587` (port submission standard universel, pas un secret).
- Documente `AUTH_SECRET` et `NEXT_PUBLIC_PORTAL_URL` comme **REQUIRED** dans `deploy/docker/.env.example`.
- Joint le rapport d'audit complet `docs/superpowers/audits/2026-05-27-codebase-audit.md` + le plan d'implémentation.

## Test plan

- [ ] `npm run build` passe sur `web-portal/`
- [ ] `npm run lint` ne remonte aucune nouvelle erreur
- [ ] Smoke test local : retirer `AUTH_SECRET` de l'env, démarrer `npm run dev`, appeler la route password-recovery → 500 + log `Missing required environment variable: AUTH_SECRET`
- [ ] **Avant le merge** : vérifier que `AUTH_SECRET` ET `NEXT_PUBLIC_PORTAL_URL` sont déjà définis sur l'environnement Docker prod du portail. Si absent, ce sera un downtime du flow de récupération de mot de passe au premier appel après déploiement.

## Déploiement

✅ **Client / serveur backend uniquement** : aucun redéploiement master ni shard. Lock-step **web portal** uniquement, avec exigence env de prod ci-dessus.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

- [ ] **Step 3 : Vérifier la PR créée**

```bash
gh pr view --web
```

Expected : URL de la PR, statut "Open", reviewers proposés selon la config repo.

---

## Self-review

**Spec coverage (vs `2026-05-27-codebase-audit.md` section 8 PR 1) :**
- ✅ Fail-fast `getRecoverySecret()` si `AUTH_SECRET` absent → Task 3
- ✅ Grep transverse autres fallbacks `process.env.X || "..."` dans `web-portal/` → Task 5
- ✅ Fix des fallbacks trouvés → Tasks 3, 4 (+ rationale Task 5 pour SMTP_PORT)
- ✅ Déploiement : web portal seul, aucun redéploiement serveur → titre PR + plan

**Placeholder scan :** RAS. Tout est rempli, commandes explicites, code intégral fourni à chaque step.

**Type consistency :** `requireEnv(name: string): string` défini en Task 2, utilisé tel quel en Tasks 3, 4. Aucune incohérence.

**Risques / edge cases couverts :**
- ✅ Alias `@/` vs chemin relatif (Task 3 Step 2)
- ✅ Choix de conservation de `SMTP_PORT` documenté en commentaire (Task 5 Step 2)
- ✅ Lock-step env prod explicité dans le commit Task 3 et la PR body (Task 8)
- ✅ Pas d'introduction de framework de test (justifié dans le header et la sortie de Task 5)
