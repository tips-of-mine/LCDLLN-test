// POST /api/player/account/cancel-email-change
import { NextResponse } from 'next/server'
import { cookies } from 'next/headers'
import { query } from '@/lib/db'

export async function POST() {
  const jar = cookies()
  const raw = jar.get('lcdlln_portal_account')?.value
  if (!raw) return NextResponse.json({ ok: false }, { status: 401 })
  const accountId = parseInt(raw, 10)
  if (isNaN(accountId)) return NextResponse.json({ ok: false }, { status: 401 })

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
