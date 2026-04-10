import type { ResultSetHeader, RowDataPacket } from "mysql2/promise";
import { createHash, createHmac, randomBytes, timingSafeEqual } from "node:crypto";
import { query } from "@/lib/db";
import { hashPasswordForGameMaster } from "@/lib/gamePasswordHash";

type AccountRow = RowDataPacket & {
  id: number;
  login: string;
  email: string;
  password_hash: string;
};

type RecoveryProfileRow = RowDataPacket & {
  account_id: number;
  birth_date: string | null;
  street_address: string | null;
  city: string | null;
  postal_code: string | null;
};

type RecoveryQuestionRow = RowDataPacket & {
  id: number;
  question: string;
  answer_hash: string;
};

export type RecoveryProfileInput = {
  accountId: number;
  birthDate: string;
  address: string;
  city: string;
  postalCode: string;
  secretQuestions: Array<{ question: string; answer: string }>;
};

export type RecoveryProfileData = {
  accountId: number;
  birthDate: string;
  address: string;
  city: string;
  postalCode: string;
  secretQuestions: Array<{ id: number; question: string }>;
};

export type RecoveryRequestInput = {
  loginOrEmail: string;
  birthDate: string;
  age: string;
  address: string;
  city: string;
  postalCode: string;
  secretAnswers: string[];
};

type RecoveryProfileBundle = {
  profile: RecoveryProfileRow | null;
  questions: RecoveryQuestionRow[];
};

function getRecoverySecret(): string {
  return process.env.AUTH_SECRET || "lcdlln-dev-recovery-secret";
}

function normalizeTrimmed(value: string): string {
  return value.trim();
}

function normalizeLower(value: string): string {
  return value.trim().toLowerCase();
}

function normalizeBirthDate(value: string): string {
  const trimmed = value.trim();
  return /^\d{4}-\d{2}-\d{2}$/.test(trimmed) ? trimmed : "";
}

function normalizePostalCode(value: string): string {
  return value.trim().replace(/\s+/g, "").toUpperCase();
}

function normalizeAnswer(value: string): string {
  return value.trim().replace(/\s+/g, " ").toLowerCase();
}

function hashAnswer(value: string): string {
  return createHmac("sha256", getRecoverySecret()).update(normalizeAnswer(value)).digest("hex");
}

function hashToken(token: string): string {
  return createHash("sha256").update(token).digest("hex");
}

function computeAgeYears(birthDate: string): number | null {
  const date = new Date(`${birthDate}T00:00:00Z`);
  if (Number.isNaN(date.getTime())) return null;
  const now = new Date();
  let age = now.getUTCFullYear() - date.getUTCFullYear();
  const monthDelta = now.getUTCMonth() - date.getUTCMonth();
  if (monthDelta < 0 || (monthDelta === 0 && now.getUTCDate() < date.getUTCDate())) {
    age -= 1;
  }
  return age >= 0 ? age : null;
}

function verifyPasswordStrength(password: string): string | null {
  if (password.length < 8) return "Le mot de passe doit contenir au moins 8 caracteres.";
  if (password.length > 256) return "Le mot de passe depasse la longueur maximale autorisee.";
  if (!/[A-Za-z]/.test(password) || !/\d/.test(password)) {
    return "Le mot de passe doit contenir au moins une lettre et un chiffre.";
  }
  return null;
}

