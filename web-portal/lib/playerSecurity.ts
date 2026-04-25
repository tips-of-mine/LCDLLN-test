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
