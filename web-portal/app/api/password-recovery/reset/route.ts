import { NextResponse } from "next/server";
import { resetPasswordWithToken } from "@/lib/passwordRecovery";

export async function POST(request: Request) {
  try {
    const body = (await request.json()) as {
      token?: string;
      newPassword?: string;
    };

    const result = await resetPasswordWithToken(body.token || "", body.newPassword || "");
    return NextResponse.json(result, { status: result.ok ? 200 : 400 });
  } catch (error) {
    console.error("[POST /api/password-recovery/reset]", error);
    return NextResponse.json({ ok: false, message: "Erreur serveur" }, { status: 500 });
  }
}
