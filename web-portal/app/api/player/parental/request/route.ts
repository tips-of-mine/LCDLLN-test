// POST /api/player/parental/request
// Body: { parentalEmail: string }
import { NextResponse } from 'next/server'
import { cookies } from 'next/headers'
import { query } from '@/lib/db/connection'
import { sendParentalValidation } from '@/lib/email/sender'
import { randomBytes } from 'node:crypto'
import type { RowDataPacket } from 'mysql2/promise'

export async function POST(request: Request) {
  const jar = cookies()
  const raw = jar.get('lcdlln_portal_account')?.value
  if (!raw) return NextResponse.json({ ok: false }, { status: 401 })
  const accountId = parseInt(raw, 10)
  if (isNaN(accountId)) return NextResponse.json({ ok: false }, { status: 401 })

  try {
    const body = await request.json() as { parentalEmail?: string }
    const parentalEmail = body.parentalEmail?.trim().toLowerCase()
    if (!parentalEmail || !parentalEmail.includes('@')) {
      return NextResponse.json({ ok: false, message: 'Adresse email invalide' }, { status: 400 })
    }

    const rows = await query<Array<RowDataPacket & { login: string; birth_date: string }>>(
      'SELECT login, birth_date FROM accounts WHERE id = ? LIMIT 1', [accountId]
    )
    if (!rows[0]) return NextResponse.json({ ok: false }, { status: 404 })

    // Verify minor
    const birthDate = new Date(rows[0].birth_date)
    const age = Math.floor((Date.now() - birthDate.getTime()) / (365.25 * 24 * 60 * 60 * 1000))
    if (age >= 18) {
      return NextResponse.json({ ok: false, message: 'Ce compte n\'est pas mineur' }, { status: 403 })
    }

    const token = randomBytes(64).toString('hex')
    const expires = new Date(Date.now() + 7 * 24 * 60 * 60 * 1000) // 7 days

    await query(
      'UPDATE accounts SET parental_email = ?, parental_validated = 0, parental_token = ?, parental_token_expires_at = ? WHERE id = ?',
      [parentalEmail, token, expires, accountId]
    )

    const baseUrl = process.env.PORTAL_URL ?? 'http://localhost:3000'
    const validationLink = `${baseUrl}/api/player/parental/validate?token=${token}`
    await sendParentalValidation(parentalEmail, rows[0].login, validationLink)

    return NextResponse.json({ ok: true })
  } catch {
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}
