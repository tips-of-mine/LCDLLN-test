// web-portal/app/api/player/characters/[id]/route.ts
import { NextResponse } from "next/server";
import { cookies } from "next/headers";
import { readSession } from "@/lib/session";
import { deleteCharacter } from "@/lib/playerCharacters";

export async function DELETE(
  _request: Request,
  { params }: { params: { id: string } },
) {
  const session = readSession(cookies());
  if (!session) return NextResponse.json({ ok: false, message: "Non authentifié." }, { status: 401 });

  const charId = parseInt(params.id, 10);
  if (!Number.isFinite(charId) || charId <= 0) {
    return NextResponse.json({ ok: false, message: "Identifiant invalide." }, { status: 400 });
  }

  const result = await deleteCharacter(charId, session.accountId);
  return NextResponse.json(result, { status: result.ok ? 200 : 404 });
}
