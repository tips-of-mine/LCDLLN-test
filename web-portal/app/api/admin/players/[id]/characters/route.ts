import { NextResponse } from 'next/server'
import { isAuthenticatedAdmin } from '@/lib/apiAuth'
import { query } from '@/lib/db'
import type { RowDataPacket } from 'mysql2/promise'

export async function GET(_req: Request, { params }: { params: { id: string } }) {
  if (!await isAuthenticatedAdmin()) return NextResponse.json({ ok: false }, { status: 403 })
  const id = parseInt(params.id, 10)
  if (isNaN(id)) return NextResponse.json({ ok: false }, { status: 400 })

  try {
    const characters = await query<Array<RowDataPacket & {
      id: number; name: string; slot: number; deleted_at: string | null; force_rename: number
    }>>(
      'SELECT id, name, slot, deleted_at, force_rename FROM characters WHERE account_id = ? AND deleted_at IS NULL ORDER BY slot ASC',
      [id]
    )
    return NextResponse.json({ ok: true, characters })
  } catch {
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}