export async function ensurePasswordRecoveryTables(): Promise<void> {
  await query<ResultSetHeader>(`
    CREATE TABLE IF NOT EXISTS account_recovery_profiles (
      account_id BIGINT UNSIGNED NOT NULL,
      birth_date DATE NULL,
      street_address VARCHAR(255) NULL,
      city VARCHAR(128) NULL,
      postal_code VARCHAR(32) NULL,
      created_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
      updated_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
      PRIMARY KEY (account_id),
      CONSTRAINT fk_account_recovery_profiles_account
        FOREIGN KEY (account_id) REFERENCES accounts(id)
        ON DELETE CASCADE
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  `);
  await query<ResultSetHeader>(`
    CREATE TABLE IF NOT EXISTS account_recovery_secret_questions (
      id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
      account_id BIGINT UNSIGNED NOT NULL,
      question VARCHAR(255) NOT NULL,
      answer_hash CHAR(64) NOT NULL,
      created_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
      PRIMARY KEY (id),
      KEY ix_account_recovery_secret_questions_account (account_id),
      CONSTRAINT fk_account_recovery_secret_questions_account
        FOREIGN KEY (account_id) REFERENCES accounts(id)
        ON DELETE CASCADE
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  `);
  await query<ResultSetHeader>(`
    CREATE TABLE IF NOT EXISTS account_password_reset_tokens (
      token_hash CHAR(64) NOT NULL,
      account_id BIGINT UNSIGNED NOT NULL,
      expires_at TIMESTAMP NOT NULL,
      used_at TIMESTAMP NULL DEFAULT NULL,
      created_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
      PRIMARY KEY (token_hash),
      KEY ix_account_password_reset_tokens_account (account_id),
      KEY ix_account_password_reset_tokens_expires_at (expires_at),
      CONSTRAINT fk_account_password_reset_tokens_account
        FOREIGN KEY (account_id) REFERENCES accounts(id)
        ON DELETE CASCADE
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  `);
}

export async function getRecoveryProfile(accountId: number): Promise<RecoveryProfileData | null> {
  await ensurePasswordRecoveryTables();
  const accounts = await query<AccountRow[]>(
    "SELECT id, login, email, password_hash FROM accounts WHERE id = ? LIMIT 1",
    [accountId],
  );
  if (accounts.length === 0) return null;

  const bundle = await loadRecoveryProfileBundle(accountId);
  return {
    accountId,
    birthDate: bundle.profile?.birth_date ?? "",
    address: bundle.profile?.street_address ?? "",
    city: bundle.profile?.city ?? "",
    postalCode: bundle.profile?.postal_code ?? "",
    secretQuestions: bundle.questions.map((row) => ({ id: row.id, question: row.question })),
  };
}

export async function upsertRecoveryProfile(input: RecoveryProfileInput): Promise<{ ok: true }> {
  await ensurePasswordRecoveryTables();
  const questions = input.secretQuestions
    .map((item) => ({
      question: normalizeTrimmed(item.question),
      answer: normalizeTrimmed(item.answer),
    }))
    .filter((item) => item.question.length > 0 && item.answer.length > 0)
    .slice(0, 3);

  if (questions.length !== 3) {
    throw new Error("Trois questions secretes sont requises.");
  }

  await query<ResultSetHeader>(
    `
      INSERT INTO account_recovery_profiles (account_id, birth_date, street_address, city, postal_code)
      VALUES (?, ?, ?, ?, ?)
      ON DUPLICATE KEY UPDATE
        birth_date = VALUES(birth_date),
        street_address = VALUES(street_address),
        city = VALUES(city),
        postal_code = VALUES(postal_code)
    `,
    [
      input.accountId,
      normalizeBirthDate(input.birthDate) || null,
      normalizeTrimmed(input.address) || null,
      normalizeTrimmed(input.city) || null,
      normalizePostalCode(input.postalCode) || null,
    ],
  );
  await query<ResultSetHeader>("DELETE FROM account_recovery_secret_questions WHERE account_id = ?", [input.accountId]);
  for (const item of questions) {
    await query<ResultSetHeader>(
      "INSERT INTO account_recovery_secret_questions (account_id, question, answer_hash) VALUES (?, ?, ?)",
      [input.accountId, item.question, hashAnswer(item.answer)],
    );
  }

  return { ok: true };
}

