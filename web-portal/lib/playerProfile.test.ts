// web-portal/lib/playerProfile.test.ts
import { describe, it, expect, vi, beforeEach } from "vitest";

vi.mock("@/lib/db", () => ({
  query: vi.fn(),
}));
vi.mock("@/lib/passwordRecovery", () => ({
  getRecoveryProfile: vi.fn(),
  upsertRecoveryProfile: vi.fn(),
}));
vi.mock("node:crypto", async (importOriginal) => {
  const real = await importOriginal<typeof import("node:crypto")>();
  return { ...real, randomInt: vi.fn(() => 500000) };
});

import { query } from "@/lib/db";
import { getRecoveryProfile, upsertRecoveryProfile } from "@/lib/passwordRecovery";
import { getAccountProfile, updateAccountProfile, requestEmailChange, confirmEmailChange } from "./playerProfile";

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

describe("requestEmailChange", () => {
  it("retourne une erreur si email invalide", async () => {
    const result = await requestEmailChange(1, "pas-un-email");
    expect(result.ok).toBe(false);
    expect((result as { ok: false; message: string }).message).toContain("email");
  });

  it("insère un token si email valide et non pris", async () => {
    // email not taken check
    mockQuery.mockResolvedValueOnce([{ cnt: 0 }]);
    // INSERT token
    mockQuery.mockResolvedValueOnce({ affectedRows: 1 });
    // UPDATE email_pending + email_verified=0
    mockQuery.mockResolvedValueOnce({ affectedRows: 1 });
    const result = await requestEmailChange(1, "nouveau@test.com");
    expect(result.ok).toBe(true);
  });

  it("retourne une erreur si email déjà utilisé", async () => {
    mockQuery.mockResolvedValueOnce([{ cnt: 1 }]);
    const result = await requestEmailChange(1, "pris@test.com");
    expect(result.ok).toBe(false);
  });
});

describe("confirmEmailChange", () => {
  it("retourne une erreur si code invalide", async () => {
    mockQuery.mockResolvedValueOnce([]); // token not found
    const result = await confirmEmailChange(1, "000000");
    expect(result.ok).toBe(false);
  });

  it("applique le changement d'email si code valide", async () => {
    mockQuery.mockResolvedValueOnce([
      { id: 5, new_email: "nouveau@test.com", used_at: null, expired: 0 },
    ]);
    mockQuery.mockResolvedValueOnce({ affectedRows: 1 }); // UPDATE token used_at (atomic gate)
    mockQuery.mockResolvedValueOnce({ affectedRows: 1 }); // UPDATE accounts
    const result = await confirmEmailChange(1, "123456");
    expect(result.ok).toBe(true);
  });

  it("retourne une erreur si token déjà utilisé", async () => {
    mockQuery.mockResolvedValueOnce([
      { id: 5, new_email: "nouveau@test.com", used_at: "2026-04-25 10:00:00", expired: 0 },
    ]);
    const result = await confirmEmailChange(1, "123456");
    expect(result.ok).toBe(false);
  });

  it("retourne une erreur si token expiré", async () => {
    mockQuery.mockResolvedValueOnce([
      { id: 5, new_email: "nouveau@test.com", used_at: null, expired: 1 },
    ]);
    const result = await confirmEmailChange(1, "123456");
    expect(result.ok).toBe(false);
  });

  it("retourne une erreur si code mal formaté (aucun appel DB)", async () => {
    const result = await confirmEmailChange(1, "ABCDEF");
    expect(result.ok).toBe(false);
    expect(mockQuery).not.toHaveBeenCalled();
  });
});
