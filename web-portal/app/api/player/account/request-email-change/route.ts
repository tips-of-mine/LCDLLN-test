// POST /api/player/account/request-email-change
// Body: { newEmail: string }
import { NextResponse } from 'next/server'
import { getSession } from '@/lib/auth/session'
import { query } from '@/lib/db/connection'
import { sendEmailChange } from '@/lib/email/sender'
import { requireEnv } from '@/lib/env'
import { randomBytes } from 'node:crypto'
import type { RowDataPacket } from 'mysql2/promise'
import { logError } from '@/lib/log'

export async function POST(request: Request) {
  const session = await getSession()
  if (!session) return NextResponse.json({ ok: false }, { status: 401 })
  const accountId = session.accountId

  try {
    const body = await request.json() as { newEmail?: string }
    const newEmail = body.newEmail?.trim().toLowerCase()
    if (!newEmail || !newEmail.includes('@')) {
      return NextResponse.json({ ok: false, message: 'Adresse email invalide' }, { status: 400 })
    }

    const rows = await query<Array<RowDataPacket & { login: string; email: string }>>(
      'SELECT login, email FROM accounts WHERE id = ? LIMIT 1', [accountId]
    )
    if (!rows[0]) return NextResponse.json({ ok: false }, { status: 404 })

    // Check email not already taken
    const existing = await query<RowDataPacket[]>(
      'SELECT id FROM accounts WHERE LOWER(email) = ? AND id != ? LIMIT 1', [newEmail, accountId]
    )
    if ((existing as RowDataPacket[]).length > 0) {
      return NextResponse.json({ ok: false, message: 'Cette adresse email est déjà utilisée' }, { status: 409 })
    }

    const token = randomBytes(64).toString('hex')
    const expires = new Date(Date.now() + 48 * 60 * 60 * 1000)

    await query(
      'UPDATE accounts SET email_pending = ?, email_pending_token = ?, email_pending_expires_at = ? WHERE id = ?',
      [newEmail, token, expires, accountId]
    )

    const baseUrl = requireEnv("PORTAL_URL")
    const confirmationLink = `${baseUrl}/api/player/account/confirm-email?token=${token}`
    await sendEmailChange(newEmail, rows[0].login, newEmail, confirmationLink)

    return NextResponse.json({ ok: true })
  } catch (err) {
    logError('POST /api/player/account/request-email-change', 'Email change request failed', { err })
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}
