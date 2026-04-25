# Espace joueur (Sous-projet B) — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Construire l'espace joueur complet sur `/player` — édition des détails du compte, statistiques en jeu (personnages + temps joué par serveur), historique CGU, paramètres de vie privée, contrôle parental et changement de mot de passe.

**Architecture:** Les Server Components lisent la session via `readSession(cookies())` et fetchent les données serveur ; les Client Components gèrent les interactions de formulaires et confirmations. Chaque section a sa propre route de page. Les fichiers `lib/` encapsulent toute la logique DB. Les API routes gèrent les mutations. Toutes les mutations vérifient la session via `readSession(cookies())` et renvoient 401 si absente.

**Tech Stack:** Next.js 14 App Router, MySQL2 (`query` de `@/lib/db`), node:crypto (randomBytes pour codes email), argon2 + `hashPasswordForGameMaster` (changement MDP), Vitest.

---

## Carte des fichiers

### Migrations
| Fichier | Contenu |
|---------|---------|
| `db/migrations/0021_accounts_player_profile.sql` | first_name, last_name, email_pending, profile_visibility, parental_email, parental_consent_at sur accounts + table email_change_tokens |
| `db/migrations/0022_character_stats.sql` | table character_stats (temps de jeu par personnage par serveur) |

### Lib (Node.js — côté serveur uniquement)
| Fichier | Exports |
|---------|---------|
| `web-portal/lib/playerProfile.ts` | `getAccountProfile`, `updateAccountProfile`, `requestEmailChange`, `confirmEmailChange` |
| `web-portal/lib/playerCharacters.ts` | `getCharactersWithStats`, `deleteCharacter` |
| `web-portal/lib/playerCgu.ts` | `getCguAcceptances` |
| `web-portal/lib/playerPrivacy.ts` | `getPrivacySettings`, `updatePrivacySettings` |
| `web-portal/lib/playerSecurity.ts` | `changePassword` |

### API Routes
| Fichier | Méthodes | Rôle |
|---------|----------|------|
| `web-portal/app/api/player/profile/route.ts` | GET, PATCH | Lire et mettre à jour le profil |
| `web-portal/app/api/player/profile/email/route.ts` | POST | Demander un changement d'email |
| `web-portal/app/api/player/profile/email/verify/route.ts` | POST | Confirmer le code de vérification |
| `web-portal/app/api/player/characters/[id]/route.ts` | DELETE | Supprimer un personnage |
| `web-portal/app/api/player/security/password/route.ts` | POST | Changer le mot de passe |
| `web-portal/app/api/player/privacy/route.ts` | PATCH | Mettre à jour les paramètres de vie privée |

### Pages (Server Components — lisent la session)
| Fichier | Route | Rôle |
|---------|-------|------|
| `web-portal/app/player/page.tsx` | `/player` | Dashboard avec vraies stats |
| `web-portal/app/player/account/page.tsx` | `/player/account` | Détails du compte |
| `web-portal/app/player/servers/page.tsx` | `/player/servers` | Personnages & temps joué |
| `web-portal/app/player/cgu/page.tsx` | `/player/cgu` | CGU avec vraies données |
| `web-portal/app/player/privacy/page.tsx` | `/player/privacy` | Vie privée |
| `web-portal/app/player/parental/page.tsx` | `/player/parental` | Contrôle parental |
| `web-portal/app/player/security/page.tsx` | `/player/security` | Mot de passe + MFA placeholder |

### Client Components
| Fichier | Rôle |
|---------|------|
| `web-portal/components/player/AccountForm.tsx` | Formulaire champs profil modifiables |
| `web-portal/components/player/EmailChangeForm.tsx` | Changement d'email + saisie du code |
| `web-portal/components/player/CharacterCard.tsx` | Carte personnage avec suppression en 2 étapes |
| `web-portal/components/player/PasswordChangeForm.tsx` | Formulaire changement mot de passe |
| `web-portal/components/player/PrivacyForm.tsx` | Sélecteur de visibilité du profil |

---

## Tâche 1 — Migration 0021 : Colonnes profil joueur

**Fichiers :**
- Créer : `db/migrations/0021_accounts_player_profile.sql`

- [ ] **Étape 1 : Écrire la migration**

```sql
-- Migration 0021 — Colonnes profil joueur + table email_change_tokens
-- Appliquer après 0020_accounts_role.sql

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

-- first_name
SET @m21_a := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'first_name'
);
SET @m21_sa := IF(@m21_a = 0,
  'ALTER TABLE accounts ADD COLUMN first_name VARCHAR(100) NOT NULL DEFAULT \'\'',
  'SELECT 1');
PREPARE m21_pa FROM @m21_sa; EXECUTE m21_pa; DEALLOCATE PREPARE m21_pa;

-- last_name
SET @m21_b := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'last_name'
);
SET @m21_sb := IF(@m21_b = 0,
  'ALTER TABLE accounts ADD COLUMN last_name VARCHAR(100) NOT NULL DEFAULT \'\'',
  'SELECT 1');
PREPARE m21_pb FROM @m21_sb; EXECUTE m21_pb; DEALLOCATE PREPARE m21_pb;

-- email_pending (nouvel email en attente de validation)
SET @m21_c := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'email_pending'
);
SET @m21_sc := IF(@m21_c = 0,
  'ALTER TABLE accounts ADD COLUMN email_pending VARCHAR(256) NULL DEFAULT NULL',
  'SELECT 1');
PREPARE m21_pc FROM @m21_sc; EXECUTE m21_pc; DEALLOCATE PREPARE m21_pc;

-- profile_visibility
SET @m21_d := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'profile_visibility'
);
SET @m21_sd := IF(@m21_d = 0,
  'ALTER TABLE accounts ADD COLUMN profile_visibility ENUM(\'public\',\'friends\',\'private\') NOT NULL DEFAULT \'public\'',
  'SELECT 1');
PREPARE m21_pd FROM @m21_sd; EXECUTE m21_pd; DEALLOCATE PREPARE m21_pd;

-- parental_email
SET @m21_e := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'parental_email'
);
SET @m21_se := IF(@m21_e = 0,
  'ALTER TABLE accounts ADD COLUMN parental_email VARCHAR(256) NULL DEFAULT NULL',
  'SELECT 1');
PREPARE m21_pe FROM @m21_se; EXECUTE m21_pe; DEALLOCATE PREPARE m21_pe;

-- parental_consent_at
SET @m21_f := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'parental_consent_at'
);
SET @m21_sf := IF(@m21_f = 0,
  'ALTER TABLE accounts ADD COLUMN parental_consent_at TIMESTAMP NULL DEFAULT NULL',
  'SELECT 1');
PREPARE m21_pf FROM @m21_sf; EXECUTE m21_pf; DEALLOCATE PREPARE m21_pf;

-- Table email_change_tokens
CREATE TABLE IF NOT EXISTS email_change_tokens (
  id         BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  account_id BIGINT UNSIGNED NOT NULL,
  new_email  VARCHAR(256)    NOT NULL,
  code       CHAR(6)         NOT NULL COMMENT '6 chiffres',
  expires_at TIMESTAMP       NOT NULL,
  used_at    TIMESTAMP       NULL DEFAULT NULL,
  created_at TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY idx_email_change_account (account_id),
  KEY idx_email_change_expires (expires_at),
  CONSTRAINT fk_email_change_account
    FOREIGN KEY (account_id) REFERENCES accounts (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Tokens de changement d''email — code 6 chiffres envoyé au nouvel email';

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
```

- [ ] **Étape 2 : Vérifier le fichier**

```bash
cat db/migrations/0021_accounts_player_profile.sql
```

Attendu : le fichier s'affiche sans erreur de syntaxe visible.

- [ ] **Étape 3 : Commit**

```bash
git add db/migrations/0021_accounts_player_profile.sql
git commit -m "feat(db): migration 0021 — colonnes profil joueur + email_change_tokens"
```

---

## Tâche 2 — Migration 0022 : Temps de jeu par personnage

**Fichiers :**
- Créer : `db/migrations/0022_character_stats.sql`

- [ ] **Étape 1 : Écrire la migration**

```sql
-- Migration 0022 — Statistiques de jeu par personnage par serveur
-- Le serveur de jeu met à jour total_play_seconds au fil des sessions.
-- Le portail web lit ces données en lecture seule.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

CREATE TABLE IF NOT EXISTS character_stats (
  character_id       BIGINT UNSIGNED NOT NULL,
  server_id          INT UNSIGNED    NOT NULL,
  total_play_seconds BIGINT UNSIGNED NOT NULL DEFAULT 0
                     COMMENT 'Secondes de jeu cumulées sur ce serveur',
  last_seen          DATETIME        NULL DEFAULT NULL
                     COMMENT 'Dernière fois que le personnage a été vu sur ce serveur',
  PRIMARY KEY (character_id, server_id),
  CONSTRAINT fk_char_stats_character
    FOREIGN KEY (character_id) REFERENCES characters (id) ON DELETE CASCADE,
  CONSTRAINT fk_char_stats_server
    FOREIGN KEY (server_id) REFERENCES game_servers (server_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Temps de jeu cumulé et dernière activité par (personnage, serveur)';

SET FOREIGN_KEY_CHECKS = 1;
```

- [ ] **Étape 2 : Commit**

```bash
git add db/migrations/0022_character_stats.sql
git commit -m "feat(db): migration 0022 — character_stats (temps de jeu par serveur)"
```

---

## Tâche 3 — lib/playerProfile.ts : getAccountProfile + updateAccountProfile

**Fichiers :**
- Créer : `web-portal/lib/playerProfile.ts`
- Créer : `web-portal/lib/playerProfile.test.ts`

- [ ] **Étape 1 : Écrire le test (TDD)**

