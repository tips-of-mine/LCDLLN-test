// In-memory rate limiter: sliding-window failure counter with exponential lockout.
// Buckets are keyed by IP (or any caller-provided string). Reset on success.
// Note: per-process state — in a multi-replica deployment, prefer a shared store.

type Bucket = {
  fails: number;
  lockedUntilMs: number;
  lastFailMs: number;
};

const buckets = new Map<string, Bucket>();

const FAIL_WINDOW_MS = 15 * 60 * 1000;
const MAX_FAILS_BEFORE_LOCK = 5;
const BASE_LOCK_MS = 30 * 1000;
const MAX_LOCK_MS = 30 * 60 * 1000;
const CLEANUP_INTERVAL_MS = 5 * 60 * 1000;

let lastCleanupMs = Date.now();

function cleanup(now: number): void {
  if (now - lastCleanupMs < CLEANUP_INTERVAL_MS) return;
  lastCleanupMs = now;
  for (const [key, bucket] of buckets.entries()) {
    const stale = now - bucket.lastFailMs > FAIL_WINDOW_MS && bucket.lockedUntilMs <= now;
    if (stale) buckets.delete(key);
  }
}

export type RateLimitResult =
  | { allowed: true }
  | { allowed: false; retryAfterMs: number };

export function checkRateLimit(key: string): RateLimitResult {
  const now = Date.now();
  cleanup(now);
  const bucket = buckets.get(key);
  if (!bucket) return { allowed: true };
  if (bucket.lockedUntilMs > now) {
    return { allowed: false, retryAfterMs: bucket.lockedUntilMs - now };
  }
  if (now - bucket.lastFailMs > FAIL_WINDOW_MS) {
    buckets.delete(key);
    return { allowed: true };
  }
  return { allowed: true };
}

export function recordFailure(key: string): void {
  const now = Date.now();
  const existing = buckets.get(key);
  const inWindow = existing && now - existing.lastFailMs <= FAIL_WINDOW_MS;
  const fails = inWindow ? existing!.fails + 1 : 1;
  let lockedUntilMs = 0;
  if (fails >= MAX_FAILS_BEFORE_LOCK) {
    const overflow = fails - MAX_FAILS_BEFORE_LOCK;
    const lock = Math.min(BASE_LOCK_MS * Math.pow(2, overflow), MAX_LOCK_MS);
    lockedUntilMs = now + lock;
  }
  buckets.set(key, { fails, lockedUntilMs, lastFailMs: now });
}

export function recordSuccess(key: string): void {
  buckets.delete(key);
}

export function getClientIp(request: Request): string {
  const xff = request.headers.get("x-forwarded-for");
  if (xff) {
    const first = xff.split(",")[0]?.trim();
    if (first) return first;
  }
  const real = request.headers.get("x-real-ip");
  if (real) return real.trim();
  return "unknown";
}
