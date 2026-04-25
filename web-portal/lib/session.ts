import { createHmac, timingSafeEqual } from "node:crypto";
import { COOKIE_NAME, COOKIE_MAX_AGE_SEC } from "./session-constants";
export { COOKIE_NAME, COOKIE_MAX_AGE_SEC };

export type SessionPayload = {
  v: 1;
  accountId: number;
  tagId: string;
  login: string;
  role: "player" | "admin" | "moderator";
};

type CookieStore = { get(name: string): { value: string } | undefined };

function getSecret(): string {
  const s = process.env.SESSION_HMAC_SECRET;
  if (!s) throw new Error("SESSION_HMAC_SECRET non défini");
  return s;
}

function toBase64url(data: string): string {
  return Buffer.from(data, "utf8")
    .toString("base64")
    .replace(/\+/g, "-")
    .replace(/\//g, "_")
    .replace(/=/g, "");
}

function fromBase64url(s: string): string {
  const padded =
    s.replace(/-/g, "+").replace(/_/g, "/") +
    "=".repeat((4 - (s.length % 4)) % 4);
  return Buffer.from(padded, "base64").toString("utf8");
}

function hmacHex(payload: string, secret: string): string {
  return createHmac("sha256", secret).update(payload).digest("hex");
}

export function signSession(payload: SessionPayload): string {
  const secret = getSecret();
  const payloadB64 = toBase64url(JSON.stringify(payload));
  const sig = hmacHex(payloadB64, secret);
  return `${payloadB64}.${sig}`;
}

export function verifySession(cookieValue: string): SessionPayload | null {
  try {
    const secret = process.env.SESSION_HMAC_SECRET;
    if (!secret) return null;

    const dotIdx = cookieValue.lastIndexOf(".");
    if (dotIdx === -1) return null;

    const payloadB64 = cookieValue.slice(0, dotIdx);
    const sig = cookieValue.slice(dotIdx + 1);
    const expectedSig = hmacHex(payloadB64, secret);

    // Constant-time comparison (anti-timing attack)
    if (sig.length !== 64) return null;
    const sigBuf = Buffer.from(sig, "hex");
    const expectedBuf = Buffer.from(expectedSig, "hex");
    if (sigBuf.length !== 32 || expectedBuf.length !== 32) return null;
    if (!timingSafeEqual(sigBuf, expectedBuf)) return null;

    const parsed = JSON.parse(fromBase64url(payloadB64)) as SessionPayload;
    if (parsed.v !== 1) return null;
    return parsed;
  } catch {
    return null;
  }
}

export function readSession(cookieStore: CookieStore): SessionPayload | null {
  const val = cookieStore.get(COOKIE_NAME)?.value;
  if (!val) return null;
  return verifySession(val);
}
