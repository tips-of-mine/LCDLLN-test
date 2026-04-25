import { NextResponse } from "next/server";
import { cookies } from "next/headers";
import { readSession } from "@/lib/session";
import { changePassword } from "@/lib/playerSecurity";

export async function POST(request: Request) {
  const session = readSession(cookies());
  if (!session) return NextResponse.json({ ok: false, message: "Non authentifié." }, { status: 401 });

  let body: { currentPassword?: string; newPassword?: string };
  try {
    body = await request.json();
  } catch {
    return NextResponse.json({ ok: false, message: "Requête invalide." }, { status: 400 });
  }

  try {
    const result = await changePassword(
      session.accountId,
      typeof body.currentPassword === "string" ? body.currentPassword : "",
      typeof body.newPassword === "string" ? body.newPassword : "",
    );
    return NextResponse.json(result, { status: result.ok ? 200 : 400 });
  } catch {
    return NextResponse.json({ ok: false, message: "Erreur serveur." }, { status: 500 });
  }
}
