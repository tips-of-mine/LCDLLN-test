// HMAC-SHA256 sur les valeurs de cookie sensibles (lcdlln_portal_account / role).
// Web Crypto API : utilisable à la fois en Edge Runtime (middleware Next.js) et en
// Node runtime (Server Components / route handlers).

function getAuthSecret(): string {
  return process.env.AUTH_SECRET || "lcdlln-dev-recovery-secret";
}

let cachedKey: CryptoKey | null = null;
let cachedKeySecret: string | null = null;

async function getKey(): Promise<CryptoKey> {
  const secret = getAuthSecret();
  if (cachedKey && cachedKeySecret === secret) return cachedKey;
  const enc = new TextEncoder();
  const key = await crypto.subtle.importKey(
    "raw",
    enc.encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign", "verify"],
  );
  cachedKey = key;
  cachedKeySecret = secret;
  return key;
}

function bytesToHex(bytes: ArrayBuffer): string {
  const view = new Uint8Array(bytes);
  let out = "";
  for (let i = 0; i < view.length; i += 1) {
    out += view[i].toString(16).padStart(2, "0");
  }
  return out;
}

function hexToBytes(hex: string): Uint8Array | null {
  if (hex.length % 2 !== 0) return null;
  const out = new Uint8Array(hex.length / 2);
  for (let i = 0; i < out.length; i += 1) {
    const byte = parseInt(hex.substring(i * 2, i * 2 + 2), 16);
    if (Number.isNaN(byte)) return null;
    out[i] = byte;
  }
  return out;
}

export async function signCookieValue(rawValue: string): Promise<string> {
  const key = await getKey();
  const enc = new TextEncoder();
  const sig = await crypto.subtle.sign("HMAC", key, enc.encode(rawValue));
  return `${rawValue}.${bytesToHex(sig)}`;
}

export async function verifyCookieValue(signedValue: string | undefined): Promise<string | null> {
  if (!signedValue) return null;
  const dot = signedValue.lastIndexOf(".");
  if (dot <= 0 || dot === signedValue.length - 1) return null;
  const rawValue = signedValue.substring(0, dot);
  const providedHex = signedValue.substring(dot + 1);
  if (!/^[a-f0-9]{64}$/i.test(providedHex)) return null;
  const providedBytes = hexToBytes(providedHex);
  if (!providedBytes) return null;
  const key = await getKey();
  const enc = new TextEncoder();
  const ok = await crypto.subtle.verify("HMAC", key, providedBytes, enc.encode(rawValue));
  return ok ? rawValue : null;
}
