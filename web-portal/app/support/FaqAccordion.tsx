"use client";

import { useState } from "react";

type FaqItem = {
  id: number;
  question: string;
  answer: string;
  category: string | null;
};

function AccordionItem({ q, a }: { q: string; a: string }) {
  const [open, setOpen] = useState(false);

  return (
    <div className="wp-acc-item">
      <button
        className="wp-acc-trigger"
        onClick={() => setOpen((v) => !v)}
        aria-expanded={open}
      >
        <span>{q}</span>
        <span>{open ? "▲" : "▼"}</span>
      </button>
      {open && (
        <div className="wp-acc-body">
          {a}
        </div>
      )}
    </div>
  );
}

export default function FaqAccordion({ items }: { items: FaqItem[] }) {
  return (
    <div className="wp-accordion">
      {items.map((item) => (
        <AccordionItem key={item.id} q={item.question} a={item.answer} />
      ))}
    </div>
  );
}
