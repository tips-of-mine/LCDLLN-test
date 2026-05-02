import type { Metadata } from "next";
import { ResetPasswordForm } from "@/components/ResetPasswordForm";

// Empêche que le token de reset ne fuite via le header Referer vers d'éventuelles
// ressources tierces chargées par cette page.
export const metadata: Metadata = {
  referrer: "no-referrer",
};

export default function PasswordRecoveryResetPage({
  searchParams,
}: {
  searchParams: { token?: string };
}) {
  const token = searchParams.token || "";

  return (
    <>
      <h1>Changer le mot de passe</h1>
      <p>
        Ce lien est valable 10 minutes et ne peut etre utilise qu&apos;une seule fois. En cas
        d&apos;expiration, recommencez la verification sur le portail.
      </p>
      {token ? (
        <ResetPasswordForm token={token} />
      ) : (
        <div className="card error-box">Lien incomplet : le token de reinitialisation est absent.</div>
      )}
    </>
  );
}
