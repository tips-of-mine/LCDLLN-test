import { NextResponse } from "next/server";
import { getRecoveryProfile, upsertRecoveryProfile } from "@/lib/auth/passwordRecovery";
import { getSession } from "@/lib/auth/session";
import { logError } from "@/lib/log";

// FAILLE IDOR CORRIGÉE : avant ce fix, GET et POST acceptaient un accountId
// arbitraire (query string pour GET, body pour POST) sans aucun check de
// session. Conséquence côté GET : un attaquant pouvait lire le profil de
// récupération de n'importe quel utilisateur (date de naissance, adresse,
// code postal, **questions secrètes**) par simple curl. Conséquence côté
// POST encore plus grave : un attaquant pouvait ÉCRASER le profil de
// récupération de n'importe quel utilisateur, plantant ses propres
// questions et réponses → bypass du parcours "mot de passe oublié" et
// compromission complète du compte cible.
//
// Maintenant l'accountId vient EXCLUSIVEMENT de la session — query/body
// sont ignorés côté autorité. Cette API ne sert que la page connectée
// /player/recovery-profile (RecoveryProfileForm), elle peut donc exiger
// l'authentification. Le parcours "mot de passe oublié" non connecté
// utilise des endpoints distincts (/api/password-recovery/request|reset)
// qui n'ont pas le même modèle d'accès et restent inchangés.

export async function GET(_request: Request) {
  const session = await getSession();
  if (!session) {
    return NextResponse.json({ error: "Non authentifié." }, { status: 401 });
  }
  try {
    const accountId = session.accountId;
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
    logError("GET /api/password-recovery/profile", "Fetch recovery profile failed", { err: error });
    const message = error instanceof Error ? error.message : "Erreur inconnue.";
    return NextResponse.json({ error: message }, { status: 500 });
  }
}

export async function POST(request: Request) {
  const session = await getSession();
  if (!session) {
    return NextResponse.json({ error: "Non authentifié." }, { status: 401 });
  }
  try {
    const body = (await request.json()) as {
      birthDate?: string;
      address?: string;
      city?: string;
      postalCode?: string;
      secretQuestions?: Array<{ question?: string; answer?: string }>;
    };

    await upsertRecoveryProfile({
      accountId: session.accountId,
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
    logError("POST /api/password-recovery/profile", "Upsert recovery profile failed", { err: error });
    const message = error instanceof Error ? error.message : "Erreur inconnue.";
    return NextResponse.json({ error: message }, { status: 400 });
  }
}
