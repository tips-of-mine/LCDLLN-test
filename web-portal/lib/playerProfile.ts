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
