import Link from "next/link";
import { RecoveryProfileForm } from "@/components/player/RecoveryProfileForm";

type PageProps = {
  searchParams: { accountId?: string };
};

export default function RecoveryProfilePage({ searchParams }: PageProps) {
  const accountId = Number.parseInt(searchParams.accountId || "", 10);

  return (
    <div className="wp-main narrow">
      <div className="wp-page-header">
        <h1>Profil de récupération</h1>
        <p>
          Ces informations de vérification sont utilisées par le parcours
          « mot de passe oublié » du portail.
        </p>
      </div>

      {!Number.isFinite(accountId) || accountId <= 0 ? (
        <div className="wp-alert warning">
          <span className="wp-alert-icon">⚠</span>
          Ajoutez <code>?accountId=1</code> à l&apos;URL pour charger un compte en dev.
        </div>
      ) : (
        <>
          <RecoveryProfileForm accountId={accountId} />
          <div className="wp-card" style={{ marginTop: 16 }}>
            <p style={{ fontFamily: "var(--font-body)", fontStyle: "italic", fontSize: 13.5, color: "var(--ln-muted)", margin: 0 }}>
              Une fois le profil renseigné, le joueur peut utiliser{" "}
              <Link href="/password-recovery">la page de récupération</Link> pour recevoir un
              lien de changement de mot de passe valable 10 minutes.
            </p>
          </div>
        </>
      )}
    </div>
  );
}
