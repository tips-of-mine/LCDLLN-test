// web-portal/lib/playerCharacters.test.ts
import { describe, it, expect, vi, beforeEach } from "vitest";

vi.mock("@/lib/db", () => ({ query: vi.fn() }));

import { query } from "@/lib/db";
import { getCharactersWithStats, deleteCharacter } from "./playerCharacters";

const mockQuery = vi.mocked(query);

beforeEach(() => vi.clearAllMocks());

describe("getCharactersWithStats", () => {
  it("retourne les personnages avec leurs stats", async () => {
    mockQuery.mockResolvedValueOnce([
      {
        id: 1,
        name: "Arkan",
        slot: 0,
        server_name: "Serveur Alpha",
        server_id: 10,
        total_play_seconds: 3600,
        last_seen: "2026-04-20 12:00:00",
        created_at: "2026-01-01 00:00:00",
      },
    ]);
    const result = await getCharactersWithStats(42);
    expect(result).toHaveLength(1);
    expect(result[0].name).toBe("Arkan");
    expect(result[0].totalPlaySeconds).toBe(3600);
  });

  it("retourne un tableau vide si pas de personnages", async () => {
    mockQuery.mockResolvedValueOnce([]);
    const result = await getCharactersWithStats(42);
    expect(result).toHaveLength(0);
  });
});

describe("deleteCharacter", () => {
  it("supprime le personnage si il appartient au compte", async () => {
    mockQuery.mockResolvedValueOnce({ affectedRows: 1 });
    const result = await deleteCharacter(1, 42);
    expect(result.ok).toBe(true);
  });

  it("retourne une erreur si personnage introuvable ou non-owned", async () => {
    mockQuery.mockResolvedValueOnce({ affectedRows: 0 });
    const result = await deleteCharacter(99, 42);
    expect(result.ok).toBe(false);
  });
});
