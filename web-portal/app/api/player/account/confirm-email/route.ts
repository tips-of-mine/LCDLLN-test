// GET /api/player/account/confirm-email?token=xxx
import { NextResponse } from 'next/server'
import { query } from '@/lib/db'
import type { RowDataPacket } from 'mysql2/promise'

export async function GET(request: Request) {
  const url = new URL(request.url)
  const token = url.searchParams.get('token')
  if (!token) return NextResponse.redirect(new URL('/player/account?error=token_manquant', request.url))

  try {
    const rows = await query<Array<RowDataPacket & { id: number; email_pending: string; email_pending_expires_at: string }>>(
      'SELECT id, email_pending, email_pending_expires_at FROM accounts WHERE email_pending_token = ? LIMIT 1',
      [token]
    )
    const account = rows[0]
    if (!account) return NextResponse.redirect(new URL('/player/account?error=token_invalide', request.url))
    if (new Date(account.email_pending_expires_at) < new Date()) {
      return NextResponse.redirect(new URL('/player/account?error=token_expire', request.url))
    }

    await query(
      'UPDATE accounts SET email = ?, email_pending = NULL, email_pending_token = NULL, email_pending_expires_at = NULL WHERE id = ?',
      [account.email_pending, account.id]
    )

    return NextResponse.redirect(new URL('/player/account?success=email_confirme', request.url))
  } catch {
    return NextResponse.redirect(new URL('/player/account?error=erreur_serveur', request.url))
  }
}
