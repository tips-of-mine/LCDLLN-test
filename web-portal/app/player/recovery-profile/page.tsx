import Link from "next/link";
import { redirect } from "next/navigation";
import { RecoveryProfileForm } from "@/components/player/RecoveryProfileForm";
import { getSession } from "@/lib/auth/session";

export const dynamic = "force-dynamic";

// Source d'autorité = la session (cookie `lcdlln_portal_account` posé au login).
// Avant ce fix la page exigeait `?accountId=N` manuellement dans l'URL — un
// TODO dev qui n'avait jamais été terminé. Le middleware `/player/:path*` +
// getSession() garantissent un accountId valide ici.
export default async function RecoveryProfilePage() {
  const session = await getSession();
  if (!session) redirect("/login?redirect=/player/recovery-profile");

  const accountId = session.accountId;

  return (
    <div className="wp-main narrow">
      <div className="wp-page-header">
        <h1>Profil de récupération</h1>
        <p>
          Ces informations de vérification sont utilisées par le parcours
          « mot de passe oublié » du portail.
        </p>
      </div>

      <RecoveryProfileForm accountId={accountId} />
      <div className="wp-card" style={{ marginTop: 16 }}>
        <p style={{ fontFamily: "var(--font-body)", fontStyle: "italic", fontSize: 13.5, color: "var(--ln-muted)", margin: 0 }}>
          Une fois le profil renseigné, le joueur peut utiliser{" "}
          <Link href="/password-recovery">la page de récupération</Link> pour recevoir un
          lien de changement de mot de passe valable 10 minutes.
        </p>
      </div>
    </div>
  );
}
