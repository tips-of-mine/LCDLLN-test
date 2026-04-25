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
