import { NextResponse } from 'next/server'
import type { NextRequest } from 'next/server'
import { isStaff } from '@/lib/auth/roles'

export function middleware(request: NextRequest) {
  const accountId = request.cookies.get('lcdlln_portal_account')?.value
  const role = request.cookies.get('lcdlln_portal_role')?.value
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
    if (!isStaff(role)) {
      return NextResponse.redirect(new URL('/', request.url))
    }
  }

  return NextResponse.next()
}

export const config = {
  matcher: ['/player', '/player/:path*', '/admin', '/admin/:path*'],
}
