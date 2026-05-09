// PATCH /api/player/characters/[id]/delete
import { NextResponse } from 'next/server'
import { cookies } from 'next/headers'
import { query } from '@/lib/db/connection'
import type { RowDataPacket } from 'mysql2/promise'

export async function PATCH(
  _request: Request,
  { params }: { params: { id: string } }
) {
  const jar = cookies()
  const raw = jar.get('lcdlln_portal_account')?.value
  if (!raw) return NextResponse.json({ ok: false }, { status: 401 })
  const accountId = parseInt(raw, 10)
  const characterId = parseInt(params.id, 10)
  if (isNaN(accountId) || isNaN(characterId)) return NextResponse.json({ ok: false }, { status: 400 })

  try {
    // Verify ownership
    const rows = await query<Array<RowDataPacket & { id: number }>>(
      'SELECT id FROM characters WHERE id = ? AND account_id = ? AND deleted_at IS NULL LIMIT 1',
      [characterId, accountId]
    )
    if (!rows[0]) return NextResponse.json({ ok: false, message: 'Personnage introuvable' }, { status: 404 })

    await query('UPDATE characters SET deleted_at = NOW() WHERE id = ?', [characterId])
    return NextResponse.json({ ok: true })
  } catch {
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}
