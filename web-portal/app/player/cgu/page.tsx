export default function PlayerCguPage() {
  return (
    <>
      <h1>Mes conditions générales</h1>
      <p>
        Liste des versions acceptées, possibilité de relire le texte, et acceptation des nouvelles
        éditions publiées (aligné sur les tables <code>terms_*</code> et{" "}
        <code>account_terms_acceptances</code>).
      </p>
      <div className="card">
        <p style={{ color: "var(--muted)" }}>À connecter aux API du master / base MySQL (lecture seule pour l’historique).</p>
      </div>
    </>
  );
}
