import Link from "next/link";

const tiers = [5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 100];

export default function BugsPage() {
  return (
    <div className="wp-main narrow">
      <div className="wp-page-header">
        <h1>Signaler un bug</h1>
        <p>
          Chaque signalement contribue à l&apos;amélioration du jeu
          et vous rapproche d&apos;un nouvel exploit.
        </p>
      </div>

      <div className="wp-grid wp-grid-3">
        {[
          { icon: "🔍", title: "1. Identifier", desc: "Notez les étapes pour reproduire le comportement inattendu." },
          { icon: "📝", title: "2. Décrire",    desc: "Titre clair, description détaillée, étapes de reproduction." },
          { icon: "🏆", title: "3. Progresser", desc: "Chaque palier atteint débloque un exploit exclusif." },
        ].map((s) => (
          <div key={s.title} className="wp-card">
            <div className="wp-card-icon">{s.icon}</div>
            <h3>{s.title}</h3>
            <p>{s.desc}</p>
          </div>
        ))}
      </div>

      <p className="wp-section-title">Soumettre un rapport</p>
      <div className="wp-card" style={{ textAlign: "center", padding: 32 }}>
        <div style={{ fontSize: 36, marginBottom: 16, opacity: 0.5 }}>🔒</div>
        <div style={{ fontFamily: "var(--font-display)", fontSize: 13, letterSpacing: ".18em", textTransform: "uppercase", color: "var(--ln-text)", marginBottom: 8 }}>
          Connexion requise
        </div>
        <p style={{ fontFamily: "var(--font-body)", fontStyle: "italic", fontSize: 13.5, color: "var(--ln-muted)", marginBottom: 20 }}>
          Vous devez être connecté pour soumettre un rapport de bug.
        </p>
        <Link href="/login" className="btn btn-primary">Se connecter</Link>
      </div>

      <div className="wp-divider" />

      <p className="wp-section-title">Paliers d&apos;exploits</p>
      <p className="wp-section-sub">20 niveaux — de 5 à 100 signalements. Un exploit exclusif à chaque palier.</p>
      <div className="wp-tiers" style={{ marginBottom: 28 }}>
        {tiers.map((t) => (
          <div key={t} className="wp-tier">
            <div className="wp-tier-num">{t}</div>
            <div className="wp-tier-label">bugs</div>
          </div>
        ))}
      </div>

      <div className="wp-card" style={{ borderColor: "rgba(74,123,184,.3)" }}>
        <h3 style={{ marginBottom: 8 }}>Note technique</h3>
        <p>
          Les signalements sont stockés dans <code>bug_reports</code>. Les paliers
          correspondent à des entrées du catalogue <code>exploits</code> avec{" "}
          <code>metric_source = &apos;bug_reports&apos;</code>. Chaque seuil atteint crée
          automatiquement une entrée dans <code>account_exploit_unlocks</code>.
        </p>
      </div>
    </div>
  );
}
