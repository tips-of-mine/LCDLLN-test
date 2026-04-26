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

      <div className="wp-section-title" style={{ marginTop: "2rem" }}>Mon compte & sécurité</div>

      <div className="wp-grid wp-grid-2">
        <Link href="/player/account" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>👤</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Détail du compte</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Consultez et modifiez vos informations personnelles et vos préférences.
            </p>
            <span className="wp-badge active">Accéder</span>
          </div>
        </Link>

        <Link href="/player/security" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>🔐</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Sécurité du compte</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Changez votre mot de passe, authentification deux facteurs et sessions actives.
            </p>
            <span className="wp-badge active">Configurer</span>
          </div>
        </Link>

        <Link href="/player/privacy" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>🛡️</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Vie Privée</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Gérez vos données personnelles, téléchargements et suppressions de compte.
            </p>
            <span className="wp-badge active">Accéder</span>
          </div>
        </Link>

        <Link href="/player/parental" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>👨‍👩‍👧</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Contrôle Parental</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Configurez les restrictions et autorisations pour les mineurs.
            </p>
            <span className="wp-badge active">Configurer</span>
          </div>
        </Link>
      </div>

      <div className="wp-section-title" style={{ marginTop: "2rem" }}>Mes contenus</div>

      <div className="wp-grid wp-grid-2">
        <Link href="/player/chronicles" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>📖</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Mes Chroniques</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Consultez et gérez vos chroniques, vos brouillons et vos publications.
            </p>
            <span className="wp-badge active">Voir mes chroniques</span>
          </div>
        </Link>
      </div>
    </div>
  );
}
