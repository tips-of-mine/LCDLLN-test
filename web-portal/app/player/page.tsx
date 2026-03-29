export default function PlayerHomePage() {
  return (
    <>
      <h1>Espace joueur</h1>
      <p className="card" style={{ color: "var(--fg)" }}>
        Contenu visible uniquement après connexion (middleware + session). Prévu : profil, serveurs en
        ligne, personnages (suppression avec double validation), amis / guilde en ligne, CGU acceptées
        et nouvelles à valider pour accélérer l’accès au jeu.
      </p>
      <ul style={{ color: "var(--muted)" }}>
        <li>
          <a href="/player/exploits">Mes Exploits</a> (ajouter <code>?accountId=…</code> en dev)
        </li>
        <li>
          <a href="/player/cgu">Mes CGU</a>
        </li>
      </ul>
    </>
  );
}
