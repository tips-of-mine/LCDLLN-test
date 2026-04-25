# Web Portal — Auth & Navigation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finaliser le système d'authentification du portail Next.js et rendre la navigation conditionnelle selon la session (TAG-ID, déconnexion, Espace joueur, Admin).

**Architecture:** Cookie HMAC-SHA256 signé `lcdlln_session` contenant `{accountId, tagId, login, role}`. Le layout racine est un Server Component qui lit la session et la passe au `SiteHeader` en prop. Un middleware Next.js protège `/player/*` et `/admin/*`. `SiteHeader` est splitté en shell serveur + `NavToggle` client.

**Tech Stack:** Next.js 14 App Router, `node:crypto` (HMAC-SHA256, sync), TypeScript strict, Vitest (tests unitaires), MySQL2.

---

## Carte des fichiers

| Fichier | Action |
|---|---|
| `db/migrations/0020_accounts_role.sql` | Créer |
| `web-portal/.env.example` | Modifier |
| `web-portal/package.json` | Modifier (ajouter vitest) |
| `web-portal/vitest.config.ts` | Créer |
| `web-portal/lib/session.ts` | Créer |
| `web-portal/lib/session.test.ts` | Créer |
| `web-portal/lib/portalLogin.ts` | Modifier |
| `web-portal/app/api/auth/login/route.ts` | Modifier |
| `web-portal/app/api/auth/logout/route.ts` | Créer |
| `web-portal/middleware.ts` | Créer |
| `web-portal/app/layout.tsx` | Modifier |
| `web-portal/components/NavToggle.tsx` | Créer |
| `web-portal/components/SiteHeader.tsx` | Modifier (split server/client) |
| `web-portal/components/LoginForm.tsx` | Créer |
| `web-portal/app/login/page.tsx` | Modifier |

---

## Task 1 : Migration DB — colonne `role`

**Files:**
- Create: `db/migrations/0020_accounts_role.sql`

- [ ] **Step 1 : Créer le fichier de migration**

```sql
-- Migration 0020 — Colonne role sur les comptes (portail web).
-- Appliquer après 0019. Idempotente.
SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

SET @m20_c := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE()
    AND table_name   = 'accounts'
    AND column_name  = 'role'
);
SET @m20_s := IF(@m20_c = 0,
  'ALTER TABLE accounts ADD COLUMN role ENUM(''player'',''admin'',''moderator'') NOT NULL DEFAULT ''player'' AFTER tag_id',
  'SELECT 1');
PREPARE m20_p FROM @m20_s; EXECUTE m20_p; DEALLOCATE PREPARE m20_p;

SET FOREIGN_KEY_CHECKS = 1;
```

- [ ] **Step 2 : Vérifier l'idempotence (optionnel si DB disponible)**

```bash
mysql -u root -p lcdlln_master < db/migrations/0020_accounts_role.sql
mysql -u root -p lcdlln_master -e "DESCRIBE accounts;" | grep role
# Attendu : role  enum('player','admin','moderator')  NO  (empty)  player
# Relancer la migration une 2e fois → pas d'erreur
```

- [ ] **Step 3 : Commit**

```bash
git add db/migrations/0020_accounts_role.sql
git commit -m "feat(db): migration 0020 — colonne role sur accounts"
```

---

## Task 2 : Infrastructure de tests (Vitest)

**Files:**
- Modify: `web-portal/package.json`
- Create: `web-portal/vitest.config.ts`

- [ ] **Step 1 : Ajouter Vitest aux dépendances**

Dans `web-portal/package.json`, modifier :

```json
{
  "scripts": {
    "dev": "next dev -p 3000",
    "build": "next build",
    "start": "next start -p 3000",
    "lint": "next lint",
    "test": "vitest run",
    "test:watch": "vitest"
  },
  "dependencies": {
    "argon2": "^0.41.1",
    "mysql2": "^3.11.0",
    "next": "^14.2.18",
    "nodemailer": "^6.10.1",
    "react": "^18.3.1",
    "react-dom": "^18.3.1"
  },
  "devDependencies": {
    "@types/node": "^20.14.0",
    "@types/react": "^18.3.0",
    "@types/react-dom": "^18.3.0",
    "typescript": "^5.5.0",
    "vitest": "^1.6.0"
  }
}
```

- [ ] **Step 2 : Créer `web-portal/vitest.config.ts`**

