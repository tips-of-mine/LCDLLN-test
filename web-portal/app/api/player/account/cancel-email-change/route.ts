// POST /api/player/account/cancel-email-change
import { NextResponse } from 'next/server'
import { query } from '@/lib/db'
import { getAuthenticatedAccountId } from '@/lib/apiAuth'

export async function POST() {
  const accountId = await getAuthenticatedAccountId()
  if (!accountId) return NextResponse.json({ ok: false }, { status: 401 })

  try {
    await query(
      'UPDATE accounts SET email_pending = NULL, email_pending_token = NULL, email_pending_expires_at = NULL WHERE id = ?',
      [accountId]
    )
    return NextResponse.json({ ok: true })
  } catch {
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}
