import Link from "next/link";
import { getSession } from "@/lib/session";

export default async function HomePage() {
  const session = await getSession();

  return (
    <div className="wp-main">
      <div className="wp-hero">
        <h1>Les Chroniques De La Lune Noire</h1>
        <p>
          Portail officiel du projet. Suivez l&apos;avancement, consultez la feuille de route,
          gérez votre profil et suivez vos exploits en jeu.
        </p>
        <div style={{ display: "flex", gap: 12, flexWrap: "wrap", justifyContent: "center" }}>
          {session ? (
            <Link href="/player" className="btn btn-accent">
              Mon espace joueur
            </Link>
          ) : (
            <Link href="/login" className="btn btn-accent">
              Se connecter
            </Link>
          )}
          <Link href="/roadmap" className="btn btn-ghost">
            Voir la roadmap
          </Link>
        </div>
      </div>

      <div className="wp-stats">
        <div className="wp-stat">
          <div className="wp-stat-value">20</div>
          <div className="wp-stat-label">Paliers d&apos;exploits</div>
        </div>
        <div className="wp-stat">
          <div className="wp-stat-value">3</div>
          <div className="wp-stat-label">Modules actifs</div>
        </div>
        <div className="wp-stat">
          <div className="wp-stat-value">100%</div>
          <div className="wp-stat-label">Open source</div>
        </div>
        <div className="wp-stat">
          <div className="wp-stat-value">24/7</div>
          <div className="wp-stat-label">Portail en ligne</div>
        </div>
      </div>

      <div className="wp-section-title">Fonctionnalités du portail</div>
      <div className="wp-section-sub">
        Tout ce dont vous avez besoin pour gérer votre expérience de jeu.
      </div>

      <div className="wp-grid wp-grid-2">
        <Link href="/player/exploits" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>🏆</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Exploits &amp; Succès</h3>
            <p style={{ fontSize: 14, margin: 0, color: "var(--ln-muted)" }}>
              Suivez votre progression, débloquez des succès secrets et comparez-vous
              aux autres joueurs.
            </p>
          </div>
        </Link>

        <Link href="/password-recovery" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>🔒</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Récupération sécurisée</h3>
            <p style={{ fontSize: 14, margin: 0, color: "var(--ln-muted)" }}>
              Système de récupération de mot de passe par questions secrètes
              et token temporaire.
            </p>
          </div>
        </Link>

        <Link href="/bugs" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>🐛</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Signalement de bugs</h3>
            <p style={{ fontSize: 14, margin: 0, color: "var(--ln-muted)" }}>
              Signalez des bugs et gagnez des exploits en jeu à chaque palier
              de signalements atteint.
            </p>
          </div>
        </Link>

        <Link href="/player/cgu" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>📜</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>CGU &amp; Conformité</h3>
            <p style={{ fontSize: 14, margin: 0, color: "var(--ln-muted)" }}>
              Consultez et acceptez les conditions générales depuis le portail
              pour accélérer l&apos;accès au jeu.
            </p>
          </div>
        </Link>
      </div>

      <div className="wp-section-title" style={{ marginTop: "2rem" }}>Accès rapide</div>

      <div className="wp-grid wp-grid-3">
        <Link href="/support" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive" style={{ textAlign: "center", padding: "1.5rem 1rem" }}>
            <div style={{ fontSize: "1.5rem", marginBottom: "0.5rem" }}>💬</div>
            <div style={{ fontWeight: 600, color: "var(--ln-text)", fontSize: "0.9rem", fontFamily: "var(--font-display)" }}>Support &amp; FAQ</div>
          </div>
        </Link>
        <Link href="/contact" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive" style={{ textAlign: "center", padding: "1.5rem 1rem" }}>
            <div style={{ fontSize: "1.5rem", marginBottom: "0.5rem" }}>✉️</div>
            <div style={{ fontWeight: 600, color: "var(--ln-text)", fontSize: "0.9rem", fontFamily: "var(--font-display)" }}>Contact</div>
          </div>
        </Link>
        <Link href="/player" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive" style={{ textAlign: "center", padding: "1.5rem 1rem" }}>
            <div style={{ fontSize: "1.5rem", marginBottom: "0.5rem" }}>👤</div>
            <div style={{ fontWeight: 600, color: "var(--ln-text)", fontSize: "0.9rem", fontFamily: "var(--font-display)" }}>Mon espace</div>
          </div>
        </Link>
      </div>
    </div>
  );
}
