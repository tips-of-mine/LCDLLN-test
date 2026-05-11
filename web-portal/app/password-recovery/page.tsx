import Link from "next/link";
import { PasswordRecoveryRequestForm } from "@/components/auth/PasswordRecoveryRequestForm";

export default function PasswordRecoveryPage() {
  return (
    <div className="wp-main narrow">
      <div className="wp-page-header">
        <h1>Récupération du mot de passe</h1>
        <p>
          Renseignez vos informations d&apos;identité et vos réponses aux questions secrètes.
          Si la vérification est validée, un lien à usage unique valable 10 minutes
          vous est envoyé par e-mail.
        </p>
      </div>

      <PasswordRecoveryRequestForm />

      <div className="wp-card" style={{ marginTop: 16 }}>
        <p style={{ fontFamily: "var(--font-body)", fontStyle: "italic", fontSize: 13.5, color: "var(--ln-muted)", margin: "0 0 8px" }}>
          Si le lien expire avant utilisation, vous devrez repasser les contrôles puis demander un nouvel e-mail.
        </p>
        <p style={{ fontFamily: "var(--font-body)", fontStyle: "italic", fontSize: 13.5, color: "var(--ln-muted)", margin: 0 }}>
          Pour préparer vos contrôles de récupération sur un compte existant, utilisez{" "}
          <Link href="/player/recovery-profile?accountId=1">la page de profil de récupération</Link>.
        </p>
      </div>
    </div>
  );
}
