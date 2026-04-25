import { describe, it, expect } from "vitest";
import { signSession, verifySession, readSession, type SessionPayload } from "./session";

const TEST_SECRET = "test-secret-at-least-32-chars-long-for-hmac-sha256!!";

const SAMPLE: SessionPayload = {
  v: 1,
  accountId: 42,
  tagId: "TAG001",
  login: "joueur1",
  role: "player",
};

function withSecret<T>(fn: () => T): T {
  const prev = process.env.SESSION_HMAC_SECRET;
  process.env.SESSION_HMAC_SECRET = TEST_SECRET;
  try {
    return fn();
  } finally {
    process.env.SESSION_HMAC_SECRET = prev ?? "";
  }
}

describe("signSession", () => {
  it("produit une chaîne avec un séparateur point", () => {
    const val = withSecret(() => signSession(SAMPLE));
    expect(val.split(".")).toHaveLength(2);
  });

  it("lève une erreur si SESSION_HMAC_SECRET est absent", () => {
    const prev = process.env.SESSION_HMAC_SECRET;
    process.env.SESSION_HMAC_SECRET = "";
    expect(() => signSession(SAMPLE)).toThrow("SESSION_HMAC_SECRET");
    process.env.SESSION_HMAC_SECRET = prev ?? "";
  });
});

describe("verifySession", () => {
  it("retourne le payload pour un cookie valide", () => {
    const val = withSecret(() => signSession(SAMPLE));
    const result = withSecret(() => verifySession(val));
    expect(result).toEqual(SAMPLE);
  });

  it("retourne null si la signature est altérée", () => {
    const val = withSecret(() => signSession(SAMPLE));
    const tampered = val.slice(0, -4) + "0000";
    expect(withSecret(() => verifySession(tampered))).toBeNull();
  });

  it("retourne null si le payload est altéré (signature ne correspond plus)", () => {
    const val = withSecret(() => signSession(SAMPLE));
    const [, sig] = val.split(".");
    const fakePayload = Buffer.from(
      JSON.stringify({ ...SAMPLE, role: "admin" })
    ).toString("base64url");
    expect(withSecret(() => verifySession(`${fakePayload}.${sig}`))).toBeNull();
  });

  it("retourne null si le cookie n'a pas de point", () => {
    expect(withSecret(() => verifySession("noseparator"))).toBeNull();
  });

  it("retourne null si SESSION_HMAC_SECRET est absent", () => {
    const prev = process.env.SESSION_HMAC_SECRET;
    process.env.SESSION_HMAC_SECRET = "";
    expect(verifySession("anything.here")).toBeNull();
    process.env.SESSION_HMAC_SECRET = prev ?? "";
  });

  it("retourne null si v !== 1", () => {
    const badPayload: unknown = { v: 2, accountId: 1, tagId: "", login: "x", role: "player" };
    const val = withSecret(() => signSession(badPayload as SessionPayload));
    expect(withSecret(() => verifySession(val))).toBeNull();
  });
});

describe("readSession", () => {
  it("retourne null si le cookie est absent", () => {
    const store = { get: (_: string) => undefined };
    expect(withSecret(() => readSession(store))).toBeNull();
  });

  it("retourne le payload pour un cookie valide", () => {
    const cookieVal = withSecret(() => signSession(SAMPLE));
    const store = { get: (_: string) => ({ value: cookieVal }) };
    expect(withSecret(() => readSession(store))).toEqual(SAMPLE);
  });
});