```typescript
// web-portal/lib/playerProfile.test.ts
import { describe, it, expect, vi, beforeEach } from "vitest";

vi.mock("@/lib/db", () => ({
  query: vi.fn(),
}));
vi.mock("@/lib/passwordRecovery", () => ({
  getRecoveryProfile: vi.fn(),
  upsertRecoveryProfile: vi.fn(),
}));

import { query } from "@/lib/db";
import { getRecoveryProfile, upsertRecoveryProfile } from "@/lib/passwordRecovery";
import { getAccountProfile, updateAccountProfile } from "./playerProfile";

const mockQuery = vi.mocked(query);
const mockGetRecovery = vi.mocked(getRecoveryProfile);
const mockUpsertRecovery = vi.mocked(upsertRecoveryProfile);

beforeEach(() => {
  vi.clearAllMocks();
});

describe("getAccountProfile", () => {
  it("retourne le profil quand le compte existe", async () => {
    mockQuery.mockResolvedValueOnce([
      {
        id: 1,
        login: "joueur1",
        email: "j@test.com",
        tag_id: "TAG001",
        first_name: "Jean",
        last_name: "Dupont",
        email_pending: null,
        email_verified: 1,
        profile_visibility: "public",
        parental_email: null,
        parental_consent_at: null,
        role: "player",
      },
    ]);
    mockGetRecovery.mockResolvedValueOnce({
      accountId: 1,
      birthDate: "1990-05-10",
      address: "1 rue de la Lune",
      city: "Paris",
      postalCode: "75001",
      secretQuestions: [],
    });
    const result = await getAccountProfile(1);
    expect(result).not.toBeNull();
    expect(result!.login).toBe("joueur1");
    expect(result!.firstName).toBe("Jean");
    expect(result!.address).toBe("1 rue de la Lune");
  });

  it("retourne null si le compte n'existe pas", async () => {
    mockQuery.mockResolvedValueOnce([]);
    const result = await getAccountProfile(99);
    expect(result).toBeNull();
  });
});

describe("updateAccountProfile", () => {
  it("met à jour les champs du compte et le profil de récupération", async () => {
    mockQuery.mockResolvedValueOnce({ affectedRows: 1 });
    mockUpsertRecovery.mockResolvedValueOnce({ ok: true });
    const result = await updateAccountProfile(1, {
      firstName: "Marie",
      lastName: "Martin",
      address: "2 av. Soleil",
      city: "Lyon",
      postalCode: "69001",
    });
    expect(result.ok).toBe(true);
    expect(mockQuery).toHaveBeenCalledTimes(1);
    expect(mockUpsertRecovery).toHaveBeenCalledTimes(1);
  });

  it("retourne une erreur si firstName est vide", async () => {
    const result = await updateAccountProfile(1, {
      firstName: "  ",
      lastName: "Martin",
      address: "",
      city: "",
      postalCode: "",
    });
    expect(result.ok).toBe(false);
    expect((result as { ok: false; message: string }).message).toContain("Prénom");
  });
});
```

- [ ] **Étape 2 : Lancer le test pour vérifier qu'il échoue**

```bash
cd web-portal && npm run test -- --reporter=verbose 2>&1 | head -40
```

Attendu : FAIL — `Cannot find module './playerProfile'`

- [ ] **Étape 3 : Implémenter lib/playerProfile.ts**

```typescript
// web-portal/lib/playerProfile.ts
import type { RowDataPacket, ResultSetHeader } from "mysql2/promise";
import { query } from "@/lib/db";
import { getRecoveryProfile, upsertRecoveryProfile } from "@/lib/passwordRecovery";

type AccountRow = RowDataPacket & {
  id: number;
  login: string;
  email: string;
  tag_id: string;
  first_name: string;
  last_name: string;
  email_pending: string | null;
  email_verified: number;
  profile_visibility: "public" | "friends" | "private";
  parental_email: string | null;
  parental_consent_at: string | null;
  role: string;
};

export type AccountProfile = {
  accountId: number;
  login: string;
  email: string;
  tagId: string;
  firstName: string;
  lastName: string;
  emailPending: string | null;
  emailVerified: boolean;
  profileVisibility: "public" | "friends" | "private";
  parentalEmail: string | null;
  parentalConsentAt: string | null;
  role: string;
  // From account_recovery_profiles
  birthDate: string;
  address: string;
  city: string;
  postalCode: string;
};

export type UpdateProfileInput = {
  firstName: string;
  lastName: string;
  address: string;
  city: string;
  postalCode: string;
};

export type ProfileResult =
  | { ok: true }
  | { ok: false; message: string };

export async function getAccountProfile(accountId: number): Promise<AccountProfile | null> {
  const rows = await query<AccountRow[]>(
    `SELECT id, login, email, tag_id, first_name, last_name, email_pending,
            email_verified, profile_visibility, parental_email, parental_consent_at, role
     FROM accounts WHERE id = ? LIMIT 1`,
    [accountId],
  );
  const row = rows[0];
  if (!row) return null;

  const recovery = await getRecoveryProfile(accountId);

  return {
    accountId: row.id,
    login: row.login,
    email: row.email,
    tagId: row.tag_id ?? "",
    firstName: row.first_name,
    lastName: row.last_name,
    emailPending: row.email_pending,
    emailVerified: row.email_verified === 1,
    profileVisibility: row.profile_visibility,
    parentalEmail: row.parental_email,
    parentalConsentAt: row.parental_consent_at,
    role: row.role,
    birthDate: recovery?.birthDate ?? "",
    address: recovery?.address ?? "",
    city: recovery?.city ?? "",
    postalCode: recovery?.postalCode ?? "",
  };
}

export async function updateAccountProfile(
  accountId: number,
  input: UpdateProfileInput,
): Promise<ProfileResult> {
  const firstName = input.firstName.trim();
  const lastName = input.lastName.trim();

  if (!firstName) return { ok: false, message: "Prénom requis." };
  if (!lastName) return { ok: false, message: "Nom requis." };

  await query<ResultSetHeader>(
    "UPDATE accounts SET first_name = ?, last_name = ? WHERE id = ?",
    [firstName, lastName, accountId],
  );

  await upsertRecoveryProfile({
    accountId,
    birthDate: "",
    address: input.address.trim(),
    city: input.city.trim(),
    postalCode: input.postalCode.trim(),
    secretQuestions: [],
  });

  return { ok: true };
}
```

**Note :** `upsertRecoveryProfile` exige 3 questions secrètes — adapter pour accepter 0 questions en mettant à jour seulement les champs adresse. Voir étape 4.

- [ ] **Étape 4 : Adapter upsertRecoveryProfile pour accepter 0 questions**

Modifier `web-portal/lib/passwordRecovery.ts` ligne 185–186 : remplacer la vérification stricte à 3 questions par une vérification conditionnelle.

Chercher :
```typescript
  if (questions.length !== 3) {
    throw new Error("Trois questions secretes sont requises.");
  }
```

Remplacer par :
```typescript
  if (input.secretQuestions.length > 0 && questions.length !== 3) {
    throw new Error("Trois questions secretes sont requises.");
  }
```

Et dans la boucle d'insertion (ligne ~212), entourer le DELETE et les INSERTs :
```typescript
  if (questions.length > 0) {
    await query<ResultSetHeader>("DELETE FROM account_recovery_secret_questions WHERE account_id = ?", [input.accountId]);
    for (const item of questions) {
      await query<ResultSetHeader>(
        "INSERT INTO account_recovery_secret_questions (account_id, question, answer_hash) VALUES (?, ?, ?)",
        [input.accountId, item.question, hashAnswer(item.answer)],
      );
    }
  }
```

- [ ] **Étape 5 : Lancer les tests**

```bash
cd web-portal && npm run test -- --reporter=verbose 2>&1 | head -60
```

Attendu : tous les tests playerProfile passent (4 cas), tests session toujours OK.

- [ ] **Étape 6 : Commit**

```bash
git add web-portal/lib/playerProfile.ts web-portal/lib/playerProfile.test.ts web-portal/lib/passwordRecovery.ts
git commit -m "feat(player): getAccountProfile + updateAccountProfile + assouplissement upsertRecoveryProfile"
```

---

## Tâche 4 — lib/playerProfile.ts : requestEmailChange + confirmEmailChange

**Fichiers :**
- Modifier : `web-portal/lib/playerProfile.ts`
- Modifier : `web-portal/lib/playerProfile.test.ts`

- [ ] **Étape 1 : Ajouter les tests email change**

Ajouter à la fin de `web-portal/lib/playerProfile.test.ts` :

```typescript
vi.mock("node:crypto", async (importOriginal) => {
  const real = await importOriginal<typeof import("node:crypto")>();
  return { ...real, randomBytes: vi.fn(() => Buffer.from("123456", "utf8")) };
});

import { requestEmailChange, confirmEmailChange } from "./playerProfile";

describe("requestEmailChange", () => {
  it("retourne une erreur si email invalide", async () => {
    const result = await requestEmailChange(1, "pas-un-email");
    expect(result.ok).toBe(false);
    expect((result as { ok: false; message: string }).message).toContain("email");
  });

  it("insère un token si email valide et non pris", async () => {
    // email not taken check
    mockQuery.mockResolvedValueOnce([]); // SELECT COUNT(*)
    // INSERT token
    mockQuery.mockResolvedValueOnce({ affectedRows: 1 });
    // UPDATE email_pending + email_verified=0
    mockQuery.mockResolvedValueOnce({ affectedRows: 1 });
    const result = await requestEmailChange(1, "nouveau@test.com");
    expect(result.ok).toBe(true);
  });

  it("retourne une erreur si email déjà utilisé", async () => {
    mockQuery.mockResolvedValueOnce([{ cnt: 1 }]);
    const result = await requestEmailChange(1, "pris@test.com");
    expect(result.ok).toBe(false);
  });
});

describe("confirmEmailChange", () => {
  it("retourne une erreur si code invalide", async () => {
    mockQuery.mockResolvedValueOnce([]); // token not found
    const result = await confirmEmailChange(1, "000000");
    expect(result.ok).toBe(false);
  });

  it("applique le changement d'email si code valide", async () => {
    mockQuery.mockResolvedValueOnce([
      { id: 5, new_email: "nouveau@test.com", used_at: null, expired: 0 },
    ]);
    mockQuery.mockResolvedValueOnce({ affectedRows: 1 }); // UPDATE accounts
    mockQuery.mockResolvedValueOnce({ affectedRows: 1 }); // UPDATE token used_at
    const result = await confirmEmailChange(1, "123456");
    expect(result.ok).toBe(true);
  });
});
```

- [ ] **Étape 2 : Lancer les tests pour vérifier qu'ils échouent**

```bash
cd web-portal && npm run test -- --reporter=verbose 2>&1 | head -50
```

Attendu : FAIL — `requestEmailChange is not a function`

- [ ] **Étape 3 : Ajouter requestEmailChange + confirmEmailChange dans lib/playerProfile.ts**

Ajouter après la fonction `updateAccountProfile` :