```typescript
import { defineConfig } from "vitest/config";
import path from "path";

export default defineConfig({
  test: {
    environment: "node",
    globals: false,
  },
  resolve: {
    alias: {
      "@": path.resolve(__dirname, "./"),
    },
  },
});
```

- [ ] **Step 3 : Installer les dépendances**

```bash
cd web-portal
npm install
# Vérifier que vitest apparaît dans node_modules/.bin/vitest
```

- [ ] **Step 4 : Commit**

```bash
git add web-portal/package.json web-portal/vitest.config.ts
git commit -m "chore(web-portal): ajouter Vitest pour les tests unitaires"
```

---

## Task 3 : Module session (`lib/session.ts`)

**Files:**
- Create: `web-portal/lib/session.ts`
- Create: `web-portal/lib/session.test.ts`

Ce module tourne exclusivement en runtime Node.js (Server Components, API routes). Il utilise `node:crypto` (HMAC-SHA256 synchrone). Le middleware utilise un parseur léger sans crypto (voir Task 7).

- [ ] **Step 1 : Écrire les tests (fichier `web-portal/lib/session.test.ts`)**

```typescript
import { describe, it, expect, beforeEach, afterEach } from "vitest";
import { signSession, verifySession, readSession, type SessionPayload } from "./session";

const TEST_SECRET = "test-secret-at-least-32-chars-long-for-hmac-sha256!!";

const SAMPLE: SessionPayload = {
  v: 1,
  accountId: 42,
  tagId: "TAG001",
  login: "joueur1",
  role: "player",
};

function withSecret<T>(fn: () => T): T {
  const prev = process.env.SESSION_HMAC_SECRET;
  process.env.SESSION_HMAC_SECRET = TEST_SECRET;
  try {
    return fn();
  } finally {
    process.env.SESSION_HMAC_SECRET = prev ?? "";
  }
}

describe("signSession", () => {
  it("produit une chaîne avec un séparateur point", () => {
    const val = withSecret(() => signSession(SAMPLE));
    expect(val.split(".")).toHaveLength(2);
  });

  it("lève une erreur si SESSION_HMAC_SECRET est absent", () => {
    const prev = process.env.SESSION_HMAC_SECRET;
    process.env.SESSION_HMAC_SECRET = "";
    expect(() => signSession(SAMPLE)).toThrow("SESSION_HMAC_SECRET");
    process.env.SESSION_HMAC_SECRET = prev ?? "";
  });
});

describe("verifySession", () => {
  it("retourne le payload pour un cookie valide", () => {
    const val = withSecret(() => signSession(SAMPLE));
    const result = withSecret(() => verifySession(val));
    expect(result).toEqual(SAMPLE);
  });

  it("retourne null si la signature est altérée", () => {
    const val = withSecret(() => signSession(SAMPLE));
    const tampered = val.slice(0, -4) + "0000";
    expect(withSecret(() => verifySession(tampered))).toBeNull();
  });

  it("retourne null si le payload est altéré (signature ne correspond plus)", () => {
    const val = withSecret(() => signSession(SAMPLE));
    const [, sig] = val.split(".");
    const fakePayload = Buffer.from(
      JSON.stringify({ ...SAMPLE, role: "admin" })
    ).toString("base64url");
    expect(withSecret(() => verifySession(`${fakePayload}.${sig}`))).toBeNull();
  });

  it("retourne null si le cookie n'a pas de point", () => {
    expect(withSecret(() => verifySession("noseparator"))).toBeNull();
  });

  it("retourne null si SESSION_HMAC_SECRET est absent", () => {
    const prev = process.env.SESSION_HMAC_SECRET;
    process.env.SESSION_HMAC_SECRET = "";
    expect(verifySession("anything.here")).toBeNull();
    process.env.SESSION_HMAC_SECRET = prev ?? "";
  });

  it("retourne null si v !== 1", () => {
    const badPayload: unknown = { v: 2, accountId: 1, tagId: "", login: "x", role: "player" };
    process.env.SESSION_HMAC_SECRET = TEST_SECRET;
    const val = signSession(badPayload as SessionPayload);
    const result = verifySession(val);
    process.env.SESSION_HMAC_SECRET = "";
    expect(result).toBeNull();
  });
});

describe("readSession", () => {
  it("retourne null si le cookie est absent", () => {
    const store = { get: (_: string) => undefined };
    expect(withSecret(() => readSession(store))).toBeNull();
  });

  it("retourne le payload pour un cookie valide", () => {
    const cookieVal = withSecret(() => signSession(SAMPLE));
    const store = { get: (_: string) => ({ value: cookieVal }) };
    expect(withSecret(() => readSession(store))).toEqual(SAMPLE);
  });
});
```

