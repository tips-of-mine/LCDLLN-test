import { cookies } from "next/headers";
import { redirect } from "next/navigation";
import Link from "next/link";
import { readSession } from "@/lib/session";
import { getAccountProfile } from "@/lib/playerProfile";

export const dynamic = "force-dynamic";

function computeAge(birthDate: string): number | null {
  const date = new Date(`${birthDate}T00:00:00Z`);
  if (Number.isNaN(date.getTime())) return null;
  const now = new Date();
  let age = now.getUTCFullYear() - date.getUTCFullYear();
  const monthDelta = now.getUTCMonth() - date.getUTCMonth();
  if (monthDelta < 0 || (monthDelta === 0 && now.getUTCDate() < date.getUTCDate())) age -= 1;
  return age >= 0 ? age : null;
}

export default async function PlayerParentalPage() {
  const session = readSession(cookies());
  if (!session) redirect("/login?next=/player/parental");

  const profile = await getAccountProfile(session.accountId);
  if (!profile) redirect("/player");

  const age = profile.birthDate ? computeAge(profile.birthDate) : null;
  const isMinor = age !== null && age < 18;

  return (
    <div className="wp-main narrow">
      <div className="wp-page-header">
        <h1>Contrôle parental</h1>
        <p>Supervision du compte par un parent ou tuteur légal pour les joueurs mineurs.</p>
      </div>

      {!profile.birthDate ? (
        <div className="wp-card">
          <div className="wp-section-title" style={{ marginBottom: "0.5rem" }}>Date de naissance non renseignée</div>
          <p style={{ fontSize: 13, color: "var(--ln-muted)" }}>
            Renseignez votre date de naissance dans{" "}
            <Link href="/player/account" style={{ color: "var(--ln-accent)" }}>
              les détails de votre compte
            </Link>{" "}
            pour accéder aux options de contrôle parental.
          </p>
        </div>
      ) : !isMinor ? (
        <div className="wp-card">
          <div className="wp-section-title" style={{ marginBottom: "0.5rem" }}>Compte majeur</div>
          <p style={{ fontSize: 13, color: "var(--ln-muted)" }}>
            Le contrôle parental n&apos;est disponible que pour les joueurs mineurs (moins de 18 ans).
            Votre compte est enregistré avec un âge de <strong>{age} ans</strong>.
          </p>
        </div>
      ) : (
        <>
          <div className="wp-card" style={{ marginBottom: "1.5rem", borderColor: "rgba(95,184,110,.3)" }}>
            <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
              <span style={{ fontSize: "1.25rem" }}>👶</span>
              <div>
                <div style={{ fontFamily: "var(--font-display)", fontSize: 12, letterSpacing: ".14em", textTransform: "uppercase", color: "var(--ln-success)", marginBottom: 4 }}>
                  Compte mineur
                </div>
                <p style={{ margin: 0, fontSize: 13, color: "var(--ln-muted)" }}>
                  Votre compte est enregistré pour un joueur de <strong>{age} ans</strong>.
                </p>
              </div>
            </div>
          </div>

          <div className="wp-card">
            <div className="wp-section-title" style={{ marginBottom: "1rem" }}>
              Consentement parental
              <span className="wp-badge planned" style={{ marginLeft: 8 }}>Bientôt</span>
            </div>
            <p style={{ fontSize: 13, color: "var(--ln-muted)" }}>
              {profile.parentalEmail ? (
                <>
                  Email parental enregistré : <strong>{profile.parentalEmail}</strong>
                  {profile.parentalConsentAt
                    ? ` — Consentement accordé le ${new Date(profile.parentalConsentAt).toLocaleDateString("fr-FR")}.`
                    : " — En attente de consentement."}
                </>
              ) : (
                "Le système de validation parentale sera disponible dans une prochaine mise à jour. " +
                "Un email sera envoyé au parent ou tuteur légal pour confirmer l'autorisation de jeu."
              )}
            </p>
          </div>
        </>
      )}

      <div style={{ marginTop: "1.5rem" }}>
        <Link href="/player" className="btn btn-ghost">&larr; Espace joueur</Link>
      </div>
    </div>
  );
}