```typescript
function isValidEmail(email: string): boolean {
  return /^[^\s@]+@[^\s@]+\.[^\s@]{2,}$/.test(email.trim());
}

export async function requestEmailChange(
  accountId: number,
  newEmail: string,
): Promise<ProfileResult> {
  const email = newEmail.trim().toLowerCase();
  if (!isValidEmail(email)) return { ok: false, message: "Adresse email invalide." };

  const taken = await query<Array<RowDataPacket & { cnt: number }>>(
    "SELECT COUNT(*) AS cnt FROM accounts WHERE LOWER(email) = ? AND id <> ?",
    [email, accountId],
  );
  if ((taken[0]?.cnt ?? 0) > 0) {
    return { ok: false, message: "Cette adresse email est déjà utilisée." };
  }

  const { randomBytes } = await import("node:crypto");
  const code = String(parseInt(randomBytes(3).toString("hex"), 16) % 1000000).padStart(6, "0");
  const expiresAt = new Date(Date.now() + 24 * 60 * 60 * 1000);
  const expiresAtStr = expiresAt.toISOString().slice(0, 19).replace("T", " ");

  await query<ResultSetHeader>(
    `INSERT INTO email_change_tokens (account_id, new_email, code, expires_at)
     VALUES (?, ?, ?, ?)`,
    [accountId, email, code, expiresAtStr],
  );

  await query<ResultSetHeader>(
    "UPDATE accounts SET email_pending = ?, email_verified = 0 WHERE id = ?",
    [email, accountId],
  );

  // Envoi email — log en dev, nodemailer en prod
  const portalUrl = (process.env.NEXT_PUBLIC_PORTAL_URL ?? "http://localhost:3000").replace(/\/+$/, "");
  console.info(
    `[email-change] Code ${code} pour account ${accountId} → ${email} | Lien: ${portalUrl}/player/account`,
  );

  return { ok: true };
}

export async function confirmEmailChange(
  accountId: number,
  code: string,
): Promise<ProfileResult> {
  const cleanCode = code.trim();
  if (!/^\d{6}$/.test(cleanCode)) return { ok: false, message: "Code invalide." };

  const rows = await query<
    Array<RowDataPacket & { id: number; new_email: string; used_at: string | null; expired: number }>
  >(
    `SELECT id, new_email, used_at,
            CASE WHEN expires_at < UTC_TIMESTAMP() THEN 1 ELSE 0 END AS expired
     FROM email_change_tokens
     WHERE account_id = ? AND code = ?
     ORDER BY created_at DESC LIMIT 1`,
    [accountId, cleanCode],
  );

  const token = rows[0];
  if (!token || token.used_at !== null || token.expired === 1) {
    return { ok: false, message: "Code invalide ou expiré." };
  }

  await query<ResultSetHeader>(
    "UPDATE accounts SET email = ?, email_pending = NULL, email_verified = 1 WHERE id = ?",
    [token.new_email, accountId],
  );
  await query<ResultSetHeader>(
    "UPDATE email_change_tokens SET used_at = UTC_TIMESTAMP() WHERE id = ?",
    [token.id],
  );

  return { ok: true };
}
```

- [ ] **Étape 4 : Lancer les tests**

```bash
cd web-portal && npm run test -- --reporter=verbose 2>&1 | head -80
```

Attendu : tous les tests playerProfile passent (8 cas).

- [ ] **Étape 5 : Commit**

```bash
git add web-portal/lib/playerProfile.ts web-portal/lib/playerProfile.test.ts
git commit -m "feat(player): requestEmailChange + confirmEmailChange"
```

---

## Tâche 5 — API Routes : profil et email

**Fichiers :**
- Créer : `web-portal/app/api/player/profile/route.ts`
- Créer : `web-portal/app/api/player/profile/email/route.ts`
- Créer : `web-portal/app/api/player/profile/email/verify/route.ts`

- [ ] **Étape 1 : Créer app/api/player/profile/route.ts**

```typescript
// web-portal/app/api/player/profile/route.ts
import { NextResponse } from "next/server";
import { cookies } from "next/headers";
import { readSession } from "@/lib/session";
import { getAccountProfile, updateAccountProfile } from "@/lib/playerProfile";

function unauthorized() {
  return NextResponse.json({ ok: false, message: "Non authentifié." }, { status: 401 });
}

export async function GET() {
  const session = readSession(cookies());
  if (!session) return unauthorized();

  const profile = await getAccountProfile(session.accountId);
  if (!profile) return NextResponse.json({ ok: false, message: "Compte introuvable." }, { status: 404 });

  return NextResponse.json({ ok: true, profile });
}

export async function PATCH(request: Request) {
  const session = readSession(cookies());
  if (!session) return unauthorized();

  let body: { firstName?: string; lastName?: string; address?: string; city?: string; postalCode?: string };
  try {
    body = await request.json();
  } catch {
    return NextResponse.json({ ok: false, message: "Requête invalide." }, { status: 400 });
  }

  const result = await updateAccountProfile(session.accountId, {
    firstName: typeof body.firstName === "string" ? body.firstName : "",
    lastName: typeof body.lastName === "string" ? body.lastName : "",
    address: typeof body.address === "string" ? body.address : "",
    city: typeof body.city === "string" ? body.city : "",
    postalCode: typeof body.postalCode === "string" ? body.postalCode : "",
  });

  return NextResponse.json(result, { status: result.ok ? 200 : 400 });
}
```

- [ ] **Étape 2 : Créer app/api/player/profile/email/route.ts**

```typescript
// web-portal/app/api/player/profile/email/route.ts
import { NextResponse } from "next/server";
import { cookies } from "next/headers";
import { readSession } from "@/lib/session";
import { requestEmailChange } from "@/lib/playerProfile";

export async function POST(request: Request) {
  const session = readSession(cookies());
  if (!session) return NextResponse.json({ ok: false, message: "Non authentifié." }, { status: 401 });

  let body: { newEmail?: string };
  try {
    body = await request.json();
  } catch {
    return NextResponse.json({ ok: false, message: "Requête invalide." }, { status: 400 });
  }

  const result = await requestEmailChange(
    session.accountId,
    typeof body.newEmail === "string" ? body.newEmail : "",
  );

  return NextResponse.json(result, { status: result.ok ? 200 : 400 });
}
```

- [ ] **Étape 3 : Créer app/api/player/profile/email/verify/route.ts**

```typescript
// web-portal/app/api/player/profile/email/verify/route.ts
import { NextResponse } from "next/server";
import { cookies } from "next/headers";
import { readSession } from "@/lib/session";
import { confirmEmailChange } from "@/lib/playerProfile";

export async function POST(request: Request) {
  const session = readSession(cookies());
  if (!session) return NextResponse.json({ ok: false, message: "Non authentifié." }, { status: 401 });

  let body: { code?: string };
  try {
    body = await request.json();
  } catch {
    return NextResponse.json({ ok: false, message: "Requête invalide." }, { status: 400 });
  }

  const result = await confirmEmailChange(
    session.accountId,
    typeof body.code === "string" ? body.code : "",
  );

  return NextResponse.json(result, { status: result.ok ? 200 : 400 });
}
```

- [ ] **Étape 4 : Commit**

```bash
git add web-portal/app/api/player/profile/
git commit -m "feat(api): routes /api/player/profile (GET/PATCH) + email change"
```

---

## Tâche 6 — Page /player/account + composants AccountForm et EmailChangeForm

**Fichiers :**
- Créer : `web-portal/app/player/account/page.tsx`
- Créer : `web-portal/components/player/AccountForm.tsx`
- Créer : `web-portal/components/player/EmailChangeForm.tsx`

- [ ] **Étape 1 : Créer components/player/AccountForm.tsx**

```typescript
// web-portal/components/player/AccountForm.tsx
"use client";

import { useState } from "react";
import type { AccountProfile } from "@/lib/playerProfile";

type Props = { profile: AccountProfile };

export function AccountForm({ profile }: Props) {
  const [firstName, setFirstName] = useState(profile.firstName);
  const [lastName, setLastName] = useState(profile.lastName);
  const [address, setAddress] = useState(profile.address);
  const [city, setCity] = useState(profile.city);
  const [postalCode, setPostalCode] = useState(profile.postalCode);
  const [status, setStatus] = useState<"idle" | "saving" | "ok" | "error">("idle");
  const [message, setMessage] = useState("");

  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault();
    setStatus("saving");
    setMessage("");
    const res = await fetch("/api/player/profile", {
      method: "PATCH",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ firstName, lastName, address, city, postalCode }),
    });
    const data = (await res.json()) as { ok: boolean; message?: string };
    if (data.ok) {
      setStatus("ok");
      setMessage("Profil mis à jour.");
    } else {
      setStatus("error");
      setMessage(data.message ?? "Erreur lors de la mise à jour.");
    }
  }

  return (
    <form onSubmit={handleSubmit} className="wp-form" style={{ display: "grid", gap: "1rem" }}>
      <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: "1rem" }}>
        <div>
          <label className="wp-label">Prénom</label>
          <input
            className="wp-input"
            value={firstName}
            onChange={(e) => setFirstName(e.target.value)}
            required
            maxLength={100}
          />
        </div>
        <div>
          <label className="wp-label">Nom</label>
          <input
            className="wp-input"
            value={lastName}
            onChange={(e) => setLastName(e.target.value)}
            required
            maxLength={100}
          />
        </div>
      </div>

      <div>
        <label className="wp-label">TAG-ID</label>
        <input
          className="wp-input"
          value={profile.tagId || profile.login}
          readOnly
          style={{ opacity: 0.6, cursor: "default" }}
        />
      </div>

      <div>
        <label className="wp-label">Email actuel</label>
        <input className="wp-input" value={profile.email} readOnly style={{ opacity: 0.6, cursor: "default" }} />
        {profile.emailPending && (
          <p style={{ fontSize: 12, color: "var(--ln-warning)", marginTop: 4 }}>
            Changement en attente vers <strong>{profile.emailPending}</strong> — vérifiez votre boîte mail.
          </p>
        )}
      </div>

      <div>
        <label className="wp-label">Adresse</label>
        <input
          className="wp-input"
          value={address}
          onChange={(e) => setAddress(e.target.value)}
          maxLength={255}
          placeholder="1 rue de la Lune Noire"
        />
      </div>

      <div style={{ display: "grid", gridTemplateColumns: "1fr 2fr", gap: "1rem" }}>
        <div>
          <label className="wp-label">Code postal</label>
          <input
            className="wp-input"
            value={postalCode}
            onChange={(e) => setPostalCode(e.target.value)}
            maxLength={20}
          />
        </div>
        <div>
          <label className="wp-label">Ville</label>
          <input
            className="wp-input"
            value={city}
            onChange={(e) => setCity(e.target.value)}
            maxLength={100}
          />
        </div>
      </div>

      {message && (
        <p style={{ fontSize: 13, color: status === "ok" ? "var(--ln-success)" : "var(--ln-danger)" }}>
          {message}
        </p>
      )}

      <button type="submit" className="btn btn-primary" disabled={status === "saving"}>
        {status === "saving" ? "Enregistrement…" : "Enregistrer"}
      </button>
    </form>
  );
}
```

