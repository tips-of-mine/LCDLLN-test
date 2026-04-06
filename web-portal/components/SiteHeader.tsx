"use client";

import Link from "next/link";
import { useState } from "react";

export function SiteHeader() {
  const [open, setOpen] = useState(false);

  return (
    <header className="site">
      <Link href="/" className="logo">
        <span className="logo-dot" />
        Les Chroniques De La Lune Noire
      </Link>

      <button
        className="mobile-toggle"
        onClick={() => setOpen((v) => !v)}
        aria-label="Menu"
        aria-expanded={open}
      >
        {open ? "\u2715" : "\u2630"}
      </button>

      <nav className={open ? "open" : ""} onClick={() => setOpen(false)}>
        <Link href="/roadmap">Roadmap</Link>
        <Link href="/support">Support</Link>
        <Link href="/bugs">Signaler un bug</Link>
        <Link href="/contact">Contact</Link>
        <Link href="/player">Espace joueur</Link>
        <Link href="/admin">Admin</Link>
        <Link href="/login" className="nav-cta">Connexion</Link>
      </nav>
    </header>
  );
}
