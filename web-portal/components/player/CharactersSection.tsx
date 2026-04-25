"use client";

import { useState } from "react";
import type { CharacterWithStats } from "@/lib/playerCharacters";
import { CharacterCard } from "@/components/player/CharacterCard";

type Props = { initialCharacters: CharacterWithStats[] };

export function CharactersSection({ initialCharacters }: Props) {
  const [characters, setCharacters] = useState(initialCharacters);

  function handleDeleted(id: number) {
    setCharacters((prev) => prev.filter((c) => c.id !== id));
  }

  if (characters.length === 0) {
    return (
      <div className="wp-card" style={{ textAlign: "center", padding: "2rem" }}>
        <p style={{ color: "var(--ln-muted)", fontStyle: "italic" }}>Tous vos personnages ont été supprimés.</p>
      </div>
    );
  }

  return (
    <div>
      {characters.map((char) => (
        <CharacterCard key={char.id} character={char} onDeleted={handleDeleted} />
      ))}
    </div>
  );
}
