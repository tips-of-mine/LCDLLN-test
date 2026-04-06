import Link from "next/link";

export default function PlayerHomePage() {
  return (
    <>
      <div className="page-header">
        <h1>Espace joueur</h1>
        <p>
          Gérez votre profil, suivez vos exploits et consultez vos informations de compte.
          Contenu accessible après connexion.
        </p>
      </div>

      {/* Quick stats */}
      <div className="stats-row">
        <div className="stat-item">
          <div className="stat-value">—</div>
          <div className="stat-label">Exploits débloqués</div>
        </div>
        <div className="stat-item">
          <div className="stat-value">—</div>
          <div className="stat-label">Personnages</div>
        </div>
        <div className="stat-item">
          <div className="stat-value">—</div>
          <div className="stat-label">Bugs signalés</div>
        </div>
      </div>

      {/* Navigation cards */}
      <h2>Mes sections</h2>
      <div className="card-grid">
        <Link href="/player/exploits" style={{ textDecoration: "none" }}>
          <div className="card card-interactive" style={{ margin: 0 }}>
            <div className="feature-icon">&#127942;</div>
            <h3 className="mt-0">Mes Exploits</h3>
            <p className="text-sm mb-0">
              Consultez votre progression, exploits visibles et secrets.
            </p>
            <div className="mt-2">
              <span className="badge">Voir mes exploits</span>
            </div>
          </div>
        </Link>

        <Link href="/player/cgu" style={{ textDecoration: "none" }}>
          <div className="card card-interactive" style={{ margin: 0 }}>
            <div className="feature-icon orange">&#128220;</div>
            <h3 className="mt-0">Mes CGU</h3>
            <p className="text-sm mb-0">
              Historique des versions acceptées et nouvelles éditions à valider.
            </p>
            <div className="mt-2">
              <span className="badge">Voir mes CGU</span>
            </div>
          </div>
        </Link>

        <Link href="/player/recovery-profile" style={{ textDecoration: "none" }}>
          <div className="card card-interactive" style={{ margin: 0 }}>
            <div className="feature-icon green">&#128737;</div>
            <h3 className="mt-0">Profil de récupération</h3>
            <p className="text-sm mb-0">
              Configurez vos questions secrètes pour la récupération de compte.
            </p>
            <div className="mt-2">
              <span className="badge">Configurer</span>
            </div>
          </div>
        </Link>

        <Link href="/bugs" style={{ textDecoration: "none" }}>
          <div className="card card-interactive" style={{ margin: 0 }}>
            <div className="feature-icon purple">&#128027;</div>
            <h3 className="mt-0">Signaler un bug</h3>
            <p className="text-sm mb-0">
              Contribuez à l&apos;amélioration du jeu et gagnez des exploits.
            </p>
            <div className="mt-2">
              <span className="badge">Signaler</span>
            </div>
          </div>
        </Link>
      </div>

      {/* Upcoming features */}
      <h2>Fonctionnalités à venir</h2>
      <div className="card">
        <div style={{ display: "grid", gap: "0.75rem" }}>
          {[
            { icon: "\uD83D\uDDA5\uFE0F", label: "Serveurs en ligne", desc: "État temps réel des serveurs de jeu" },
            { icon: "\uD83D\uDC64", label: "Personnages", desc: "Gestion et suppression avec double validation" },
            { icon: "\uD83D\uDC65", label: "Amis & Guilde", desc: "Liste d'amis et membres de guilde en ligne" },
            { icon: "\uD83D\uDD14", label: "Notifications", desc: "Alertes de maintenance et mises à jour" },
          ].map((f) => (
            <div
              key={f.label}
              style={{
                display: "flex",
                alignItems: "center",
                gap: "0.75rem",
                padding: "0.5rem 0",
                borderBottom: "1px solid var(--border-light)",
              }}
            >
              <span style={{ fontSize: "1.25rem", flexShrink: 0 }}>{f.icon}</span>
              <div>
                <div style={{ fontWeight: 600, fontSize: "0.9rem" }}>{f.label}</div>
                <div className="text-sm text-muted">{f.desc}</div>
              </div>
              <span className="badge badge-muted" style={{ marginLeft: "auto", flexShrink: 0 }}>Bientôt</span>
            </div>
          ))}
        </div>
      </div>
    </>
  );
}
