import Link from "next/link";
import type { SessionPayload } from "@/lib/session";
import { NavToggle } from "./NavToggle";

export function SiteHeader({ session }: { session: SessionPayload | null }) {
  return (
    <header className="wp-header">
      <Link href="/" className="wp-logo">
        <div className="wp-logo-moon" />
        <span className="wp-logo-text">Les Chroniques de la Lune Noire</span>
      </Link>

      <NavToggle>
        <Link href="/roadmap">Roadmap</Link>
        <Link href="/support">Support</Link>
        <Link href="/bugs">Signaler un bug</Link>

        {session !== null && (
          <Link href="/player">Espace joueur</Link>
        )}

        {(session?.role === "admin" || session?.role === "moderator") && (
          <Link href="/admin">Admin</Link>
        )}

        {session !== null ? (
          <>
            <span
              style={{
                fontFamily: "var(--font-display)",
                fontSize: 11,
                letterSpacing: ".2em",
                textTransform: "uppercase",
                color: "var(--ln-accent)",
                padding: "0 0.5rem",
                cursor: "default",
                userSelect: "none",
              }}
              title={`Connecté en tant que ${session.login}`}
            >
              {session.tagId || session.login}
            </span>
            <form action="/api/auth/logout" method="POST" style={{ display: "contents" }}>
              <button
                type="submit"
                className="btn btn-ghost"
                style={{ fontSize: 11, letterSpacing: ".15em", textTransform: "uppercase" }}
              >
                Déconnexion
              </button>
            </form>
          </>
        ) : (
          <Link href="/login" className="cta">Connexion</Link>
        )}
      </NavToggle>
    </header>
  );
}
