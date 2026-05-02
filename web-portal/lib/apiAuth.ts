// Helpers d'authentification pour les route handlers API : lisent les cookies signés,
// vérifient le HMAC, retournent les valeurs de confiance.
import { cookies } from "next/headers";
import { verifyCookieValue } from "@/lib/cookieSigning";

export async function getAuthenticatedAccountId(): Promise<number | null> {
  const jar = cookies();
  const raw = await verifyCookieValue(jar.get("lcdlln_portal_account")?.value);
  if (!raw) return null;
  const accountId = parseInt(raw, 10);
  if (isNaN(accountId) || accountId <= 0) return null;
  return accountId;
}

export async function isAuthenticatedAdmin(): Promise<boolean> {
  const jar = cookies();
  const role = await verifyCookieValue(jar.get("lcdlln_portal_role")?.value);
  return role === "admin";
}
