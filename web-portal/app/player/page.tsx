// web-portal/app/player/page.tsx
import { cookies } from "next/headers";
import { redirect } from "next/navigation";
import Link from "next/link";
import { readSession } from "@/lib/session";
import { getAccountProfile } from "@/lib/playerProfile";
import { getCharactersWithStats } from "@/lib/playerCharacters";
import { getPlayerExploitsData } from "@/lib/exploitsData";

export const dynamic = "force-dynamic";

export default async function PlayerHomePage() {
  const session = readSession(cookies());
  if (!session) redirect("/login?next=/player");

  const [profile, characters, exploits] = await Promise.all([
    getAccountProfile(session.accountId),
    getCharactersWithStats(session.accountId).catch(() => []),
    getPlayerExploitsData(session.accountId).catch(() => null),
  ]);

  if (!profile) redirect("/login");

  const completedExploits = exploits?.totals.completedByPlayer ?? 0;
  const totalExploits = exploits?.totals.totalInGame ?? 0;

  return (
    <div className="wp-main">
      <div className="wp-page-header">
        <h1>
          Bienvenue,{" "}
          <span style={{ fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>
            {profile.tagId || profile.login}
          </span>
        </h1>
        <p>Gérez votre profil, suivez vos exploits et consultez vos informations de compte.</p>
      </div>

      {profile.emailPending && !profile.emailVerified && (
        <div className="wp-card" style={{ marginBottom: "1rem", borderColor: "rgba(220,180,50,.4)", background: "rgba(220,180,50,.05)" }}>
          <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
            <span style={{ fontSize: "1.1rem" }}>⚠</span>
            <div>
              <div style={{ fontFamily: "var(--font-display)", fontSize: 11, letterSpacing: ".14em", textTransform: "uppercase", color: "var(--ln-warning)", marginBottom: 4 }}>
                Email en attente de validation
              </div>
              <p style={{ margin: 0, fontSize: 13, color: "var(--ln-muted)" }}>
                Un code de vérification a été envoyé à <strong>{profile.emailPending}</strong>.{" "}
                <Link href="/player/account" style={{ color: "var(--ln-accent)" }}>Valider</Link>
              </p>
            </div>
          </div>
        </div>
      )}

      <div className="wp-stats">
        <div className="wp-stat">
          <div className="wp-stat-value">{completedExploits}</div>
          <div className="wp-stat-label">Exploits débloqués</div>
        </div>
        <div className="wp-stat">
          <div className="wp-stat-value">{characters.length}</div>
          <div className="wp-stat-label">Personnages</div>
        </div>
        <div className="wp-stat">
          <div className="wp-stat-value">{totalExploits > 0 ? `${Math.round((completedExploits / totalExploits) * 100)}%` : "—"}</div>
          <div className="wp-stat-label">Progression</div>
        </div>
      </div>

      <div className="wp-section-title">Mon espace</div>

      <div className="wp-grid wp-grid-2">
        <Link href="/player/account" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>👤</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Détail du compte</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Nom, prénom, adresse, email.
            </p>
            <span className="wp-badge active">Gérer</span>
          </div>
        </Link>

        <Link href="/player/servers" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>⚔️</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Mes aventures</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Personnages, temps joué par serveur.
            </p>
            <span className="wp-badge active">Voir</span>
          </div>
        </Link>

        <Link href="/player/exploits" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>🏆</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Mes Exploits</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Progression, exploits secrets et taux de complétion.
            </p>
            <span className="wp-badge active">Voir mes exploits</span>
          </div>
        </Link>

        <Link href="/player/cgu" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>📜</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Mes CGU</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Historique des versions acceptées.
            </p>
            <span className="wp-badge active">Voir</span>
          </div>
        </Link>

        <Link href="/player/privacy" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>🔒</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Vie privée</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Visibilité du profil, historique CGU.
            </p>
            <span className="wp-badge active">Configurer</span>
          </div>
        </Link>

        <Link href="/player/security" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>🛡️</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Sécurité</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Mot de passe et double authentification.
            </p>
            <span className="wp-badge active">Gérer</span>
          </div>
        </Link>

        <Link href="/player/parental" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>👨‍👩‍👧</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Contrôle parental</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Validation parentale pour joueurs mineurs.
            </p>
            <span className="wp-badge active">Configurer</span>
          </div>
        </Link>

        <Link href="/bugs" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive">
            <div style={{ fontSize: 28, marginBottom: 8 }}>🐛</div>
            <h3 style={{ margin: "0 0 8px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Signaler un bug</h3>
            <p style={{ margin: "0 0 12px", fontSize: 14, color: "var(--ln-muted)" }}>
              Contribuez à l&apos;amélioration du jeu.
            </p>
            <span className="wp-badge active">Signaler</span>
          </div>
        </Link>
      </div>
    </div>
  );
}
