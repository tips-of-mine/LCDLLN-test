import Link from "next/link";
import { redirect } from "next/navigation";
import { getSession } from "@/lib/session";

export default async function AdminHomePage() {
  const session = await getSession();
  if (!session) redirect("/login?redirect=/admin");
  if (session.role !== "admin") redirect("/");
  return (
    <div className="wp-main">
      <div className="wp-page-header">
        <h1>Administration</h1>
        <p>
          Panneau d&apos;administration réservé aux comptes autorisés.
          Gestion des CGU, suivi des acceptations et consultation des profils joueurs en lecture seule.
        </p>
      </div>

      <div className="wp-stats">
        <div className="wp-stat">
          <div className="wp-stat-value">—</div>
          <div className="wp-stat-label">Comptes actifs</div>
        </div>
        <div className="wp-stat">
          <div className="wp-stat-value">—</div>
          <div className="wp-stat-label">CGU publiées</div>
        </div>
        <div className="wp-stat">
          <div className="wp-stat-value">—</div>
          <div className="wp-stat-label">Bugs signalés</div>
        </div>
        <div className="wp-stat">
          <div className="wp-stat-value">—</div>
          <div className="wp-stat-label">Acceptations CGU</div>
        </div>
      </div>

      <div className="wp-section-title">Modules d&apos;administration</div>

      <div className="wp-grid wp-grid-2">
        <Link href="/admin/cgu" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>📜</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Gestion des CGU</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Créer, modifier, publier et retirer les conditions générales.
              Gestion multilingue et versionnée.
            </p>
            <span className="wp-badge active">CRUD</span>
          </div>
        </Link>

        <Link href="/admin/acceptances" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>✅</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Suivi des acceptations</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Visualiser quels joueurs ont accepté quelles versions des CGU,
              avec dates et historique complet.
            </p>
            <span className="wp-badge done">Lecture seule</span>
          </div>
        </Link>

        <Link href="/admin/players" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>👥</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Gestion des Joueurs</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Liste paginée, validation email, activation / désactivation de comptes
              et gestion des personnages avec renommage forcé.
            </p>
            <span className="wp-badge active">Actions</span>
          </div>
        </Link>

        <Link href="/admin/roadmap" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>🗺️</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Roadmap</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Créer, modifier et publier la feuille de route des développements.
              Gestion des priorités et des versions.
            </p>
            <span className="wp-badge active">CRUD</span>
          </div>
        </Link>

        <Link href="/admin/faq" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>❓</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>FAQ & Support</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Gérer les questions fréquemment posées et les articles de support.
              Multilingue et versionnée.
            </p>
            <span className="wp-badge active">CRUD</span>
          </div>
        </Link>

        <Link href="/admin/bugs" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>🐛</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Suivi des Bugs</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Suivi et gestion des bugs signalés par les joueurs.
              États, priorités et résolution.
            </p>
            <span className="wp-badge active">CRUD</span>
          </div>
        </Link>
      </div>

      <div className="wp-card" style={{ borderColor: "rgba(232,165,92,.35)", marginTop: "1.5rem" }}>
        <div style={{ display: "flex", alignItems: "center", gap: 8, marginBottom: 8 }}>
          <span style={{ fontSize: "1.1rem" }}>⚠️</span>
          <h3 style={{ margin: 0, fontFamily: "var(--font-display)", color: "var(--ln-warning)" }}>Sécurité</h3>
        </div>
        <p style={{ margin: 0, fontSize: 14, color: "var(--ln-muted)" }}>
          L&apos;accès à ce panneau est restreint aux comptes avec le rôle administrateur.
          Toutes les actions sont journalisées. Les profils joueurs sont en lecture seule :
          aucune modification arbitraire n&apos;est possible depuis cet écran (spécification).
        </p>
      </div>
    </div>
  );
}
