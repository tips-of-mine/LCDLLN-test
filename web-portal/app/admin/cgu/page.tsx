export default function AdminCguPage() {
  return (
    <>
      <h1>Gestion des CGU</h1>
      <p>
        Édition des contenus multilingues, statuts brouillon / publié / retiré, dates de publication.
        Les données vivent en base (<code>terms_editions</code>, <code>terms_localizations</code>) —
        ce portail n’est qu’une interface ; le jeu n’a pas besoin d’être recompilé pour publier une
        nouvelle version.
      </p>
      <div className="card">
        <p style={{ color: "var(--muted)" }}>UI admin à brancher sur l’API (rôle admin).</p>
      </div>
    </>
  );
}
