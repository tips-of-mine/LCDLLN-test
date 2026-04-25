import Link from "next/link";

export default function PlayerHomePage() {
  return (
    <div className="wp-main">
      <div className="wp-page-header">
        <h1>Espace joueur</h1>
        <p>
          Gérez votre profil, suivez vos exploits et consultez vos informations de compte.
          Contenu accessible après connexion.
        </p>
      </div>

      <div className="wp-stats">
        <div className="wp-stat">
          <div className="wp-stat-value">—</div>
          <div className="wp-stat-label">Exploits débloqués</div>
        </div>
        <div className="wp-stat">
          <div className="wp-stat-value">—</div>
          <div className="wp-stat-label">Personnages</div>
        </div>
        <div className="wp-stat">
          <div className="wp-stat-value">—</div>
          <div className="wp-stat-label">Bugs signalés</div>
        </div>
      </div>

      <div className="wp-section-title">Mes sections</div>

      <div className="wp-grid wp-grid-2">
        <Link href="/player/exploits" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>🏆</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Mes Exploits</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Consultez votre progression, exploits visibles et secrets.
            </p>
            <span className="wp-badge active">Voir mes exploits</span>
          </div>
        </Link>

        <Link href="/player/cgu" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>📜</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Mes CGU</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Historique des versions acceptées et nouvelles éditions à valider.
            </p>
            <span className="wp-badge active">Voir mes CGU</span>
          </div>
        </Link>

        <Link href="/player/recovery-profile" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>🛡️</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Profil de récupération</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Configurez vos questions secrètes pour la récupération de compte.
            </p>
            <span className="wp-badge active">Configurer</span>
          </div>
        </Link>

        <Link href="/bugs" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>🐛</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Signaler un bug</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Contribuez à l&apos;amélioration du jeu et gagnez des exploits.
            </p>
            <span className="wp-badge active">Signaler</span>
          </div>
        </Link>
      </div>

      <div className="wp-section-title" style={{ marginTop: "2rem" }}>Fonctionnalités à venir</div>

      <div className="wp-card">
        <div style={{ display: "grid", gap: "0.75rem" }}>
          {[
            { icon: "🖥️", label: "Serveurs en ligne", desc: "État temps réel des serveurs de jeu" },
            { icon: "👤", label: "Personnages", desc: "Gestion et suppression avec double validation" },
            { icon: "👥", label: "Amis & Guilde", desc: "Liste d'amis et membres de guilde en ligne" },
            { icon: "🔔", label: "Notifications", desc: "Alertes de maintenance et mises à jour" },
          ].map((f) => (
            <div
              key={f.label}
              style={{
                display: "flex",
                alignItems: "center",
                gap: "0.75rem",
                padding: "0.5rem 0",
                borderBottom: "1px solid var(--ln-border)",
              }}
            >
              <span style={{ fontSize: "1.25rem", flexShrink: 0 }}>{f.icon}</span>
              <div>
                <div style={{ fontWeight: 600, fontSize: "0.9rem", color: "var(--ln-text)" }}>{f.label}</div>
                <div style={{ fontSize: 13, color: "var(--ln-muted)" }}>{f.desc}</div>
              </div>
              <span className="wp-badge planned" style={{ marginLeft: "auto", flexShrink: 0 }}>Bientôt</span>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
