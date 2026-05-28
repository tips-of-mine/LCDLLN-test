// POST /api/player/account/cancel-email-change
import { NextResponse } from 'next/server'
import { getSession } from '@/lib/auth/session'
import { query } from '@/lib/db/connection'
import { logError } from '@/lib/log'

export async function POST() {
  const session = await getSession()
  if (!session) return NextResponse.json({ ok: false }, { status: 401 })
  const accountId = session.accountId

  try {
    await query(
      'UPDATE accounts SET email_pending = NULL, email_pending_token = NULL, email_pending_expires_at = NULL WHERE id = ?',
      [accountId]
    )
    return NextResponse.json({ ok: true })
  } catch (err) {
    logError('POST /api/player/account/cancel-email-change', 'Cancel email change failed', { err })
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}
