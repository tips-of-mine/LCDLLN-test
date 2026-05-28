import { NextResponse } from 'next/server'
import { isStaff } from '@/lib/auth/roles'
import { cookies } from 'next/headers'
import { query } from '@/lib/db/connection'
import type { RowDataPacket } from 'mysql2/promise'
import { logError } from '@/lib/log'

async function checkAdmin() {
  const role = cookies().get('lcdlln_portal_role')?.value
  return isStaff(role)
}

export async function GET(_req: Request, { params }: { params: { id: string } }) {
  if (!await checkAdmin()) return NextResponse.json({ ok: false }, { status: 403 })
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
  } catch (err) {
    logError('GET /api/admin/players/[id]/characters', 'Fetch characters failed', { err })
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}
