export default function AdminHomePage() {
  return (
    <>
      <h1>Administration</h1>
      <p className="card" style={{ color: "var(--fg)" }}>
        Réservé aux comptes administrateurs. Gestion des CGU (CRUD + publication), lecture seule des
        profils joueurs, suivi des acceptations CGU. Aucune modification arbitraire du profil joueur
        depuis cet écran (spécification).
      </p>
      <ul style={{ color: "var(--muted)" }}>
        <li>
          <a href="/admin/cgu">Gestion des CGU</a>
        </li>
        <li>
          <a href="/admin/acceptances">Suivi des acceptations</a>
        </li>
      </ul>
    </>
  );
}
