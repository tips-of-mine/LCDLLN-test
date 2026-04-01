import { NextResponse } from "next/server";
import { getRecoveryProfile, upsertRecoveryProfile } from "@/lib/passwordRecovery";

function parseAccountId(value: string | null): number {
  const parsed = Number.parseInt(value || "", 10);
  return Number.isFinite(parsed) && parsed > 0 ? parsed : 0;
}

export async function GET(request: Request) {
  try {
    const { searchParams } = new URL(request.url);
    const accountId = parseAccountId(searchParams.get("accountId"));
    if (!accountId) {
      return NextResponse.json({ error: "accountId invalide." }, { status: 400 });
    }

    const profile = await getRecoveryProfile(accountId);
    return NextResponse.json({
      profile: profile ?? {
        accountId,
        birthDate: "",
        address: "",
        city: "",
        postalCode: "",
        secretQuestions: [],
      },
    });
  } catch (error) {
    const message = error instanceof Error ? error.message : "Erreur inconnue.";
    return NextResponse.json({ error: message }, { status: 500 });
  }
}

export async function POST(request: Request) {
  try {
    const body = (await request.json()) as {
      accountId?: number;
      birthDate?: string;
      address?: string;
      city?: string;
      postalCode?: string;
      secretQuestions?: Array<{ question?: string; answer?: string }>;
    };

    if (!body.accountId || body.accountId <= 0) {
      return NextResponse.json({ error: "accountId invalide." }, { status: 400 });
    }

    await upsertRecoveryProfile({
      accountId: body.accountId,
      birthDate: body.birthDate || "",
      address: body.address || "",
      city: body.city || "",
      postalCode: body.postalCode || "",
      secretQuestions: (body.secretQuestions || []).map((item) => ({
        question: item.question || "",
        answer: item.answer || "",
      })),
    });

    return NextResponse.json({ ok: true });
  } catch (error) {
    const message = error instanceof Error ? error.message : "Erreur inconnue.";
    return NextResponse.json({ error: message }, { status: 400 });
  }
}
