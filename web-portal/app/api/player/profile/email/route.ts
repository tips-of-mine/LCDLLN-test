import { NextResponse } from "next/server";
import { cookies } from "next/headers";
import { readSession } from "@/lib/session";
import { requestEmailChange } from "@/lib/playerProfile";

export async function POST(request: Request) {
  const session = readSession(cookies());
  if (!session) return NextResponse.json({ ok: false, message: "Non authentifié." }, { status: 401 });

  let body: { newEmail?: string };
  try {
    body = await request.json();
  } catch {
    return NextResponse.json({ ok: false, message: "Requête invalide." }, { status: 400 });
  }

  try {
    const result = await requestEmailChange(
      session.accountId,
      typeof body.newEmail === "string" ? body.newEmail : "",
    );
    return NextResponse.json(result, { status: result.ok ? 200 : 400 });
  } catch {
    return NextResponse.json({ ok: false, message: "Erreur serveur." }, { status: 503 });
  }
}
