import { NextResponse } from 'next/server'
import type { NextRequest } from 'next/server'
import { SESSION_COOKIE_NAME } from '@/lib/auth/session'

// Le middleware vérifie uniquement la PRÉSENCE d'un cookie session_token.
// Il NE DOIT PAS faire de lookup DB ici (le middleware tourne sur Edge
// Runtime et un lookup MySQL serait coûteux + non disponible). La vraie
// vérification (token existe en DB + non expiré + rôle staff pour /admin)
// est faite dans chaque page/route via `getSession()` ou `requireAdmin()`.
//
// Conséquence : le middleware sert juste de filtre UX pour rediriger les
// utilisateurs anonymes vers /login. Un attaquant qui pose un cookie
// session_token bidon passe le middleware mais sera rejeté par
// `getSession()` (token introuvable en DB → null → page redirige ou route
// retourne 401/403). C'est de la défense-en-profondeur : middleware best-
// effort UX, page/route = vraie autorité.
//
// Note historique : avant ce refactor, le middleware vérifiait aussi le
// rôle via `isStaff(cookie.role)` pour /admin/*. Ce check trustait un
// cookie modifiable et donnait une fausse impression de sécurité. Il est
// retiré ici : la page admin elle-même fait le check rôle via
// `getSession()`.
export function middleware(request: NextRequest) {
  const hasSession = Boolean(request.cookies.get(SESSION_COOKIE_NAME)?.value)
  const { pathname } = request.nextUrl

  if (pathname.startsWith('/player') || pathname.startsWith('/admin')) {
    if (!hasSession) {
      const loginUrl = new URL('/login', request.url)
      loginUrl.searchParams.set('redirect', pathname)
      return NextResponse.redirect(loginUrl)
    }
  }

  return NextResponse.next()
}

export const config = {
  matcher: ['/player', '/player/:path*', '/admin', '/admin/:path*'],
}