- [ ] **Step 2 : Lancer les tests — vérifier qu'ils échouent**

```bash
cd web-portal && npm test
# Attendu : FAIL — "Cannot find module './session'"
```

- [ ] **Step 3 : Créer `web-portal/lib/session.ts`**

```typescript
import { createHmac, timingSafeEqual } from "node:crypto";

export const COOKIE_NAME = "lcdlln_session";
export const COOKIE_MAX_AGE_SEC = 60 * 60 * 24 * 7;

export type SessionPayload = {
  v: 1;
  accountId: number;
  tagId: string;
  login: string;
  role: "player" | "admin" | "moderator";
};

type CookieStore = { get(name: string): { value: string } | undefined };

function getSecret(): string {
  const s = process.env.SESSION_HMAC_SECRET;
  if (!s) throw new Error("SESSION_HMAC_SECRET non défini");
  return s;
}

function toBase64url(data: string): string {
  return Buffer.from(data, "utf8")
    .toString("base64")
    .replace(/\+/g, "-")
    .replace(/\//g, "_")
    .replace(/=/g, "");
}

function fromBase64url(s: string): string {
  const padded =
    s.replace(/-/g, "+").replace(/_/g, "/") +
    "=".repeat((4 - (s.length % 4)) % 4);
  return Buffer.from(padded, "base64").toString("utf8");
}

function hmacHex(payload: string, secret: string): string {
  return createHmac("sha256", secret).update(payload).digest("hex");
}

export function signSession(payload: SessionPayload): string {
  const secret = getSecret();
  const payloadB64 = toBase64url(JSON.stringify(payload));
  const sig = hmacHex(payloadB64, secret);
  return `${payloadB64}.${sig}`;
}

export function verifySession(cookieValue: string): SessionPayload | null {
  try {
    const secret = process.env.SESSION_HMAC_SECRET;
    if (!secret) return null;

    const dotIdx = cookieValue.lastIndexOf(".");
    if (dotIdx === -1) return null;

    const payloadB64 = cookieValue.slice(0, dotIdx);
    const sig = cookieValue.slice(dotIdx + 1);
    const expectedSig = hmacHex(payloadB64, secret);

    // Comparaison en temps constant (anti-timing attack)
    if (sig.length !== 64) return null;
    const sigBuf = Buffer.from(sig, "hex");
    const expectedBuf = Buffer.from(expectedSig, "hex");
    if (sigBuf.length !== 32 || expectedBuf.length !== 32) return null;
    if (!timingSafeEqual(sigBuf, expectedBuf)) return null;

    const parsed = JSON.parse(fromBase64url(payloadB64)) as SessionPayload;
    if (parsed.v !== 1) return null;
    return parsed;
  } catch {
    return null;
  }
}

export function readSession(cookieStore: CookieStore): SessionPayload | null {
  const val = cookieStore.get(COOKIE_NAME)?.value;
  if (!val) return null;
  return verifySession(val);
}
```

- [ ] **Step 4 : Lancer les tests — vérifier qu'ils passent**

```bash
cd web-portal && npm test
# Attendu : PASS — toutes les suites vertes
```

- [ ] **Step 5 : Commit**

```bash
git add web-portal/lib/session.ts web-portal/lib/session.test.ts
git commit -m "feat(web-portal): module session HMAC-SHA256 avec tests"
```

---

## Task 4 : Mettre à jour `lib/portalLogin.ts`

**Files:**
- Modify: `web-portal/lib/portalLogin.ts`

- [ ] **Step 1 : Remplacer le contenu du fichier**

