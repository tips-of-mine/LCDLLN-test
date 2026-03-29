export default function RoadmapPage() {
  return (
    <>
      <h1>Roadmap</h1>
      <p>
        Liste indicative à maintenir côté équipe (contenu éditorial — pas codé en dur dans le moteur).
      </p>
      <div className="card">
        <span className="badge">À venir</span>
        <ul style={{ marginTop: "0.75rem", color: "var(--muted)" }}>
          <li>Continuité des milestones moteur / gameplay</li>
          <li>Portail : auth complète, profil, liste serveurs temps réel</li>
          <li>Intégration CGU (lecture / acceptation) avec le master</li>
        </ul>
      </div>
    </>
  );
}
