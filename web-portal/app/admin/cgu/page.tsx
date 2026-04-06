import Link from "next/link";

export default function AdminCguPage() {
  return (
    <>
      <div className="page-header">
        <h1>Gestion des CGU</h1>
        <p>
          Création, édition et publication des conditions générales d&apos;utilisation.
          Multilingue et versionné — le jeu n&apos;a pas besoin d&apos;être recompilé pour publier
          une nouvelle version.
        </p>
      </div>

      {/* Action bar */}
      <div className="flex items-center justify-between flex-wrap gap-1 mb-2">
        <div className="flex items-center gap-1">
          <button className="btn btn-primary" disabled>Nouvelle édition</button>
        </div>
        <div className="flex items-center gap-1">
          <span className="badge badge-muted">API non connectée</span>
        </div>
      </div>

      {/* Table */}
      <div className="table-wrapper">
        <table>
          <thead>
            <tr>
              <th>Version</th>
              <th>Statut</th>
              <th>Date de publication</th>
              <th>Langues</th>
              <th>Actions</th>
            </tr>
          </thead>
          <tbody>
            <tr>
              <td colSpan={5} style={{ textAlign: "center", color: "var(--muted)", padding: "2rem 1rem" }}>
                Interface à connecter aux endpoints API du master.
                <br />
                <span className="text-xs">Tables : <code>terms_editions</code>, <code>terms_localizations</code></span>
              </td>
            </tr>
          </tbody>
        </table>
      </div>

      {/* Workflow explanation */}
      <h2>Workflow de publication</h2>
      <div className="card-grid-3">
        <div className="card" style={{ margin: 0 }}>
          <div className="flex items-center gap-1 mb-1">
            <span className="badge badge-muted">1</span>
            <strong className="text-sm">Brouillon</strong>
          </div>
          <p className="text-sm mb-0">
            Rédigez le contenu multilingue. Non visible par les joueurs.
          </p>
        </div>
        <div className="card" style={{ margin: 0 }}>
          <div className="flex items-center gap-1 mb-1">
            <span className="badge badge-warning">2</span>
            <strong className="text-sm">Publié</strong>
          </div>
          <p className="text-sm mb-0">
            Les joueurs sont invités à accepter cette version.
          </p>
        </div>
        <div className="card" style={{ margin: 0 }}>
          <div className="flex items-center gap-1 mb-1">
            <span className="badge badge-error">3</span>
            <strong className="text-sm">Retiré</strong>
          </div>
          <p className="text-sm mb-0">
            Version archivée, remplacée par une édition plus récente.
          </p>
        </div>
      </div>

      <div className="mt-2">
        <Link href="/admin" className="btn btn-ghost">&larr; Retour à l&apos;administration</Link>
      </div>
    </>
  );
}
