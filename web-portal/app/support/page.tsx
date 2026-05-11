import Link from "next/link";
import { query } from "@/lib/db/connection";
import type { RowDataPacket } from "mysql2/promise";
import FaqAccordion from "./FaqAccordion";

export default async function SupportPage() {
  let items: Array<{ id: number; question: string; answer: string; category: string | null }> = [];
  try {
    items = await query<
      Array<
        RowDataPacket & {
          id: number;
          question: string;
          answer: string;
          category: string | null;
        }
      >
    >(
      "SELECT id, question, answer, category FROM faq_items WHERE published = 1 ORDER BY display_order ASC, id ASC"
    );
  } catch {
    // DB not available — show empty state
  }

  return (
    <div className="wp-main">
      <div className="wp-page-header">
        <h1>Support &amp; FAQ</h1>
        <p>
          Trouvez des réponses à vos questions ci-dessous. Si votre problème
          persiste, n&apos;hésitez pas à nous contacter.
        </p>
      </div>

      <div className="wp-grid wp-grid-3">
        <Link href="/password-recovery" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive" style={{ textAlign: "center", padding: "1.5rem 1rem" }}>
            <div style={{ fontSize: "1.5rem", marginBottom: "0.5rem" }}>🔒</div>
            <div style={{ fontWeight: 600, color: "var(--ln-text)", fontSize: "0.9rem", fontFamily: "var(--font-display)" }}>Mot de passe oublié</div>
            <p style={{ fontSize: 12, margin: "4px 0 0", color: "var(--ln-muted)" }}>Récupérer l&apos;accès à votre compte</p>
          </div>
        </Link>
        <Link href="/bugs" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive" style={{ textAlign: "center", padding: "1.5rem 1rem" }}>
            <div style={{ fontSize: "1.5rem", marginBottom: "0.5rem" }}>🐛</div>
            <div style={{ fontWeight: 600, color: "var(--ln-text)", fontSize: "0.9rem", fontFamily: "var(--font-display)" }}>Signaler un bug</div>
            <p style={{ fontSize: 12, margin: "4px 0 0", color: "var(--ln-muted)" }}>Aidez-nous à améliorer le jeu</p>
          </div>
        </Link>
        <Link href="/contact" style={{ textDecoration: "none" }}>
          <div className="wp-card interactive" style={{ textAlign: "center", padding: "1.5rem 1rem" }}>
            <div style={{ fontSize: "1.5rem", marginBottom: "0.5rem" }}>✉️</div>
            <div style={{ fontWeight: 600, color: "var(--ln-text)", fontSize: "0.9rem", fontFamily: "var(--font-display)" }}>Contacter l&apos;équipe</div>
            <p style={{ fontSize: 12, margin: "4px 0 0", color: "var(--ln-muted)" }}>Nous sommes là pour vous</p>
          </div>
        </Link>
      </div>

      <div className="wp-section-title" style={{ marginTop: "2rem" }}>Questions fréquentes</div>
      <div className="wp-section-sub">Cliquez sur une question pour afficher la réponse.</div>

      {items.length === 0 ? (
        <div className="wp-card" style={{ color: "var(--ln-muted)", textAlign: "center", padding: "2rem" }}>
          La FAQ sera disponible prochainement.
        </div>
      ) : (
        <FaqAccordion items={items} />
      )}
    </div>
  );
}
