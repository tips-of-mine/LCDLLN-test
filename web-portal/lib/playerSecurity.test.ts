import { describe, it, expect, vi, beforeEach } from "vitest";

vi.mock("@/lib/db", () => ({ query: vi.fn() }));
vi.mock("@/lib/portalLogin", () => ({ verifyPortalCredentials: vi.fn() }));
vi.mock("@/lib/gamePasswordHash", () => ({ hashPasswordForGameMaster: vi.fn() }));

import { query } from "@/lib/db";
import { verifyPortalCredentials } from "@/lib/portalLogin";
import { hashPasswordForGameMaster } from "@/lib/gamePasswordHash";
import { changePassword } from "./playerSecurity";

const mockQuery = vi.mocked(query);
const mockVerify = vi.mocked(verifyPortalCredentials);
const mockHash = vi.mocked(hashPasswordForGameMaster);

beforeEach(() => vi.clearAllMocks());

describe("changePassword", () => {
  it("retourne une erreur si le mot de passe actuel est incorrect", async () => {
    mockQuery.mockResolvedValueOnce([{ login: "joueur1" }]);
    mockVerify.mockResolvedValueOnce({ ok: false, code: "invalid" });
    const result = await changePassword(1, "mauvais", "Nouveau1!");
    expect(result.ok).toBe(false);
    expect((result as { ok: false; message: string }).message).toContain("actuel");
  });

  it("retourne une erreur si le nouveau MDP est trop court", async () => {
    mockQuery.mockResolvedValueOnce([{ login: "joueur1", email: "j@t.com" }]);
    mockVerify.mockResolvedValueOnce({ ok: true, accountId: 1, login: "joueur1", tagId: "T", role: "player" });
    const result = await changePassword(1, "correct", "abc");
    expect(result.ok).toBe(false);
    expect((result as { ok: false; message: string }).message).toContain("8");
  });

  it("met à jour le hash si tout est valide", async () => {
    mockQuery.mockResolvedValueOnce([{ login: "joueur1", email: "j@t.com" }]);
    mockVerify.mockResolvedValueOnce({ ok: true, accountId: 1, login: "joueur1", tagId: "T", role: "player" });
    mockHash.mockResolvedValueOnce("$argon2id$...");
    mockQuery.mockResolvedValueOnce({ affectedRows: 1 });
    const result = await changePassword(1, "correct", "NouveauMdp1!");
    expect(result.ok).toBe(true);
    expect(mockHash).toHaveBeenCalledWith("joueur1", "NouveauMdp1!");
  });
});
