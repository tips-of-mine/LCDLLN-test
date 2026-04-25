import { NextResponse } from "next/server";
import { cookies } from "next/headers";
import { readSession } from "@/lib/session";
import { updatePrivacySettings } from "@/lib/playerPrivacy";

export async function PATCH(request: Request) {
  const session = readSession(cookies());
  if (!session) return NextResponse.json({ ok: false, message: "Non authentifié." }, { status: 401 });

  let body: { profileVisibility?: string };
  try {
    body = await request.json();
  } catch {
    return NextResponse.json({ ok: false, message: "Requête invalide." }, { status: 400 });
  }

  try {
    const result = await updatePrivacySettings(session.accountId, {
      profileVisibility: body.profileVisibility as "public" | "friends" | "private" | undefined,
    });
    return NextResponse.json(result, { status: result.ok ? 200 : 400 });
  } catch {
    return NextResponse.json({ ok: false, message: "Erreur serveur." }, { status: 500 });
  }
}
