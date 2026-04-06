import Link from "next/link";

export default function AdminHomePage() {
  return (
    <>
      <div className="page-header">
        <h1>Administration</h1>
        <p>
          Panneau d&apos;administration réservé aux comptes autorisés.
          Gestion des CGU, suivi des acceptations et consultation des profils joueurs en lecture seule.
        </p>
      </div>

      {/* Admin stats */}
      <div className="stats-row">
        <div className="stat-item">
          <div className="stat-value">—</div>
          <div className="stat-label">Comptes actifs</div>
        </div>
        <div className="stat-item">
          <div className="stat-value">—</div>
          <div className="stat-label">CGU publiées</div>
        </div>
        <div className="stat-item">
          <div className="stat-value">—</div>
          <div className="stat-label">Bugs signalés</div>
        </div>
        <div className="stat-item">
          <div className="stat-value">—</div>
          <div className="stat-label">Acceptations CGU</div>
        </div>
      </div>

      {/* Admin sections */}
      <h2>Modules d&apos;administration</h2>
      <div className="card-grid">
        <Link href="/admin/cgu" style={{ textDecoration: "none" }}>
          <div className="card card-interactive" style={{ margin: 0 }}>
            <div className="feature-icon orange">&#128220;</div>
            <h3 className="mt-0">Gestion des CGU</h3>
            <p className="text-sm mb-0">
              Créer, modifier, publier et retirer les conditions générales.
              Gestion multilingue et versionnée.
            </p>
            <div className="mt-2">
              <span className="badge badge-warning">CRUD</span>
            </div>
          </div>
        </Link>

        <Link href="/admin/acceptances" style={{ textDecoration: "none" }}>
          <div className="card card-interactive" style={{ margin: 0 }}>
            <div className="feature-icon green">&#9989;</div>
            <h3 className="mt-0">Suivi des acceptations</h3>
            <p className="text-sm mb-0">
              Visualiser quels joueurs ont accepté quelles versions des CGU,
              avec dates et historique complet.
            </p>
            <div className="mt-2">
              <span className="badge badge-success">Lecture seule</span>
            </div>
          </div>
        </Link>

        <div className="card" style={{ margin: 0, opacity: 0.6 }}>
          <div className="feature-icon">&#128100;</div>
          <h3 className="mt-0">Profils joueurs</h3>
          <p className="text-sm mb-0">
            Consultation en lecture seule des profils joueurs.
            Aucune modification depuis cet écran.
          </p>
          <div className="mt-2">
            <span className="badge badge-muted">Bientôt</span>
          </div>
        </div>

        <div className="card" style={{ margin: 0, opacity: 0.6 }}>
          <div className="feature-icon purple">&#128202;</div>
          <h3 className="mt-0">Statistiques</h3>
          <p className="text-sm mb-0">
            Tableaux de bord avec métriques d&apos;utilisation,
            taux d&apos;acceptation CGU et activité globale.
          </p>
          <div className="mt-2">
            <span className="badge badge-muted">Bientôt</span>
          </div>
        </div>
      </div>

      {/* Security note */}
      <div className="card" style={{ borderColor: "rgba(245, 158, 11, 0.3)", marginTop: "1.5rem" }}>
        <div className="flex items-center gap-1 mb-1">
          <span style={{ fontSize: "1.1rem" }}>&#9888;&#65039;</span>
          <h3 className="mt-0 mb-0">Sécurité</h3>
        </div>
        <p className="text-sm mb-0">
          L&apos;accès à ce panneau est restreint aux comptes avec le rôle administrateur.
          Toutes les actions sont journalisées. Les profils joueurs sont en lecture seule :
          aucune modification arbitraire n&apos;est possible depuis cet écran (spécification).
        </p>
      </div>
    </>
  );
}
