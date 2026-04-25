import { LoginForm } from "@/components/LoginForm";

export default function LoginPage({
  searchParams,
}: {
  searchParams: { next?: string };
}) {
  const nextPath =
    typeof searchParams.next === "string" && searchParams.next.startsWith("/")
      ? searchParams.next
      : "/player";

  return (
    <div
      className="wp-main narrow"
      style={{ display: "flex", flexDirection: "column", alignItems: "center", paddingTop: 48 }}
    >
      <div style={{ textAlign: "center", marginBottom: 32 }}>
        <div
          style={{
            width: 64,
            height: 64,
            borderRadius: "50%",
            background:
              "radial-gradient(circle at 32% 30%, #2a2330 0%, #0B0712 55%, #000 100%)",
            border: "1px solid #1a1420",
            boxShadow: "0 0 30px rgba(74,123,184,.4)",
            margin: "0 auto 18px",
          }}
        />
        <div
          style={{
            fontFamily: "var(--font-display)",
            fontWeight: 700,
            fontSize: 20,
            letterSpacing: ".2em",
            textTransform: "uppercase",
            color: "var(--ln-text)",
            marginBottom: 6,
          }}
        >
          Connexion
        </div>
        <div
          style={{
            fontFamily: "var(--font-body)",
            fontStyle: "italic",
            fontSize: 14,
            color: "var(--ln-muted)",
          }}
        >
          Accédez à votre espace joueur
        </div>
      </div>

      <LoginForm nextPath={nextPath} />

      <p
        style={{
          fontFamily: "var(--font-body)",
          fontStyle: "italic",
          fontSize: 12.5,
          color: "var(--ln-muted)",
          textAlign: "center",
          maxWidth: 400,
          marginTop: 20,
          lineHeight: 1.6,
        }}
      >
        Pas encore de compte ? Créez-le dans le client jeu. Le même identifiant
        et mot de passe que pour le jeu fonctionnent ici.
      </p>
    </div>
  );
}
