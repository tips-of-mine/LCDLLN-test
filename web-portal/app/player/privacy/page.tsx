import { cookies } from "next/headers";
import { redirect } from "next/navigation";
import Link from "next/link";
import { readSession } from "@/lib/session";
import { getPrivacySettings } from "@/lib/playerPrivacy";
import { getCguAcceptances } from "@/lib/playerCgu";
import { PrivacyForm } from "@/components/player/PrivacyForm";

export const dynamic = "force-dynamic";

export default async function PlayerPrivacyPage() {
  const session = readSession(cookies());
  if (!session) redirect("/login?next=/player/privacy");

  const [privacy, acceptances] = await Promise.all([
    getPrivacySettings(session.accountId),
    getCguAcceptances(session.accountId).catch(() => []),
  ]);

  if (!privacy) redirect("/player");

  return (
    <div className="wp-main narrow">
      <div className="wp-page-header">
        <h1>Vie privée</h1>
        <p>Gérez la visibilité de votre profil et consultez vos CGU.</p>
      </div>

      <div className="wp-card" style={{ marginBottom: "1.5rem" }}>
        <div className="wp-section-title" style={{ marginBottom: "1rem" }}>Visibilité du profil</div>
        <PrivacyForm initial={privacy.profileVisibility} />
      </div>

      <div className="wp-card">
        <div className="wp-section-title" style={{ marginBottom: "1rem" }}>Conditions Générales d&apos;Utilisation</div>
        {acceptances.length === 0 ? (
          <p style={{ fontSize: 13, color: "var(--ln-muted)", fontStyle: "italic" }}>
            Aucune CGU publiée pour le moment.
          </p>
        ) : (
          <div className="wp-table-wrap">
            <table className="wp-table">
              <thead>
                <tr>
                  <th>Version</th>
                  <th>Publiée le</th>
                  <th>Acceptée le</th>
                </tr>
              </thead>
              <tbody>
                {acceptances.map((a) => (
                  <tr key={a.editionId}>
                    <td>{a.title}</td>
                    <td>{new Date(a.publishedAt).toLocaleDateString("fr-FR")}</td>
                    <td>
                      {a.acceptedAt
                        ? new Date(a.acceptedAt).toLocaleDateString("fr-FR")
                        : <span style={{ color: "var(--ln-muted)" }}>—</span>}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>

      <div style={{ marginTop: "1.5rem" }}>
        <Link href="/player" className="btn btn-ghost">&larr; Espace joueur</Link>
      </div>
    </div>
  );
}
