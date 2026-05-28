import { NextResponse } from "next/server";
import { cookies } from "next/headers";
import { verifyPortalCredentials } from "@/lib/auth/portalLogin";
import { query } from "@/lib/db/connection";
import type { RowDataPacket } from "mysql2/promise";
import { isStaff, normalizeRole } from "@/lib/auth/roles";
import { createSession, SESSION_COOKIE_NAME, SESSION_MAX_AGE_SEC } from "@/lib/auth/session";
import { logError } from "@/lib/log";

export async function POST(request: Request) {
  try {
    const body = (await request.json()) as { identifier?: string; password?: string };
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

    const accountRows = await query<Array<RowDataPacket & { role: string; tag_id: string | null }>>(
      'SELECT role, tag_id FROM accounts WHERE id = ? LIMIT 1',
      [result.accountId]
    );
    const accountRole = normalizeRole(accountRows[0]?.role);
    const accountTagId = accountRows[0]?.tag_id ?? null;

    // Crée une session côté serveur : génère un session_token de 256 bits,
    // INSERT dans portal_sessions, et retourne le token à poser en cookie.
    // C'est le SEUL mécanisme valide pour qu'un client soit reconnu comme
    // authentifié par getSession() — le cookie est désormais opaque (un
    // simple identifiant aléatoire) et non altérable utilement par un
    // attaquant. L'autorité (account_id, role) est lue depuis la DB à
    // chaque requête, jamais depuis un cookie.
    const userAgent = request.headers.get('user-agent');
    // x-forwarded-for est posé par les reverse-proxies (typiquement notre
    // setup en prod). Sans proxy, on retombe sur null — la colonne est
    // nullable côté DB.
    const forwardedFor = request.headers.get('x-forwarded-for');
    const ip = forwardedFor?.split(',')[0]?.trim() || null;
    const sessionToken = await createSession(result.accountId, { userAgent, ip });

    const jar = cookies();
    jar.set(SESSION_COOKIE_NAME, sessionToken, {
      httpOnly: true,
      sameSite: "lax",
      secure: process.env.NODE_ENV === "production",
      path: "/",
      maxAge: SESSION_MAX_AGE_SEC,
    });

    return NextResponse.json({
      ok: true,
      login: result.login,
      tagId: accountTagId,
      redirect: isStaff(accountRole) ? '/admin' : '/player',
    });
  } catch (err) {
    logError("POST /api/auth/login", "Login handler failed", { err });
    return NextResponse.json({ ok: false, message: "Requête invalide." }, { status: 400 });
  }
}
