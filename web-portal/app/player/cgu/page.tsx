import { cookies } from "next/headers";
import { redirect } from "next/navigation";
import Link from "next/link";
import { readSession } from "@/lib/session";
import { getCguAcceptances } from "@/lib/playerCgu";

export const dynamic = "force-dynamic";

function formatDate(dateStr: string | null): string {
  if (!dateStr) return "—";
  return new Date(dateStr).toLocaleDateString("fr-FR", {
    day: "2-digit",
    month: "long",
    year: "numeric",
  });
}

export default async function PlayerCguPage() {
  const session = readSession(cookies());
  if (!session) redirect("/login?next=/player/cgu");

  let acceptances = await getCguAcceptances(session.accountId).catch(() => null);

  return (
    <div className="wp-main narrow">
      <div className="wp-page-header">
        <h1>Mes conditions générales</h1>
        <p>Historique des versions acceptées et refusées.</p>
      </div>

      {acceptances === null ? (
        <div className="wp-card" style={{ textAlign: "center", color: "var(--ln-muted)" }}>
          Données temporairement indisponibles.
        </div>
      ) : acceptances.length === 0 ? (
        <div className="wp-card" style={{ textAlign: "center", color: "var(--ln-muted)" }}>
          <p style={{ fontStyle: "italic" }}>Aucune CGU publiée pour le moment.</p>
        </div>
      ) : (
        <>
          {acceptances.some((a) => a.status === "published" && !a.accepted) && (
            <div className="wp-card" style={{ marginBottom: 16, borderColor: "rgba(220,80,80,.4)", background: "rgba(220,80,80,.05)" }}>
              <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
                <span style={{ fontSize: "1.1rem" }}>⚠</span>
                <div>
                  <div style={{ fontFamily: "var(--font-display)", fontSize: 11, letterSpacing: ".14em", textTransform: "uppercase", color: "var(--ln-danger)", marginBottom: 4 }}>
                    CGU non acceptée
                  </div>
                  <p style={{ margin: 0, fontSize: 13, color: "var(--ln-muted)" }}>
                    Une ou plusieurs versions en vigueur n&apos;ont pas encore été acceptées.
                  </p>
                </div>
              </div>
            </div>
          )}

          <div className="wp-table-wrap">
            <table className="wp-table">
              <thead>
                <tr>
                  <th>Version</th>
                  <th>Publiée le</th>
                  <th>Date d&apos;acceptation</th>
                  <th>Statut</th>
                </tr>
              </thead>
              <tbody>
                {acceptances.map((a) => (
                  <tr key={a.editionId}>
                    <td style={{ fontFamily: "var(--font-display)" }}>{a.title}</td>
                    <td>{formatDate(a.publishedAt)}</td>
                    <td>{formatDate(a.acceptedAt)}</td>
                    <td>
                      {a.accepted ? (
                        <span className="wp-badge active">Acceptée</span>
                      ) : a.status === "published" ? (
                        <span className="wp-badge" style={{ borderColor: "var(--ln-danger)", color: "var(--ln-danger)" }}>Non acceptée</span>
                      ) : (
                        <span className="wp-badge planned">Retirée</span>
                      )}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </>
      )}

      <div style={{ marginTop: 24 }}>
        <Link href="/player" className="btn btn-ghost">&larr; Retour à l&apos;espace joueur</Link>
      </div>
    </div>
  );
}
