// POST /api/player/password
// Body: { currentPassword: string, newPassword: string }
import { NextResponse } from 'next/server'
import { cookies } from 'next/headers'
import { query } from '@/lib/db/connection'
import { verifyGameMasterPassword, hashPasswordForGameMaster } from '@/lib/auth/gamePasswordHash'
import type { RowDataPacket } from 'mysql2/promise'
import { logError } from '@/lib/log'

export async function POST(request: Request) {
  const jar = cookies()
  const raw = jar.get('lcdlln_portal_account')?.value
  if (!raw) return NextResponse.json({ ok: false }, { status: 401 })
  const accountId = parseInt(raw, 10)
  if (isNaN(accountId)) return NextResponse.json({ ok: false }, { status: 401 })

  try {
    const body = await request.json() as { currentPassword?: string; newPassword?: string }
    const { currentPassword, newPassword } = body
    if (!currentPassword || !newPassword) {
      return NextResponse.json({ ok: false, message: 'Champs requis manquants' }, { status: 400 })
    }
    if (newPassword.length < 8) {
      return NextResponse.json({ ok: false, message: 'Le nouveau mot de passe doit contenir au moins 8 caractères' }, { status: 400 })
    }

    const rows = await query<Array<RowDataPacket & { login: string; password_hash: string }>>(
      'SELECT login, password_hash FROM accounts WHERE id = ? LIMIT 1',
      [accountId]
    )
    if (!rows[0]) return NextResponse.json({ ok: false }, { status: 404 })

    const { login, password_hash } = rows[0]
    const valid = await verifyGameMasterPassword(login, currentPassword, password_hash)
    if (!valid) {
      return NextResponse.json({ ok: false, message: 'Mot de passe actuel incorrect' }, { status: 401 })
    }

    const newHash = await hashPasswordForGameMaster(login, newPassword)
    await query('UPDATE accounts SET password_hash = ? WHERE id = ?', [newHash, accountId])

    return NextResponse.json({ ok: true })
  } catch (err) {
    logError('POST /api/player/password', 'Password change failed', { err })
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}
