import { NextResponse } from "next/server";
import { cookies } from "next/headers";
import { readSession } from "@/lib/session";
import { getAccountProfile, updateAccountProfile } from "@/lib/playerProfile";

function unauthorized() {
  return NextResponse.json({ ok: false, message: "Non authentifié." }, { status: 401 });
}

export async function GET() {
  const session = readSession(cookies());
  if (!session) return unauthorized();

  try {
    const profile = await getAccountProfile(session.accountId);
    if (!profile) return NextResponse.json({ ok: false, message: "Compte introuvable." }, { status: 404 });
    return NextResponse.json({ ok: true, profile });
  } catch {
    return NextResponse.json({ ok: false, message: "Erreur serveur." }, { status: 503 });
  }
}

export async function PATCH(request: Request) {
  const session = readSession(cookies());
  if (!session) return unauthorized();

  let body: { firstName?: string; lastName?: string; address?: string; city?: string; postalCode?: string };
  try {
    body = await request.json();
  } catch {
    return NextResponse.json({ ok: false, message: "Requête invalide." }, { status: 400 });
  }

  try {
    const result = await updateAccountProfile(session.accountId, {
      firstName: typeof body.firstName === "string" ? body.firstName : "",
      lastName: typeof body.lastName === "string" ? body.lastName : "",
      address: typeof body.address === "string" ? body.address : "",
      city: typeof body.city === "string" ? body.city : "",
      postalCode: typeof body.postalCode === "string" ? body.postalCode : "",
    });
    return NextResponse.json(result, { status: result.ok ? 200 : 400 });
  } catch {
    return NextResponse.json({ ok: false, message: "Erreur serveur." }, { status: 503 });
  }
}
