import { NextResponse } from "next/server";
import { cookies } from "next/headers";
import { verifyPortalCredentials } from "@/lib/portalLogin";
import { query } from "@/lib/db";
import { signCookieValue } from "@/lib/cookieSigning";
import { checkRateLimit, getClientIp, recordFailure, recordSuccess } from "@/lib/rateLimit";
import type { RowDataPacket } from "mysql2/promise";

const COOKIE_NAME = "lcdlln_portal_account";
const COOKIE_MAX_AGE_SEC = 60 * 60 * 24 * 7;

export async function POST(request: Request) {
  const ipKey = `login:${getClientIp(request)}`;
  const limit = checkRateLimit(ipKey);
  if (!limit.allowed) {
    const retryAfterSec = Math.ceil(limit.retryAfterMs / 1000);
    return NextResponse.json(
      { ok: false, message: "Trop de tentatives. Reessayez plus tard." },
      { status: 429, headers: { "Retry-After": String(retryAfterSec) } },
    );
  }

  try {
    const body = (await request.json()) as { identifier?: string; password?: string };
    const identifier = typeof body.identifier === "string" ? body.identifier : "";
    const password = typeof body.password === "string" ? body.password : "";

    const result = await verifyPortalCredentials(identifier, password);
    if (!result.ok) {
      if (result.code !== "missing") recordFailure(ipKey);
      const status = result.code === "db" ? 503 : 401;
      const message =
        result.code === "missing"
          ? "Identifiant et mot de passe requis."
          : result.code === "db"
            ? "Service temporairement indisponible (base de données)."
            : "Identifiant ou mot de passe incorrect.";
      return NextResponse.json({ ok: false, message }, { status });
    }

    recordSuccess(ipKey);

    const accountRows = await query<Array<RowDataPacket & { role: string; tag_id: string | null }>>(
      'SELECT role, tag_id FROM accounts WHERE id = ? LIMIT 1',
      [result.accountId]
    );
    const accountRole = accountRows[0]?.role ?? 'player';
    const accountTagId = accountRows[0]?.tag_id ?? null;

    const signedAccount = await signCookieValue(String(result.accountId));
    const signedRole = await signCookieValue(accountRole);

    const jar = cookies();
    jar.set(COOKIE_NAME, signedAccount, {
      httpOnly: true,
      sameSite: "lax",
      secure: process.env.NODE_ENV === "production",
      path: "/",
      maxAge: COOKIE_MAX_AGE_SEC,
    });
    jar.set('lcdlln_portal_role', signedRole, {
      httpOnly: true,
      sameSite: 'lax',
      secure: process.env.NODE_ENV === 'production',
      path: '/',
      maxAge: COOKIE_MAX_AGE_SEC,
    });

    return NextResponse.json({
      ok: true,
      login: result.login,
      tagId: accountTagId,
      redirect: accountRole === 'admin' ? '/admin' : '/player',
    });
  } catch {
    return NextResponse.json({ ok: false, message: "Requête invalide." }, { status: 400 });
  }
}
