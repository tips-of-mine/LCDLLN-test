import { cookies } from "next/headers";
import { redirect } from "next/navigation";
import Link from "next/link";
import { readSession } from "@/lib/session";
import { getAccountProfile } from "@/lib/playerProfile";
import { AccountForm } from "@/components/player/AccountForm";
import { EmailChangeForm } from "@/components/player/EmailChangeForm";

export const dynamic = "force-dynamic";

export default async function PlayerAccountPage() {
  const session = readSession(cookies());
  if (!session) redirect("/login?next=/player/account");

  const profile = await getAccountProfile(session.accountId);
  if (!profile) redirect("/player");

  return (
    <div className="wp-main narrow">
      <div className="wp-page-header">
        <h1>Détail du compte</h1>
        <p>Gérez vos informations personnelles.</p>
      </div>

      <div className="wp-card" style={{ marginBottom: "1.5rem" }}>
        <div className="wp-section-title" style={{ marginBottom: "1rem" }}>Informations personnelles</div>
        <AccountForm profile={profile} />
      </div>

      <div className="wp-card">
        <div className="wp-section-title" style={{ marginBottom: "1rem" }}>Changer d&apos;adresse email</div>
        <p style={{ fontSize: 13, color: "var(--ln-muted)", marginBottom: "1rem" }}>
          Un code de vérification sera envoyé à la nouvelle adresse. Votre compte sera temporairement marqué
          comme non vérifié jusqu&apos;à confirmation.
        </p>
        <EmailChangeForm />
      </div>

      <div style={{ marginTop: "1.5rem" }}>
        <Link href="/player" className="btn btn-ghost">&larr; Espace joueur</Link>
      </div>
    </div>
  );
}
