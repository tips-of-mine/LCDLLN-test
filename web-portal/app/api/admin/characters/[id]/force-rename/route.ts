import { NextResponse } from 'next/server'
import { cookies } from 'next/headers'
import { query } from '@/lib/db'
import type { RowDataPacket } from 'mysql2/promise'

async function checkAdmin() {
  const role = cookies().get('lcdlln_portal_role')?.value
  return role === 'admin'
}

export async function PATCH(_req: Request, { params }: { params: { id: string } }) {
  if (!await checkAdmin()) return NextResponse.json({ ok: false }, { status: 403 })
  const id = parseInt(params.id, 10)
  if (isNaN(id)) return NextResponse.json({ ok: false }, { status: 400 })

  try {
    const rows = await query<Array<RowDataPacket & { id: number }>>(
      'SELECT id FROM characters WHERE id = ? AND deleted_at IS NULL LIMIT 1',
      [id]
    )
    if (!rows[0]) return NextResponse.json({ ok: false, message: 'Personnage introuvable' }, { status: 404 })

    await query('UPDATE characters SET force_rename = 1 WHERE id = ?', [id])
    return NextResponse.json({ ok: true })
  } catch {
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}
