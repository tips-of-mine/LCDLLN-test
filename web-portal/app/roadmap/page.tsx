export default function RoadmapPage() {
  return (
    <>
      <div className="page-header">
        <h1>Roadmap</h1>
        <p>
          Feuille de route indicative du projet. Les priorités peuvent évoluer
          en fonction des retours de la communauté.
        </p>
      </div>

      {/* Progress overview */}
      <div className="stats-row">
        <div className="stat-item">
          <div className="stat-value">4</div>
          <div className="stat-label">Complétés</div>
        </div>
        <div className="stat-item">
          <div className="stat-value">3</div>
          <div className="stat-label">En cours</div>
        </div>
        <div className="stat-item">
          <div className="stat-value">5</div>
          <div className="stat-label">Planifiés</div>
        </div>
      </div>

      {/* Timeline */}
      <h2>Historique &amp; prochaines étapes</h2>
      <div className="timeline">
        <div className="timeline-item done">
          <div className="card" style={{ margin: 0 }}>
            <div className="flex items-center justify-between flex-wrap gap-1">
              <strong>Infrastructure de base</strong>
              <span className="badge badge-success">Terminé</span>
            </div>
            <p className="text-sm mt-1">
              Mise en place du serveur master, base MySQL, Docker et pipeline CI/CD.
            </p>
          </div>
        </div>

        <div className="timeline-item done">
          <div className="card" style={{ margin: 0 }}>
            <div className="flex items-center justify-between flex-wrap gap-1">
              <strong>Système d&apos;exploits</strong>
              <span className="badge badge-success">Terminé</span>
            </div>
            <p className="text-sm mt-1">
              Catalogue global, déblocage par compte/personnage, statistiques globales,
              paliers bug reports.
            </p>
          </div>
        </div>

        <div className="timeline-item done">
          <div className="card" style={{ margin: 0 }}>
            <div className="flex items-center justify-between flex-wrap gap-1">
              <strong>Récupération de mot de passe</strong>
              <span className="badge badge-success">Terminé</span>
            </div>
            <p className="text-sm mt-1">
              Profil de récupération, questions secrètes, tokens temporaires,
              envoi d&apos;e-mail via SMTP.
            </p>
          </div>
        </div>

        <div className="timeline-item done">
          <div className="card" style={{ margin: 0 }}>
            <div className="flex items-center justify-between flex-wrap gap-1">
              <strong>Portail web (v1)</strong>
              <span className="badge badge-success">Terminé</span>
            </div>
            <p className="text-sm mt-1">
              Next.js 14, pages publiques, affichage des exploits, administration CGU.
            </p>
          </div>
        </div>

        <div className="timeline-item active">
          <div className="card" style={{ margin: 0, borderColor: "rgba(245, 158, 11, 0.3)" }}>
            <div className="flex items-center justify-between flex-wrap gap-1">
              <strong>Authentification complète</strong>
              <span className="badge badge-warning">En cours</span>
            </div>
            <p className="text-sm mt-1">
              JWT / sessions sécurisées, middleware de protection des routes,
              gestion des rôles (joueur / admin).
            </p>
          </div>
        </div>

        <div className="timeline-item active">
          <div className="card" style={{ margin: 0, borderColor: "rgba(245, 158, 11, 0.3)" }}>
            <div className="flex items-center justify-between flex-wrap gap-1">
              <strong>Refonte UI/UX du portail</strong>
              <span className="badge badge-warning">En cours</span>
            </div>
            <p className="text-sm mt-1">
              Design moderne, navigation responsive, animations, pages enrichies.
            </p>
          </div>
        </div>

        <div className="timeline-item active">
          <div className="card" style={{ margin: 0, borderColor: "rgba(245, 158, 11, 0.3)" }}>
            <div className="flex items-center justify-between flex-wrap gap-1">
              <strong>API CGU (CRUD)</strong>
              <span className="badge badge-warning">En cours</span>
            </div>
            <p className="text-sm mt-1">
              Endpoints pour créer, modifier, publier et retirer les CGU ;
              suivi des acceptations par compte.
            </p>
          </div>
        </div>

        <div className="timeline-item">
          <div className="card" style={{ margin: 0 }}>
            <div className="flex items-center justify-between flex-wrap gap-1">
              <strong>Formulaire de bug reports</strong>
              <span className="badge badge-muted">Planifié</span>
            </div>
            <p className="text-sm mt-1">
              Soumission authentifiée, catégorisation, pièces jointes,
              déblocage automatique des exploits palier.
            </p>
          </div>
        </div>

        <div className="timeline-item">
          <div className="card" style={{ margin: 0 }}>
            <div className="flex items-center justify-between flex-wrap gap-1">
              <strong>Profil joueur complet</strong>
              <span className="badge badge-muted">Planifié</span>
            </div>
            <p className="text-sm mt-1">
              Personnages, serveurs en ligne, amis &amp; guilde, suppression de compte
              avec double validation.
            </p>
          </div>
        </div>

        <div className="timeline-item">
          <div className="card" style={{ margin: 0 }}>
            <div className="flex items-center justify-between flex-wrap gap-1">
              <strong>Monitoring &amp; état des serveurs</strong>
              <span className="badge badge-muted">Planifié</span>
            </div>
            <p className="text-sm mt-1">
              Dashboard temps réel affichant l&apos;état des serveurs de jeu,
              le nombre de joueurs connectés et les événements planifiés.
            </p>
          </div>
        </div>

        <div className="timeline-item">
          <div className="card" style={{ margin: 0 }}>
            <div className="flex items-center justify-between flex-wrap gap-1">
              <strong>Système de tickets support</strong>
              <span className="badge badge-muted">Planifié</span>
            </div>
            <p className="text-sm mt-1">
              Ouverture de tickets, suivi, réponse admin, historique par compte.
            </p>
          </div>
        </div>

        <div className="timeline-item">
          <div className="card" style={{ margin: 0 }}>
            <div className="flex items-center justify-between flex-wrap gap-1">
              <strong>Notifications &amp; annonces</strong>
              <span className="badge badge-muted">Planifié</span>
            </div>
            <p className="text-sm mt-1">
              Annonces globales, notifications par compte, alertes de maintenance
              et de mise à jour.
            </p>
          </div>
        </div>
      </div>
    </>
  );
}
