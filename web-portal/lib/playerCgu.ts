import type { RowDataPacket } from "mysql2/promise";
import { query } from "@/lib/db";

type CguRow = RowDataPacket & {
  edition_id: number;
  version_label: string;
  published_at: string;
  status: "draft" | "published" | "retired";
  title: string | null;
  accepted_at: string | null;
};

export type CguAcceptance = {
  editionId: number;
  versionLabel: string;
  publishedAt: string;
  status: "draft" | "published" | "retired";
  title: string;
  acceptedAt: string | null;
  accepted: boolean;
};

export async function getCguAcceptances(accountId: number): Promise<CguAcceptance[]> {
  const rows = await query<CguRow[]>(
    `SELECT
       te.id            AS edition_id,
       te.version_label,
       te.published_at,
       te.status,
       COALESCE(tl.title, te.version_label) AS title,
       ata.accepted_at
     FROM terms_editions te
     LEFT JOIN terms_localizations tl
       ON tl.edition_id = te.id AND tl.locale = 'fr'
     LEFT JOIN account_terms_acceptances ata
       ON ata.edition_id = te.id AND ata.account_id = ?
     WHERE te.status IN ('published', 'retired')
     ORDER BY te.published_at DESC`,
    [accountId],
  );

  return rows.map((r) => ({
    editionId: r.edition_id,
    versionLabel: r.version_label,
    publishedAt: r.published_at,
    status: r.status,
    title: r.title ?? r.version_label,
    acceptedAt: r.accepted_at ?? null,
    accepted: r.accepted_at !== null,
  }));
}