- [ ] **Étape 2 : Créer components/player/EmailChangeForm.tsx**

```typescript
// web-portal/components/player/EmailChangeForm.tsx
"use client";

import { useState } from "react";

export function EmailChangeForm() {
  const [step, setStep] = useState<"request" | "verify">("request");
  const [newEmail, setNewEmail] = useState("");
  const [code, setCode] = useState("");
  const [status, setStatus] = useState<"idle" | "loading" | "ok" | "error">("idle");
  const [message, setMessage] = useState("");

  async function handleRequest(e: React.FormEvent) {
    e.preventDefault();
    setStatus("loading");
    setMessage("");
    const res = await fetch("/api/player/profile/email", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ newEmail }),
    });
    const data = (await res.json()) as { ok: boolean; message?: string };
    if (data.ok) {
      setStep("verify");
      setStatus("idle");
      setMessage("Un code à 6 chiffres a été envoyé à " + newEmail);
    } else {
      setStatus("error");
      setMessage(data.message ?? "Erreur.");
    }
  }

  async function handleVerify(e: React.FormEvent) {
    e.preventDefault();
    setStatus("loading");
    setMessage("");
    const res = await fetch("/api/player/profile/email/verify", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ code }),
    });
    const data = (await res.json()) as { ok: boolean; message?: string };
    if (data.ok) {
      setStatus("ok");
      setMessage("Email mis à jour avec succès. Reconnectez-vous.");
    } else {
      setStatus("error");
      setMessage(data.message ?? "Code invalide.");
    }
  }

  if (status === "ok") {
    return (
      <p style={{ color: "var(--ln-success)", fontSize: 13 }}>
        {message}
      </p>
    );
  }

  return (
    <div style={{ display: "grid", gap: "0.75rem" }}>
      {step === "request" ? (
        <form onSubmit={handleRequest} style={{ display: "grid", gap: "0.75rem" }}>
          <div>
            <label className="wp-label">Nouvel email</label>
            <input
              className="wp-input"
              type="email"
              value={newEmail}
              onChange={(e) => setNewEmail(e.target.value)}
              required
              placeholder="nouveau@example.com"
            />
          </div>
          {message && <p style={{ fontSize: 13, color: "var(--ln-danger)" }}>{message}</p>}
          <button type="submit" className="btn btn-secondary" disabled={status === "loading"}>
            {status === "loading" ? "Envoi…" : "Envoyer le code de vérification"}
          </button>
        </form>
      ) : (
        <form onSubmit={handleVerify} style={{ display: "grid", gap: "0.75rem" }}>
          {message && (
            <p style={{ fontSize: 13, color: status === "error" ? "var(--ln-danger)" : "var(--ln-muted)" }}>
              {message}
            </p>
          )}
          <div>
            <label className="wp-label">Code reçu par email</label>
            <input
              className="wp-input"
              value={code}
              onChange={(e) => setCode(e.target.value)}
              maxLength={6}
              pattern="\d{6}"
              required
              placeholder="123456"
            />
          </div>
          <div style={{ display: "flex", gap: "0.5rem" }}>
            <button type="submit" className="btn btn-primary" disabled={status === "loading"}>
              {status === "loading" ? "Vérification…" : "Valider le code"}
            </button>
            <button type="button" className="btn btn-ghost" onClick={() => { setStep("request"); setMessage(""); }}>
              Recommencer
            </button>
          </div>
        </form>
      )}
    </div>
  );
}
```

- [ ] **Étape 3 : Créer app/player/account/page.tsx**

```typescript
// web-portal/app/player/account/page.tsx
import { cookies } from "next/headers";
import { redirect } from "next/navigation";
import Link from "next/link";
import { readSession } from "@/lib/session";
import { getAccountProfile } from "@/lib/playerProfile";
import { AccountForm } from "@/components/player/AccountForm";
import { EmailChangeForm } from "@/components/player/EmailChangeForm";

export const dynamic = "force-dynamic";

export default async function PlayerAccountPage() {
  const session = readSession(cookies());
  if (!session) redirect("/login?next=/player/account");

  const profile = await getAccountProfile(session.accountId);
  if (!profile) redirect("/player");

  return (
    <div className="wp-main narrow">
      <div className="wp-page-header">
        <h1>Détail du compte</h1>
        <p>Gérez vos informations personnelles.</p>
      </div>

      <div className="wp-card" style={{ marginBottom: "1.5rem" }}>
        <div className="wp-section-title" style={{ marginBottom: "1rem" }}>Informations personnelles</div>
        <AccountForm profile={profile} />
      </div>

      <div className="wp-card">
        <div className="wp-section-title" style={{ marginBottom: "1rem" }}>Changer d'adresse email</div>
        <p style={{ fontSize: 13, color: "var(--ln-muted)", marginBottom: "1rem" }}>
          Un code de vérification sera envoyé à la nouvelle adresse. Votre compte sera temporairement marqué
          comme non vérifié jusqu'à confirmation.
        </p>
        <EmailChangeForm />
      </div>

      <div style={{ marginTop: "1.5rem" }}>
        <Link href="/player" className="btn btn-ghost">&larr; Espace joueur</Link>
      </div>
    </div>
  );
}
```

- [ ] **Étape 4 : Commit**

```bash
git add web-portal/app/player/account/ web-portal/components/player/
git commit -m "feat(player): page /player/account — AccountForm + EmailChangeForm"
```

---

## Tâche 7 — lib/playerCharacters.ts + API DELETE /api/player/characters/[id]

**Fichiers :**
- Créer : `web-portal/lib/playerCharacters.ts`
- Créer : `web-portal/lib/playerCharacters.test.ts`
- Créer : `web-portal/app/api/player/characters/[id]/route.ts`

- [ ] **Étape 1 : Écrire les tests**

```typescript
// web-portal/lib/playerCharacters.test.ts
import { describe, it, expect, vi, beforeEach } from "vitest";

vi.mock("@/lib/db", () => ({ query: vi.fn() }));

import { query } from "@/lib/db";
import { getCharactersWithStats, deleteCharacter } from "./playerCharacters";

const mockQuery = vi.mocked(query);

beforeEach(() => vi.clearAllMocks());

describe("getCharactersWithStats", () => {
  it("retourne les personnages avec leurs stats", async () => {
    mockQuery.mockResolvedValueOnce([
      {
        id: 1,
        name: "Arkan",
        slot: 0,
        server_name: "Serveur Alpha",
        server_id: 10,
        total_play_seconds: 3600,
        last_seen: "2026-04-20 12:00:00",
        created_at: "2026-01-01 00:00:00",
      },
    ]);
    const result = await getCharactersWithStats(42);
    expect(result).toHaveLength(1);
    expect(result[0].name).toBe("Arkan");
    expect(result[0].totalPlaySeconds).toBe(3600);
  });

  it("retourne un tableau vide si pas de personnages", async () => {
    mockQuery.mockResolvedValueOnce([]);
    const result = await getCharactersWithStats(42);
    expect(result).toHaveLength(0);
  });
});

describe("deleteCharacter", () => {
  it("supprime le personnage si il appartient au compte", async () => {
    mockQuery.mockResolvedValueOnce({ affectedRows: 1 });
    const result = await deleteCharacter(1, 42);
    expect(result.ok).toBe(true);
  });

  it("retourne une erreur si personnage introuvable ou non-owned", async () => {
    mockQuery.mockResolvedValueOnce({ affectedRows: 0 });
    const result = await deleteCharacter(99, 42);
    expect(result.ok).toBe(false);
  });
});
```

- [ ] **Étape 2 : Lancer les tests pour vérifier qu'ils échouent**

```bash
cd web-portal && npm run test -- --reporter=verbose 2>&1 | head -40
```

Attendu : FAIL — `Cannot find module './playerCharacters'`

- [ ] **Étape 3 : Implémenter lib/playerCharacters.ts**

```typescript
// web-portal/lib/playerCharacters.ts
import type { RowDataPacket, ResultSetHeader } from "mysql2/promise";
import { query } from "@/lib/db";

type CharacterRow = RowDataPacket & {
  id: number;
  name: string;
  slot: number;
  server_name: string | null;
  server_id: number | null;
  total_play_seconds: number | null;
  last_seen: string | null;
  created_at: string;
};

export type CharacterWithStats = {
  id: number;
  name: string;
  slot: number;
  serverName: string | null;
  serverId: number | null;
  totalPlaySeconds: number;
  lastSeen: string | null;
  createdAt: string;
};

export async function getCharactersWithStats(accountId: number): Promise<CharacterWithStats[]> {
  const rows = await query<CharacterRow[]>(
    `SELECT
       c.id,
       c.name,
       c.slot,
       gs.name        AS server_name,
       gs.server_id   AS server_id,
       cs.total_play_seconds,
       cs.last_seen,
       c.created_at
     FROM characters c
     LEFT JOIN character_stats cs ON cs.character_id = c.id
     LEFT JOIN game_servers gs    ON gs.server_id = cs.server_id
     WHERE c.account_id = ?
     ORDER BY c.slot ASC, cs.total_play_seconds DESC`,
    [accountId],
  );

  return rows.map((r) => ({
    id: r.id,
    name: r.name,
    slot: r.slot,
    serverName: r.server_name ?? null,
    serverId: r.server_id ?? null,
    totalPlaySeconds: r.total_play_seconds ?? 0,
    lastSeen: r.last_seen ?? null,
    createdAt: r.created_at,
  }));
}

export type DeleteResult = { ok: true } | { ok: false; message: string };

export async function deleteCharacter(
  characterId: number,
  accountId: number,
): Promise<DeleteResult> {
  const res = await query<ResultSetHeader>(
    "DELETE FROM characters WHERE id = ? AND account_id = ?",
    [characterId, accountId],
  );
  if (res.affectedRows === 0) {
    return { ok: false, message: "Personnage introuvable ou accès refusé." };
  }
  return { ok: true };
}
```

- [ ] **Étape 4 : Lancer les tests**

```bash
cd web-portal && npm run test -- --reporter=verbose 2>&1 | head -60
```

Attendu : 4 nouveaux tests passent.

- [ ] **Étape 5 : Créer app/api/player/characters/[id]/route.ts**

```typescript
// web-portal/app/api/player/characters/[id]/route.ts
import { NextResponse } from "next/server";
import { cookies } from "next/headers";
import { readSession } from "@/lib/session";
import { deleteCharacter } from "@/lib/playerCharacters";

export async function DELETE(
  _request: Request,
  { params }: { params: { id: string } },
) {
  const session = readSession(cookies());
  if (!session) return NextResponse.json({ ok: false, message: "Non authentifié." }, { status: 401 });

  const charId = parseInt(params.id, 10);
  if (!Number.isFinite(charId) || charId <= 0) {
    return NextResponse.json({ ok: false, message: "Identifiant invalide." }, { status: 400 });
  }

  const result = await deleteCharacter(charId, session.accountId);
  return NextResponse.json(result, { status: result.ok ? 200 : 404 });
}
```

