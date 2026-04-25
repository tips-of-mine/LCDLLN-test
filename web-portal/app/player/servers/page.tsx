import { cookies } from "next/headers";
import { redirect } from "next/navigation";
import Link from "next/link";
import { readSession } from "@/lib/session";
import { getCharactersWithStats } from "@/lib/playerCharacters";
import { CharactersSection } from "@/components/player/CharactersSection";

export const dynamic = "force-dynamic";

export default async function PlayerServersPage() {
  const session = readSession(cookies());
  if (!session) redirect("/login?next=/player/servers");

  const characters = await getCharactersWithStats(session.accountId);

  return (
    <div className="wp-main">
      <div className="wp-page-header">
        <h1>Mes aventures</h1>
        <p>Vos personnages et leur temps de jeu par serveur.</p>
      </div>

      {characters.length === 0 ? (
        <div className="wp-card" style={{ textAlign: "center", padding: "2rem" }}>
          <p style={{ color: "var(--ln-muted)", fontStyle: "italic" }}>
            Aucun personnage créé dans le jeu pour le moment.
          </p>
        </div>
      ) : (
        <CharactersSection initialCharacters={characters} />
      )}

      <div style={{ marginTop: "1.5rem" }}>
        <Link href="/player" className="btn btn-ghost">&larr; Espace joueur</Link>
      </div>
    </div>
  );
}
