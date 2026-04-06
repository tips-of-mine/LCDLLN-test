import Link from "next/link";

export default function PlayerCguPage() {
  return (
    <>
      <div className="page-header">
        <h1>Mes conditions générales</h1>
        <p>
          Consultez les versions acceptées et validez les nouvelles éditions
          pour accélérer votre accès au jeu.
        </p>
      </div>

      {/* Status overview */}
      <div className="card" style={{ borderColor: "rgba(34, 197, 94, 0.3)" }}>
        <div className="flex items-center gap-1">
          <span style={{ fontSize: "1.25rem" }}>&#9989;</span>
          <div>
            <div style={{ fontWeight: 600 }}>CGU à jour</div>
            <p className="text-sm mb-0">
              Vous avez accepté la dernière version en vigueur.
            </p>
          </div>
        </div>
      </div>

      {/* History table */}
      <h2>Historique des acceptations</h2>
      <div className="table-wrapper">
        <table>
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
              <td colSpan={4} style={{ textAlign: "center", color: "var(--muted)", padding: "2rem 1rem" }}>
                Les données seront chargées depuis l&apos;API une fois l&apos;authentification active.
                <br />
                <span className="text-xs">Tables : <code>terms_editions</code>, <code>terms_localizations</code>, <code>account_terms_acceptances</code></span>
              </td>
            </tr>
          </tbody>
        </table>
      </div>

      <div className="mt-2">
        <Link href="/player" className="btn btn-ghost">&larr; Retour à l&apos;espace joueur</Link>
      </div>
    </>
  );
}
