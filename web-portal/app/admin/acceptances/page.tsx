import Link from "next/link";
import { redirect } from "next/navigation";
import { getSession } from "@/lib/auth/session";
import { isStaff } from "@/lib/auth/roles";
import { query } from "@/lib/db/connection";
import { logError } from "@/lib/log";
import type { RowDataPacket } from "mysql2/promise";

type AcceptanceRow = RowDataPacket & {
  account_id: number;
  login: string;
  email: string;
  version_label: string;
  accepted_at: string;
};

export default async function AdminAcceptancesPage() {
  const session = await getSession();
  if (!session) redirect("/login?redirect=/admin/acceptances");
  if (!isStaff(session.role)) redirect("/");

  let rows: AcceptanceRow[] = [];
  let dbError = false;

  try {
    rows = await query<AcceptanceRow[]>(
      `SELECT ata.account_id, a.login, a.email,
              te.version_label, ata.accepted_at
       FROM account_terms_acceptances ata
       JOIN accounts a ON a.id = ata.account_id
       JOIN terms_editions te ON te.id = ata.edition_id
       ORDER BY ata.accepted_at DESC
       LIMIT 200`,
      []
    );
  } catch (err) {
    logError("AdminAcceptancesPage", "DB error", { err });
    dbError = true;
  }

  return (
    <div className="wp-main">
      <div className="wp-page-header">
        <h1>Suivi des acceptations</h1>
        <p>Consultation des acceptations CGU par compte. Données en lecture seule.</p>
      </div>

      {dbError && (
        <div className="wp-alert error" style={{ marginBottom: "1.5rem" }}>
          Impossible de charger les acceptations. Vérifiez la connexion à la base de données.
        </div>
      )}

      {!dbError && (
        <div className="wp-card" style={{ padding: 0, overflow: "hidden" }}>
          <table style={{ width: "100%", borderCollapse: "collapse", fontSize: 13.5 }}>
            <thead>
              <tr style={{ borderBottom: "1px solid var(--ln-border)", background: "rgba(0,0,0,.15)" }}>
                {["#", "Login", "Email", "Version CGU", "Date d'acceptation"].map((h) => (
                  <th key={h} style={{ padding: "10px 16px", textAlign: "left", fontFamily: "var(--font-ui)", fontSize: 11, letterSpacing: ".15em", textTransform: "uppercase", color: "var(--ln-muted)", fontWeight: 500 }}>
                    {h}
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {rows.length === 0 ? (
                <tr>
                  <td colSpan={5} style={{ padding: "2rem", textAlign: "center", color: "var(--ln-muted)", fontStyle: "italic" }}>
                    Aucune acceptation CGU enregistrée.
                  </td>
                </tr>
              ) : (
                rows.map((row, i) => (
                  <tr key={`${row.account_id}-${row.version_label}`} style={{ borderBottom: i < rows.length - 1 ? "1px solid var(--ln-border)" : "none" }}>
                    <td style={{ padding: "10px 16px", color: "var(--ln-muted)", fontFamily: "var(--font-mono)", fontSize: 12 }}>
                      {row.account_id}
                    </td>
                    <td style={{ padding: "10px 16px", color: "var(--ln-text)", fontWeight: 500 }}>
                      {row.login}
                    </td>
                    <td style={{ padding: "10px 16px", color: "var(--ln-muted)", fontSize: 12 }}>
                      {row.email}
                    </td>
                    <td style={{ padding: "10px 16px" }}>
                      <span className="wp-badge active" style={{ fontSize: 11 }}>{row.version_label}</span>
                    </td>
                    <td style={{ padding: "10px 16px", color: "var(--ln-muted)", fontFamily: "var(--font-mono)", fontSize: 12 }}>
                      {new Date(row.accepted_at).toLocaleDateString("fr-FR", {
                        day: "2-digit", month: "2-digit", year: "numeric",
                        hour: "2-digit", minute: "2-digit",
                      })}
                    </td>
                  </tr>
                ))
              )}
            </tbody>
          </table>
        </div>
      )}

      <div style={{ marginTop: "2rem" }}>
        <Link href="/admin" style={{ fontFamily: "var(--font-ui)", fontSize: 10, letterSpacing: ".18em", textTransform: "uppercase", color: "var(--ln-muted)", textDecoration: "none" }}>
          &larr; Retour à l&apos;administration
        </Link>
      </div>
    </div>
  );
}