export async function requestPasswordRecovery(input: RecoveryRequestInput): Promise<{ accepted: boolean; delivery: "email" | "noop"; message: string }> {
  await ensurePasswordRecoveryTables();
  const identifier = normalizeTrimmed(input.loginOrEmail);
  if (!identifier) {
    return {
      accepted: false,
      delivery: "noop",
      message: "Renseignez votre login ou votre email.",
    };
  }

  const account = await resolveAccount(identifier);
  if (!account) {
    return {
      accepted: true,
      delivery: "noop",
      message: "Si les informations correspondent, un e-mail de reinitialisation a ete envoye.",
    };
  }

  const bundle = await loadRecoveryProfileBundle(account.id);
  if (!hasRecoveryFactors(bundle)) {
    return {
      accepted: true,
      delivery: "noop",
      message: "Si les informations correspondent, un e-mail de reinitialisation a ete envoye.",
    };
  }

  if (!matchesRecoveryFactors(bundle, input)) {
    return {
      accepted: true,
      delivery: "noop",
      message: "Si les informations correspondent, un e-mail de reinitialisation a ete envoye.",
    };
  }

  const recentRows = await query<Array<RowDataPacket & { recent_count: number }>>(
    `
      SELECT COUNT(*) AS recent_count
      FROM account_password_reset_tokens
      WHERE account_id = ?
        AND created_at >= (UTC_TIMESTAMP() - INTERVAL 1 HOUR)
    `,
    [account.id],
  );
  if ((recentRows[0]?.recent_count ?? 0) >= 3) {
    return {
      accepted: true,
      delivery: "noop",
      message: "Si les informations correspondent, un e-mail de reinitialisation a ete envoye.",
    };
  }

  const rawToken = randomBytes(32).toString("hex");
  await query<ResultSetHeader>(
    `
      INSERT INTO account_password_reset_tokens (token_hash, account_id, expires_at)
      VALUES (?, ?, UTC_TIMESTAMP() + INTERVAL 10 MINUTE)
    `,
    [hashToken(rawToken), account.id],
  );

  const baseUrl = (process.env.NEXT_PUBLIC_PORTAL_URL || "http://127.0.0.1:3000").replace(/\/+$/, "");
  const resetUrl = `${baseUrl}/password-recovery/reset?token=${encodeURIComponent(rawToken)}`;
  await sendResetEmail(account.email, resetUrl);

  return {
    accepted: true,
    delivery: "email",
    message: "Si les informations correspondent, un e-mail de reinitialisation valable 10 minutes a ete envoye.",
  };
}

export async function resetPasswordWithToken(token: string, newPassword: string): Promise<{ ok: boolean; message: string }> {
  await ensurePasswordRecoveryTables();
  const cleanToken = token.trim();
  if (!/^[a-f0-9]{64}$/i.test(cleanToken)) {
    return { ok: false, message: "Le lien de reinitialisation est invalide." };
  }

  const passwordError = verifyPasswordStrength(newPassword);
  if (passwordError) {
    return { ok: false, message: passwordError };
  }

  const rows = await query<Array<RowDataPacket & { account_id: number; used_at: string | null; expired: number }>>(
    `
      SELECT account_id, used_at, CASE WHEN expires_at < UTC_TIMESTAMP() THEN 1 ELSE 0 END AS expired
      FROM account_password_reset_tokens
      WHERE token_hash = ?
      LIMIT 1
    `,
    [hashToken(cleanToken)],
  );
  const tokenRow = rows[0];
  if (!tokenRow || tokenRow.used_at || tokenRow.expired === 1) {
    return {
      ok: false,
      message: "Ce lien est invalide ou expire. Recommencez la verification sur le portail.",
    };
  }

  const accountRows = await query<Array<RowDataPacket & { login: string }>>(
    "SELECT login FROM accounts WHERE id = ? LIMIT 1",
    [tokenRow.account_id],
  );
  const accountLogin = accountRows[0]?.login?.trim();
  if (!accountLogin) {
    return { ok: false, message: "Compte introuvable." };
  }

  const gamePasswordHash = await hashPasswordForGameMaster(accountLogin, newPassword);
  await query<ResultSetHeader>("UPDATE accounts SET password_hash = ? WHERE id = ?", [
    gamePasswordHash,
    tokenRow.account_id,
  ]);
  await query<ResultSetHeader>(
    "UPDATE account_password_reset_tokens SET used_at = UTC_TIMESTAMP() WHERE token_hash = ?",
    [hashToken(cleanToken)],
  );
  await query<ResultSetHeader>(
    "UPDATE account_password_reset_tokens SET used_at = COALESCE(used_at, UTC_TIMESTAMP()) WHERE account_id = ? AND token_hash <> ? AND used_at IS NULL",
    [tokenRow.account_id, hashToken(cleanToken)],
  );

  return {
    ok: true,
    message: "Votre mot de passe a ete modifie. Vous pouvez revenir dans le jeu.",
  };
}

