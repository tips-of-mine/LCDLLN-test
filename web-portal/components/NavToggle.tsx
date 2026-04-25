"use client";

import { useState } from "react";

export function NavToggle({ children }: { children: React.ReactNode }) {
  const [open, setOpen] = useState(false);
  return (
    <>
      <button
        className="wp-nav-toggle"
        onClick={() => setOpen((v) => !v)}
        aria-label="Menu"
        aria-expanded={open}
      >
        {open ? "✕" : "☰"}
      </button>
      <nav
        className={`wp-nav${open ? " open" : ""}`}
        onClick={() => setOpen(false)}
      >
        {children}
      </nav>
    </>
  );
}
