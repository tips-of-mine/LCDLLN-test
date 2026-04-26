import Link from "next/link";
import { query } from "@/lib/db";
import { CguManager } from "@/components/admin/CguManager";
import type { RowDataPacket } from "mysql2/promise";

type EditionRow = RowDataPacket & {
  id: number;
  version_label: string;
  published_at: string | null;
  status: "draft" | "published" | "retired";
  retired_reason: string | null;
  title_fr: string | null;
  acceptance_count: number;
};

async function getEditions(): Promise<EditionRow[]> {
  try {
    return await query<EditionRow[]>(
      `SELECT te.id, te.version_label, te.published_at, te.status, te.retired_reason,
              tl_fr.title as title_fr,
              COUNT(DISTINCT ata.account_id) as acceptance_count
       FROM terms_editions te
       LEFT JOIN terms_localizations tl_fr ON tl_fr.edition_id = te.id AND tl_fr.locale = 'fr'
       LEFT JOIN account_terms_acceptances ata ON ata.edition_id = te.id
       GROUP BY te.id
       ORDER BY te.id DESC`
    );
  } catch {
    return [];
  }
}

export default async function AdminCguPage() {
  const editions = await getEditions();

  return (
    <>
      <div className="page-header">
        <h1>Gestion des CGU</h1>
        <p>
          Création, édition et publication des conditions générales d&apos;utilisation.
          Multilingue et versionné — le jeu n&apos;a pas besoin d&apos;être recompilé pour publier
          une nouvelle version.
        </p>
      </div>

      <CguManager editions={editions} />

      {/* Workflow explanation */}
      <h2>Workflow de publication</h2>
      <div className="card-grid-3">
        <div className="card" style={{ margin: 0 }}>
          <div className="flex items-center gap-1 mb-1">
            <span className="badge badge-muted">1</span>
            <strong className="text-sm">Brouillon</strong>
          </div>
          <p className="text-sm mb-0">
            Rédigez le contenu multilingue. Non visible par les joueurs.
            Modifiable et supprimable librement.
          </p>
        </div>
        <div className="card" style={{ margin: 0 }}>
          <div className="flex items-center gap-1 mb-1">
            <span className="badge badge-warning">2</span>
            <strong className="text-sm">Publié</strong>
          </div>
          <p className="text-sm mb-0">
            Les joueurs sont invités à accepter cette version.
            Non modifiable — retrait avec motif obligatoire.
          </p>
        </div>
        <div className="card" style={{ margin: 0 }}>
          <div className="flex items-center gap-1 mb-1">
            <span className="badge badge-error">3</span>
            <strong className="text-sm">Retiré</strong>
          </div>
          <p className="text-sm mb-0">
            Version archivée, remplacée par une édition plus récente.
            En lecture seule, motif conservé.
          </p>
        </div>
      </div>

      <div className="mt-2">
        <Link href="/admin" className="btn btn-ghost">&larr; Retour à l&apos;administration</Link>
      </div>
    </>
  );
}