```typescript
import { scryptSync, timingSafeEqual } from "node:crypto";
import type { RowDataPacket } from "mysql2/promise";
import { query } from "@/lib/db";
import { verifyGameMasterPassword } from "@/lib/gamePasswordHash";

function normalizeLower(value: string): string {
  return value.trim().toLowerCase();
}

function verifyLegacyScryptPassword(password: string, stored: string): boolean {
  if (!stored.startsWith("scrypt$")) return false;
  const parts = stored.split("$");
  if (parts.length !== 3) return false;
  try {
    const salt = Buffer.from(parts[1], "hex");
    const expected = Buffer.from(parts[2], "hex");
    if (salt.length === 0 || expected.length === 0) return false;
    const derived = scryptSync(password, salt, 64);
    if (derived.length !== expected.length) return false;
    return timingSafeEqual(derived, expected);
  } catch {
    return false;
  }
}

export type PortalLoginResult =
  | { ok: true; accountId: number; login: string; tagId: string; role: string }
  | { ok: false; code: "missing" | "invalid" | "db" };

export async function verifyPortalCredentials(
  identifier: string,
  plainPassword: string,
): Promise<PortalLoginResult> {
  const idTrim = identifier.trim();
  if (!idTrim || !plainPassword) {
    return { ok: false, code: "missing" };
  }
  try {
    const rows = await query<
      Array<
        RowDataPacket & {
          id: number;
          login: string;
          password_hash: string;
          tag_id: string;
          role: string;
        }
      >
    >(
      `SELECT id, login, password_hash, tag_id, role
       FROM accounts
       WHERE LOWER(email) = ? OR login = ?
       LIMIT 1`,
      [normalizeLower(idTrim), idTrim],
    );

    const row = rows[0];
    if (!row) return { ok: false, code: "invalid" };

    const dbLogin = row.login.trim();
    const tagId = row.tag_id ?? "";
    const role = row.role ?? "player";

    if (await verifyGameMasterPassword(dbLogin, plainPassword, row.password_hash)) {
      return { ok: true, accountId: row.id, login: dbLogin, tagId, role };
    }
    if (verifyLegacyScryptPassword(plainPassword, row.password_hash)) {
      return { ok: true, accountId: row.id, login: dbLogin, tagId, role };
    }
    return { ok: false, code: "invalid" };
  } catch {
    return { ok: false, code: "db" };
  }
}
```

- [ ] **Step 2 : Vérifier la compilation TypeScript**

```bash
cd web-portal && npx tsc --noEmit
# Attendu : aucune erreur
```

- [ ] **Step 3 : Commit**

```bash
git add web-portal/lib/portalLogin.ts
git commit -m "feat(web-portal): portalLogin retourne tagId et role"
```

---

## Task 5 : Mettre à jour la route login + créer logout

**Files:**
- Modify: `web-portal/app/api/auth/login/route.ts`
- Create: `web-portal/app/api/auth/logout/route.ts`
- Modify: `web-portal/.env.example`

- [ ] **Step 1 : Remplacer `app/api/auth/login/route.ts`**

```typescript
import { NextResponse } from "next/server";
import { cookies } from "next/headers";
import { verifyPortalCredentials } from "@/lib/portalLogin";
import { signSession, COOKIE_NAME, COOKIE_MAX_AGE_SEC } from "@/lib/session";

const LEGACY_COOKIE = "lcdlln_portal_account";

function sanitizeNext(raw: unknown): string {
  if (typeof raw !== "string") return "/player";
  const trimmed = raw.trim();
  if (!trimmed.startsWith("/") || trimmed.startsWith("//")) return "/player";
  return trimmed;
}

export async function POST(request: Request) {
  try {
    const body = (await request.json()) as {
      identifier?: string;
      password?: string;
      next?: string;
    };
    const identifier = typeof body.identifier === "string" ? body.identifier : "";
    const password = typeof body.password === "string" ? body.password : "";

    const result = await verifyPortalCredentials(identifier, password);
    if (!result.ok) {
      const status = result.code === "db" ? 503 : 401;
      const message =
        result.code === "missing"
          ? "Identifiant et mot de passe requis."
          : result.code === "db"
            ? "Service temporairement indisponible (base de données)."
            : "Identifiant ou mot de passe incorrect.";
      return NextResponse.json({ ok: false, message }, { status });
    }

    const sessionValue = signSession({
      v: 1,
      accountId: result.accountId,
      tagId: result.tagId,
      login: result.login,
      role: result.role as "player" | "admin" | "moderator",
    });

    const jar = cookies();
    jar.delete(LEGACY_COOKIE);
    jar.set(COOKIE_NAME, sessionValue, {
      httpOnly: true,
      sameSite: "lax",
      secure: process.env.NODE_ENV === "production",
      path: "/",
      maxAge: COOKIE_MAX_AGE_SEC,
    });

    return NextResponse.json({ ok: true, redirect: sanitizeNext(body.next) });
  } catch {
    return NextResponse.json({ ok: false, message: "Requête invalide." }, { status: 400 });
  }
}
```

- [ ] **Step 2 : Créer `app/api/auth/logout/route.ts`**

```typescript
import { NextResponse } from "next/server";
import { cookies } from "next/headers";
import { COOKIE_NAME } from "@/lib/session";

export async function POST() {
  cookies().delete(COOKIE_NAME);
  return NextResponse.json({ ok: true });
}
```

