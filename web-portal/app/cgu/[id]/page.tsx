import Link from "next/link";
import { notFound } from "next/navigation";
import { query } from "@/lib/db";
import type { RowDataPacket } from "mysql2/promise";

type EditionRow = RowDataPacket & {
  id: number;
  version_label: string;
  published_at: string | null;
  status: string;
};

type LocalizationRow = RowDataPacket & {
  locale: string;
  title: string;
  content: string;
};

interface PageProps {
  params: { id: string };
  searchParams: { lang?: string };
}

export default async function CguDetailPage({ params, searchParams }: PageProps) {
  const id = parseInt(params.id, 10);
  if (isNaN(id)) notFound();

  let edition: EditionRow | null = null;
  let localizations: LocalizationRow[] = [];

  try {
    const edRows = await query<EditionRow[]>(
      "SELECT id, version_label, published_at, status FROM terms_editions WHERE id = ? AND status = 'published' LIMIT 1",
      [id]
    );
    edition = edRows[0] ?? null;
    if (!edition) notFound();

    localizations = await query<LocalizationRow[]>(
      "SELECT locale, title, content FROM terms_localizations WHERE edition_id = ? ORDER BY locale ASC",
      [id]
    );
  } catch {
    notFound();
  }

  const lang = searchParams.lang ?? "fr";
  const loc = localizations.find((l) => l.locale === lang) ?? localizations[0];
  const otherLangs = localizations.filter((l) => l.locale !== lang);

  return (
    <div className="wp-main narrow">
      <div className="wp-page-header">
        <h1>{loc?.title ?? edition!.version_label}</h1>
        <p style={{ display: "flex", gap: 16, alignItems: "center", flexWrap: "wrap" }}>
          <span style={{ fontFamily: "var(--font-display)", fontSize: 11, letterSpacing: ".14em", color: "var(--ln-muted)" }}>
            Version {edition!.version_label}
          </span>
          {edition!.published_at && (
            <span style={{ fontSize: 12, color: "var(--ln-muted)" }}>
              Publiée le{" "}
              {new Date(edition!.published_at).toLocaleDateString("fr-FR", {
                day: "2-digit", month: "long", year: "numeric",
              })}
            </span>
          )}
          {otherLangs.map((l) => (
            <Link
              key={l.locale}
              href={`/cgu/${id}?lang=${l.locale}`}
              style={{ fontSize: 11, color: "var(--ln-accent)", textDecoration: "none", letterSpacing: ".1em" }}
            >
              {l.locale.toUpperCase()}
            </Link>
          ))}
        </p>
      </div>

      <div className="wp-card" style={{ whiteSpace: "pre-wrap", lineHeight: 1.75, fontSize: 14.5, color: "var(--ln-text)" }}>
        {loc?.content ?? "Contenu indisponible."}
      </div>

      <div style={{ marginTop: 24, display: "flex", gap: 12 }}>
        <Link href="/player/privacy" className="btn btn-ghost">
          &larr; Vie privée
        </Link>
        <Link href="/player/cgu" className="btn btn-ghost">
          Mes CGU
        </Link>
      </div>
    </div>
  );
}
