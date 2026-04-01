import Link from "next/link";
import { RecoveryProfileForm } from "@/components/RecoveryProfileForm";

type PageProps = {
  searchParams: {
    accountId?: string;
  };
};

export default function RecoveryProfilePage({ searchParams }: PageProps) {
  const accountId = Number.parseInt(searchParams.accountId || "", 10);

  return (
    <>
      <h1>Profil de recuperation</h1>
      <p>
        Cette page permet d&apos;enrichir les informations de verification utilisees par le parcours
        &laquo; mot de passe oublie &raquo; du portail.
      </p>
      {!Number.isFinite(accountId) || accountId <= 0 ? (
        <div className="card error-box">
          Ajoutez <code>?accountId=1</code> a l&apos;URL pour charger un compte en dev.
        </div>
      ) : (
        <>
          <RecoveryProfileForm accountId={accountId} />
          <div className="card">
            <p>
              Une fois le profil renseigne, le joueur peut utiliser{" "}
              <Link href="/password-recovery">la page de recuperation</Link> pour recevoir un lien de
              changement de mot de passe valable 10 minutes.
            </p>
          </div>
        </>
      )}
    </>
  );
}