- [ ] **Step 3 : Mettre à jour `.env.example`**

Vérifier si le fichier existe (`web-portal/.env.example`). S'il n'existe pas, le créer. Y ajouter :

```bash
# Base de données MySQL (format DSN)
DATABASE_URL=mysql://user:password@localhost:3306/lcdlln_master

# Secret HMAC pour la signature des cookies de session (min. 32 caractères aléatoires)
# Générer avec : node -e "console.log(require('crypto').randomBytes(32).toString('hex'))"
SESSION_HMAC_SECRET=changez-moi-avec-une-valeur-aleatoire-de-64-chars
```

- [ ] **Step 4 : Vérifier la compilation TypeScript**

```bash
cd web-portal && npx tsc --noEmit
# Attendu : aucune erreur
```

- [ ] **Step 5 : Commit**

```bash
git add web-portal/app/api/auth/login/route.ts \
        web-portal/app/api/auth/logout/route.ts \
        web-portal/.env.example
git commit -m "feat(web-portal): route login écrit cookie signé, route logout efface cookie"
```

---

## Task 6 : Middleware de protection des routes

**Files:**
- Create: `web-portal/middleware.ts`

Le middleware s'exécute dans l'Edge runtime de Next.js — `node:crypto` n'est pas disponible. Il parse le payload du cookie en base64 sans vérification HMAC (la sécurité cryptographique est assurée par les Server Components et routes API). Le middleware est une barrière UX : redirection vers login pour les utilisateurs non identifiés.

- [ ] **Step 1 : Créer `web-portal/middleware.ts`**

```typescript
import { NextRequest, NextResponse } from "next/server";

const COOKIE_NAME = "lcdlln_session";
const ADMIN_ROLES = new Set(["admin", "moderator"]);

function parsePayload(cookieVal: string): { role?: string } | null {
  try {
    const dotIdx = cookieVal.lastIndexOf(".");
    if (dotIdx === -1) return null;
    const b64 = cookieVal
      .slice(0, dotIdx)
      .replace(/-/g, "+")
      .replace(/_/g, "/");
    const padded = b64.padEnd(b64.length + ((4 - (b64.length % 4)) % 4), "=");
    return JSON.parse(atob(padded)) as { role?: string };
  } catch {
    return null;
  }
}

export function middleware(request: NextRequest) {
  const { pathname } = request.nextUrl;
  const cookieVal = request.cookies.get(COOKIE_NAME)?.value;
  const payload = cookieVal ? parsePayload(cookieVal) : null;

  if (pathname.startsWith("/admin")) {
    if (!payload) {
      const next = encodeURIComponent(pathname + request.nextUrl.search);
      return NextResponse.redirect(new URL(`/login?next=${next}`, request.url));
    }
    if (!ADMIN_ROLES.has(payload.role ?? "")) {
      return NextResponse.redirect(new URL("/", request.url));
    }
  }

  if (pathname.startsWith("/player")) {
    if (!payload) {
      const next = encodeURIComponent(pathname + request.nextUrl.search);
      return NextResponse.redirect(new URL(`/login?next=${next}`, request.url));
    }
  }

  return NextResponse.next();
}

export const config = {
  matcher: ["/player/:path*", "/admin/:path*"],
};
```

- [ ] **Step 2 : Vérifier la compilation TypeScript**

```bash
cd web-portal && npx tsc --noEmit
# Attendu : aucune erreur
```

- [ ] **Step 3 : Commit**

```bash
git add web-portal/middleware.ts
git commit -m "feat(web-portal): middleware protection routes /player et /admin"
```

---

## Task 7 : Mettre à jour le layout racine

**Files:**
- Modify: `web-portal/app/layout.tsx`

- [ ] **Step 1 : Remplacer le contenu de `app/layout.tsx`**

```typescript
import type { Metadata } from "next";
import { cookies } from "next/headers";
import "./globals.css";
import { SiteHeader } from "@/components/SiteHeader";
import { readSession } from "@/lib/session";

export const metadata: Metadata = {
  title: "Les Chroniques De La Lune Noire — Portail",
  description: "Portail joueur et administration — Les Chroniques De La Lune Noire",
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  const session = readSession(cookies());
  return (
    <html lang="fr">
      <body>
        <SiteHeader session={session} />
        <main>{children}</main>
        <footer className="wp-footer">
          <span
            style={{
              fontFamily: "var(--font-display)",
              fontSize: 11,
              letterSpacing: ".24em",
              textTransform: "uppercase",
            }}
          >
            © 2026 · Les Chroniques de la Lune Noire
          </span>
          <div className="wp-footer-links">
            <a href="/support">Support</a>
            <a href="/bugs">Signaler un bug</a>
            <a href="/contact">Contact</a>
          </div>
        </footer>
      </body>
    </html>
  );
}
```

