import { NextResponse } from "next/server";
import { cookies } from "next/headers";
import { verifyPortalCredentials } from "@/lib/portalLogin";
import { signSession, COOKIE_NAME, COOKIE_MAX_AGE_SEC } from "@/lib/session";

const LEGACY_COOKIE = "lcdlln_portal_account";

function sanitizeNext(raw: unknown): string {
  if (typeof raw !== "string") return "/player";
  const trimmed = raw.trim();
  if (!trimmed.startsWith("/") || trimmed.startsWith("//")) return "/player";
  return trimmed;
}

export async function POST(request: Request) {
  try {
    const body = (await request.json()) as {
      identifier?: string;
      password?: string;
      next?: string;
    };
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

    const sessionValue = signSession({
      v: 1,
      accountId: result.accountId,
      tagId: result.tagId,
      login: result.login,
      role: result.role,
    });

    const jar = cookies();
    jar.delete(LEGACY_COOKIE);
    jar.set(COOKIE_NAME, sessionValue, {
      httpOnly: true,
      sameSite: "lax",
      secure: process.env.NODE_ENV === "production",
      path: "/",
      maxAge: COOKIE_MAX_AGE_SEC,
    });

    return NextResponse.json({ ok: true, redirect: sanitizeNext(body.next) });
  } catch {
    return NextResponse.json({ ok: false, message: "Requête invalide." }, { status: 400 });
  }
}
