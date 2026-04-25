import { NextRequest, NextResponse } from "next/server";

const COOKIE_NAME = "lcdlln_session";
const ADMIN_ROLES = new Set(["admin", "moderator"]);

function parsePayload(cookieVal: string): { role?: string } | null {
  try {
    const dotIdx = cookieVal.lastIndexOf(".");
    if (dotIdx === -1) return null;
    const b64 = cookieVal
      .slice(0, dotIdx)
      .replace(/-/g, "+")
      .replace(/_/g, "/");
    const padded = b64.padEnd(b64.length + ((4 - (b64.length % 4)) % 4), "=");
    return JSON.parse(atob(padded)) as { role?: string };
  } catch {
    return null;
  }
}

export function middleware(request: NextRequest) {
  const { pathname } = request.nextUrl;
  const cookieVal = request.cookies.get(COOKIE_NAME)?.value;
  const payload = cookieVal ? parsePayload(cookieVal) : null;

  if (pathname.startsWith("/admin")) {
    if (!payload) {
      const next = encodeURIComponent(pathname + request.nextUrl.search);
      return NextResponse.redirect(new URL(`/login?next=${next}`, request.url));
    }
    if (!ADMIN_ROLES.has(payload.role ?? "")) {
      return NextResponse.redirect(new URL("/", request.url));
    }
  }

  if (pathname.startsWith("/player")) {
    if (!payload) {
      const next = encodeURIComponent(pathname + request.nextUrl.search);
      return NextResponse.redirect(new URL(`/login?next=${next}`, request.url));
    }
  }

  return NextResponse.next();
}

export const config = {
  matcher: ["/player/:path*", "/admin/:path*"],
};