async function resolveAccount(identifier: string): Promise<AccountRow | null> {
  const rows = await query<AccountRow[]>(
    `
      SELECT id, login, email, password_hash
      FROM accounts
      WHERE LOWER(email) = ? OR login = ?
      LIMIT 1
    `,
    [normalizeLower(identifier), identifier],
  );
  return rows[0] ?? null;
}

async function loadRecoveryProfileBundle(accountId: number): Promise<RecoveryProfileBundle> {
  const profiles = await query<RecoveryProfileRow[]>(
    `
      SELECT account_id, birth_date, street_address, city, postal_code
      FROM account_recovery_profiles
      WHERE account_id = ?
      LIMIT 1
    `,
    [accountId],
  );
  const questions = await query<RecoveryQuestionRow[]>(
    `
      SELECT id, question, answer_hash
      FROM account_recovery_secret_questions
      WHERE account_id = ?
      ORDER BY id ASC
    `,
    [accountId],
  );
  return {
    profile: profiles[0] ?? null,
    questions,
  };
}

function hasRecoveryFactors(bundle: RecoveryProfileBundle): boolean {
  return Boolean(
    bundle.profile?.birth_date ||
      bundle.profile?.street_address ||
      bundle.profile?.city ||
      bundle.profile?.postal_code ||
      bundle.questions.length > 0,
  );
}

function matchesRecoveryFactors(bundle: RecoveryProfileBundle, input: RecoveryRequestInput): boolean {
  const profile = bundle.profile;
  if (!profile) return false;

  if (profile.birth_date) {
    if (normalizeBirthDate(input.birthDate) !== profile.birth_date) return false;
    const expectedAge = computeAgeYears(profile.birth_date);
    if (input.age.trim() && expectedAge !== null && Number.parseInt(input.age, 10) !== expectedAge) return false;
  }
  if (profile.street_address && normalizeLower(input.address) !== normalizeLower(profile.street_address)) return false;
  if (profile.city && normalizeLower(input.city) !== normalizeLower(profile.city)) return false;
  if (profile.postal_code && normalizePostalCode(input.postalCode) !== normalizePostalCode(profile.postal_code)) return false;
  if (bundle.questions.length > 0) {
    if (input.secretAnswers.length < bundle.questions.length) return false;
    for (let index = 0; index < bundle.questions.length; index += 1) {
      const answer = input.secretAnswers[index] || "";
      const provided = Buffer.from(hashAnswer(answer), "hex");
      const expected = Buffer.from(bundle.questions[index].answer_hash, "hex");
      if (provided.length !== expected.length || !timingSafeEqual(provided, expected)) return false;
    }
  }

  return true;
}

async function sendResetEmail(to: string, resetUrl: string): Promise<void> {
  const { createTransport } = await import("nodemailer");
  const host = process.env.SMTP_HOST;
  const port = Number.parseInt(process.env.SMTP_PORT || "587", 10);
  const user = process.env.SMTP_USER;
  const pass = process.env.SMTP_PASS;
  const from = process.env.SMTP_FROM;

  if (!host || !from) {
    console.warn(`[password-recovery] SMTP non configure, lien genere pour ${to}: ${resetUrl}`);
    return;
  }

  const transport = createTransport({
    host,
    port,
    secure: port === 465,
    auth: user && pass ? { user, pass } : undefined,
  });

  await transport.sendMail({
    from,
    to,
    subject: "LCDLLN - Reinitialisation de votre mot de passe",
    text: [
      "Une demande de reinitialisation de mot de passe vient d'etre validee.",
      "",
      "Utilisez ce lien dans les 10 minutes :",
      resetUrl,
      "",
      "Si le lien expire, recommencez les verifications sur le portail web.",
    ].join("\n"),
  });
}
