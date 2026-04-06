import Link from "next/link";

export default function HomePage() {
  return (
    <>
      {/* Hero */}
      <section className="hero">
        <h1>LCDLLN</h1>
        <p>
          Portail officiel du projet. Suivez l&apos;avancement, consultez la feuille de route,
          gérez votre profil et suivez vos exploits en jeu.
        </p>
        <div className="hero-actions">
          <Link href="/login" className="btn btn-primary">
            Se connecter
          </Link>
          <Link href="/roadmap" className="btn btn-secondary">
            Voir la roadmap
          </Link>
        </div>
      </section>

      {/* Stats */}
      <div className="stats-row">
        <div className="stat-item">
          <div className="stat-value">20</div>
          <div className="stat-label">Paliers d&apos;exploits</div>
        </div>
        <div className="stat-item">
          <div className="stat-value">3</div>
          <div className="stat-label">Modules actifs</div>
        </div>
        <div className="stat-item">
          <div className="stat-value">100%</div>
          <div className="stat-label">Open source</div>
        </div>
        <div className="stat-item">
          <div className="stat-value">24/7</div>
          <div className="stat-label">Portail en ligne</div>
        </div>
      </div>

      {/* Features */}
      <h2>Fonctionnalités du portail</h2>
      <p className="section-subtitle">
        Tout ce dont vous avez besoin pour gérer votre expérience de jeu.
      </p>

      <div className="card-grid">
        <Link href="/player/exploits" style={{ textDecoration: "none" }}>
          <div className="card card-interactive">
            <div className="feature-icon">&#127942;</div>
            <h3 className="mt-0">Exploits &amp; Succès</h3>
            <p className="text-sm">
              Suivez votre progression, débloquez des succès secrets et comparez-vous
              aux autres joueurs.
            </p>
          </div>
        </Link>

        <Link href="/password-recovery" style={{ textDecoration: "none" }}>
          <div className="card card-interactive">
            <div className="feature-icon green">&#128274;</div>
            <h3 className="mt-0">Récupération sécurisée</h3>
            <p className="text-sm">
              Système de récupération de mot de passe par questions secrètes
              et token temporaire.
            </p>
          </div>
        </Link>

        <Link href="/bugs" style={{ textDecoration: "none" }}>
          <div className="card card-interactive">
            <div className="feature-icon purple">&#128027;</div>
            <h3 className="mt-0">Signalement de bugs</h3>
            <p className="text-sm">
              Signalez des bugs et gagnez des exploits en jeu à chaque palier
              de signalements atteint.
            </p>
          </div>
        </Link>

        <Link href="/player/cgu" style={{ textDecoration: "none" }}>
          <div className="card card-interactive">
            <div className="feature-icon orange">&#128220;</div>
            <h3 className="mt-0">CGU &amp; Conformité</h3>
            <p className="text-sm">
              Consultez et acceptez les conditions générales depuis le portail
              pour accélérer l&apos;accès au jeu.
            </p>
          </div>
        </Link>
      </div>

      {/* Quick access */}
      <h2>Accès rapide</h2>
      <div className="card-grid-3">
        <Link href="/support" style={{ textDecoration: "none" }}>
          <div className="card card-interactive" style={{ textAlign: "center", padding: "1.5rem 1rem" }}>
            <div style={{ fontSize: "1.5rem", marginBottom: "0.5rem" }}>&#128172;</div>
            <div style={{ fontWeight: 600, color: "var(--fg)", fontSize: "0.9rem" }}>Support &amp; FAQ</div>
          </div>
        </Link>
        <Link href="/contact" style={{ textDecoration: "none" }}>
          <div className="card card-interactive" style={{ textAlign: "center", padding: "1.5rem 1rem" }}>
            <div style={{ fontSize: "1.5rem", marginBottom: "0.5rem" }}>&#9993;</div>
            <div style={{ fontWeight: 600, color: "var(--fg)", fontSize: "0.9rem" }}>Contact</div>
          </div>
        </Link>
        <Link href="/player" style={{ textDecoration: "none" }}>
          <div className="card card-interactive" style={{ textAlign: "center", padding: "1.5rem 1rem" }}>
            <div style={{ fontSize: "1.5rem", marginBottom: "0.5rem" }}>&#128100;</div>
            <div style={{ fontWeight: 600, color: "var(--fg)", fontSize: "0.9rem" }}>Mon espace</div>
          </div>
        </Link>
      </div>
    </>
  );
}