- [ ] **Étape 6 : Commit**

```bash
git add web-portal/lib/playerCharacters.ts web-portal/lib/playerCharacters.test.ts \
        web-portal/app/api/player/characters/
git commit -m "feat(player): playerCharacters — getCharactersWithStats + deleteCharacter + API DELETE"
```

---

## Tâche 8 — Page /player/servers + CharacterCard

**Fichiers :**
- Créer : `web-portal/app/player/servers/page.tsx`
- Créer : `web-portal/components/player/CharacterCard.tsx`

- [ ] **Étape 1 : Créer components/player/CharacterCard.tsx**

```typescript
// web-portal/components/player/CharacterCard.tsx
"use client";

import { useState } from "react";
import type { CharacterWithStats } from "@/lib/playerCharacters";

function formatPlayTime(seconds: number): string {
  if (seconds === 0) return "Aucune session enregistrée";
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  if (h === 0) return `${m}m`;
  return `${h}h ${m}m`;
}

type Props = {
  character: CharacterWithStats;
  onDeleted: (id: number) => void;
};

export function CharacterCard({ character, onDeleted }: Props) {
  const [step, setStep] = useState<"idle" | "confirm1" | "confirm2" | "deleting">("idle");
  const [error, setError] = useState("");

  async function handleDelete() {
    setStep("deleting");
    setError("");
    const res = await fetch(`/api/player/characters/${character.id}`, { method: "DELETE" });
    const data = (await res.json()) as { ok: boolean; message?: string };
    if (data.ok) {
      onDeleted(character.id);
    } else {
      setError(data.message ?? "Erreur lors de la suppression.");
      setStep("idle");
    }
  }

  return (
    <div className="wp-card" style={{ marginBottom: "1rem" }}>
      <div style={{ display: "flex", alignItems: "flex-start", justifyContent: "space-between", gap: "1rem" }}>
        <div>
          <div style={{ fontFamily: "var(--font-display)", fontSize: 16, color: "var(--ln-accent)" }}>
            {character.name}
          </div>
          <div style={{ fontSize: 12, color: "var(--ln-muted)", marginTop: 4 }}>
            Slot {character.slot + 1}
            {character.serverName && ` · ${character.serverName}`}
          </div>
          <div style={{ fontSize: 13, color: "var(--ln-text)", marginTop: 6 }}>
            Temps joué : <strong>{formatPlayTime(character.totalPlaySeconds)}</strong>
          </div>
          {character.lastSeen && (
            <div style={{ fontSize: 11, color: "var(--ln-muted)", marginTop: 2 }}>
              Dernière connexion : {new Date(character.lastSeen).toLocaleDateString("fr-FR")}
            </div>
          )}
        </div>

        <div>
          {step === "idle" && (
            <button
              className="btn btn-ghost"
              style={{ color: "var(--ln-danger)", fontSize: 12 }}
              onClick={() => setStep("confirm1")}
            >
              Supprimer
            </button>
          )}
          {step === "confirm1" && (
            <div style={{ textAlign: "right" }}>
              <p style={{ fontSize: 12, color: "var(--ln-warning)", marginBottom: 6 }}>
                Supprimer <strong>{character.name}</strong> ?
              </p>
              <div style={{ display: "flex", gap: "0.5rem", justifyContent: "flex-end" }}>
                <button className="btn btn-ghost" onClick={() => setStep("idle")}>Annuler</button>
                <button
                  className="btn btn-secondary"
                  style={{ background: "rgba(200,50,50,0.15)", color: "var(--ln-danger)" }}
                  onClick={() => setStep("confirm2")}
                >
                  Continuer
                </button>
              </div>
            </div>
          )}
          {step === "confirm2" && (
            <div style={{ textAlign: "right" }}>
              <p style={{ fontSize: 12, color: "var(--ln-danger)", marginBottom: 6 }}>
                Action irréversible. Confirmez la suppression définitive ?
              </p>
              <div style={{ display: "flex", gap: "0.5rem", justifyContent: "flex-end" }}>
                <button className="btn btn-ghost" onClick={() => setStep("idle")}>Annuler</button>
                <button
                  className="btn btn-secondary"
                  style={{ background: "rgba(200,50,50,0.25)", color: "var(--ln-danger)" }}
                  onClick={handleDelete}
                >
                  Supprimer définitivement
                </button>
              </div>
            </div>
          )}
          {step === "deleting" && (
            <span style={{ fontSize: 12, color: "var(--ln-muted)" }}>Suppression…</span>
          )}
        </div>
      </div>
      {error && <p style={{ fontSize: 12, color: "var(--ln-danger)", marginTop: 8 }}>{error}</p>}
    </div>
  );
}
```

- [ ] **Étape 2 : Créer app/player/servers/page.tsx**

```typescript
// web-portal/app/player/servers/page.tsx
import { cookies } from "next/headers";
import { redirect } from "next/navigation";
import Link from "next/link";
import { readSession } from "@/lib/session";
import { getCharactersWithStats } from "@/lib/playerCharacters";
import { CharactersSection } from "@/components/player/CharactersSection";

export const dynamic = "force-dynamic";

export default async function PlayerServersPage() {
  const session = readSession(cookies());
  if (!session) redirect("/login?next=/player/servers");

  const characters = await getCharactersWithStats(session.accountId);

  return (
    <div className="wp-main">
      <div className="wp-page-header">
        <h1>Mes aventures</h1>
        <p>Vos personnages et leur temps de jeu par serveur.</p>
      </div>

      {characters.length === 0 ? (
        <div className="wp-card" style={{ textAlign: "center", padding: "2rem" }}>
          <p style={{ color: "var(--ln-muted)", fontStyle: "italic" }}>
            Aucun personnage créé dans le jeu pour le moment.
          </p>
        </div>
      ) : (
        <CharactersSection initialCharacters={characters} />
      )}

      <div style={{ marginTop: "1.5rem" }}>
        <Link href="/player" className="btn btn-ghost">&larr; Espace joueur</Link>
      </div>
    </div>
  );
}
```

- [ ] **Étape 3 : Créer components/player/CharactersSection.tsx**

Ce composant Client gère l'état local de la liste de personnages après suppression.

```typescript
// web-portal/components/player/CharactersSection.tsx
"use client";

import { useState } from "react";
import type { CharacterWithStats } from "@/lib/playerCharacters";
import { CharacterCard } from "@/components/player/CharacterCard";

type Props = { initialCharacters: CharacterWithStats[] };

export function CharactersSection({ initialCharacters }: Props) {
  const [characters, setCharacters] = useState(initialCharacters);

  function handleDeleted(id: number) {
    setCharacters((prev) => prev.filter((c) => c.id !== id));
  }

  if (characters.length === 0) {
    return (
      <div className="wp-card" style={{ textAlign: "center", padding: "2rem" }}>
        <p style={{ color: "var(--ln-muted)", fontStyle: "italic" }}>Tous vos personnages ont été supprimés.</p>
      </div>
    );
  }

  return (
    <div>
      {characters.map((char) => (
        <CharacterCard key={char.id} character={char} onDeleted={handleDeleted} />
      ))}
    </div>
  );
}
```

- [ ] **Étape 4 : Commit**

```bash
git add web-portal/app/player/servers/ \
        web-portal/components/player/CharacterCard.tsx \
        web-portal/components/player/CharactersSection.tsx
git commit -m "feat(player): page /player/servers — personnages + temps joué + suppression en 2 étapes"
```

---

## Tâche 9 — lib/playerCgu.ts + page /player/cgu avec vraies données

**Fichiers :**
- Créer : `web-portal/lib/playerCgu.ts`
- Modifier : `web-portal/app/player/cgu/page.tsx`

- [ ] **Étape 1 : Créer lib/playerCgu.ts**

```typescript
// web-portal/lib/playerCgu.ts
import type { RowDataPacket } from "mysql2/promise";
import { query } from "@/lib/db";

type CguRow = RowDataPacket & {
  edition_id: number;
  version_label: string;
  published_at: string;
  status: "draft" | "published" | "retired";
  title: string | null;
  accepted_at: string | null;
};

export type CguAcceptance = {
  editionId: number;
  versionLabel: string;
  publishedAt: string;
  status: "draft" | "published" | "retired";
  title: string;
  acceptedAt: string | null;
  accepted: boolean;
};

export async function getCguAcceptances(accountId: number): Promise<CguAcceptance[]> {
  const rows = await query<CguRow[]>(
    `SELECT
       te.id            AS edition_id,
       te.version_label,
       te.published_at,
       te.status,
       COALESCE(tl.title, te.version_label) AS title,
       ata.accepted_at
     FROM terms_editions te
     LEFT JOIN terms_localizations tl
       ON tl.edition_id = te.id AND tl.locale = 'fr'
     LEFT JOIN account_terms_acceptances ata
       ON ata.edition_id = te.id AND ata.account_id = ?
     WHERE te.status IN ('published', 'retired')
     ORDER BY te.published_at DESC`,
    [accountId],
  );

  return rows.map((r) => ({
    editionId: r.edition_id,
    versionLabel: r.version_label,
    publishedAt: r.published_at,
    status: r.status,
    title: r.title ?? r.version_label,
    acceptedAt: r.accepted_at ?? null,
    accepted: r.accepted_at !== null,
  }));
}
```

- [ ] **Étape 2 : Remplacer app/player/cgu/page.tsx**