- [ ] **Step 2 : Vérifier la compilation TypeScript**

```bash
cd web-portal && npx tsc --noEmit
# Attendu : aucune erreur (SiteHeader n'accepte pas encore session — erreur attendue à cette étape, sera corrigée en Task 8)
```

- [ ] **Step 3 : Commit**

```bash
git add web-portal/app/layout.tsx
git commit -m "feat(web-portal): layout lit la session et la passe à SiteHeader"
```

---

## Task 8 : Refactoriser `SiteHeader` (split server / client)

**Files:**
- Create: `web-portal/components/NavToggle.tsx`
- Modify: `web-portal/components/SiteHeader.tsx`

- [ ] **Step 1 : Créer `components/NavToggle.tsx`**

```typescript
"use client";

import { useState } from "react";

export function NavToggle({ children }: { children: React.ReactNode }) {
  const [open, setOpen] = useState(false);
  return (
    <>
      <button
        className="wp-nav-toggle"
        onClick={() => setOpen((v) => !v)}
        aria-label="Menu"
        aria-expanded={open}
      >
        {open ? "✕" : "☰"}
      </button>
      <nav
        className={`wp-nav${open ? " open" : ""}`}
        onClick={() => setOpen(false)}
      >
        {children}
      </nav>
    </>
  );
}
```

- [ ] **Step 2 : Remplacer `components/SiteHeader.tsx`**

```typescript
import Link from "next/link";
import type { SessionPayload } from "@/lib/session";
import { NavToggle } from "./NavToggle";

export function SiteHeader({ session }: { session: SessionPayload | null }) {
  return (
    <header className="wp-header">
      <Link href="/" className="wp-logo">
        <div className="wp-logo-moon" />
        <span className="wp-logo-text">Les Chroniques de la Lune Noire</span>
      </Link>

      <NavToggle>
        <Link href="/roadmap">Roadmap</Link>
        <Link href="/support">Support</Link>
        <Link href="/bugs">Signaler un bug</Link>

        {session !== null && (
          <Link href="/player">Espace joueur</Link>
        )}

        {(session?.role === "admin" || session?.role === "moderator") && (
          <Link href="/admin">Admin</Link>
        )}

        {session !== null ? (
          <>
            <span
              style={{
                fontFamily: "var(--font-display)",
                fontSize: 11,
                letterSpacing: ".2em",
                textTransform: "uppercase",
                color: "var(--ln-accent)",
                padding: "0 0.5rem",
                cursor: "default",
                userSelect: "none",
              }}
              title={`Connecté en tant que ${session.login}`}
            >
              {session.tagId || session.login}
            </span>
            <form action="/api/auth/logout" method="POST" style={{ display: "contents" }}>
              <button
                type="submit"
                className="btn btn-ghost"
                style={{ fontSize: 11, letterSpacing: ".15em", textTransform: "uppercase" }}
              >
                Déconnexion
              </button>
            </form>
          </>
        ) : (
          <Link href="/login" className="cta">Connexion</Link>
        )}
      </NavToggle>
    </header>
  );
}
```

Note : si `tag_id` est vide (comptes antérieurs à la migration 0016), on affiche le `login` en fallback.

- [ ] **Step 3 : Vérifier la compilation TypeScript**

```bash
cd web-portal && npx tsc --noEmit
# Attendu : aucune erreur
```

- [ ] **Step 4 : Lancer les tests**

```bash
cd web-portal && npm test
# Attendu : PASS — tous les tests session.test.ts passent
```

- [ ] **Step 5 : Commit**

```bash
git add web-portal/components/NavToggle.tsx web-portal/components/SiteHeader.tsx
git commit -m "feat(web-portal): SiteHeader conditionnel selon session (TAG-ID, déconnexion, gates)"
```

---

## Task 9 : Mettre à jour la page login

**Files:**
- Create: `web-portal/components/LoginForm.tsx`
- Modify: `web-portal/app/login/page.tsx`

La page login devient un Server Component (lit `searchParams.next`). Le formulaire est extrait dans `LoginForm` (Client Component).

