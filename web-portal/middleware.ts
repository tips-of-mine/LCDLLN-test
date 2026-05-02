import { NextResponse } from 'next/server'
import type { NextRequest } from 'next/server'
import { verifyCookieValue } from '@/lib/cookieSigning'

export async function middleware(request: NextRequest) {
  const accountId = await verifyCookieValue(request.cookies.get('lcdlln_portal_account')?.value)
  const role = await verifyCookieValue(request.cookies.get('lcdlln_portal_role')?.value)
  const { pathname } = request.nextUrl

  if (pathname.startsWith('/player')) {
    if (!accountId) {
      const loginUrl = new URL('/login', request.url)
      loginUrl.searchParams.set('redirect', pathname)
      return NextResponse.redirect(loginUrl)
    }
  }

  if (pathname.startsWith('/admin')) {
    if (!accountId) {
      const loginUrl = new URL('/login', request.url)
      loginUrl.searchParams.set('redirect', pathname)
      return NextResponse.redirect(loginUrl)
    }
    if (role !== 'admin') {
      return NextResponse.redirect(new URL('/', request.url))
    }
  }

  return NextResponse.next()
}

export const config = {
  matcher: ['/player', '/player/:path*', '/admin', '/admin/:path*'],
}
