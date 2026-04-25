// web-portal/lib/playerProfile.test.ts
import { describe, it, expect, vi, beforeEach } from "vitest";

vi.mock("@/lib/db", () => ({
  query: vi.fn(),
}));
vi.mock("@/lib/passwordRecovery", () => ({
  getRecoveryProfile: vi.fn(),
  upsertRecoveryProfile: vi.fn(),
}));

import { query } from "@/lib/db";
import { getRecoveryProfile, upsertRecoveryProfile } from "@/lib/passwordRecovery";
import { getAccountProfile, updateAccountProfile } from "./playerProfile";

const mockQuery = vi.mocked(query);
const mockGetRecovery = vi.mocked(getRecoveryProfile);
const mockUpsertRecovery = vi.mocked(upsertRecoveryProfile);

beforeEach(() => {
  vi.clearAllMocks();
});

describe("getAccountProfile", () => {
  it("retourne le profil quand le compte existe", async () => {
    mockQuery.mockResolvedValueOnce([
      {
        id: 1,
        login: "joueur1",
        email: "j@test.com",
        tag_id: "TAG001",
        first_name: "Jean",
        last_name: "Dupont",
        email_pending: null,
        email_verified: 1,
        profile_visibility: "public",
        parental_email: null,
        parental_consent_at: null,
        role: "player",
      },
    ]);
    mockGetRecovery.mockResolvedValueOnce({
      accountId: 1,
      birthDate: "1990-05-10",
      address: "1 rue de la Lune",
      city: "Paris",
      postalCode: "75001",
      secretQuestions: [],
    });
    const result = await getAccountProfile(1);
    expect(result).not.toBeNull();
    expect(result!.login).toBe("joueur1");
    expect(result!.firstName).toBe("Jean");
    expect(result!.address).toBe("1 rue de la Lune");
  });

  it("retourne null si le compte n'existe pas", async () => {
    mockQuery.mockResolvedValueOnce([]);
    const result = await getAccountProfile(99);
    expect(result).toBeNull();
  });
});

describe("updateAccountProfile", () => {
  it("met à jour les champs du compte et le profil de récupération", async () => {
    mockQuery.mockResolvedValueOnce({ affectedRows: 1 });
    mockUpsertRecovery.mockResolvedValueOnce({ ok: true });
    const result = await updateAccountProfile(1, {
      firstName: "Marie",
      lastName: "Martin",
      address: "2 av. Soleil",
      city: "Lyon",
      postalCode: "69001",
    });
    expect(result.ok).toBe(true);
    expect(mockQuery).toHaveBeenCalledTimes(1);
    expect(mockUpsertRecovery).toHaveBeenCalledTimes(1);
  });

  it("retourne une erreur si firstName est vide", async () => {
    const result = await updateAccountProfile(1, {
      firstName: "  ",
      lastName: "Martin",
      address: "",
      city: "",
      postalCode: "",
    });
    expect(result.ok).toBe(false);
    expect((result as { ok: false; message: string }).message).toContain("Prénom");
  });
});
