import { NextResponse } from "next/server";
import { requestPasswordRecovery } from "@/lib/passwordRecovery";

export async function POST(request: Request) {
  try {
    const body = (await request.json()) as {
      loginOrEmail?: string;
      birthDate?: string;
      age?: string;
      address?: string;
      city?: string;
      postalCode?: string;
      secretAnswers?: string[];
    };

    const result = await requestPasswordRecovery({
      loginOrEmail: body.loginOrEmail || "",
      birthDate: body.birthDate || "",
      age: body.age || "",
      address: body.address || "",
      city: body.city || "",
      postalCode: body.postalCode || "",
      secretAnswers: Array.isArray(body.secretAnswers) ? body.secretAnswers : [],
    });

    return NextResponse.json(result);
  } catch (error) {
    const message = error instanceof Error ? error.message : "Erreur inconnue.";
    return NextResponse.json({ accepted: false, delivery: "noop", message }, { status: 500 });
  }
}
