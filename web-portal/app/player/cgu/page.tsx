import Link from "next/link";
import { redirect } from "next/navigation";
import { getSession } from "@/lib/session";
import { query } from "@/lib/db";
import { CguAcceptButton } from "@/components/CguAcceptButton";
import type { RowDataPacket } from "mysql2/promise";

type CguRow = RowDataPacket & {
  id: number;
  version_label: string;
  published_at: string | null;
  status: string;
  accepted_at: string | null;
  title: string;
};

export const dynamic = "force-dynamic";

export default async function PlayerCguPage() {
  const session = await getSession();
  if (!session) redirect("/login?redirect=/player/cgu");

  const accountId = session.accountId;

  let rows: CguRow[] = [];
  let dbError = false;
  try {
    rows = await query<CguRow[]>(
      `SELECT te.id, te.version_label, te.published_at, te.status,
              ata.accepted_at,
              COALESCE(tl.title, te.version_label) as title
       FROM terms_editions te
       LEFT JOIN terms_localizations tl ON tl.edition_id = te.id AND tl.locale = 'fr'
       LEFT JOIN account_terms_acceptances ata ON ata.edition_id = te.id AND ata.account_id = ?
       WHERE te.status = 'published'
       ORDER BY te.published_at DESC, te.id DESC`,
      [accountId]
    );
  } catch (err) {
    console.error("[PlayerCguPage] query error:", err);
    dbError = true;
  }

  const pending = rows.filter((r) => !r.accepted_at);
  const allAccepted = rows.length > 0 && pending.length === 0;

  return (
    <div className="wp-main narrow">
      <div className="wp-page-header">
        <h1>Mes conditions générales</h1>
        <p>
          Consultez les versions en vigueur et acceptez les nouvelles éditions
          pour maintenir votre accès au jeu.
        </p>
      </div>

      {/* Status banner */}
      {!dbError && rows.length > 0 && (
        <div className="wp-card" style={{
          marginBottom: 24,
          borderColor: allAccepted ? "rgba(95,184,110,.3)" : "rgba(232,165,92,.4)",
          background: allAccepted ? "rgba(95,184,110,.04)" : "rgba(232,165,92,.04)",
        }}>
          <div style={{ display: "flex", alignItems: "center", gap: 12 }}>
            <span style={{ fontSize: "1.25rem" }}>{allAccepted ? "✅" : "⚠️"}</span>
            <div>
              <div style={{
                fontFamily: "var(--font-display)", fontSize: 12, letterSpacing: ".14em",
                textTransform: "uppercase", marginBottom: 4,
                color: allAccepted ? "var(--ln-success)" : "var(--ln-warning)",
              }}>
                {allAccepted ? "CGU à jour" : `${pending.length} CGU en attente d'acceptation`}
              </div>
              <p style={{ margin: 0, fontFamily: "var(--font-body)", fontStyle: "italic", fontSize: 13, color: "var(--ln-muted)" }}>
                {allAccepted
                  ? "Vous avez accepté toutes les versions en vigueur."
                  : "Acceptez les CGU ci-dessous pour conserver l'accès au jeu."}
              </p>
            </div>
          </div>
        </div>
      )}

      {dbError && (
        <div className="wp-alert error" style={{ marginBottom: 24 }}>
          Impossible de charger les CGU. Vérifiez la connexion à la base de données.
        </div>
      )}

      {/* Pending CGU first */}
      {pending.length > 0 && (
        <>
          <p className="wp-section-title">À accepter</p>
          <div style={{ display: "flex", flexDirection: "column", gap: 12, marginBottom: 24 }}>
            {pending.map((row) => (
              <div key={row.id} className="wp-card" style={{
                borderColor: "rgba(232,165,92,.4)", background: "rgba(232,165,92,.03)",
                display: "flex", alignItems: "center", justifyContent: "space-between",
                flexWrap: "wrap", gap: 12,
              }}>
                <div>
                  <div style={{ fontFamily: "var(--font-display)", fontSize: 14, color: "var(--ln-text)", marginBottom: 4 }}>
                    {row.title}
                  </div>
                  <div style={{ fontFamily: "var(--font-mono)", fontSize: 11, color: "var(--ln-muted)" }}>
                    {row.version_label}
                    {row.published_at && (
                      <> · Publiée le {new Date(row.published_at).toLocaleDateString("fr-FR", { day: "2-digit", month: "long", year: "numeric" })}</>
                    )}
                  </div>
                </div>
                <CguAcceptButton editionId={row.id} />
              </div>
            ))}
          </div>
        </>
      )}

      {/* History table */}
      <p className="wp-section-title">Historique des acceptations</p>
      <div className="wp-card" style={{ padding: 0, overflow: "hidden" }}>
        <table style={{ width: "100%", borderCollapse: "collapse", fontSize: 13.5 }}>
          <thead>
            <tr style={{ borderBottom: "1px solid var(--ln-border)", background: "rgba(0,0,0,.15)" }}>
              {["Version", "Titre", "Date de publication", "Acceptation"].map((h) => (
                <th key={h} style={{
                  padding: "10px 16px", textAlign: "left",
                  fontFamily: "var(--font-ui)", fontSize: 11, letterSpacing: ".15em",
                  textTransform: "uppercase", color: "var(--ln-muted)", fontWeight: 500,
                }}>
                  {h}
                </th>
              ))}
            </tr>
          </thead>
          <tbody>
            {rows.length === 0 ? (
              <tr>
                <td colSpan={4} style={{ padding: "2rem", textAlign: "center", color: "var(--ln-muted)", fontStyle: "italic" }}>
                  Aucune condition générale publiée.
                </td>
              </tr>
            ) : (
              rows.map((row, i) => (
                <tr key={row.id} style={{ borderBottom: i < rows.length - 1 ? "1px solid var(--ln-border)" : "none" }}>
                  <td style={{ padding: "12px 16px", fontFamily: "var(--font-display)", fontSize: 12, letterSpacing: ".06em", color: "var(--ln-muted)" }}>
                    {row.version_label}
                  </td>
                  <td style={{ padding: "12px 16px", color: "var(--ln-text)" }}>
                    {row.title}
                  </td>
                  <td style={{ padding: "12px 16px", color: "var(--ln-muted)", fontSize: 12 }}>
                    {row.published_at
                      ? new Date(row.published_at).toLocaleDateString("fr-FR", { day: "2-digit", month: "long", year: "numeric" })
                      : "—"}
                  </td>
                  <td style={{ padding: "12px 16px" }}>
                    {row.accepted_at ? (
                      <span className="wp-badge active">
                        {new Date(row.accepted_at).toLocaleDateString("fr-FR", { day: "2-digit", month: "2-digit", year: "numeric" })}
                      </span>
                    ) : (
                      <span className="wp-badge planned">En attente</span>
                    )}
                  </td>
                </tr>
              ))
            )}
          </tbody>
        </table>
      </div>

      <div style={{ marginTop: 24 }}>
        <Link href="/player" className="btn btn-ghost">&larr; Retour à l&apos;espace joueur</Link>
      </div>
    </div>
  );
}
