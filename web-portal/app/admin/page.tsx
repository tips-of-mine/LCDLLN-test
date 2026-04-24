import Link from "next/link";

export default function AdminHomePage() {
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

        <div className="wp-card" style={{ opacity: 0.6 }}>
          <div style={{ fontSize: 28, marginBottom: 8 }}>👤</div>
          <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Profils joueurs</h3>
          <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
            Consultation en lecture seule des profils joueurs.
            Aucune modification depuis cet écran.
          </p>
          <span className="wp-badge planned">Bientôt</span>
        </div>

        <div className="wp-card" style={{ opacity: 0.6 }}>
          <div style={{ fontSize: 28, marginBottom: 8 }}>📊</div>
          <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Statistiques</h3>
          <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
            Tableaux de bord avec métriques d&apos;utilisation,
            taux d&apos;acceptation CGU et activité globale.
          </p>
          <span className="wp-badge planned">Bientôt</span>
        </div>
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
