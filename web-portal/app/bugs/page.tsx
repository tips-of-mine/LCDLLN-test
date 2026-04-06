import Link from "next/link";

const tiers = [
  5, 10, 15, 20, 25, 30, 35, 40, 45, 50,
  55, 60, 65, 70, 75, 80, 85, 90, 95, 100,
];

export default function BugsPage() {
  return (
    <>
      <div className="page-header">
        <h1>Signaler un bug</h1>
        <p>
          Chaque signalement contribue à l&apos;amélioration du jeu et vous rapproche
          d&apos;un nouvel exploit. Connectez-vous pour soumettre un rapport.
        </p>
      </div>

      {/* How it works */}
      <div className="card-grid">
        <div className="card" style={{ margin: 0 }}>
          <div className="feature-icon">&#128270;</div>
          <h3 className="mt-0">1. Identifier</h3>
          <p className="text-sm mb-0">
            Rencontrez un comportement inattendu ? Notez les étapes pour le reproduire.
          </p>
        </div>
        <div className="card" style={{ margin: 0 }}>
          <div className="feature-icon green">&#128221;</div>
          <h3 className="mt-0">2. Signaler</h3>
          <p className="text-sm mb-0">
            Remplissez le formulaire avec un titre clair, une description détaillée
            et les étapes de reproduction.
          </p>
        </div>
        <div className="card" style={{ margin: 0 }}>
          <div className="feature-icon purple">&#127942;</div>
          <h3 className="mt-0">3. Progresser</h3>
          <p className="text-sm mb-0">
            Chaque palier de signalements atteint débloque un exploit exclusif
            sur votre compte.
          </p>
        </div>
      </div>

      {/* Bug report form placeholder */}
      <h2>Soumettre un rapport</h2>
      <div className="card">
        <div style={{ textAlign: "center", padding: "1.5rem 0" }}>
          <div style={{ fontSize: "2.5rem", marginBottom: "0.75rem", opacity: 0.5 }}>&#128274;</div>
          <p style={{ color: "var(--fg)", fontWeight: 600, marginBottom: "0.5rem" }}>
            Connexion requise
          </p>
          <p className="text-sm mb-0">
            Vous devez être connecté pour soumettre un rapport de bug.
          </p>
          <div style={{ marginTop: "1rem" }}>
            <Link href="/login" className="btn btn-primary">
              Se connecter
            </Link>
          </div>
        </div>
      </div>

      {/* Tiers */}
      <h2>Paliers d&apos;exploits</h2>
      <p className="section-subtitle">
        Débloquez un exploit à chaque palier atteint. 20 niveaux au total, de 5 à 100 signalements.
      </p>

      <div className="card" style={{ padding: "1rem" }}>
        <div style={{
          display: "grid",
          gridTemplateColumns: "repeat(auto-fill, minmax(5rem, 1fr))",
          gap: "0.5rem",
        }}>
          {tiers.map((t) => (
            <div
              key={t}
              style={{
                textAlign: "center",
                padding: "0.75rem 0.5rem",
                borderRadius: "var(--radius-sm)",
                background: "rgba(29, 155, 240, 0.06)",
                border: "1px solid var(--border)",
              }}
            >
              <div style={{ fontSize: "1.25rem", fontWeight: 800, color: "var(--accent)" }}>
                {t}
              </div>
              <div className="text-xs text-muted">bugs</div>
            </div>
          ))}
        </div>
      </div>

      {/* Technical note */}
      <div className="card" style={{ borderColor: "rgba(29, 155, 240, 0.2)", marginTop: "1.5rem" }}>
        <h3 className="mt-0">Note technique</h3>
        <p className="text-sm mb-0">
          Les signalements sont stockés dans <code>bug_reports</code>. Les paliers correspondent
          à des entrées du catalogue <code>exploits</code> avec <code>metric_source = &apos;bug_reports&apos;</code>.
          Chaque seuil atteint crée automatiquement une entrée dans <code>account_exploit_unlocks</code>,
          le même système que pour tous les autres exploits du jeu.
        </p>
      </div>
    </>
  );
}
