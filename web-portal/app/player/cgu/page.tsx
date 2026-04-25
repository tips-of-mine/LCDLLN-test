import Link from "next/link";

export default function PlayerCguPage() {
  return (
    <div className="wp-main narrow">
      <div className="wp-page-header">
        <h1>Mes conditions générales</h1>
        <p>
          Consultez les versions acceptées et validez les nouvelles éditions
          pour accélérer votre accès au jeu.
        </p>
      </div>

      <div className="wp-card" style={{ marginBottom: 24, borderColor: "rgba(95,184,110,.3)", background: "rgba(95,184,110,.04)" }}>
        <div style={{ display: "flex", alignItems: "center", gap: 12 }}>
          <span style={{ fontSize: "1.25rem" }}>✅</span>
          <div>
            <div style={{ fontFamily: "var(--font-display)", fontSize: 12, letterSpacing: ".14em", textTransform: "uppercase", color: "var(--ln-success)", marginBottom: 4 }}>CGU à jour</div>
            <p style={{ margin: 0, fontFamily: "var(--font-body)", fontStyle: "italic", fontSize: 13, color: "var(--ln-muted)" }}>
              Vous avez accepté la dernière version en vigueur.
            </p>
          </div>
        </div>
      </div>

      <p className="wp-section-title">Historique des acceptations</p>
      <div className="wp-table-wrap">
        <table className="wp-table">
          <thead>
            <tr>
              <th>Version</th>
              <th>Langue</th>
              <th>Date d&apos;acceptation</th>
              <th>Statut</th>
            </tr>
          </thead>
          <tbody>
            <tr>
              <td colSpan={4} style={{ textAlign: "center", padding: "2rem 1rem" }}>
                <span style={{ fontFamily: "var(--font-body)", fontStyle: "italic", color: "var(--ln-muted)" }}>
                  Les données seront chargées depuis l&apos;API une fois l&apos;authentification active.
                </span>
                <br />
                <span style={{ fontFamily: "var(--font-mono)", fontSize: 11, color: "var(--ln-muted)" }}>
                  Tables : <code>terms_editions</code>, <code>terms_localizations</code>, <code>account_terms_acceptances</code>
                </span>
              </td>
            </tr>
          </tbody>
        </table>
      </div>

      <div style={{ marginTop: 24 }}>
        <Link href="/player" className="btn btn-ghost">&larr; Retour à l&apos;espace joueur</Link>
      </div>
    </div>
  );
}
