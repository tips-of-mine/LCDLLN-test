export default function HomePage() {
  return (
    <>
      <h1>LCDLLN</h1>
      <p className="card" style={{ color: "var(--fg)" }}>
        Bienvenue sur le portail officiel. Ici : présentation du projet, avancement, feuille de route,
        contact et support. Connectez-vous pour gérer votre profil, vos personnages, vos CGU et suivre
        l’état des serveurs.
      </p>
      <h2>Avancement (aperçu)</h2>
      <p>
        Le moteur et les systèmes core évoluent par milestones ; consultez la{" "}
        <a href="/roadmap">roadmap</a> pour les prochaines étapes prévues.
      </p>
      <h2>Accès rapide</h2>
      <ul style={{ color: "var(--muted)" }}>
        <li>
          <a href="/login">Connexion joueur</a>
        </li>
        <li>
          <a href="/support">Support &amp; FAQ</a>
        </li>
        <li>
          <a href="/bugs">Signaler un bug</a> (alimente aussi votre progression « exploit » en jeu,
          voir tables <code>exploits</code> / <code>account_exploit_unlocks</code> — migration 0008)
        </li>
      </ul>
    </>
  );
}