```typescript
// web-portal/app/player/cgu/page.tsx
import { cookies } from "next/headers";
import { redirect } from "next/navigation";
import Link from "next/link";
import { readSession } from "@/lib/session";
import { getCguAcceptances } from "@/lib/playerCgu";

export const dynamic = "force-dynamic";

function formatDate(dateStr: string | null): string {
  if (!dateStr) return "—";
  return new Date(dateStr).toLocaleDateString("fr-FR", {
    day: "2-digit",
    month: "long",
    year: "numeric",
  });
}

export default async function PlayerCguPage() {
  const session = readSession(cookies());
  if (!session) redirect("/login?next=/player/cgu");

  let acceptances = await getCguAcceptances(session.accountId).catch(() => null);

  return (
    <div className="wp-main narrow">
      <div className="wp-page-header">
        <h1>Mes conditions générales</h1>
        <p>Historique des versions acceptées et refusées.</p>
      </div>

      {acceptances === null ? (
        <div className="wp-card" style={{ textAlign: "center", color: "var(--ln-muted)" }}>
          Données temporairement indisponibles.
        </div>
      ) : acceptances.length === 0 ? (
        <div className="wp-card" style={{ textAlign: "center", color: "var(--ln-muted)" }}>
          <p style={{ fontStyle: "italic" }}>Aucune CGU publiée pour le moment.</p>
        </div>
      ) : (
        <>
          {acceptances.some((a) => a.status === "published" && !a.accepted) && (
            <div className="wp-card" style={{ marginBottom: 16, borderColor: "rgba(220,80,80,.4)", background: "rgba(220,80,80,.05)" }}>
              <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
                <span style={{ fontSize: "1.1rem" }}>⚠</span>
                <div>
                  <div style={{ fontFamily: "var(--font-display)", fontSize: 11, letterSpacing: ".14em", textTransform: "uppercase", color: "var(--ln-danger)", marginBottom: 4 }}>
                    CGU non acceptée
                  </div>
                  <p style={{ margin: 0, fontSize: 13, color: "var(--ln-muted)" }}>
                    Une ou plusieurs versions en vigueur n&apos;ont pas encore été acceptées.
                  </p>
                </div>
              </div>
            </div>
          )}

          <div className="wp-table-wrap">
            <table className="wp-table">
              <thead>
                <tr>
                  <th>Version</th>
                  <th>Publiée le</th>
                  <th>Date d&apos;acceptation</th>
                  <th>Statut</th>
                </tr>
              </thead>
              <tbody>
                {acceptances.map((a) => (
                  <tr key={a.editionId}>
                    <td style={{ fontFamily: "var(--font-display)" }}>{a.title}</td>
                    <td>{formatDate(a.publishedAt)}</td>
                    <td>{formatDate(a.acceptedAt)}</td>
                    <td>
                      {a.accepted ? (
                        <span className="wp-badge active">Acceptée</span>
                      ) : a.status === "published" ? (
                        <span className="wp-badge" style={{ borderColor: "var(--ln-danger)", color: "var(--ln-danger)" }}>Non acceptée</span>
                      ) : (
                        <span className="wp-badge planned">Retirée</span>
                      )}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </>
      )}

      <div style={{ marginTop: 24 }}>
        <Link href="/player" className="btn btn-ghost">&larr; Retour à l&apos;espace joueur</Link>
      </div>
    </div>
  );
}
```

- [ ] **Étape 3 : Commit**

```bash
git add web-portal/lib/playerCgu.ts web-portal/app/player/cgu/page.tsx
git commit -m "feat(player): page /player/cgu avec vraies données CGU"
```

---

## Tâche 10 — lib/playerSecurity.ts + API + page /player/security + PasswordChangeForm

**Fichiers :**
- Créer : `web-portal/lib/playerSecurity.ts`
- Créer : `web-portal/lib/playerSecurity.test.ts`
- Créer : `web-portal/app/api/player/security/password/route.ts`
- Créer : `web-portal/app/player/security/page.tsx`
- Créer : `web-portal/components/player/PasswordChangeForm.tsx`

- [ ] **Étape 1 : Écrire les tests**

```typescript
// web-portal/lib/playerSecurity.test.ts
import { describe, it, expect, vi, beforeEach } from "vitest";

vi.mock("@/lib/db", () => ({ query: vi.fn() }));
vi.mock("@/lib/portalLogin", () => ({ verifyPortalCredentials: vi.fn() }));
vi.mock("@/lib/gamePasswordHash", () => ({ hashPasswordForGameMaster: vi.fn() }));

import { query } from "@/lib/db";
import { verifyPortalCredentials } from "@/lib/portalLogin";
import { hashPasswordForGameMaster } from "@/lib/gamePasswordHash";
import { changePassword } from "./playerSecurity";

const mockQuery = vi.mocked(query);
const mockVerify = vi.mocked(verifyPortalCredentials);
const mockHash = vi.mocked(hashPasswordForGameMaster);

beforeEach(() => vi.clearAllMocks());

describe("changePassword", () => {
  it("retourne une erreur si le mot de passe actuel est incorrect", async () => {
    mockQuery.mockResolvedValueOnce([{ login: "joueur1" }]);
    mockVerify.mockResolvedValueOnce({ ok: false, code: "invalid" });
    const result = await changePassword(1, "mauvais", "Nouveau1!");
    expect(result.ok).toBe(false);
    expect((result as { ok: false; message: string }).message).toContain("actuel");
  });

  it("retourne une erreur si le nouveau MDP est trop court", async () => {
    mockQuery.mockResolvedValueOnce([{ login: "joueur1", email: "j@t.com" }]);
    mockVerify.mockResolvedValueOnce({ ok: true, accountId: 1, login: "joueur1", tagId: "T", role: "player" });
    const result = await changePassword(1, "correct", "abc");
    expect(result.ok).toBe(false);
    expect((result as { ok: false; message: string }).message).toContain("8");
  });

  it("met à jour le hash si tout est valide", async () => {
    mockQuery.mockResolvedValueOnce([{ login: "joueur1", email: "j@t.com" }]);
    mockVerify.mockResolvedValueOnce({ ok: true, accountId: 1, login: "joueur1", tagId: "T", role: "player" });
    mockHash.mockResolvedValueOnce("$argon2id$...");
    mockQuery.mockResolvedValueOnce({ affectedRows: 1 });
    const result = await changePassword(1, "correct", "NouveauMdp1!");
    expect(result.ok).toBe(true);
    expect(mockHash).toHaveBeenCalledWith("joueur1", "NouveauMdp1!");
  });
});
```

- [ ] **Étape 2 : Lancer le test pour vérifier qu'il échoue**

```bash
cd web-portal && npm run test -- --reporter=verbose 2>&1 | head -40
```

Attendu : FAIL — `Cannot find module './playerSecurity'`

- [ ] **Étape 3 : Implémenter lib/playerSecurity.ts**

```typescript
// web-portal/lib/playerSecurity.ts
import type { RowDataPacket, ResultSetHeader } from "mysql2/promise";
import { query } from "@/lib/db";
import { verifyPortalCredentials } from "@/lib/portalLogin";
import { hashPasswordForGameMaster } from "@/lib/gamePasswordHash";

type PasswordResult = { ok: true } | { ok: false; message: string };

function validateNewPassword(password: string): string | null {
  if (password.length < 8) return "Le mot de passe doit contenir au moins 8 caractères.";
  if (password.length > 256) return "Le mot de passe est trop long.";
  if (!/[A-Za-z]/.test(password) || !/\d/.test(password)) {
    return "Le mot de passe doit contenir au moins une lettre et un chiffre.";
  }
  return null;
}

export async function changePassword(
  accountId: number,
  currentPassword: string,
  newPassword: string,
): Promise<PasswordResult> {
  const rows = await query<Array<RowDataPacket & { login: string; email: string }>>(
    "SELECT login, email FROM accounts WHERE id = ? LIMIT 1",
    [accountId],
  );
  const account = rows[0];
  if (!account) return { ok: false, message: "Compte introuvable." };

  const verif = await verifyPortalCredentials(account.login, currentPassword);
  if (!verif.ok) return { ok: false, message: "Mot de passe actuel incorrect." };

  const passwordError = validateNewPassword(newPassword);
  if (passwordError) return { ok: false, message: passwordError };

  const newHash = await hashPasswordForGameMaster(account.login, newPassword);
  await query<ResultSetHeader>("UPDATE accounts SET password_hash = ? WHERE id = ?", [newHash, accountId]);

  return { ok: true };
}
```

- [ ] **Étape 4 : Lancer les tests**

```bash
cd web-portal && npm run test -- --reporter=verbose 2>&1 | head -60
```

Attendu : 3 nouveaux tests passent.

- [ ] **Étape 5 : Créer l'API route**

```typescript
// web-portal/app/api/player/security/password/route.ts
import { NextResponse } from "next/server";
import { cookies } from "next/headers";
import { readSession } from "@/lib/session";
import { changePassword } from "@/lib/playerSecurity";

export async function POST(request: Request) {
  const session = readSession(cookies());
  if (!session) return NextResponse.json({ ok: false, message: "Non authentifié." }, { status: 401 });

  let body: { currentPassword?: string; newPassword?: string };
  try {
    body = await request.json();
  } catch {
    return NextResponse.json({ ok: false, message: "Requête invalide." }, { status: 400 });
  }

  const result = await changePassword(
    session.accountId,
    typeof body.currentPassword === "string" ? body.currentPassword : "",
    typeof body.newPassword === "string" ? body.newPassword : "",
  );
  return NextResponse.json(result, { status: result.ok ? 200 : 400 });
}
```

- [ ] **Étape 6 : Créer components/player/PasswordChangeForm.tsx**

```typescript
// web-portal/components/player/PasswordChangeForm.tsx
"use client";

import { useState } from "react";

export function PasswordChangeForm() {
  const [current, setCurrent] = useState("");
  const [newPass, setNewPass] = useState("");
  const [confirm, setConfirm] = useState("");
  const [status, setStatus] = useState<"idle" | "loading" | "ok" | "error">("idle");
  const [message, setMessage] = useState("");

  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault();
    if (newPass !== confirm) {
      setStatus("error");
      setMessage("Les mots de passe ne correspondent pas.");
      return;
    }
    setStatus("loading");
    setMessage("");
    const res = await fetch("/api/player/security/password", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ currentPassword: current, newPassword: newPass }),
    });
    const data = (await res.json()) as { ok: boolean; message?: string };
    if (data.ok) {
      setStatus("ok");
      setMessage("Mot de passe modifié avec succès.");
      setCurrent(""); setNewPass(""); setConfirm("");
    } else {
      setStatus("error");
      setMessage(data.message ?? "Erreur.");
    }
  }

  return (
    <form onSubmit={handleSubmit} style={{ display: "grid", gap: "1rem" }}>
      <div>
        <label className="wp-label">Mot de passe actuel</label>
        <input
          className="wp-input"
          type="password"
          value={current}
          onChange={(e) => setCurrent(e.target.value)}
          required
          autoComplete="current-password"
        />
      </div>
      <div>
        <label className="wp-label">Nouveau mot de passe</label>
        <input
          className="wp-input"
          type="password"
          value={newPass}
          onChange={(e) => setNewPass(e.target.value)}
          required
          minLength={8}
          autoComplete="new-password"
        />
      </div>
      <div>
        <label className="wp-label">Confirmer le nouveau mot de passe</label>
        <input
          className="wp-input"
          type="password"
          value={confirm}
          onChange={(e) => setConfirm(e.target.value)}
          required
          autoComplete="new-password"
        />
      </div>
      {message && (
        <p style={{ fontSize: 13, color: status === "ok" ? "var(--ln-success)" : "var(--ln-danger)" }}>
          {message}
        </p>
      )}
      <button type="submit" className="btn btn-primary" disabled={status === "loading"}>
        {status === "loading" ? "Modification…" : "Changer le mot de passe"}
      </button>
    </form>
  );
}
```

