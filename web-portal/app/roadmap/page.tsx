export default function RoadmapPage() {
  return (
    <div className="wp-main">
      <div className="wp-page-header">
        <h1>Roadmap</h1>
        <p>
          Feuille de route indicative du projet. Les priorités peuvent évoluer
          en fonction des retours de la communauté.
        </p>
      </div>

      <div className="wp-stats">
        <div className="wp-stat">
          <div className="wp-stat-value">4</div>
          <div className="wp-stat-label">Complétés</div>
        </div>
        <div className="wp-stat">
          <div className="wp-stat-value">3</div>
          <div className="wp-stat-label">En cours</div>
        </div>
        <div className="wp-stat">
          <div className="wp-stat-value">5</div>
          <div className="wp-stat-label">Planifiés</div>
        </div>
      </div>

      <div className="wp-section-title" style={{ marginTop: "2rem" }}>Historique &amp; prochaines étapes</div>

      <div className="wp-timeline">
        <div className="wp-tl-item done">
          <div className="wp-card">
            <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", flexWrap: "wrap", gap: 8 }}>
              <strong style={{ fontFamily: "var(--font-display)" }}>Infrastructure de base</strong>
              <span className="wp-badge done">Terminé</span>
            </div>
            <p style={{ fontSize: 13, margin: "8px 0 0", color: "var(--ln-muted)" }}>
              Mise en place du serveur master, base MySQL, Docker et pipeline CI/CD.
            </p>
          </div>
        </div>

        <div className="wp-tl-item done">
          <div className="wp-card">
            <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", flexWrap: "wrap", gap: 8 }}>
              <strong style={{ fontFamily: "var(--font-display)" }}>Système d&apos;exploits</strong>
              <span className="wp-badge done">Terminé</span>
            </div>
            <p style={{ fontSize: 13, margin: "8px 0 0", color: "var(--ln-muted)" }}>
              Catalogue global, déblocage par compte/personnage, statistiques globales,
              paliers bug reports.
            </p>
          </div>
        </div>

        <div className="wp-tl-item done">
          <div className="wp-card">
            <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", flexWrap: "wrap", gap: 8 }}>
              <strong style={{ fontFamily: "var(--font-display)" }}>Récupération de mot de passe</strong>
              <span className="wp-badge done">Terminé</span>
            </div>
            <p style={{ fontSize: 13, margin: "8px 0 0", color: "var(--ln-muted)" }}>
              Profil de récupération, questions secrètes, tokens temporaires,
              envoi d&apos;e-mail via SMTP.
            </p>
          </div>
        </div>

        <div className="wp-tl-item done">
          <div className="wp-card">
            <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", flexWrap: "wrap", gap: 8 }}>
              <strong style={{ fontFamily: "var(--font-display)" }}>Portail web (v1)</strong>
              <span className="wp-badge done">Terminé</span>
            </div>
            <p style={{ fontSize: 13, margin: "8px 0 0", color: "var(--ln-muted)" }}>
              Next.js 14, pages publiques, affichage des exploits, administration CGU.
            </p>
          </div>
        </div>

        <div className="wp-tl-item active">
          <div className="wp-card" style={{ borderColor: "rgba(232,165,92,.35)" }}>
            <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", flexWrap: "wrap", gap: 8 }}>
              <strong style={{ fontFamily: "var(--font-display)" }}>Authentification complète</strong>
              <span className="wp-badge active">À faire</span>
            </div>
            <p style={{ fontSize: 13, margin: "8px 0 0", color: "var(--ln-muted)" }}>
              JWT / sessions sécurisées, middleware de protection des routes,
              gestion des rôles (joueur / admin).
            </p>
          </div>
        </div>

        <div className="wp-tl-item active">
          <div className="wp-card" style={{ borderColor: "rgba(232,165,92,.35)" }}>
            <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", flexWrap: "wrap", gap: 8 }}>
              <strong style={{ fontFamily: "var(--font-display)" }}>Refonte UI/UX du portail</strong>
              <span className="wp-badge active">À faire</span>
            </div>
            <p style={{ fontSize: 13, margin: "8px 0 0", color: "var(--ln-muted)" }}>
              Design moderne, navigation responsive, animations, pages enrichies.
            </p>
          </div>
        </div>

        <div className="wp-tl-item active">
          <div className="wp-card" style={{ borderColor: "rgba(232,165,92,.35)" }}>
            <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", flexWrap: "wrap", gap: 8 }}>
              <strong style={{ fontFamily: "var(--font-display)" }}>API CGU (CRUD)</strong>
              <span className="wp-badge active">À faire</span>
            </div>
            <p style={{ fontSize: 13, margin: "8px 0 0", color: "var(--ln-muted)" }}>
              Endpoints pour créer, modifier, publier et retirer les CGU ;
              suivi des acceptations par compte.
            </p>
          </div>
        </div>

        <div className="wp-tl-item">
          <div className="wp-card">
            <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", flexWrap: "wrap", gap: 8 }}>
              <strong style={{ fontFamily: "var(--font-display)" }}>Formulaire de bug reports</strong>
              <span className="wp-badge planned">Planifié</span>
            </div>
            <p style={{ fontSize: 13, margin: "8px 0 0", color: "var(--ln-muted)" }}>
              Soumission authentifiée, catégorisation, pièces jointes,
              déblocage automatique des exploits palier.
            </p>
          </div>
        </div>

        <div className="wp-tl-item">
          <div className="wp-card">
            <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", flexWrap: "wrap", gap: 8 }}>
              <strong style={{ fontFamily: "var(--font-display)" }}>Profil joueur complet</strong>
              <span className="wp-badge planned">Planifié</span>
            </div>
            <p style={{ fontSize: 13, margin: "8px 0 0", color: "var(--ln-muted)" }}>
              Personnages, serveurs en ligne, amis &amp; guilde, suppression de compte
              avec double validation.
            </p>
          </div>
        </div>

        <div className="wp-tl-item">
          <div className="wp-card">
            <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", flexWrap: "wrap", gap: 8 }}>
              <strong style={{ fontFamily: "var(--font-display)" }}>Monitoring &amp; état des serveurs</strong>
              <span className="wp-badge planned">Planifié</span>
            </div>
            <p style={{ fontSize: 13, margin: "8px 0 0", color: "var(--ln-muted)" }}>
              Dashboard temps réel affichant l&apos;état des serveurs de jeu,
              le nombre de joueurs connectés et les événements planifiés.
            </p>
          </div>
        </div>

        <div className="wp-tl-item">
          <div className="wp-card">
            <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", flexWrap: "wrap", gap: 8 }}>
              <strong style={{ fontFamily: "var(--font-display)" }}>Système de tickets support</strong>
              <span className="wp-badge planned">Planifié</span>
            </div>
            <p style={{ fontSize: 13, margin: "8px 0 0", color: "var(--ln-muted)" }}>
              Ouverture de tickets, suivi, réponse admin, historique par compte.
            </p>
          </div>
        </div>

        <div className="wp-tl-item">
          <div className="wp-card">
            <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", flexWrap: "wrap", gap: 8 }}>
              <strong style={{ fontFamily: "var(--font-display)" }}>Notifications &amp; annonces</strong>
              <span className="wp-badge planned">Planifié</span>
            </div>
            <p style={{ fontSize: 13, margin: "8px 0 0", color: "var(--ln-muted)" }}>
              Annonces globales, notifications par compte, alertes de maintenance
              et de mise à jour.
            </p>
          </div>
        </div>
      </div>
    </div>
  );
}