- [ ] **Step 1 : Créer `components/LoginForm.tsx`**

```typescript
"use client";

import { useState, type FormEvent } from "react";
import Link from "next/link";
import { useRouter } from "next/navigation";

export function LoginForm({ nextPath }: { nextPath: string }) {
  const router = useRouter();
  const [identifier, setIdentifier] = useState("");
  const [password, setPassword] = useState("");
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState("");

  async function handleSubmit(e: FormEvent<HTMLFormElement>) {
    e.preventDefault();
    setLoading(true);
    setError("");
    try {
      const res = await fetch("/api/auth/login", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ identifier, password, next: nextPath }),
      });
      const data = (await res.json()) as {
        ok?: boolean;
        message?: string;
        redirect?: string;
      };
      if (!res.ok || !data.ok) {
        setError(data.message || "Connexion impossible.");
        return;
      }
      router.push(data.redirect || "/player");
      router.refresh();
    } catch {
      setError("Erreur réseau. Réessayez.");
    } finally {
      setLoading(false);
    }
  }

  return (
    <div className="wp-card" style={{ width: "100%", maxWidth: 440, padding: 28 }}>
      <form onSubmit={handleSubmit} style={{ display: "flex", flexDirection: "column", gap: 16 }}>
        {error && (
          <div className="wp-alert error">
            <span className="wp-alert-icon">✕</span>
            {error}
          </div>
        )}
        <div className="field">
          <label htmlFor="identifier">Identifiant ou e-mail</label>
          <input
            id="identifier"
            type="text"
            value={identifier}
            onChange={(ev) => setIdentifier(ev.target.value)}
            placeholder="Votre login ou adresse e-mail"
            required
            autoComplete="username"
            autoFocus
          />
        </div>
        <div className="field">
          <label htmlFor="password">Mot de passe</label>
          <input
            id="password"
            type="password"
            value={password}
            onChange={(ev) => setPassword(ev.target.value)}
            placeholder="Votre mot de passe"
            required
            autoComplete="current-password"
          />
        </div>
        <button
          type="submit"
          className="btn btn-primary"
          style={{ width: "100%" }}
          disabled={loading}
        >
          {loading ? "Connexion…" : "Se connecter"}
        </button>
      </form>
      <div
        style={{
          marginTop: 18,
          paddingTop: 18,
          borderTop: "1px solid var(--ln-border)",
          textAlign: "center",
        }}
      >
        <Link
          href="/password-recovery"
          style={{
            fontFamily: "var(--font-ui)",
            fontSize: 10,
            letterSpacing: ".2em",
            textTransform: "uppercase",
            color: "var(--ln-muted)",
          }}
        >
          Mot de passe oublié ?
        </Link>
      </div>
    </div>
  );
}
```

- [ ] **Step 2 : Remplacer `app/login/page.tsx`**

```typescript
import { LoginForm } from "@/components/LoginForm";

export default function LoginPage({
  searchParams,
}: {
  searchParams: { next?: string };
}) {
  const nextPath =
    typeof searchParams.next === "string" && searchParams.next.startsWith("/")
      ? searchParams.next
      : "/player";

  return (
    <div
      className="wp-main narrow"
      style={{ display: "flex", flexDirection: "column", alignItems: "center", paddingTop: 48 }}
    >
      <div style={{ textAlign: "center", marginBottom: 32 }}>
        <div
          style={{
            width: 64,
            height: 64,
            borderRadius: "50%",
            background:
              "radial-gradient(circle at 32% 30%, #2a2330 0%, #0B0712 55%, #000 100%)",
            border: "1px solid #1a1420",
            boxShadow: "0 0 30px rgba(74,123,184,.4)",
            margin: "0 auto 18px",
          }}
        />
        <div
          style={{
            fontFamily: "var(--font-display)",
            fontWeight: 700,
            fontSize: 20,
            letterSpacing: ".2em",
            textTransform: "uppercase",
            color: "var(--ln-text)",
            marginBottom: 6,
          }}
        >
          Connexion
        </div>
        <div
          style={{
            fontFamily: "var(--font-body)",
            fontStyle: "italic",
            fontSize: 14,
            color: "var(--ln-muted)",
          }}
        >
          Accédez à votre espace joueur
        </div>
      </div>

      <LoginForm nextPath={nextPath} />

      <p
        style={{
          fontFamily: "var(--font-body)",
          fontStyle: "italic",
          fontSize: 12.5,
          color: "var(--ln-muted)",
          textAlign: "center",
          maxWidth: 400,
          marginTop: 20,
          lineHeight: 1.6,
        }}
      >
        Pas encore de compte ? Créez-le dans le client jeu. Le même identifiant
        et mot de passe que pour le jeu fonctionnent ici.
      </p>
    </div>
  );
}
```

