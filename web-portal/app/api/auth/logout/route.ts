import { NextResponse } from 'next/server'
import { cookies } from 'next/headers'
import { deleteSession, getRawSessionToken, SESSION_COOKIE_NAME } from '@/lib/auth/session'

export async function POST() {
  // DELETE côté serveur d'abord (révoque la session côté DB → un
  // attaquant qui aurait volé le cookie ne peut plus s'en servir, même
  // si le cookie est encore présent dans son navigateur). Puis efface
  // le cookie côté client.
  const token = getRawSessionToken()
  if (token) {
    await deleteSession(token).catch(() => undefined)
  }
  const jar = cookies()
  jar.set(SESSION_COOKIE_NAME, '', { maxAge: 0, path: '/' })
  // Cleanup transitoire : si l'utilisateur a un ancien cookie résiduel
  // d'avant le refactor session-ID, on le purge aussi pour éviter
  // qu'il traîne avec sa fausse autorité (inoffensive maintenant que
  // plus rien ne le lit, mais on assainit quand même).
  jar.set('lcdlln_portal_account', '', { maxAge: 0, path: '/' })
  jar.set('lcdlln_portal_role', '', { maxAge: 0, path: '/' })
  return NextResponse.json({ ok: true })
}
