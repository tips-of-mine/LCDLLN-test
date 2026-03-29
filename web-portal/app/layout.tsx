import type { Metadata } from "next";
import "./globals.css";
import { SiteHeader } from "@/components/SiteHeader";

export const metadata: Metadata = {
  title: "LCDLLN — Portail",
  description: "Portail joueur et administration — LCDLLN",
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="fr">
      <body>
        <SiteHeader />
        <main>{children}</main>
      </body>
    </html>
  );
}
