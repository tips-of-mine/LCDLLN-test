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
