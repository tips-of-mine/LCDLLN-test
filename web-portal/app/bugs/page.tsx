export default function BugsPage() {
  return (
    <>
      <h1>Signaler un bug</h1>
      <p>
        Les signalements vont dans <code>bug_reports</code> (migration <strong>0008</strong>). Les
        paliers correspondent à des lignes du catalogue global <code>exploits</code> (
        <code>metric_source = &apos;bug_reports&apos;</code>, seuils 5 à 100). Chaque seuil atteint
        débloque un exploit via <code>account_exploit_unlocks</code> — le même système que pour le
        reste des exploits du jeu (exploration, objectifs, etc.).
      </p>
      <div className="card">
        <p style={{ color: "var(--muted)" }}>
          Formulaire à connecter à l’API authentifiée (JWT + compte <code>account_id</code> uniquement —
          jamais d’accès aux données d’un autre joueur).
        </p>
      </div>
    </>
  );
}