- [ ] **Étape 7 : Créer app/player/security/page.tsx**

```typescript
// web-portal/app/player/security/page.tsx
import { cookies } from "next/headers";
import { redirect } from "next/navigation";
import Link from "next/link";
import { readSession } from "@/lib/session";
import { PasswordChangeForm } from "@/components/player/PasswordChangeForm";

export const dynamic = "force-dynamic";

export default function PlayerSecurityPage() {
  const session = readSession(cookies());
  if (!session) redirect("/login?next=/player/security");

  return (
    <div className="wp-main narrow">
      <div className="wp-page-header">
        <h1>Sécurité du compte</h1>
        <p>Modifiez votre mot de passe et configurez les protections supplémentaires.</p>
      </div>

      <div className="wp-card" style={{ marginBottom: "1.5rem" }}>
        <div className="wp-section-title" style={{ marginBottom: "1rem" }}>Changer le mot de passe</div>
        <PasswordChangeForm />
      </div>

      <div className="wp-card">
        <div className="wp-section-title" style={{ marginBottom: "0.5rem" }}>
          Double authentification (MFA)
          <span className="wp-badge planned" style={{ marginLeft: 8 }}>Bientôt</span>
        </div>
        <p style={{ fontSize: 13, color: "var(--ln-muted)", margin: 0 }}>
          La double authentification (TOTP / application d&apos;authentification) sera disponible dans
          une prochaine mise à jour du portail.
        </p>
      </div>

      <div style={{ marginTop: "1.5rem" }}>
        <Link href="/player" className="btn btn-ghost">&larr; Espace joueur</Link>
      </div>
    </div>
  );
}
```

- [ ] **Étape 8 : Commit**

```bash
git add web-portal/lib/playerSecurity.ts web-portal/lib/playerSecurity.test.ts \
        web-portal/app/api/player/security/ \
        web-portal/app/player/security/ \
        web-portal/components/player/PasswordChangeForm.tsx
git commit -m "feat(player): sécurité — changement MDP + MFA placeholder"
```

---

## Tâche 11 — lib/playerPrivacy.ts + API + page /player/privacy + PrivacyForm

**Fichiers :**
- Créer : `web-portal/lib/playerPrivacy.ts`
- Créer : `web-portal/app/api/player/privacy/route.ts`
- Créer : `web-portal/app/player/privacy/page.tsx`
- Créer : `web-portal/components/player/PrivacyForm.tsx`

- [ ] **Étape 1 : Créer lib/playerPrivacy.ts**

```typescript
// web-portal/lib/playerPrivacy.ts
import type { RowDataPacket, ResultSetHeader } from "mysql2/promise";
import { query } from "@/lib/db";

export type ProfileVisibility = "public" | "friends" | "private";

export type PrivacySettings = {
  profileVisibility: ProfileVisibility;
};

const VALID_VISIBILITY = new Set<string>(["public", "friends", "private"]);

export async function getPrivacySettings(accountId: number): Promise<PrivacySettings | null> {
  const rows = await query<Array<RowDataPacket & { profile_visibility: string }>>(
    "SELECT profile_visibility FROM accounts WHERE id = ? LIMIT 1",
    [accountId],
  );
  const row = rows[0];
  if (!row) return null;

  const visibility = VALID_VISIBILITY.has(row.profile_visibility)
    ? (row.profile_visibility as ProfileVisibility)
    : "public";

  return { profileVisibility: visibility };
}

export async function updatePrivacySettings(
  accountId: number,
  settings: Partial<PrivacySettings>,
): Promise<{ ok: true } | { ok: false; message: string }> {
  if (settings.profileVisibility !== undefined && !VALID_VISIBILITY.has(settings.profileVisibility)) {
    return { ok: false, message: "Valeur de visibilité invalide." };
  }

  if (settings.profileVisibility !== undefined) {
    await query<ResultSetHeader>(
      "UPDATE accounts SET profile_visibility = ? WHERE id = ?",
      [settings.profileVisibility, accountId],
    );
  }

  return { ok: true };
}
```

- [ ] **Étape 2 : Créer app/api/player/privacy/route.ts**

```typescript
// web-portal/app/api/player/privacy/route.ts
import { NextResponse } from "next/server";
import { cookies } from "next/headers";
import { readSession } from "@/lib/session";
import { updatePrivacySettings } from "@/lib/playerPrivacy";

export async function PATCH(request: Request) {
  const session = readSession(cookies());
  if (!session) return NextResponse.json({ ok: false, message: "Non authentifié." }, { status: 401 });

  let body: { profileVisibility?: string };
  try {
    body = await request.json();
  } catch {
    return NextResponse.json({ ok: false, message: "Requête invalide." }, { status: 400 });
  }

  const result = await updatePrivacySettings(session.accountId, {
    profileVisibility: body.profileVisibility as "public" | "friends" | "private" | undefined,
  });
  return NextResponse.json(result, { status: result.ok ? 200 : 400 });
}
```

- [ ] **Étape 3 : Créer components/player/PrivacyForm.tsx**

```typescript
// web-portal/components/player/PrivacyForm.tsx
"use client";

import { useState } from "react";
import type { ProfileVisibility } from "@/lib/playerPrivacy";

const LABELS: Record<ProfileVisibility, string> = {
  public: "Public — tout le monde peut voir mon profil",
  friends: "Amis uniquement",
  private: "Privé — personne ne peut voir mon profil",
};

type Props = { initial: ProfileVisibility };

export function PrivacyForm({ initial }: Props) {
  const [visibility, setVisibility] = useState<ProfileVisibility>(initial);
  const [status, setStatus] = useState<"idle" | "saving" | "ok" | "error">("idle");
  const [message, setMessage] = useState("");

  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault();
    setStatus("saving");
    const res = await fetch("/api/player/privacy", {
      method: "PATCH",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ profileVisibility: visibility }),
    });
    const data = (await res.json()) as { ok: boolean; message?: string };
    if (data.ok) {
      setStatus("ok");
      setMessage("Paramètres mis à jour.");
    } else {
      setStatus("error");
      setMessage(data.message ?? "Erreur.");
    }
  }

  return (
    <form onSubmit={handleSubmit} style={{ display: "grid", gap: "1rem" }}>
      <div>
        <label className="wp-label">Visibilité du profil</label>
        {(["public", "friends", "private"] as ProfileVisibility[]).map((v) => (
          <label
            key={v}
            style={{
              display: "flex",
              alignItems: "center",
              gap: 10,
              padding: "0.6rem 0",
              borderBottom: "1px solid var(--ln-border)",
              cursor: "pointer",
            }}
          >
            <input
              type="radio"
              name="visibility"
              value={v}
              checked={visibility === v}
              onChange={() => setVisibility(v)}
            />
            <span style={{ fontSize: 14 }}>{LABELS[v]}</span>
          </label>
        ))}
      </div>
      {message && (
        <p style={{ fontSize: 13, color: status === "ok" ? "var(--ln-success)" : "var(--ln-danger)" }}>
          {message}
        </p>
      )}
      <button type="submit" className="btn btn-primary" disabled={status === "saving"}>
        {status === "saving" ? "Enregistrement…" : "Enregistrer"}
      </button>
    </form>
  );
}
```

- [ ] **Étape 4 : Créer app/player/privacy/page.tsx**

```typescript
// web-portal/app/player/privacy/page.tsx
import { cookies } from "next/headers";
import { redirect } from "next/navigation";
import Link from "next/link";
import { readSession } from "@/lib/session";
import { getPrivacySettings } from "@/lib/playerPrivacy";
import { getCguAcceptances } from "@/lib/playerCgu";
import { PrivacyForm } from "@/components/player/PrivacyForm";

export const dynamic = "force-dynamic";

export default async function PlayerPrivacyPage() {
  const session = readSession(cookies());
  if (!session) redirect("/login?next=/player/privacy");

  const [privacy, acceptances] = await Promise.all([
    getPrivacySettings(session.accountId),
    getCguAcceptances(session.accountId).catch(() => []),
  ]);

  if (!privacy) redirect("/player");

  return (
    <div className="wp-main narrow">
      <div className="wp-page-header">
        <h1>Vie privée</h1>
        <p>Gérez la visibilité de votre profil et consultez vos CGU.</p>
      </div>

      <div className="wp-card" style={{ marginBottom: "1.5rem" }}>
        <div className="wp-section-title" style={{ marginBottom: "1rem" }}>Visibilité du profil</div>
        <PrivacyForm initial={privacy.profileVisibility} />
      </div>

      <div className="wp-card">
        <div className="wp-section-title" style={{ marginBottom: "1rem" }}>Conditions Générales d&apos;Utilisation</div>
        {acceptances.length === 0 ? (
          <p style={{ fontSize: 13, color: "var(--ln-muted)", fontStyle: "italic" }}>
            Aucune CGU publiée pour le moment.
          </p>
        ) : (
          <div className="wp-table-wrap">
            <table className="wp-table">
              <thead>
                <tr>
                  <th>Version</th>
                  <th>Publiée le</th>
                  <th>Acceptée le</th>
                </tr>
              </thead>
              <tbody>
                {acceptances.map((a) => (
                  <tr key={a.editionId}>
                    <td>{a.title}</td>
                    <td>{new Date(a.publishedAt).toLocaleDateString("fr-FR")}</td>
                    <td>
                      {a.acceptedAt
                        ? new Date(a.acceptedAt).toLocaleDateString("fr-FR")
                        : <span style={{ color: "var(--ln-muted)" }}>—</span>}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>

      <div style={{ marginTop: "1.5rem" }}>
        <Link href="/player" className="btn btn-ghost">&larr; Espace joueur</Link>
      </div>
    </div>
  );
}
```

- [ ] **Étape 5 : Commit**

```bash
git add web-portal/lib/playerPrivacy.ts \
        web-portal/app/api/player/privacy/ \
        web-portal/app/player/privacy/ \
        web-portal/components/player/PrivacyForm.tsx
git commit -m "feat(player): vie privée — visibilité profil + récap CGU"
```

---

## Tâche 12 — Page /player/parental : Contrôle parental

**Fichiers :**
- Créer : `web-portal/app/player/parental/page.tsx`

La détection de la minorité se base sur `birth_date` stocké dans `account_recovery_profiles`. Si la date de naissance est absente ou si le joueur est majeur, un message d'information est affiché. Si le joueur est mineur, un formulaire de saisie de l'email parental est proposé.

