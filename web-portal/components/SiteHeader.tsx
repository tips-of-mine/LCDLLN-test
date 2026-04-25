"use client";

import Link from "next/link";
import { useState } from "react";

export function SiteHeader() {
  const [open, setOpen] = useState(false);

  return (
    <header className="wp-header">
      <Link href="/" className="wp-logo" onClick={() => setOpen(false)}>
        <div className="wp-logo-moon" />
        <span className="wp-logo-text">Les Chroniques de la Lune Noire</span>
      </Link>

      <button
        className="wp-nav-toggle"
        onClick={() => setOpen((v) => !v)}
        aria-label="Menu"
        aria-expanded={open}
      >
        {open ? "✕" : "☰"}
      </button>

      <nav className={`wp-nav${open ? " open" : ""}`} onClick={() => setOpen(false)}>
        <Link href="/roadmap">Roadmap</Link>
        <Link href="/support">Support</Link>
        <Link href="/bugs">Signaler un bug</Link>
        <Link href="/player">Espace joueur</Link>
        <Link href="/admin">Admin</Link>
        <Link href="/login" className="cta">Connexion</Link>
      </nav>
    </header>
  );
}
