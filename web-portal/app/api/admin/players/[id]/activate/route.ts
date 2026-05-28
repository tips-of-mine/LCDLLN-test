import { NextResponse } from 'next/server'
import { requireAdmin } from '@/lib/auth/admin'
import { query } from '@/lib/db/connection'
import { logError } from '@/lib/log'

export async function PATCH(_req: Request, { params }: { params: { id: string } }) {
  if (!(await requireAdmin())) return NextResponse.json({ ok: false }, { status: 403 })
  const id = parseInt(params.id, 10)
  if (isNaN(id)) return NextResponse.json({ ok: false }, { status: 400 })
  try {
    await query(
      "UPDATE accounts SET account_status = 'active', disabled_reason = NULL WHERE id = ?",
      [id]
    )
    return NextResponse.json({ ok: true })
  } catch (err) {
    logError('PATCH /api/admin/players/[id]/activate', 'Activate account failed', { err })
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}
