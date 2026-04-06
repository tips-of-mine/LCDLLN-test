import Link from "next/link";

export default function AdminAcceptancesPage() {
  return (
    <>
      <div className="page-header">
        <h1>Suivi des acceptations</h1>
        <p>
          Consultation des acceptations CGU par compte.
          Données en lecture seule, corrélation avec les profils joueurs.
        </p>
      </div>

      {/* Filters */}
      <div className="card" style={{ display: "flex", gap: "1rem", alignItems: "flex-end", flexWrap: "wrap" }}>
        <div className="field" style={{ flex: 1, minWidth: "12rem" }}>
          <label>Version CGU</label>
          <select disabled>
            <option>Toutes les versions</option>
          </select>
        </div>
        <div className="field" style={{ flex: 1, minWidth: "12rem" }}>
          <label>Recherche compte</label>
          <input type="text" placeholder="ID, login ou e-mail…" disabled />
        </div>
        <button className="btn btn-secondary" disabled>Filtrer</button>
      </div>

      {/* Table */}
      <div className="table-wrapper">
        <table>
          <thead>
            <tr>
              <th>Compte</th>
              <th>Login</th>
              <th>Version CGU</th>
              <th>Date d&apos;acceptation</th>
              <th>Source</th>
            </tr>
          </thead>
          <tbody>
            <tr>
              <td colSpan={5} style={{ textAlign: "center", color: "var(--muted)", padding: "2rem 1rem" }}>
                Tableau à alimenter via l&apos;API authentifiée (rôle admin).
                <br />
                <span className="text-xs">Table : <code>account_terms_acceptances</code></span>
              </td>
            </tr>
          </tbody>
        </table>
      </div>

      {/* Export */}
      <div className="flex items-center justify-between flex-wrap gap-1 mt-2">
        <Link href="/admin" className="btn btn-ghost">&larr; Retour à l&apos;administration</Link>
        <button className="btn btn-secondary" disabled>Exporter CSV</button>
      </div>
    </>
  );
}
