import { cookies } from "next/headers";
import { redirect } from "next/navigation";
import Link from "next/link";
import { readSession } from "@/lib/session";
import { PasswordChangeForm } from "@/components/player/PasswordChangeForm";

export const dynamic = "force-dynamic";

export default function PlayerSecurityPage() {
  const session = readSession(cookies());
  if (!session) redirect("/login?next=/player/security");

  return (
    <div className="wp-main narrow">
      <div className="wp-page-header">
        <h1>Sécurité du compte</h1>
        <p>Modifiez votre mot de passe et configurez les protections supplémentaires.</p>
      </div>

      <div className="wp-card" style={{ marginBottom: "1.5rem" }}>
        <div className="wp-section-title" style={{ marginBottom: "1rem" }}>Changer le mot de passe</div>
        <PasswordChangeForm />
      </div>

      <div className="wp-card">
        <div className="wp-section-title" style={{ marginBottom: "0.5rem" }}>
          Double authentification (MFA)
          <span className="wp-badge planned" style={{ marginLeft: 8 }}>Bientôt</span>
        </div>
        <p style={{ fontSize: 13, color: "var(--ln-muted)", margin: 0 }}>
          La double authentification (TOTP / application d&apos;authentification) sera disponible dans
          une prochaine mise à jour du portail.
        </p>
      </div>

      <div style={{ marginTop: "1.5rem" }}>
        <Link href="/player" className="btn btn-ghost">&larr; Espace joueur</Link>
      </div>
    </div>
  );
}
