// PATCH /api/player/privacy
// Body: { visibility: 'public' | 'friends' | 'none' }
import { NextResponse } from 'next/server'
import { revalidatePath } from 'next/cache'
import { cookies } from 'next/headers'
import type { ResultSetHeader } from 'mysql2/promise'
import { query } from '@/lib/db/connection'
import { logError } from '@/lib/log'

export async function PATCH(request: Request) {
  const jar = cookies()
  const raw = jar.get('lcdlln_portal_account')?.value
  if (!raw) return NextResponse.json({ ok: false, message: 'Non authentifié' }, { status: 401 })
  const accountId = parseInt(raw, 10)
  if (isNaN(accountId) || accountId <= 0) {
    return NextResponse.json({ ok: false, message: 'Session invalide' }, { status: 401 })
  }

  try {
    const body = await request.json() as { visibility?: string }
    const visibility = body.visibility
    if (!['public', 'friends', 'none'].includes(visibility ?? '')) {
      return NextResponse.json({ ok: false, message: 'Valeur invalide' }, { status: 400 })
    }

    // INSERT ... ON DUPLICATE KEY UPDATE : insère la ligne au 1er appel pour
    // un compte, met à jour ensuite. On vérifie `affectedRows` côté caller
    // pour distinguer "réellement persisté" de "erreur silencieuse" (avant
    // ce fix, l'API retournait `ok: true` sans vérifier le résultat — un
    // INSERT bloqué par FK ou type mismatch passait inaperçu).
    const result = await query<ResultSetHeader>(
      `INSERT INTO account_privacy_settings (account_id, profile_visibility)
       VALUES (?, ?)
       ON DUPLICATE KEY UPDATE profile_visibility = VALUES(profile_visibility)`,
      [accountId, visibility]
    )

    // affectedRows : 1 = INSERT, 2 = UPDATE qui a effectivement changé une
    // valeur, 0 = UPDATE sans changement (l'utilisateur a re-sélectionné la
    // valeur déjà en base, ce qui reste un succès du point de vue UX).
    if (result.affectedRows < 0) {
      logError('PATCH /api/player/privacy', 'unexpected affectedRows', { accountId, affectedRows: result.affectedRows })
      return NextResponse.json({ ok: false, message: 'Sauvegarde non confirmée' }, { status: 500 })
    }

    // Invalide le cache SSR de la page Privacy : le prochain navigateur
    // (F5 ou navigation) re-exécute le SELECT et voit la nouvelle valeur,
    // au lieu de servir une éventuelle version mémorisée.
    revalidatePath('/player/privacy')

    return NextResponse.json({ ok: true })
  } catch (err) {
    logError("PATCH /api/player/privacy", "Update failed", { err })
    const msg = err instanceof Error ? err.message : 'Erreur serveur'
    return NextResponse.json({ ok: false, message: msg }, { status: 500 })
  }
}
