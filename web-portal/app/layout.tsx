import type { Metadata } from "next";
import "./globals.css";
import { SiteHeader } from "@/components/SiteHeader";

export const metadata: Metadata = {
  title: "Les Chroniques De La Lune Noire — Portail",
  description: "Portail joueur et administration — Les Chroniques De La Lune Noire",
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="fr">
      <body>
        <SiteHeader />
        <main>{children}</main>
        <footer className="wp-footer">
          <span style={{ fontFamily: "var(--font-display)", fontSize: 11, letterSpacing: ".24em", textTransform: "uppercase" }}>
            © 2026 · Les Chroniques de la Lune Noire
          </span>
          <div className="wp-footer-links">
            <a href="/support">Support</a>
            <a href="/bugs">Signaler un bug</a>
            <a href="/contact">Contact</a>
          </div>
        </footer>
      </body>
    </html>
  );
}
