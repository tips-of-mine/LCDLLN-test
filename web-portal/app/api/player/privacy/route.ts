// PATCH /api/player/privacy
// Body: { visibility: 'public' | 'friends' | 'none' }
import { NextResponse } from 'next/server'
import { cookies } from 'next/headers'
import { query } from '@/lib/db/connection'

export async function PATCH(request: Request) {
  const jar = cookies()
  const raw = jar.get('lcdlln_portal_account')?.value
  if (!raw) return NextResponse.json({ ok: false }, { status: 401 })
  const accountId = parseInt(raw, 10)
  if (isNaN(accountId)) return NextResponse.json({ ok: false }, { status: 401 })

  try {
    const body = await request.json() as { visibility?: string }
    const visibility = body.visibility
    if (!['public', 'friends', 'none'].includes(visibility ?? '')) {
      return NextResponse.json({ ok: false, message: 'Valeur invalide' }, { status: 400 })
    }

    await query(
      `INSERT INTO account_privacy_settings (account_id, profile_visibility)
       VALUES (?, ?)
       ON DUPLICATE KEY UPDATE profile_visibility = VALUES(profile_visibility)`,
      [accountId, visibility]
    )

    return NextResponse.json({ ok: true })
  } catch (err) {
    console.error('[PATCH /api/player/privacy]', err)
    const msg = err instanceof Error ? err.message : 'Erreur serveur'
    return NextResponse.json({ ok: false, message: msg }, { status: 500 })
  }
}
