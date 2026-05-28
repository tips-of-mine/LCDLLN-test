# PR 4 — Web portal polish (F10) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduire un cache Next.js `unstable_cache` sur `getStats()` dans la page d'admin pour éviter 4 `COUNT(*)` par render, ET remplacer les 9 occurrences de `console.error/warn` éparpillées par un wrapper logger structuré minimal `web-portal/lib/log.ts`.

**Architecture:** Wrapper logger pur (zero dep externe) avec 3 fonctions `logInfo / logWarn / logError(context, message, meta?)`. Output : JSON one-line en prod (parseable par stack ELK/Loki), texte préfixé en dev (lisible). Sous-jacent : `console.log/warn/error` pour préserver l'output Docker. Le cache `unstable_cache` est isolé de `getSession()` — il enveloppe uniquement `getStats()` qui ne dépend pas de la session, donc on garde la page dynamique pour l'auth mais on évite les queries répétées.

**Tech Stack:** Next.js 14 (App Router, `unstable_cache`), TypeScript strict, zero dépendance ajoutée.

**Spec source:** [docs/superpowers/audits/2026-05-27-codebase-audit.md](../audits/closed/2026-05-27-codebase-audit.md) section 8 (PR 4, chantier E partiel F10).

**Branche:** depuis `origin/main` (indépendante de PR 1 #719). Conflict potentiel uniquement sur `passwordRecovery.ts` (PR 1 a ajouté un import L.5, PR 4 modifiera la ligne 440 `console.warn`) — git devrait merger automatiquement, sinon rebase trivial.

**Déploiement:** ✅ web portal uniquement, aucun redéploiement serveur. Pas de lock-step env (le logger lit `NODE_ENV` qui existe déjà partout en prod Next.js).

---

## File structure

| Fichier | Action | Responsabilité |
|---|---|---|
| `web-portal/lib/log.ts` | **Create** | Helper logger : 3 exports `logInfo/logWarn/logError`. Sérialise les `Error` objects en meta. |
| `web-portal/app/admin/page.tsx` | Modify | Wrap `getStats()` dans `unstable_cache(..., ['admin-stats'], { revalidate: 60 })`. |
| `web-portal/app/api/bugs/route.ts` | Modify | 1× `console.error` → `logError`. |
| `web-portal/app/admin/acceptances/page.tsx` | Modify | 1× `console.error` → `logError`. |
| `web-portal/app/admin/players/page.tsx` | Modify | 1× `console.error` → `logError`. |
| `web-portal/app/api/admin/cgu/route.ts` | Modify | 1× `console.error` → `logError`. |
| `web-portal/app/api/player/privacy/route.ts` | Modify | 1× `console.error` → `logError`. |
| `web-portal/app/player/cgu/page.tsx` | Modify | 1× `console.error` → `logError`. |
| `web-portal/app/player/privacy/page.tsx` | Modify | 2× `console.error` → `logError`. |
| `web-portal/lib/auth/passwordRecovery.ts` | Modify | 1× `console.warn` → `logWarn` (SMTP non configuré). |

> Pas d'ajout de tests unitaires (web-portal n'a pas d'infra de test ; introduire vitest serait scope creep). Validation par build + lint en CI au push.

---

## Task 1 : Préparer la branche

**Files:** Aucun — opération git pure.

- [ ] **Step 1 : Vérifier l'état initial**

```bash
git status
git branch --show-current
```

Expected : sur `claude/pr1-web-portal-secret-fail-fast`, working tree clean (la PR 1 est créée et pushée).

- [ ] **Step 2 : Créer la nouvelle branche depuis origin/main**

```bash
git fetch origin
git switch -c claude/pr4-web-portal-polish origin/main
git status
git log --oneline -1
```

Expected : `On branch claude/pr4-web-portal-polish`, working tree clean, HEAD = dernier commit de `origin/main` (PAS les commits de la PR 1).

- [ ] **Step 3 : Porter le plan de cette PR sur la nouvelle branche**

Le fichier `docs/superpowers/plans/2026-05-27-pr4-web-portal-polish.md` (ce document) existe sur la branche `claude/pr1-...` mais pas sur main ni sur la nouvelle branche. On veut qu'il accompagne la PR 4 pour traçabilité.

```bash
git checkout claude/pr1-web-portal-secret-fail-fast -- docs/superpowers/plans/2026-05-27-pr4-web-portal-polish.md
git status --short
```

Expected : 1 fichier staged (le plan).

- [ ] **Step 4 : Premier commit (doc)**

```bash
git commit -m "$(cat <<'EOF'
docs(plan): plan d'implémentation PR 4 — web portal polish

Issu du rapport d'audit codebase 2026-05-27 (chantier E partiel, F10) :
cache unstable_cache sur stats admin + logger structuré minimal en
remplacement des 9 console.error/warn éparpillés dans web-portal/.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
git log --oneline -2
```

Expected : 1 commit, branch ahead de main par 1 commit.

---

## Task 2 : Créer le helper logger `lib/log.ts`

**Files:** Create `web-portal/lib/log.ts`.

- [ ] **Step 1 : Vérifier que le fichier n'existe pas**

```bash
ls web-portal/lib/log.ts 2>&1 || echo "absent, OK pour création"
```

- [ ] **Step 2 : Créer le fichier**

Utilise Write avec EXACTEMENT ce contenu :

```typescript
// Logger structuré minimal pour le web-portal.
//
// Trois niveaux : info / warn / error.
// Sortie : JSON one-line en production (parseable par stack ELK/Loki/Grafana),
// texte préfixé [context] en développement (lisible humainement).
// Sous-jacent : console.log / console.warn / console.error — préserve
// l'output Docker logs, journald, PM2, etc.
//
// Usage :
//   logError("POST /api/bugs", "Insert failed", { err });
//   logWarn("password-recovery", "SMTP non configuré, lien généré localement", { to });
//   logInfo("admin/players", "Account disabled by admin", { accountId, by });
//
// La meta `err` est traitée spécialement : si c'est un Error, on extrait
// message + stack + name pour que la sérialisation JSON soit utile.

type LogMeta = Record<string, unknown>;

function isProduction(): boolean {
  return process.env.NODE_ENV === "production";
}

function serializeError(err: unknown): unknown {
  if (err instanceof Error) {
    return {
      name: err.name,
      message: err.message,
      stack: err.stack,
    };
  }
  return err;
}

function normalizeMeta(meta: LogMeta | undefined): LogMeta | undefined {
  if (!meta) return undefined;
  const normalized: LogMeta = {};
  for (const [key, value] of Object.entries(meta)) {
    normalized[key] = key === "err" ? serializeError(value) : value;
  }
  return normalized;
}

function emit(
  level: "info" | "warn" | "error",
  context: string,
  message: string,
  meta: LogMeta | undefined
): void {
  const normalized = normalizeMeta(meta);
  if (isProduction()) {
    const payload = {
      ts: new Date().toISOString(),
      level,
      context,
      message,
      ...(normalized ?? {}),
    };
    const line = JSON.stringify(payload);
    if (level === "error") console.error(line);
    else if (level === "warn") console.warn(line);
    else console.log(line);
  } else {
    const prefix = `[${context}]`;
    if (normalized) {
      if (level === "error") console.error(prefix, message, normalized);
      else if (level === "warn") console.warn(prefix, message, normalized);
      else console.log(prefix, message, normalized);
    } else {
      if (level === "error") console.error(prefix, message);
      else if (level === "warn") console.warn(prefix, message);
      else console.log(prefix, message);
    }
  }
}

export function logInfo(context: string, message: string, meta?: LogMeta): void {
  emit("info", context, message, meta);
}

export function logWarn(context: string, message: string, meta?: LogMeta): void {
  emit("warn", context, message, meta);
}

export function logError(context: string, message: string, meta?: LogMeta): void {
  emit("error", context, message, meta);
}
```

- [ ] **Step 3 : Commit**

```bash
git add web-portal/lib/log.ts
git status --short
git commit -m "$(cat <<'EOF'
feat(web-portal): add structured logger helper (lib/log.ts)

Trois fonctions logInfo / logWarn / logError (context, message, meta).
JSON one-line en prod, texte préfixé en dev. Pas de dépendance externe.

Traitement spécial pour meta.err : extraction message/stack/name pour
sérialisation JSON utile (Error n'est pas serializable nativement).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 4 : Vérifier**

```bash
git log --oneline -3
ls -la web-portal/lib/log.ts
```

Expected : commit présent, fichier existant ~90 lignes.

---

## Task 3 : Cache `getStats()` via `unstable_cache`

**Files:** Modify `web-portal/app/admin/page.tsx:1-25`.

- [ ] **Step 1 : Relire la fonction actuelle**

Lis les lignes 1-25 de `web-portal/app/admin/page.tsx`. Tu dois voir :

```typescript
import Link from "next/link";
import { redirect } from "next/navigation";
import { getSession } from "@/lib/auth/session";
import { isStaff } from "@/lib/auth/roles";
import { query } from "@/lib/db/connection";
import type { RowDataPacket } from "mysql2/promise";

async function getStats() {
  try {
    const [accounts, cgu, bugs, acceptances] = await Promise.all([
      query<Array<RowDataPacket & { n: number }>>("SELECT COUNT(*) as n FROM accounts", []),
      query<Array<RowDataPacket & { n: number }>>("SELECT COUNT(*) as n FROM terms_editions WHERE status = 'published'", []),
      query<Array<RowDataPacket & { n: number }>>("SELECT COUNT(*) as n FROM bug_reports", []),
      query<Array<RowDataPacket & { n: number }>>("SELECT COUNT(*) as n FROM account_terms_acceptances", []),
    ]);
    return {
      accounts: accounts[0]?.n ?? 0,
      cgu: cgu[0]?.n ?? 0,
      bugs: bugs[0]?.n ?? 0,
      acceptances: acceptances[0]?.n ?? 0,
    };
  } catch {
    return { accounts: "—", cgu: "—", bugs: "—", acceptances: "—" };
  }
}
```

- [ ] **Step 2 : Ajouter l'import `unstable_cache`**

Utilise Edit. old_string :
```typescript
import Link from "next/link";
import { redirect } from "next/navigation";
import { getSession } from "@/lib/auth/session";
import { isStaff } from "@/lib/auth/roles";
import { query } from "@/lib/db/connection";
import type { RowDataPacket } from "mysql2/promise";
```

new_string :
```typescript
import Link from "next/link";
import { unstable_cache } from "next/cache";
import { redirect } from "next/navigation";
import { getSession } from "@/lib/auth/session";
import { isStaff } from "@/lib/auth/roles";
import { query } from "@/lib/db/connection";
import type { RowDataPacket } from "mysql2/promise";
```

(L'import `unstable_cache` placé entre `next/link` et `next/navigation` pour grouper les imports `next/*`.)

- [ ] **Step 3 : Wrapper `getStats` dans `unstable_cache`**

Utilise Edit. old_string :
```typescript
async function getStats() {
  try {
    const [accounts, cgu, bugs, acceptances] = await Promise.all([
      query<Array<RowDataPacket & { n: number }>>("SELECT COUNT(*) as n FROM accounts", []),
      query<Array<RowDataPacket & { n: number }>>("SELECT COUNT(*) as n FROM terms_editions WHERE status = 'published'", []),
      query<Array<RowDataPacket & { n: number }>>("SELECT COUNT(*) as n FROM bug_reports", []),
      query<Array<RowDataPacket & { n: number }>>("SELECT COUNT(*) as n FROM account_terms_acceptances", []),
    ]);
    return {
      accounts: accounts[0]?.n ?? 0,
      cgu: cgu[0]?.n ?? 0,
      bugs: bugs[0]?.n ?? 0,
      acceptances: acceptances[0]?.n ?? 0,
    };
  } catch {
    return { accounts: "—", cgu: "—", bugs: "—", acceptances: "—" };
  }
}
```

new_string :
```typescript
// Stats admin cachées 60s côté serveur Next.js — évite 4 COUNT(*) par render.
// Le cache est isolé de getSession() pour ne pas leaker entre utilisateurs ;
// les stats sont les mêmes pour tous les admins (lecture globale).
// Invalidation manuelle possible via revalidateTag("admin-stats") après mutation.
const getStats = unstable_cache(
  async () => {
    try {
      const [accounts, cgu, bugs, acceptances] = await Promise.all([
        query<Array<RowDataPacket & { n: number }>>("SELECT COUNT(*) as n FROM accounts", []),
        query<Array<RowDataPacket & { n: number }>>("SELECT COUNT(*) as n FROM terms_editions WHERE status = 'published'", []),
        query<Array<RowDataPacket & { n: number }>>("SELECT COUNT(*) as n FROM bug_reports", []),
        query<Array<RowDataPacket & { n: number }>>("SELECT COUNT(*) as n FROM account_terms_acceptances", []),
      ]);
      return {
        accounts: accounts[0]?.n ?? 0,
        cgu: cgu[0]?.n ?? 0,
        bugs: bugs[0]?.n ?? 0,
        acceptances: acceptances[0]?.n ?? 0,
      };
    } catch {
      return { accounts: "—", cgu: "—", bugs: "—", acceptances: "—" };
    }
  },
  ["admin-stats"],
  { revalidate: 60, tags: ["admin-stats"] }
);
```

(Note importante : le type de retour reste compatible — `unstable_cache` préserve la signature de la fonction passée. L'appel `await getStats()` dans `AdminHomePage` n'a PAS besoin de changer.)

- [ ] **Step 4 : Vérifier le diff**

```bash
git diff web-portal/app/admin/page.tsx
```

Expected : 2 chunks — ajout de l'import + transformation de `async function getStats` en `const getStats = unstable_cache(...)`. AdminHomePage et tout le JSX inchangés.

- [ ] **Step 5 : Commit**

```bash
git add web-portal/app/admin/page.tsx
git commit -m "$(cat <<'EOF'
perf(web-portal): cache admin stats 60s via unstable_cache

Évite 4× COUNT(*) (accounts, terms_editions, bug_reports,
account_terms_acceptances) à chaque render server-side de /admin.

Cache isolé via clé "admin-stats" + tag "admin-stats" → invalidation
manuelle possible plus tard via revalidateTag(...) après mutations
importantes. revalidate 60s suffit pour des stats peu critiques.

getSession() reste dynamique (auth par cookie), seul getStats() est mis
en cache. Donc aucun risque de leak inter-utilisateurs : les stats
sont globales pour tous les admins.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4 : Migrer les 9 sites `console.error/warn` vers le logger

**Files:** Modify (9 sites dans 8 fichiers).

Cette task migre tous les sites en un seul commit pour rester compact. Chaque sous-step est un fichier.

- [ ] **Step 1 : `web-portal/lib/auth/passwordRecovery.ts` — `console.warn` SMTP**

Lis les imports en tête (lignes 1-5). Selon que la PR 1 a été mergée ou pas, l'import `requireEnv` peut être présent ou absent.

**Cas A — PR 1 non mergée (on part de main, requireEnv absent)** :
Ajoute en fin de bloc imports :
```typescript
import { logWarn } from "@/lib/log";
```

**Cas B — PR 1 déjà mergée (requireEnv présent en L.5)** :
Le bloc d'imports est :
```typescript
import type { ResultSetHeader, RowDataPacket } from "mysql2/promise";
import { createHash, createHmac, randomBytes, timingSafeEqual } from "node:crypto";
import { query } from "@/lib/db/connection";
import { hashPasswordForGameMaster } from "@/lib/auth/gamePasswordHash";
import { requireEnv } from "@/lib/env";
```
Ajoute à la suite :
```typescript
import { logWarn } from "@/lib/log";
```

Puis remplace ligne ~440 :

old_string (la ligne exacte telle qu'elle est) :
```typescript
    console.warn(`[password-recovery] SMTP non configure, lien genere pour ${to}: ${resetUrl}`);
```

new_string :
```typescript
    logWarn("password-recovery", "SMTP non configuré, lien généré localement", { to, resetUrl });
```

- [ ] **Step 2 : `web-portal/app/api/bugs/route.ts` — `console.error`**

Lis les imports en tête. Ajoute (à la fin du bloc) :
```typescript
import { logError } from "@/lib/log";
```

(Si la convention apparente est en chemin relatif, utilise `"../../../lib/log"` à la place — vérifie d'abord les autres imports du fichier.)

Puis remplace ligne 36 :

old_string :
```typescript
    console.error('[POST /api/bugs]', err)
```

new_string :
```typescript
    logError("POST /api/bugs", "Insert bug report failed", { err })
```

- [ ] **Step 3 : `web-portal/app/admin/acceptances/page.tsx`**

Ajoute import `logError` cohérent avec les autres imports du fichier (`@/lib/log` si convention `@/`).

Remplace ligne 36 :

old_string :
```typescript
    console.error("[AdminAcceptancesPage] DB error:", err);
```

new_string :
```typescript
    logError("AdminAcceptancesPage", "DB error", { err });
```

- [ ] **Step 4 : `web-portal/app/admin/players/page.tsx`**

Ajoute import `logError`.

Remplace ligne 75 :

old_string :
```typescript
    console.error('[AdminPlayersPage] DB error:', err)
```

new_string :
```typescript
    logError("AdminPlayersPage", "DB error", { err })
```

- [ ] **Step 5 : `web-portal/app/api/admin/cgu/route.ts`**

Ajoute import `logError`.

Remplace ligne 65 :

old_string :
```typescript
    console.error('[POST /api/admin/cgu]', err)
```

new_string :
```typescript
    logError("POST /api/admin/cgu", "Operation failed", { err })
```

- [ ] **Step 6 : `web-portal/app/api/player/privacy/route.ts`**

Ajoute import `logError`.

Remplace ligne 30 :

old_string :
```typescript
    console.error('[PATCH /api/player/privacy]', err)
```

new_string :
```typescript
    logError("PATCH /api/player/privacy", "Update failed", { err })
```

- [ ] **Step 7 : `web-portal/app/player/cgu/page.tsx`**

Ajoute import `logError`.

Remplace ligne 40 :

old_string :
```typescript
    console.error("[PlayerCguPage] query error:", err);
```

new_string :
```typescript
    logError("PlayerCguPage", "query error", { err });
```

- [ ] **Step 8 : `web-portal/app/player/privacy/page.tsx` (2 lignes)**

Ajoute import `logError` (une fois).

Remplace ligne 42 :

old_string :
```typescript
    console.error('[PrivacyPage] CGU query error:', err)
```

new_string :
```typescript
    logError("PrivacyPage", "CGU query error", { err })
```

Remplace ligne 52 :

old_string :
```typescript
    console.error('[PrivacyPage] Privacy query error:', err)
```

new_string :
```typescript
    logError("PrivacyPage", "Privacy query error", { err })
```

- [ ] **Step 9 : Vérifier le diff complet**

```bash
git diff --stat
git diff
```

Expected : 8 fichiers modifiés, ~16 lignes ajoutées (8 imports + 9 remplacements ; le fichier privacy/page.tsx a 2 remplacements mais 1 seul import).

- [ ] **Step 10 : Stage + commit**

```bash
git add web-portal/lib/auth/passwordRecovery.ts \
        web-portal/app/api/bugs/route.ts \
        web-portal/app/admin/acceptances/page.tsx \
        web-portal/app/admin/players/page.tsx \
        web-portal/app/api/admin/cgu/route.ts \
        web-portal/app/api/player/privacy/route.ts \
        web-portal/app/player/cgu/page.tsx \
        web-portal/app/player/privacy/page.tsx
git status --short
git commit -m "$(cat <<'EOF'
refactor(web-portal): migrate 9 console.error/warn to structured logger

Remplace les 9 sites console.error/warn éparpillés par logError/logWarn
du helper lib/log.ts (introduit par la commit précédent feat: add
structured logger helper).

Sites migrés :
- lib/auth/passwordRecovery.ts (SMTP non configuré)
- app/api/bugs/route.ts (insert bug failed)
- app/admin/acceptances/page.tsx (DB error)
- app/admin/players/page.tsx (DB error)
- app/api/admin/cgu/route.ts (operation failed)
- app/api/player/privacy/route.ts (update failed)
- app/player/cgu/page.tsx (query error)
- app/player/privacy/page.tsx (CGU + Privacy query errors)

Bénéfice : output structuré JSON en prod (parseable par ELK/Loki/
Grafana), texte préfixé en dev (lisible). Error sérialisé proprement.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5 : Vérification finale (grep zero matches)

**Files:** Aucun — vérification.

- [ ] **Step 1 : Grep pour s'assurer qu'aucun `console.error/warn` ne traîne**

```bash
grep -rnE 'console\.(error|warn)' web-portal/ --include='*.ts' --include='*.tsx'
```

Expected : **0 matches**. Si des matches apparaissent dans des fichiers que tu n'as PAS modifiés, c'est qu'il y a un site oublié — créer une étape supplémentaire pour le corriger de la même manière.

Si la commande retourne uniquement des matches dans `lib/log.ts` lui-même (qui utilise `console.error/warn/log` sous le capot), c'est ATTENDU : le logger est l'unique consommateur autorisé.

```bash
grep -nE 'console\.(error|warn)' web-portal/lib/log.ts
```

Expected : ~3-6 matches dans `lib/log.ts` (les 3 niveaux × 2 branches prod/dev).

- [ ] **Step 2 : Vérifier qu'aucun `console.log` non-logger ne s'est glissé**

```bash
grep -rn 'console\.log' web-portal/ --include='*.ts' --include='*.tsx' | grep -v 'web-portal/lib/log.ts'
```

Expected : 0 matches (sauf si du `console.log` existait avant la PR — laisser tel quel, hors scope F10 qui ciblait error/warn).

- [ ] **Step 3 : Vérifier que tous les imports `@/lib/log` sont résolus**

```bash
grep -rn 'from "@/lib/log"' web-portal/ --include='*.ts' --include='*.tsx'
```

Expected : 8 lignes (8 fichiers ont importé `logError` ou `logWarn`).

```bash
grep -rn 'from "@/lib/log"' web-portal/app/player/privacy/page.tsx
```

Expected : 1 ligne (l'import n'est ajouté qu'une fois même s'il y a 2 sites migrés).

- [ ] **Step 4 : Aucun commit ici — c'est de la pure vérification**

Si tous les checks passent, passe à Task 6. Si un check échoue, retourne à Task 4 et corrige.

---

## Task 6 : Push + créer la PR

**Files:** Aucun — opérations git/gh.

- [ ] **Step 1 : État final**

```bash
git log --oneline origin/main..HEAD
git status
git branch --show-current
```

Expected : branch `claude/pr4-web-portal-polish`, working tree clean, 3 commits ahead de main :
1. `docs(plan): plan d'implémentation PR 4`
2. `feat(web-portal): add structured logger helper`
3. `perf(web-portal): cache admin stats 60s via unstable_cache`
4. `refactor(web-portal): migrate 9 console.error/warn to structured logger`

(4 commits exactement, pas 3 — j'avais compté trop court.)

- [ ] **Step 2 : Push**

```bash
git push -u origin claude/pr4-web-portal-polish
```

- [ ] **Step 3 : Créer la PR via `gh`**

```bash
gh pr create --title "perf(web-portal): cache admin stats + structured logger" --body "$(cat <<'EOF'
## Summary

- **Cache admin stats** : wrap `getStats()` dans `unstable_cache(..., ["admin-stats"], { revalidate: 60 })`. Évite 4× `COUNT(*)` à chaque render server-side de `/admin`. Cache isolé du flux `getSession()` (auth reste dynamique). Tag `"admin-stats"` permet une invalidation manuelle ultérieure si besoin.
- **Logger structuré** : helper minimal `web-portal/lib/log.ts` exposant `logInfo/logWarn/logError(context, message, meta?)`. JSON one-line en prod (parseable par ELK/Loki/Grafana), texte préfixé en dev. Zero dépendance externe.
- **9 sites migrés** : remplace les `console.error/warn` éparpillés dans `lib/auth/passwordRecovery.ts`, 3 route handlers API (bugs, admin/cgu, player/privacy) et 5 pages (admin/acceptances, admin/players, player/cgu, player/privacy × 2). Sérialisation correcte des `Error` objects via meta.err.

PR issue de l'audit codebase 2026-05-27 ([rapport complet](docs/superpowers/audits/2026-05-27-codebase-audit.md), chantier E partiel F10).

## Test plan

- [ ] CI build-web verte.
- [ ] Smoke test prod : vérifier que les logs sont au format JSON one-line quand `NODE_ENV=production`. En dev (`NODE_ENV !== "production"`), output texte préfixé `[context]`.
- [ ] Charger 2× la page `/admin` en moins de 60s → la 2e fois doit servir les stats du cache (constatable via durée de réponse ou logs DB côté MySQL).
- [ ] Aucune régression UX : la page admin affiche les stats correctement.

## Déploiement

✅ **Web portal uniquement** — aucun redéploiement master ni shard. Pas de lock-step env (le logger lit `NODE_ENV` qui existe déjà dans toute conf Next.js prod).

## Suivi

PR 4 sur 4 du plan d'audit. Reste : PR 3 (stabilisation CI tests) et PR 2 (hygiène SQL serveur — gros lot lock-step master + DB).

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

- [ ] **Step 4 : Vérifier**

```bash
gh pr view --json url,number,state,title
```

Expected : JSON avec URL de la PR, état OPEN.

---

## Self-review

**Spec coverage (vs `2026-05-27-codebase-audit.md` section 8 PR 4 / F10) :**
- ✅ Cache `revalidatePath`/`revalidateTag` sur stats admin → Task 3 (variante : `unstable_cache` qui est l'idiomatic Next.js 14 pour ce cas, avec tag pour permettre `revalidateTag` ultérieur)
- ✅ Logger structuré pour remplacer les 7 `console.error/warn` → Tasks 2 + 4 (en réalité 9 sites, le rapport avait sous-estimé)
- ✅ Web portal uniquement, pas de redéploiement serveur → mentionné explicitement Task 6 et PR body
- ✅ Aucune dépendance externe ajoutée → vérifiable via `git diff package.json` (doit montrer 0 modif)

**Placeholder scan :** RAS. Tous les chemins de fichiers, lignes, code blocks et commandes git sont concrets.

**Type consistency :**
- `logInfo / logWarn / logError(context: string, message: string, meta?: Record<string, unknown>)` défini en Task 2, utilisé tel quel dans Task 4 (les 9 sites).
- `meta.err` traitée spécialement par le logger (sérialisation Error → object) — cohérent dans tous les usages Task 4.
- `getStats` reste appelable comme `await getStats()` (signature préservée par `unstable_cache`) — l'appel ligne 32 d'`admin/page.tsx` n'est pas touché.

**Risques / edge cases :**
- ✅ Convention import `@/lib/log` : confirmée par observation du fichier `passwordRecovery.ts` (utilise déjà `@/lib/db/connection`, `@/lib/auth/gamePasswordHash`).
- ✅ Conflict potentiel avec PR 1 sur `passwordRecovery.ts` : minimal (PR 1 a touché L.5 import + L.62 + L.287 + L.432-434 ; PR 4 touche L.~5-6 import et L.440). Si rebase nécessaire, trivial.
- ✅ Pas d'élargissement vers `console.log` (hors scope F10 qui cible error/warn) — explicité Task 5 Step 2.
