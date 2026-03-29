import Link from "next/link";

export function SiteHeader() {
  return (
    <header className="site">
      <Link href="/" style={{ fontWeight: 700, color: "var(--fg)" }}>
        LCDLLN
      </Link>
      <nav>
        <Link href="/roadmap">Roadmap</Link>
        <Link href="/contact">Contact</Link>
        <Link href="/support">Support</Link>
        <Link href="/bugs">Signaler un bug</Link>
        <Link href="/login">Connexion</Link>
        <Link href="/player">Espace joueur</Link>
        <Link href="/admin">Admin</Link>
      </nav>
    </header>
  );
}
