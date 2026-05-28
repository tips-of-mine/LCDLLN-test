import { NextResponse } from "next/server";
import { resetPasswordWithToken } from "@/lib/auth/passwordRecovery";
import { logError } from "@/lib/log";

export async function POST(request: Request) {
  try {
    const body = (await request.json()) as {
      token?: string;
      newPassword?: string;
    };

    const result = await resetPasswordWithToken(body.token || "", body.newPassword || "");
    return NextResponse.json(result, { status: result.ok ? 200 : 400 });
  } catch (error) {
    logError("POST /api/password-recovery/reset", "Reset failed", { err: error });
    const message = error instanceof Error ? error.message : "Erreur inconnue.";
    return NextResponse.json({ ok: false, message }, { status: 500 });
  }
}
