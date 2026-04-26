// GET /api/player/parental/validate?token=xxx
import { NextResponse } from 'next/server'
import { query } from '@/lib/db'
import type { RowDataPacket } from 'mysql2/promise'

export async function GET(request: Request) {
  const url = new URL(request.url)
  const token = url.searchParams.get('token')
  if (!token) return NextResponse.redirect(new URL('/?parental=token_manquant', request.url))

  try {
    const rows = await query<Array<RowDataPacket & { id: number; parental_token_expires_at: string }>>(
      'SELECT id, parental_token_expires_at FROM accounts WHERE parental_token = ? LIMIT 1',
      [token]
    )
    const account = rows[0]
    if (!account) return NextResponse.redirect(new URL('/?parental=token_invalide', request.url))
    if (new Date(account.parental_token_expires_at) < new Date()) {
      return NextResponse.redirect(new URL('/?parental=token_expire', request.url))
    }

    await query(
      'UPDATE accounts SET parental_validated = 1, parental_token = NULL, parental_token_expires_at = NULL WHERE id = ?',
      [account.id]
    )

    return NextResponse.redirect(new URL('/?parental=valide', request.url))
  } catch {
    return NextResponse.redirect(new URL('/?parental=erreur', request.url))
  }
}
