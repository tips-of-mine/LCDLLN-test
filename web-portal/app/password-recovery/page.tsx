import Link from "next/link";
import { PasswordRecoveryRequestForm } from "@/components/PasswordRecoveryRequestForm";

export default function PasswordRecoveryPage() {
  return (
    <>
      <h1>Recuperation du mot de passe</h1>
      <p>
        Renseignez vos informations d&apos;identite et vos reponses aux questions secretes. Si la
        verification est validee, un lien a usage unique valable 10 minutes vous est envoye par e-mail.
      </p>
      <PasswordRecoveryRequestForm />
      <div className="card">
        <p>
          Si le lien expire avant utilisation, vous devrez repasser les controles puis demander un nouvel
          e-mail.
        </p>
        <p>
          Pour preparer vos controles de recuperation sur un compte existant, utilisez{" "}
          <Link href="/player/recovery-profile?accountId=1">la page de profil de recuperation</Link>.
        </p>
      </div>
    </>
  );
}