- [ ] **Step 3 : Vérifier la compilation TypeScript**

```bash
cd web-portal && npx tsc --noEmit
# Attendu : aucune erreur
```

- [ ] **Step 4 : Commit**

```bash
git add web-portal/components/LoginForm.tsx web-portal/app/login/page.tsx
git commit -m "feat(web-portal): page login gère ?next= via LoginForm client"
```

---

## Task 10 : Vérification end-to-end et commit final

- [ ] **Step 1 : Lancer le build Next.js**

```bash
cd web-portal && npm run build
# Attendu : succès — aucune erreur de compilation ou de lint
# Si erreur TypeScript sur cookies() : vérifier que layout.tsx n'est pas marqué "use client"
```

- [ ] **Step 2 : Démarrer le serveur de développement**

```bash
cd web-portal && npm run dev
# Ouvrir http://localhost:3000
```

- [ ] **Step 3 : Scénario test manuel — utilisateur non connecté**

Vérifier dans le navigateur :
- [ ] Menu : Roadmap, Support, Signaler un bug, **Connexion** visibles — "Espace joueur" et "Admin" absents
- [ ] Accès direct à `http://localhost:3000/player` → redirect vers `/login?next=%2Fplayer`
- [ ] Accès direct à `http://localhost:3000/admin` → redirect vers `/login?next=%2Fadmin`

- [ ] **Step 4 : Scénario test manuel — connexion joueur**

(Nécessite un compte en DB avec `SESSION_HMAC_SECRET` défini dans `.env.local`)

- [ ] Se connecter via `/login` avec un compte de rôle `player`
- [ ] Menu : TAG-ID ou login, bouton **Déconnexion** visibles — "Connexion" absent — "Admin" absent
- [ ] Accès à `/player` : fonctionne sans redirection
- [ ] Accès à `/admin` : redirect vers `/`
- [ ] Cliquer **Déconnexion** → redirect vers `/`, retour à l'état non connecté

- [ ] **Step 5 : Scénario test manuel — connexion admin**

(Compte avec `role = 'admin'` en DB — `UPDATE accounts SET role='admin' WHERE login='votre_login';`)

- [ ] Se connecter → Menu affiche aussi le lien **Admin**
- [ ] Accès à `/admin` : fonctionne sans redirection

- [ ] **Step 6 : Mettre à jour CODEBASE_MAP.md**

Dans la section 12 (Web portal), ajouter la ligne dans le tableau Pages et composants :

```markdown
| `web-portal/middleware.ts` | Protection routes `/player/*` et `/admin/*` via cookie HMAC signé. |
| `web-portal/lib/session.ts` | Signature/vérification du cookie `lcdlln_session` (HMAC-SHA256). |
| `web-portal/components/NavToggle.tsx` | Toggle menu mobile (Client Component). |
| `web-portal/components/LoginForm.tsx` | Formulaire de connexion (Client Component, reçoit `nextPath`). |
```

Et mettre à jour la date de la note en haut du fichier.

- [ ] **Step 7 : Commit final**

```bash
git add CODEBASE_MAP.md
git commit -m "docs(CODEBASE_MAP): mise à jour section 12 — auth & navigation"
```

---

## Vérification post-implémentation

| Spec §  | Couvert par |
|---|---|
| Migration 0020 `role` | Task 1 |
| `lib/session.ts` (signSession, verifySession, readSession) | Task 3 |
| `portalLogin.ts` retourne `tagId` et `role` | Task 4 |
| Route login écrit `lcdlln_session`, supprime ancien cookie | Task 5 |
| Route logout efface le cookie | Task 5 |
| Middleware `/player/*` → redirect login | Task 6 |
| Middleware `/admin/*` → redirect login ou `/` si rôle insuffisant | Task 6 |
| Layout Server Component lit la session | Task 7 |
| SiteHeader conditionnel (TAG-ID, déconnexion, Espace joueur, Admin) | Task 8 |
| Login page lit `?next=` et le transmet | Task 9 |
| `.env.example` documenté | Task 5 |
| Fallback TAG-ID → login si tag_id vide | Task 8 step 2 (note) |
| Protection open redirect dans sanitizeNext | Task 5 step 1 |
