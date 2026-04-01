export default function LoginPage() {
  return (
    <>
      <h1>Connexion</h1>
      <p>
        Authentification à brancher sur le même modèle de comptes que le master (JWT ou flux OAuth2 —
        à définir). Aucune donnée d’un autre joueur n’est exposée ici.
      </p>
      <div className="card">
        <p style={{ color: "var(--muted)" }}>Formulaire placeholder — implémenter auth sécurisée.</p>
        <p>
          Mot de passe oublié : <a href="/password-recovery">ouvrir la récupération web</a>.
        </p>
        <p>
          Préparation du profil de récupération en dev :{" "}
          <a href="/player/recovery-profile?accountId=1">/player/recovery-profile?accountId=1</a>
        </p>
      </div>
    </>
  );
}