- [ ] **Étape 1 : Créer app/player/parental/page.tsx**

```typescript
// web-portal/app/player/parental/page.tsx
import { cookies } from "next/headers";
import { redirect } from "next/navigation";
import Link from "next/link";
import { readSession } from "@/lib/session";
import { getAccountProfile } from "@/lib/playerProfile";

export const dynamic = "force-dynamic";

function computeAge(birthDate: string): number | null {
  const date = new Date(`${birthDate}T00:00:00Z`);
  if (Number.isNaN(date.getTime())) return null;
  const now = new Date();
  let age = now.getUTCFullYear() - date.getUTCFullYear();
  const monthDelta = now.getUTCMonth() - date.getUTCMonth();
  if (monthDelta < 0 || (monthDelta === 0 && now.getUTCDate() < date.getUTCDate())) age -= 1;
  return age >= 0 ? age : null;
}

export default async function PlayerParentalPage() {
  const session = readSession(cookies());
  if (!session) redirect("/login?next=/player/parental");

  const profile = await getAccountProfile(session.accountId);
  if (!profile) redirect("/player");

  const age = profile.birthDate ? computeAge(profile.birthDate) : null;
  const isMinor = age !== null && age < 18;

  return (
    <div className="wp-main narrow">
      <div className="wp-page-header">
        <h1>Contrôle parental</h1>
        <p>Supervision du compte par un parent ou tuteur légal pour les joueurs mineurs.</p>
      </div>

      {!profile.birthDate ? (
        <div className="wp-card">
          <div className="wp-section-title" style={{ marginBottom: "0.5rem" }}>Date de naissance non renseignée</div>
          <p style={{ fontSize: 13, color: "var(--ln-muted)" }}>
            Renseignez votre date de naissance dans{" "}
            <Link href="/player/account" style={{ color: "var(--ln-accent)" }}>
              les détails de votre compte
            </Link>{" "}
            pour accéder aux options de contrôle parental.
          </p>
        </div>
      ) : !isMinor ? (
        <div className="wp-card">
          <div className="wp-section-title" style={{ marginBottom: "0.5rem" }}>Compte majeur</div>
          <p style={{ fontSize: 13, color: "var(--ln-muted)" }}>
            Le contrôle parental n&apos;est disponible que pour les joueurs mineurs (moins de 18 ans).
            Votre compte est enregistré avec un âge de <strong>{age} ans</strong>.
          </p>
        </div>
      ) : (
        <>
          <div className="wp-card" style={{ marginBottom: "1.5rem", borderColor: "rgba(95,184,110,.3)" }}>
            <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
              <span style={{ fontSize: "1.25rem" }}>👶</span>
              <div>
                <div style={{ fontFamily: "var(--font-display)", fontSize: 12, letterSpacing: ".14em", textTransform: "uppercase", color: "var(--ln-success)", marginBottom: 4 }}>
                  Compte mineur
                </div>
                <p style={{ margin: 0, fontSize: 13, color: "var(--ln-muted)" }}>
                  Votre compte est enregistré pour un joueur de <strong>{age} ans</strong>.
                </p>
              </div>
            </div>
          </div>

          <div className="wp-card">
            <div className="wp-section-title" style={{ marginBottom: "1rem" }}>
              Consentement parental
              <span className="wp-badge planned" style={{ marginLeft: 8 }}>Bientôt</span>
            </div>
            <p style={{ fontSize: 13, color: "var(--ln-muted)" }}>
              {profile.parentalEmail ? (
                <>
                  Email parental enregistré : <strong>{profile.parentalEmail}</strong>
                  {profile.parentalConsentAt
                    ? ` — Consentement accordé le ${new Date(profile.parentalConsentAt).toLocaleDateString("fr-FR")}.`
                    : " — En attente de consentement."}
                </>
              ) : (
                "Le système de validation parentale sera disponible dans une prochaine mise à jour. " +
                "Un email sera envoyé au parent ou tuteur légal pour confirmer l'autorisation de jeu."
              )}
            </p>
          </div>
        </>
      )}

      <div style={{ marginTop: "1.5rem" }}>
        <Link href="/player" className="btn btn-ghost">&larr; Espace joueur</Link>
      </div>
    </div>
  );
}
```

- [ ] **Étape 2 : Commit**

```bash
git add web-portal/app/player/parental/
git commit -m "feat(player): page /player/parental — contrôle parental (mineur/majeur)"
```

---

## Tâche 13 — Refactoring dashboard /player avec vraies données

**Fichiers :**
- Modifier : `web-portal/app/player/page.tsx`

Remplacer les statistiques fictives par de vraies données depuis la session et la DB. Ajouter les liens vers toutes les nouvelles sections.

- [ ] **Étape 1 : Remplacer app/player/page.tsx**

```typescript
// web-portal/app/player/page.tsx
import { cookies } from "next/headers";
import { redirect } from "next/navigation";
import Link from "next/link";
import { readSession } from "@/lib/session";
import { getAccountProfile } from "@/lib/playerProfile";
import { getCharactersWithStats } from "@/lib/playerCharacters";
import { getPlayerExploitsData } from "@/lib/exploitsData";

export const dynamic = "force-dynamic";

export default async function PlayerHomePage() {
  const session = readSession(cookies());
  if (!session) redirect("/login?next=/player");

  const [profile, characters, exploits] = await Promise.all([
    getAccountProfile(session.accountId),
    getCharactersWithStats(session.accountId).catch(() => []),
    getPlayerExploitsData(session.accountId).catch(() => null),
  ]);

  if (!profile) redirect("/login");

  const completedExploits = exploits?.totals.completedByPlayer ?? 0;
  const totalExploits = exploits?.totals.totalInGame ?? 0;

  return (
    <div className="wp-main">
      <div className="wp-page-header">
        <h1>
          Bienvenue,{" "}
          <span style={{ fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>
            {profile.tagId || profile.login}
          </span>
        </h1>
        <p>Gérez votre profil, suivez vos exploits et consultez vos informations de compte.</p>
      </div>

      {profile.emailPending && !profile.emailVerified && (
        <div className="wp-card" style={{ marginBottom: "1rem", borderColor: "rgba(220,180,50,.4)", background: "rgba(220,180,50,.05)" }}>
          <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
            <span style={{ fontSize: "1.1rem" }}>⚠</span>
            <div>
              <div style={{ fontFamily: "var(--font-display)", fontSize: 11, letterSpacing: ".14em", textTransform: "uppercase", color: "var(--ln-warning)", marginBottom: 4 }}>
                Email en attente de validation
              </div>
              <p style={{ margin: 0, fontSize: 13, color: "var(--ln-muted)" }}>
                Un code de vérification a été envoyé à <strong>{profile.emailPending}</strong>.{" "}
                <Link href="/player/account" style={{ color: "var(--ln-accent)" }}>Valider</Link>
              </p>
            </div>
          </div>
        </div>
      )}

      <div className="wp-stats">
        <div className="wp-stat">
          <div className="wp-stat-value">{completedExploits}</div>
          <div className="wp-stat-label">Exploits débloqués</div>
        </div>
        <div className="wp-stat">
          <div className="wp-stat-value">{characters.length}</div>
          <div className="wp-stat-label">Personnages</div>
        </div>
        <div className="wp-stat">
          <div className="wp-stat-value">{totalExploits > 0 ? `${Math.round((completedExploits / totalExploits) * 100)}%` : "—"}</div>
          <div className="wp-stat-label">Progression</div>
        </div>
      </div>

      <div className="wp-section-title">Mon espace</div>

      <div className="wp-grid wp-grid-2">
        <Link href="/player/account" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>👤</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Détail du compte</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Nom, prénom, adresse, email.
            </p>
            <span className="wp-badge active">Gérer</span>
          </div>
        </Link>

        <Link href="/player/servers" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>⚔️</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Mes aventures</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Personnages, temps joué par serveur.
            </p>
            <span className="wp-badge active">Voir</span>
          </div>
        </Link>

        <Link href="/player/exploits" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>🏆</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Mes Exploits</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Progression, exploits secrets et taux de complétion.
            </p>
            <span className="wp-badge active">Voir mes exploits</span>
          </div>
        </Link>

        <Link href="/player/cgu" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>📜</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Mes CGU</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Historique des versions acceptées.
            </p>
            <span className="wp-badge active">Voir</span>
          </div>
        </Link>

        <Link href="/player/privacy" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>🔒</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Vie privée</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Visibilité du profil, historique CGU.
            </p>
            <span className="wp-badge active">Configurer</span>
          </div>
        </Link>

        <Link href="/player/security" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>🛡️</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Sécurité</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Mot de passe et double authentification.
            </p>
            <span className="wp-badge active">Gérer</span>
          </div>
        </Link>

        <Link href="/player/parental" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>👨‍👩‍👧</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Contrôle parental</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Validation parentale pour joueurs mineurs.
            </p>
            <span className="wp-badge active">Configurer</span>
          </div>
        </Link>

        <Link href="/bugs" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>🐛</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Signaler un bug</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Contribuez à l&apos;amélioration du jeu.
            </p>
            <span className="wp-badge active">Signaler</span>
          </div>
        </Link>
      </div>
    </div>
  );
}
```

- [ ] **Étape 2 : Lancer les tests finaux**

```bash
cd web-portal && npm run test -- --reporter=verbose 2>&1
```

Attendu : tous les tests passent (session + playerProfile + playerCharacters + playerSecurity).

- [ ] **Étape 3 : Commit**

```bash
git add web-portal/app/player/page.tsx
git commit -m "feat(player): dashboard /player avec vraies données — exploits, personnages, liens sections"
```

---

## Vérification finale

- [ ] `npm run test` dans `web-portal/` : tous les tests passent.
- [ ] `npm run build` dans `web-portal/` : build TypeScript sans erreur.
- [ ] Naviguer vers `/player` avec un compte authentifié : les vraies stats s'affichent.
- [ ] `/player/account` : modifier prénom/nom → enregistré ; changer email → code reçu (log console en dev) → confirmation → email mis à jour.
- [ ] `/player/servers` : personnages listés ; suppression demande 2 confirmations.
- [ ] `/player/cgu` : tableau réel (vide si aucune CGU publiée, pas de crash).
- [ ] `/player/security` : changement MDP avec vérification du MDP actuel.
- [ ] `/player/privacy` : sélecteur de visibilité enregistré.
- [ ] `/player/parental` : affiche "compte majeur" ou "compte mineur" selon `birth_date`.
- [ ] Accès sans session sur n'importe quelle route `/player/*` → redirect vers `/login?next=…`.
