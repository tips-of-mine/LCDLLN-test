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
